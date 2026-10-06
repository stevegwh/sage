#include "engine/Colors.hpp"
#include "engine/components/EntityVisibility.hpp"
#include "engine/MathConstants.hpp"

#include "LightManager.hpp"

#include "Camera.hpp"
#include "components/Renderable.hpp"
#include "Light.hpp"
#include "raymath.h"
#include "rlgl.h"
#include "Settings.hpp"
#include "ShaderPaths.hpp"
#include "systems/RenderSystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace sage
{
    namespace
    {
        constexpr int SHADOW_MAP_SIZE = 512;
        constexpr float SHADOW_FAR_PLANE = 100.0f;
        // Material maps occupy slots 0..11; keep the sun in 14 and point maps in the remaining slots.
        constexpr std::array<int, 3> POINT_SHADOW_TEXTURE_SLOTS{12, 13, 15};
        constexpr std::array<const char*, 3> POINT_SHADOW_SAMPLERS{
            "pointShadowMap0", "pointShadowMap1", "pointShadowMap2"};
        constexpr int SUN_SHADOW_MAP_SIZE = 2048;
        constexpr float SUN_SHADOW_RADIUS = 100.0f;
        constexpr float SUN_SHADOW_FAR_PLANE = 300.0f;
        constexpr int SUN_SHADOW_TEXTURE_SLOT = 14;
        constexpr std::array<Vector3, 6> SHADOW_DIRECTIONS{
            {{.x = 1.0f, .y = 0.0f, .z = 0.0f},
             {.x = -1.0f, .y = 0.0f, .z = 0.0f},
             {.x = 0.0f, .y = 1.0f, .z = 0.0f},
             {.x = 0.0f, .y = -1.0f, .z = 0.0f},
             {.x = 0.0f, .y = 0.0f, .z = 1.0f},
             {.x = 0.0f, .y = 0.0f, .z = -1.0f}}};
        constexpr std::array<Vector3, 6> SHADOW_UP{
            {{.x = 0.0f, .y = -1.0f, .z = 0.0f},
             {.x = 0.0f, .y = -1.0f, .z = 0.0f},
             {.x = 0.0f, .y = 0.0f, .z = 1.0f},
             {.x = 0.0f, .y = 0.0f, .z = -1.0f},
             {.x = 0.0f, .y = -1.0f, .z = 0.0f},
             {.x = 0.0f, .y = -1.0f, .z = 0.0f}}};
    } // namespace

    LightManager::ShaderState::ShaderState(const Shader source)
        : shader(source),
          ambientLocation(GetShaderLocation(source, "ambient")),
          gammaLocation(GetShaderLocation(source, "gamma")),
          lightsCountLocation(GetShaderLocation(source, "lightsCount")),
          pointShadowIndexLocation(GetShaderLocation(source, "pointShadowLightIndices[0]")),
          sunShadowIndexLocation(GetShaderLocation(source, "sunShadowLightIndex")),
          sunMatrixLocation(GetShaderLocation(source, "sunLightMatrix")),
          bloomLocation(GetShaderLocation(source, "bloomMask"))
    {
        const int sunSlot = SUN_SHADOW_TEXTURE_SLOT;
        const float farPlane = SHADOW_FAR_PLANE;
        for (std::size_t index = 0; index < MAX_POINT_SHADOWS; ++index)
            SetShaderValue(
                source,
                GetShaderLocation(source, POINT_SHADOW_SAMPLERS.at(index)),
                &POINT_SHADOW_TEXTURE_SLOTS.at(index),
                SHADER_UNIFORM_INT);
        SetShaderValue(source, GetShaderLocation(source, "sunShadowMap"), &sunSlot, SHADER_UNIFORM_INT);
        SetShaderValue(source, GetShaderLocation(source, "shadowFarPlane"), &farPlane, SHADER_UNIFORM_FLOAT);
    }

    void LightManager::updateShaderLights(ShaderState& state)
    {
        const auto shader = state.shader;
        const std::array<float, 5> ambientValues{ambient[0], ambient[1], ambient[2], ambient[3], gamma};
        if (state.ambientValues != ambientValues)
        {
            SetShaderValue(shader, state.ambientLocation, ambient.data(), SHADER_UNIFORM_VEC4);
            SetShaderValue(shader, state.gammaLocation, &gamma, SHADER_UNIFORM_FLOAT);
            state.ambientValues = ambientValues;
        }
        if (!state.lightValues || *state.lightValues != activeLights)
        {
            for (std::size_t index = 0; index < activeLights.size(); ++index)
            {
                if (state.lightLocations.size() <= index)
                    state.lightLocations.emplace_back(shader, static_cast<int>(index));
                activeLights[index].LinkShader(shader, state.lightLocations[index]);
            }
            SetShaderValue(shader, state.lightsCountLocation, &lightsCount, SHADER_UNIFORM_INT);
            state.lightValues = activeLights;
        }
        std::array<int, MAX_POINT_SHADOWS + 1> shadowIndices{};
        for (std::size_t index = 0; index < pointShadows.size(); ++index)
        {
            const auto lightIndex = pointShadows.at(index).lightIndex;
            shadowIndices.at(index) = lightIndex ? static_cast<int>(*lightIndex) : -1;
        }
        shadowIndices.back() = sunShadowLightIndex;
        if (state.shadowIndices != shadowIndices)
        {
            SetShaderValueV(
                shader,
                state.pointShadowIndexLocation,
                shadowIndices.data(),
                SHADER_UNIFORM_INT,
                static_cast<int>(MAX_POINT_SHADOWS));
            SetShaderValue(shader, state.sunShadowIndexLocation, &sunShadowLightIndex, SHADER_UNIFORM_INT);
            state.shadowIndices = shadowIndices;
        }
        if (sunShadowLightIndex >= 0)
        {
            std::array<float, 16> matrixValues{};
            std::ranges::copy(MatrixToFloatV(sunLightMatrix).v, matrixValues.begin());
            if (state.sunMatrix != matrixValues)
            {
                SetShaderValueMatrix(shader, state.sunMatrixLocation, sunLightMatrix);
                state.sunMatrix = matrixValues;
            }
        }
    }

    void LightManager::RemoveLight(entt::entity light)
    {
        registry->destroy(light);
        RefreshLights();
    }

    entt::entity LightManager::CreateLight(
        LightType type, Vector3 position, Vector3 target, Color color, float intensity)
    {
        if (lightsCount < MAX_LIGHT_COUNT)
        {
            auto entity = registry->create();
            auto& light = registry->emplace<Light>(entity);
            light.type = type;
            light.enabled = true;
            light.position = position;
            light.target = target;
            light.color = color;
            light.brightness = intensity;
            RefreshLights();
            return entity;
        }
        std::cout << "Scene: Max light sources reached. Ignoring. \n";
        return entt::null;
    }

    void LightManager::LinkShaderToLights(Shader& _shader)
    {
        const auto existing = std::ranges::find_if(
            shaders, [&_shader](const auto& state) { return state->shader.id == _shader.id; });
        if (existing == shaders.end())
        {
            // Editor and Play worlds share ResourceManager shaders and their actual GPU uniforms.
            // Share the cache too, so switching worlds always restores that world's values.
            static std::unordered_map<unsigned int, std::weak_ptr<ShaderState>> states;
            std::erase_if(states, [](const auto& entry) { return entry.second.expired(); });
            auto state = states[_shader.id].lock();
            if (!state)
            {
                state = std::make_shared<ShaderState>(_shader);
                states[_shader.id] = state;
            }
            shaders.push_back(std::move(state));
        }
        RefreshLights();
    }

    void LightManager::ApplyLightSettings(const LightSettings& settings)
    {
        ambient = {settings.ambient.x, settings.ambient.y, settings.ambient.z, settings.ambient.w};
        gamma = settings.gamma;
        RefreshLights();
    }

    void LightManager::RefreshLights()
    {
        activeLights.clear();
        for (const auto entity : registry->view<Light>())
        {
            const auto& light = registry->get<Light>(entity);
            if (!light.enabled || !IsEntityVisible(*registry, entity)) continue;
            if (activeLights.size() == MAX_LIGHT_COUNT) break;
            activeLights.push_back(light);
        }
        lightsCount = static_cast<int>(activeLights.size());
        for (auto& state : shaders)
            updateShaderLights(*state);
    }

    void LightManager::onLightAdded(entt::entity)
    {
        RefreshLights();
    }

    void LightManager::LinkRenderableToLight(entt::entity entity) const
    {
        auto& renderable = registry->get<Renderable>(entity);
        for (int i = 0; i < renderable.GetModel()->get().GetMaterialCount(); ++i)
        {
            renderable.GetModel()->get().SetShader(defaultShader, i);
        }
    }

    void LightManager::DrawDebugLights() const
    {
        for (const auto view = registry->view<Light>(); auto& entity : view)
        {
            auto& light = registry->get<Light>(entity);
            if (!IsEntityVisible(*registry, entity)) continue;
            if (light.enabled)
                DrawSphereEx(light.position, 0.2f, 8, 8, light.color);
            else
                DrawSphereWires(light.position, 0.2f, 8, 8, ColorAlpha(light.color, 0.3f));
        }
    }

    void LightManager::Update()
    {
        auto [x, y, z] = camera->GetPosition();
        const std::array<float, 3> cameraPos = {x, y, z};
        for (const auto& sharedState : shaders)
        {
            auto& state = *sharedState;
            if (state.cameraPosition == cameraPos) continue;
            SetShaderValue(
                state.shader, state.shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos.data(), SHADER_UNIFORM_VEC3);
            state.cameraPosition = cameraPos;
        }
    }

    void LightManager::SetBloomMask(const bool enabled) const
    {
        const int value = enabled ? 1 : 0;
        for (const auto& state : shaders)
            if (state->bloomLocation >= 0)
                SetShaderValue(state->shader, state->bloomLocation, &value, SHADER_UNIFORM_INT);
    }

    void LightManager::DrawShadowMap(const RenderSystem& renderer)
    {
        sunShadowLightIndex = -1;
        for (auto& shadow : pointShadows)
            shadow.lightIndex.reset();
        // Selection and shader light indices must use the same enabled/visible light list.
        RefreshLights();
        if (!shadowsEnabled) return;

        std::vector<std::size_t> pointCandidates;
        for (std::size_t index = 0; index < activeLights.size(); ++index)
        {
            const auto& light = activeLights.at(index);
            if (!light.castsShadows || light.brightness <= 0.0f) continue;
            if (light.type == LightType::Sun && sunShadowLightIndex < 0)
                sunShadowLightIndex = static_cast<int>(index);
            else if (light.type == LightType::Point)
                pointCandidates.push_back(index);
        }
        const Vector3 focus = camera->getRaylibCam()->target;
        std::ranges::stable_sort(pointCandidates, [&](const auto left, const auto right) {
            return Vector3DistanceSqr(activeLights.at(left).position, focus) <
                   Vector3DistanceSqr(activeLights.at(right).position, focus);
        });

        if (sunShadowLightIndex >= 0)
            drawSunShadowMap(renderer, activeLights.at(static_cast<std::size_t>(sunShadowLightIndex)));
        const auto count = std::min(pointCandidates.size(), pointShadows.size());
        for (std::size_t slot = 0; slot < count; ++slot)
        {
            auto& shadow = pointShadows.at(slot);
            shadow.lightIndex = pointCandidates.at(slot);
            drawPointShadowMap(renderer, activeLights.at(*shadow.lightIndex), shadow);
        }
        for (auto& state : shaders)
            updateShaderLights(*state);
    }

    void LightManager::drawPointShadowMap(const RenderSystem& renderer, const Light& light, PointShadowMap& shadow)
    {
        if (shadowShader.id == 0)
        {
            shadowShader = ResourceManager::GetInstance().ShaderLoad(
                ShaderPath("custom/point_shadow.vs"), ShaderPath("custom/point_shadow.fs"));
            shadowPositionLocation = GetShaderLocation(shadowShader, "lightPosition");
            shadowSkinnedLocation = GetShaderLocation(shadowShader, "skinned");
            const float farPlane = SHADOW_FAR_PLANE;
            SetShaderValue(
                shadowShader, GetShaderLocation(shadowShader, "shadowFarPlane"), &farPlane, SHADER_UNIFORM_FLOAT);
        }
        if (shadow.framebuffer == 0)
        {
            const std::vector<float> clearData(6 * SHADOW_MAP_SIZE * SHADOW_MAP_SIZE, 1.0f);
            shadow.cubemap =
                rlLoadTextureCubemap(clearData.data(), SHADOW_MAP_SIZE, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
            shadow.framebuffer = rlLoadFramebuffer();
            const unsigned int depthBuffer = rlLoadTextureDepth(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, true);
            rlFramebufferAttach(
                shadow.framebuffer,
                shadow.cubemap,
                RL_ATTACHMENT_COLOR_CHANNEL0,
                RL_ATTACHMENT_CUBEMAP_POSITIVE_X,
                0);
            rlFramebufferAttach(
                shadow.framebuffer, depthBuffer, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
            if (!rlFramebufferComplete(shadow.framebuffer))
            {
                rlUnloadTexture(shadow.cubemap);
                rlUnloadFramebuffer(shadow.framebuffer);
                shadow.cubemap = 0;
                shadow.framebuffer = 0;
                shadow.lightIndex.reset();
                std::cerr << "LIGHTING: Could not allocate point-light shadow map\n";
                return;
            }
        }

        const std::array<float, 3> lightPosition = {light.position.x, light.position.y, light.position.z};
        SetShaderValue(shadowShader, shadowPositionLocation, lightPosition.data(), SHADER_UNIFORM_VEC3);

        const unsigned int previousFramebuffer = rlGetActiveFramebuffer();
        const int previousWidth = rlGetFramebufferWidth();
        const int previousHeight = rlGetFramebufferHeight();
        for (int face = 0; face < 6; ++face)
        {
            rlFramebufferAttach(shadow.framebuffer, shadow.cubemap, RL_ATTACHMENT_COLOR_CHANNEL0, face, 0);
            rlEnableFramebuffer(shadow.framebuffer);
            rlViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
            rlSetFramebufferWidth(SHADOW_MAP_SIZE);
            rlSetFramebufferHeight(SHADOW_MAP_SIZE);
            ClearBackground(sage::colors::WHITE_COLOR);

            Camera3D faceCamera{};
            faceCamera.position = light.position;
            faceCamera.target = Vector3Add(light.position, SHADOW_DIRECTIONS.at(face));
            faceCamera.up = SHADOW_UP.at(face);
            faceCamera.fovy = 90.0f;
            faceCamera.projection = CAMERA_PERSPECTIVE;
            BeginMode3D(faceCamera);
            rlSetMatrixProjection(
                MatrixPerspective(90.0 * sage::math::DEGREES_TO_RADIANS, 1.0, 0.1, SHADOW_FAR_PLANE));
            renderer.DrawShadowCasters(shadowShader, shadowSkinnedLocation);
            EndMode3D();
        }
        rlEnableFramebuffer(previousFramebuffer);
        rlViewport(0, 0, previousWidth, previousHeight);
        rlSetFramebufferWidth(previousWidth);
        rlSetFramebufferHeight(previousHeight);
    }

    void LightManager::drawSunShadowMap(const RenderSystem& renderer, const Light& sun)
    {
        if (sunShadowFramebuffer == 0)
        {
            sunShadowShader = ResourceManager::GetInstance().ShaderLoad(
                ShaderPath("custom/point_shadow.vs"), ShaderPath("custom/sun_shadow.fs"));
            sunSkinnedLocation = GetShaderLocation(sunShadowShader, "skinned");
            const std::vector<float> clearData(SUN_SHADOW_MAP_SIZE * SUN_SHADOW_MAP_SIZE, 1.0f);
            sunShadowTexture = rlLoadTexture(
                clearData.data(), SUN_SHADOW_MAP_SIZE, SUN_SHADOW_MAP_SIZE, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
            sunShadowFramebuffer = rlLoadFramebuffer();
            const unsigned int depthBuffer = rlLoadTextureDepth(SUN_SHADOW_MAP_SIZE, SUN_SHADOW_MAP_SIZE, true);
            rlFramebufferAttach(
                sunShadowFramebuffer, sunShadowTexture, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
            rlFramebufferAttach(
                sunShadowFramebuffer, depthBuffer, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
            if (!rlFramebufferComplete(sunShadowFramebuffer))
            {
                rlUnloadTexture(sunShadowTexture);
                rlUnloadFramebuffer(sunShadowFramebuffer);
                sunShadowTexture = 0;
                sunShadowFramebuffer = 0;
                sunShadowLightIndex = -1;
                return;
            }
        }

        const Vector3 sunDirection = Vector3Normalize(Vector3Subtract(sun.position, sun.target));
        const Vector3 center = camera->getRaylibCam()->target;
        const Vector3 sunPosition = Vector3Add(center, Vector3Scale(sunDirection, SUN_SHADOW_FAR_PLANE * 0.5f));
        const Vector3 up = std::abs(sunDirection.y) > 0.95f ? Vector3{.x = 0.0f, .y = 0.0f, .z = 1.0f}
                                                            : Vector3{.x = 0.0f, .y = 1.0f, .z = 0.0f};
        Camera3D sunCamera{};
        sunCamera.position = sunPosition;
        sunCamera.target = center;
        sunCamera.up = up;
        sunCamera.fovy = SUN_SHADOW_RADIUS * 2.0f;
        sunCamera.projection = CAMERA_ORTHOGRAPHIC;

        const unsigned int previousFramebuffer = rlGetActiveFramebuffer();
        const int previousWidth = rlGetFramebufferWidth();
        const int previousHeight = rlGetFramebufferHeight();
        rlEnableFramebuffer(sunShadowFramebuffer);
        rlViewport(0, 0, SUN_SHADOW_MAP_SIZE, SUN_SHADOW_MAP_SIZE);
        rlSetFramebufferWidth(SUN_SHADOW_MAP_SIZE);
        rlSetFramebufferHeight(SUN_SHADOW_MAP_SIZE);
        ClearBackground(sage::colors::WHITE_COLOR);
        BeginMode3D(sunCamera);
        rlSetMatrixProjection(MatrixOrtho(
            -SUN_SHADOW_RADIUS,
            SUN_SHADOW_RADIUS,
            -SUN_SHADOW_RADIUS,
            SUN_SHADOW_RADIUS,
            0.1,
            SUN_SHADOW_FAR_PLANE));
        sunLightMatrix = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
        renderer.DrawShadowCasters(sunShadowShader, sunSkinnedLocation);
        EndMode3D();
        rlEnableFramebuffer(previousFramebuffer);
        rlViewport(0, 0, previousWidth, previousHeight);
        rlSetFramebufferWidth(previousWidth);
        rlSetFramebufferHeight(previousHeight);
    }

    void LightManager::BindShadowMap() const
    {
        if (sunShadowLightIndex >= 0 && sunShadowTexture != 0)
        {
            rlActiveTextureSlot(SUN_SHADOW_TEXTURE_SLOT);
            rlEnableTexture(sunShadowTexture);
        }
        for (std::size_t index = 0; index < pointShadows.size(); ++index)
        {
            const auto& shadow = pointShadows.at(index);
            if (!shadow.lightIndex || shadow.cubemap == 0) continue;
            rlActiveTextureSlot(POINT_SHADOW_TEXTURE_SLOTS.at(index));
            rlEnableTextureCubemap(shadow.cubemap);
        }
        rlActiveTextureSlot(0);
    }

    void LightManager::UnbindShadowMap() const
    {
        if (sunShadowLightIndex >= 0 && sunShadowTexture != 0)
        {
            rlActiveTextureSlot(SUN_SHADOW_TEXTURE_SLOT);
            rlDisableTexture();
        }
        for (std::size_t index = 0; index < pointShadows.size(); ++index)
        {
            const auto& shadow = pointShadows.at(index);
            if (!shadow.lightIndex || shadow.cubemap == 0) continue;
            rlActiveTextureSlot(POINT_SHADOW_TEXTURE_SLOTS.at(index));
            rlDisableTextureCubemap();
        }
        rlActiveTextureSlot(0);
    }

    LightManager::LightManager(
        entt::registry* _registry, Camera* _camera, const LightSettings& settings, const bool _shadowsEnabled)
        : registry(_registry), camera(_camera), shadowsEnabled(_shadowsEnabled)
    {
        registry->on_construct<Light>().connect<&LightManager::onLightAdded>(this);
        defaultShader = ResourceManager::GetInstance().ShaderLoad(
            ShaderPath("custom/lighting.vs"), ShaderPath("custom/lighting.fs"));

        ApplyLightSettings(settings);
        LinkShaderToLights(defaultShader);
    }

    LightManager::~LightManager()
    {
        registry->on_construct<Light>().disconnect<&LightManager::onLightAdded>(this);
        for (const auto& shadow : pointShadows)
        {
            if (shadow.cubemap != 0) rlUnloadTexture(shadow.cubemap);
            if (shadow.framebuffer != 0) rlUnloadFramebuffer(shadow.framebuffer);
        }
        if (sunShadowTexture != 0) rlUnloadTexture(sunShadowTexture);
        if (sunShadowFramebuffer != 0) rlUnloadFramebuffer(sunShadowFramebuffer);
    }
} // namespace sage

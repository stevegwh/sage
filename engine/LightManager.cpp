

#include "LightManager.hpp"

#include "Camera.hpp"
#include "components/Renderable.hpp"
#include "Light.hpp"
#include "Settings.hpp"
#include "ShaderPaths.hpp"
#include "systems/RenderSystem.hpp"
#include "rlgl.h"
#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sage
{
    namespace
    {
        constexpr int SHADOW_MAP_SIZE = 512;
        constexpr float SHADOW_FAR_PLANE = 100.0f;
        constexpr int SHADOW_TEXTURE_SLOT = 15;
        constexpr int SUN_SHADOW_MAP_SIZE = 2048;
        constexpr float SUN_SHADOW_RADIUS = 100.0f;
        constexpr float SUN_SHADOW_FAR_PLANE = 300.0f;
        constexpr int SUN_SHADOW_TEXTURE_SLOT = 14;
        constexpr std::array<Vector3, 6> SHADOW_DIRECTIONS{{
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}};
        constexpr std::array<Vector3, 6> SHADOW_UP{{
            {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
            {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}};
    }

    void LightManager::updateShaderLights(Shader& _shader)
    {
        updateAmbientLight(_shader);
        lightsCount = 0;
        for (const auto view = registry->view<Light>(); auto& entity : view)
        {
            const auto& light = registry->get<Light>(entity);
            if (!light.enabled) continue;

            if (lightsCount < MAX_LIGHT_COUNT)
            {
                light.LinkShader(_shader, lightsCount);
                lightsCount++;
            }
            else
            {
                std::cout << "Scene: Max light sources reached. \n";
                break;
            }
        }

        auto lightsCountLoc = GetShaderLocation(_shader, "lightsCount");
        SetShaderValue(_shader, lightsCountLoc, &lightsCount, SHADER_UNIFORM_INT);
        auto gammaLoc = GetShaderLocation(_shader, "gamma");
        SetShaderValue(_shader, gammaLoc, &gamma, SHADER_UNIFORM_FLOAT);
        SetShaderValue(_shader, GetShaderLocation(_shader, "shadowLightIndex"), &shadowLightIndex, SHADER_UNIFORM_INT);
        const int shadowSlot = SHADOW_TEXTURE_SLOT;
        SetShaderValue(_shader, GetShaderLocation(_shader, "pointShadowMap"), &shadowSlot, SHADER_UNIFORM_INT);
        const float farPlane = SHADOW_FAR_PLANE;
        SetShaderValue(_shader, GetShaderLocation(_shader, "shadowFarPlane"), &farPlane, SHADER_UNIFORM_FLOAT);
        SetShaderValue(_shader, GetShaderLocation(_shader, "sunShadowLightIndex"),
                       &sunShadowLightIndex, SHADER_UNIFORM_INT);
        const int sunShadowSlot = SUN_SHADOW_TEXTURE_SLOT;
        SetShaderValue(_shader, GetShaderLocation(_shader, "sunShadowMap"),
                       &sunShadowSlot, SHADER_UNIFORM_INT);
        if (sunShadowLightIndex >= 0)
            SetShaderValueMatrix(_shader, GetShaderLocation(_shader, "sunLightMatrix"), sunLightMatrix);
    }

    void LightManager::updateAmbientLight(Shader& _shader) const
    {
        auto ambientLoc = GetShaderLocation(_shader, "ambient");
        float ambientValue[4] = {ambient[0], ambient[1], ambient[2], ambient[3]};
        SetShaderValue(_shader, ambientLoc, ambientValue, SHADER_UNIFORM_VEC4);
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
        auto it = std::find_if(shaders.begin(), shaders.end(), [&_shader](const Shader& existingShader) {
            return existingShader.id == _shader.id;
        });

        if (it == shaders.end())
        {
            shaders.push_back(_shader);
        }
        updateShaderLights(_shader);
    }

    void LightManager::ApplyLightSettings(const LightSettings& settings)
    {
        ambient = {settings.ambient.x, settings.ambient.y, settings.ambient.z, settings.ambient.w};
        gamma = settings.gamma;
        RefreshLights();
    }

    void LightManager::RefreshLights()
    {
        for (auto& _shader : shaders)
        {
            updateShaderLights(_shader);
        }
    }

    void LightManager::onLightAdded(entt::entity)
    {
        RefreshLights();
    }

    void LightManager::LinkRenderableToLight(entt::entity entity) const
    {
        auto& renderable = registry->get<Renderable>(entity);
        for (int i = 0; i < renderable.GetModel()->GetMaterialCount(); ++i)
        {
            renderable.GetModel()->SetShader(defaultShader, i);
        }
    }

    void LightManager::DrawDebugLights() const
    {
        for (const auto view = registry->view<Light>(); auto& entity : view)
        {
            auto& light = registry->get<Light>(entity);
            if (light.enabled)
                DrawSphereEx(light.position, 0.2f, 8, 8, light.color);
            else
                DrawSphereWires(light.position, 0.2f, 8, 8, ColorAlpha(light.color, 0.3f));
        }
    }

    void LightManager::Update() const
    {
        auto [x, y, z] = camera->GetPosition();
        const float cameraPos[3] = {x, y, z};
        for (auto& shader : shaders)
        {
            SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
        }
    }

    void LightManager::SetBloomMask(const bool enabled) const
    {
        const int value = enabled ? 1 : 0;
        for (const auto& shader : shaders)
        {
            const int location = GetShaderLocation(shader, "bloomMask");
            if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
        }
    }

    void LightManager::DrawShadowMap(const RenderSystem& renderer)
    {
        sunShadowLightIndex = -1;
        shadowLightIndex = -1;
        const Light* sun = nullptr;
        const Light* point = nullptr;
        int index = 0;
        for (const auto entity : registry->view<Light>())
        {
            const auto& light = registry->get<Light>(entity);
            if (!light.enabled) continue;
            if (index >= MAX_LIGHT_COUNT) break;
            if (light.castsShadows)
            {
                if (light.type == LightType::Sun && sun == nullptr)
                {
                    sun = &light;
                    sunShadowLightIndex = index;
                }
                else if (light.type == LightType::Point && point == nullptr)
                {
                    point = &light;
                    shadowLightIndex = index;
                }
            }
            ++index;
            if (sun != nullptr && point != nullptr) break;
        }

        if (sun != nullptr) drawSunShadowMap(renderer, *sun);
        if (point != nullptr) drawPointShadowMap(renderer, *point);
        RefreshLights();
    }

    void LightManager::drawPointShadowMap(const RenderSystem& renderer, const Light& light)
    {
        if (shadowFramebuffer == 0)
        {
            shadowShader = ResourceManager::GetInstance().ShaderLoad(
                ShaderPath("custom/point_shadow.vs").c_str(), ShaderPath("custom/point_shadow.fs").c_str());
            const std::vector<float> clearData(6 * SHADOW_MAP_SIZE * SHADOW_MAP_SIZE, 1.0f);
            shadowCubemap = rlLoadTextureCubemap(
                clearData.data(), SHADOW_MAP_SIZE, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
            shadowFramebuffer = rlLoadFramebuffer();
            const unsigned int depthBuffer = rlLoadTextureDepth(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, true);
            rlFramebufferAttach(shadowFramebuffer, shadowCubemap, RL_ATTACHMENT_COLOR_CHANNEL0,
                                RL_ATTACHMENT_CUBEMAP_POSITIVE_X, 0);
            rlFramebufferAttach(shadowFramebuffer, depthBuffer, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
            if (!rlFramebufferComplete(shadowFramebuffer))
            {
                rlUnloadTexture(shadowCubemap);
                rlUnloadFramebuffer(shadowFramebuffer);
                shadowCubemap = 0;
                shadowFramebuffer = 0;
                shadowLightIndex = -1;
                return;
            }
        }

        const float lightPosition[3] = {
            light.position.x, light.position.y, light.position.z};
        SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "lightPosition"),
                       lightPosition, SHADER_UNIFORM_VEC3);
        const float farPlane = SHADOW_FAR_PLANE;
        SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowFarPlane"),
                       &farPlane, SHADER_UNIFORM_FLOAT);
        const int skinnedLocation = GetShaderLocation(shadowShader, "skinned");

        const unsigned int previousFramebuffer = rlGetActiveFramebuffer();
        const int previousWidth = rlGetFramebufferWidth();
        const int previousHeight = rlGetFramebufferHeight();
        for (int face = 0; face < 6; ++face)
        {
            rlFramebufferAttach(shadowFramebuffer, shadowCubemap, RL_ATTACHMENT_COLOR_CHANNEL0, face, 0);
            rlEnableFramebuffer(shadowFramebuffer);
            rlViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
            rlSetFramebufferWidth(SHADOW_MAP_SIZE);
            rlSetFramebufferHeight(SHADOW_MAP_SIZE);
            ClearBackground(WHITE);

            Camera3D faceCamera{};
            faceCamera.position = light.position;
            faceCamera.target = Vector3Add(light.position, SHADOW_DIRECTIONS[face]);
            faceCamera.up = SHADOW_UP[face];
            faceCamera.fovy = 90.0f;
            faceCamera.projection = CAMERA_PERSPECTIVE;
            BeginMode3D(faceCamera);
            rlSetMatrixProjection(MatrixPerspective(90.0 * DEG2RAD, 1.0, 0.1, SHADOW_FAR_PLANE));
            renderer.DrawShadowCasters(shadowShader, skinnedLocation);
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
                ShaderPath("custom/point_shadow.vs").c_str(), ShaderPath("custom/sun_shadow.fs").c_str());
            const std::vector<float> clearData(SUN_SHADOW_MAP_SIZE * SUN_SHADOW_MAP_SIZE, 1.0f);
            sunShadowTexture = rlLoadTexture(
                clearData.data(), SUN_SHADOW_MAP_SIZE, SUN_SHADOW_MAP_SIZE, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
            sunShadowFramebuffer = rlLoadFramebuffer();
            const unsigned int depthBuffer = rlLoadTextureDepth(SUN_SHADOW_MAP_SIZE, SUN_SHADOW_MAP_SIZE, true);
            rlFramebufferAttach(sunShadowFramebuffer, sunShadowTexture, RL_ATTACHMENT_COLOR_CHANNEL0,
                                RL_ATTACHMENT_TEXTURE2D, 0);
            rlFramebufferAttach(sunShadowFramebuffer, depthBuffer, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
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
        const Vector3 up = std::abs(sunDirection.y) > 0.95f ? Vector3{0.0f, 0.0f, 1.0f}
                                                             : Vector3{0.0f, 1.0f, 0.0f};
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
        ClearBackground(WHITE);
        BeginMode3D(sunCamera);
        rlSetMatrixProjection(MatrixOrtho(
            -SUN_SHADOW_RADIUS, SUN_SHADOW_RADIUS, -SUN_SHADOW_RADIUS, SUN_SHADOW_RADIUS,
            0.1, SUN_SHADOW_FAR_PLANE));
        sunLightMatrix = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
        renderer.DrawShadowCasters(sunShadowShader, GetShaderLocation(sunShadowShader, "skinned"));
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
        if (shadowLightIndex >= 0 && shadowCubemap != 0)
        {
            rlActiveTextureSlot(SHADOW_TEXTURE_SLOT);
            rlEnableTextureCubemap(shadowCubemap);
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
        if (shadowLightIndex >= 0 && shadowCubemap != 0)
        {
            rlActiveTextureSlot(SHADOW_TEXTURE_SLOT);
            rlDisableTextureCubemap();
        }
        rlActiveTextureSlot(0);
    }

    LightManager::LightManager(entt::registry* _registry, Camera* _camera, const LightSettings& settings)
        : registry(_registry), camera(_camera)
    {
        registry->on_construct<Light>().connect<&LightManager::onLightAdded>(this);
        defaultShader = ResourceManager::GetInstance().ShaderLoad(
            ShaderPath("custom/lighting.vs").c_str(), ShaderPath("custom/lighting.fs").c_str());

        ApplyLightSettings(settings);
        LinkShaderToLights(defaultShader);
    }

    LightManager::~LightManager()
    {
        if (shadowCubemap != 0) rlUnloadTexture(shadowCubemap);
        if (shadowFramebuffer != 0) rlUnloadFramebuffer(shadowFramebuffer);
        if (sunShadowTexture != 0) rlUnloadTexture(sunShadowTexture);
        if (sunShadowFramebuffer != 0) rlUnloadFramebuffer(sunShadowFramebuffer);
    }
} // namespace sage

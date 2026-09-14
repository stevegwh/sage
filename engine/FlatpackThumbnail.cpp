#include "FlatpackThumbnail.hpp"

#include "components/Collideable.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "components/UberShaderComponent.hpp"
#include "Flatpack.hpp"
#include "ResourceManager.hpp"
#include "systems/TransformSystem.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace sage
{
    namespace
    {
        constexpr int kPreviewLightDirectional = 0;
        constexpr int kPreviewLightPoint = 1;
        constexpr float kPreviewGamma = 1.9f;
        constexpr Color kPreviewLightColor = {255, 244, 214, 255};

        Shader LoadThumbnailShader()
        {
            auto shader = ResourceManager::GetInstance().ShaderLoadUnique(
                "resources/shaders/custom/ubershader.vs", "resources/shaders/custom/ubershader.fs");
            shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, "emissionMap");
            return shader;
        }

        UberShaderComponent CreateThumbnailUberComponent(const ModelView& model, const Shader shader)
        {
            UberShaderComponent uber(static_cast<unsigned int>(model.GetMaterialCount()));
            uber.shader = shader;
            uber.litLoc = GetShaderLocation(shader, "lit");
            uber.skinnedLoc = GetShaderLocation(shader, "skinned");
            uber.hasEmissiveTexLoc = GetShaderLocation(shader, "hasEmissionTex");
            uber.hasEmissiveColLoc = GetShaderLocation(shader, "hasEmissionCol");
            uber.colEmissiveLoc = GetShaderLocation(shader, "colEmission");
            uber.SetFlagAll(UberShaderComponent::Flags::Lit);

            const auto& modelData = model.GetRlModel();
            for (int i = 0; i < modelData.materialCount; ++i)
            {
                const auto emissionColor = modelData.materials[i].maps[MATERIAL_MAP_EMISSION].color;
                const auto emissionTexture = modelData.materials[i].maps[MATERIAL_MAP_EMISSION].texture.id;
                if (emissionColor.r != 0 || emissionColor.g != 0 || emissionColor.b != 0)
                    uber.SetFlag(static_cast<unsigned int>(i), UberShaderComponent::Flags::EmissiveCol);
                if (emissionTexture > 1)
                    uber.SetFlag(static_cast<unsigned int>(i), UberShaderComponent::Flags::EmissiveTexture);
            }
            return uber;
        }

        void SetThumbnailLight(
            const Shader shader,
            const int index,
            const int type,
            const Vector3 position,
            const Vector3 target,
            const Color color,
            const float brightness)
        {
            constexpr int enabled = 1;
            constexpr float constant = 1.0f;
            constexpr float linear = 0.0f;
            constexpr float quadratic = 0.0f;
            const float positionValue[3] = {position.x, position.y, position.z};
            const float targetValue[3] = {target.x, target.y, target.z};
            const float colorValue[4] = {
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                static_cast<float>(color.a) / 255.0f};

            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].enabled", index)),
                &enabled,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].type", index)),
                &type,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].position", index)),
                positionValue,
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].target", index)),
                targetValue,
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].color", index)),
                colorValue,
                SHADER_UNIFORM_VEC4);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].brightness", index)),
                &brightness,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].constant", index)),
                &constant,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].linear", index)),
                &linear,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].quadratic", index)),
                &quadratic,
                SHADER_UNIFORM_FLOAT);
        }

        void ConfigureThumbnailLighting(const Shader shader, const Camera3D& camera, const Vector3 center)
        {
            const float ambient[4] = {0.6f, 0.2f, 0.8f, 1.0f};
            SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient, SHADER_UNIFORM_VEC4);

            constexpr int lightCount = 2;
            SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightCount, SHADER_UNIFORM_INT);
            SetShaderValue(shader, GetShaderLocation(shader, "gamma"), &kPreviewGamma, SHADER_UNIFORM_FLOAT);

            const float viewPosition[3] = {camera.position.x, camera.position.y, camera.position.z};
            SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), viewPosition, SHADER_UNIFORM_VEC3);

            SetThumbnailLight(shader, 0, kPreviewLightPoint, camera.position, center, kPreviewLightColor, 1.17f);
            SetThumbnailLight(
                shader,
                1,
                kPreviewLightDirectional,
                Vector3Add(center, {-3.0f, 4.0f, -4.0f}),
                center,
                Color{172, 202, 255, 255},
                0.23f);
        }

        void ExpandBounds(BoundingBox& bounds, const BoundingBox other)
        {
            bounds.min.x = std::min(bounds.min.x, other.min.x);
            bounds.min.y = std::min(bounds.min.y, other.min.y);
            bounds.min.z = std::min(bounds.min.z, other.min.z);
            bounds.max.x = std::max(bounds.max.x, other.max.x);
            bounds.max.y = std::max(bounds.max.y, other.max.y);
            bounds.max.z = std::max(bounds.max.z, other.max.z);
        }
    } // namespace

    RenderTexture2D CreateFlatpackThumbnail(
        const std::filesystem::path& path, const int size, const Color background)
    {
        if (size <= 0) return {};

        entt::registry previewRegistry;
        TransformSystem previewTransforms(&previewRegistry);
        const auto instance = LoadFlatpack(previewRegistry, path.string().c_str(), Vector3Zero());
        if (!instance) return {};

        std::optional<BoundingBox> bounds;
        for (const auto entity : instance.entities)
        {
            const auto* transform = previewRegistry.try_get<sgTransform>(entity);
            const auto* renderable = previewRegistry.try_get<Renderable>(entity);
            const auto* model = renderable ? renderable->GetModel() : nullptr;
            if (!transform || !renderable || !renderable->active || !model || model->GetMeshCount() == 0) continue;

            const auto entityBounds =
                TransformBoundingBoxByCorners(model->CalcLocalBoundingBox(), transform->GetMatrix());
            if (bounds)
                ExpandBounds(*bounds, entityBounds);
            else
                bounds = entityBounds;
        }
        if (!bounds) return {};

        const Vector3 boundsSize = Vector3Subtract(bounds->max, bounds->min);
        const Vector3 center = Vector3Scale(Vector3Add(bounds->min, bounds->max), 0.5f);
        const float radius =
            std::max({std::fabs(boundsSize.x), std::fabs(boundsSize.y), std::fabs(boundsSize.z), 1.0f});

        Camera3D camera{};
        camera.position = Vector3Add(center, {radius * 1.35f, radius * 0.85f, radius * 1.65f});
        camera.target = center;
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 32.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        auto thumbnail = LoadRenderTexture(size, size);
        const auto shader = LoadThumbnailShader();
        BeginTextureMode(thumbnail);
        ClearBackground(background);
        BeginMode3D(camera);
        ConfigureThumbnailLighting(shader, camera, center);
        for (const auto entity : instance.entities)
        {
            const auto* transform = previewRegistry.try_get<sgTransform>(entity);
            auto* renderable = previewRegistry.try_get<Renderable>(entity);
            auto* model = renderable ? renderable->GetModel() : nullptr;
            if (!transform || !renderable || !renderable->active || !model || model->GetMeshCount() == 0) continue;

            auto uber = CreateThumbnailUberComponent(*model, shader);
            std::vector<Shader> originalShaders;
            originalShaders.reserve(static_cast<std::size_t>(model->GetMaterialCount()));
            for (int material = 0; material < model->GetMaterialCount(); ++material)
                originalShaders.push_back(model->GetShader(material));
            model->SetShader(shader);
            model->DrawUber(
                &uber,
                transform->GetWorldPos(),
                transform->GetWorldRot(),
                transform->GetScale(),
                renderable->hint);
            for (int material = 0; material < model->GetMaterialCount(); ++material)
                model->SetShader(originalShaders[static_cast<std::size_t>(material)], material);
        }
        EndMode3D();
        EndTextureMode();
        UnloadShader(shader);
        return thumbnail;
    }
} // namespace sage

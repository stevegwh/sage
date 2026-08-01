//
// Created by Steve Wheeler on 21/02/2024.
//

#include "RenderSystem.hpp"

#include "components/CustomShaderComponent.hpp"
#include "components/DynamicRenderable.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"

#include "components/UberShaderComponent.hpp"
#include "raylib.h"
#include "ResourceManager.hpp"
#include "rlgl.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace sage
{
    namespace
    {
#if defined(PLATFORM_DESKTOP)
        constexpr const char* SKYBOX_VERTEX_SHADER = "resources/shaders/glsl330/skybox.vs";
        constexpr const char* SKYBOX_FRAGMENT_SHADER = "resources/shaders/glsl330/skybox.fs";
#else
        constexpr const char* SKYBOX_VERTEX_SHADER = "resources/shaders/glsl100/skybox.vs";
        constexpr const char* SKYBOX_FRAGMENT_SHADER = "resources/shaders/glsl100/skybox.fs";
#endif
    } // namespace

    class Skybox
    {
        Model model{};
        TextureCubemap cubemap{};

      public:
        explicit Skybox(const std::string& imageKey)
        {
            auto image = ResourceManager::GetInstance().GetImage(imageKey);
            const Image& source = image.GetImage();
            if (source.width % 4 != 0 || source.height % 3 != 0 || source.width / 4 != source.height / 3)
            {
                throw std::runtime_error("Skybox image '" + imageKey + "' must use a 4x3 cubemap cross layout");
            }

            model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
            model.materials[0].shader =
                ResourceManager::GetInstance().ShaderLoad(SKYBOX_VERTEX_SHADER, SKYBOX_FRAGMENT_SHADER);

            const int environmentMap = MATERIAL_MAP_CUBEMAP;
            const int disabled = 0;
            SetShaderValue(
                model.materials[0].shader,
                GetShaderLocation(model.materials[0].shader, "environmentMap"),
                &environmentMap,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                model.materials[0].shader,
                GetShaderLocation(model.materials[0].shader, "vflipped"),
                &disabled,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                model.materials[0].shader,
                GetShaderLocation(model.materials[0].shader, "doGamma"),
                &disabled,
                SHADER_UNIFORM_INT);

            cubemap = LoadTextureCubemap(source, CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE);
            if (cubemap.id == 0)
            {
                UnloadModel(model);
                model = {};
                throw std::runtime_error("Could not upload skybox image '" + imageKey + "'");
            }
            SetTextureFilter(cubemap, TEXTURE_FILTER_BILINEAR);
            model.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = cubemap;
        }

        ~Skybox()
        {
            if (cubemap.id != 0) UnloadTexture(cubemap);
            if (model.meshCount != 0) UnloadModel(model);
        }

        Skybox(const Skybox&) = delete;
        Skybox& operator=(const Skybox&) = delete;

        void Draw() const
        {
            rlDisableBackfaceCulling();
            rlDisableDepthMask();
            DrawModel(model, Vector3Zero(), 1.0f, WHITE);
            rlEnableDepthMask();
            rlEnableBackfaceCulling();
        }
    };

    void RenderSystem::Update()
    {
    }

    void RenderSystem::Draw() // Can't be const as GetModel returns pointers
    {
        if (skybox) skybox->Draw();

        auto normalView = registry->view<Renderable, sgTransform>(
            entt::exclude<RenderableDeferred, UberShaderComponent, CustomShaderComponent>);
        auto deferredView = registry->view<Renderable, sgTransform, RenderableDeferred>(
            entt::exclude<UberShaderComponent, CustomShaderComponent>);
        auto customShaderView =
            registry->view<Renderable, sgTransform, CustomShaderComponent>(entt::exclude<RenderableDeferred>);
        auto customShaderDeferredView =
            registry->view<Renderable, sgTransform, CustomShaderComponent, RenderableDeferred>();
        auto uberView =
            registry->view<Renderable, sgTransform, UberShaderComponent>(entt::exclude<CustomShaderComponent>);
        auto dynamicView = registry->view<DynamicRenderable, sgTransform>(entt::exclude<RenderableDeferred>);
        auto dynamicDeferredView = registry->view<DynamicRenderable, sgTransform, RenderableDeferred>();

        auto renderEntity = [this](auto& renderable, const auto& transform, const entt::entity entity) {
            if (!renderable.active) return;

            auto* model = renderable.GetModel();
            if (model == nullptr) return;

            if (renderable.reqShaderUpdate) renderable.reqShaderUpdate(entity);

            model->Draw(transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale(), renderable.hint);
        };

        auto renderDynamicEntity = [this](auto& renderable, const auto& transform, const entt::entity entity) {
            if (!renderable.active) return;

            if (renderable.reqShaderUpdate) renderable.reqShaderUpdate(entity);

            Vector3 rotationAxis = {0.0f, 1.0f, 0.0f};

            renderable.Draw(
                transform.GetWorldPos(),
                rotationAxis,
                transform.GetWorldRot().y,
                transform.GetScale(),
                renderable.hint);
        };

        const auto drawAll = [](auto& view, const auto& draw) {
            for (auto [entity, renderable, transform] : view.each())
                draw(renderable, transform, entity);
        };
        const auto drawCustomAll = [&renderEntity](auto& view) {
            for (const auto entity : view)
            {
                auto& renderable = view.template get<Renderable>(entity);
                if (!renderable.active || renderable.GetModel() == nullptr) continue;
                view.template get<CustomShaderComponent>(entity).Update(renderable);
                renderEntity(renderable, view.template get<sgTransform>(entity), entity);
            }
        };

        // Pass order is intentional: ordinary forward geometry first, then dynamic geometry,
        // uber-shader objects, deferred objects, and finally dynamic deferred objects.
        drawAll(normalView, renderEntity);
        drawAll(dynamicView, renderDynamicEntity);

        drawCustomAll(customShaderView);

        for (auto entity : uberView)
        {
            auto& renderable = uberView.get<Renderable>(entity);
            if (!renderable.active) continue;

            auto* model = renderable.GetModel();
            if (model == nullptr) continue;

            const auto& transform = uberView.get<sgTransform>(entity);
            auto& uber = uberView.get<UberShaderComponent>(entity);
            if (renderable.reqShaderUpdate) renderable.reqShaderUpdate(entity);

            model->DrawUber(
                &uber, transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale(), renderable.hint);
        }

        drawAll(deferredView, renderEntity);
        drawCustomAll(customShaderDeferredView);
        drawAll(dynamicDeferredView, renderDynamicEntity);
    }

    RenderSystem::RenderSystem(entt::registry* _registry) : registry(_registry)
    {
    }

    RenderSystem::~RenderSystem() = default;

    void RenderSystem::SetSkybox(const std::string& imageKey)
    {
        skybox = std::make_unique<Skybox>(imageKey);
    }

    void RenderSystem::ClearSkybox()
    {
        skybox.reset();
    }
} // namespace sage

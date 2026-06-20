//
// Created by Steve Wheeler on 21/02/2024.
//

#include "RenderSystem.hpp"

#include "components/DynamicRenderable.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"

#include "components/UberShaderComponent.hpp"
#include "raylib.h"

namespace sage
{

    void RenderSystem::Update()
    {
    }

    void RenderSystem::Draw() // Can't be const as GetModel returns pointers
    {
        auto normalView =
            registry->view<Renderable, sgTransform>(entt::exclude<RenderableDeferred, UberShaderComponent>);
        auto deferredView =
            registry->view<Renderable, sgTransform, RenderableDeferred>(entt::exclude<UberShaderComponent>);
        auto uberView = registry->view<Renderable, sgTransform, UberShaderComponent>();
        auto dynamicView = registry->view<DynamicRenderable, sgTransform>(entt::exclude<RenderableDeferred>);
        auto dynamicDeferredView = registry->view<DynamicRenderable, sgTransform, RenderableDeferred>();

        auto renderEntity = [this](auto& renderable, const auto& transform, const entt::entity entity) {
            if (!renderable.active) return;

            if (renderable.reqShaderUpdate) renderable.reqShaderUpdate(entity);

            renderable.GetModel()->Draw(
                transform.GetWorldPos(),
                transform.GetWorldRot(),
                transform.GetScale(),
                renderable.hint);
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
            for (auto [entity, renderable, transform] : view.each()) draw(renderable, transform, entity);
        };

        // Pass order is intentional: ordinary forward geometry first, then dynamic geometry,
        // uber-shader objects, deferred objects, and finally dynamic deferred objects.
        drawAll(normalView, renderEntity);
        drawAll(dynamicView, renderDynamicEntity);

        for (auto entity : uberView)
        {
            auto& renderable = uberView.get<Renderable>(entity);
            if (!renderable.active) continue;

            const auto& transform = uberView.get<sgTransform>(entity);
            auto& uber = uberView.get<UberShaderComponent>(entity);
            if (renderable.reqShaderUpdate) renderable.reqShaderUpdate(entity);

            renderable.GetModel()->DrawUber(
                &uber,
                transform.GetWorldPos(),
                transform.GetWorldRot(),
                transform.GetScale(),
                renderable.hint);
        }

        drawAll(deferredView, renderEntity);
        drawAll(dynamicDeferredView, renderDynamicEntity);
    }

    RenderSystem::RenderSystem(entt::registry* _registry) : registry(_registry)
    {
    }
} // namespace sage

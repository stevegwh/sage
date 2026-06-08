#include "EditorPickingService.hpp"

#include "EditorTransformMath.hpp"
#include "engine/Camera.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Settings.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/components/TriggerVolume.hpp"
#include "engine/systems/CollisionSystem.hpp"

#include "raymath.h"

#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace sage::editor
{
    namespace
    {
        // Editor-only convenience: the large ground/terrain mesh is tagged with "_MAPBASE_"
        // in its transform name (see EditorMapLoader). It almost always sits under the cursor,
        // so we deprioritise it during picking to avoid grabbing it instead of props on top.
        bool isMapBaseTransform(const sgTransform& transform)
        {
            return transform.name.find("_MAPBASE_") != std::string::npos;
        }
    } // namespace

    EditorPickingService::EditorPickingService(EngineSystems* _sys) : sys(_sys)
    {
    }

    std::optional<entt::entity> EditorPickingService::PickSceneEntity(
        const Vector2 screenPosition,
        const entt::entity ignoredEntity) const
    {
        if (!sys->settings->IsPointInRenderViewport(screenPosition)) return std::nullopt;

        const auto viewport = sys->settings->GetRenderViewPort();
        const auto renderPosition = sys->settings->ScreenToRenderViewportPosition(screenPosition);
        const auto ray = GetScreenToWorldRayEx(renderPosition, *sys->camera->getRaylibCam(), viewport.x, viewport.y);
        auto collisions = sys->collisionSystem->GetCollisionsWithRay(ray, CollisionMask{~0ull});

        std::vector<CollisionInfo> objectHits;
        std::vector<CollisionInfo> fallbackHits;

        for (auto collision : collisions)
        {
            const auto entity = collision.collidedEntityId;
            if (entity == entt::null || entity == ignoredEntity || !sys->registry->valid(entity) ||
                !sys->registry->any_of<sgTransform>(entity))
            {
                continue;
            }

            if (sys->registry->any_of<Renderable>(entity))
            {
                const auto& renderable = sys->registry->get<Renderable>(entity);
                const auto* model = renderable.GetModel();
                if (model == nullptr) continue;

                const auto& transform = sys->registry->get<sgTransform>(entity);
                const Matrix entityMatrix =
                    BuildRenderableEntityMatrix(transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
                bool meshHit = false;
                RayCollision closestMeshHit{};
                closestMeshHit.distance = std::numeric_limits<float>::max();

                for (int meshIndex = 0; meshIndex < model->GetMeshCount(); ++meshIndex)
                {
                    const auto meshCollision = model->GetRayMeshCollision(ray, meshIndex, entityMatrix);
                    if (meshCollision.hit && meshCollision.distance < closestMeshHit.distance)
                    {
                        closestMeshHit = meshCollision;
                        meshHit = true;
                    }
                }

                if (!meshHit) continue;
                collision.rlCollision = closestMeshHit;
            }

            if (IsNavigationLayer(collision.collisionLayer) ||
                isMapBaseTransform(sys->registry->get<sgTransform>(entity)))
            {
                fallbackHits.push_back(collision);
            }
            else
            {
                objectHits.push_back(collision);
            }
        }

        // Markers (spawners, trigger volumes) carry no Collideable, so test the ray
        // against their stand-in shapes directly. Keeps the marker components as plain
        // data instead of coupling them to the collision system.
        std::optional<std::pair<entt::entity, float>> markerHit;
        const auto considerMarker = [&](const entt::entity entity, const RayCollision& col) {
            if (!col.hit || entity == ignoredEntity) return;
            if (!markerHit || col.distance < markerHit->second) markerHit = {entity, col.distance};
        };
        for (const auto entity : sys->registry->view<Spawner, sgTransform>())
        {
            const auto position = sys->registry->get<sgTransform>(entity).GetWorldPos();
            considerMarker(entity, GetRayCollisionSphere(ray, position, 0.5f));
        }
        for (const auto entity : sys->registry->view<TriggerVolume, sgTransform>())
        {
            const auto& trigger = sys->registry->get<TriggerVolume>(entity);
            const auto position = sys->registry->get<sgTransform>(entity).GetWorldPos();
            const BoundingBox box{
                Vector3Subtract(position, trigger.halfExtents), Vector3Add(position, trigger.halfExtents)};
            considerMarker(entity, GetRayCollisionBox(ray, box));
        }

        CollisionSystem::SortCollisionsByDistance(objectHits);
        const float objectDistance =
            objectHits.empty() ? std::numeric_limits<float>::max() : objectHits.front().rlCollision.distance;

        // Markers win ties so small handles aren't swallowed by a surface at the same depth.
        if (markerHit && markerHit->second <= objectDistance) return markerHit->first;
        if (!objectHits.empty()) return objectHits.front().collidedEntityId;
        if (markerHit) return markerHit->first;

        if (fallbackHits.empty()) return std::nullopt;
        CollisionSystem::SortCollisionsByDistance(fallbackHits);
        return fallbackHits.front().collidedEntityId;
    }
} // namespace sage::editor

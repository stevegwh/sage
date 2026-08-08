//
// Created by Steve Wheeler on 18/02/2024.
//
#include "CollisionSystem.hpp"

#include "components/CollisionIntent.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include <Serializer.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace sage
{
    namespace
    {
        template <typename Test>
        std::vector<CollisionInfo> CollectCollisions(
            entt::registry& registry,
            const CollisionMask mask,
            const entt::entity caster,
            Test&& test,
            const bool firstOnly = false)
        {
            std::vector<CollisionInfo> collisions;
            for (const auto view = registry.view<Collideable>(); const auto entity : view)
            {
                const auto& collideable = view.get<Collideable>(entity);
                if (!collideable.active || entity == caster || !mask.Contains(collideable.collisionLayer)) continue;

                const std::optional<RayCollision> hit = test(entity, collideable);
                if (!hit.has_value()) continue;
                collisions.push_back({entity, collideable.worldBoundingBox, *hit, collideable.collisionLayer});
                if (firstOnly) break;
            }
            return collisions;
        }

        Vector3 BoxCenter(const BoundingBox& box)
        {
            return Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
        }
    } // namespace

    CollisionMask CollisionSystem::ResolveQueryMask(const CollisionLayer layer) const
    {
        return matrix.GetMask(layer);
    }

    void CollisionSystem::Update()
    {
        // Static collideables opt out via the static bool — their worldBoundingBox
        // was baked at construction and never needs recomputing unless a
        // transient static override asks us to refresh it anyway.
        auto view = registry->view<sgTransform, Collideable>();
        view.each([this](const entt::entity entity, const sgTransform& t, Collideable& c) {
            const bool forceRefresh = registry->any_of<CollideableStaticOverride>(entity);
            if (c.isStatic && !forceRefresh) return;
            c.worldBoundingBox = forceRefresh ? TransformBoundingBoxByCorners(c.localBoundingBox, t.GetMatrix())
                                              : TransformAabbNoRotation(c.localBoundingBox, t.GetMatrixNoRot());
        });

        UpdateTriggers();
    }

    void CollisionSystem::UpdateTriggers()
    {
        std::unordered_map<entt::entity, std::unordered_set<entt::entity>> current;

        for (auto view = registry->view<Collideable, TriggerVolume>(); const auto trigger : view)
        {
            const auto& c = view.get<Collideable>(trigger);
            const auto& triggerVolume = view.get<TriggerVolume>(trigger);
            if (!triggerVolume.active || !c.active) continue;

            auto& currentSet = current[trigger];
            const auto& previousSet = triggerOverlaps[trigger];

            for (const auto& hit : GetCollisionsWithBoundingBox(c.worldBoundingBox, triggerVolume.overlapMask))
            {
                const auto other = hit.collidedEntityId;
                if (other == trigger) continue;

                currentSet.insert(other);
                if (previousSet.contains(other))
                    onTriggerStay.Publish(trigger, other);
                else
                    onTriggerEnter.Publish(trigger, other);
            }
        }

        // Anything in a trigger's previous set but not its current set has exited (covers
        // triggers gone inactive/invalid and entities destroyed while inside).
        for (const auto& [trigger, previousSet] : triggerOverlaps)
        {
            const auto it = current.find(trigger);
            for (const auto other : previousSet)
            {
                const bool stillInside = it != current.end() && it->second.contains(other);
                if (!stillInside) onTriggerExit.Publish(trigger, other);
            }
        }

        triggerOverlaps = std::move(current);
    }

    void CollisionSystem::SortCollisionsByDistance(std::vector<CollisionInfo>& collisions)
    {
        std::ranges::sort(collisions, [](const CollisionInfo& a, const CollisionInfo& b) {
            return a.rlCollision.distance < b.rlCollision.distance;
        });
    }

    std::vector<CollisionInfo> CollisionSystem::GetCollisionsWithBoundingBox(
        const BoundingBox& bb, CollisionLayer layer)
    {
        return GetCollisionsWithBoundingBox(bb, ResolveQueryMask(layer));
    }

    std::vector<CollisionInfo> CollisionSystem::GetCollisionsWithBoundingBox(
        const BoundingBox& bb, CollisionMask mask)
    {
        const Vector3 queryCenter = BoxCenter(bb);
        auto collisions = CollectCollisions(*registry, mask, entt::null, [&](entt::entity, const Collideable& c) {
            if (!CheckCollisionBoxes(bb, c.worldBoundingBox)) return std::optional<RayCollision>{};
            RayCollision hit{.hit = true};
            hit.distance = Vector3Distance(queryCenter, BoxCenter(c.worldBoundingBox));
            return std::optional{hit};
        });
        SortCollisionsByDistance(collisions);
        return collisions;
    }

    std::vector<CollisionInfo> CollisionSystem::GetCollisionsWithRay(const Ray& ray, CollisionLayer layer)
    {
        return GetCollisionsWithRay(entt::null, ray, layer);
    }

    std::vector<CollisionInfo> CollisionSystem::GetCollisionsWithRay(const Ray& ray, CollisionMask mask)
    {
        return GetCollisionsWithRay(entt::null, ray, mask);
    }

    std::vector<CollisionInfo> CollisionSystem::GetCollisionsWithRay(
        const entt::entity& caster, const Ray& ray, CollisionLayer layer)
    {
        return GetCollisionsWithRay(caster, ray, ResolveQueryMask(layer));
    }

    std::vector<CollisionInfo> CollisionSystem::GetCollisionsWithRay(
        const entt::entity& caster, const Ray& ray, CollisionMask mask)
    {
        auto collisions = CollectCollisions(*registry, mask, caster, [&](entt::entity, const Collideable& c) {
            const auto hit = GetRayCollisionBox(ray, c.worldBoundingBox);
            return hit.hit ? std::optional{hit} : std::nullopt;
        });
        SortCollisionsByDistance(collisions);
        return collisions;
    }

    bool CollisionSystem::GetFirstCollisionWithRay(const Ray& ray, CollisionInfo& info, CollisionLayer layer) const
    {
        return GetFirstCollisionWithRay(ray, info, ResolveQueryMask(layer));
    }

    bool CollisionSystem::GetFirstCollisionWithRay(const Ray& ray, CollisionInfo& info, CollisionMask mask) const
    {
        const auto collisions = CollectCollisions(
            *registry,
            mask,
            entt::null,
            [&](entt::entity, const Collideable& c) {
                const auto hit = GetRayCollisionBox(ray, c.worldBoundingBox);
                return hit.hit ? std::optional{hit} : std::nullopt;
            },
            true);
        if (collisions.empty()) return false;
        info = collisions.front();
        return true;
    }

    std::vector<CollisionInfo> CollisionSystem::GetMeshCollisionsWithRay(
        const entt::entity& caster, const Ray& ray, CollisionLayer layer)
    {
        return GetMeshCollisionsWithRay(caster, ray, ResolveQueryMask(layer));
    }

    std::vector<CollisionInfo> CollisionSystem::GetMeshCollisionsWithRay(
        const entt::entity& caster, const Ray& ray, CollisionMask mask)
    {
        auto collisions = CollectCollisions(*registry, mask, caster, [&](const entt::entity entity, const Collideable& c) {
            if (c.shape != ColliderShape::RenderMesh ||
                !registry->all_of<Renderable, sgTransform>(entity))
                return std::optional<RayCollision>{};
            auto& renderable = registry->get<Renderable>(entity);
            const auto& transform = registry->get<sgTransform>(entity);
            const auto hit = renderable.GetModel()->GetRayMeshCollision(ray, 0, transform.GetMatrix());
            return hit.hit ? std::optional{hit} : std::nullopt;
        });
        SortCollisionsByDistance(collisions);
        return collisions;
    }

    void CollisionSystem::DrawDebug() const
    {
        const auto view = registry->view<Collideable>();
        for (const auto entity : view)
        {
            const auto& c = registry->get<Collideable>(entity);
            if (!c.active) continue;
            const bool draw =
                c.debugDraw || registry->any_of<NavigationSurface, NavigationObstacle, TriggerVolume>(entity);
            if (draw)
            {
                auto col = ORANGE;
                if (registry->any_of<NavigationSurface>(entity)) col = GREEN;
                if (registry->any_of<NavigationObstacle>(entity)) col = YELLOW;
                if (registry->any_of<TriggerVolume>(entity)) col = BLUE;
                DrawBoundingBox(c.worldBoundingBox, col);
            }
        }
    }

    void CollisionSystem::BoundingBoxDraw(entt::entity entityId, Color color) const
    {
        auto& col = registry->get<Collideable>(entityId);
        Vector3 min = col.worldBoundingBox.min;
        Vector3 max = col.worldBoundingBox.max;

        // Calculate the center of the bounding box
        Vector3 center = {(min.x + max.x) / 2, (min.y + max.y) / 2, (min.z + max.z) / 2};

        // Calculate dimensions
        float width = max.x - min.x;
        float height = max.y - min.y;
        float depth = max.z - min.z;

        color.a = 100;

        // Draw the cube at the calculated center with the correct dimensions
        DrawCube(center, width, height, depth, color);
    }

    bool CollisionSystem::CheckBoxCollision(const BoundingBox& col1, const BoundingBox& col2)
    {
        return CheckCollisionBoxes(col1, col2);
    }

    bool CollisionSystem::GetFirstCollisionBB(
        entt::entity caller, BoundingBox bb, CollisionLayer layer, CollisionInfo& out)
    {
        return GetFirstCollisionBB(caller, bb, ResolveQueryMask(layer), out);
    }

    bool CollisionSystem::GetFirstCollisionBB(
        entt::entity caller, BoundingBox bb, CollisionMask mask, CollisionInfo& out) const
    {
        const auto collisions = CollectCollisions(
            *registry,
            mask,
            caller,
            [&](entt::entity, const Collideable& col) {
                return CheckBoxCollision(bb, col.worldBoundingBox)
                           ? std::optional{RayCollision{.hit = true}}
                           : std::nullopt;
            },
            true);
        if (collisions.empty()) return false;
        out = collisions.front();
        return true;
    }

    CollisionSystem::CollisionSystem(entt::registry* _registry) : registry(_registry)
    {
        matrix.Load();
    }

} // namespace sage

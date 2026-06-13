//
// Created by Steve Wheeler on 18/02/2024.
//

#pragma once

#include "engine/CollisionMatrix.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/Event.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sage
{
    struct CollisionInfo
    {
        entt::entity collidedEntityId{};
        BoundingBox collidedBB{};
        RayCollision rlCollision{};
        CollisionLayer collisionLayer{};
    };

    class CollisionSystem
    {
        entt::registry* registry;
        [[nodiscard]] CollisionMask ResolveQueryMask(CollisionLayer layer) const;

        // Entities overlapping each TriggerVolume last frame, for enter/exit diffing.
        std::unordered_map<entt::entity, std::unordered_set<entt::entity>> triggerOverlaps;
        void UpdateTriggers();

      public:
        // Unity-style layer collision matrix, loaded from CollisionMatrix::DEFAULT_PATH
        // at construction. Layer-based queries and trigger overlap checks resolve their
        // masks through it; the editor's Collision Matrix window edits and saves it.
        CollisionMatrix matrix;

        // Trigger callbacks, fired by UpdateTriggers() for entities with Collideable +
        // TriggerVolume. Each passes (trigger, other): the trigger entity and the entity
        // overlapping it. Subscribe via the Event<>/Subscription pattern.
        Event<entt::entity, entt::entity> onTriggerEnter;
        Event<entt::entity, entt::entity> onTriggerStay;
        Event<entt::entity, entt::entity> onTriggerExit;

        // Recomputes worldBoundingBox for every dynamic Collideable from its sgTransform,
        // then diffs trigger overlaps and fires the onTrigger* events. Static collideables
        // are not refreshed (their world bbox is baked at construction). Call once per
        // frame, after positions have been mutated and before any queries.
        void Update();

        static void SortCollisionsByDistance(std::vector<CollisionInfo>& collisions);
        [[nodiscard]] std::vector<CollisionInfo> GetMeshCollisionsWithRay(
            const entt::entity& caster, const Ray& ray, CollisionLayer layer);
        [[nodiscard]] std::vector<CollisionInfo> GetMeshCollisionsWithRay(
            const entt::entity& caster, const Ray& ray, CollisionMask mask);
        [[nodiscard]] std::vector<CollisionInfo> GetCollisionsWithRay(
            const entt::entity& caster, const Ray& ray, CollisionLayer layer = sage::collision_layers::Default);
        [[nodiscard]] std::vector<CollisionInfo> GetCollisionsWithRay(
            const entt::entity& caster, const Ray& ray, CollisionMask mask);
        [[nodiscard]] std::vector<CollisionInfo> GetCollisionsWithRay(
            const Ray& ray, CollisionLayer layer = sage::collision_layers::Default);
        [[nodiscard]] std::vector<CollisionInfo> GetCollisionsWithRay(const Ray& ray, CollisionMask mask);
        [[nodiscard]] bool GetFirstCollisionWithRay(
            const Ray& ray, CollisionInfo& info, CollisionLayer layer = sage::collision_layers::Default) const;
        [[nodiscard]] bool GetFirstCollisionWithRay(const Ray& ray, CollisionInfo& info, CollisionMask mask) const;
        [[nodiscard]] std::vector<CollisionInfo> GetCollisionsWithBoundingBox(
            const BoundingBox& bb, CollisionLayer layer = sage::collision_layers::Default);
        [[nodiscard]] std::vector<CollisionInfo> GetCollisionsWithBoundingBox(
            const BoundingBox& bb, CollisionMask mask);
        void BoundingBoxDraw(entt::entity entityId, Color color = LIME) const;
        static bool CheckBoxCollision(const BoundingBox& col1, const BoundingBox& col2);
        bool GetFirstCollisionBB(entt::entity caller, BoundingBox bb, CollisionLayer layer, CollisionInfo& out);
        bool GetFirstCollisionBB(
            entt::entity caller, BoundingBox bb, CollisionMask mask, CollisionInfo& out) const;
        void DrawDebug() const;
        explicit CollisionSystem(entt::registry* _registry);
    };
} // namespace sage

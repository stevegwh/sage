//
// Created by Steve Wheeler on 18/02/2024.
//

#pragma once

#include "../engine_config.hpp"

#include "cereal/cereal.hpp"
#include "entt/entt.hpp"

#include "raylib.h"
#include "raymath.h"

namespace sage
{
    class Collideable
    {
        entt::registry* registry{};

      public:
        bool active = true;
        void OnTransformUpdate(entt::entity entity);
        BoundingBox localBoundingBox{}; // BoundingBox in local space
        BoundingBox worldBoundingBox{}; // BoundingBox in world space (bb * world mat)

        CollisionLayer collisionLayer = CollisionLayer::DEFAULT;
        bool debugDraw = false;

        void SetWorldBoundingBox(Matrix mat);
        void Enable();
        void Disable();

        // Static, non-moveable object
        explicit Collideable(const BoundingBox& _localBoundingBox, const Matrix& worldMatrix);
        // Moveable object
        Collideable(entt::registry* _registry, entt::entity _self, BoundingBox _localBoundingBox);
        // Default constructor required for serialization, should *not* be used outside of this.
        Collideable() = default;

        Collideable(const Collideable&) = delete;
        Collideable& operator=(const Collideable&) = delete;

        template <class Archive>
        void save(Archive& archive) const
        {
            archive(localBoundingBox, worldBoundingBox, collisionLayer);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            archive(localBoundingBox, worldBoundingBox, collisionLayer);
        }
    };
} // namespace sage

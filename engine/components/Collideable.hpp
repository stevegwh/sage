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
    struct Collideable
    {
        BoundingBox localBoundingBox{};
        BoundingBox worldBoundingBox{};
        CollisionLayer collisionLayer = CollisionLayer::DEFAULT;
        bool active = true;
        bool debugDraw = false;

        Collideable() = default;
        Collideable(const BoundingBox& local, const Matrix& worldMat);

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

    // Empty tag. Attach alongside Collideable to opt out of CollisionSystem::Update —
    // the entity's worldBoundingBox is baked at construction (or load) and never
    // recomputed. Matches the RenderableDeferred convention.
    struct StaticCollideable
    {
    };

    // Transforms a bounding box by a world matrix.
    BoundingBox TransformBoundingBox(const BoundingBox& local, const Matrix& worldMat);
} // namespace sage

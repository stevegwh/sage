//
// Created by Steve Wheeler on 18/02/2024.
//

#pragma once

#include "../CollisionLayers.hpp"

#include "cereal/cereal.hpp"
#include "entt/entt.hpp"

#include "raylib.h"
#include "raymath.h"

#include <cstdint>

namespace sage
{
    enum class ColliderShape
    {
        Box,
        RenderMesh
    };

    struct Collideable
    {
        BoundingBox localBoundingBox{};
        BoundingBox worldBoundingBox{};
        CollisionLayer collisionLayer = sage::collision_layers::Default;
        ColliderShape shape = ColliderShape::Box;
        bool active = true;
        bool isStatic = false;
        bool debugDraw = false;

        Collideable() = default;
        Collideable(const BoundingBox& local, const Matrix& worldMat);
        // What a layer collides with is decided globally by CollisionSystem's
        // CollisionMatrix, not per-collideable.
        void SetCollisionLayer(CollisionLayer layer);

        template <class Archive>
        void save(Archive& archive) const
        {
            const auto shapeValue = static_cast<std::uint8_t>(shape);
            archive(localBoundingBox, worldBoundingBox, collisionLayer, shapeValue);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            std::uint8_t shapeValue = 0;
            archive(localBoundingBox, worldBoundingBox, collisionLayer, shapeValue);
            shape = static_cast<ColliderShape>(shapeValue);
            collisionLayer.layerName = GetCollisionLayerName(collisionLayer.bit);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            const bool meshCollider = shape == ColliderShape::RenderMesh;
            i.field("Active", active);
            i.field("IsStatic", isStatic);
            i.field("Debug Draw", debugDraw);
            i.field("Shape", shape);
            i.field("Collision Layer", collisionLayer);
            // A mesh collider's box must enclose the meshes or rays never reach
            // them, so hand edits are disabled.
            i.field("Local Bounds", localBoundingBox, !meshCollider);
            // i.field("World Bounds", worldBoundingBox, false);
        }
    };

    // Transient ECS tag: when present, CollisionSystem ignores Collideable::isStatic
    // for bounds refreshes without changing the authored static flag.
    struct CollideableStaticOverride
    {
    };

    // Transforms a bounding box by a world matrix.
    BoundingBox TransformBoundingBox(const BoundingBox& local, const Matrix& worldMat);
    BoundingBox TransformBoundingBoxByCorners(const BoundingBox& local, const Matrix& worldMat);
} // namespace sage

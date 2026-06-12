//
// Created by Steve Wheeler on 18/02/2024.
//

#pragma once

#include "../CollisionLayers.hpp"

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
        CollisionLayer collisionLayer = sage::collision_layers::Default;
        bool active = true;
        bool isStatic = false;
        bool debugDraw = false;
        bool blocksNavigation = false;
        // Unity-style trigger: the box reports overlaps (CollisionSystem fires
        // onTriggerEnter/Stay/Exit) but never contributes physical/navigation blocking.
        bool isTrigger = false;

        Collideable() = default;
        Collideable(const BoundingBox& local, const Matrix& worldMat);
        // What a layer collides with is decided globally by CollisionSystem's
        // CollisionMatrix, not per-collideable.
        void SetCollisionLayer(CollisionLayer layer);

        template <class Archive>
        void save(Archive& archive) const
        {
            archive(localBoundingBox, worldBoundingBox, collisionLayer, isTrigger);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            archive(localBoundingBox, worldBoundingBox, collisionLayer, isTrigger);
            collisionLayer.layerName = GetCollisionLayerName(collisionLayer.bit);
        }

        template <class Inspector>
        void define_editor_fields(Inspector& i)
        {
            // Derived from the layer, not stored: GeometryComplex/Stairs rays
            // refine against the model's meshes; the box is only the broad-phase.
            const bool meshCollider = RequiresMeshCollision(collisionLayer);
            i.note("Collider", meshCollider ? "Mesh (from layer; box is broad-phase)" : "Box");
            i.field("Active", active);
            i.field("IsStatic", isStatic);
            i.field("Debug Draw", debugDraw);
            i.field("Blocks Navigation", blocksNavigation);
            i.field("Is Trigger", isTrigger);
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

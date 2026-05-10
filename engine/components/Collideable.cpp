//
// Created by Steve Wheeler on 02/05/2024.
//

#include "Collideable.hpp"

#include "raymath.h"

namespace sage
{
    BoundingBox TransformBoundingBox(const BoundingBox& local, const Matrix& worldMat)
    {
        return {Vector3Transform(local.min, worldMat), Vector3Transform(local.max, worldMat)};
    }

    Collideable::Collideable(const BoundingBox& local, const Matrix& worldMat)
    {
        localBoundingBox = local;
        worldBoundingBox = TransformBoundingBox(local, worldMat);
    }

    void Collideable::SetCollisionLayer(const CollisionLayer layer, const CollisionMask mask)
    {
        collisionLayer = layer;
        collidesWith = mask.IsEmpty() ? GetDefaultCollisionMask(layer) : mask;
    }
} // namespace sage

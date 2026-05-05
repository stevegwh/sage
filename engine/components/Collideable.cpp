//
// Created by Steve Wheeler on 02/05/2024.
//

#include "Collideable.hpp"

#include "raymath.h"

namespace sage
{
    BoundingBox TransformBoundingBox(BoundingBox local, Matrix worldMat)
    {
        return {Vector3Transform(local.min, worldMat), Vector3Transform(local.max, worldMat)};
    }

    Collideable::Collideable(BoundingBox local, Matrix worldMat)
    {
        localBoundingBox = local;
        worldBoundingBox = TransformBoundingBox(local, worldMat);
    }
} // namespace sage

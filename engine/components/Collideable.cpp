//
// Created by Steve Wheeler on 02/05/2024.
//

#include "Collideable.hpp"

#include "raymath.h"

#include <algorithm>
#include <array>

namespace sage
{
    BoundingBox TransformAabbNoRotation(const BoundingBox& local, const Matrix& worldMat)
    {
        return {.min = Vector3Transform(local.min, worldMat), .max = Vector3Transform(local.max, worldMat)};
    }

    BoundingBox TransformBoundingBoxByCorners(const BoundingBox& local, const Matrix& worldMat)
    {
        const std::array<Vector3, 8> corners = {
            Vector3{.x = local.min.x, .y = local.min.y, .z = local.min.z},
            Vector3{.x = local.min.x, .y = local.min.y, .z = local.max.z},
            Vector3{.x = local.min.x, .y = local.max.y, .z = local.min.z},
            Vector3{.x = local.min.x, .y = local.max.y, .z = local.max.z},
            Vector3{.x = local.max.x, .y = local.min.y, .z = local.min.z},
            Vector3{.x = local.max.x, .y = local.min.y, .z = local.max.z},
            Vector3{.x = local.max.x, .y = local.max.y, .z = local.min.z},
            Vector3{.x = local.max.x, .y = local.max.y, .z = local.max.z},
        };

        BoundingBox transformed{};
        transformed.min = transformed.max = Vector3Transform(corners.front(), worldMat);

        for (const auto& corner : corners)
        {
            const auto worldCorner = Vector3Transform(corner, worldMat);
            transformed.min.x = std::min(transformed.min.x, worldCorner.x);
            transformed.min.y = std::min(transformed.min.y, worldCorner.y);
            transformed.min.z = std::min(transformed.min.z, worldCorner.z);
            transformed.max.x = std::max(transformed.max.x, worldCorner.x);
            transformed.max.y = std::max(transformed.max.y, worldCorner.y);
            transformed.max.z = std::max(transformed.max.z, worldCorner.z);
        }

        return transformed;
    }

    Collideable::Collideable(const BoundingBox& local, const Matrix& worldMat)
        : localBoundingBox(local), worldBoundingBox(TransformAabbNoRotation(local, worldMat))
    {
    }

} // namespace sage

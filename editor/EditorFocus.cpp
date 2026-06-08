#include "EditorFocus.hpp"

#include "EditorTransformMath.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"

#include "raymath.h"

#include <algorithm>

namespace sage::editor
{
    namespace
    {
        FocusTarget focusTargetFromBounds(const BoundingBox& bounds)
        {
            const Vector3 center = BoundingBoxCenter(bounds);
            const Vector3 halfSize = Vector3Scale(Vector3Subtract(bounds.max, bounds.min), 0.5f);
            return {.position = center, .radius = std::max(1.0f, Vector3Length(halfSize))};
        }

        BoundingBox boundsFromPoint(const Vector3 point)
        {
            return BoundingBox{.min = point, .max = point};
        }

        void expandBounds(BoundingBox& bounds, const BoundingBox& other)
        {
            bounds.min.x = std::min(bounds.min.x, other.min.x);
            bounds.min.y = std::min(bounds.min.y, other.min.y);
            bounds.min.z = std::min(bounds.min.z, other.min.z);
            bounds.max.x = std::max(bounds.max.x, other.max.x);
            bounds.max.y = std::max(bounds.max.y, other.max.y);
            bounds.max.z = std::max(bounds.max.z, other.max.z);
        }

        std::optional<BoundingBox> focusBoundsForEntity(entt::registry& registry, const entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.any_of<sgTransform>(entity)) return std::nullopt;

            if (registry.any_of<Collideable>(entity))
            {
                return registry.get<Collideable>(entity).worldBoundingBox;
            }

            if (registry.any_of<Renderable>(entity))
            {
                const auto& transform = registry.get<sgTransform>(entity);
                const auto& renderable = registry.get<Renderable>(entity);
                if (const auto* model = renderable.GetModel(); model != nullptr)
                {
                    const Matrix entityMatrix = BuildRenderableEntityMatrix(
                        transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
                    const Matrix worldMatrix = MatrixMultiply(model->GetTransform(), entityMatrix);
                    return TransformBoundingBoxByCorners(model->CalcLocalBoundingBox(), worldMatrix);
                }
            }

            return boundsFromPoint(registry.get<sgTransform>(entity).GetWorldPos());
        }
    } // namespace

    std::optional<FocusTarget> ComputeFocusTarget(
        entt::registry& registry, const std::vector<entt::entity>& entities)
    {
        std::optional<BoundingBox> combinedBounds;
        for (const auto entity : entities)
        {
            const auto bounds = focusBoundsForEntity(registry, entity);
            if (!bounds.has_value()) continue;

            if (combinedBounds.has_value())
                expandBounds(*combinedBounds, *bounds);
            else
                combinedBounds = bounds;
        }

        if (!combinedBounds.has_value()) return std::nullopt;
        return focusTargetFromBounds(*combinedBounds);
    }
} // namespace sage::editor

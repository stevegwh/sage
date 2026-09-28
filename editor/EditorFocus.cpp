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
            if (!registry.valid(entity)) return std::nullopt;
            const auto* transform = registry.try_get<sgTransform>(entity);
            if (!transform) return std::nullopt;

            if (const auto* renderable = registry.try_get<Renderable>(entity))
            {
                if (const auto* model = renderable->GetModel())
                {
                    const Matrix entityMatrix = BuildRenderableEntityMatrix(
                        transform->GetWorldPos(), transform->GetWorldRot(), transform->GetScale());
                    // CalcLocalBoundingBox already includes the model's local transform.
                    return TransformBoundingBoxByCorners(model->CalcLocalBoundingBox(), entityMatrix);
                }
            }

            if (const auto* collider = registry.try_get<Collideable>(entity))
            {
                return collider->worldBoundingBox;
            }

            const auto point = transform->GetWorldPos();
            return BoundingBox{.min = point, .max = point};
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
        const Vector3 halfSize = Vector3Scale(Vector3Subtract(combinedBounds->max, combinedBounds->min), 0.5f);
        return FocusTarget{.position = BoundingBoxCenter(*combinedBounds),
                           .radius = std::max(1.0f, Vector3Length(halfSize))};
    }
} // namespace sage::editor

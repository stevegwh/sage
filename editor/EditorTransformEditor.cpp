//
// EditorTransformEditor implementation. See EditorTransformEditor.hpp.
//

#include "EditorTransformEditor.hpp"

#include "EditorTransformMath.hpp"
#include "engine/Camera.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Settings.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/systems/NavigationGridSystem.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace sage::editor
{
    namespace
    {
        // Mirrors the floor used elsewhere in the editor when treating uniform
        // scale as a single number. Kept local; placement-grid extraction will
        // eventually centralise this.
        constexpr float MIN_SCALE = 0.1f;

        BoundingBox BoundsFromPoint(const Vector3 point)
        {
            return BoundingBox{.min = point, .max = point};
        }

        void ExpandBounds(BoundingBox& bounds, const BoundingBox& other)
        {
            bounds.min.x = std::min(bounds.min.x, other.min.x);
            bounds.min.y = std::min(bounds.min.y, other.min.y);
            bounds.min.z = std::min(bounds.min.z, other.min.z);
            bounds.max.x = std::max(bounds.max.x, other.max.x);
            bounds.max.y = std::max(bounds.max.y, other.max.y);
            bounds.max.z = std::max(bounds.max.z, other.max.z);
        }

        std::optional<BoundingBox> EntityBounds(entt::registry& registry, const entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.any_of<sgTransform>(entity)) return std::nullopt;

            if (registry.any_of<Renderable>(entity))
            {
                const auto& transform = registry.get<sgTransform>(entity);
                const auto& renderable = registry.get<Renderable>(entity);
                if (const auto* model = renderable.GetModel(); model != nullptr)
                {
                    const Matrix entityMatrix = BuildRenderableEntityMatrix(
                        transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
                    return TransformBoundingBoxByCorners(model->CalcLocalBoundingBox(), entityMatrix);
                }
            }

            if (registry.any_of<Collideable>(entity))
            {
                return registry.get<Collideable>(entity).worldBoundingBox;
            }

            return BoundsFromPoint(registry.get<sgTransform>(entity).GetWorldPos());
        }

        void AppendSubtreeBounds(
            entt::registry& registry, const entt::entity entity, std::optional<BoundingBox>& bounds)
        {
            if (!registry.valid(entity) || !registry.any_of<sgTransform>(entity)) return;

            if (const auto entityBounds = EntityBounds(registry, entity); entityBounds.has_value())
            {
                if (bounds.has_value())
                    ExpandBounds(*bounds, *entityBounds);
                else
                    bounds = entityBounds;
            }

            for (const auto child : registry.get<sgTransform>(entity).GetChildren())
            {
                AppendSubtreeBounds(registry, child, bounds);
            }
        }

        bool HasValidTransform(entt::registry& registry, const std::vector<entt::entity>& entities)
        {
            return std::ranges::any_of(entities, [&registry](const entt::entity entity) {
                return registry.valid(entity) && registry.any_of<sgTransform>(entity);
            });
        }

        float UniformScale(const Vector3 scale)
        {
            return std::max(MIN_SCALE, (scale.x + scale.y + scale.z) / 3.0f);
        }
    } // namespace

    EditorTransformEditor::EditorTransformEditor(EngineSystems* _sys, OnApplied _onApplied)
        : sys(_sys), onApplied(std::move(_onApplied))
    {
    }

    void EditorTransformEditor::ExitEditMode()
    {
        if (gizmo.IsDragging())
        {
            sys->camera->UnlockInput();
        }
        gizmo.EndDrag();
    }

    void EditorTransformEditor::Update(const std::vector<entt::entity>& entities)
    {
        if (!gizmo.IsDragging()) return;

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || !HasValidTransform(*sys->registry, entities))
        {
            ExitEditMode();
            return;
        }

        const Camera3D camera = *sys->camera->getRaylibCam();
        const Vector2 viewport = sys->settings->GetRenderViewPort();
        const Vector2 mousePosition = sys->settings->ScreenToRenderViewportPosition(GetMousePosition());
        const Vector3 origin = PivotWorldPosition(entities);

        const auto sample = gizmo.SampleDrag(camera, viewport, origin, mode, mousePosition);
        if (sample.axis == EditGizmo::Axis::None) return;

        switch (mode)
        {
        case EditGizmo::Mode::Translate:
        {
            // Convert the projected screen-pixel delta back into world units by
            // walking the same screen-axis-length the gizmo used for projection.
            const Vector3 axisVector = EditGizmo::AxisVector(sample.axis);
            const float size = EditGizmo::SizeForCamera(camera.position, origin);
            const Vector2 screenStart = GetWorldToScreenEx(
                origin, camera, static_cast<int>(viewport.x), static_cast<int>(viewport.y));
            const Vector2 screenEnd = GetWorldToScreenEx(
                Vector3Add(origin, Vector3Scale(axisVector, size)),
                camera,
                static_cast<int>(viewport.x),
                static_cast<int>(viewport.y));
            const float screenLength = Vector2Length(Vector2Subtract(screenEnd, screenStart));
            if (screenLength > 0.0001f)
            {
                // Accumulate sub-grid drag motion in dragUnsnappedPosition so we don't lose
                // precision to per-frame snap rounding, then commit the snapped result.
                const Vector3 worldDelta =
                    Vector3Scale(axisVector, sample.projectedAxisPixels * size / screenLength);
                dragUnsnappedPivotPosition = Vector3Add(dragUnsnappedPivotPosition, worldDelta);
                const Vector3 snapped = snapToGridXZ(dragUnsnappedPivotPosition);
                const Vector3 appliedDelta = Vector3Subtract(snapped, dragLastSnappedPivotPosition);
                if (!Vector3Equals(appliedDelta, Vector3Zero()))
                {
                    applyPositionDelta(entities, appliedDelta);
                    dragLastSnappedPivotPosition = snapped;
                }
            }
            break;
        }
        case EditGizmo::Mode::Rotate:
            AdjustRotationAxis(entities, sample.axis, sample.rotationDegrees);
            break;
        case EditGizmo::Mode::Scale:
            AdjustScale(entities, sample.projectedAxisPixels * 0.01f);
            break;
        }
    }

    bool EditorTransformEditor::TryStartDrag(
        const std::vector<entt::entity>& entities, const Vector2 mousePosition)
    {
        if (!HasValidTransform(*sys->registry, entities)) return false;
        if (!sys->settings->IsPointInRenderViewport(mousePosition)) return false;

        const Camera3D camera = *sys->camera->getRaylibCam();
        const Vector2 viewport = sys->settings->GetRenderViewPort();
        const Vector2 renderMousePosition = sys->settings->ScreenToRenderViewportPosition(mousePosition);
        const Vector3 origin = PivotWorldPosition(entities);

        const auto axis = gizmo.HitTest(camera, viewport, origin, mode, renderMousePosition);
        if (axis == EditGizmo::Axis::None) return false;

        gizmo.BeginDrag(axis, renderMousePosition);
        dragUnsnappedPivotPosition = origin;
        dragLastSnappedPivotPosition = snapToGridXZ(origin);
        sys->camera->LockInput();
        return true;
    }

    void EditorTransformEditor::Draw3D(const std::vector<entt::entity>& entities) const
    {
        if (!HasValidTransform(*sys->registry, entities)) return;

        const Camera3D camera = *sys->camera->getRaylibCam();
        const Vector2 viewport = sys->settings->GetRenderViewPort();
        const Vector3 origin = PivotWorldPosition(entities);
        gizmo.Draw(camera, viewport, origin, mode);
    }

    void EditorTransformEditor::SetMode(const EditGizmo::Mode newMode)
    {
        mode = newMode;
    }

    void EditorTransformEditor::TogglePivotMode()
    {
        pivotMode = pivotMode == PivotMode::LocalCenter ? PivotMode::World : PivotMode::LocalCenter;
    }

    std::string EditorTransformEditor::DescribeMode() const
    {
        switch (mode)
        {
        case EditGizmo::Mode::Translate:
            return "Translate";
        case EditGizmo::Mode::Rotate:
            return "Rotate";
        case EditGizmo::Mode::Scale:
            return "Scale";
        }
        return "Translate";
    }

    Vector3 EditorTransformEditor::PivotWorldPosition(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) return Vector3Zero();

        const auto& transform = sys->registry->get<sgTransform>(entity);
        if (pivotMode != PivotMode::LocalCenter || !sys->registry->any_of<Renderable>(entity))
        {
            return transform.GetWorldPos();
        }

        const auto& renderable = sys->registry->get<Renderable>(entity);
        const auto* model = renderable.GetModel();
        if (model == nullptr) return transform.GetWorldPos();

        const Matrix entityMatrix = BuildRenderableEntityMatrix(
            transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
        const BoundingBox worldBounds = TransformBoundingBoxByCorners(model->CalcLocalBoundingBox(), entityMatrix);
        return BoundingBoxCenter(worldBounds);
    }

    Vector3 EditorTransformEditor::PivotWorldPosition(const std::vector<entt::entity>& entities) const
    {
        if (entities.empty()) return Vector3Zero();

        if (pivotMode != PivotMode::LocalCenter)
        {
            Vector3 sum{};
            int count = 0;
            for (const auto entity : entities)
            {
                if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) continue;
                sum = Vector3Add(sum, sys->registry->get<sgTransform>(entity).GetWorldPos());
                ++count;
            }
            return count > 0 ? Vector3Scale(sum, 1.0f / static_cast<float>(count)) : Vector3Zero();
        }

        std::optional<BoundingBox> bounds;
        for (const auto entity : entities)
        {
            AppendSubtreeBounds(*sys->registry, entity, bounds);
        }

        if (bounds.has_value()) return BoundingBoxCenter(*bounds);
        for (const auto entity : entities)
        {
            if (sys->registry->valid(entity) && sys->registry->any_of<sgTransform>(entity))
            {
                return PivotWorldPosition(entity);
            }
        }
        return Vector3Zero();
    }

    void EditorTransformEditor::AdjustPosition(
        const std::vector<entt::entity>& entities, const Vector3 worldDelta)
    {
        if (!HasValidTransform(*sys->registry, entities)) return;

        const Vector3 pivot = PivotWorldPosition(entities);
        const Vector3 snappedPivot = snapToGridXZ(Vector3Add(pivot, worldDelta));
        const Vector3 appliedDelta = Vector3Subtract(snappedPivot, pivot);
        if (Vector3Equals(appliedDelta, Vector3Zero())) return;

        applyPositionDelta(entities, appliedDelta);
    }

    Vector3 EditorTransformEditor::snapToGridXZ(const Vector3 worldPos) const
    {
        const float spacing = sys->navigationGridSystem->spacing;
        if (spacing <= 0.0f) return worldPos;
        return {
            (std::floor(worldPos.x / spacing) + 0.5f) * spacing,
            worldPos.y,
            (std::floor(worldPos.z / spacing) + 0.5f) * spacing};
    }

    void EditorTransformEditor::applyPositionDelta(
        const std::vector<entt::entity>& entities, const Vector3 worldDelta)
    {
        entt::entity notifiedEntity = entt::null;
        for (const auto entity : entities)
        {
            if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) continue;

            auto& transform = sys->registry->get<sgTransform>(entity);
            transform.position.world = Vector3Add(transform.GetWorldPos(), worldDelta);
            updateEntityCollisionBounds(entity);
            if (notifiedEntity == entt::null) notifiedEntity = entity;
        }
        notify(notifiedEntity);
    }

    void EditorTransformEditor::AdjustRotationAxis(
        const std::vector<entt::entity>& entities, const EditGizmo::Axis axis, const float amount)
    {
        if (!HasValidTransform(*sys->registry, entities) || axis == EditGizmo::Axis::None ||
            axis == EditGizmo::Axis::Uniform)
        {
            return;
        }

        auto wrapDegrees = [](float degrees) {
            while (degrees >= 360.0f) degrees -= 360.0f;
            while (degrees < 0.0f) degrees += 360.0f;
            return degrees;
        };

        Matrix rotationDelta = MatrixIdentity();

        if (axis == EditGizmo::Axis::X)
        {
            rotationDelta = MatrixRotateX(amount * DEG2RAD);
        }
        else if (axis == EditGizmo::Axis::Z)
        {
            rotationDelta = MatrixRotateZ(amount * DEG2RAD);
        }
        else
        {
            rotationDelta = MatrixRotateY(amount * DEG2RAD);
        }

        const Vector3 pivot = PivotWorldPosition(entities);
        entt::entity notifiedEntity = entt::null;
        for (const auto entity : entities)
        {
            if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) continue;

            auto& transform = sys->registry->get<sgTransform>(entity);
            Vector3 rotation = transform.GetWorldRot();
            const Vector3 offset = Vector3Subtract(transform.GetWorldPos(), pivot);
            const Vector3 newOffset = Vector3Transform(offset, rotationDelta);
            transform.position.world = Vector3Add(pivot, newOffset);

            if (axis == EditGizmo::Axis::X)
                rotation.x = wrapDegrees(rotation.x + amount);
            else if (axis == EditGizmo::Axis::Z)
                rotation.z = wrapDegrees(rotation.z + amount);
            else
                rotation.y = wrapDegrees(rotation.y + amount);

            transform.rotation.world = rotation;
            updateEntityCollisionBounds(entity);
            if (notifiedEntity == entt::null) notifiedEntity = entity;
        }

        notify(notifiedEntity);
    }

    void EditorTransformEditor::AdjustScale(const std::vector<entt::entity>& entities, const float delta)
    {
        if (!HasValidTransform(*sys->registry, entities)) return;

        float totalUniformScale = 0.0f;
        int scaleCount = 0;
        for (const auto entity : entities)
        {
            if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) continue;
            totalUniformScale += UniformScale(sys->registry->get<sgTransform>(entity).GetScale());
            ++scaleCount;
        }
        if (scaleCount == 0) return;

        const float currentUniformScale = totalUniformScale / static_cast<float>(scaleCount);
        const float nextUniformScale = std::max(MIN_SCALE, currentUniformScale + delta);
        const float scaleFactor = nextUniformScale / currentUniformScale;
        const Vector3 pivot = PivotWorldPosition(entities);

        entt::entity notifiedEntity = entt::null;
        for (const auto entity : entities)
        {
            if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) continue;

            auto& transform = sys->registry->get<sgTransform>(entity);
            const Vector3 offset = Vector3Subtract(transform.GetWorldPos(), pivot);
            const Vector3 newOffset = Vector3Scale(offset, scaleFactor);
            transform.position.world = Vector3Add(pivot, newOffset);

            const float entityScale = std::max(MIN_SCALE, UniformScale(transform.GetScale()) * scaleFactor);
            transform.scale.world = {entityScale, entityScale, entityScale};
            updateEntityCollisionBounds(entity);
            if (notifiedEntity == entt::null) notifiedEntity = entity;
        }

        notify(notifiedEntity);
    }

    void EditorTransformEditor::updateEntityCollisionBounds(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity)) return;

        if (sys->registry->all_of<sgTransform, Collideable>(entity))
        {
            const auto& transform = sys->registry->get<sgTransform>(entity);
            auto& collideable = sys->registry->get<Collideable>(entity);
            if (collideable.blocksNavigation)
            {
                sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, false, entity);
            }

            const Matrix entityMatrix = BuildRenderableEntityMatrix(
                transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
            if (sys->registry->any_of<Renderable>(entity))
            {
                const auto& renderable = sys->registry->get<Renderable>(entity);
                if (const auto* model = renderable.GetModel(); model != nullptr)
                {
                    collideable.localBoundingBox = model->CalcLocalBoundingBox();
                }
            }
            collideable.worldBoundingBox =
                TransformBoundingBoxByCorners(collideable.localBoundingBox, entityMatrix);

            if (collideable.blocksNavigation)
            {
                sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);
            }
        }

        // Descendant transforms get propagated by TransformSystem::propagateChildren,
        // but their collideables don't refresh themselves — recurse so child bounds
        // track the new parent rotation/position/scale.
        if (sys->registry->any_of<sgTransform>(entity))
        {
            for (const auto child : sys->registry->get<sgTransform>(entity).GetChildren())
            {
                updateEntityCollisionBounds(child);
            }
        }
    }

    void EditorTransformEditor::notify(const entt::entity entity) const
    {
        if (onApplied) onApplied(entity);
    }
} // namespace sage::editor

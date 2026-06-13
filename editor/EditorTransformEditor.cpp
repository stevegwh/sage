//
// EditorTransformEditor implementation. See EditorTransformEditor.hpp.
//

#include "EditorTransformEditor.hpp"

#include "EditorTransformMath.hpp"
#include "engine/Camera.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Settings.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
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
        bool IsNavigationObstacle(entt::registry& registry, const entt::entity entity)
        {
            const auto* obstacle = registry.try_get<NavigationObstacle>(entity);
            return obstacle != nullptr && obstacle->active;
        }
    } // namespace

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

        std::optional<BoundingBox> SelectionBounds(
            entt::registry& registry, const std::vector<entt::entity>& entities)
        {
            std::optional<BoundingBox> bounds;
            for (const auto entity : entities)
            {
                AppendSubtreeBounds(registry, entity, bounds);
            }
            return bounds;
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

        Vector3 DeltaForBoundsMinSnap(
            const Vector3 currentBoundsMin, const Vector3 desiredBoundsMin, const Vector3 movementDelta)
        {
            Vector3 targetBoundsMin = desiredBoundsMin;
            if (std::abs(movementDelta.x) > 0.0001f)
            {
                targetBoundsMin.x = SnapGridCoord(desiredBoundsMin.x);
            }
            else
            {
                targetBoundsMin.x = currentBoundsMin.x;
            }

            if (std::abs(movementDelta.z) > 0.0001f)
            {
                targetBoundsMin.z = SnapGridCoord(desiredBoundsMin.z);
            }
            else
            {
                targetBoundsMin.z = currentBoundsMin.z;
            }

            return Vector3Subtract(targetBoundsMin, currentBoundsMin);
        }
    } // namespace

    EditorTransformEditor::EditorTransformEditor(EngineSystems* _sys, OnApplied _onApplied)
        : sys(_sys), onApplied(std::move(_onApplied))
    {
    }

    void EditorTransformEditor::ExitEditMode()
    {
        if (gizmo.IsDragging() || boxGizmo.IsDragging())
        {
            sys->camera->UnlockInput();
        }
        gizmo.EndDrag();
        boxGizmo.EndDrag();
    }

    void EditorTransformEditor::Update(const std::vector<entt::entity>& entities)
    {
        if (mode == EditGizmo::Mode::BoxCollider)
        {
            updateBoxDrag(entities);
            return;
        }

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
                const Vector3 worldDelta =
                    Vector3Scale(axisVector, sample.projectedAxisPixels * size / screenLength);
                if (snapToGrid)
                {
                    const auto bounds = SelectionBounds(*sys->registry, entities);
                    if (!bounds.has_value()) return;

                    // Keep sub-grid drag motion between frames, then commit only when the
                    // accumulated bounds min crosses a drawn grid line.
                    dragUnsnappedBoundsMinPosition =
                        Vector3Add(dragUnsnappedBoundsMinPosition, worldDelta);
                    const Vector3 appliedDelta =
                        DeltaForBoundsMinSnap(bounds->min, dragUnsnappedBoundsMinPosition, worldDelta);
                    if (!Vector3Equals(appliedDelta, Vector3Zero()))
                    {
                        applyPositionDelta(entities, appliedDelta);
                    }
                }
                else if (!Vector3Equals(worldDelta, Vector3Zero()))
                {
                    applyPositionDelta(entities, worldDelta);
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
        case EditGizmo::Mode::BoxCollider:
            // Routed to updateBoxDrag before this switch is reached.
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

        if (mode == EditGizmo::Mode::BoxCollider)
        {
            const auto target = boxEditTarget(entities);
            if (target == entt::null) return false;
            const auto& worldBox = sys->registry->get<Collideable>(target).worldBoundingBox;
            const auto face = boxGizmo.HitTest(camera, viewport, worldBox, renderMousePosition);
            if (face == BoxColliderGizmo::Face::None) return false;
            boxGizmo.BeginDrag(face, renderMousePosition);
            sys->camera->LockInput();
            return true;
        }

        const Vector3 origin = PivotWorldPosition(entities);

        const auto axis = gizmo.HitTest(camera, viewport, origin, mode, renderMousePosition, gizmoViewportScale());
        if (axis == EditGizmo::Axis::None) return false;

        gizmo.BeginDrag(axis, renderMousePosition);
        const auto bounds = SelectionBounds(*sys->registry, entities);
        dragUnsnappedBoundsMinPosition = bounds.has_value() ? bounds->min : origin;
        sys->camera->LockInput();
        return true;
    }

    float EditorTransformEditor::gizmoViewportScale() const
    {
        const float windowHeight = sys->settings->GetScreenSize().y;
        const float renderViewportHeight = sys->settings->GetRenderViewPort().y;
        if (renderViewportHeight <= 1.0f) return 1.0f;
        return windowHeight / renderViewportHeight;
    }

    void EditorTransformEditor::Draw3D(const std::vector<entt::entity>& entities) const
    {
        if (!HasValidTransform(*sys->registry, entities)) return;

        const Camera3D camera = *sys->camera->getRaylibCam();
        const Vector2 viewport = sys->settings->GetRenderViewPort();
        const float viewportScale = gizmoViewportScale();

        if (mode == EditGizmo::Mode::BoxCollider)
        {
            const auto target = boxEditTarget(entities);
            if (target == entt::null) return;
            boxGizmo.Draw(camera, sys->registry->get<Collideable>(target).worldBoundingBox, viewportScale);
            return;
        }

        const Vector3 origin = PivotWorldPosition(entities);
        gizmo.Draw(camera, viewport, origin, mode, viewportScale);
    }

    void EditorTransformEditor::SetMode(const EditGizmo::Mode newMode)
    {
        mode = newMode;
    }

    void EditorTransformEditor::SetSnapToGrid(const bool enabled)
    {
        snapToGrid = enabled;
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
        case EditGizmo::Mode::BoxCollider:
            return "Box Collider";
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

        const auto bounds = SelectionBounds(*sys->registry, entities);

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

        if (!snapToGrid)
        {
            applyPositionDelta(entities, worldDelta);
            return;
        }

        const auto bounds = SelectionBounds(*sys->registry, entities);
        if (!bounds.has_value()) return;

        const Vector3 desiredBoundsMin = Vector3Add(bounds->min, worldDelta);
        const Vector3 appliedDelta = DeltaForBoundsMinSnap(bounds->min, desiredBoundsMin, worldDelta);
        if (Vector3Equals(appliedDelta, Vector3Zero())) return;

        applyPositionDelta(entities, appliedDelta);
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

    void EditorTransformEditor::refitEntityCollisionBounds(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity)) return;

        if (sys->registry->all_of<sgTransform, Collideable>(entity))
        {
            const auto& transform = sys->registry->get<sgTransform>(entity);
            auto& collideable = sys->registry->get<Collideable>(entity);
            const bool navigationObstacle = IsNavigationObstacle(*sys->registry, entity);
            if (navigationObstacle)
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

            if (navigationObstacle)
            {
                sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);
            }
        }
    }

    void EditorTransformEditor::updateEntityCollisionBounds(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity)) return;

        refitEntityCollisionBounds(entity);

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

    entt::entity EditorTransformEditor::boxEditTarget(const std::vector<entt::entity>& entities) const
    {
        for (const auto entity : entities)
        {
            if (sys->registry->valid(entity) && sys->registry->all_of<sgTransform, Collideable>(entity))
            {
                return entity;
            }
        }
        return entt::null;
    }

    void EditorTransformEditor::updateBoxDrag(const std::vector<entt::entity>& entities)
    {
        if (!boxGizmo.IsDragging()) return;

        const auto target = boxEditTarget(entities);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || target == entt::null)
        {
            ExitEditMode();
            return;
        }

        const Camera3D camera = *sys->camera->getRaylibCam();
        const Vector2 viewport = sys->settings->GetRenderViewPort();
        const Vector2 mousePosition = sys->settings->ScreenToRenderViewportPosition(GetMousePosition());
        const auto& worldBox = sys->registry->get<Collideable>(target).worldBoundingBox;

        const auto sample = boxGizmo.SampleDrag(camera, viewport, worldBox, mousePosition);
        if (sample.face == BoxColliderGizmo::Face::None || std::abs(sample.worldDelta) <= 0.0001f) return;

        applyBoxFaceDelta(target, sample.face, sample.worldDelta);
    }

    void EditorTransformEditor::applyBoxFaceDelta(
        const entt::entity entity, const BoxColliderGizmo::Face face, const float worldDelta) const
    {
        if (!sys->registry->valid(entity) || !sys->registry->all_of<sgTransform, Collideable>(entity)) return;

        const auto& transform = sys->registry->get<sgTransform>(entity);
        auto& collideable = sys->registry->get<Collideable>(entity);

        // The handle is dragged in world units along the face's outward normal,
        // but the box is authored in local space — divide out the entity's scale
        // on that axis. Assumes an axis-aligned (unrotated) box, which holds for
        // trigger volumes and other meshless collideables.
        const Vector3 scale = transform.GetScale();
        constexpr float MIN_THICKNESS = 0.05f;

        const bool navigationObstacle = IsNavigationObstacle(*sys->registry, entity);
        if (navigationObstacle)
        {
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, false, entity);
        }

        BoundingBox& box = collideable.localBoundingBox;
        switch (face)
        {
        case BoxColliderGizmo::Face::MaxX:
            box.max.x = std::max(box.min.x + MIN_THICKNESS, box.max.x + worldDelta / std::max(scale.x, 0.0001f));
            break;
        case BoxColliderGizmo::Face::MinX:
            box.min.x = std::min(box.max.x - MIN_THICKNESS, box.min.x - worldDelta / std::max(scale.x, 0.0001f));
            break;
        case BoxColliderGizmo::Face::MaxY:
            box.max.y = std::max(box.min.y + MIN_THICKNESS, box.max.y + worldDelta / std::max(scale.y, 0.0001f));
            break;
        case BoxColliderGizmo::Face::MinY:
            box.min.y = std::min(box.max.y - MIN_THICKNESS, box.min.y - worldDelta / std::max(scale.y, 0.0001f));
            break;
        case BoxColliderGizmo::Face::MaxZ:
            box.max.z = std::max(box.min.z + MIN_THICKNESS, box.max.z + worldDelta / std::max(scale.z, 0.0001f));
            break;
        case BoxColliderGizmo::Face::MinZ:
            box.min.z = std::min(box.max.z - MIN_THICKNESS, box.min.z - worldDelta / std::max(scale.z, 0.0001f));
            break;
        case BoxColliderGizmo::Face::None:
            break;
        }

        // Recompute the world box from the edited local box. Note this is the one
        // place we don't route through updateEntityCollisionBounds: that helper
        // re-derives localBoundingBox from a Renderable's mesh, which would
        // immediately discard the manual edit.
        const Matrix entityMatrix = BuildRenderableEntityMatrix(
            transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
        collideable.worldBoundingBox = TransformBoundingBoxByCorners(box, entityMatrix);

        if (navigationObstacle)
        {
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);
        }

        notify(entity);
    }

    void EditorTransformEditor::notify(const entt::entity entity) const
    {
        if (onApplied) onApplied(entity);
    }
} // namespace sage::editor

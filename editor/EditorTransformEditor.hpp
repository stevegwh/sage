//
// Edit-mode behaviour: gizmo input, pivot toggling, pivot-aware matrix
// application, inspector-input commit path. Owned by EditorScene; driven by
// EditorModeStateMachine during EditorEditState.
//

#pragma once

#include "BoxColliderGizmo.hpp"
#include "EditGizmo.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <functional>
#include <string>
#include <vector>

namespace sage
{
    class EngineSystems;
}

namespace sage::editor
{
    class EditorTransformEditor
    {
      public:
        enum class PivotMode
        {
            World,
            LocalCenter
        };

        // Invoked after every mutation that touches the selected entity. The
        // scene uses it to keep the placement overlay (snap markers,
        // placement rotation/scale cache) and GUI windows in sync with the
        // entity's new transform.
        using OnApplied = std::function<void(entt::entity)>;

        EditorTransformEditor(EngineSystems* sys, OnApplied onApplied);

        // Lifecycle.
        void ExitEditMode();

        // Per-frame gizmo update / draw. Update is a no-op unless a drag is
        // currently active; the state machine calls TryStartDrag separately on
        // mouse-down to begin one.
        void Update(const std::vector<entt::entity>& entities);
        bool TryStartDrag(const std::vector<entt::entity>& entities, Vector2 mousePosition);
        void Draw3D(const std::vector<entt::entity>& entities) const;

        // Mode + pivot.
        void SetMode(EditGizmo::Mode);
        void TogglePivotMode();
        [[nodiscard]] EditGizmo::Mode Mode() const
        {
            return mode;
        }
        [[nodiscard]] PivotMode Pivot() const
        {
            return pivotMode;
        }
        [[nodiscard]] std::string DescribeMode() const;
        [[nodiscard]] Vector3 PivotWorldPosition(entt::entity entity) const;
        [[nodiscard]] Vector3 PivotWorldPosition(const std::vector<entt::entity>& entities) const;
        [[nodiscard]] bool IsGizmoDragging() const
        {
            return gizmo.IsDragging() || boxGizmo.IsDragging();
        }
        void SetSnapToGrid(bool enabled);

        // Keyboard / arrow-key paths driven by the state machine.
        void AdjustPosition(const std::vector<entt::entity>& entities, Vector3 worldDelta);
        void AdjustRotationAxis(const std::vector<entt::entity>& entities, EditGizmo::Axis axis, float degrees);
        void AdjustScale(const std::vector<entt::entity>& entities, float delta);

        // Rebuilds collision bounds for `entity` and all its descendants.
        // Used after bulk-instantiating entities (e.g. flatpacks) so each new
        // Collideable.worldBoundingBox reflects the actual world transform.
        void RefreshCollisionBoundsRecursive(entt::entity entity) const
        {
            updateEntityCollisionBounds(entity);
        }

      private:
        EngineSystems* sys;
        OnApplied onApplied;
        EditGizmo gizmo;
        BoxColliderGizmo boxGizmo;
        EditGizmo::Mode mode = EditGizmo::Mode::Translate;
        PivotMode pivotMode = PivotMode::LocalCenter;
        bool snapToGrid = false;

        // windowHeight / renderViewportHeight — passed to the gizmos so their
        // on-screen size tracks the window rather than the (docked-panel-shrunk)
        // 3D render viewport. See EditGizmo::SizeForCamera.
        [[nodiscard]] float gizmoViewportScale() const;

        void updateEntityCollisionBounds(entt::entity entity) const;
        void notify(entt::entity entity) const;
        void applyPositionDelta(const std::vector<entt::entity>& entities, Vector3 worldDelta);

        // Box-collider mode: the first selected entity carrying a Collideable,
        // or entt::null. Box editing is single-target by nature.
        [[nodiscard]] entt::entity boxEditTarget(const std::vector<entt::entity>& entities) const;
        void updateBoxDrag(const std::vector<entt::entity>& entities);
        void applyBoxFaceDelta(entt::entity entity, BoxColliderGizmo::Face face, float worldDelta) const;

        Vector3 dragUnsnappedBoundsMinPosition{};
    };
} // namespace sage::editor

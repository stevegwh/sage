#include "EditorModeStateMachine.hpp"

#include "EditorGui.hpp"
#include "EditorHistory.hpp"
#include "EditorScene.hpp"
#include "EditorTransformEditor.hpp"
#include "engine/Camera.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Settings.hpp"
#include "engine/UserInput.hpp"

#include "raylib.h"

#include <algorithm>

namespace sage::editor
{
    namespace
    {
        constexpr float PLACEMENT_MARKER_HEIGHT = 0.16f;
        constexpr float EDIT_TRANSLATION_STEP = 1.0f;
        constexpr float PLACEMENT_ROTATION_STEP = 15.0f;
        constexpr float PLACEMENT_SCALE_STEP = 0.1f;

        bool IsKeyPressedOrRepeated(const int key)
        {
            return IsKeyPressed(key) || IsKeyPressedRepeat(key);
        }

        void EnableCollideableStaticOverride(entt::registry& registry, const entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.any_of<Collideable>(entity) ||
                registry.any_of<CollideableStaticOverride>(entity))
            {
                return;
            }

            registry.emplace<CollideableStaticOverride>(entity);
        }

        void DisableCollideableStaticOverride(entt::registry& registry, const entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.any_of<CollideableStaticOverride>(entity)) return;

            registry.remove<CollideableStaticOverride>(entity);
        }
    } // namespace

    // ===== Select Mode =============================================================

    std::string EditorSelectState::GetName(const EditorModeStateMachine& machine)
    {
        return "Select";
    }

    void EditorSelectState::OnEnter(EditorModeStateMachine& machine)
    {
        machine.refreshOverlay();
    }

    void EditorSelectState::OnExit(EditorModeStateMachine&)
    {
    }

    void EditorSelectState::Update(EditorModeStateMachine& machine)
    {
        HandleDeleteConfirmationInput(machine);

        if (machine.isKeyboardEditing()) return;

        HandleKeyboardInput(machine);
        HandleMouseInput(machine);
    }

    void EditorSelectState::Draw3D(const EditorModeStateMachine&) const
    {
    }

    void EditorSelectState::HandleDeleteConfirmationInput(EditorModeStateMachine& machine)
    {
        const auto action = machine.consumeDeleteConfirmationAction();
        if (action == EditorGui::DeleteConfirmationAction::None) return;

        if (action == EditorGui::DeleteConfirmationAction::Confirm)
        {
            ConfirmDeleteSelectedEntity(machine);
        }
        else
        {
            CancelDeleteSelectedEntity(machine);
        }
    }

    void EditorSelectState::HandleKeyboardInput(EditorModeStateMachine& machine)
    {
        if (IsKeyPressed(KEY_DELETE) && !machine.isDeleteConfirmationVisible())
        {
            RequestDeleteSelectedEntity(machine);
        }

        const bool altHeld = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        if (IsKeyPressed(KEY_F) && !altHeld && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)))
        {
            machine.focusSelectedObjectInHierarchy();
        }
        else if (IsKeyPressed(KEY_F) && !altHeld)
        {
            machine.focusSelectedObject();
        }

        if (IsKeyPressed(KEY_TAB))
        {
            BeginEditSelectedTransform(machine);
        }
    }

    void EditorSelectState::HandleMouseInput(EditorModeStateMachine& machine)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !machine.isMouseOverUiCell())
        {
            const bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (!SelectSceneEntityUnderCursor(machine))
            {
                if (!additive)
                {
                    ClearSceneEntitySelection(machine);
                }
            }
        }
    }

    bool EditorSelectState::SelectSceneEntityUnderCursor(EditorModeStateMachine& machine)
    {
        const auto entity = machine.pickSceneEntityUnderCursor();
        if (!entity.has_value()) return false;

        const bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        SelectSceneEntity(machine, *entity, additive);
        return true;
    }

    void EditorSelectState::ClearSceneEntitySelection(EditorModeStateMachine& machine)
    {
        machine.selection().Clear();
        if (machine.IsEditMode())
        {
            machine.ChangeState(EditorSelectState{});
        }
        machine.hideDeleteConfirmation();
        machine.refreshSceneWindows();
    }

    void EditorSelectState::SelectSceneEntity(
        EditorModeStateMachine& machine, const entt::entity entity, const bool additive)
    {
        const bool changed = additive ? machine.selection().Toggle(entity) : machine.selection().Select(entity);
        if (!changed) return;
        machine.ChangeState(EditorSelectState{});
        machine.hideDeleteConfirmation();
        machine.refreshSceneWindows();
    }

    void EditorSelectState::SelectSceneFromHierarchy(
        EditorModeStateMachine& machine, const EditorGui::SceneSelectionRequest& request)
    {
        bool changed = false;
        switch (request.mode)
        {
        case EditorGui::SceneSelectionMode::Replace:
            changed = machine.selection().Select(request.entity);
            break;
        case EditorGui::SceneSelectionMode::Toggle:
            changed = machine.selection().Toggle(request.entity);
            break;
        case EditorGui::SceneSelectionMode::Range:
            changed = machine.selection().SelectRange(request.rangeEntities);
            break;
        }
        if (!changed) return;

        machine.ChangeState(EditorSelectState{});
        machine.hideDeleteConfirmation();
        machine.refreshSceneWindows();
    }

    void EditorSelectState::RequestDeleteSelectedEntity(EditorModeStateMachine& machine)
    {
        if (!machine.selection().HasSelection()) return;
        if (machine.selection().Selected().empty())
        {
            ClearSceneEntitySelection(machine);
            return;
        }

        machine.showDeleteConfirmationForSelection();
    }

    void EditorSelectState::CancelDeleteSelectedEntity(EditorModeStateMachine& machine)
    {
        machine.hideDeleteConfirmation();
    }

    void EditorSelectState::ConfirmDeleteSelectedEntity(EditorModeStateMachine& machine)
    {
        const auto selectedEntities = machine.selection().Selected();
        if (selectedEntities.empty())
        {
            machine.hideDeleteConfirmation();
            return;
        }

        machine.selection().Clear();
        machine.hideDeleteConfirmation();
        machine.deleteEntitiesAndChildren(selectedEntities);
        machine.refreshSceneWindows();
        machine.refreshOverlay();
    }

    void EditorSelectState::BeginEditSelectedTransform(EditorModeStateMachine& machine)
    {
        const auto selectedEntities = machine.selection().Selected();
        if (selectedEntities.empty())
        {
            ClearSceneEntitySelection(machine);
            return;
        }

        machine.hideDeleteConfirmation();
        machine.ChangeState(EditorEditState{.entities = selectedEntities});
        machine.refreshOverlay();
        machine.refreshSceneWindows();
    }

    bool EditorSelectState::HandleEscape(EditorModeStateMachine&)
    {
        return false;
    }

    void EditorSelectState::OnTransformApplied(EditorModeStateMachine&, entt::entity)
    {
    }

    // ===== Place Mode ==============================================================
    std::string EditorPlaceState::GetName(const EditorModeStateMachine& machine)
    {
        return "Place";
    }

    void EditorPlaceState::OnEnter(EditorModeStateMachine& machine)
    {
        // For flatpack placement we just need the cursor snap target. Skip the
        // single-asset selection path so the placement controller doesn't try to
        // build a preview from a non-existent placeable.
        if (!flatpackPath.has_value())
        {
            machine.selectPlaceableAsset(placeableIndex);
        }
        ResetPlacementTransform(machine);
        machine.refreshOverlay();
    }

    void EditorPlaceState::OnExit(EditorModeStateMachine&)
    {
    }

    void EditorPlaceState::Update(EditorModeStateMachine& machine)
    {
        if (machine.isKeyboardEditing()) return;

        if (IsKeyPressed(KEY_LEFT_BRACKET))
        {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
            {
                AdjustPlacementScale(machine, -PLACEMENT_SCALE_STEP);
            }
            else
            {
                AdjustPlacementRotation(machine, -PLACEMENT_ROTATION_STEP);
            }
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET))
        {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
            {
                AdjustPlacementScale(machine, PLACEMENT_SCALE_STEP);
            }
            else
            {
                AdjustPlacementRotation(machine, PLACEMENT_ROTATION_STEP);
            }
        }
        if (IsKeyPressed(KEY_P))
        {
            (void)PlaceSelectedMesh(machine);
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !machine.isMouseOverUiCell())
        {
            if (!PlaceSelectedMesh(machine))
            {
                SelectSceneEntityUnderCursor(machine);
            }
        }
    }

    void EditorPlaceState::Draw3D(const EditorModeStateMachine& machine) const
    {
        const auto& snappedPlacementPosition = machine.snappedPlacementPosition();
        if (!snappedPlacementPosition.has_value()) return;

        DrawPlacementPreview(machine);

        const Vector3 marker = {
            snappedPlacementPosition->x,
            snappedPlacementPosition->y + PLACEMENT_MARKER_HEIGHT,
            snappedPlacementPosition->z};
        DrawCubeWires(marker, 1.0f, PLACEMENT_MARKER_HEIGHT, 1.0f, GOLD);
        DrawSphere(marker, 0.08f, GOLD);
    }

    bool EditorPlaceState::SelectSceneEntityUnderCursor(EditorModeStateMachine& machine)
    {
        const auto entity = machine.pickSceneEntityUnderCursor();
        if (!entity.has_value()) return false;

        machine.SelectSceneEntity(*entity);
        return true;
    }

    void EditorPlaceState::ResetPlacementTransform(EditorModeStateMachine& machine)
    {
        machine.placement().ResetTransform();
    }

    void EditorPlaceState::AdjustPlacementRotation(EditorModeStateMachine& machine, const float amount)
    {
        machine.placement().AdjustRotation(amount);
        machine.refreshOverlay();
    }

    void EditorPlaceState::AdjustPlacementScale(EditorModeStateMachine& machine, const float amount)
    {
        machine.placement().AdjustScale(amount);
        machine.refreshOverlay();
    }

    bool EditorPlaceState::PlaceSelectedMesh(EditorModeStateMachine& machine)
    {
        std::optional<entt::entity> entity;
        if (flatpackPath.has_value())
        {
            const auto& snap = machine.snappedPlacementPosition();
            if (!snap.has_value()) return false;
            entity = machine.scene.PlaceFlatpackAt(*flatpackPath, *snap);
        }
        else
        {
            entity = machine.placement().PlaceSelectedMesh();
        }
        if (!entity.has_value()) return false;

        machine.history().RecordCreate(EditAction::Place, {*entity});
        (void)machine.selection().Select(*entity);
        machine.refreshSceneWindows();
        machine.focusHierarchyOnEntity(*entity);
        machine.ChangeState(EditorSelectState{});
        machine.refreshOverlay();
        return true;
    }

    void EditorPlaceState::DrawPlacementPreview(const EditorModeStateMachine& machine) const
    {
        // Flatpacks don't yet have a hierarchy ghost preview, so just rely on
        // the cursor marker that EditorPlaceState::Draw3D already renders.
        if (flatpackPath.has_value()) return;
        machine.placement().DrawPreview();
    }

    bool EditorPlaceState::HandleEscape(EditorModeStateMachine& machine)
    {
        ResetPlacementTransform(machine);
        machine.ChangeState(EditorSelectState{});
        machine.refreshOverlay();
        machine.refreshSceneWindows();
        return true;
    }

    void EditorPlaceState::OnTransformApplied(EditorModeStateMachine&, entt::entity)
    {
    }

    // ===== Edit Mode ===============================================================

    std::string EditorEditState::GetName(const EditorModeStateMachine& machine)
    {
        return "Edit: " + machine.transformEditor.DescribeMode();
    }

    void EditorEditState::OnEnter(EditorModeStateMachine& machine)
    {
        if (entities.empty())
        {
            entities = machine.selection().Selected();
        }

        std::erase_if(entities, [&machine](const entt::entity entity) {
            return !machine.hasTransform(entity);
        });

        if (entities.empty())
        {
            machine.ChangeState(EditorSelectState{});
            return;
        }

        machine.enableCollideableStaticOverride(entities);
        machine.history().Begin(EditAction::Transform, entities);
        SyncPlacementFromEntity(machine, entities.front());
        machine.refreshOverlay();
        machine.refreshSceneWindows();
    }

    void EditorEditState::OnExit(EditorModeStateMachine& machine)
    {
        machine.disableCollideableStaticOverride(entities);
        machine.transformEditor.ExitEditMode();
        if (machine.history().HasActiveTransaction())
        {
            machine.history().Commit();
        }
    }

    void EditorEditState::Update(EditorModeStateMachine& machine)
    {
        std::erase_if(entities, [&machine](const entt::entity entity) {
            return !machine.hasTransform(entity);
        });

        if (entities.empty())
        {
            ClearSceneEntitySelection(machine);
            return;
        }

        SyncPlacementFromEntity(machine, entities.front());

        if (machine.isKeyboardEditing()) return;

        if (IsKeyPressed(KEY_TAB))
        {
            FinishEditSelectedTransform(machine);
            return;
        }

        if (machine.transformEditor.IsGizmoDragging())
        {
            machine.transformEditor.Update(entities);
            return;
        }

        if (IsKeyPressed(KEY_T))
        {
            machine.transformEditor.SetMode(EditGizmo::Mode::Translate);
        }
        if (IsKeyPressed(KEY_R))
        {
            machine.transformEditor.SetMode(EditGizmo::Mode::Rotate);
        }
        if (IsKeyPressed(KEY_Y))
        {
            machine.transformEditor.SetMode(EditGizmo::Mode::Scale);
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !machine.isMouseOverUiCell())
        {
            if (machine.transformEditor.TryStartDrag(entities, GetMousePosition())) return;
        }

        const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (!IsMetaKeyDown())
        {
            switch (machine.transformEditor.Mode())
            {
            case EditGizmo::Mode::Translate: {
                Vector3 positionDelta{};
                if (IsKeyPressedOrRepeated(KEY_LEFT))
                {
                    positionDelta.x -= EDIT_TRANSLATION_STEP;
                }
                if (IsKeyPressedOrRepeated(KEY_RIGHT))
                {
                    positionDelta.x += EDIT_TRANSLATION_STEP;
                }
                if (shiftDown)
                {
                    if (IsKeyPressedOrRepeated(KEY_UP))
                    {
                        positionDelta.y += EDIT_TRANSLATION_STEP;
                    }
                    if (IsKeyPressedOrRepeated(KEY_DOWN))
                    {
                        positionDelta.y -= EDIT_TRANSLATION_STEP;
                    }
                }
                else
                {
                    if (IsKeyPressedOrRepeated(KEY_UP))
                    {
                        positionDelta.z += EDIT_TRANSLATION_STEP;
                    }
                    if (IsKeyPressedOrRepeated(KEY_DOWN))
                    {
                        positionDelta.z -= EDIT_TRANSLATION_STEP;
                    }
                }
                if (positionDelta.x != 0.0f || positionDelta.y != 0.0f || positionDelta.z != 0.0f)
                {
                    machine.transformEditor.AdjustPosition(entities, positionDelta);
                }
                break;
            }
            case EditGizmo::Mode::Rotate: {
                if (IsKeyPressedOrRepeated(KEY_LEFT))
                {
                    machine.transformEditor.AdjustRotationAxis(
                        entities, EditGizmo::Axis::Y, -PLACEMENT_ROTATION_STEP);
                }
                if (IsKeyPressedOrRepeated(KEY_RIGHT))
                {
                    machine.transformEditor.AdjustRotationAxis(
                        entities, EditGizmo::Axis::Y, PLACEMENT_ROTATION_STEP);
                }
                if (IsKeyPressedOrRepeated(KEY_UP))
                {
                    machine.transformEditor.AdjustRotationAxis(
                        entities, EditGizmo::Axis::X, PLACEMENT_ROTATION_STEP);
                }
                if (IsKeyPressedOrRepeated(KEY_DOWN))
                {
                    machine.transformEditor.AdjustRotationAxis(
                        entities, EditGizmo::Axis::X, -PLACEMENT_ROTATION_STEP);
                }
                break;
            }
            case EditGizmo::Mode::Scale: {
                if (IsKeyPressedOrRepeated(KEY_LEFT))
                {
                    machine.transformEditor.AdjustScale(entities, -PLACEMENT_SCALE_STEP);
                }
                if (IsKeyPressedOrRepeated(KEY_RIGHT))
                {
                    machine.transformEditor.AdjustScale(entities, PLACEMENT_SCALE_STEP);
                }
                break;
            }
            }
        }

        if (IsKeyPressed(KEY_P))
        {
            FinishEditSelectedTransform(machine);
        }
    }

    void EditorEditState::Draw3D(const EditorModeStateMachine& machine) const
    {
        const auto& snappedPlacementPosition = machine.snappedPlacementPosition();
        if (!snappedPlacementPosition.has_value()) return;

        const Vector3 marker = {
            snappedPlacementPosition->x,
            snappedPlacementPosition->y + PLACEMENT_MARKER_HEIGHT,
            snappedPlacementPosition->z};
        DrawCubeWires(marker, 1.0f, PLACEMENT_MARKER_HEIGHT, 1.0f, ORANGE);
        DrawSphere(marker, 0.08f, ORANGE);

        machine.transformEditor.Draw3D(entities);
    }

    void EditorEditState::FinishEditSelectedTransform(EditorModeStateMachine& machine)
    {
        machine.ChangeState(EditorSelectState{});
        machine.refreshOverlay();
        machine.refreshSceneWindows();
    }

    bool EditorEditState::CancelEditSelectedTransform(EditorModeStateMachine& machine)
    {
        if (machine.history().HasActiveTransaction())
        {
            machine.history().Rollback();
        }
        machine.ChangeState(EditorSelectState{});
        machine.refreshOverlay();
        machine.refreshSceneWindows();
        return true;
    }

    void EditorEditState::ToggleEditPivotMode(EditorModeStateMachine& machine)
    {
        if (entities.empty())
        {
            ClearSceneEntitySelection(machine);
            return;
        }

        machine.transformEditor.TogglePivotMode();
        SyncPlacementFromEntity(machine, entities.front());
        machine.refreshOverlay();
        machine.refreshSceneWindows();
    }

    void EditorEditState::ClearSceneEntitySelection(EditorModeStateMachine& machine)
    {
        machine.selection().Clear();
        machine.ChangeState(EditorSelectState{});
        machine.hideDeleteConfirmation();
        machine.refreshSceneWindows();
    }

    void EditorEditState::SyncPlacementFromEntity(EditorModeStateMachine& machine, const entt::entity entity)
    {
        machine.placement().SyncFromEntity(entity);
    }

    bool EditorEditState::HandleEscape(EditorModeStateMachine& machine)
    {
        return CancelEditSelectedTransform(machine);
    }

    void EditorEditState::OnTransformApplied(EditorModeStateMachine& machine, const entt::entity)
    {
        if (!entities.empty())
        {
            SyncPlacementFromEntity(machine, entities.front());
        }
        machine.refreshOverlay();
        machine.refreshSceneWindows();
    }

    // ===== Controller actions =========================================================

    void EditorModeStateMachine::refreshOverlay() const
    {
        scene.refreshOverlay();
    }

    void EditorModeStateMachine::refreshSceneWindows() const
    {
        scene.refreshSceneWindows();
    }

    void EditorModeStateMachine::RefreshPlacementTarget()
    {
        scene.placementController->RefreshTarget();
    }

    void EditorModeStateMachine::AdjustGridSurfaceY(const float amount)
    {
        scene.placementController->AdjustGridSurfaceY(amount);
        refreshOverlay();
    }

    bool EditorModeStateMachine::isMouseOverUiCell() const
    {
        return scene.gui->WantsMouseCapture() ||
               !scene.sys->settings->IsPointInRenderViewport(GetMousePosition());
    }

    bool EditorModeStateMachine::isKeyboardEditing() const
    {
        return scene.gui->WantsKeyboardCapture();
    }

    bool EditorModeStateMachine::isDeleteConfirmationVisible() const
    {
        return scene.gui->IsDeleteConfirmationVisible();
    }

    EditorGui::DeleteConfirmationAction EditorModeStateMachine::consumeDeleteConfirmationAction()
    {
        return scene.gui->ConsumeDeleteConfirmationAction();
    }

    std::optional<entt::entity> EditorModeStateMachine::pickSceneEntityUnderCursor() const
    {
        return scene.pickingService->PickSceneEntity(
            GetMousePosition(), scene.placementController->GridSurfaceEntity());
    }

    EditorSelection& EditorModeStateMachine::selection()
    {
        return *scene.selection;
    }

    const EditorSelection& EditorModeStateMachine::selection() const
    {
        return *scene.selection;
    }

    void EditorModeStateMachine::hideDeleteConfirmation() const
    {
        scene.gui->HideDeleteConfirmation();
    }

    void EditorModeStateMachine::showDeleteConfirmationForSelection() const
    {
        scene.gui->ShowDeleteConfirmation(scene.describeSelectedSceneEntity());
    }

    void EditorModeStateMachine::deleteEntityAndChildren(const entt::entity entity) const
    {
        scene.entityOperations->DeleteEntityAndChildren(entity);
    }

    void EditorModeStateMachine::deleteEntitiesAndChildren(const std::vector<entt::entity>& entities) const
    {
        history().RecordDestroy(EditAction::Delete, entities);
        for (const auto entity : entities)
        {
            deleteEntityAndChildren(entity);
        }
    }

    void EditorModeStateMachine::focusHierarchyOnEntity(const entt::entity entity) const
    {
        scene.gui->FocusHierarchyOnEntity(entity);
    }

    void EditorModeStateMachine::focusSelectedObject() const
    {
        scene.focusSelectedObject();
    }

    void EditorModeStateMachine::focusSelectedObjectInHierarchy() const
    {
        scene.focusSelectedObjectInHierarchy();
    }

    bool EditorModeStateMachine::canSelectPlaceable(const std::size_t index) const
    {
        return index < scene.assetCatalog->Size();
    }

    void EditorModeStateMachine::selectPlaceableAsset(const std::size_t index)
    {
        scene.assetCatalog->Select(index);
    }

    const std::optional<Vector3>& EditorModeStateMachine::snappedPlacementPosition() const
    {
        return scene.placementController->SnappedPlacementPosition();
    }

    bool EditorModeStateMachine::hasTransform(const entt::entity entity) const
    {
        return scene.sys->registry->valid(entity) && scene.sys->registry->any_of<sgTransform>(entity);
    }

    EditorPlacementController& EditorModeStateMachine::placement()
    {
        return *scene.placementController;
    }

    const EditorPlacementController& EditorModeStateMachine::placement() const
    {
        return *scene.placementController;
    }

    EditorHistory& EditorModeStateMachine::history() const
    {
        return *scene.history;
    }

    void EditorModeStateMachine::enableCollideableStaticOverride(const entt::entity entity) const
    {
        EnableCollideableStaticOverride(*scene.sys->registry, entity);
    }

    void EditorModeStateMachine::disableCollideableStaticOverride(const entt::entity entity) const
    {
        DisableCollideableStaticOverride(*scene.sys->registry, entity);
    }

    void EditorModeStateMachine::enableCollideableStaticOverride(
        const std::vector<entt::entity>& entities) const
    {
        for (const auto entity : entities)
        {
            enableCollideableStaticOverride(entity);
        }
    }

    void EditorModeStateMachine::disableCollideableStaticOverride(
        const std::vector<entt::entity>& entities) const
    {
        for (const auto entity : entities)
        {
            disableCollideableStaticOverride(entity);
        }
    }

    void EditorModeStateMachine::SelectPlaceable(const std::size_t index)
    {
        if (!canSelectPlaceable(index)) return;
        ChangeState(EditorPlaceState{.placeableIndex = index});
    }

    void EditorModeStateMachine::SelectFlatpack(std::filesystem::path path)
    {
        ChangeState(EditorPlaceState{.flatpackPath = std::move(path)});
    }

    void EditorModeStateMachine::SelectSceneEntity(const entt::entity entity, const bool additive)
    {
        ChangeState(EditorSelectState{});
        std::get<EditorSelectState>(currentState).SelectSceneEntity(*this, entity, additive);
    }

    void EditorModeStateMachine::SelectSceneFromHierarchy(const EditorGui::SceneSelectionRequest& request)
    {
        ChangeState(EditorSelectState{});
        std::get<EditorSelectState>(currentState).SelectSceneFromHierarchy(*this, request);
    }

    bool EditorModeStateMachine::HandleEscapePressed()
    {
        return std::visit([this](auto& current) { return current.HandleEscape(*this); }, currentState);
    }

    void EditorModeStateMachine::OnTransformApplied(const entt::entity entity)
    {
        std::visit([this, entity](auto& current) { current.OnTransformApplied(*this, entity); }, currentState);
    }

    // ===== Lifecycle ===============================================================

    void EditorModeStateMachine::Update()
    {
        std::visit([this](auto& current) { current.Update(*this); }, currentState);
    }

    void EditorModeStateMachine::Draw3D() const
    {
        std::visit([this](const auto& current) { current.Draw3D(*this); }, currentState);
    }

    bool EditorModeStateMachine::IsPlaceMode() const
    {
        return std::holds_alternative<EditorPlaceState>(currentState);
    }

    bool EditorModeStateMachine::IsEditMode() const
    {
        return std::holds_alternative<EditorEditState>(currentState);
    }

    std::string EditorModeStateMachine::GetStateName() const
    {
        return std::visit([this](auto& current) { return current.GetName(*this); }, currentState);
    }

    EditorEditState* EditorModeStateMachine::CurrentEditState()
    {
        return std::get_if<EditorEditState>(&currentState);
    }

    const EditorEditState* EditorModeStateMachine::CurrentEditState() const
    {
        return std::get_if<EditorEditState>(&currentState);
    }

    EditorModeStateMachine::EditorModeStateMachine(EditorScene& scene, EditorTransformEditor& transformEditor)
        : scene(scene), transformEditor(transformEditor)
    {
    }
} // namespace sage::editor

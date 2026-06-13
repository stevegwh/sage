#pragma once

#include "EditorGui.hpp"
#include "engine/TerrainMesh.hpp"

#include "entt/entt.hpp"
#include "raylib.h"
#include "raymath.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace sage
{
    class EditorScene;
    class EngineSystems;
}

namespace sage::editor
{
    class EditorHistory;
    class EditorModeStateMachine;
    class EditorPlacementController;
    class EditorSelection;
    class EditorTransformEditor;

    struct EditorSelectState
    {
        static std::string GetName(const EditorModeStateMachine& machine);
        void OnEnter(EditorModeStateMachine& machine);
        void OnExit(EditorModeStateMachine& machine);
        void Update(EditorModeStateMachine& machine);
        void Draw3D(const EditorModeStateMachine& machine) const;
        void HandleDeleteConfirmationInput(EditorModeStateMachine& machine);
        void HandleKeyboardInput(EditorModeStateMachine& machine);
        void HandleMouseInput(EditorModeStateMachine& machine);
        bool SelectSceneEntityUnderCursor(EditorModeStateMachine& machine);
        void ClearSceneEntitySelection(EditorModeStateMachine& machine);
        void SelectSceneEntity(EditorModeStateMachine& machine, entt::entity entity, bool additive);
        void SelectSceneFromHierarchy(
            EditorModeStateMachine& machine, const EditorGui::SceneSelectionRequest& request);
        void RequestDeleteSelectedEntity(EditorModeStateMachine& machine);
        void CancelDeleteSelectedEntity(EditorModeStateMachine& machine);
        void ConfirmDeleteSelectedEntity(EditorModeStateMachine& machine);
        void BeginEditSelectedTransform(EditorModeStateMachine& machine);
        [[nodiscard]] bool HandleEscape(EditorModeStateMachine& machine);
        void OnTransformApplied(EditorModeStateMachine& machine, entt::entity entity);
    };

    struct EditorPlaceState
    {
        static std::string GetName(const EditorModeStateMachine& machine);
        std::size_t placeableIndex = 0;
        // When set, this place state instantiates a flatpack instead of a
        // single asset on each click.
        std::optional<std::filesystem::path> flatpackPath;
        void OnEnter(EditorModeStateMachine& machine);
        void OnExit(EditorModeStateMachine& machine);
        void Update(EditorModeStateMachine& machine);
        void Draw3D(const EditorModeStateMachine& machine) const;
        bool SelectSceneEntityUnderCursor(EditorModeStateMachine& machine);
        void ResetPlacementTransform(EditorModeStateMachine& machine);
        void AdjustPlacementRotation(EditorModeStateMachine& machine, float amount);
        void AdjustPlacementScale(EditorModeStateMachine& machine, float amount);
        [[nodiscard]] bool PlaceSelectedMesh(EditorModeStateMachine& machine);
        void DrawPlacementPreview(const EditorModeStateMachine& machine) const;
        [[nodiscard]] bool HandleEscape(EditorModeStateMachine& machine);
        void OnTransformApplied(EditorModeStateMachine& machine, entt::entity entity);
    };

    struct EditorEditState
    {
        static std::string GetName(const EditorModeStateMachine& machine);
        std::vector<entt::entity> entities;

        void OnEnter(EditorModeStateMachine& machine);
        void OnExit(EditorModeStateMachine& machine);
        void Update(EditorModeStateMachine& machine);
        void Draw3D(const EditorModeStateMachine& machine) const;
        void FinishEditSelectedTransform(EditorModeStateMachine& machine);
        [[nodiscard]] bool CancelEditSelectedTransform(EditorModeStateMachine& machine);
        void ToggleEditPivotMode(EditorModeStateMachine& machine);
        void ClearSceneEntitySelection(EditorModeStateMachine& machine);
        void SyncPlacementFromEntity(EditorModeStateMachine& machine, entt::entity entity);
        [[nodiscard]] bool HandleEscape(EditorModeStateMachine& machine);
        void OnTransformApplied(EditorModeStateMachine& machine, entt::entity entity);
    };

    // Paints raise/lower strokes onto one terrain entity's height field. The
    // whole stroke (mouse press to release) is a single history transaction.
    struct EditorTerrainSculptState
    {
        static std::string GetName(const EditorModeStateMachine& machine);
        entt::entity terrain = entt::null;
        float brushRadius = 6.0f;
        // World units per second applied at the brush centre.
        float brushStrength = 6.0f;
        TerrainBrushMode brushMode = TerrainBrushMode::RaiseLower;

        void OnEnter(EditorModeStateMachine& machine);
        void OnExit(EditorModeStateMachine& machine);
        void Update(EditorModeStateMachine& machine);
        void Draw3D(const EditorModeStateMachine& machine) const;
        [[nodiscard]] bool HandleEscape(EditorModeStateMachine& machine);
        void OnTransformApplied(EditorModeStateMachine& machine, entt::entity entity);

      private:
        std::optional<Vector3> cursorHit;
        bool stroking = false;
        // Reference height captured at stroke start for the Flatten brush.
        float flattenTarget = 0.0f;
        // First endpoint (terrain-local x/z) of a pending Ramp; the next click
        // completes and applies the ramp.
        std::optional<Vector2> rampStart;
        void applyBrushStroke(EditorModeStateMachine& machine) const;
        void finishStroke(EditorModeStateMachine& machine, bool keepChanges);
        void updateRamp(EditorModeStateMachine& machine);
        // Re-uploads the touched mesh chunks and refits the collider bounds.
        void commitTerrainRegion(EditorModeStateMachine& machine, const TerrainRegion& region) const;
    };

    class EditorModeStateMachine final
    {
        using State =
            std::variant<EditorSelectState, EditorPlaceState, EditorEditState, EditorTerrainSculptState>;

        friend struct EditorSelectState;
        friend struct EditorPlaceState;
        friend struct EditorEditState;
        friend struct EditorTerrainSculptState;

        EditorScene& scene;
        EditorTransformEditor& transformEditor;
        State currentState = EditorSelectState{};

        void refreshOverlay() const;
        void refreshSceneWindows() const;
        [[nodiscard]] bool isMouseOverUiCell() const;
        [[nodiscard]] bool isKeyboardEditing() const;
        [[nodiscard]] bool isDeleteConfirmationVisible() const;
        [[nodiscard]] EditorGui::DeleteConfirmationAction consumeDeleteConfirmationAction();
        [[nodiscard]] std::optional<entt::entity> pickSceneEntityUnderCursor() const;
        [[nodiscard]] EditorSelection& selection();
        [[nodiscard]] const EditorSelection& selection() const;
        void hideDeleteConfirmation() const;
        void showDeleteConfirmationForSelection() const;
        void deleteEntityAndChildren(entt::entity entity) const;
        void deleteEntitiesAndChildren(const std::vector<entt::entity>& entities) const;
        void adoptIntoFlatpackRoot(const std::vector<entt::entity>& roots) const;
        void focusHierarchyOnEntity(entt::entity entity) const;
        void focusSelectedObject() const;
        void focusSelectedObjectInHierarchy() const;
        [[nodiscard]] bool canSelectPlaceable(std::size_t index) const;
        void selectPlaceableAsset(std::size_t index);
        [[nodiscard]] const std::optional<Vector3>& snappedPlacementPosition() const;
        [[nodiscard]] bool hasTransform(entt::entity entity) const;
        [[nodiscard]] EditorPlacementController& placement();
        [[nodiscard]] const EditorPlacementController& placement() const;
        [[nodiscard]] EditorHistory& history() const;
        [[nodiscard]] entt::registry& registry() const;
        // Mouse ray through the render viewport; nullopt when the cursor is
        // outside it.
        [[nodiscard]] std::optional<Ray> viewportMouseRay() const;
        void enableCollideableStaticOverride(entt::entity entity) const;
        void disableCollideableStaticOverride(entt::entity entity) const;
        void enableCollideableStaticOverride(const std::vector<entt::entity>& entities) const;
        void disableCollideableStaticOverride(const std::vector<entt::entity>& entities) const;

      public:
        template <typename NewState>
        void ChangeState(NewState newState = {})
        {
            std::visit([this](auto& current) { current.OnExit(*this); }, currentState);
            currentState = std::move(newState);
            std::get<NewState>(currentState).OnEnter(*this);
        }

        void Update();
        void Draw3D() const;
        void RefreshPlacementTarget();
        void AdjustGridSurfaceY(float amount);
        void SelectPlaceable(std::size_t index);
        void SelectFlatpack(std::filesystem::path path);
        void SelectSceneEntity(entt::entity entity, bool additive = false);
        void SelectSceneFromHierarchy(const EditorGui::SceneSelectionRequest& request);
        bool HandleEscapePressed();
        void OnTransformApplied(entt::entity entity);

        // Enters terrain sculpting when exactly one entity is selected and it
        // has a Terrain component; no-op otherwise.
        void BeginTerrainSculptOnSelection();
        [[nodiscard]] bool CanBeginTerrainSculpt() const;

        [[nodiscard]] bool IsPlaceMode() const;
        [[nodiscard]] bool IsEditMode() const;
        [[nodiscard]] std::string GetStateName() const;
        [[nodiscard]] EditorEditState* CurrentEditState();
        [[nodiscard]] const EditorEditState* CurrentEditState() const;
        [[nodiscard]] EditorTerrainSculptState* CurrentTerrainSculptState();

        EditorModeStateMachine(EditorScene& scene, EditorTransformEditor& transformEditor);
    };
} // namespace sage::editor

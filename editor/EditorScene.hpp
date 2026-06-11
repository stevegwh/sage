#pragma once

#include "EditorAssetCatalog.hpp"
#include "EditorEntityOperations.hpp"
#include "EditorGui.hpp"
#include "EditorHierarchyTree.hpp"
#include "EditorHistory.hpp"
#include "EditorInspector.hpp"
#include "EditorMapController.hpp"
#include "EditorModelDefaultsController.hpp"
#include "EditorModeStateMachine.hpp"
#include "EditorPickingService.hpp"
#include "EditorPlacementController.hpp"
#include "EditorSelection.hpp"
#include "EditorSettings.hpp"
#include "EditorTransformEditor.hpp"

#include "entt/entt.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ImGui
{
    class FileBrowser;
}

namespace sage
{
    class EngineSystems;
    namespace editor
    {
        struct EditorDockLayout;
    }

    class EditorScene
    {
        friend class editor::EditorModeStateMachine;

        EngineSystems* sys{};
        std::unique_ptr<editor::EditorGui> gui;
        editor::InspectorRegistry inspectorRegistry;
        std::unique_ptr<editor::EditorAssetCatalog> assetCatalog;
        std::unique_ptr<editor::EditorModelDefaultsController> modelDefaults;
        std::unique_ptr<editor::EditorSelection> selection;
        std::unique_ptr<editor::EditorPickingService> pickingService;
        std::unique_ptr<editor::EditorEntityOperations> entityOperations;
        std::unique_ptr<editor::EditorHierarchyTree> hierarchyTree;
        std::unique_ptr<editor::EditorPlacementController> placementController;
        std::unique_ptr<editor::EditorTransformEditor> transformEditor;
        std::unique_ptr<editor::EditorHistory> history;
        std::unique_ptr<editor::EditorModeStateMachine> editorModes;
        std::unique_ptr<editor::EditorMapController> mapController;
        // Picks the lua file for "Add Component > Script" (save-style: typing a
        // new filename creates a template script).
        std::unique_ptr<ImGui::FileBrowser> scriptBrowser;
        mutable entt::entity hierarchyContextEntity = entt::null;
        bool viewportFullscreen = false;
        mutable bool snapToGrid = false;
        mutable bool orbitingCamera = false;
        mutable bool panningCamera = false;
        void applyLitShaderToLoadedRenderables() const;
        void giveTransformsToLights() const;
        void refreshOverlay() const;
        void refreshSceneWindows() const;
        void setSnapToGrid(bool enabled) const;
        void drawMainMenuBar(bool& exitRequested) const;
        void addLight() const;
        void addSpawner() const;
        void addTriggerVolume() const;
        void clearCurrentMap() const;
        void ensureDefaultMapBase() const;
        [[nodiscard]] std::vector<entt::entity> collectMapHierarchyOrder() const;
        void syncLightTransforms() const;
        void refreshAfterMapLoad() const;
        editor::EditorGui::AssetRenameResult handleAssetFileRename(
            std::size_t index,
            const std::string& requestedFileName) const;
        void moveHierarchyEntity(const editor::EditorGui::HierarchyMoveRequest& request) const;
        void drawHierarchyContextMenu() const;
        void drawExitConfirmationModal(bool& exitRequested, bool& exitConfirmed) const;
        void handleFileShortcuts() const;
        void handleClipboardShortcuts() const;
        void handleHistoryShortcuts() const;
        void handleInspectorEdit(const editor::EditorGui::InspectorEditResult& result) const;
        void drawScriptBrowser() const;
        void attachScriptToSelection(const std::filesystem::path& scriptFile) const;
        void removeScriptFromSelection() const;
        void addAnimationToSelection() const;
        void removeAnimationFromSelection() const;
        void addMoveableActorToSelection() const;
        void removeMoveableActorFromSelection() const;
        void onHistoryApplied(const std::vector<entt::entity>& restored) const;
        void createFlatpackFromEntity(entt::entity entity) const;
        void copyEntitiesToClipboard(const std::vector<entt::entity>& roots) const;
        void refreshFlatpackCatalog() const;

        void focusSelectedObject() const;
        void focusSelectedObjectInHierarchy() const;
        void handleMouseCameraControls() const;

        [[nodiscard]] const editor::PlaceableAsset& selectedPlaceable() const;
        [[nodiscard]] bool isPlaceState() const;
        [[nodiscard]] bool isEditState() const;
        [[nodiscard]] std::string describeSelectedAsset() const;
        [[nodiscard]] std::string describeCursorPosition() const;
        [[nodiscard]] std::string describeSelectedSceneEntity() const;

      public:
        void Update() const;
        void Draw3D() const;
        void DrawOverlay2D() const;
        void DrawImGui(bool& exitRequested, bool& exitConfirmed) const;
        [[nodiscard]] bool HandleEscapePressed() const;
        [[nodiscard]] bool ConsumeDockLayoutChanged() const;

        // Instantiates a flatpack at the given world anchor. Returns the root
        // entity on success. Called from the editor state machine when the
        // user clicks in-scene while a flatpack is armed.
        [[nodiscard]] std::optional<entt::entity> PlaceFlatpackAt(
            const std::filesystem::path& path, Vector3 anchor) const;

        // Copies the current selection (and descendants) into the clipboard.
        void CopySelection() const;
        // Instantiates the clipboard contents, selecting the new root entities.
        void PasteClipboard() const;
        [[nodiscard]] bool HasClipboard() const;

        void SetViewportFullscreen(bool fullscreen);
        void SetSceneName(const std::string& sceneName) const;

        EditorScene(
            EngineSystems* _sys,
            editor::EditorDockLayout* dockLayout,
            EditorSettings* editorSettings,
            std::function<void()> onEditorSettingsChanged);
        ~EditorScene();
    };
} // namespace sage

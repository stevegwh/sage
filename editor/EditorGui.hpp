#pragma once

#include "EditorDockLayout.hpp"
#include "EditorInspector.hpp"

#include "imgui.h"
#include "raylib.h"

#include "entt/entt.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sage
{
    struct Settings;

    namespace editor
    {
        class EditorGui
        {
          public:
            struct AssetEntry
            {
                std::string displayName;
                std::string modelKey;
                std::filesystem::path sourcePath;
                std::filesystem::path defaultsPath;
            };

            struct AssetRenameResult
            {
                bool renamed = false;
                std::string message;
                std::optional<AssetEntry> updatedEntry;
            };

            struct SceneObjectEntry
            {
                entt::entity entity = entt::null;
                entt::entity parent = entt::null;
                std::string displayName;
                // Font Awesome glyph (from IconsFontAwesome6.h) shown before the name in the hierarchy.
                const char* icon = nullptr;
                int depth = 0;
            };

            struct HierarchyMoveRequest
            {
                entt::entity dragged = entt::null;
                entt::entity newParent = entt::null;
                entt::entity insertBefore = entt::null;
            };

            enum class SceneSelectionMode
            {
                Replace, // plain click: select only the clicked entity
                Toggle,  // alt/meta click: add or remove a single entity
                Range    // shift click: select every entity between the anchor and the clicked entity
            };

            struct SceneSelectionRequest
            {
                entt::entity entity = entt::null;
                SceneSelectionMode mode = SceneSelectionMode::Replace;
                // Populated for Range mode: the ordered entities spanning anchor..clicked.
                std::vector<entt::entity> rangeEntities;
            };

            struct FlatpackEntry
            {
                std::string displayName;
                std::filesystem::path path;
            };

            struct ModelDefaultCallbacks
            {
                std::function<void()> heightDown;
                std::function<void()> heightUp;
                std::function<void()> rotationDown;
                std::function<void()> rotationUp;
                std::function<void()> scaleDown;
                std::function<void()> scaleUp;
                std::function<void()> apply;
                std::function<void()> reset;
            };

            enum class DeleteConfirmationAction
            {
                None,
                Confirm,
                Cancel
            };

            // Result of drawing the inspector for one frame. `changed` is true the
            // frame a field's value is written; `began`/`committed` bracket an edit
            // gesture so undo/redo can open and close a single transaction.
            struct InspectorEditResult
            {
                bool changed = false;
                bool began = false;
                bool committed = false;
            };

          private:
            enum class BrowserTab
            {
                Assets,
                Flatpacks
            };

            Settings* settings{};
            EditorDockLayout* dockLayout{};
            std::vector<AssetEntry> assetEntries;
            std::vector<RenderTexture2D> assetThumbnails;
            std::vector<SceneObjectEntry> hierarchyEntries;
            std::vector<FlatpackEntry> flatpackEntries;
            std::function<void(std::size_t)> onAssetSelectedCb;
            std::function<AssetRenameResult(std::size_t, const std::string&)> onAssetRenameCb;
            std::function<void(std::filesystem::path)> onFlatpackSelectedCb;
            std::function<void(const SceneSelectionRequest&)> onSceneObjectSelectedCb;
            std::function<void(const HierarchyMoveRequest&)> onHierarchyMoveCb;
            BrowserTab currentTab = BrowserTab::Assets;
            ImGuiTextFilter assetFilter;
            ImGuiTextFilter flatpackFilter;
            ImGuiTextFilter hierarchyFilter;
            ModelDefaultCallbacks modelDefaultCallbacks;
            DeleteConfirmationAction pendingDeleteConfirmationAction = DeleteConfirmationAction::None;
            std::optional<std::size_t> selectedAssetIndex;
            std::optional<std::size_t> renamingAssetIndex;
            std::vector<entt::entity> selectedSceneEntities;
            entt::entity hierarchySelectionAnchor = entt::null;
            std::optional<entt::entity> focusedHierarchyEntity;
            std::optional<entt::entity> pendingHierarchyContextEntity;
            std::string inspectorSelectedEntity = "None";
            std::vector<InspectedComponent> inspectedComponents;
            std::string assetDefaultsAssetName = "None";
            std::string assetDefaultsHeight = "0.00";
            std::string assetDefaultsRotation = "0";
            std::string assetDefaultsScale = "1.00";
            std::string assetRenameInput;
            std::string assetRenameStatus;
            std::string deleteConfirmationPrompt = "Delete selected entity?";
            bool deleteConfirmationVisible = false;
            bool assetRenamePopupOpenRequested = false;
            mutable std::string sceneNameStatus = "Scene";
            mutable std::string modeStatus = "Select";
            mutable std::string cursorStatus = "-";
            mutable std::string saveStatus = "";
            mutable bool sceneHasUnsavedChanges = false;
            bool dockLayoutChanged = false;

            RenderTexture2D createAssetThumbnail(const AssetEntry& asset) const;
            void openAssetRenamePopup(std::size_t index);
            void drawAssetRenamePopup();
            void drawAssetDefaultsControls();
            void drawAssetGrid();
            void drawFlatpackGrid();
            // Builds a selection request for a hierarchy click, reading the active
            // keyboard modifiers (shift = range, alt/meta = toggle single).
            [[nodiscard]] SceneSelectionRequest makeSceneSelectionRequest(entt::entity clicked) const;

          public:
            void StartImGui();
            void EndImGui();
            void DrawHierarchyWindow();
            InspectorEditResult DrawInspectorWindow();
            void DrawAssetDrawerWindow();
            void DrawDeleteConfirmationModal();
            void SetOverlayStatus(const std::string& mode, const std::string& cursor) const;
            void SetSaveStatus(const std::string& status, bool hasUnsavedChanges) const;
            void SetAssetDefaultsStatus(
                const std::string& assetName,
                const std::string& modelDefaultHeight,
                const std::string& modelDefaultRotation,
                const std::string& modelDefaultScale);
            void SetSceneName(const std::string& sceneName) const;
            void SetSelectedAsset(std::optional<std::size_t> index);
            void SetFlatpacks(std::vector<FlatpackEntry> entries);
            void SetHierarchy(
                const std::vector<SceneObjectEntry>& entries,
                std::vector<entt::entity> selectedEntities,
                entt::entity selectionAnchor);
            void FocusHierarchyOnEntity(entt::entity entity);
            [[nodiscard]] std::optional<entt::entity> ConsumeHierarchyContextEntity();
            void SetInspector(
                const std::string& selectedEntity, const std::vector<InspectedComponent>& inspectedComponents);
            void DrawSceneViewInfo() const;
            void ShowDeleteConfirmation(const std::string& selectedEntity);
            void HideDeleteConfirmation();
            [[nodiscard]] bool IsDeleteConfirmationVisible() const;
            [[nodiscard]] bool WantsMouseCapture() const;
            [[nodiscard]] bool WantsKeyboardCapture() const;
            [[nodiscard]] bool ConsumeDockLayoutChanged();
            [[nodiscard]] DeleteConfirmationAction ConsumeDeleteConfirmationAction();
            EditorGui(
                Settings* _settings,
                EditorDockLayout* _dockLayout,
                const std::vector<AssetEntry>& assets,
                const std::function<void(std::size_t)>& onAssetSelected,
                const std::function<AssetRenameResult(std::size_t, const std::string&)>& onAssetRename,
                const std::function<void(std::filesystem::path)>& onFlatpackSelected,
                const std::function<void(const SceneSelectionRequest&)>& onSceneObjectSelected,
                const std::function<void(const HierarchyMoveRequest&)>& onHierarchyMove,
                ModelDefaultCallbacks callbacks);
            ~EditorGui();
        };
    } // namespace editor
} // namespace sage

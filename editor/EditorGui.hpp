#pragma once

#include "EditorInspector.hpp"

#include "engine/Event.hpp"
#include "raylib.h"

#include "entt/entt.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sage
{
    class GameUIEngine;
    class ImageBox;
    class Table;
    class TextBox;
    class Window;
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
            };

            struct SceneObjectEntry
            {
                entt::entity entity = entt::null;
                std::string displayName;
                int depth = 0;
            };

            struct FlatpackEntry
            {
                std::string displayName;
                std::filesystem::path path;
            };

            enum class BrowserTab
            {
                Assets,
                Flatpacks
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

          private:
            Window* assetWindow{};
            Window* assetDefaultsWindow{};
            Window* deleteConfirmationWindow{};
            GameUIEngine* ui{};
            Settings* settings{};
            Texture2D editorWindowBackgroundTexture{};
            std::vector<AssetEntry> assetEntries;
            std::vector<RenderTexture2D> assetThumbnails;
            std::vector<ImageBox*> assetButtons;
            std::vector<SceneObjectEntry> hierarchyEntries;
            std::vector<FlatpackEntry> flatpackEntries;
            std::vector<TextBox*> browserTabButtons;
            std::function<void(std::size_t)> onAssetSelectedCb;
            std::function<void(std::filesystem::path)> onFlatpackSelectedCb;
            std::function<void(entt::entity)> onSceneObjectSelectedCb;
            std::function<void(entt::entity, entt::entity)> onHierarchyReparentCb;
            BrowserTab currentTab = BrowserTab::Assets;
            Subscription assetScrollSub{};
            ModelDefaultCallbacks modelDefaultCallbacks;
            DeleteConfirmationAction pendingDeleteConfirmationAction = DeleteConfirmationAction::None;
            std::optional<std::size_t> selectedAssetIndex;
            std::optional<entt::entity> selectedSceneEntity;
            std::optional<entt::entity> focusedHierarchyEntity;
            std::optional<entt::entity> pendingHierarchyContextEntity;
            std::string inspectorSelectedEntity = "None";
            std::vector<InspectedComponent> inspectedComponents;
            mutable std::string sceneNameStatus = "Scene";
            mutable std::string modeStatus = "Select";
            mutable std::string cursorStatus = "-";
            TextBox* defaultsAssetText{};
            TextBox* defaultsPositionText{};
            TextBox* defaultsRotationText{};
            TextBox* defaultsScaleText{};
            TextBox* deleteConfirmationText{};
            bool imGuiEnabled = false;

            RenderTexture2D createAssetThumbnail(const AssetEntry& asset) const;
            void createAssetWindow(
                const std::vector<AssetEntry>& assets, const std::function<void(std::size_t)>& onAssetSelected);
            void createAssetDefaultsWindow();
            void createDeleteConfirmationWindow();
            void refreshAssetButtonContent();

          public:
            void StartImGui();
            void EndImGui();
            void DrawHierarchyWindow();
            void DrawInspectorWindow();
            void SetOverlayStatus(const std::string& mode, const std::string& cursor) const;
            void SetAssetDefaultsStatus(
                const std::string& assetName,
                const std::string& modelDefaultHeight,
                const std::string& modelDefaultRotation,
                const std::string& modelDefaultScale) const;
            void SetSceneName(const std::string& sceneName) const;
            void SetSelectedAsset(std::optional<std::size_t> index);
            void SetFlatpacks(std::vector<FlatpackEntry> entries);
            [[nodiscard]] BrowserTab CurrentBrowserTab() const { return currentTab; }
            void SetHierarchy(
                const std::vector<SceneObjectEntry>& entries, std::optional<entt::entity> selectedEntity);
            void FocusHierarchyOnEntity(entt::entity entity);
            [[nodiscard]] std::optional<entt::entity> ConsumeHierarchyContextEntity();
            void SetInspector(
                const std::string& selectedEntity, const std::vector<InspectedComponent>& inspectedComponents);
            void DrawSceneViewInfo() const;
            void ShowDeleteConfirmation(const std::string& selectedEntity) const;
            void HideDeleteConfirmation() const;
            [[nodiscard]] bool IsDeleteConfirmationVisible() const;
            [[nodiscard]] bool WantsMouseCapture() const;
            [[nodiscard]] bool WantsKeyboardCapture() const;
            [[nodiscard]] DeleteConfirmationAction ConsumeDeleteConfirmationAction();
            EditorGui(
                GameUIEngine* _ui,
                Settings* _settings,
                const std::vector<AssetEntry>& assets,
                const std::function<void(std::size_t)>& onAssetSelected,
                const std::function<void(std::filesystem::path)>& onFlatpackSelected,
                const std::function<void(entt::entity)>& onSceneObjectSelected,
                const std::function<void(entt::entity, entt::entity)>& onHierarchyReparent,
                ModelDefaultCallbacks callbacks);
            ~EditorGui();
        };
    } // namespace editor
} // namespace sage

//
// Owns the editor's "current map" session: the open map path, the load/save
// file browsers, the transient save-feedback message, and persistence of the
// last-opened map into EditorSettings. The scene-side steps (clearing the
// scene, post-load refresh, naming the scene, collecting hierarchy order) are
// supplied as callbacks so this stays focused on map file I/O.
//

#pragma once

#include "entt/entt.hpp"

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
    struct EditorSettings;

    namespace editor
    {
        class EditorHistory;

        class EditorMapController
        {
          public:
            struct Callbacks
            {
                // Clears the current scene before a load (resets editor state and
                // destroys map entities).
                std::function<void()> prepareForLoad;
                // Runs post-load fixups (shaders, light transforms, placement grid,
                // selection, overlay/window refresh).
                std::function<void()> finishLoad;
                // Renames the scene/window after a load or save.
                std::function<void(const std::string&)> setSceneName;
                // Ensures the default map base exists, then returns the hierarchy
                // order to serialise. Called immediately before writing the map.
                std::function<std::vector<entt::entity>()> prepareSave;
            };

            EditorMapController(
                EngineSystems* sys,
                EditorSettings* editorSettings,
                std::function<void()> onEditorSettingsChanged,
                EditorHistory* history,
                Callbacks callbacks);
            ~EditorMapController();

            void Update(); // ticks down the transient save-feedback message
            void DrawBrowsers();
            void OpenLoadBrowser();
            void OpenSaveBrowser();
            void LoadMap(const std::filesystem::path& path);
            void SaveMap();
            void RestoreLastOpenedMap();

            [[nodiscard]] bool HasUnsavedChanges() const;
            [[nodiscard]] std::string CurrentSaveStatus() const;
            [[nodiscard]] std::string CurrentSceneName() const;

          private:
            void saveMapAs(const std::filesystem::path& path);
            void rememberCurrentMapPath();
            void markSaved(const std::filesystem::path& path);
            [[nodiscard]] std::filesystem::path browserDirectory() const;

            EngineSystems* sys{};
            EditorSettings* editorSettings{};
            std::function<void()> onEditorSettingsChanged;
            EditorHistory* history{};
            Callbacks callbacks;
            std::filesystem::path currentMapPath;
            float saveFeedbackRemaining = 0.0f;
            std::string saveFeedbackStatus;
            std::unique_ptr<ImGui::FileBrowser> loadMapBrowser;
            std::unique_ptr<ImGui::FileBrowser> saveMapBrowser;
        };
    } // namespace editor
} // namespace sage

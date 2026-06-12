#include "EditorMapController.hpp"

#include "EditorHistory.hpp"
#include "EditorMapLoader.hpp"
#include "EditorSettings.hpp"
#include "engine/EngineSystems.hpp"

#include "imgui.h"

#include "imfilebrowser.h"
#include "raylib.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <utility>

namespace sage::editor
{
    namespace
    {
        constexpr const char* UNTITLED_SCENE_NAME = "Untitled";
        constexpr const char* DEFAULT_SAVE_FILENAME = "untitled.map";
        constexpr float SAVE_FEEDBACK_SECONDS = 2.5f;
        constexpr ImGuiFileBrowserFlags LOAD_BROWSER_FLAGS =
            ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SkipItemsCausingError;
        constexpr ImGuiFileBrowserFlags SAVE_BROWSER_FLAGS =
            LOAD_BROWSER_FLAGS | ImGuiFileBrowserFlags_EnterNewFilename;

        std::filesystem::path ensureMapExtension(std::filesystem::path path)
        {
            if (path.extension() != ".map")
            {
                path.replace_extension(".map");
            }
            return path;
        }

        std::string sceneNameFromPath(const std::filesystem::path& path)
        {
            const auto stem = path.stem().string();
            return stem.empty() ? UNTITLED_SCENE_NAME : stem;
        }

        std::filesystem::path defaultBrowserDirectory(
            const std::filesystem::path& currentMapPath, const EditorSettings* editorSettings)
        {
            if (!currentMapPath.empty() && !currentMapPath.parent_path().empty())
            {
                return currentMapPath.parent_path();
            }

            if (editorSettings != nullptr && !editorSettings->lastVisitedDirectory.empty())
            {
                const std::filesystem::path lastVisited{editorSettings->lastVisitedDirectory};
                if (std::filesystem::is_directory(lastVisited))
                {
                    return lastVisited;
                }
            }

            const std::filesystem::path resources{"resources"};
            if (std::filesystem::is_directory(resources))
            {
                return resources;
            }

            return std::filesystem::current_path();
        }
    } // namespace

    EditorMapController::EditorMapController(
        EngineSystems* _sys,
        EditorSettings* _editorSettings,
        std::function<void()> _onEditorSettingsChanged,
        EditorHistory* _history,
        Callbacks _callbacks)
        : sys(_sys),
          editorSettings(_editorSettings),
          onEditorSettingsChanged(std::move(_onEditorSettingsChanged)),
          history(_history),
          callbacks(std::move(_callbacks))
    {
        loadMapBrowser = std::make_unique<ImGui::FileBrowser>(LOAD_BROWSER_FLAGS);
        loadMapBrowser->SetTitle("Load map");
        loadMapBrowser->SetTypeFilters({".map"});
        loadMapBrowser->SetDirectory(browserDirectory());

        saveMapBrowser = std::make_unique<ImGui::FileBrowser>(SAVE_BROWSER_FLAGS);
        saveMapBrowser->SetTitle("Save map as");
        saveMapBrowser->SetTypeFilters({".map"});
        saveMapBrowser->SetDirectory(browserDirectory());
        saveMapBrowser->SetInputName(DEFAULT_SAVE_FILENAME);
    }

    EditorMapController::~EditorMapController() = default;

    void EditorMapController::Update()
    {
        if (saveFeedbackRemaining > 0.0f)
        {
            saveFeedbackRemaining = std::max(0.0f, saveFeedbackRemaining - GetFrameTime());
        }
    }

    void EditorMapController::DrawBrowsers()
    {
        if (loadMapBrowser)
        {
            loadMapBrowser->Display();
            if (loadMapBrowser->HasSelected())
            {
                LoadMap(loadMapBrowser->GetSelected());
                loadMapBrowser->ClearSelected();
            }
        }

        if (saveMapBrowser)
        {
            saveMapBrowser->Display();
            if (saveMapBrowser->HasSelected())
            {
                saveMapAs(saveMapBrowser->GetSelected());
                saveMapBrowser->ClearSelected();
            }
        }
    }

    void EditorMapController::OpenLoadBrowser()
    {
        if (!loadMapBrowser) return;
        loadMapBrowser->SetDirectory(browserDirectory());
        loadMapBrowser->Open();
    }

    void EditorMapController::OpenSaveBrowser()
    {
        if (!saveMapBrowser) return;
        saveMapBrowser->SetDirectory(browserDirectory());
        saveMapBrowser->SetInputName(
            currentMapPath.empty() ? DEFAULT_SAVE_FILENAME : currentMapPath.filename().string());
        saveMapBrowser->Open();
    }

    std::filesystem::path EditorMapController::browserDirectory() const
    {
        return defaultBrowserDirectory(currentMapPath, editorSettings);
    }

    void EditorMapController::LoadMap(const std::filesystem::path& path)
    {
        const auto selectedPath = ensureMapExtension(path);
        const auto pathString = selectedPath.string();
        if (!editor::IsEditorLayoutMap(pathString.c_str()))
        {
            std::cerr << "ERROR: Not an editor layout map: " << pathString << std::endl;
            return;
        }

        if (callbacks.prepareForLoad) callbacks.prepareForLoad();
        if (!editor::LoadMap(sys->registry, pathString.c_str())) return;
        currentMapPath = selectedPath;
        if (callbacks.setSceneName) callbacks.setSceneName(sceneNameFromPath(currentMapPath));
        rememberCurrentMapPath();
        if (callbacks.finishLoad) callbacks.finishLoad();
        if (history) history->MarkSaved();
        saveFeedbackRemaining = 0.0f;
        saveFeedbackStatus.clear();
    }

    void EditorMapController::SaveMap()
    {
        if (currentMapPath.empty())
        {
            OpenSaveBrowser();
            return;
        }
        saveMapAs(currentMapPath);
    }

    void EditorMapController::saveMapAs(const std::filesystem::path& path)
    {
        // prepareSave ensures the default map base exists, then yields the hierarchy
        // order to serialise (the base entity must be in place before it is collected).
        std::vector<entt::entity> hierarchyOrder;
        if (callbacks.prepareSave) hierarchyOrder = callbacks.prepareSave();

        currentMapPath = ensureMapExtension(path);
        const auto pathString = currentMapPath.string();
        editor::SaveMap(*sys->registry, pathString.c_str(), hierarchyOrder);
        if (callbacks.setSceneName) callbacks.setSceneName(sceneNameFromPath(currentMapPath));
        rememberCurrentMapPath();
        markSaved(currentMapPath);
    }

    void EditorMapController::RestoreLastOpenedMap()
    {
        if (editorSettings == nullptr || editorSettings->lastOpenedMap.empty()) return;

        const std::filesystem::path path{editorSettings->lastOpenedMap};
        if (!std::filesystem::is_regular_file(path))
        {
            std::cerr << "WARNING: Last opened editor map no longer exists: " << path << std::endl;
            return;
        }

        LoadMap(path);
    }

    void EditorMapController::rememberCurrentMapPath()
    {
        if (editorSettings == nullptr || currentMapPath.empty()) return;

        editorSettings->lastOpenedMap = currentMapPath.string();
        if (!currentMapPath.parent_path().empty())
        {
            editorSettings->lastVisitedDirectory = currentMapPath.parent_path().string();
        }
        if (onEditorSettingsChanged)
        {
            onEditorSettingsChanged();
        }
    }

    void EditorMapController::markSaved(const std::filesystem::path& path)
    {
        if (history) history->MarkSaved();
        const auto fileName = path.filename().string();
        saveFeedbackStatus = fileName.empty() ? "Saved map" : std::format("Saved {}", fileName);
        saveFeedbackRemaining = SAVE_FEEDBACK_SECONDS;
    }

    bool EditorMapController::HasUnsavedChanges() const
    {
        return history && history->HasUnsavedChanges();
    }

    std::string EditorMapController::CurrentSaveStatus() const
    {
        if (HasUnsavedChanges()) return "Unsaved changes";
        if (saveFeedbackRemaining > 0.0f) return saveFeedbackStatus;
        return {};
    }

    std::string EditorMapController::CurrentSceneName() const
    {
        return sceneNameFromPath(currentMapPath);
    }
} // namespace sage::editor

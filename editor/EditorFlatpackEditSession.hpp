//
// Owns the editor's "open flatpack" session: Unity-prefab-style isolated
// editing of a .flatpack file in the shared viewport. Opening stashes the
// current map (scene, camera pose, unsaved-changes flag) to a temp file,
// clears the scene, and loads the flatpack at the origin; closing restores
// the stashed map. Only one flatpack is open at a time. The scene-side steps
// are supplied as callbacks so this stays focused on session state and file
// I/O, mirroring EditorMapController.
//

#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sage
{
    class EngineSystems;

    namespace editor
    {
        class EditorHistory;

        class EditorFlatpackEditSession
        {
          public:
            struct Callbacks
            {
                // Clears the current scene (resets editor state and destroys map
                // entities) before an open or a close.
                std::function<void()> clearScene;
                // Loads the flatpack at the origin with the usual placement fixups
                // (lit shader, collision bounds). Returns the root entity
                // (entt::null on failure).
                std::function<entt::entity(const std::filesystem::path&)> loadFlatpack;
                // Post-open fixups (map base, placement grid, overlay/window refresh).
                std::function<void()> finishOpen;
                // Ensures the default map base exists, then returns the hierarchy
                // order to serialise. Called immediately before stashing the map.
                std::function<std::vector<entt::entity>()> prepareMapStash;
                // Runs post-load fixups and restores the map's scene name after the
                // stashed map is reloaded on close.
                std::function<void()> finishMapRestore;
                // Renames the scene/window while a flatpack is open.
                std::function<void(const std::string&)> setSceneName;
                // Refreshes the asset drawer's flatpack catalog after a save.
                std::function<void()> catalogChanged;
            };

            EditorFlatpackEditSession(EngineSystems* sys, EditorHistory* history, Callbacks callbacks);

            void Update(); // ticks down the transient save-feedback message

            // Opens `path` for isolated editing. If another flatpack is already
            // open it is closed first (prompting about unsaved changes), then the
            // requested one opens.
            void Open(std::filesystem::path path);
            void Save();
            // Closes the session and restores the stashed map, prompting about
            // unsaved changes when needed.
            void RequestClose();
            void DrawCloseConfirmationModal();

            [[nodiscard]] bool IsActive() const;
            [[nodiscard]] entt::entity Root() const;
            [[nodiscard]] const std::filesystem::path& Path() const;
            [[nodiscard]] std::string FlatpackName() const;
            [[nodiscard]] bool HasUnsavedChanges() const;
            [[nodiscard]] bool StashedMapHadUnsavedChanges() const;
            [[nodiscard]] std::string CurrentSaveStatus() const;

          private:
            void open(const std::filesystem::path& path);
            void close();
            void restoreStashedMap();

            EngineSystems* sys{};
            EditorHistory* history{};
            Callbacks callbacks;

            std::filesystem::path flatpackPath;
            entt::entity root = entt::null;
            bool active = false;

            // Stash of the map session while a flatpack is open.
            std::filesystem::path stashPath;
            bool stashedMapDirty = false;
            Camera3D stashedCamera{};

            bool closePromptRequested = false;
            // Set when Open is called while another flatpack is active: opened
            // once the close (and any prompt) resolves.
            std::filesystem::path pendingOpenPath;

            float saveFeedbackRemaining = 0.0f;
            std::string saveFeedbackStatus;
        };
    } // namespace editor
} // namespace sage

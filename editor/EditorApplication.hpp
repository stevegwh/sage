#pragma once

#include "EditorDockLayout.hpp"
#include "EditorSettings.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <memory>

namespace sage
{
    class AudioManager;
    class EngineSystems;
    class KeyMapping;
    struct Settings;
    class EditorScene;

    class EditorApplication
    {
        RenderTexture renderTexture{};
        // Game UI is rendered into this (sized to the app viewport) during play
        // and blitted at the viewport offset, mirroring the game's own 2D
        // compositing so its mouse mapping and scissor clipping stay consistent.
        RenderTexture gameUiTexture{};
        editor::EditorDockLayout dockLayout{};
        EditorSettings editorSettings{};

        std::unique_ptr<entt::registry> registry;
        std::unique_ptr<KeyMapping> keyMapping;
        std::unique_ptr<Settings> settings;
        std::unique_ptr<AudioManager> audioManager;
        std::unique_ptr<EngineSystems> systems;
        std::unique_ptr<EditorScene> scene;

        bool exitWindowRequested = false;
        bool exitWindow = false;
        bool viewportFullscreen = false;

        void init();
        void draw();
        void handleScreenUpdate();
        void handleWindowResize();
        void handleViewportFullscreenToggle();
        void refreshViewportLayout(Vector2 previousViewport);
        void saveEditorSettings() const;

      public:
        void Update();
        EditorApplication();
        ~EditorApplication();

        EditorApplication(const EditorApplication&) = delete;
        EditorApplication& operator=(const EditorApplication&) = delete;
    };
} // namespace sage

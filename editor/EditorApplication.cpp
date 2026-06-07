#include "EditorApplication.hpp"

#include "EditorScene.hpp"

#include "engine/AudioManager.hpp"
#include "engine/Camera.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/KeyMapping.hpp"
#include "engine/Serializer.hpp"
#include "engine/Settings.hpp"
#include "engine/UserInput.hpp"

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include <algorithm>

namespace sage
{
    namespace
    {
        constexpr const char* EDITOR_SETTINGS_PATH = "resources/editor-settings.xml";

        void ConfigureEditorSceneViewport(
            Settings& settings, editor::EditorDockLayout& dockLayout, const bool fullscreen)
        {
            editor::ClampEditorDockLayout(dockLayout);
            const Rectangle viewport = editor::CalculateEditorSceneViewport(settings, dockLayout, fullscreen);
            settings.SetRenderViewport(
                static_cast<int>(viewport.width), static_cast<int>(viewport.height), {viewport.x, viewport.y});
        }

        RenderTexture LoadFilteredRenderTexture(const int width, const int height)
        {
            auto texture = LoadRenderTexture(std::max(1, width), std::max(1, height));
            SetTextureFilter(texture.texture, TEXTURE_FILTER_BILINEAR);
            return texture;
        }
    } // namespace

    void EditorApplication::init()
    {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
        const auto screenSize = settings->GetScreenSize();
        InitWindow(static_cast<int>(screenSize.x), static_cast<int>(screenSize.y), "BG Raylib Editor");
        settings->UpdateViewport();
        ConfigureEditorSceneViewport(*settings, dockLayout, viewportFullscreen);
        SetExitKey(KEY_NULL);
        EnableCursor();

        systems =
            std::make_unique<EngineSystems>(registry.get(), keyMapping.get(), settings.get(), audioManager.get());

        serializer::LoadAssetBinFile(registry.get(), "resources/assets.bin");
        if (FileExists("resources/editor-map-assets.bin"))
        {
            serializer::LoadAssetBinFile(registry.get(), "resources/editor-map-assets.bin");
        }
        scene = std::make_unique<EditorScene>(
            systems.get(), &dockLayout, &editorSettings, [this]() { saveEditorSettings(); });

        const auto renderViewport = settings->GetRenderViewPort();
        renderTexture =
            LoadFilteredRenderTexture(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
        rlImGuiSetup(true);
    }

    void EditorApplication::draw()
    {
        BeginTextureMode(renderTexture);
        ClearBackground(BLANK);
        BeginMode3D(*systems->camera->getRaylibCam());
        scene->Draw3D();
        EndMode3D();
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);

        const auto appViewportOffset = settings->GetViewportOffset();
        const auto renderViewport = settings->GetRenderViewPort();
        const auto renderViewportOffset = settings->GetRenderViewportOffset();

        DrawTextureRec(
            renderTexture.texture,
            {0, 0, renderViewport.x, -renderViewport.y},
            {appViewportOffset.x + renderViewportOffset.x, appViewportOffset.y + renderViewportOffset.y},
            WHITE);

        scene->DrawOverlay2D();
        scene->DrawImGui(exitWindowRequested, exitWindow);
        DrawFPS(12, 32);

        EndDrawing();
    }

    void EditorApplication::handleScreenUpdate()
    {
        if (!settings->toggleFullScreenRequested) return;

        const auto prev = settings->GetViewPort();

#ifdef __APPLE__
        if (!IsWindowFullscreen())
        {
            const int monitor = GetCurrentMonitor();
            SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
            settings->SetScreenSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
            ToggleFullscreen();
        }
        else
        {
            ToggleFullscreen();
            settings->ResetToUserDefined();
            const auto screen = settings->GetScreenSize();
            SetWindowSize(static_cast<int>(screen.x), static_cast<int>(screen.y));
        }
#else
        const bool maximized = GetScreenWidth() == GetMonitorWidth(GetCurrentMonitor()) &&
                               GetScreenHeight() == GetMonitorHeight(GetCurrentMonitor());
        if (!maximized)
        {
            const int monitor = GetCurrentMonitor();
            SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
            settings->SetScreenSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
            ToggleBorderlessWindowed();
        }
        else
        {
            ToggleBorderlessWindowed();
            settings->ResetToUserDefined();
            const auto screen = settings->GetScreenSize();
            SetWindowSize(static_cast<int>(screen.x), static_cast<int>(screen.y));
        }
#endif

        settings->toggleFullScreenRequested = false;
        refreshViewportLayout(prev);
    }

    void EditorApplication::refreshViewportLayout(const Vector2 previousViewport)
    {
        settings->SetScreenSize(GetScreenWidth(), GetScreenHeight());
        ConfigureEditorSceneViewport(*settings, dockLayout, viewportFullscreen);
        const auto appViewport = settings->GetViewPort();
        systems->userInput->onWindowUpdate.Publish(previousViewport, appViewport);

        UnloadRenderTexture(renderTexture);
        const auto renderViewport = settings->GetRenderViewPort();
        renderTexture =
            LoadFilteredRenderTexture(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
    }

    void EditorApplication::handleViewportFullscreenToggle()
    {
        if (!(IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) || !IsKeyPressed(KEY_F)) return;
        viewportFullscreen = !viewportFullscreen;
        scene->SetViewportFullscreen(viewportFullscreen);
        refreshViewportLayout(settings->GetViewPort());
    }

    void EditorApplication::saveEditorSettings() const
    {
        serializer::SaveClassXML<EditorSettings>(EDITOR_SETTINGS_PATH, editorSettings);
    }

    void EditorApplication::handleWindowResize()
    {
        const auto screen = settings->GetScreenSize();
        if (static_cast<int>(screen.x) == GetScreenWidth() && static_cast<int>(screen.y) == GetScreenHeight())
        {
            return;
        }

        refreshViewportLayout(settings->GetViewPort());
    }

    void EditorApplication::Update()
    {
        init();
        SetTargetFPS(60);

        while (!exitWindow)
        {
            if (WindowShouldClose()) exitWindowRequested = true;
            if (IsKeyPressed(KEY_ESCAPE)) (void)scene->HandleEscapePressed();

            handleWindowResize();
            handleViewportFullscreenToggle();
            scene->Update();
            draw();
            if (scene->ConsumeDockLayoutChanged())
            {
                refreshViewportLayout(settings->GetViewPort());
            }
            handleScreenUpdate();
        }
    }

    EditorApplication::EditorApplication()
        : registry(std::make_unique<entt::registry>()),
          keyMapping(std::make_unique<KeyMapping>()),
          settings(std::make_unique<Settings>(&exitWindow)),
          audioManager(std::make_unique<AudioManager>())
    {
        serializer::DeserializeXMLFile<EditorSettings>(EDITOR_SETTINGS_PATH, editorSettings);
    }

    EditorApplication::~EditorApplication()
    {
        rlImGuiShutdown();
        UnloadRenderTexture(renderTexture);
        CloseWindow();
    }
} // namespace sage

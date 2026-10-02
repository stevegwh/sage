#include "EditorApplication.hpp"
#include "engine/Colors.hpp"

#include "EditorScene.hpp"

#include "engine/AudioManager.hpp"
#include "engine/BloomPass.hpp"
#include "engine/Camera.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/KeyMapping.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneRenderTarget.hpp"
#include "engine/Serializer.hpp"
#include "engine/Settings.hpp"
#include "engine/systems/RenderSystem.hpp"
#include "engine/UserInput.hpp"

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "ShaderPaths.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <utility>

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
                static_cast<int>(viewport.width),
                static_cast<int>(viewport.height),
                {.x = viewport.x, .y = viewport.y});
        }

        RenderTexture LoadFilteredRenderTexture(const int width, const int height)
        {
            auto texture = LoadRenderTexture(std::max(1, width), std::max(1, height));
            SetTextureFilter(texture.texture, TEXTURE_FILTER_BILINEAR);
            return texture;
        }

        void DrawViewportFpsCounter(const Settings& settings)
        {
            const auto viewport = settings.GetRenderViewPort();
            const float scale = Settings::GetScreenScaleFactor(viewport.x, viewport.y);
            const int fps = GetFPS();
            const char* fpsText = TextFormat("%i FPS", fps);
            const int fontSize = std::max(10, static_cast<int>(std::round(20.0f * scale)));
            const int padding = std::max(4, static_cast<int>(std::round(10.0f * scale)));
            const int textWidth = MeasureText(fpsText, fontSize);

            Color color = sage::colors::LIME_COLOR;
            if (fps < 15)
            {
                color = sage::colors::RED_COLOR;
            }
            else if (fps < 30)
            {
                color = sage::colors::ORANGE_COLOR;
            }

            const int x = std::max(padding, static_cast<int>(viewport.x) - textWidth - padding);
            DrawText(fpsText, x, padding, fontSize, color);
        }
    } // namespace

    void EditorApplication::initWindow()
    {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
        const auto screenSize = settings->GetScreenSize();
        InitWindow(static_cast<int>(screenSize.x), static_cast<int>(screenSize.y), "BG Raylib Editor");
        windowReady = true;
        settings->UpdateViewport();
        ConfigureEditorSceneViewport(*settings, dockLayout, viewportFullscreen);
        SetExitKey(KEY_NULL);
        EnableCursor();
        SetTargetFPS(60);
    }

    void EditorApplication::drawLoadingScreen(const char* stage)
    {
        if (WindowShouldClose()) exitWindowRequested = true;

        const int width = GetScreenWidth();
        const int height = GetScreenHeight();
        const char* title = "Loading editor";
        const int titleSize = 30;
        const int stageSize = 20;
        const int barWidth = std::min(400, width - 80);
        const int barX = (width - barWidth) / 2;
        const int barY = height / 2 + 50;
        const int markerWidth = std::max(20, barWidth / 5);
        const auto travel = static_cast<float>(std::max(0, barWidth - markerWidth));
        const auto phase = static_cast<float>(std::fmod(GetTime() * 0.8, 2.0));
        const float position = phase <= 1.0f ? phase : 2.0f - phase;

        BeginDrawing();
        ClearBackground({.r = 24, .g = 27, .b = 34, .a = 255});
        DrawText(
            title,
            (width - MeasureText(title, titleSize)) / 2,
            height / 2 - 65,
            titleSize,
            sage::colors::RAY_WHITE_COLOR);
        DrawText(
            stage,
            (width - MeasureText(stage, stageSize)) / 2,
            height / 2 - 10,
            stageSize,
            sage::colors::LIGHT_GRAY_COLOR);
        DrawRectangle(barX, barY, barWidth, 8, {.r = 55, .g = 61, .b = 72, .a = 255});
        DrawRectangle(
            barX + static_cast<int>(position * travel), barY, markerWidth, 8, sage::colors::SKY_BLUE_COLOR);
        EndDrawing();
    }

    void EditorApplication::initEditor()
    {
        drawLoadingScreen("Initializing engine...");

        systems =
            std::make_unique<EngineSystems>(registry.get(), keyMapping.get(), settings.get(), audioManager.get());

        drawLoadingScreen("Loading packed assets...");
        serializer::LoadAssetBinFile(
            registry.get(), "resources/assets.bin", [this]() { drawLoadingScreen("Loading packed assets..."); });
        drawLoadingScreen("Preparing scene...");
        colorGradeShader =
            ResourceManager::GetInstance().ShaderLoad(std::nullopt, ShaderPath("custom/color_grade.fs"));
        bloomTextureLocation = GetShaderLocation(colorGradeShader, "bloomTexture");
        SetSceneGraphicsUniforms(colorGradeShader, settings->GetGraphicsSettings());
        if (!skyboxImageKey.empty()) systems->renderSystem->SetSkybox(skyboxImageKey);
        auto lastMapUpdate = std::chrono::steady_clock::now();
        scene = std::make_unique<EditorScene>(
            systems.get(),
            &dockLayout,
            &editorSettings,
            [this]() { saveEditorSettings(); },
            registerGameComponents,
            csharpScripts,
            [this, &lastMapUpdate]() {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastMapUpdate < std::chrono::milliseconds(50)) return;
                drawLoadingScreen("Restoring last map...");
                lastMapUpdate = now;
            });

        const auto renderViewport = settings->GetRenderViewPort();
        renderTexture =
            LoadSceneRenderTarget(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
        // Game UI shares the (docked) render viewport so it scales to and centres
        // in the same area as the game's 3D view.
        gameUiTexture =
            LoadFilteredRenderTexture(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
        bloomPass =
            std::make_unique<BloomPass>(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
        rlImGuiSetup(true);
        imguiReady = true;

#if !defined(__APPLE__)
        // Keep Raylib's window and framebuffer coordinates identical. Its Linux
        // borderless-window implementation uses monitor pixel dimensions, which
        // makes the editor viewport overflow when FLAG_WINDOW_HIGHDPI is active.
        // Scale ImGui itself instead so desktop DPI affects the editor UI only.
        const Vector2 dpiScale = GetWindowScaleDPI();
        const float editorUiScale = std::max(dpiScale.x, dpiScale.y);
        ImGui::GetIO().FontGlobalScale = editorUiScale;
        ImGui::GetStyle().ScaleAllSizes(editorUiScale);
#endif
    }

    void EditorApplication::draw()
    {
        const bool playing = scene->IsPlaying();

        scene->DrawShadowMap();

        BeginTextureMode(renderTexture);
        ClearBackground(sage::colors::BLANK_COLOR);
        // ActiveCamera() is the running game's camera during play, the editor's
        // otherwise.
        BeginMode3D(*scene->ActiveCamera());
        scene->Draw3D();
        EndMode3D();
        if (!playing) DrawViewportFpsCounter(*settings);
        EndTextureMode();

        if (settings->GetGraphicsSettings().bloom)
        {
            BeginTextureMode(bloomPass->MaskTarget());
            ClearBackground(sage::colors::BLACK_COLOR);
            BeginMode3D(*scene->ActiveCamera());
            scene->DrawBloomMask();
            EndMode3D();
            EndTextureMode();
            bloomPass->Blur();
        }

        if (playing)
        {
            // Render the game UI into a viewport-sized texture at viewport-local
            // coords (so its scissor clipping stays consistent), then blit it at
            // the viewport offset where the game's mouse mapping expects it.
            BeginTextureMode(gameUiTexture);
            ClearBackground(sage::colors::BLANK_COLOR);
            scene->DrawGame2D();
            DrawViewportFpsCounter(*settings);
            EndTextureMode();
        }

        SetSceneGraphicsUniforms(colorGradeShader, settings->GetGraphicsSettings());
        scene->CaptureAutomationFrame(
            renderTexture, playing ? gameUiTexture.texture : Texture2D{}, colorGradeShader, bloomPass->Texture());

        BeginDrawing();
        ClearBackground(sage::colors::BLACK_COLOR);

        const auto appViewportOffset = settings->GetViewportOffset();
        const auto renderViewport = settings->GetRenderViewPort();
        const auto renderViewportOffset = settings->GetRenderViewportOffset();

        BeginShaderMode(colorGradeShader);
        SetSceneOcclusionUniforms(colorGradeShader, renderTexture, *scene->ActiveCamera());
        SetShaderValueTexture(colorGradeShader, bloomTextureLocation, bloomPass->Texture());
        DrawTextureRec(
            renderTexture.texture,
            {.x = 0, .y = 0, .width = renderViewport.x, .height = -renderViewport.y},
            {.x = appViewportOffset.x + renderViewportOffset.x, .y = appViewportOffset.y + renderViewportOffset.y},
            sage::colors::WHITE_COLOR);
        EndShaderMode();

        if (playing)
        {
            DrawTextureRec(
                gameUiTexture.texture,
                {.x = 0, .y = 0, .width = renderViewport.x, .height = -renderViewport.y},
                {.x = appViewportOffset.x + renderViewportOffset.x,
                 .y = appViewportOffset.y + renderViewportOffset.y},
                sage::colors::WHITE_COLOR);
        }

        scene->DrawOverlay2D();
        scene->DrawImGui(exitWindowRequested, exitWindow);

        EndDrawing();
    }

    void EditorApplication::handleScreenUpdate()
    {
        if (!settings->toggleFullScreenRequested) return;

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
        refreshViewportLayout();
    }

    void EditorApplication::refreshViewportLayout()
    {
        settings->SetScreenSize(GetScreenWidth(), GetScreenHeight());
        ConfigureEditorSceneViewport(*settings, dockLayout, viewportFullscreen);

        UnloadRenderTexture(renderTexture);
        const auto renderViewport = settings->GetRenderViewPort();
        renderTexture =
            LoadSceneRenderTarget(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));

        UnloadRenderTexture(gameUiTexture);
        gameUiTexture =
            LoadFilteredRenderTexture(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
        bloomPass->Resize(static_cast<int>(renderViewport.x), static_cast<int>(renderViewport.y));
    }

    void EditorApplication::handleViewportFullscreenToggle()
    {
        if (!(IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) || !IsKeyPressed(KEY_F)) return;
        viewportFullscreen = !viewportFullscreen;
        scene->SetViewportFullscreen(viewportFullscreen);
        refreshViewportLayout();
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

        refreshViewportLayout();
    }

    bool EditorApplication::Update()
    {
        initWindow();
        drawLoadingScreen("Checking assets...");
        if (prepareAssets)
        {
            auto packing = std::async(std::launch::async, prepareAssets);
            while (packing.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready)
                drawLoadingScreen("Checking or packing assets...");
            if (!packing.get()) return false;
        }
        if (exitWindowRequested) return true;
        initEditor();
        if (exitWindowRequested) return true;

        while (!exitWindow)
        {
            if (WindowShouldClose()) exitWindowRequested = true;
            if (IsKeyPressed(KEY_ESCAPE)) static_cast<void>(scene->HandleEscapePressed());

            handleWindowResize();
            handleViewportFullscreenToggle();
            scene->Update();
            draw();
            if (scene->ConsumeDockLayoutChanged())
            {
                refreshViewportLayout();
            }
            handleScreenUpdate();
        }
        return true;
    }

    EditorApplication::EditorApplication(
        std::string _skyboxImageKey,
        std::function<void(editor::InspectorRegistry&)> _registerGameComponents,
        editor::CSharpScriptEditorConfig _csharpScripts,
        std::function<bool()> _prepareAssets)
        : registry(std::make_unique<entt::registry>()),
          keyMapping(std::make_unique<KeyMapping>()),
          settings(std::make_unique<Settings>(&exitWindow)),
          audioManager(std::make_unique<AudioManager>()),
          skyboxImageKey(std::move(_skyboxImageKey)),
          registerGameComponents(std::move(_registerGameComponents)),
          csharpScripts(std::move(_csharpScripts)),
          prepareAssets(std::move(_prepareAssets))
    {
        serializer::DeserializeXMLFile<EditorSettings>(EDITOR_SETTINGS_PATH, editorSettings);
    }

    EditorApplication::~EditorApplication()
    {
        if (imguiReady) rlImGuiShutdown();
        if (renderTexture.id != 0) UnloadRenderTexture(renderTexture);
        if (gameUiTexture.id != 0) UnloadRenderTexture(gameUiTexture);
        bloomPass.reset();
        scene.reset();
        systems.reset();
        if (windowReady) CloseWindow();
    }
} // namespace sage

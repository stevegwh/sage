//
// The play-in-editor boundary. The editor lives in the engine (sage) layer and
// must not depend on game (lq) code, but "playing" a map means running the real
// game systems. IGameRuntime inverts that dependency: the game layer implements
// a self-contained, tickable world and registers a factory; the editor drives
// it through this interface without any compile-time knowledge of lq.
//

#pragma once

#include "systems/CSharpScriptSystem.hpp"

#include "content/Json.hpp"
#include "raylib.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace sage
{
    class AudioManager;
    struct LightSettings;
    struct GraphicsSettings;

    // What the editor hands a runtime when entering play mode. The runtime owns
    // its own registry, scene, settings and per-frame systems; only the audio
    // device is shared (one open device). viewportScreenRect is the editor's
    // docked scene-view rectangle in window coordinates, so the game scales,
    // centres and hit-tests its UI against the play area rather than the whole
    // window.
    struct GameRuntimeContext
    {
        std::optional<std::reference_wrapper<AudioManager>> audioManager;
        Vector2 windowSize{};
        Rectangle viewportScreenRect{};
        // Working-dir-relative path to the map the editor snapshotted for this
        // play session.
        std::string mapPath;
        // Optional editor-owned destination for Sage.Log calls made by hosted C#
        // scripts during this play session.
        CSharpLogSink managedLogSink;
    };

    // A live, tickable game world. Created fresh on Play and destroyed on Stop,
    // so its registry never touches the editor's scene.
    class IGameRuntime
    {
      public:
        IGameRuntime() = default;
        IGameRuntime(const IGameRuntime&) = default;
        IGameRuntime& operator=(const IGameRuntime&) = default;
        IGameRuntime(IGameRuntime&&) = default;
        IGameRuntime& operator=(IGameRuntime&&) = default;
        virtual ~IGameRuntime() = default;

        // One simulation step (input, systems, cleanup).
        virtual void Update(bool listenForInput = true) = 0;
        virtual void SetPaused(bool paused) = 0;
        [[nodiscard]] virtual bool IsPaused() const = 0;
        // Borrowed only for inspection on the window thread. The runtime retains ownership.
        [[nodiscard]] virtual entt::registry& InspectionRegistry() = 0;
        [[nodiscard]] virtual json::Document InspectScript(entt::entity entity) = 0;
        virtual void Step(float deltaTime, std::uint32_t frames = 1) = 0;
        virtual json::Document Inspect() = 0;
        virtual json::Document Command(const json::Value& command) = 0;
        virtual void Draw3D() = 0;
        virtual void DrawBloomMask() = 0;
        virtual void DrawShadowMap() = 0;
        virtual void Draw2D() = 0;
        virtual void ApplyProjectSettings(const LightSettings& light, const GraphicsSettings& graphics) = 0;

        // Keeps the game's viewport pinned to the editor's docked scene view as
        // it moves/resizes; called each frame before Update.
        virtual void SetViewport(Rectangle screenRect) = 0;

        // The camera the editor viewport should render through while playing.
        [[nodiscard]] virtual Camera3D* GetCamera() = 0;
    };

    using GameRuntimeFactory = std::function<std::unique_ptr<IGameRuntime>(const GameRuntimeContext&)>;

    // Registered once by the game executable before the editor starts. The
    // standalone editor leaves it unset, which disables play mode.
    void SetGameRuntimeFactory(GameRuntimeFactory factory);
    [[nodiscard]] bool HasGameRuntimeFactory();

    // Builds a runtime via the registered factory; returns nullptr when no
    // factory is registered or construction throws.
    [[nodiscard]] std::unique_ptr<IGameRuntime> CreateGameRuntime(const GameRuntimeContext& context);

    // The game host launches its standalone executable. On success the child
    // owns the snapshot and removes it when it exits; on failure the editor does.
    using StandaloneGameLauncher = std::function<bool(const std::string& mapPath)>;
    void SetStandaloneGameLauncher(StandaloneGameLauncher launcher);
    [[nodiscard]] bool HasStandaloneGameLauncher();
    [[nodiscard]] bool LaunchStandaloneGame(const std::string& mapPath);
} // namespace sage

//
// The play-in-editor boundary. The editor lives in the engine (sage) layer and
// must not depend on game (lq) code, but "playing" a map means running the real
// game systems. IGameRuntime inverts that dependency: the game layer implements
// a self-contained, tickable world and registers a factory; the editor drives
// it through this interface without any compile-time knowledge of lq.
//

#pragma once

#include "raylib.h"

#include <functional>
#include <memory>
#include <string>

namespace sage
{
    class AudioManager;

    // What the editor hands a runtime when entering play mode. The runtime owns
    // its own registry, scene, settings and per-frame systems; only the audio
    // device is shared (one open device). viewportScreenRect is the editor's
    // docked scene-view rectangle in window coordinates, so the game scales,
    // centres and hit-tests its UI against the play area rather than the whole
    // window.
    struct GameRuntimeContext
    {
        AudioManager* audioManager = nullptr;
        Vector2 windowSize{};
        Rectangle viewportScreenRect{};
        // Working-dir-relative path to the map the editor snapshotted for this
        // play session.
        std::string mapPath;
    };

    // A live, tickable game world. Created fresh on Play and destroyed on Stop,
    // so its registry never touches the editor's authored scene.
    class IGameRuntime
    {
      public:
        virtual ~IGameRuntime() = default;

        // One simulation step (input, systems, cleanup).
        virtual void Update() = 0;
        virtual void Draw3D() = 0;
        virtual void Draw2D() = 0;

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
} // namespace sage

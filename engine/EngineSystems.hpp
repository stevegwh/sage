//
// Created by Steve Wheeler on 27/03/2024.
//

#pragma once

#include "entt/entt.hpp"

#include <memory>

namespace sage
{
    // Core structs/classes
    struct Settings;
    struct KeyMapping;
    class AudioManager;

    // Input/UI group
    class UserInput;
    class Cursor;
    class Picker;
    class Camera;
    class LightManager;
    class GameUIEngine;

    // Systems group
    class TransformSystem;
    class RenderSystem;
    class CollisionSystem;
    class NavigationGridSystem;
    class ActorMovementSystem;
    class ControllableActorSystem;
    class AnimationSystem;
    class UberShaderSystem;
    class FullscreenTextOverlayManager;
    class SpatialAudioSystem;
    class DoorSystem;

    class EngineSystems
    {
      public:
        entt::registry* registry;
        Settings* settings;
        AudioManager* audioManager;
        std::unique_ptr<GameUIEngine> uiEngine;

        std::unique_ptr<UserInput> userInput;
        std::unique_ptr<Camera> camera;
        std::unique_ptr<Picker> picker;
        std::unique_ptr<Cursor> cursor;
        std::unique_ptr<LightManager> lightSubSystem;

        std::unique_ptr<TransformSystem> transformSystem;
        std::unique_ptr<RenderSystem> renderSystem;
        std::unique_ptr<CollisionSystem> collisionSystem;
        std::unique_ptr<NavigationGridSystem> navigationGridSystem;
        std::unique_ptr<ActorMovementSystem> actorMovementSystem;
        std::unique_ptr<AnimationSystem> animationSystem;
        std::unique_ptr<UberShaderSystem> uberShaderSystem;
        std::unique_ptr<FullscreenTextOverlayManager> fullscreenTextOverlayFactory;
        std::unique_ptr<SpatialAudioSystem> spatialAudioSystem;
        std::unique_ptr<DoorSystem> doorSystem;

        EngineSystems(
            entt::registry* _registry, KeyMapping* _keyMapping, Settings* _settings, AudioManager* _audioManager);
        ~EngineSystems();

        [[nodiscard]] GameUIEngine& UI();
        [[nodiscard]] const GameUIEngine& UI() const;
        void ReplaceUiEngine(std::unique_ptr<GameUIEngine> replacement);
    };
} // namespace sage

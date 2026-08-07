//
// Created by Steve Wheeler on 27/03/2024.
//

#include "EngineSystems.hpp"
#include "systems/LuaBinding.hpp"

#include "Archetypes.hpp"
#include "Flatpack.hpp"
#include "Serializer.hpp"

#include "Camera.hpp"
#include "components/Animation.hpp"
#include "components/Collideable.hpp"
#include "components/CollisionIntent.hpp"
#include "components/MoveableActor.hpp"
#include "components/sgTransform.hpp"
#include "Cursor.hpp"
#include "FullscreenTextOverlayManager.hpp"
#include "LightManager.hpp"
#include "MousePicker.hpp"
#include "Settings.hpp"
#include "systems/ActorMovementSystem.hpp"
#include "systems/AnimationSystem.hpp"
#include "systems/CollisionSystem.hpp"
#include "systems/ControllableActorSystem.hpp"
#include "systems/NavigationGridSystem.hpp"
#include "systems/RenderSystem.hpp"
#include "systems/ScriptSystem.hpp"
#include "systems/SpatialAudioSystem.hpp"
#include "systems/TransformSystem.hpp"
#include "systems/UberShaderSystem.hpp"
#include "ui/GameUIEngine.hpp"
#include "UserInput.hpp"

#include "sol/sol.hpp"

#include <cassert>
#include <cstdint>
#include <optional>

namespace sage
{
    EngineSystems::EngineSystems(
        entt::registry* _registry, KeyMapping* _keyMapping, Settings* _settings, AudioManager* _audioManager)
        : registry(_registry),
          settings(_settings),
          audioManager(_audioManager),
          userInput(std::make_unique<UserInput>(_keyMapping, _settings)),
          camera(std::make_unique<Camera>(_registry, userInput.get(), this)),
          picker(std::make_unique<MousePicker>(_registry, this)),
          cursor(std::make_unique<Cursor>(_registry, this)),
          lightSubSystem(std::make_unique<LightManager>(_registry, camera.get(), _settings->GetLightSettings())),
          transformSystem(std::make_unique<TransformSystem>(_registry)),
          renderSystem(std::make_unique<RenderSystem>(_registry)),
          collisionSystem(std::make_unique<CollisionSystem>(_registry)),
          navigationGridSystem(std::make_unique<NavigationGridSystem>(_registry, collisionSystem.get())),
          actorMovementSystem(std::make_unique<ActorMovementSystem>(_registry, this)),
          controllableActorSystem(std::make_unique<ControllableActorSystem>(_registry, this)),
          animationSystem(std::make_unique<AnimationSystem>(_registry)),
          uberShaderSystem(std::make_unique<UberShaderSystem>(_registry, this)),
          fullscreenTextOverlayManager(std::make_unique<FullscreenTextOverlayManager>(this)),
          spatialAudioSystem(std::make_unique<SpatialAudioSystem>(_registry, this)),
          scriptSystem(std::make_unique<ScriptSystem>(_registry))
    {
        uiEngine = std::make_unique<GameUIEngine>(_registry, this);
        RegisterAllLuaBindings();
    }

    void EngineSystems::RegisterAllLuaBindings()
    {
        RegisterArchetypeLuaApi(*scriptSystem);
        scriptSystem->RegisterComponent<sgTransform>("Transform", "GetTransform");
        scriptSystem->RegisterComponent<Collideable>("Collideable", "GetCollideable");
        scriptSystem->RegisterComponent<Animation>("Animation", "GetAnimation");

        collisionSystem->RegisterLuaBindings(*scriptSystem);
        navigationGridSystem->RegisterLuaBindings(*scriptSystem);
        actorMovementSystem->RegisterLuaBindings(*scriptSystem);
        animationSystem->RegisterLuaBindings(*scriptSystem);

        scriptSystem->RegisterApiExtension(
            "sage", [this](sol::table& api, entt::entity, entt::registry& destination) {
                const auto spawnFlatpack =
                    [this, &destination](
                        const std::string& name,
                        const Vector3 position,
                        const std::optional<Vector3> rotation,
                        sol::this_state state) -> sol::object {
                    auto instance =
                        InstantiateFlatpackByName(destination, name, position, rotation);
                    if (!instance) return sol::lua_nil;

                    for (const auto entity : instance.entities)
                    {
                        const auto* obstacle = destination.try_get<NavigationObstacle>(entity);
                        const auto* collideable = destination.try_get<Collideable>(entity);
                        if (obstacle && obstacle->active && collideable &&
                            !destination.any_of<MoveableActor>(entity))
                        {
                            navigationGridSystem->MarkSquareAreaOccupied(
                                collideable->worldBoundingBox, true, entity);
                        }
                    }

                    return sol::make_object(
                        state, static_cast<std::uint32_t>(instance.root));
                };

                api.set_function(
                    "SpawnFlatpack",
                    sol::overload(
                        [spawnFlatpack](
                            const std::string& name,
                            const Vector3 position,
                            sol::this_state state) {
                            return spawnFlatpack(name, position, std::nullopt, state);
                        },
                        [spawnFlatpack](
                            const std::string& name,
                            const Vector3 position,
                            const Vector3 rotation,
                            sol::this_state state) {
                            return spawnFlatpack(name, position, rotation, state);
                        }));
            });
    }

    EngineSystems::~EngineSystems()
    {
        // Windows unsubscribe from UserInput events during teardown, so destroy
        // the UI before UserInput's events are destroyed by member cleanup.
        uiEngine.reset();
    }

    GameUIEngine& EngineSystems::UI()
    {
        assert(uiEngine);
        return *uiEngine;
    }

    const GameUIEngine& EngineSystems::UI() const
    {
        assert(uiEngine);
        return *uiEngine;
    }

    void EngineSystems::ReplaceUiEngine(std::unique_ptr<GameUIEngine> replacement)
    {
        assert(replacement);
        uiEngine = std::move(replacement);
    }
} // namespace sage

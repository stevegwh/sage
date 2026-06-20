//
// Created by Steve Wheeler on 11/06/2026.
//

#include "ScriptSystem.hpp"

#include "Archetypes.hpp"
#include "ActorMovementSystem.hpp"
#include "CollisionSystem.hpp"
#include "EngineSystems.hpp"
#include "SceneTags.hpp"
#include "components/Animation.hpp"
#include "components/Collideable.hpp"
#include "components/MoveableActor.hpp"
#include "components/ScriptComponent.hpp"
#include "components/sgTransform.hpp"

#include "raylib.h"
#include "raymath.h"

#define SOL_ALL_SAFETIES_ON 1
#include "sol/sol.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sage
{
    namespace
    {
        enum class ScriptEventSource
        {
            Collision,
            Movement,
            Animation,
        };

        struct ScriptEventSubscription
        {
            Subscription subscription;
            sol::protected_function callback;
            ScriptEventSource source = ScriptEventSource::Collision;
            entt::entity sourceEntity = entt::null;
        };

        struct ScriptInstance
        {
            std::string loadedPath;
            sol::environment env;
            sol::protected_function awake;
            sol::protected_function start;
            sol::protected_function update;
            sol::protected_function onEnable;
            sol::protected_function onDisable;
            sol::protected_function onTriggerEnter;
            sol::protected_function onTriggerUpdate;
            sol::protected_function onTriggerLeave;
            sol::protected_function onMovementStarted;
            sol::protected_function onDestinationReached;
            sol::protected_function onDestinationUnreachable;
            sol::protected_function onMovementCancelled;
            sol::protected_function onPathChanged;
            Subscription movementStartedSub;
            Subscription destinationReachedSub;
            Subscription destinationUnreachableSub;
            Subscription movementCancelledSub;
            Subscription pathChangedSub;
            std::unordered_map<std::uint32_t, ScriptEventSubscription> eventSubscriptions;
            std::uint32_t nextEventSubscriptionId = 0;
            bool started = false;
            bool wasEnabled = false;
            // Set on load or runtime error (after logging once); the instance is halted
            // until the script path is reassigned.
            bool failed = false;
        };
    } // namespace

    struct ScriptSystem::Impl
    {
        sol::state lua;
        std::unordered_map<entt::entity, ScriptInstance> instances;
        entt::registry* registry;
        EngineSystems* sys;
        Subscription triggerEnterSub;
        Subscription triggerStaySub;
        Subscription triggerExitSub;

        Impl(entt::registry* _registry, EngineSystems* _sys) : registry(_registry), sys(_sys)
        {
            lua.open_libraries(
                sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os);
            bindCoreTypes();

            triggerEnterSub = sys->collisionSystem->onTriggerEnter.Subscribe(
                [this](const entt::entity trigger, const entt::entity other) {
                    callTriggerCallback(trigger, other, &ScriptInstance::onTriggerEnter);
                });
            triggerStaySub = sys->collisionSystem->onTriggerStay.Subscribe(
                [this](const entt::entity trigger, const entt::entity other) {
                    callTriggerCallback(trigger, other, &ScriptInstance::onTriggerUpdate);
                });
            triggerExitSub = sys->collisionSystem->onTriggerExit.Subscribe(
                [this](const entt::entity trigger, const entt::entity other) {
                    callTriggerCallback(trigger, other, &ScriptInstance::onTriggerLeave);
                });
        }

        ~Impl()
        {
            triggerEnterSub.UnSubscribe();
            triggerStaySub.UnSubscribe();
            triggerExitSub.UnSubscribe();
            unbindAllCallbacks();
        }

        // Routes a CollisionSystem trigger event to the trigger entity's script, passing
        // the overlapping entity's id. Instances are created lazily in Update(), so events
        // fired before the script's first tick (or while it is disabled) are dropped.
        void callTriggerCallback(
            const entt::entity trigger, const entt::entity other, sol::protected_function ScriptInstance::*fn)
        {
            const auto it = instances.find(trigger);
            if (it == instances.end() || !it->second.wasEnabled) return;
            call(it->second, it->second.*fn, static_cast<std::uint32_t>(other));
        }

        void callMovementCallback(
            const entt::entity entity, sol::protected_function ScriptInstance::*fn)
        {
            const auto it = instances.find(entity);
            if (it == instances.end() || !it->second.wasEnabled) return;
            call(it->second, it->second.*fn);
        }

        void callDestinationUnreachable(const entt::entity entity, const Vector3 destination)
        {
            const auto it = instances.find(entity);
            if (it == instances.end() || !it->second.wasEnabled) return;
            call(it->second, it->second.onDestinationUnreachable, destination);
        }

        static void unbindMovementCallbacks(ScriptInstance& instance)
        {
            instance.movementStartedSub.UnSubscribe();
            instance.destinationReachedSub.UnSubscribe();
            instance.destinationUnreachableSub.UnSubscribe();
            instance.movementCancelledSub.UnSubscribe();
            instance.pathChangedSub.UnSubscribe();
        }

        void bindMovementCallbacks(const entt::entity entity)
        {
            const auto instanceIt = instances.find(entity);
            auto* moveable = registry->try_get<MoveableActor>(entity);
            if (instanceIt == instances.end() || moveable == nullptr) return;

            auto& instance = instanceIt->second;
            unbindMovementCallbacks(instance);
            instance.movementStartedSub = moveable->onStartMovement.Subscribe(
                [this](const entt::entity actor) {
                    callMovementCallback(actor, &ScriptInstance::onMovementStarted);
                });
            instance.destinationReachedSub = moveable->onDestinationReached.Subscribe(
                [this](const entt::entity actor) {
                    callMovementCallback(actor, &ScriptInstance::onDestinationReached);
                });
            instance.destinationUnreachableSub = moveable->onDestinationUnreachable.Subscribe(
                [this](const entt::entity actor, const Vector3 destination) {
                    callDestinationUnreachable(actor, destination);
                });
            instance.movementCancelledSub = moveable->onMovementCancel.Subscribe(
                [this](const entt::entity actor) {
                    callMovementCallback(actor, &ScriptInstance::onMovementCancelled);
                });
            instance.pathChangedSub = moveable->onPathChanged.Subscribe(
                [this](const entt::entity actor) {
                    callMovementCallback(actor, &ScriptInstance::onPathChanged);
                });
        }

        void unbindMovementCallbacks(const entt::entity entity)
        {
            const auto it = instances.find(entity);
            if (it != instances.end()) unbindMovementCallbacks(it->second);
        }

        static bool unsubscribeEvent(ScriptInstance& instance, const std::uint32_t id)
        {
            const auto it = instance.eventSubscriptions.find(id);
            if (it == instance.eventSubscriptions.end()) return false;
            it->second.subscription.UnSubscribe();
            instance.eventSubscriptions.erase(it);
            return true;
        }

        static void unbindEventCallbacks(ScriptInstance& instance)
        {
            for (auto& [id, event] : instance.eventSubscriptions)
            {
                event.subscription.UnSubscribe();
            }
            instance.eventSubscriptions.clear();
        }

        static void unbindEventCallbacks(
            ScriptInstance& instance, const ScriptEventSource source, const entt::entity sourceEntity)
        {
            for (auto it = instance.eventSubscriptions.begin(); it != instance.eventSubscriptions.end();)
            {
                if (it->second.source != source || it->second.sourceEntity != sourceEntity)
                {
                    ++it;
                    continue;
                }
                it->second.subscription.UnSubscribe();
                it = instance.eventSubscriptions.erase(it);
            }
        }

        void unbindEventCallbacksFromSource(
            const entt::entity sourceEntity, const ScriptEventSource source)
        {
            for (auto& [subscriber, instance] : instances)
            {
                unbindEventCallbacks(instance, source, sourceEntity);
            }
        }

        template <typename... Args>
        void callEventCallback(const entt::entity entity, const std::uint32_t id, Args&&... args)
        {
            const auto instanceIt = instances.find(entity);
            if (instanceIt == instances.end() || !instanceIt->second.wasEnabled) return;

            auto& instance = instanceIt->second;
            const auto eventIt = instance.eventSubscriptions.find(id);
            if (eventIt == instance.eventSubscriptions.end()) return;

            // The callback may unsubscribe itself, so retain a Lua reference for the
            // duration of this invocation instead of calling through the map entry.
            const sol::protected_function callback = eventIt->second.callback;
            call(instance, callback, std::forward<Args>(args)...);
        }

        std::uint32_t addEventSubscription(
            ScriptInstance& instance,
            sol::protected_function callback,
            const ScriptEventSource source,
            const entt::entity sourceEntity)
        {
            const std::uint32_t id = ++instance.nextEventSubscriptionId;
            auto& event = instance.eventSubscriptions[id];
            event.callback = std::move(callback);
            event.source = source;
            event.sourceEntity = sourceEntity;
            return id;
        }

        std::optional<std::uint32_t> subscribeToEvent(
            const entt::entity subscriber,
            const entt::entity sourceEntity,
            const std::string& name,
            sol::protected_function callback)
        {
            const auto instanceIt = instances.find(subscriber);
            if (instanceIt == instances.end() || !registry->valid(sourceEntity) || !callback.valid())
                return std::nullopt;
            auto& instance = instanceIt->second;

            if (name == "TriggerEnter" || name == "TriggerStay" || name == "TriggerExit")
            {
                const auto id = addEventSubscription(
                    instance, std::move(callback), ScriptEventSource::Collision, sourceEntity);
                auto handler = [this, subscriber, sourceEntity, id](
                                   const entt::entity trigger, const entt::entity other) {
                    if (trigger != sourceEntity) return;
                    callEventCallback(
                        subscriber,
                        id,
                        static_cast<std::uint32_t>(trigger),
                        static_cast<std::uint32_t>(other));
                };

                auto& event = instance.eventSubscriptions.at(id);
                if (name == "TriggerEnter")
                    event.subscription = sys->collisionSystem->onTriggerEnter.Subscribe(std::move(handler));
                else if (name == "TriggerStay")
                    event.subscription = sys->collisionSystem->onTriggerStay.Subscribe(std::move(handler));
                else
                    event.subscription = sys->collisionSystem->onTriggerExit.Subscribe(std::move(handler));
                return id;
            }

            if (name == "MovementStarted" || name == "DestinationReached" ||
                name == "DestinationUnreachable" || name == "MovementCancelled" || name == "PathChanged")
            {
                auto* moveable = registry->try_get<MoveableActor>(sourceEntity);
                if (moveable == nullptr) return std::nullopt;
                const auto id = addEventSubscription(
                    instance, std::move(callback), ScriptEventSource::Movement, sourceEntity);
                auto& event = instance.eventSubscriptions.at(id);

                if (name == "DestinationUnreachable")
                {
                    event.subscription = moveable->onDestinationUnreachable.Subscribe(
                        [this, subscriber, id](const entt::entity actor, const Vector3 destination) {
                            callEventCallback(
                                subscriber, id, static_cast<std::uint32_t>(actor), destination);
                        });
                }
                else
                {
                    auto handler = [this, subscriber, id](const entt::entity actor) {
                        callEventCallback(subscriber, id, static_cast<std::uint32_t>(actor));
                    };
                    if (name == "MovementStarted")
                        event.subscription = moveable->onStartMovement.Subscribe(std::move(handler));
                    else if (name == "DestinationReached")
                        event.subscription = moveable->onDestinationReached.Subscribe(std::move(handler));
                    else if (name == "MovementCancelled")
                        event.subscription = moveable->onMovementCancel.Subscribe(std::move(handler));
                    else
                        event.subscription = moveable->onPathChanged.Subscribe(std::move(handler));
                }
                return id;
            }

            if (name == "AnimationStarted" || name == "AnimationEnded" || name == "AnimationUpdated")
            {
                auto* animation = registry->try_get<Animation>(sourceEntity);
                if (animation == nullptr) return std::nullopt;
                const auto id = addEventSubscription(
                    instance, std::move(callback), ScriptEventSource::Animation, sourceEntity);
                auto handler = [this, subscriber, id](const entt::entity actor) {
                    callEventCallback(subscriber, id, static_cast<std::uint32_t>(actor));
                };
                auto& event = instance.eventSubscriptions.at(id);
                if (name == "AnimationStarted")
                    event.subscription = animation->onAnimationStart.Subscribe(std::move(handler));
                else if (name == "AnimationEnded")
                    event.subscription = animation->onAnimationEnd.Subscribe(std::move(handler));
                else
                    event.subscription = animation->onAnimationUpdated.Subscribe(std::move(handler));
                return id;
            }

            return std::nullopt;
        }

        // Usertypes and free functions shared by every script.
        void bindCoreTypes()
        {
            lua.new_usertype<Vector3>(
                "Vec3",
                sol::call_constructor,
                sol::factories(
                    []() { return Vector3{}; },
                    [](const float x, const float y, const float z) { return Vector3{x, y, z}; }),
                "x",
                &Vector3::x,
                "y",
                &Vector3::y,
                "z",
                &Vector3::z,
                sol::meta_function::addition,
                [](const Vector3& a, const Vector3& b) { return Vector3Add(a, b); },
                sol::meta_function::subtraction,
                [](const Vector3& a, const Vector3& b) { return Vector3Subtract(a, b); },
                sol::meta_function::multiplication,
                [](const Vector3& a, const float s) { return Vector3Scale(a, s); },
                sol::meta_function::to_string,
                [](const Vector3& v) {
                    return std::string(TextFormat("(%.3f, %.3f, %.3f)", v.x, v.y, v.z));
                });

            // Setters route through the position/rotation/scale proxy fields so
            // TransformSystem's dirty propagation runs automatically.
            lua.new_usertype<sgTransform>(
                "Transform",
                sol::no_constructor,
                "name",
                &sgTransform::name,
                "GetPosition",
                [](const sgTransform& t) -> Vector3 { return t.GetWorldPos(); },
                "SetPosition",
                [](sgTransform& t, const Vector3& v) { t.position.world = v; },
                "GetLocalPosition",
                [](const sgTransform& t) -> Vector3 { return t.GetLocalPos(); },
                "SetLocalPosition",
                [](sgTransform& t, const Vector3& v) { t.position.local = v; },
                "GetRotation",
                [](const sgTransform& t) -> Vector3 { return t.GetWorldRot(); },
                "SetRotation",
                [](sgTransform& t, const Vector3& v) { t.rotation.world = v; },
                "GetScale",
                [](const sgTransform& t) -> Vector3 { return t.GetScale(); },
                "SetScale",
                [](sgTransform& t, const Vector3& v) { t.scale.world = v; },
                "Forward",
                [](const sgTransform& t) { return t.forward(); },
                // Returns the parent entity id (usable with GetTransform(id) etc.),
                // or nil when this transform is a root. Lets a child collider's
                // OnTrigger forward to its logical owner (the parent).
                "GetParent",
                [](const sgTransform& t, sol::this_state s) -> sol::object {
                    const auto parent = t.GetParent();
                    if (parent == entt::null) return sol::lua_nil;
                    return sol::make_object(s, static_cast<std::uint32_t>(parent));
                });

            lua.new_usertype<Collideable>(
                "Collideable",
                sol::no_constructor,
                "active",
                &Collideable::active,
                "debugDraw",
                &Collideable::debugDraw);

            lua.new_usertype<Archetype>(
                "Archetype",
                sol::no_constructor,
                "id",
                sol::readonly(&Archetype::id),
                "Is",
                [](const Archetype& archetype, const std::string& name) {
                    return archetype == MakeArchetype(name);
                });

            // Clip names are the GLB animation names (Blender NLA tracks). Play/PlayOneShot
            // return false when no clip matches, so scripts can react instead of crashing.
            lua.new_usertype<Animation>(
                "Animation",
                sol::no_constructor,
                "Play",
                sol::overload(
                    [](Animation& a, const std::string& clip) { return a.ChangeAnimationByName(clip); },
                    [](Animation& a, const std::string& clip, const int speed) {
                        return a.ChangeAnimationByName(clip, speed);
                    }),
                "PlayOneShot",
                sol::overload(
                    [](Animation& a, const std::string& clip) { return a.PlayOneShotByName(clip, 1); },
                    [](Animation& a, const std::string& clip, const int speed) {
                        return a.PlayOneShotByName(clip, speed);
                    }),
                "ClipCount",
                [](const Animation& a) { return a.animsCount; },
                "GetClipNames",
                [](const Animation& a) { return sol::as_table(a.clipNames); },
                "SetBlendDuration",
                [](Animation& a, const float seconds) { a.blendDuration = std::max(0.0f, seconds); },
                "GetBlendDuration",
                [](const Animation& a) { return a.blendDuration; });

            lua.set_function(
                "Log", [](const std::string& msg) { TraceLog(LOG_INFO, "Lua: %s", msg.c_str()); });
        }

        // Per-instance API: component accessors default to the owning entity, or take
        // another entity id (as exposed via the `entity` global) for cross-entity access.
        // Getters return nil when the component is missing.
        void bindEntityApi(sol::environment& env, const entt::entity entity)
        {
            env["entity"] = static_cast<std::uint32_t>(entity);
            sol::table api = lua.create_table();
            env["sage"] = api;
            api["Vec3"] = lua["Vec3"];
            api["Log"] = lua["Log"];

            sol::table events = lua.create_table();
            events["TriggerEnter"] = "TriggerEnter";
            events["TriggerStay"] = "TriggerStay";
            events["TriggerExit"] = "TriggerExit";
            events["MovementStarted"] = "MovementStarted";
            events["DestinationReached"] = "DestinationReached";
            events["DestinationUnreachable"] = "DestinationUnreachable";
            events["MovementCancelled"] = "MovementCancelled";
            events["PathChanged"] = "PathChanged";
            events["AnimationStarted"] = "AnimationStarted";
            events["AnimationEnded"] = "AnimationEnded";
            events["AnimationUpdated"] = "AnimationUpdated";
            api["Events"] = events;

            api.set_function(
                "Subscribe",
                [this, entity](
                    const std::uint32_t source,
                    const std::string& eventName,
                    sol::protected_function callback,
                    sol::this_state state) -> sol::object {
                    const auto sourceEntity = static_cast<entt::entity>(source);
                    const auto id = subscribeToEvent(entity, sourceEntity, eventName, std::move(callback));
                    if (!id)
                    {
                        TraceLog(
                            LOG_WARNING,
                            "Lua: entity %u cannot subscribe to event '%s' on entity %u",
                            static_cast<std::uint32_t>(entity),
                            eventName.c_str(),
                            source);
                        return sol::lua_nil;
                    }
                    return sol::make_object(state, *id);
                });

            api.set_function("Unsubscribe", [this, entity](const std::uint32_t id) {
                const auto it = instances.find(entity);
                return it != instances.end() && unsubscribeEvent(it->second, id);
            });

            api.set_function(
                "FindFirstWithArchetype",
                [this](const std::string& name, sol::this_state state) -> sol::object {
                    const auto found = sage::FindFirstWithArchetype(*registry, MakeArchetype(name));
                    if (!found) return sol::lua_nil;
                    return sol::make_object(state, static_cast<std::uint32_t>(*found));
                });

            api.set_function(
                "MoveToLocation",
                [this, entity](const Vector3& destination) {
                    if (!registry->all_of<sgTransform>(entity)) return false;
                    sys->actorMovementSystem->MoveToLocation(entity, destination);
                    return true;
                });

            api.set_function(
                "TryPathfindToLocation",
                sol::overload(
                    [this, entity](const Vector3& destination) {
                        if (!registry->all_of<sgTransform, MoveableActor, Collideable>(entity)) return false;
                        return sys->actorMovementSystem->TryPathfindToLocation(entity, destination);
                    },
                    [this, entity](const Vector3& destination, const bool astar) {
                        if (!registry->all_of<sgTransform, MoveableActor, Collideable>(entity)) return false;
                        return sys->actorMovementSystem->TryPathfindToLocation(entity, destination, astar);
                    }));

            api.set_function(
                "FindFirstWithTag",
                [this](const std::string& tag, sol::this_state state) -> sol::object {
                    const auto found = sage::FindFirstWithTag(*registry, tag);
                    if (found == entt::null) return sol::lua_nil;
                    return sol::make_object(state, static_cast<std::uint32_t>(found));
                });

            api.set_function(
                "HasTag",
                sol::overload(
                    [this, entity](const std::string& tag) { return sage::HasTag(*registry, entity, tag); },
                    [this](const std::uint32_t e, const std::string& tag) {
                        const auto target = static_cast<entt::entity>(e);
                        return registry->valid(target) && sage::HasTag(*registry, target, tag);
                    }));

            api.set_function(
                "GetTags",
                sol::overload(
                    [this, entity](sol::this_state state) {
                        sol::state_view luaState(state);
                        sol::table result = luaState.create_table();
                        if (const auto* meta = registry->try_get<MetaData>(entity))
                        {
                            ForEachSceneTag(SceneTagText(meta->tags), [&](const std::string_view tag) {
                                result.add(std::string{tag});
                            });
                        }
                        return result;
                    },
                    [this](const std::uint32_t e, sol::this_state state) {
                        sol::state_view luaState(state);
                        sol::table result = luaState.create_table();
                        const auto target = static_cast<entt::entity>(e);
                        if (registry->valid(target))
                        {
                            if (const auto* meta = registry->try_get<MetaData>(target))
                            {
                                ForEachSceneTag(SceneTagText(meta->tags), [&](const std::string_view tag) {
                                    result.add(std::string{tag});
                                });
                            }
                        }
                        return result;
                    }));

            api.set_function(
                "GetArchetype",
                sol::overload(
                    [this, entity]() { return registry->try_get<Archetype>(entity); },
                    [this](const std::uint32_t e) {
                        const auto target = static_cast<entt::entity>(e);
                        return registry->valid(target) ? registry->try_get<Archetype>(target) : nullptr;
                    }));

            api.set_function(
                "GetTransform",
                sol::overload(
                    [this, entity]() { return registry->try_get<sgTransform>(entity); },
                    [this](const std::uint32_t e) {
                        return registry->try_get<sgTransform>(static_cast<entt::entity>(e));
                    }));

            api.set_function(
                "GetCollideable",
                sol::overload(
                    [this, entity]() { return registry->try_get<Collideable>(entity); },
                    [this](const std::uint32_t e) {
                        return registry->try_get<Collideable>(static_cast<entt::entity>(e));
                    }));

            api.set_function(
                "GetAnimation",
                sol::overload(
                    [this, entity]() { return registry->try_get<Animation>(entity); },
                    [this](const std::uint32_t e) {
                        return registry->try_get<Animation>(static_cast<entt::entity>(e));
                    }));

            // Temporary compatibility aliases for scripts authored before the API
            // moved under `sage`. LuaLS advertises only the namespaced surface.
            for (const char* name : {
                     "FindFirstWithArchetype",
                     "MoveToLocation",
                     "TryPathfindToLocation",
                     "FindFirstWithTag",
                     "HasTag",
                     "GetTags",
                     "GetArchetype",
                     "GetTransform",
                     "GetCollideable",
                     "GetAnimation"})
            {
                env[name] = api[name];
            }
        }

        template <typename... Args>
        void call(ScriptInstance& instance, const sol::protected_function& fn, Args&&... args)
        {
            if (instance.failed || !fn.valid()) return;
            if (const auto result = fn(std::forward<Args>(args)...); !result.valid())
            {
                const sol::error err = result;
                TraceLog(LOG_ERROR, "Lua (%s): %s", instance.loadedPath.c_str(), err.what());
                instance.failed = true;
            }
        }

        ScriptInstance& createInstance(const entt::entity entity, const ScriptComponent& script)
        {
            auto& instance = instances[entity];
            instance.loadedPath = script.scriptPath;
            instance.env = sol::environment(lua, sol::create, lua.globals());
            bindEntityApi(instance.env, entity);

            if (const auto result =
                    lua.safe_script_file(script.scriptPath, instance.env, sol::script_pass_on_error);
                !result.valid())
            {
                const sol::error err = result;
                TraceLog(LOG_ERROR, "Lua: failed to load '%s': %s", script.scriptPath.c_str(), err.what());
                instance.failed = true;
                return instance;
            }

            instance.awake = instance.env["Awake"];
            instance.start = instance.env["Start"];
            instance.update = instance.env["Update"];
            instance.onEnable = instance.env["OnEnable"];
            instance.onDisable = instance.env["OnDisable"];
            instance.onTriggerEnter = instance.env["OnTriggerEnter"];
            instance.onTriggerUpdate = instance.env["OnTriggerUpdate"];
            instance.onTriggerLeave = instance.env["OnTriggerLeave"];
            instance.onMovementStarted = instance.env["OnMovementStarted"];
            instance.onDestinationReached = instance.env["OnDestinationReached"];
            instance.onDestinationUnreachable = instance.env["OnDestinationUnreachable"];
            instance.onMovementCancelled = instance.env["OnMovementCancelled"];
            instance.onPathChanged = instance.env["OnPathChanged"];

            bindMovementCallbacks(entity);

            call(instance, instance.awake);
            return instance;
        }

        void destroyInstance(const entt::entity entity)
        {
            const auto it = instances.find(entity);
            if (it == instances.end()) return;
            if (it->second.wasEnabled) call(it->second, it->second.onDisable);
            unbindMovementCallbacks(it->second);
            unbindEventCallbacks(it->second);
            instances.erase(it);
        }

        void unbindAllCallbacks()
        {
            for (auto& [entity, instance] : instances)
            {
                unbindMovementCallbacks(instance);
                unbindEventCallbacks(instance);
            }
        }
    };

    void ScriptSystem::Update()
    {
        const float dt = GetFrameTime();
        for (const auto view = registry->view<ScriptComponent>(); const auto entity : view)
        {
            auto& script = view.get<ScriptComponent>(entity);

            auto it = impl->instances.find(entity);
            // Hot-swap: the path was edited (e.g. in the inspector) — tear down and reload.
            if (it != impl->instances.end() && it->second.loadedPath != script.scriptPath)
            {
                impl->destroyInstance(entity);
                it = impl->instances.end();
            }
            if (it == impl->instances.end())
            {
                if (script.scriptPath.empty()) continue;
                impl->createInstance(entity, script);
                it = impl->instances.find(entity);
            }

            auto& instance = it->second;
            if (instance.failed) continue;

            if (script.enabled && !instance.wasEnabled)
            {
                instance.wasEnabled = true;
                impl->call(instance, instance.onEnable);
            }
            else if (!script.enabled && instance.wasEnabled)
            {
                instance.wasEnabled = false;
                impl->call(instance, instance.onDisable);
            }
            if (!script.enabled) continue;

            if (!instance.started)
            {
                instance.started = true;
                impl->call(instance, instance.start);
            }
            impl->call(instance, instance.update, dt);
        }
    }

    void ScriptSystem::onScriptComponentDestroyed(entt::registry& /*reg*/, const entt::entity entity)
    {
        impl->destroyInstance(entity);
    }

    void ScriptSystem::onMoveableActorConstructed(entt::registry& /*reg*/, const entt::entity entity)
    {
        impl->bindMovementCallbacks(entity);
    }

    void ScriptSystem::onMoveableActorDestroyed(entt::registry& /*reg*/, const entt::entity entity)
    {
        impl->unbindMovementCallbacks(entity);
        impl->unbindEventCallbacksFromSource(entity, ScriptEventSource::Movement);
    }

    void ScriptSystem::onAnimationDestroyed(entt::registry& /*reg*/, const entt::entity entity)
    {
        impl->unbindEventCallbacksFromSource(entity, ScriptEventSource::Animation);
    }

    void ScriptSystem::onCollideableDestroyed(entt::registry& /*reg*/, const entt::entity entity)
    {
        impl->unbindEventCallbacksFromSource(entity, ScriptEventSource::Collision);
    }

    ScriptSystem::ScriptSystem(entt::registry* _registry, EngineSystems* _sys)
        : impl(std::make_unique<Impl>(_registry, _sys)), registry(_registry)
    {
        registry->on_destroy<ScriptComponent>().connect<&ScriptSystem::onScriptComponentDestroyed>(this);
        registry->on_construct<MoveableActor>().connect<&ScriptSystem::onMoveableActorConstructed>(this);
        registry->on_destroy<MoveableActor>().connect<&ScriptSystem::onMoveableActorDestroyed>(this);
        registry->on_destroy<Animation>().connect<&ScriptSystem::onAnimationDestroyed>(this);
        registry->on_destroy<Collideable>().connect<&ScriptSystem::onCollideableDestroyed>(this);
    }

    ScriptSystem::~ScriptSystem()
    {
        impl->unbindAllCallbacks();
        registry->on_destroy<ScriptComponent>().disconnect<&ScriptSystem::onScriptComponentDestroyed>(this);
        registry->on_construct<MoveableActor>().disconnect<&ScriptSystem::onMoveableActorConstructed>(this);
        registry->on_destroy<MoveableActor>().disconnect<&ScriptSystem::onMoveableActorDestroyed>(this);
        registry->on_destroy<Animation>().disconnect<&ScriptSystem::onAnimationDestroyed>(this);
        registry->on_destroy<Collideable>().disconnect<&ScriptSystem::onCollideableDestroyed>(this);
    }
} // namespace sage

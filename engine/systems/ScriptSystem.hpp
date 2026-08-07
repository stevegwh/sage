//
// Created by Steve Wheeler on 11/06/2026.
//

#pragma once

#include "engine/Event.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#ifndef SOL_ALL_SAFETIES_ON
#define SOL_ALL_SAFETIES_ON 1
#endif
#include "sol/forward.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sage
{
    class LuaEventCallback
    {
        std::function<void()> withoutArguments;
        std::function<void(std::uint32_t)> withEntity;
        std::function<void(Vector3)> withVector;

      public:
        LuaEventCallback(
            std::function<void()> noArgs,
            std::function<void(std::uint32_t)> entityArg,
            std::function<void(Vector3)> vectorArg)
            : withoutArguments(std::move(noArgs)),
              withEntity(std::move(entityArg)),
              withVector(std::move(vectorArg))
        {
        }

        void operator()() const
        {
            withoutArguments();
        }
        void operator()(const std::uint32_t entity) const
        {
            withEntity(entity);
        }
        void operator()(const Vector3 value) const
        {
            withVector(value);
        }
    };

    // Runs Lua scripts attached to entities via ScriptComponent.
    //
    // Lifecycle (Unity-style), driven from Update():
    //   Awake()      — once, when the script instance is first created, even if disabled.
    //   OnEnable()   — whenever enabled transitions to true (including the first frame).
    //   Start()      — once, before the first Update() while enabled.
    //   Update(dt)   — every frame while enabled.
    //   OnDisable()  — whenever enabled transitions to false, and on destruction.
    //
    // Events are subscribed to explicitly via the per-script `sage` API, never as magic
    // globals. Each registration takes a source entity and a callback, and returns a
    // subscription id for sage.Unsubscribe; the callback receives only the event's own
    // arguments (the source entity is the one you passed in) and fires only while the
    // script is enabled. Subscriptions are removed when the script is destroyed/reloaded
    // or the source's component-owned event is destroyed.
    //   sage.OnTriggerEnter(e, fn(other))    — an entity started overlapping e's trigger.
    //   sage.OnTriggerStay(e, fn(other))     — every frame an entity remains inside.
    //   sage.OnTriggerExit(e, fn(other))     — an entity stopped overlapping (or destroyed).
    //   sage.OnMovementStarted(e, fn())
    //   sage.OnDestinationReached(e, fn())
    //   sage.OnDestinationUnreachable(e, fn(destination))
    //   sage.OnMovementCancelled(e, fn())
    //   sage.OnPathChanged(e, fn())
    //   sage.OnAnimationStarted/Ended/Updated(e, fn())
    //
    // Lifecycle callbacks (Awake/OnEnable/Start/Update/OnDisable) are the only magic
    // globals. Each script gets its own environment (globals don't leak between scripts)
    // with a per-script `sage` API table (e.g. sage.GetTransform(sage.GetEntity())). A
    // script error logs once and halts that instance. Changing ScriptComponent::scriptPath
    // hot-swaps the script (the old instance is disabled and a fresh one Awakes).
    //
    // The full sol2/Lua implementation stays private to the .cpp (Impl). This
    // header uses only sol's lightweight forward declarations for API extensions.
    class ScriptSystem
    {
      public:
        // Adds functions to a namespaced table in every script environment.
        // The callback is invoked once per script instance and may capture the
        // owning game's systems. Keeping registration here preserves the
        // engine -> game dependency direction.
        using ApiExtension = std::function<void(sol::table&, entt::entity, entt::registry&)>;
        using EventSubscriber = std::function<Subscription(entt::entity source, const LuaEventCallback& callback)>;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        entt::registry* registry;
        std::vector<entt::scoped_connection> eventSourceCleanupConnections;
        std::unordered_set<entt::id_type> registeredEventSourceTypes;

        void onScriptComponentDestroyed(entt::registry& reg, entt::entity entity);
        void registerEventLuaBinding(std::string eventName, entt::id_type sourceType, EventSubscriber subscribe);
        void removeEventSubscriptions(entt::entity source, entt::id_type sourceType);

        template <typename Source>
        void onEventSourceDestroyed(entt::registry&, const entt::entity entity)
        {
            removeEventSubscriptions(entity, entt::type_hash<Source>::value());
        }

      public:
        void Update();
        [[nodiscard]] sol::state& GetLuaState();
        void RegisterApiExtension(std::string namespaceName, ApiExtension extension);

        // Registers a no-constructor usertype from T::define_lua_api(binder) and
        // installs its nullable component accessor into each script's sage table.
        // Include systems/LuaBinding.hpp at the call site for the implementation.
        template <class T>
        void RegisterComponent(std::string typeName, std::string accessorName);

        // Registers a non-component value type from the same declaration, without
        // creating an ECS accessor.
        template <class T>
        void RegisterType(std::string typeName);

        // Exposes every named enumerator as a read-only Lua table using its C++
        // spelling, e.g. EmoteType::Happy becomes EmoteType.Happy.
        template <class E>
        void RegisterEnum(std::string typeName);

        template <typename Source>
        void RegisterEventLuaBinding(std::string eventName, EventSubscriber subscribe)
        {
            const auto sourceType = entt::type_hash<Source>::value();
            if (registeredEventSourceTypes.emplace(sourceType).second)
            {
                eventSourceCleanupConnections.emplace_back(
                    registry->on_destroy<Source>().template connect<&ScriptSystem::onEventSourceDestroyed<Source>>(
                        *this));
            }
            registerEventLuaBinding(std::move(eventName), sourceType, std::move(subscribe));
        }

        explicit ScriptSystem(entt::registry* _registry);
        ~ScriptSystem();
    };
} // namespace sage

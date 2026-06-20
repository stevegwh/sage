//
// Created by Steve Wheeler on 11/06/2026.
//

#pragma once

#include "entt/entt.hpp"

#include <memory>

namespace sage
{
    class EngineSystems;

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
    // sol2/Lua stay private to the .cpp (Impl) so the 29k-line sol header is compiled
    // exactly once.
    class ScriptSystem
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        entt::registry* registry;

        void onScriptComponentDestroyed(entt::registry& reg, entt::entity entity);
        void onMoveableActorDestroyed(entt::registry& reg, entt::entity entity);
        void onAnimationDestroyed(entt::registry& reg, entt::entity entity);
        void onCollideableDestroyed(entt::registry& reg, entt::entity entity);

      public:
        void Update();
        ScriptSystem(entt::registry* _registry, EngineSystems* _sys);
        ~ScriptSystem();
    };
} // namespace sage

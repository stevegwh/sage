//
// Created by Steve Wheeler on 11/06/2026.
//

#pragma once

#include "entt/entt.hpp"

#include <memory>

namespace sage
{
    // Runs Lua scripts attached to entities via ScriptComponent.
    //
    // Lifecycle (Unity-style), driven from Update():
    //   Awake()      — once, when the script instance is first created, even if disabled.
    //   OnEnable()   — whenever enabled transitions to true (including the first frame).
    //   Start()      — once, before the first Update() while enabled.
    //   Update(dt)   — every frame while enabled.
    //   OnDisable()  — whenever enabled transitions to false, and on destruction.
    //
    // All callbacks are optional globals in the script file. Each script gets its own
    // environment (globals don't leak between scripts) with `entity` plus accessors
    // like GetTransform()/GetCollideable() bound in. A script error logs once and
    // halts that instance. Changing ScriptComponent::scriptPath hot-swaps the script
    // (the old instance is disabled and a fresh one Awakes).
    //
    // sol2/Lua stay private to the .cpp (Impl) so the 29k-line sol header is compiled
    // exactly once.
    class ScriptSystem
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        entt::registry* registry;

        void onScriptComponentDestroyed(entt::registry& reg, entt::entity entity);

      public:
        void Update();
        explicit ScriptSystem(entt::registry* _registry);
        ~ScriptSystem();
    };
} // namespace sage

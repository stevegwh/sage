//
// Created by Steve Wheeler on 11/06/2026.
//

#include "ScriptSystem.hpp"

#include "components/Animation.hpp"
#include "components/Collideable.hpp"
#include "components/ScriptComponent.hpp"
#include "components/sgTransform.hpp"

#include "raylib.h"
#include "raymath.h"

#define SOL_ALL_SAFETIES_ON 1
#include "sol/sol.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sage
{
    namespace
    {
        struct ScriptInstance
        {
            std::string loadedPath;
            sol::environment env;
            sol::protected_function awake;
            sol::protected_function start;
            sol::protected_function update;
            sol::protected_function onEnable;
            sol::protected_function onDisable;
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

        explicit Impl(entt::registry* _registry) : registry(_registry)
        {
            lua.open_libraries(
                sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os);
            bindCoreTypes();
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
                [](const sgTransform& t) { return t.forward(); });

            lua.new_usertype<Collideable>(
                "Collideable",
                sol::no_constructor,
                "active",
                &Collideable::active,
                "isTrigger",
                &Collideable::isTrigger,
                "debugDraw",
                &Collideable::debugDraw,
                "blocksNavigation",
                &Collideable::blocksNavigation);

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
                [](const Animation& a) { return sol::as_table(a.clipNames); });

            lua.set_function(
                "Log", [](const std::string& msg) { TraceLog(LOG_INFO, "Lua: %s", msg.c_str()); });
        }

        // Per-instance API: component accessors default to the owning entity, or take
        // another entity id (as exposed via the `entity` global) for cross-entity access.
        // Getters return nil when the component is missing.
        void bindEntityApi(sol::environment& env, const entt::entity entity)
        {
            env["entity"] = static_cast<std::uint32_t>(entity);

            env.set_function(
                "GetTransform",
                sol::overload(
                    [this, entity]() { return registry->try_get<sgTransform>(entity); },
                    [this](const std::uint32_t e) {
                        return registry->try_get<sgTransform>(static_cast<entt::entity>(e));
                    }));

            env.set_function(
                "GetCollideable",
                sol::overload(
                    [this, entity]() { return registry->try_get<Collideable>(entity); },
                    [this](const std::uint32_t e) {
                        return registry->try_get<Collideable>(static_cast<entt::entity>(e));
                    }));

            env.set_function(
                "GetAnimation",
                sol::overload(
                    [this, entity]() { return registry->try_get<Animation>(entity); },
                    [this](const std::uint32_t e) {
                        return registry->try_get<Animation>(static_cast<entt::entity>(e));
                    }));
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

            call(instance, instance.awake);
            return instance;
        }

        void destroyInstance(const entt::entity entity)
        {
            const auto it = instances.find(entity);
            if (it == instances.end()) return;
            if (it->second.wasEnabled) call(it->second, it->second.onDisable);
            instances.erase(it);
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

    ScriptSystem::ScriptSystem(entt::registry* _registry)
        : impl(std::make_unique<Impl>(_registry)), registry(_registry)
    {
        registry->on_destroy<ScriptComponent>().connect<&ScriptSystem::onScriptComponentDestroyed>(this);
    }

    ScriptSystem::~ScriptSystem()
    {
        registry->on_destroy<ScriptComponent>().disconnect<&ScriptSystem::onScriptComponentDestroyed>(this);
    }
} // namespace sage

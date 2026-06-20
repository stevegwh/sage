//
// Created by Steve Wheeler on 11/06/2026.
//

#include "ScriptSystem.hpp"

#include "SceneTags.hpp"
#include "components/ScriptComponent.hpp"

#include "raylib.h"
#include "raymath.h"

#include "sol/sol.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sage
{
    namespace
    {
        struct ScriptEventSubscription
        {
            Subscription subscription;
            sol::protected_function callback;
            entt::id_type sourceType;
            entt::entity sourceEntity;
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
        struct ApiExtensionEntry
        {
            std::string namespaceName;
            ApiExtension bind;
        };

        sol::state lua;
        std::unordered_map<entt::entity, ScriptInstance> instances;
        std::vector<ApiExtensionEntry> apiExtensions;
        entt::registry* registry;

        explicit Impl(entt::registry* _registry) : registry(_registry)
        {
            lua.open_libraries(
                sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os);
            bindCoreTypes();
        }

        ~Impl()
        {
            for (auto& [entity, instance] : instances)
            {
                unbindEventCallbacks(instance);
            }
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
            ScriptInstance& instance, const entt::id_type sourceType, const entt::entity sourceEntity)
        {
            for (auto it = instance.eventSubscriptions.begin(); it != instance.eventSubscriptions.end();)
            {
                if (it->second.sourceType != sourceType || it->second.sourceEntity != sourceEntity)
                {
                    ++it;
                    continue;
                }
                it->second.subscription.UnSubscribe();
                it = instance.eventSubscriptions.erase(it);
            }
        }

        void unbindEventCallbacksFromSource(
            const entt::entity sourceEntity, const entt::id_type sourceType)
        {
            for (auto& [subscriber, instance] : instances)
            {
                unbindEventCallbacks(instance, sourceType, sourceEntity);
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
            const entt::id_type sourceType,
            const entt::entity sourceEntity)
        {
            const std::uint32_t id = ++instance.nextEventSubscriptionId;
            instance.eventSubscriptions.emplace(
                id,
                ScriptEventSubscription{
                    .subscription = {},
                    .callback = std::move(callback),
                    .sourceType = sourceType,
                    .sourceEntity = sourceEntity});
            return id;
        }

        std::optional<std::uint32_t> subscribeToEvent(
            const entt::entity subscriber,
            const entt::entity sourceEntity,
            const entt::id_type sourceType,
            sol::protected_function callback,
            const EventSubscriber& subscribe)
        {
            const auto instanceIt = instances.find(subscriber);
            if (instanceIt == instances.end() || !registry->valid(sourceEntity) || !callback.valid())
                return std::nullopt;
            auto& instance = instanceIt->second;
            const auto id = addEventSubscription(instance, std::move(callback), sourceType, sourceEntity);
            const LuaEventCallback dispatch{
                [this, subscriber, id] { callEventCallback(subscriber, id); },
                [this, subscriber, id](const std::uint32_t entity) { callEventCallback(subscriber, id, entity); },
                [this, subscriber, id](const Vector3 value) { callEventCallback(subscriber, id, value); }};

            auto& event = instance.eventSubscriptions.at(id);
            event.subscription = subscribe(sourceEntity, dispatch);
            if (event.subscription.IsActive()) return id;

            instance.eventSubscriptions.erase(id);
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
                [](const Vector3& v) { return std::string(TextFormat("(%.3f, %.3f, %.3f)", v.x, v.y, v.z)); });

            lua.set_function(
                "Log", [](const std::string& msg) { TraceLog(LOG_INFO, "Lua: %s", msg.c_str()); });
        }

        // Per-instance API exposed as the `sage` table in each script's environment.
        // Component accessors take an explicit entity id (use sage.GetEntity() for the
        // owning entity) and return nil when the component is missing.
        void bindEntityApi(sol::environment& env, const entt::entity entity)
        {
            sol::table api = lua.create_table();
            env["sage"] = api;
            api["Vec3"] = lua["Vec3"];
            api["Log"] = lua["Log"];

            api.set_function("GetEntity", [entity]() { return static_cast<std::uint32_t>(entity); });

            api.set_function("Unsubscribe", [this, entity](const std::uint32_t id) {
                const auto it = instances.find(entity);
                return it != instances.end() && unsubscribeEvent(it->second, id);
            });

            api.set_function(
                "FindFirstWithTag",
                [this](const std::string& tag, sol::this_state state) -> sol::object {
                    const auto found = sage::FindFirstWithTag(*registry, tag);
                    if (found == entt::null) return sol::lua_nil;
                    return sol::make_object(state, static_cast<std::uint32_t>(found));
                });

            api.set_function("HasTag", [this](const std::uint32_t e, const std::string& tag) {
                const auto target = static_cast<entt::entity>(e);
                return registry->valid(target) && sage::HasTag(*registry, target, tag);
            });

            api.set_function("GetTags", [this](const std::uint32_t e, sol::this_state state) {
                sol::state_view luaState(state);
                sol::table result = luaState.create_table();
                const auto target = static_cast<entt::entity>(e);
                if (const auto* meta =
                        registry->valid(target) ? registry->try_get<MetaData>(target) : nullptr)
                {
                    ForEachSceneTag(SceneTagText(meta->tags), [&](const std::string_view tag) {
                        result.add(std::string{tag});
                    });
                }
                return result;
            });

            for (const auto& extension : apiExtensions)
            {
                bindApiExtension(env, entity, extension);
            }
        }

        void bindApiExtension(
            sol::environment& env, const entt::entity entity, const ApiExtensionEntry& extension)
        {
            sol::object existing = env[extension.namespaceName];
            sol::table api;
            if (existing.valid() && existing.get_type() == sol::type::table)
            {
                api = existing.as<sol::table>();
            }
            else
            {
                api = lua.create_table();
                env[extension.namespaceName] = api;
            }
            extension.bind(api, entity, *registry);
        }

        void registerApiExtension(std::string namespaceName, ApiExtension extension)
        {
            apiExtensions.emplace_back(
                ApiExtensionEntry{std::move(namespaceName), std::move(extension)});
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
            unbindEventCallbacks(it->second);
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

    void ScriptSystem::RegisterApiExtension(std::string namespaceName, ApiExtension extension)
    {
        if (namespaceName.empty() || !extension) return;
        impl->registerApiExtension(std::move(namespaceName), std::move(extension));
    }

    sol::state& ScriptSystem::GetLuaState()
    {
        return impl->lua;
    }

    void ScriptSystem::registerEventLuaBinding(
        std::string eventName, const entt::id_type sourceType, EventSubscriber subscribe)
    {
        const std::string functionName = "On" + eventName;
        impl->registerApiExtension(
            "sage",
            [this,
             functionName = std::move(functionName),
             eventName = std::move(eventName),
             sourceType,
             subscribe = std::move(subscribe)](sol::table& api, const entt::entity owner, entt::registry&) {
                api.set_function(
                    functionName,
                    [this, owner, eventName, sourceType, subscribe](
                        const std::uint32_t source,
                        sol::protected_function callback,
                        sol::this_state state) -> sol::object {
                        const auto id = impl->subscribeToEvent(
                            owner, static_cast<entt::entity>(source), sourceType, std::move(callback), subscribe);
                        if (!id)
                        {
                            TraceLog(
                                LOG_WARNING,
                                "Lua: entity %u cannot subscribe to '%s' on entity %u",
                                static_cast<std::uint32_t>(owner),
                                eventName.c_str(),
                                source);
                            return sol::lua_nil;
                        }
                        return sol::make_object(state, *id);
                    });
            });
    }

    void ScriptSystem::removeEventSubscriptions(const entt::entity source, const entt::id_type sourceType)
    {
        impl->unbindEventCallbacksFromSource(source, sourceType);
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

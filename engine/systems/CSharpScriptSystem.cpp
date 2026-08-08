#include "CSharpScriptSystem.hpp"

#include "engine/Archetypes.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Flatpack.hpp"
#include "engine/scripting/EngineScriptApi.hpp"
#include "engine/scripting/ScriptApi.hpp"
#include "engine/systems/ActorMovementSystem.hpp"
#include "engine/systems/CollisionSystem.hpp"
#include "engine/systems/NavigationGridSystem.hpp"

#include "raylib.h"

#include "coreclr_delegates.h"
#include "hostfxr.h"
#include "nethost.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#define SAGE_HOST_TEXT(value) L##value
#define SAGE_MANAGED_CALL __cdecl
#else
#include <dlfcn.h>
#define SAGE_HOST_TEXT(value) value
#define SAGE_MANAGED_CALL
#endif

#ifndef SAGE_MANAGED_RUNTIME_CONFIG_PATH
#error "SAGE_MANAGED_RUNTIME_CONFIG_PATH must be provided by CMake"
#endif
#ifndef SAGE_MANAGED_HOST_ASSEMBLY_PATH
#error "SAGE_MANAGED_HOST_ASSEMBLY_PATH must be provided by CMake"
#endif

namespace sage
{
    namespace
    {
        enum class ScriptEvent : int
        {
            TriggerEnter,
            TriggerStay,
            TriggerExit,
            MovementStarted,
            DestinationReached,
            DestinationUnreachable,
            MovementCancelled,
            PathChanged,
            AnimationStarted,
            AnimationEnded,
            AnimationUpdated
        };

        using HostString = std::basic_string<char_t>;
        using LogFunction = void(SAGE_MANAGED_CALL*)(void*, int, const char*);
        using EntityExistsFunction = std::uint8_t(SAGE_MANAGED_CALL*)(void*, std::uint32_t);
        using FindArchetypeFunction = std::uint8_t(SAGE_MANAGED_CALL*)(void*, const char*, std::uint32_t*);
        using GetSurfaceFunction = std::uint8_t(SAGE_MANAGED_CALL*)(void*, float, float, float, std::uint32_t*);
        using HasRouteFunction = std::uint8_t(SAGE_MANAGED_CALL*)(void*, std::uint32_t);
        using TryPathfindFunction = std::uint8_t(SAGE_MANAGED_CALL*)(
            void*, std::uint32_t, float, float, float, std::uint8_t, std::uint8_t);
        using SpawnFlatpackFunction = std::uint8_t(SAGE_MANAGED_CALL*)(
            void*, const char*, float, float, float, float, float, float, std::uint8_t, std::uint32_t*);
        using SubscribeEventFunction = std::uint8_t(SAGE_MANAGED_CALL*)(void*, std::uint32_t, std::uint32_t, int);
        using HasComponentFunction = std::uint8_t(SAGE_MANAGED_CALL*)(void*, std::uint32_t, ScriptApiRegistry::Id);
        using GetComponentPropertyFunction = std::uint8_t(SAGE_MANAGED_CALL*)(
            void*, std::uint32_t, ScriptApiRegistry::Id, ScriptApiRegistry::Id, ScriptValue*);
        using SetComponentPropertyFunction = std::uint8_t(SAGE_MANAGED_CALL*)(
            void*, std::uint32_t, ScriptApiRegistry::Id, ScriptApiRegistry::Id, ScriptValue*);
        using InvokeComponentMethodFunction = std::uint8_t(SAGE_MANAGED_CALL*)(
            void*,
            std::uint32_t,
            ScriptApiRegistry::Id,
            ScriptApiRegistry::Id,
            ScriptValue*,
            std::uint32_t,
            ScriptValue*);

        struct NativeApiTable
        {
            std::uint32_t version = 3;
            std::uint32_t size = 0;
            void* context = nullptr;
            LogFunction log = nullptr;
            EntityExistsFunction entityExists = nullptr;
            FindArchetypeFunction findFirstWithArchetype = nullptr;
            GetSurfaceFunction getNavigationSurfaceAt = nullptr;
            HasRouteFunction hasRoute = nullptr;
            TryPathfindFunction tryPathfind = nullptr;
            SpawnFlatpackFunction spawnFlatpack = nullptr;
            SubscribeEventFunction subscribeEvent = nullptr;
            HasComponentFunction hasComponent = nullptr;
            GetComponentPropertyFunction getComponentProperty = nullptr;
            SetComponentPropertyFunction setComponentProperty = nullptr;
            InvokeComponentMethodFunction invokeComponentMethod = nullptr;
        };

        struct StartSessionArgs
        {
            const char* gameplayAssemblyPath = nullptr;
            NativeApiTable nativeApi{};
            void* gameApi = nullptr;
        };

        using StartSessionFunction = int(SAGE_MANAGED_CALL*)(const StartSessionArgs*);
        using CreateScriptFunction = int(SAGE_MANAGED_CALL*)(std::uint32_t, const char*);
        using UpdateScriptFunction = int(SAGE_MANAGED_CALL*)(std::uint32_t, float, std::uint8_t);
        using DispatchEventFunction =
            int(SAGE_MANAGED_CALL*)(std::uint32_t, int, std::uint32_t, float, float, float);
        using DestroyScriptFunction = int(SAGE_MANAGED_CALL*)(std::uint32_t);
        using StopSessionFunction = int(SAGE_MANAGED_CALL*)();

        HostString ToHostString(const char* value)
        {
#if defined(_WIN32)
            if (value == nullptr || *value == '\0') return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
            if (size <= 1) return {};
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
            result.pop_back();
            return result;
#else
            return value != nullptr ? value : "";
#endif
        }

        void* LoadNativeLibrary(const char_t* path)
        {
#if defined(_WIN32)
            return reinterpret_cast<void*>(LoadLibraryW(path));
#else
            return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
        }

        void* FindExport(void* library, const char* name)
        {
            if (library == nullptr) return nullptr;
#if defined(_WIN32)
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
            return dlsym(library, name);
#endif
        }

        class ManagedHost
        {
            void* hostFxrLibrary = nullptr;
            load_assembly_and_get_function_pointer_fn loadAssembly = nullptr;
            StartSessionFunction startSession = nullptr;
            CreateScriptFunction createScript = nullptr;
            UpdateScriptFunction updateScript = nullptr;
            DispatchEventFunction dispatchEvent = nullptr;
            DestroyScriptFunction destroyScript = nullptr;
            StopSessionFunction stopSession = nullptr;
            bool ready = false;

            bool loadEntryPoint(const char_t* methodName, void** destination) const
            {
                const auto assemblyPath = ToHostString(SAGE_MANAGED_HOST_ASSEMBLY_PATH);
                const int result = loadAssembly(
                    assemblyPath.c_str(),
                    SAGE_HOST_TEXT("Sage.ScriptRuntime, Sage.Scripting"),
                    methodName,
                    UNMANAGEDCALLERSONLY_METHOD,
                    nullptr,
                    destination);
                if (result >= 0 && *destination != nullptr) return true;
                TraceLog(LOG_ERROR, "C#: could not load a managed entry point (code %d).", result);
                return false;
            }

          public:
            [[nodiscard]] bool Initialize()
            {
                if (ready) return true;
                std::vector<char_t> hostFxrPath(4096);
                std::size_t hostFxrPathSize = hostFxrPath.size();
                const int pathResult = get_hostfxr_path(hostFxrPath.data(), &hostFxrPathSize, nullptr);
                if (pathResult != 0)
                {
                    TraceLog(LOG_ERROR, "C#: could not locate hostfxr (code %d).", pathResult);
                    return false;
                }
                hostFxrLibrary = LoadNativeLibrary(hostFxrPath.data());
                if (hostFxrLibrary == nullptr)
                {
                    TraceLog(LOG_ERROR, "C#: could not load hostfxr.");
                    return false;
                }
                const auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
                    FindExport(hostFxrLibrary, "hostfxr_initialize_for_runtime_config"));
                const auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
                    FindExport(hostFxrLibrary, "hostfxr_get_runtime_delegate"));
                const auto close = reinterpret_cast<hostfxr_close_fn>(FindExport(hostFxrLibrary, "hostfxr_close"));
                if (initialize == nullptr || getDelegate == nullptr || close == nullptr)
                {
                    TraceLog(LOG_ERROR, "C#: hostfxr is missing required exports.");
                    return false;
                }
                const auto configPath = ToHostString(SAGE_MANAGED_RUNTIME_CONFIG_PATH);
                hostfxr_handle context = nullptr;
                const int initializeResult = initialize(configPath.c_str(), nullptr, &context);
                if (initializeResult < 0 || context == nullptr)
                {
                    TraceLog(LOG_ERROR, "C#: hostfxr initialization failed (code %d).", initializeResult);
                    if (context != nullptr) close(context);
                    return false;
                }
                void* loadAssemblyAddress = nullptr;
                const int delegateResult =
                    getDelegate(context, hdt_load_assembly_and_get_function_pointer, &loadAssemblyAddress);
                close(context);
                if (delegateResult < 0 || loadAssemblyAddress == nullptr)
                {
                    TraceLog(LOG_ERROR, "C#: could not acquire the runtime delegate (code %d).", delegateResult);
                    return false;
                }
                loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyAddress);
                if (!loadEntryPoint(SAGE_HOST_TEXT("StartSession"), reinterpret_cast<void**>(&startSession)) ||
                    !loadEntryPoint(SAGE_HOST_TEXT("CreateScript"), reinterpret_cast<void**>(&createScript)) ||
                    !loadEntryPoint(SAGE_HOST_TEXT("UpdateScript"), reinterpret_cast<void**>(&updateScript)) ||
                    !loadEntryPoint(SAGE_HOST_TEXT("DispatchEvent"), reinterpret_cast<void**>(&dispatchEvent)) ||
                    !loadEntryPoint(SAGE_HOST_TEXT("DestroyScript"), reinterpret_cast<void**>(&destroyScript)) ||
                    !loadEntryPoint(SAGE_HOST_TEXT("StopSession"), reinterpret_cast<void**>(&stopSession)))
                    return false;
                ready = true;
                return true;
            }

            [[nodiscard]] bool Start(const ManagedScriptingConfig& config, const NativeApiTable& api) const
            {
                if (!ready) return false;
                const StartSessionArgs args{config.gameplayAssemblyPath.c_str(), api, config.gameApi};
                return startSession(&args) == 0;
            }
            [[nodiscard]] int CreateScript(const std::uint32_t entity, const char* type) const
            {
                return ready ? createScript(entity, type) : -1;
            }
            [[nodiscard]] int UpdateScript(const std::uint32_t entity, const float dt, const bool enabled) const
            {
                return ready ? updateScript(entity, dt, enabled ? 1 : 0) : -1;
            }
            [[nodiscard]] int DispatchEvent(
                const std::uint32_t listener,
                const ScriptEvent type,
                const std::uint32_t other,
                const Vector3 value) const
            {
                return ready ? dispatchEvent(listener, static_cast<int>(type), other, value.x, value.y, value.z)
                             : -1;
            }
            void DestroyScript(const std::uint32_t entity) const
            {
                if (ready) destroyScript(entity);
            }
            void Stop() const
            {
                if (ready) stopSession();
            }
        };

        ManagedHost& GetManagedHost()
        {
            static ManagedHost host;
            return host;
        }
    } // namespace

    struct CSharpScriptSystem::Impl
    {
        struct Instance
        {
            std::string className;
            bool failed = false;
            std::unordered_map<std::uint64_t, Subscription> subscriptions;
        };

        entt::registry* registry;
        EngineSystems* systems;
        ManagedScriptingConfig config;
        ScriptApiRegistry scriptApi;
        std::unordered_map<entt::entity, Instance> instances;
        bool available = false;

        static std::uint64_t SubscriptionKey(const entt::entity source, const ScriptEvent type)
        {
            return (static_cast<std::uint64_t>(entt::to_integral(source)) << 8) | static_cast<std::uint64_t>(type);
        }

        static void SAGE_MANAGED_CALL Log(void*, const int level, const char* message)
        {
            const int raylibLevel = level == 2 ? LOG_ERROR : (level == 1 ? LOG_WARNING : LOG_INFO);
            TraceLog(raylibLevel, "C#: %s", message != nullptr ? message : "");
        }

        [[nodiscard]] static entt::entity ToEntity(const std::uint32_t value)
        {
            return static_cast<entt::entity>(value);
        }

        static std::uint8_t SAGE_MANAGED_CALL EntityExists(void* context, const std::uint32_t value)
        {
            const auto& self = *static_cast<Impl*>(context);
            return self.registry->valid(ToEntity(value)) ? 1 : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL
        FindFirstWithArchetype(void* context, const char* name, std::uint32_t* destination)
        {
            auto& self = *static_cast<Impl*>(context);
            if (self.systems == nullptr || name == nullptr || destination == nullptr) return 0;
            const auto found = sage::FindFirstWithArchetype(*self.registry, MakeArchetype(name));
            if (!found) return 0;
            *destination = entt::to_integral(*found);
            return 1;
        }

        static std::uint8_t SAGE_MANAGED_CALL GetNavigationSurfaceAt(
            void* context, const float x, const float y, const float z, std::uint32_t* destination)
        {
            const auto& self = *static_cast<Impl*>(context);
            if (self.systems == nullptr || destination == nullptr) return 0;
            const auto surface = self.systems->navigationGridSystem->GetSurfaceAt(Vector3{x, y, z});
            if (surface == entt::null || !self.registry->valid(surface)) return 0;
            *destination = entt::to_integral(surface);
            return 1;
        }

        static std::uint8_t SAGE_MANAGED_CALL HasRoute(void* context, const std::uint32_t value)
        {
            const auto& self = *static_cast<Impl*>(context);
            if (self.systems == nullptr) return 0;
            const auto* moveable = self.registry->try_get<MoveableActor>(ToEntity(value));
            return moveable != nullptr && moveable->IsMoving() ? 1 : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL TryPathfind(
            void* context,
            const std::uint32_t value,
            const float x,
            const float y,
            const float z,
            const std::uint8_t aStar,
            const std::uint8_t findClosest)
        {
            const auto& self = *static_cast<Impl*>(context);
            if (self.systems == nullptr) return 0;
            const auto entity = ToEntity(value);
            if (!self.registry->valid(entity) ||
                !self.registry->all_of<sgTransform, MoveableActor, Collideable>(entity))
                return 0;
            return self.systems->actorMovementSystem->TryPathfindToLocation(
                       entity, Vector3{x, y, z}, aStar != 0, findClosest != 0)
                       ? 1
                       : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL SpawnFlatpack(
            void* context,
            const char* name,
            const float px,
            const float py,
            const float pz,
            const float rx,
            const float ry,
            const float rz,
            const std::uint8_t hasRotation,
            std::uint32_t* destination)
        {
            auto& self = *static_cast<Impl*>(context);
            if (self.systems == nullptr || name == nullptr || destination == nullptr) return 0;
            const auto rotation = hasRotation != 0 ? std::optional{Vector3{rx, ry, rz}} : std::nullopt;
            auto instance = InstantiateFlatpackByName(*self.registry, name, Vector3{px, py, pz}, rotation);
            if (!instance) return 0;
            for (const auto entity : instance.entities)
            {
                const auto* obstacle = self.registry->try_get<NavigationObstacle>(entity);
                const auto* collideable = self.registry->try_get<Collideable>(entity);
                if (obstacle && obstacle->active && collideable && !self.registry->any_of<MoveableActor>(entity))
                    self.systems->navigationGridSystem->MarkSquareAreaOccupied(
                        collideable->worldBoundingBox, true, entity);
            }
            *destination = entt::to_integral(instance.root);
            return 1;
        }

        void dispatch(
            const entt::entity listener,
            const ScriptEvent type,
            const entt::entity other = entt::null,
            const Vector3 value = {})
        {
            const auto found = instances.find(listener);
            if (found == instances.end() || found->second.failed) return;
            if (GetManagedHost().DispatchEvent(
                    entt::to_integral(listener), type, entt::to_integral(other), value) != 0)
                found->second.failed = true;
        }

        bool subscribe(const entt::entity listener, const entt::entity source, const ScriptEvent type)
        {
            auto instance = instances.find(listener);
            if (systems == nullptr || instance == instances.end() || !registry->valid(source)) return false;
            const auto key = SubscriptionKey(source, type);
            if (instance->second.subscriptions.contains(key)) return true;

            Subscription subscription;
            switch (type)
            {
            case ScriptEvent::TriggerEnter:
                subscription = systems->collisionSystem->onTriggerEnter.Subscribe(
                    [this, listener, source](const entt::entity trigger, const entt::entity other) {
                        if (trigger == source) dispatch(listener, ScriptEvent::TriggerEnter, other);
                    });
                break;
            case ScriptEvent::TriggerStay:
                subscription = systems->collisionSystem->onTriggerStay.Subscribe(
                    [this, listener, source](const entt::entity trigger, const entt::entity other) {
                        if (trigger == source) dispatch(listener, ScriptEvent::TriggerStay, other);
                    });
                break;
            case ScriptEvent::TriggerExit:
                subscription = systems->collisionSystem->onTriggerExit.Subscribe(
                    [this, listener, source](const entt::entity trigger, const entt::entity other) {
                        if (trigger == source) dispatch(listener, ScriptEvent::TriggerExit, other);
                    });
                break;
            case ScriptEvent::MovementStarted:
            case ScriptEvent::DestinationReached:
            case ScriptEvent::DestinationUnreachable:
            case ScriptEvent::MovementCancelled:
            case ScriptEvent::PathChanged: {
                auto* moveable = registry->try_get<MoveableActor>(source);
                if (moveable == nullptr) return false;
                if (type == ScriptEvent::DestinationUnreachable)
                    subscription = moveable->onDestinationUnreachable.Subscribe(
                        [this, listener](entt::entity, const Vector3 destination) {
                            dispatch(listener, ScriptEvent::DestinationUnreachable, entt::null, destination);
                        });
                else
                {
                    Event<entt::entity>* event = nullptr;
                    if (type == ScriptEvent::MovementStarted) event = &moveable->onStartMovement;
                    if (type == ScriptEvent::DestinationReached) event = &moveable->onDestinationReached;
                    if (type == ScriptEvent::MovementCancelled) event = &moveable->onMovementCancel;
                    if (type == ScriptEvent::PathChanged) event = &moveable->onPathChanged;
                    subscription =
                        event->Subscribe([this, listener, type](entt::entity) { dispatch(listener, type); });
                }
                break;
            }
            case ScriptEvent::AnimationStarted:
            case ScriptEvent::AnimationEnded:
            case ScriptEvent::AnimationUpdated: {
                auto* animation = registry->try_get<Animation>(source);
                if (animation == nullptr) return false;
                Event<entt::entity>* event =
                    type == ScriptEvent::AnimationStarted
                        ? &animation->onAnimationStart
                        : (type == ScriptEvent::AnimationEnded ? &animation->onAnimationEnd
                                                               : &animation->onAnimationUpdated);
                subscription =
                    event->Subscribe([this, listener, type](entt::entity) { dispatch(listener, type); });
                break;
            }
            }
            instance->second.subscriptions.emplace(key, subscription);
            return true;
        }

        static std::uint8_t SAGE_MANAGED_CALL
        SubscribeEvent(void* context, const std::uint32_t listener, const std::uint32_t source, const int type)
        {
            auto& self = *static_cast<Impl*>(context);
            if (type < 0 || type > static_cast<int>(ScriptEvent::AnimationUpdated)) return 0;
            return self.subscribe(ToEntity(listener), ToEntity(source), static_cast<ScriptEvent>(type)) ? 1 : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL
        HasComponent(void* context, const std::uint32_t value, const ScriptApiRegistry::Id componentId)
        {
            const auto& self = *static_cast<Impl*>(context);
            return self.scriptApi.HasComponent(*self.registry, ToEntity(value), componentId) ? 1 : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL GetComponentProperty(
            void* context,
            const std::uint32_t value,
            const ScriptApiRegistry::Id componentId,
            const ScriptApiRegistry::Id propertyId,
            ScriptValue* destination)
        {
            auto& self = *static_cast<Impl*>(context);
            return destination != nullptr &&
                           self.scriptApi.GetProperty(
                               *self.registry, ToEntity(value), componentId, propertyId, *destination)
                       ? 1
                       : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL SetComponentProperty(
            void* context,
            const std::uint32_t value,
            const ScriptApiRegistry::Id componentId,
            const ScriptApiRegistry::Id propertyId,
            ScriptValue* input)
        {
            auto& self = *static_cast<Impl*>(context);
            return input != nullptr && self.scriptApi.SetProperty(
                                           *self.registry, ToEntity(value), componentId, propertyId, *input)
                       ? 1
                       : 0;
        }

        static std::uint8_t SAGE_MANAGED_CALL InvokeComponentMethod(
            void* context,
            const std::uint32_t value,
            const ScriptApiRegistry::Id componentId,
            const ScriptApiRegistry::Id methodId,
            ScriptValue* arguments,
            const std::uint32_t argumentCount,
            ScriptValue* result)
        {
            auto& self = *static_cast<Impl*>(context);
            if (result == nullptr || (argumentCount != 0 && arguments == nullptr)) return 0;
            return self.scriptApi.Invoke(
                       *self.registry,
                       ToEntity(value),
                       componentId,
                       methodId,
                       std::span<const ScriptValue>{arguments, argumentCount},
                       *result)
                       ? 1
                       : 0;
        }

        explicit Impl(entt::registry* source, EngineSystems* engineSystems, ManagedScriptingConfig managedConfig)
            : registry(source), systems(engineSystems), config(std::move(managedConfig))
        {
            RegisterEngineScriptApi(scriptApi);
            if (config.registerScriptApi) config.registerScriptApi(scriptApi);
            if (config.gameplayAssemblyPath.empty()) return;
            auto& host = GetManagedHost();
            if (!host.Initialize()) return;
            const NativeApiTable api{
                .version = 3,
                .size = sizeof(NativeApiTable),
                .context = this,
                .log = &Log,
                .entityExists = &EntityExists,
                .findFirstWithArchetype = &FindFirstWithArchetype,
                .getNavigationSurfaceAt = &GetNavigationSurfaceAt,
                .hasRoute = &HasRoute,
                .tryPathfind = &TryPathfind,
                .spawnFlatpack = &SpawnFlatpack,
                .subscribeEvent = &SubscribeEvent,
                .hasComponent = &HasComponent,
                .getComponentProperty = &GetComponentProperty,
                .setComponentProperty = &SetComponentProperty,
                .invokeComponentMethod = &InvokeComponentMethod};
            available = host.Start(config, api);
            if (!available) TraceLog(LOG_ERROR, "C#: gameplay session did not start.");
        }

        ~Impl()
        {
            stop();
        }

        void destroyInstance(const entt::entity entity)
        {
            const auto instance = instances.find(entity);
            if (instance == instances.end()) return;
            GetManagedHost().DestroyScript(entt::to_integral(entity));
            for (auto& [key, subscription] : instance->second.subscriptions)
                subscription.UnSubscribe();
            instances.erase(instance);
        }

        void stop()
        {
            if (!available) return;
            GetManagedHost().Stop();
            for (auto& [entity, instance] : instances)
                for (auto& [key, subscription] : instance.subscriptions)
                    subscription.UnSubscribe();
            instances.clear();
            available = false;
        }

        void removeSubscriptionsFromSource(
            const entt::entity source, const ScriptEvent firstType, const ScriptEvent lastType)
        {
            for (auto& [listener, instance] : instances)
            {
                for (auto subscription = instance.subscriptions.begin();
                     subscription != instance.subscriptions.end();)
                {
                    const auto sourceValue = subscription->first >> 8;
                    const auto typeValue = static_cast<int>(subscription->first & 0xff);
                    if (sourceValue == entt::to_integral(source) && typeValue >= static_cast<int>(firstType) &&
                        typeValue <= static_cast<int>(lastType))
                    {
                        subscription->second.UnSubscribe();
                        subscription = instance.subscriptions.erase(subscription);
                    }
                    else
                        ++subscription;
                }
            }
        }
    };

    void CSharpScriptSystem::Update(const float deltaTime)
    {
        if (!impl->available) return;
        for (const auto view = registry->view<ScriptComponent>(); const auto entity : view)
        {
            auto& script = view.get<ScriptComponent>(entity);
            auto instance = impl->instances.find(entity);
            if ((instance == impl->instances.end() || instance->second.className != script.className) &&
                impl->config.migrateScriptClass)
            {
                impl->config.migrateScriptClass(script.className);
            }
            if (instance != impl->instances.end() && instance->second.className != script.className)
            {
                impl->destroyInstance(entity);
                instance = impl->instances.end();
            }
            if (instance == impl->instances.end())
            {
                if (script.className.empty()) continue;
                instance = impl->instances.emplace(entity, Impl::Instance{.className = script.className}).first;
                const int result =
                    GetManagedHost().CreateScript(entt::to_integral(entity), script.className.c_str());
                instance->second.failed = result != 0;
                if (!instance->second.failed)
                {
                    for (int type = static_cast<int>(ScriptEvent::TriggerEnter);
                         type <= static_cast<int>(ScriptEvent::AnimationUpdated);
                         ++type)
                    {
                        impl->subscribe(entity, entity, static_cast<ScriptEvent>(type));
                    }
                }
            }
            if (instance->second.failed) continue;
            if (GetManagedHost().UpdateScript(entt::to_integral(entity), deltaTime, script.enabled) != 0)
                instance->second.failed = true;
        }
    }

    bool CSharpScriptSystem::IsAvailable() const
    {
        return impl->available;
    }
    void CSharpScriptSystem::onScriptDestroyed(entt::registry&, const entt::entity entity)
    {
        impl->destroyInstance(entity);
    }
    void CSharpScriptSystem::onMoveableDestroyed(entt::registry&, const entt::entity entity)
    {
        impl->removeSubscriptionsFromSource(entity, ScriptEvent::MovementStarted, ScriptEvent::PathChanged);
    }
    void CSharpScriptSystem::onAnimationDestroyed(entt::registry&, const entt::entity entity)
    {
        impl->removeSubscriptionsFromSource(entity, ScriptEvent::AnimationStarted, ScriptEvent::AnimationUpdated);
    }

    CSharpScriptSystem::CSharpScriptSystem(
        entt::registry* source, EngineSystems* systems, ManagedScriptingConfig config)
        : impl(std::make_unique<Impl>(source, systems, std::move(config))), registry(source)
    {
        registry->on_destroy<ScriptComponent>().connect<&CSharpScriptSystem::onScriptDestroyed>(this);
        registry->on_destroy<MoveableActor>().connect<&CSharpScriptSystem::onMoveableDestroyed>(this);
        registry->on_destroy<Animation>().connect<&CSharpScriptSystem::onAnimationDestroyed>(this);
    }

    CSharpScriptSystem::~CSharpScriptSystem()
    {
        registry->on_destroy<Animation>().disconnect<&CSharpScriptSystem::onAnimationDestroyed>(this);
        registry->on_destroy<MoveableActor>().disconnect<&CSharpScriptSystem::onMoveableDestroyed>(this);
        registry->on_destroy<ScriptComponent>().disconnect<&CSharpScriptSystem::onScriptDestroyed>(this);
    }
} // namespace sage

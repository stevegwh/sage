#pragma once

#include "entt/entt.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace sage
{
    struct EngineSystems;
    class ScriptApiRegistry;

    enum class CSharpLogLevel : int
    {
        Info,
        Warning,
        Error
    };

    using CSharpLogSink = std::function<void(CSharpLogLevel, std::string_view)>;

    struct ManagedScriptingConfig
    {
        std::string gameplayAssemblyPath;
        void* gameApi = nullptr;
        // Per-runtime output destination; this does not register script API types.
        CSharpLogSink logSink;
        std::function<void(std::string&)> migrateScriptClass;
        // Populates the native types exposed to managed scripts.
        std::function<void(ScriptApiRegistry&)> populateScriptApi;
    };

    class CSharpScriptSystem
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        entt::registry* registry;

        void onScriptDestroyed(entt::registry& source, entt::entity entity);

      public:
        void Update(float deltaTime);
        [[nodiscard]] bool IsAvailable() const;

        CSharpScriptSystem(entt::registry* registry, EngineSystems* systems, ManagedScriptingConfig config);
        ~CSharpScriptSystem();
    };
} // namespace sage

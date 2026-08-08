#pragma once

#include "entt/entt.hpp"

#include <functional>
#include <memory>
#include <string>

namespace sage
{
    struct EngineSystems;
    class ScriptApiRegistry;

    struct ManagedScriptingConfig
    {
        std::string gameplayAssemblyPath;
        void* gameApi = nullptr;
        std::function<void(std::string&)> migrateScriptClass;
        std::function<void(ScriptApiRegistry&)> registerScriptApi;
    };

    class CSharpScriptSystem
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        entt::registry* registry;

        void onScriptDestroyed(entt::registry& source, entt::entity entity);
        void onMoveableDestroyed(entt::registry& source, entt::entity entity);
        void onAnimationDestroyed(entt::registry& source, entt::entity entity);

      public:
        void Update(float deltaTime);
        [[nodiscard]] bool IsAvailable() const;

        CSharpScriptSystem(entt::registry* registry, EngineSystems* systems, ManagedScriptingConfig config);
        ~CSharpScriptSystem();
    };
} // namespace sage

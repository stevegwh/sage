#pragma once

#include "Flatpack.hpp"
#include "scripting/ScriptApi.hpp"

#include <string>
#include <type_traits>

namespace sage
{
    struct ComponentPersistence
    {
        std::string persistenceKey;
        void key(std::string value) { persistenceKey = std::move(value); }
    };

    // The same component list can populate runtime/codegen or editor services.
    // No editor dependency is needed by the engine or the game runtime.
    template <class Inspector = void>
    struct ComponentRegistration
    {
        ScriptApiRegistry* scripts = nullptr;
        Inspector* inspector = nullptr;

        template <class T>
        void Register(const std::string& scriptName, const std::string& displayName = {})
        {
            ComponentPersistence persistence;
            if constexpr (requires { T::define_persistence(persistence); })
            {
                T::define_persistence(persistence);
                RegisterFlatpackComponent<T>(persistence.persistenceKey);
            }

            if constexpr (requires(ScriptApiBinder<T>& api) { T::define_script_api(api); })
                if (scripts) scripts->RegisterComponent<T>(scriptName);

            if constexpr (!std::is_void_v<Inspector>)
            {
                if constexpr (Inspector::template SupportsComponent<T>)
                {
                    if (!inspector) return;
                    const auto& label = displayName.empty() ? scriptName : displayName;
                    if constexpr (requires { T::define_persistence(persistence); })
                        inspector->template RegisterPersistent<T>(label, persistence.persistenceKey);
                    else
                        inspector->template Register<T>(label);
                }
            }
        }
    };
} // namespace sage

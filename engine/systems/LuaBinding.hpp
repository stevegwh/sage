#pragma once

#include "ScriptSystem.hpp"

#include "magic_enum/magic_enum.hpp"
#include "sol/sol.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sage
{
    // Runtime consumer for a component's archive-style define_lua_api() declaration.
    // Component authors use this vocabulary instead of depending on sol2 directly.
    template <class T>
    class LuaTypeBinder
    {
        sol::usertype<T> type;

      public:
        explicit LuaTypeBinder(sol::usertype<T> _type) : type(std::move(_type))
        {
        }

        template <class Value>
        void property(std::string name, Value T::* member)
        {
            type.set(std::move(name), member);
        }

        template <class Value>
        void readonly(std::string name, Value T::* member)
        {
            type.set(std::move(name), sol::readonly(member));
        }

        template <class Callable>
        void method(std::string name, Callable&& callable)
        {
            type.set(std::move(name), std::forward<Callable>(callable));
        }

        template <class... Callable>
        void overload(std::string name, Callable&&... callable)
        {
            type.set(std::move(name), sol::overload(std::forward<Callable>(callable)...));
        }

        // sol2 otherwise exposes standard containers as userdata. This helper makes
        // the common API promise explicit without mentioning sol::as_table in a component.
        template <class Callable>
        void table_method(std::string name, Callable&& callable)
        {
            type.set(
                std::move(name),
                [fn = std::forward<Callable>(callable)](const T& value) {
                    return sol::as_table(std::invoke(fn, value));
                });
        }

        // Entity ids are integers in Lua, with entt::null represented as nil.
        template <class Callable>
        void nullable_entity_method(std::string name, Callable&& callable)
        {
            type.set(
                std::move(name),
                [fn = std::forward<Callable>(callable)](const T& value, sol::this_state state) -> sol::object {
                    const auto entity = std::invoke(fn, value);
                    if (entity == entt::null) return sol::lua_nil;
                    return sol::make_object(state, static_cast<std::uint32_t>(entity));
                });
        }
    };

    template <class T>
    void ScriptSystem::RegisterType(std::string typeName)
    {
        auto usertype = GetLuaState().new_usertype<T>(typeName, sol::no_constructor);
        LuaTypeBinder<T> binder{std::move(usertype)};
        T::define_lua_api(binder);
    }

    template <class E>
    void ScriptSystem::RegisterEnum(std::string typeName)
    {
        static_assert(std::is_enum_v<E>, "ScriptSystem::RegisterEnum requires an enum type");
        constexpr auto entries = magic_enum::enum_entries<E>();
        static_assert(!entries.empty(), "ScriptSystem::RegisterEnum found no named enumerators");

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            auto arguments = std::tuple_cat(std::make_tuple(entries[I].second, entries[I].first)...);
            std::apply(
                [this, &typeName](auto&&... argument) {
                    GetLuaState().new_enum(typeName, std::forward<decltype(argument)>(argument)...);
                },
                arguments);
        }(std::make_index_sequence<entries.size()>{});
    }

    template <class T>
    void ScriptSystem::RegisterComponent(std::string typeName, std::string accessorName)
    {
        RegisterType<T>(std::move(typeName));

        RegisterApiExtension(
            "sage", [accessorName = std::move(accessorName)](sol::table& api, entt::entity, entt::registry& registry) {
                api.set_function(accessorName, [&registry](const std::uint32_t id) -> T* {
                    const auto entity = static_cast<entt::entity>(id);
                    return registry.valid(entity) ? registry.template try_get<T>(entity) : nullptr;
                });
            });
    }
} // namespace sage

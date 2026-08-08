#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sage
{
    enum class ScriptValueType : std::uint32_t
    {
        None,
        Boolean,
        Int32,
        UInt32,
        Float,
        String,
        Vector3,
        Entity
    };

    // Stable, language-neutral payload used at the native/managed boundary.
    // Text points to caller-owned storage; textCapacity includes the terminator.
    struct ScriptValue
    {
        ScriptValueType type = ScriptValueType::None;
        std::uint32_t reserved = 0;
        std::uint64_t integer = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        char* text = nullptr;
        std::uint32_t textCapacity = 0;
    };

    class ScriptApiRegistry;
    template <class T>
    class ScriptApiBinder;
    template <class T>
    class ScriptSystemApiBinder;

    namespace detail
    {
        template <class T>
        using Plain = std::remove_cv_t<std::remove_reference_t<T>>;

        template <class T>
        constexpr bool IsScriptValue =
            std::same_as<Plain<T>, bool> || std::same_as<Plain<T>, int> || std::same_as<Plain<T>, unsigned int> ||
            std::same_as<Plain<T>, float> || std::same_as<Plain<T>, std::string> ||
            std::same_as<Plain<T>, std::string_view> || std::same_as<Plain<T>, const char*> ||
            std::same_as<Plain<T>, Vector3> || std::same_as<Plain<T>, entt::entity> || std::is_enum_v<Plain<T>>;

        template <class T>
        constexpr ScriptValueType ScriptValueTypeOf()
        {
            using Value = Plain<T>;
            static_assert(IsScriptValue<Value>, "This C++ type is not supported by the script API");
            if constexpr (std::same_as<Value, bool>) return ScriptValueType::Boolean;
            if constexpr (std::same_as<Value, entt::entity>) return ScriptValueType::Entity;
            if constexpr (std::same_as<Value, int> || std::is_enum_v<Value>) return ScriptValueType::Int32;
            if constexpr (std::same_as<Value, unsigned int>) return ScriptValueType::UInt32;
            if constexpr (std::same_as<Value, float>) return ScriptValueType::Float;
            if constexpr (
                std::same_as<Value, std::string> || std::same_as<Value, std::string_view> ||
                std::same_as<Value, const char*>)
                return ScriptValueType::String;
            if constexpr (std::same_as<Value, Vector3>) return ScriptValueType::Vector3;
            return ScriptValueType::None;
        }

        template <class T>
        bool EncodeScriptValue(ScriptValue& destination, const T& source)
        {
            using Value = Plain<T>;
            static_assert(IsScriptValue<Value>, "This C++ type is not supported by the script API");
            destination.type = ScriptValueTypeOf<Value>();
            if constexpr (std::same_as<Value, bool>)
                destination.integer = source ? 1 : 0;
            else if constexpr (std::same_as<Value, int>)
                destination.integer = static_cast<std::uint64_t>(static_cast<std::int64_t>(source));
            else if constexpr (std::same_as<Value, unsigned int>)
                destination.integer = source;
            else if constexpr (std::same_as<Value, float>)
                destination.x = source;
            else if constexpr (std::same_as<Value, Vector3>)
            {
                destination.x = source.x;
                destination.y = source.y;
                destination.z = source.z;
            }
            else if constexpr (std::same_as<Value, entt::entity>)
                destination.integer = entt::to_integral(source);
            else if constexpr (std::is_enum_v<Value>)
                destination.integer = static_cast<std::uint64_t>(static_cast<std::int64_t>(source));
            else
            {
                const std::string_view text = [&]() -> std::string_view {
                    if constexpr (std::same_as<Value, const char*>) return source != nullptr ? source : "";
                    return source;
                }();
                if (destination.text == nullptr || destination.textCapacity <= text.size()) return false;
                std::copy(text.begin(), text.end(), destination.text);
                destination.text[text.size()] = '\0';
            }
            return true;
        }

        template <class T>
        bool DecodeScriptValue(const ScriptValue& source, T& destination)
        {
            using Value = Plain<T>;
            static_assert(IsScriptValue<Value>, "This C++ type is not supported by the script API");
            if (source.type != ScriptValueTypeOf<Value>()) return false;
            if constexpr (std::same_as<Value, bool>)
                destination = source.integer != 0;
            else if constexpr (std::same_as<Value, int>)
                destination = static_cast<int>(static_cast<std::int64_t>(source.integer));
            else if constexpr (std::same_as<Value, unsigned int>)
                destination = static_cast<unsigned int>(source.integer);
            else if constexpr (std::same_as<Value, float>)
                destination = source.x;
            else if constexpr (std::same_as<Value, Vector3>)
                destination = Vector3{source.x, source.y, source.z};
            else if constexpr (std::same_as<Value, entt::entity>)
                destination = static_cast<entt::entity>(static_cast<std::uint32_t>(source.integer));
            else if constexpr (std::is_enum_v<Value>)
                destination = static_cast<Value>(static_cast<int>(static_cast<std::int64_t>(source.integer)));
            else if constexpr (std::same_as<Value, std::string>)
                destination = source.text != nullptr ? source.text : "";
            else if constexpr (std::same_as<Value, std::string_view>)
                destination = source.text != nullptr ? std::string_view{source.text} : std::string_view{};
            else
                destination = source.text;
            return true;
        }

        template <class T>
        struct MemberFunction;

        template <class Result, class Owner, class... Args>
        struct MemberFunction<Result (Owner::*)(Args...)>
        {
            using Return = Result;
            using Arguments = std::tuple<Plain<Args>...>;
            static constexpr bool IsConst = false;
        };

        template <class Result, class Owner, class... Args>
        struct MemberFunction<Result (Owner::*)(Args...) const>
        {
            using Return = Result;
            using Arguments = std::tuple<Plain<Args>...>;
            static constexpr bool IsConst = true;
        };

        template <class Tuple, std::size_t... Index>
        bool DecodeArguments(
            const std::span<const ScriptValue> source, Tuple& destination, std::index_sequence<Index...>)
        {
            return (DecodeScriptValue(source[Index], std::get<Index>(destination)) && ...);
        }
    } // namespace detail

    class ScriptApiRegistry
    {
      public:
        using Id = std::uint64_t;

        struct Parameter
        {
            std::string name;
            ScriptValueType type = ScriptValueType::None;
            std::string managedType;
        };

        struct Property
        {
            Id id = 0;
            std::string name;
            ScriptValueType type = ScriptValueType::None;
            std::string managedType;
            bool writable = false;
            std::function<bool(entt::registry&, entt::entity, ScriptValue&)> get;
            std::function<bool(entt::registry&, entt::entity, const ScriptValue&)> set;
        };

        struct Method
        {
            Id id = 0;
            std::string name;
            ScriptValueType returnType = ScriptValueType::None;
            std::string managedReturnType;
            std::vector<Parameter> parameters;
            std::function<bool(entt::registry&, entt::entity, std::span<const ScriptValue>, ScriptValue&)> invoke;
        };

        struct Component
        {
            Id id = 0;
            std::string managedNamespace;
            std::string managedName;
            std::function<bool(const entt::registry&, entt::entity)> has;
            std::vector<Property> properties;
            std::vector<Method> methods;
        };

        struct System
        {
            Id id = 0;
            std::string managedNamespace;
            std::string managedName;
            std::vector<Method> methods;
        };

        struct Enum
        {
            std::string managedNamespace;
            std::string managedName;
            std::vector<std::pair<std::string, std::int64_t>> values;
        };

      private:
        std::vector<Component> components;
        std::vector<System> systems;
        std::vector<Enum> enums;
        std::unordered_map<std::type_index, std::string> managedEnumNames;

        [[nodiscard]] Component* findComponent(Id componentId);
        [[nodiscard]] const Component* findComponent(Id componentId) const;
        [[nodiscard]] const System* findSystem(Id systemId) const;

        template <class T>
        [[nodiscard]] std::string managedTypeName() const
        {
            using Value = detail::Plain<T>;
            if constexpr (std::same_as<Value, bool>) return "bool";
            if constexpr (std::same_as<Value, int>) return "int";
            if constexpr (std::same_as<Value, unsigned int>) return "uint";
            if constexpr (std::same_as<Value, float>) return "float";
            if constexpr (
                std::same_as<Value, std::string> || std::same_as<Value, std::string_view> ||
                std::same_as<Value, const char*>)
                return "string";
            if constexpr (std::same_as<Value, Vector3>) return "global::Sage.Vector3";
            if constexpr (std::same_as<Value, entt::entity>) return "global::Sage.Entity";
            if constexpr (std::is_enum_v<Value>)
            {
                const auto found = managedEnumNames.find(std::type_index{typeid(Value)});
                return found != managedEnumNames.end() ? found->second : std::string{};
            }
            return {};
        }

        template <class T>
        friend class ScriptApiBinder;
        template <class T>
        friend class ScriptSystemApiBinder;

      public:
        [[nodiscard]] static Id MakeId(std::string_view name);

        template <class EnumType>
        void RegisterEnum(
            std::string managedNamespace,
            std::string managedName,
            std::initializer_list<std::pair<std::string_view, EnumType>> values)
        {
            static_assert(std::is_enum_v<EnumType>);
            Enum definition{
                .managedNamespace = std::move(managedNamespace), .managedName = std::move(managedName)};
            for (const auto& [name, value] : values)
                definition.values.emplace_back(name, static_cast<std::int64_t>(value));
            managedEnumNames.insert_or_assign(
                std::type_index{typeid(EnumType)},
                "global::" + definition.managedNamespace + "." + definition.managedName);
            enums.push_back(std::move(definition));
        }

        template <class T>
        void RegisterComponent(std::string managedNamespace, std::string managedName);

        template <class T>
        void RegisterSystem(std::string managedNamespace, std::string managedName);

        [[nodiscard]] bool HasComponent(const entt::registry& registry, entt::entity entity, Id componentId) const;
        [[nodiscard]] bool GetProperty(
            entt::registry& registry,
            entt::entity entity,
            Id componentId,
            Id propertyId,
            ScriptValue& destination) const;
        [[nodiscard]] bool SetProperty(
            entt::registry& registry,
            entt::entity entity,
            Id componentId,
            Id propertyId,
            const ScriptValue& value) const;
        [[nodiscard]] bool Invoke(
            entt::registry& registry,
            entt::entity entity,
            Id componentId,
            Id methodId,
            std::span<const ScriptValue> arguments,
            ScriptValue& result) const;
        [[nodiscard]] bool WriteCSharp(const std::filesystem::path& destination) const;
    };

    template <class T>
    class ScriptSystemApiBinder
    {
        ScriptApiRegistry& api;
        ScriptApiRegistry::System& system;

      public:
        ScriptSystemApiBinder(ScriptApiRegistry& owner, ScriptApiRegistry::System& definition)
            : api(owner), system(definition)
        {
        }

        template <class Result, class... Args>
        void method(
            std::string name,
            Result (*method)(entt::registry&, Args...),
            const std::initializer_list<std::string_view> parameterNames = {})
        {
            using Return = detail::Plain<Result>;
            using Arguments = std::tuple<detail::Plain<Args>...>;
            static_assert(std::is_void_v<Return> || detail::IsScriptValue<Return>);
            static_assert((detail::IsScriptValue<Args> && ...));

            std::vector<std::string> names;
            names.reserve(parameterNames.size());
            for (const auto parameterName : parameterNames)
                names.emplace_back(parameterName);

            std::vector<ScriptApiRegistry::Parameter> parameters;
            parameters.reserve(sizeof...(Args));
            [&]<std::size_t... Index>(std::index_sequence<Index...>) {
                (parameters.push_back(
                     {.name = Index < names.size() ? names[Index] : "arg" + std::to_string(Index),
                      .type = detail::ScriptValueTypeOf<std::tuple_element_t<Index, Arguments>>(),
                      .managedType = api.template managedTypeName<std::tuple_element_t<Index, Arguments>>()}),
                 ...);
            }(std::index_sequence_for<Args...>{});

            std::string signature = system.managedNamespace + "." + system.managedName + "." + name + "(";
            for (std::size_t index = 0; index < parameters.size(); ++index)
            {
                if (index != 0) signature += ",";
                signature += parameters[index].managedType;
            }
            signature += ")";

            constexpr auto returnType = [] {
                if constexpr (std::is_void_v<Return>)
                    return ScriptValueType::None;
                else
                    return detail::ScriptValueTypeOf<Return>();
            }();
            const std::string managedReturnType = [&] {
                if constexpr (std::is_void_v<Return>)
                    return std::string{"void"};
                else
                    return api.template managedTypeName<Return>();
            }();

            system.methods.push_back(
                {.id = ScriptApiRegistry::MakeId(signature),
                 .name = std::move(name),
                 .returnType = returnType,
                 .managedReturnType = managedReturnType,
                 .parameters = std::move(parameters),
                 .invoke = [method](
                               entt::registry& source,
                               entt::entity,
                               const std::span<const ScriptValue> input,
                               ScriptValue& output) {
                     if (input.size() != sizeof...(Args)) return false;
                     Arguments arguments{};
                     if (!detail::DecodeArguments(input, arguments, std::index_sequence_for<Args...>{}))
                         return false;
                     if constexpr (std::is_void_v<Return>)
                     {
                         std::apply([&](auto&... argument) { method(source, argument...); }, arguments);
                         output.type = ScriptValueType::None;
                         return true;
                     }
                     else
                     {
                         decltype(auto) result =
                             std::apply([&](auto&... argument) { return method(source, argument...); }, arguments);
                         return detail::EncodeScriptValue(output, result);
                     }
                 }});
        }
    };

    template <class T>
    class ScriptApiBinder
    {
        ScriptApiRegistry& api;
        ScriptApiRegistry::Component& component;

        template <class Value, class Getter, class Setter>
        void addProperty(std::string name, Getter getter, Setter setter, const bool writable)
        {
            static_assert(detail::IsScriptValue<Value>);
            const auto qualifiedName = component.managedNamespace + "." + component.managedName + "." + name;
            component.properties.push_back(
                {.id = ScriptApiRegistry::MakeId(qualifiedName),
                 .name = std::move(name),
                 .type = detail::ScriptValueTypeOf<Value>(),
                 .managedType = api.template managedTypeName<Value>(),
                 .writable = writable,
                 .get =
                     [getter = std::move(getter)](
                         entt::registry& source, const entt::entity entity, ScriptValue& destination) {
                         const auto* value = source.template try_get<T>(entity);
                         return value != nullptr &&
                                detail::EncodeScriptValue(destination, std::invoke(getter, *value));
                     },
                 .set =
                     [setter = std::move(setter),
                      writable](entt::registry& source, const entt::entity entity, const ScriptValue& input) {
                         if (!writable) return false;
                         auto* value = source.template try_get<T>(entity);
                         detail::Plain<Value> decoded{};
                         return value != nullptr && detail::DecodeScriptValue(input, decoded) &&
                                (std::invoke(setter, *value, decoded), true);
                     }});
        }

        template <class Method, std::size_t... Index>
        void addMethod(
            std::string name,
            Method method,
            const std::initializer_list<std::string_view> parameterNames,
            std::index_sequence<Index...>)
        {
            using Traits = detail::MemberFunction<Method>;
            using Arguments = typename Traits::Arguments;
            using Return = detail::Plain<typename Traits::Return>;
            static_assert(std::is_void_v<Return> || detail::IsScriptValue<Return>);
            static_assert((detail::IsScriptValue<std::tuple_element_t<Index, Arguments>> && ...));

            std::vector<std::string> names;
            names.reserve(parameterNames.size());
            for (const auto parameterName : parameterNames)
                names.emplace_back(parameterName);

            std::vector<ScriptApiRegistry::Parameter> parameters;
            parameters.reserve(sizeof...(Index));
            (parameters.push_back(
                 {.name = Index < names.size() ? names[Index] : "arg" + std::to_string(Index),
                  .type = detail::ScriptValueTypeOf<std::tuple_element_t<Index, Arguments>>(),
                  .managedType = api.template managedTypeName<std::tuple_element_t<Index, Arguments>>()}),
             ...);

            std::string signature = component.managedNamespace + "." + component.managedName + "." + name + "(";
            for (std::size_t index = 0; index < parameters.size(); ++index)
            {
                if (index != 0) signature += ",";
                signature += parameters[index].managedType;
            }
            signature += ")";

            constexpr auto returnType = [] {
                if constexpr (std::is_void_v<Return>)
                    return ScriptValueType::None;
                else
                    return detail::ScriptValueTypeOf<Return>();
            }();
            const std::string managedReturnType = [&] {
                if constexpr (std::is_void_v<Return>)
                    return std::string{"void"};
                else
                    return api.template managedTypeName<Return>();
            }();

            component.methods.push_back(
                {.id = ScriptApiRegistry::MakeId(signature),
                 .name = std::move(name),
                 .returnType = returnType,
                 .managedReturnType = managedReturnType,
                 .parameters = std::move(parameters),
                 .invoke = [method](
                               entt::registry& source,
                               const entt::entity entity,
                               const std::span<const ScriptValue> input,
                               ScriptValue& output) {
                     auto* value = source.template try_get<T>(entity);
                     if (value == nullptr || input.size() != sizeof...(Index)) return false;
                     Arguments arguments{};
                     if (!detail::DecodeArguments(input, arguments, std::index_sequence<Index...>{})) return false;
                     if constexpr (std::is_void_v<Return>)
                     {
                         std::apply(
                             [&](auto&... argument) { std::invoke(method, *value, argument...); }, arguments);
                         output.type = ScriptValueType::None;
                         return true;
                     }
                     else
                     {
                         decltype(auto) result = std::apply(
                             [&](auto&... argument) { return std::invoke(method, *value, argument...); },
                             arguments);
                         return detail::EncodeScriptValue(output, result);
                     }
                 }});
        }

      public:
        ScriptApiBinder(ScriptApiRegistry& owner, ScriptApiRegistry::Component& definition)
            : api(owner), component(definition)
        {
        }

        template <class Value>
        void property(std::string name, Value T::* member)
        {
            addProperty<Value>(
                std::move(name),
                [member](const T& value) -> const Value& { return value.*member; },
                [member](T& value, const Value& replacement) { value.*member = replacement; },
                true);
        }

        template <class Value>
        void readonly(std::string name, Value T::* member)
        {
            addProperty<Value>(
                std::move(name),
                [member](const T& value) -> const Value& { return value.*member; },
                [](T&, const Value&) {},
                false);
        }

        template <class Getter, class Setter>
        void property(std::string name, Getter getter, Setter setter)
        {
            using Value = detail::Plain<std::invoke_result_t<Getter, const T&>>;
            addProperty<Value>(std::move(name), std::move(getter), std::move(setter), true);
        }

        template <class Getter>
        void readonly(std::string name, Getter getter)
        {
            using Value = detail::Plain<std::invoke_result_t<Getter, const T&>>;
            addProperty<Value>(std::move(name), std::move(getter), [](T&, const Value&) {}, false);
        }

        template <class Result, class Owner, class... Args>
        void method(
            std::string name,
            Result (Owner::*method)(Args...),
            std::initializer_list<std::string_view> parameterNames = {})
        {
            static_assert(std::same_as<Owner, T>);
            addMethod(std::move(name), method, parameterNames, std::index_sequence_for<Args...>{});
        }

        template <class Result, class Owner, class... Args>
        void method(
            std::string name,
            Result (Owner::*method)(Args...) const,
            std::initializer_list<std::string_view> parameterNames = {})
        {
            static_assert(std::same_as<Owner, T>);
            addMethod(std::move(name), method, parameterNames, std::index_sequence_for<Args...>{});
        }
    };

    template <class T>
    void ScriptApiRegistry::RegisterComponent(std::string managedNamespace, std::string managedName)
    {
        const std::string qualifiedName = managedNamespace + "." + managedName;
        components.push_back(
            {.id = MakeId(qualifiedName),
             .managedNamespace = std::move(managedNamespace),
             .managedName = std::move(managedName),
             .has = [](const entt::registry& source, const entt::entity entity) {
                 return source.valid(entity) && source.template all_of<T>(entity);
             }});
        ScriptApiBinder<T> binder{*this, components.back()};
        T::define_script_api(binder);
    }

    template <class T>
    void ScriptApiRegistry::RegisterSystem(std::string managedNamespace, std::string managedName)
    {
        const std::string qualifiedName = managedNamespace + "." + managedName;
        systems.push_back(
            {.id = MakeId(qualifiedName),
             .managedNamespace = std::move(managedNamespace),
             .managedName = std::move(managedName)});
        ScriptSystemApiBinder<T> binder{*this, systems.back()};
        T::define_script_api(binder);
    }
} // namespace sage

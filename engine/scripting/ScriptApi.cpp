#include "ScriptApi.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace sage
{
    namespace
    {
        std::string NativeTypeName(const ScriptValueType type)
        {
            switch (type)
            {
            case ScriptValueType::Boolean:
                return "Boolean";
            case ScriptValueType::Int32:
                return "Int32";
            case ScriptValueType::UInt32:
                return "UInt32";
            case ScriptValueType::Float:
                return "Float";
            case ScriptValueType::String:
                return "String";
            case ScriptValueType::Vector3:
                return "Vector3";
            case ScriptValueType::Entity:
                return "Entity";
            case ScriptValueType::None:
                return {};
            }
            return {};
        }

        bool IsEnumType(const std::string& managedType, const ScriptValueType type)
        {
            return type == ScriptValueType::Int32 && managedType != "int";
        }

        std::string ResultExpression(const ScriptValueType type, const std::string& managedType)
        {
            if (IsEnumType(managedType, type)) return "(" + managedType + ")result.Int32";
            if (type == ScriptValueType::String) return "result.String ?? string.Empty";
            return "result." + NativeTypeName(type);
        }

        std::string ArgumentExpression(
            const ScriptValueType type, const std::string& managedType, const std::string& name)
        {
            if (IsEnumType(managedType, type)) return "global::Sage.ScriptArgument.From((int)" + name + ")";
            return "global::Sage.ScriptArgument.From(" + name + ")";
        }

        void WriteProperty(std::ostringstream& output, const ScriptApiRegistry::Property& property)
        {
            const auto nativeType = NativeTypeName(property.type);
            output << "        private const ulong " << property.name << "Id = " << property.id << "UL;\n\n";
            if (IsEnumType(property.managedType, property.type))
            {
                output << "        public bool TryGet" << property.name << "(out " << property.managedType
                       << " value)\n"
                          "        {\n"
                          "            value = default;\n"
                          "            if (!global::Sage.NativeComponentApi.TryGetInt32(Entity.Id, ComponentId, "
                       << property.name
                       << "Id, out var raw)) return false;\n"
                          "            value = ("
                       << property.managedType
                       << ")raw;\n"
                          "            return true;\n"
                          "        }\n";
                if (property.writable)
                    output << "        public bool Set" << property.name << "(" << property.managedType
                           << " value) => global::Sage.NativeComponentApi.SetInt32(Entity.Id, ComponentId, "
                           << property.name << "Id, (int)value);\n";
            }
            else
            {
                output << "        public bool TryGet" << property.name << "(out " << property.managedType
                       << " value) => global::Sage.NativeComponentApi.TryGet" << nativeType
                       << "(Entity.Id, ComponentId, " << property.name << "Id, out value);\n";
                if (property.writable)
                    output << "        public bool Set" << property.name << "(" << property.managedType
                           << " value) => global::Sage.NativeComponentApi.Set" << nativeType
                           << "(Entity.Id, ComponentId, " << property.name << "Id, value);\n";
            }

            output << "        public " << property.managedType << " " << property.name
                   << "\n"
                      "        {\n"
                      "            get => TryGet"
                   << property.name << "(out var value) ? value : "
                   << (property.type == ScriptValueType::String ? "string.Empty" : "default") << ";\n";
            if (property.writable) output << "            set => Set" << property.name << "(value);\n";
            output << "        }\n\n";
        }

        void WriteMethod(std::ostringstream& output, const ScriptApiRegistry::Method& method)
        {
            output << "        private const ulong " << method.name << "_" << method.id << "Id = " << method.id
                   << "UL;\n";
            output << "        public "
                   << (method.returnType == ScriptValueType::None ? "bool" : method.managedReturnType) << " "
                   << method.name << "(";
            for (std::size_t index = 0; index < method.parameters.size(); ++index)
            {
                if (index != 0) output << ", ";
                output << method.parameters[index].managedType << " " << method.parameters[index].name;
            }
            output << ")\n        {\n            ";
            if (method.returnType == ScriptValueType::None)
                output << "return ";
            else
                output << "if (!";
            output << "global::Sage.NativeComponentApi.TryInvoke(Entity.Id, ComponentId, " << method.name << "_"
                   << method.id << "Id, [";
            for (std::size_t index = 0; index < method.parameters.size(); ++index)
            {
                if (index != 0) output << ", ";
                const auto& parameter = method.parameters[index];
                output << ArgumentExpression(parameter.type, parameter.managedType, parameter.name);
            }
            output << "], out var result)";
            if (method.returnType == ScriptValueType::None)
                output << ";\n";
            else
                output << ") return default;\n            return "
                       << ResultExpression(method.returnType, method.managedReturnType) << ";\n";
            output << "        }\n\n";
        }
    } // namespace

    ScriptApiRegistry::Id ScriptApiRegistry::MakeId(const std::string_view name)
    {
        constexpr Id offset = 14695981039346656037ULL;
        constexpr Id prime = 1099511628211ULL;
        Id result = offset;
        for (const unsigned char value : name)
        {
            result ^= value;
            result *= prime;
        }
        return result;
    }

    ScriptApiRegistry::Component* ScriptApiRegistry::findComponent(const Id componentId)
    {
        const auto found = std::ranges::find_if(
            components, [componentId](const Component& value) { return value.id == componentId; });
        return found != components.end() ? &*found : nullptr;
    }

    const ScriptApiRegistry::Component* ScriptApiRegistry::findComponent(const Id componentId) const
    {
        const auto found = std::ranges::find_if(
            components, [componentId](const Component& value) { return value.id == componentId; });
        return found != components.end() ? &*found : nullptr;
    }

    bool ScriptApiRegistry::HasComponent(
        const entt::registry& registry, const entt::entity entity, const Id componentId) const
    {
        const auto* component = findComponent(componentId);
        return component != nullptr && component->has(registry, entity);
    }

    bool ScriptApiRegistry::GetProperty(
        entt::registry& registry,
        const entt::entity entity,
        const Id componentId,
        const Id propertyId,
        ScriptValue& destination) const
    {
        const auto* component = findComponent(componentId);
        if (component == nullptr) return false;
        const auto property = std::ranges::find_if(
            component->properties, [propertyId](const Property& value) { return value.id == propertyId; });
        return property != component->properties.end() && property->get(registry, entity, destination);
    }

    bool ScriptApiRegistry::SetProperty(
        entt::registry& registry,
        const entt::entity entity,
        const Id componentId,
        const Id propertyId,
        const ScriptValue& value) const
    {
        const auto* component = findComponent(componentId);
        if (component == nullptr) return false;
        const auto property = std::ranges::find_if(
            component->properties, [propertyId](const Property& candidate) { return candidate.id == propertyId; });
        return property != component->properties.end() && property->writable &&
               property->set(registry, entity, value);
    }

    bool ScriptApiRegistry::Invoke(
        entt::registry& registry,
        const entt::entity entity,
        const Id componentId,
        const Id methodId,
        const std::span<const ScriptValue> arguments,
        ScriptValue& result) const
    {
        const auto* component = findComponent(componentId);
        if (component == nullptr) return false;
        const auto method = std::ranges::find_if(
            component->methods, [methodId](const Method& candidate) { return candidate.id == methodId; });
        return method != component->methods.end() && method->invoke(registry, entity, arguments, result);
    }

    bool ScriptApiRegistry::WriteCSharp(const std::filesystem::path& destination) const
    {
        std::ostringstream output;
        output << "// Generated from C++ define_script_api declarations. Do not edit.\n"
                  "#nullable enable\n\n";
        for (const auto& definition : enums)
        {
            output << "namespace " << definition.managedNamespace
                   << "\n{\n"
                      "    public enum "
                   << definition.managedName << "\n    {\n";
            for (const auto& [name, value] : definition.values)
                output << "        " << name << " = " << value << ",\n";
            output << "    }\n}\n\n";
        }

        for (const auto& component : components)
        {
            output << "namespace " << component.managedNamespace
                   << "\n{\n"
                      "    public readonly struct "
                   << component.managedName
                   << "(global::Sage.Entity entity)\n    {\n"
                      "        private const ulong ComponentId = "
                   << component.id
                   << "UL;\n"
                      "        public global::Sage.Entity Entity { get; } = entity;\n"
                      "        public bool Exists => global::Sage.NativeComponentApi.HasComponent(Entity.Id, "
                      "ComponentId);\n\n";
            for (const auto& property : component.properties)
                WriteProperty(output, property);
            for (const auto& method : component.methods)
                WriteMethod(output, method);
            output << "    }\n\n"
                      "    public static class "
                   << component.managedName
                   << "EntityExtensions\n    {\n"
                      "        public static bool TryGet"
                   << component.managedName << "(this global::Sage.Entity entity, out " << component.managedName
                   << " component)\n"
                      "        {\n"
                      "            component = new "
                   << component.managedName
                   << "(entity);\n"
                      "            return component.Exists;\n"
                      "        }\n"
                      "    }\n"
                      "}\n\n";
        }

        const auto contents = output.str();
        std::ifstream existing{destination, std::ios::binary};
        if (existing)
        {
            std::ostringstream current;
            current << existing.rdbuf();
            if (current.str() == contents) return true;
        }

        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) return false;
        std::ofstream file{destination, std::ios::binary | std::ios::trunc};
        if (!file) return false;
        file << contents;
        return static_cast<bool>(file);
    }
} // namespace sage

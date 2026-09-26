#pragma once
#include "engine/raylib-cereal.hpp"
#include "entt/entt.hpp"
#include "Json.hpp"
#include "magic_enum.hpp"
#include <cctype>
#include <functional>
#include <type_traits>

namespace sage::content
{
    // Component authors describe editable values once. Both this binder and the UI
    // invoke define_editor_options, including its setters and read-only declarations.
    class ContentInspector
    {
        std::string prefix;
        std::string explicitName;
        std::vector<entt::id_type> requiredComponents;
        std::vector<entt::id_type> incompatibleComponents;
        bool editableScope = true;
        std::string target;
        const json::Value* replacement = nullptr;
        bool matched = false;
        json::Document fields{rapidjson::kArrayType};
        static std::string Name(const std::string& label)
        {
            std::string result;
            for (const unsigned char c : label)
                if (std::isalnum(c))
                    result += static_cast<char>(std::tolower(c));
                else if (!result.empty() && result.back() != '_')
                    result += '_';
            while (!result.empty() && result.back() == '_')
                result.pop_back();
            return result;
        }
        std::string Path(const std::string& label) const
        {
            return prefix + (explicitName.empty() ? Name(label) : explicitName);
        }
        template <class T>
        void Leaf(
            const std::string& label, T& value, bool editable, const std::function<void(const T&)>& setter = {})
        {
            auto& allocator = fields.GetAllocator();
            const auto path = Path(label);
            json::Value entry(rapidjson::kObjectType);
            json::Put(entry, "name", path, allocator);
            json::Put(entry, "label", label, allocator);
            json::Put(entry, "editable", editable && editableScope, allocator);
            json::Put(entry, "value", json::Encode(value), allocator);
            const char* type = std::is_enum_v<T>                ? "enum"
                               : std::is_same_v<T, bool>        ? "boolean"
                               : std::is_integral_v<T>          ? "integer"
                               : std::is_floating_point_v<T>    ? "number"
                               : std::is_same_v<T, std::string> ? "string"
                                                                : "object";
            json::Put(entry, "type", type, allocator);
            if constexpr (std::is_enum_v<T>)
            {
                json::Value options(rapidjson::kArrayType);
                for (auto item : magic_enum::enum_values<T>())
                {
                    json::Value option(rapidjson::kObjectType);
                    json::Put(option, "name", std::string(magic_enum::enum_name(item)), allocator);
                    json::Put(option, "value", json::Encode(item), allocator);
                    options.PushBack(option, allocator);
                }
                json::Put(entry, "options", options, allocator);
            }
            fields.PushBack(entry, allocator);
            if (!replacement || path != target) return;
            if (!editable || !editableScope) throw std::runtime_error("Field is read-only: " + path);
            T next{};
            json::Decode(*replacement, next);
            if constexpr (std::is_enum_v<T>)
                if (!magic_enum::enum_contains(next)) throw std::runtime_error("Invalid enum value: " + path);
            if (setter)
                setter(next);
            else
                value = next;
            matched = true;
        }

      public:
        ContentInspector() = default;
        ContentInspector(std::string field, const json::Value& value)
            : target(std::move(field)), replacement(&value)
        {
        }
        template <class T, class... Options>
        void field(const std::string& name, const std::string& label, T& value, Options&&... options)
        {
            const auto previous = explicitName;
            explicitName = name;
            field(label, value, std::forward<Options>(options)...);
            explicitName = previous;
        }
        void boundedCollection(
            const std::string& name,
            std::string label,
            std::size_t count,
            std::size_t maximum,
            std::function<void()> add,
            std::function<void()> remove)
        {
            const auto previous = explicitName;
            explicitName = name;
            boundedCollection(std::move(label), count, maximum, std::move(add), std::move(remove));
            explicitName = previous;
        }
        template <class T>
        void scriptFile(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            scriptFile(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void vertexShaderFile(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            vertexShaderFile(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void fragmentShaderFile(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            fragmentShaderFile(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void textureDropdown(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            textureDropdown(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void particleTextureDropdown(
            const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            particleTextureDropdown(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void tagSet(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            tagSet(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void clipDropdown(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            clipDropdown(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void cursorDropdown(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            cursorDropdown(label, value, editable);
            explicitName = previous;
        }

        template <class T>
        void archetypeDropdown(const std::string& name, const std::string& label, T& value, bool editable = true)
        {
            const auto previous = explicitName;
            explicitName = name;
            archetypeDropdown(label, value, editable);
            explicitName = previous;
        }
        template <class T>
            requires(!std::is_array_v<T>)
        void field(const std::string& label, T& value, bool editable = true)
        {
            if constexpr (requires { value.define_editor_options(*this); })
            {
                auto oldPrefix = prefix;
                const bool oldScope = editableScope;
                prefix = Path(label) + ".";
                const auto oldName = explicitName;
                explicitName.clear();
                editableScope = editableScope && editable;
                value.define_editor_options(*this);
                prefix = oldPrefix;
                explicitName = oldName;
                editableScope = oldScope;
            }
            else
                Leaf(label, value, editable);
        }
        template <class T>
            requires(!std::is_array_v<T>)
        void field(const std::string& label, T& value, std::function<void(const T&)> setter)
        {
            Leaf(label, value, true, setter);
        }
        template <class T>
        void requiresComponent()
        {
            requiredComponents.push_back(entt::type_hash<T>::value());
        }
        template <class T>
        void incompatibleComponent()
        {
            incompatibleComponents.push_back(entt::type_hash<T>::value());
        }
        void note(const std::string&, const std::string&)
        {
        }
        void divider(const std::string&)
        {
        }
        void boundedCollection(
            const std::string& label,
            std::size_t count,
            std::size_t maximum,
            std::function<void()> add,
            std::function<void()> remove)
        {
            auto& allocator = fields.GetAllocator();
            json::Value entry(rapidjson::kObjectType);
            json::Put(entry, "name", Path(label), allocator);
            json::Put(entry, "type", "collection", allocator);
            json::Put(entry, "count", static_cast<std::uint64_t>(count), allocator);
            json::Put(entry, "maximum", static_cast<std::uint64_t>(maximum), allocator);
            fields.PushBack(entry, allocator);
            if (!replacement) return;
            if (target == Path(label) + ".add")
            {
                if (!editableScope || count >= maximum) throw std::runtime_error("Collection is full");
                add();
                matched = true;
            }
            if (target == Path(label) + ".remove")
            {
                if (!editableScope || count == 0) throw std::runtime_error("Collection is empty");
                remove();
                matched = true;
            }
        }
        void scriptFile(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void vertexShaderFile(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void fragmentShaderFile(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void textureDropdown(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void particleTextureDropdown(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void tagSet(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void clipDropdown(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }

        void cursorDropdown(const std::string& label, std::string& value, bool editable = true)
        {
            field(label, value, editable);
        }
        template <class T>
        void archetypeDropdown(const std::string& label, T& value, bool editable = true)
        {
            Leaf(label, value, editable);
        }
        const auto& Requirements() const
        {
            return requiredComponents;
        }
        const auto& Incompatible() const
        {
            return incompatibleComponents;
        }
        json::Document Take()
        {
            return std::move(fields);
        }
        void RequireMatch() const
        {
            if (!matched) throw std::runtime_error("Unknown editable field: " + target);
        }
    };
} // namespace sage::content

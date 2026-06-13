#pragma once

#include "entt/entt.hpp"
#include "magic_enum.hpp"
#include "raylib.h"

#include "engine/CollisionLayers.hpp"
#include "engine/components/sgTransform.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sage::editor
{
    using EditorComponentId = entt::id_type;

    template <class T>
    [[nodiscard]] constexpr EditorComponentId ComponentIdOf() noexcept
    {
        return entt::type_hash<std::remove_cvref_t<T>>::value();
    }

    struct ComponentRemovalState
    {
        bool allowed = false;
        std::string blockedReason;
    };

    template <class T>
    struct LeafField
    {
        T* data = nullptr;
        std::function<void(const T&)> setter;

        // Multi-selection vector aggregates only (Vector2/Vector3); unused otherwise.
        // mixedComponents is a bitmask where bit i is set when axis i differs across the
        // selection. componentSetter writes a single axis to every selected entity while
        // preserving each entity's other axes (so e.g. setting Y=0 doesn't clobber X/Z).
        std::function<void(std::size_t axis, float value)> componentSetter;
        unsigned int mixedComponents = 0;
    };

    // Holds the data + options + index getter/setter for a dropdown-rendered field.
    // Used by both std::is_enum_v<E> values (options derived from magic_enum) and the
    // bespoke CollisionLayer overload (options derived from GetCollisionLayers()).
    struct EnumField
    {
        void* data = nullptr;
        std::vector<std::string> options;
        std::function<std::size_t()> getIndex;
        std::function<void(std::size_t)> setIndex;
    };

    // A read-only informational row with no underlying data — for derived state
    // a component wants to surface (e.g. Collideable's "Collider: Box/Mesh").
    // Never editable, never participates in edit transactions.
    struct NoteField
    {
        std::string text;
    };

    // The variant alternative *is* the kind. Leaf overloads on ComponentInspector
    // construct one alternative each; the renderer dispatches via std::visit into
    // overloaded createFieldView/Update functions.
    using FieldValue = std::variant<
        LeafField<bool>,
        LeafField<int>,
        LeafField<unsigned int>,
        LeafField<std::uint64_t>,
        LeafField<float>,
        LeafField<std::string>,
        LeafField<Vector2>,
        LeafField<Vector3>,
        LeafField<::Color>,
        EnumField,
        NoteField>;

    struct InspectorField
    {
        std::string label;
        bool editable = true;
        bool mixed = false;
        FieldValue value;
    };

    struct ComponentDescription
    {
        std::vector<InspectorField> fields;
        std::vector<EditorComponentId> requirements;
    };

    namespace detail
    {
        template <class T>
        inline constexpr bool always_false_v = false;
    }

    // Component-side authors declare a templated `define_editor_options(Inspector&)` method
    // (or a free `define_editor_options(Inspector&, T&)` for foreign types). Inside it,
    // call `i.field(label, value)` for editable/readable fields and
    // `i.requiresComponent<T>()` for entity-level component requirements.
    // Dispatch mirrors cereal's archive overloads:
    //
    //   - Concrete overloads handle "leaf" types (primitives, raylib Vec2/3/Color, …) and
    //     record one InspectorField row per call.
    //   - The enum template handles any `std::is_enum_v<E>` as an Enum-kind row.
    //   - The composite template recurses into the value's own `define_editor_options()`
    //     (member or ADL free function), pushing a label prefix and propagating editability.
    //     Labels of sub-fields become "<parent> <child>" (e.g. "Local Bounds Min").
    //
    // Concrete overloads always win over the templates via overload resolution.
    class ComponentInspector
    {
        std::vector<InspectorField> fields_;
        std::vector<EditorComponentId> requirements_;
        std::string labelPrefix_;
        bool editableScope_ = true;
        // The entity being described; lets bespoke fields source options from
        // sibling components (e.g. clipDropdown reads the entity's Animation).
        entt::registry* contextRegistry_ = nullptr;
        entt::entity contextEntity_ = entt::null;

        [[nodiscard]] std::string qualified(const std::string& label) const
        {
            if (labelPrefix_.empty()) return label;
            if (label.empty()) return labelPrefix_;
            return labelPrefix_ + " " + label;
        }

        template <class T>
        void addLeaf(std::string label, T* data, const bool editable)
        {
            fields_.push_back(
                {.label = qualified(label), .editable = editable && editableScope_, .value = LeafField<T>{data}});
        }

        template <class T>
        void addLeaf(std::string label, T* data, std::function<void(const T&)> setter)
        {
            fields_.push_back(
                {.label = qualified(label),
                 .editable = editableScope_,
                 .value = LeafField<T>{.data = data, .setter = std::move(setter)}});
        }

      public:
        template <class T>
        void requiresComponent()
        {
            requirements_.push_back(ComponentIdOf<T>());
        }

        // --- Informational row ---------------------------------------------------------
        void note(const std::string& label, std::string text)
        {
            fields_.push_back(
                {.label = qualified(label), .editable = false, .value = NoteField{std::move(text)}});
        }

        // --- Leaf overloads ------------------------------------------------------------
        void field(std::string label, bool& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, bool& v, std::function<void(const bool&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, int& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, int& v, std::function<void(const int&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, unsigned int& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, unsigned int& v, std::function<void(const unsigned int&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, std::uint64_t& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, std::uint64_t& v, std::function<void(const std::uint64_t&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, float& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, float& v, std::function<void(const float&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, std::string& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, std::string& v, std::function<void(const std::string&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, Vector2& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, Vector2& v, std::function<void(const Vector2&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, Vector3& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, Vector3& v, std::function<void(const Vector3&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }
        void field(std::string label, ::Color& v, bool rw = true)
        {
            addLeaf(std::move(label), &v, rw);
        }
        void field(std::string label, ::Color& v, std::function<void(const ::Color&)> setter)
        {
            addLeaf(std::move(label), &v, std::move(setter));
        }

        // Bespoke: dropdown sourced from GetCollisionLayers(). Stored as EnumField.
        void field(const std::string& label, sage::CollisionLayer& v, bool rw = true);

        // Bespoke: dropdown of the project's scene tags (sage::CustomSceneTags),
        // sourced like the CollisionLayer field. `tags` is the MetaData::tags
        // string; picking an option replaces it. Stored as EnumField.
        void tagSet(const std::string& label, std::string& tags, bool rw = true);

        // Bespoke: dropdown of the inspected entity's Animation clip names (GLB
        // NLA tracks). Shows only "---" and writes nothing when the entity has no
        // Animation or clips. Stored as EnumField.
        void clipDropdown(const std::string& label, std::string& value, bool rw = true);

        // Bespoke: dropdown of cursor keys — the engine's own cursors plus the
        // project's (sage::CustomCursors) — for a CursorTarget::cursor key. Sourced
        // like tagSet; the current value stays selectable even if unlisted. Stored
        // as EnumField.
        void cursorDropdown(const std::string& label, std::string& value, bool rw = true);

        void SetContext(entt::registry* registry, const entt::entity entity)
        {
            contextRegistry_ = registry;
            contextEntity_ = entity;
        }

        // sgTransform proxy. Reads come from the cached Vector3 inside the proxy;
        // writes route through the proxy's operator=, which dispatches to TransformSystem
        // so the hierarchy stays in sync. The component author just passes the field,
        // no setter lambda required.
        template <auto Write>
        void field(std::string label, ::sage::sgTransform::VectorField<Write>& proxy, bool rw = true)
        {
            // `data` points at the cached Vector3 inside the proxy (used for display only).
            // The const_cast is safe because the setter is always provided for proxy fields;
            // commitField uses the setter, not the data pointer, for writes.
            auto* data = const_cast<Vector3*>(&proxy.Get());
            if (!rw || !editableScope_)
            {
                addLeaf(std::move(label), data, false);
                return;
            }
            addLeaf(
                std::move(label),
                data,
                std::function<void(const Vector3&)>([&proxy](const Vector3& v) { proxy = v; }));
        }

        // --- Enum template -------------------------------------------------------------
        template <class E>
            requires std::is_enum_v<E>
        void field(std::string label, E& v, bool rw = true)
        {
            EnumField e{.data = &v};
            constexpr auto entries = magic_enum::enum_entries<E>();
            e.options.reserve(entries.size());
            for (const auto& [val, name] : entries)
                e.options.emplace_back(name);
            e.getIndex = [p = &v]() -> std::size_t { return magic_enum::enum_index(*p).value_or(0); };
            e.setIndex = [p = &v](const std::size_t idx) {
                constexpr auto vals = magic_enum::enum_values<E>();
                if (idx < vals.size()) *p = vals[idx];
            };
            fields_.push_back(
                {.label = qualified(label), .editable = rw && editableScope_, .value = std::move(e)});
        }

        // --- Composite template --------------------------------------------------------
        template <class T>
        void field(std::string label, T& v, bool rw = true)
        {
            const auto savedPrefix = labelPrefix_;
            const bool savedScope = editableScope_;
            labelPrefix_ = qualified(label);
            editableScope_ = editableScope_ && rw;

            if constexpr (requires { v.define_editor_options(*this); })
                v.define_editor_options(*this);
            else if constexpr (requires { define_editor_options(*this, v); })
                define_editor_options(*this, v);
            else
                static_assert(
                    detail::always_false_v<T>,
                    "ComponentInspector::field: type has no leaf overload, member define_editor_options(), or ADL "
                    "define_editor_options()");

            labelPrefix_ = savedPrefix;
            editableScope_ = savedScope;
        }

        [[nodiscard]] ComponentDescription Take() &&
        {
            return ComponentDescription{
                .fields = std::move(fields_),
                .requirements = std::move(requirements_)};
        }
    };

    struct InspectedComponent
    {
        EditorComponentId componentId{};
        std::string displayName;
        std::vector<InspectorField> fields;
        // Offers "Remove Component" in the component's context menu. Removal can
        // still be disabled when another present component declares this component
        // as a requirement.
        bool removable = false;
        bool removeAllowed = false;
        std::string removeBlockedReason;
    };

    class InspectorRegistry
    {
        struct Entry
        {
            EditorComponentId componentId{};
            std::string displayName;
            std::function<bool(const entt::registry&, entt::entity)> has;
            std::function<ComponentDescription(entt::registry&, entt::entity)> describe;
            bool removable = false;
        };

        struct DescribedEntry
        {
            const Entry* entry = nullptr;
            ComponentDescription description;
        };

        std::vector<Entry> entries_;

        [[nodiscard]] const Entry* findEntry(EditorComponentId componentId) const;
        [[nodiscard]] std::vector<DescribedEntry> describeEntity(entt::registry& registry, entt::entity entity)
            const;
        [[nodiscard]] static DescribedEntry* findDescribed(
            std::vector<DescribedEntry>& described, const Entry& entry);
        [[nodiscard]] static const DescribedEntry* findDescribed(
            const std::vector<DescribedEntry>& described, const Entry& entry);
        [[nodiscard]] ComponentRemovalState canRemoveFromDescription(
            const Entry& target, const std::vector<DescribedEntry>& described, bool multiSelection) const;

      public:
        template <class T>
        void Register(std::string displayName, bool removable = false)
        {
            entries_.push_back(
                {ComponentIdOf<T>(),
                 std::move(displayName),
                 [](const entt::registry& r, const entt::entity e) {
                     return r.valid(e) && r.template any_of<T>(e);
                 },
                 [](entt::registry& r, const entt::entity e) {
                     ComponentInspector ci;
                     ci.SetContext(&r, e);
                     r.template get<T>(e).define_editor_options(ci);
                     return std::move(ci).Take();
                 },
                 removable});
        }

        [[nodiscard]] ComponentRemovalState CanRemove(
            entt::registry& registry, EditorComponentId componentId, const std::vector<entt::entity>& entities)
            const;
        [[nodiscard]] std::vector<InspectedComponent> Inspect(
            entt::registry& registry, entt::entity entity) const;
        [[nodiscard]] std::vector<InspectedComponent> Inspect(
            entt::registry& registry, const std::vector<entt::entity>& entities) const;
    };

    void RegisterDefaultInspectorComponents(InspectorRegistry& registry);
} // namespace sage::editor

// --- ADL-discoverable define_editor_options overloads for raylib types --------
// Pattern mirrors engine/raylib-cereal.hpp: free functions in global namespace
// so unqualified `define_editor_options(i, value)` finds them via ADL when the user
// composes a raylib type inside their component's define_editor_options().

template <class Inspector>
void define_editor_options(Inspector& i, BoundingBox& bb)
{
    i.field("Min", bb.min);
    i.field("Max", bb.max);
}

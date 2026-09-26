#pragma once

#include "cereal/archives/binary.hpp"
#include "content/ContentInspector.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sage
{
    namespace detail
    {
        template <class T>
        void EditComponentField(T& value, const std::string& field, const std::string& replacement)
        {
            const auto next = json::Parse(replacement);
            content::ContentInspector inspector(field, next);
            if constexpr (requires { value.define_editor_options(inspector); })
                value.define_editor_options(inspector);
            inspector.RequireMatch();
        }

        struct ComponentOperations
        {
            std::string key;
            entt::id_type typeId = 0;
            std::vector<entt::id_type> requirements;
            std::vector<entt::id_type> incompatible;
            std::function<std::string(const std::string&)> toJson;
            std::function<std::string(
                const entt::registry&, entt::entity, const std::unordered_map<std::uint32_t, entt::entity>&)>
                captureJson;
            std::function<void(entt::registry&, entt::entity, const std::string&)> restoreJson;
            std::function<bool(const std::string&, const std::unordered_map<std::uint32_t, entt::entity>&)>
                validateJson;
            std::function<std::string(const std::string&, const std::string&, const std::string&)> editJson;
            std::function<std::string(const std::string&)> schema;
            std::function<void(entt::registry&, entt::entity, const std::string&, const std::string&)> edit;
            std::function<void(entt::registry&, entt::entity)> remove;
            std::function<std::string(const std::string&, const std::unordered_map<std::uint32_t, entt::entity>&)>
                remapJson;
            std::function<bool(const entt::registry&, entt::entity)> has;
            std::function<void(entt::registry&, entt::entity, const std::string&)> deserialize;
            std::function<std::string(const std::string&)> migrate;
            std::function<void(
                entt::registry&, entt::entity, const std::unordered_map<std::uint32_t, entt::entity>&)>
                resolveReferences;
        };

        void RegisterComponentOperations(ComponentOperations operations);
        const std::vector<ComponentOperations>& RegisteredComponentOperations();
    } // namespace detail

    // Registers a game-owned component for opaque flatpack persistence without
    // introducing an engine-to-game dependency. Re-registering the same key
    // replaces its registered operations, so game/editor startup can safely call this repeatedly.
    template <class T>
    void RegisterFlatpackComponent(std::string key)
    {
        static_assert(std::is_default_constructible_v<T>);

        T defaultValue{};
        content::ContentInspector description;
        if constexpr (requires { defaultValue.define_editor_options(description); })
            defaultValue.define_editor_options(description);
        detail::RegisterComponentOperations(
            {.key = std::move(key),
             .typeId = entt::type_hash<T>::value(),
             .requirements = description.Requirements(),
             .incompatible = description.Incompatible(),
             .toJson =
                 [](const std::string& data) {
                     T value{};
                     std::istringstream stream(data, std::ios::binary);
                     cereal::BinaryInputArchive input(stream);
                     input(value);
                     return json::Stringify(json::Encode(value));
                 },
             .captureJson =
                 [](const entt::registry& registry,
                    entt::entity entity,
                    const std::unordered_map<std::uint32_t, entt::entity>& ids) {
                     if constexpr (requires(T& value) { value.ResolveEntityReferences(ids); })
                     {
                         auto value = registry.template get<T>(entity);
                         value.ResolveEntityReferences(ids);
                         return json::Stringify(json::Encode(value));
                     }
                     else
                         return json::Stringify(json::Encode(registry.template get<T>(entity)));
                 },
             .restoreJson =
                 [](entt::registry& registry, entt::entity entity, const std::string& data) {
                     if constexpr (std::is_move_constructible_v<T>)
                     {
                         T value{};
                         json::Decode(json::Parse(data), value);
                         registry.template emplace_or_replace<T>(entity, std::move(value));
                     }
                     else
                     {
                         // Event-owning components cannot move: keep their subscriptions in place.
                         json::Decode(json::Parse(data), registry.template get_or_emplace<T>(entity));
                     }
                 },
             .validateJson =
                 [](const std::string& data, const std::unordered_map<std::uint32_t, entt::entity>& ids) {
                     T value{};
                     json::Decode(json::Parse(data), value);
                     if constexpr (requires { value.ResolveEntityReferences(ids); })
                     {
                         const auto before = json::Stringify(json::Encode(value));
                         value.ResolveEntityReferences(ids);
                         return before == json::Stringify(json::Encode(value));
                     }
                     return true;
                 },
             .editJson =
                 [](const std::string& data, const std::string& field, const std::string& replacement) {
                     T value{};
                     json::Decode(json::Parse(data), value);
                     detail::EditComponentField(value, field, replacement);
                     return json::Stringify(json::Encode(value));
                 },
             .schema =
                 [](const std::string& data) {
                     T value{};
                     if (!data.empty()) json::Decode(json::Parse(data), value);
                     content::ContentInspector inspector;
                     if constexpr (requires { value.define_editor_options(inspector); })
                         value.define_editor_options(inspector);
                     return json::Stringify(inspector.Take());
                 },
             .edit =
                 [](entt::registry& registry,
                    entt::entity entity,
                    const std::string& field,
                    const std::string& replacement) {
                     detail::EditComponentField(registry.template get<T>(entity), field, replacement);
                 },
             .remove = [](entt::registry& registry, entt::entity entity) { registry.template remove<T>(entity); },
             .remapJson =
                 [](const std::string& data, const std::unordered_map<std::uint32_t, entt::entity>& ids) {
                     T value{};
                     json::Decode(json::Parse(data), value);
                     if constexpr (requires { value.ResolveEntityReferences(ids); })
                         value.ResolveEntityReferences(ids);
                     return json::Stringify(json::Encode(value));
                 },
             .has =
                 [](const entt::registry& registry, const entt::entity entity) {
                     return registry.valid(entity) && registry.template any_of<T>(entity);
                 },
             .deserialize =
                 [](entt::registry& registry, const entt::entity entity, const std::string& data) {
                     std::istringstream stream(data, std::ios::binary);
                     cereal::BinaryInputArchive archive(stream);
                     if constexpr (std::is_move_constructible_v<T>)
                     {
                         T component{};
                         archive(component);
                         registry.template emplace_or_replace<T>(entity, std::move(component));
                     }
                     else
                         archive(registry.template get_or_emplace<T>(entity));
                 },
             .migrate =
                 [](const std::string& data) {
                     std::istringstream inputStream(data, std::ios::binary);
                     cereal::BinaryInputArchive input(inputStream);
                     T component{};
                     input(component);

                     std::ostringstream outputStream(std::ios::binary);
                     cereal::BinaryOutputArchive output(outputStream);
                     output(component);
                     return outputStream.str();
                 },
             .resolveReferences =
                 [](entt::registry& registry,
                    entt::entity entity,
                    const std::unordered_map<std::uint32_t, entt::entity>& ids) {
                     if constexpr (requires(T& value) { value.ResolveEntityReferences(ids); })
                         if (auto* component = registry.template try_get<T>(entity))
                             component->ResolveEntityReferences(ids);
                 }});
    }

    // Map loading uses the same registered codecs as flatpack loading.
    bool RestoreRegisteredComponent(
        entt::registry& registry, entt::entity entity, const std::string& key, const std::string& data);
    void ResolveRegisteredComponentReferences(
        entt::registry& registry, entt::entity entity, const std::unordered_map<std::uint32_t, entt::entity>& ids);

    struct FlatpackCatalogEntry
    {
        std::string displayName;
        std::filesystem::path path;
    };

    struct FlatpackInstance
    {
        entt::entity root = entt::null;
        std::vector<entt::entity> entities;

        [[nodiscard]] explicit operator bool() const
        {
            return root != entt::null;
        }
    };

    [[nodiscard]] bool IsFlatpackFile(const char* path);

    struct FlatpackComponentMigrationResult
    {
        bool hasComponentSection = false;
        std::size_t recognizedComponents = 0;
        std::size_t changedComponents = 0;
        bool wroteChanges = false;
    };

    // Canonicalizes registered game-component payloads through their current
    // load/save functions. Current LQF5 flatpacks carry this opaque component
    // section; older container versions have no section and are left untouched.
    [[nodiscard]] std::optional<FlatpackComponentMigrationResult> MigrateFlatpackComponents(
        const std::filesystem::path& path, bool writeChanges);

    // Serializes the transform subtree rooted at `root`. The root is rebased to
    // the origin and every entity's optional Archetype is persisted by id.
    bool SaveFlatpack(entt::registry& source, entt::entity root, const char* path);

    // Creates a fresh instance in `destination`. This runtime API deliberately
    // adds no editor-only marker components; callers receive the full entity set
    // so they can add their own context-specific state.
    [[nodiscard]] FlatpackInstance LoadFlatpack(
        entt::registry& destination, const char* path, Vector3 anchorWorldPos);

    [[nodiscard]] std::vector<FlatpackCatalogEntry> ListFlatpacks(const std::filesystem::path& directory);

    // Load + universal fix-ups layered over LoadFlatpack: optionally re-orients the
    // root (Euler degrees; propagates to children synchronously), refits every
    // collider's world box to its final transform (LoadFlatpack saves boxes in the
    // root frame; mesh colliders re-derive their local box from the model), and —
    // unless disabled — attaches the lit uber-shader to each renderable (Skinned as
    // well when the entity is animated). This is the runtime counterpart to the
    // editor's place-flatpack path.
    //
    // Registry-only by design: unlike the editor path it does NOT register navigation
    // occupancy or rebind shader systems (both need live systems). A caller that needs
    // an instantiated NavigationObstacle to block pathfinding must mark it itself.
    [[nodiscard]] FlatpackInstance InstantiateFlatpack(
        entt::registry& destination,
        const char* path,
        Vector3 position,
        std::optional<Vector3> eulerRotation = std::nullopt,
        bool applyLitShader = true);

    // Convenience over the flatpack catalog: resolves `name` (a file stem) within
    // `directory` and instantiates it. Returns an empty instance if no match exists.
    [[nodiscard]] FlatpackInstance InstantiateFlatpackByName(
        entt::registry& destination,
        const std::string& name,
        Vector3 position,
        std::optional<Vector3> eulerRotation = std::nullopt,
        std::filesystem::path directory = "resources/flatpacks");
} // namespace sage

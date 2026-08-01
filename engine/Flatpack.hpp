#pragma once

#include "cereal/archives/binary.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sage
{
    namespace detail
    {
        struct FlatpackComponentCodec
        {
            std::string key;
            std::function<bool(const entt::registry&, entt::entity)> has;
            std::function<std::string(const entt::registry&, entt::entity)> serialize;
            std::function<void(entt::registry&, entt::entity, const std::string&)> deserialize;
        };

        void RegisterFlatpackComponentCodec(FlatpackComponentCodec codec);
    } // namespace detail

    // Registers a game-owned component for opaque flatpack persistence without
    // introducing an engine-to-game dependency. Re-registering the same key
    // replaces its codec, so game/editor startup can safely call this repeatedly.
    template <class T>
    void RegisterFlatpackComponent(std::string key)
    {
        static_assert(std::is_default_constructible_v<T>);

        detail::RegisterFlatpackComponentCodec(
            {.key = std::move(key),
             .has =
                 [](const entt::registry& registry, const entt::entity entity) {
                     return registry.valid(entity) && registry.template any_of<T>(entity);
                 },
             .serialize =
                 [](const entt::registry& registry, const entt::entity entity) {
                     std::ostringstream stream(std::ios::binary);
                     cereal::BinaryOutputArchive archive(stream);
                     archive(registry.template get<T>(entity));
                     return stream.str();
                 },
             .deserialize =
                 [](entt::registry& registry, const entt::entity entity, const std::string& data) {
                     std::istringstream stream(data, std::ios::binary);
                     cereal::BinaryInputArchive archive(stream);
                     T component{};
                     archive(component);
                     registry.template emplace_or_replace<T>(entity, std::move(component));
                 }});
    }

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

    // Serializes the transform subtree rooted at `root`. The root is rebased to
    // the origin and every entity's optional Archetype is persisted by id.
    bool SaveFlatpack(entt::registry& source, entt::entity root, const char* path);

    // Creates a fresh instance in `destination`. This runtime API deliberately
    // adds no editor-only marker components; callers receive the full entity set
    // so they can add their own context-specific state.
    [[nodiscard]] FlatpackInstance LoadFlatpack(
        entt::registry& destination, const char* path, Vector3 anchorWorldPos);

    [[nodiscard]] std::vector<FlatpackCatalogEntry> ListFlatpacks(
        const std::filesystem::path& directory);

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

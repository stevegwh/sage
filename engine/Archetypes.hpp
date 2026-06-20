#pragma once

#include "entt/core/hashed_string.hpp"
#include "entt/entity/fwd.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace sage
{
    class ScriptSystem;

    /*
        An archetype defines a 'noun', as opposed to a verb (component) or adjective (tag).
    */
    struct Archetype
    {
        std::string_view name{};
        std::uint32_t id{};

        constexpr Archetype() = default;
        constexpr Archetype(const std::string_view _name, const std::uint32_t _id) : name(_name), id(_id)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const
        {
            return id != 0;
        }

        friend constexpr bool operator==(const Archetype a, const Archetype b)
        {
            return a.id == b.id;
        }

        // The id is the identity. The name is metadata sourced from the compile-time
        // table at display time, not persisted on disk (mirrors CollisionLayer).
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(id);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.archetypeDropdown("Kind", *this);
        }

        static void define_lua_bindings(ScriptSystem& scripts);
    };

    [[nodiscard]] constexpr Archetype MakeArchetype(const std::string_view name)
    {
        return Archetype{name, entt::hashed_string::value(name.data(), name.size())};
    }

    struct ArchetypeIndex;

    void EnableArchetypeIndex(entt::registry& registry);
    void RebuildArchetypeIndex(entt::registry& registry);
    void SetArchetype(entt::registry& registry, entt::entity entity, Archetype archetype);

    [[nodiscard]] const std::vector<entt::entity>& FindAllWithArchetype(
        entt::registry& registry, Archetype archetype);
    [[nodiscard]] std::optional<entt::entity> FindFirstWithArchetype(
        entt::registry& registry, Archetype archetype);
} // namespace sage

#pragma once

#include "entt/core/hashed_string.hpp"

#include <cstdint>
#include <string_view>

namespace sage
{
    // An entity's "kind" — a named identity (Player, Goblin, Chest). Where a
    // component is a *verb* a system consumes (Moveable, Collideable), an Archetype
    // is a *noun*: a queryable discriminator answering "what is this?" — e.g. when a
    // collision is resolved and gameplay code must decide what it just hit. It is
    // deliberately decoupled from composition: holding an Archetype implies nothing
    // about which other components the entity has, so dispatch code still try_gets
    // the verb-components it needs rather than assuming them from the kind.
    //
    // Mirrors the CollisionLayer value-type pattern: the kind set is declared once,
    // project-side, in project/CustomArchetypes.hpp — the engine provides the
    // mechanism but never names a kind, preserving the engine -> project dependency
    // direction. The id (a hashed name) is the identity and the only field persisted;
    // the name is display metadata sourced from the compile-time table.
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
    };

    [[nodiscard]] constexpr Archetype MakeArchetype(const std::string_view name)
    {
        return Archetype{name, entt::hashed_string::value(name.data(), name.size())};
    }
} // namespace sage

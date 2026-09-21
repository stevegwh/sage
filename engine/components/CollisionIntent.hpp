#pragma once

#include "engine/CollisionLayers.hpp"
#include "engine/CursorTypes.hpp"

#include "cereal/types/string.hpp"

#include <cstdint>
#include <string>

namespace sage
{
    struct Collideable;
    struct NavigationObstacle;

    // Shared base for the collider "aspect" components below: behaviours layered
    // onto an entity's Collideable, each toggleable via `active` and driven by its
    // own system. Holds only the shared flag — every aspect adds its own fields.
    struct ColliderAspect
    {
        bool active = true;
    };

    enum class NavigationHeightSource
    {
        FlatTop,
        RenderMesh,
        TerrainHeightField,
        Ramp
    };

    struct NavigationSurface : ColliderAspect
    {
        NavigationHeightSource heightSource = NavigationHeightSource::FlatTop;

        template <class Archive>
        void save(Archive& archive) const
        {
            const auto sourceValue = static_cast<std::uint8_t>(heightSource);
            archive(active, sourceValue);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            std::uint8_t sourceValue = 0;
            archive(active, sourceValue);
            heightSource = static_cast<NavigationHeightSource>(sourceValue);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.template incompatibleComponent<NavigationObstacle>();
            i.field("Active", active);
            i.field("Height Source", heightSource);
        }
    };

    struct NavigationObstacle : ColliderAspect
    {
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(active);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.template incompatibleComponent<NavigationSurface>();
            i.field("Active", active);
        }
    };

    struct TriggerVolume : ColliderAspect
    {
        CollisionMask overlapMask{~0ull};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(active, overlapMask);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.field("Active", active);
            i.field("Overlap Mask", overlapMask.bits);
        }
    };

    struct CursorTarget
    {
        std::string cursor{cursors::Regular};
        bool hoverable = false;
        bool allowNavigationClickThrough = true;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cursor, hoverable, allowNavigationClickThrough);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.cursorDropdown("Cursor", cursor);
            i.field("Hoverable", hoverable);
            i.field("Allow Navigation Click Through", allowNavigationClickThrough);
        }
    };
} // namespace sage

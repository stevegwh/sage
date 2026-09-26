#pragma once

#include "cereal/cereal.hpp"

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
            archive(cereal::make_nvp("active", active), cereal::make_nvp("sourceValue", sourceValue));
        }

        template <class Archive>
        void load(Archive& archive)
        {
            std::uint8_t sourceValue = 0;
            archive(cereal::make_nvp("active", active), cereal::make_nvp("sourceValue", sourceValue));
            heightSource = static_cast<NavigationHeightSource>(sourceValue);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.template incompatibleComponent<NavigationObstacle>();
            i.field("active", "Active", active);
            i.field("height_source", "Height Source", heightSource);
        }
    };

    struct NavigationObstacle : ColliderAspect
    {
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("active", active));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.template incompatibleComponent<NavigationSurface>();
            i.field("active", "Active", active);
        }
    };

    struct TriggerVolume : ColliderAspect
    {
        CollisionMask overlapMask{~0ull};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("active", active), cereal::make_nvp("overlapMask", overlapMask));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.field("active", "Active", active);
            i.field("overlap_mask", "Overlap Mask", overlapMask.bits);
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
            archive(cereal::make_nvp("cursor", cursor), cereal::make_nvp("hoverable", hoverable), cereal::make_nvp("allowNavigationClickThrough", allowNavigationClickThrough));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.cursorDropdown("cursor", "Cursor", cursor);
            i.field("hoverable", "Hoverable", hoverable);
            i.field("allow_navigation_click_through", "Allow Navigation Click Through", allowNavigationClickThrough);
        }
    };
} // namespace sage

#pragma once

#include "engine/CollisionLayers.hpp"

#include "cereal/types/string.hpp"

#include <cstdint>
#include <string>

namespace sage
{
    struct Collideable;

    enum class NavigationHeightSource
    {
        FlatTop,
        RenderMesh,
        TerrainHeightField,
        Ramp
    };

    struct NavigationSurface
    {
        bool active = true;
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
            i.field("Active", active);
            i.field("Height Source", heightSource);
        }
    };

    struct NavigationObstacle
    {
        bool active = true;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(active);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.field("Active", active);
        }
    };

    struct TriggerVolume
    {
        bool active = true;
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
        std::string cursorTexture = "cursor_regular";
        bool hoverable = false;
        bool allowNavigationClickThrough = true;
        bool deniesNavigation = false;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cursorTexture, hoverable, allowNavigationClickThrough, deniesNavigation);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.field("Cursor Texture", cursorTexture);
            i.field("Hoverable", hoverable);
            i.field("Allow Navigation Click Through", allowNavigationClickThrough);
            i.field("Denies Navigation", deniesNavigation);
        }
    };
} // namespace sage

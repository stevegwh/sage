#pragma once

#include "engine/CollisionLayers.hpp"

#include <cstdint>

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

    // The cursor image shown while hovering an entity that carries a CursorTarget.
    // Each value maps to a ResourceManager texture key via CursorTypeTextureKey;
    // that function is the single source of truth for the actual asset names.
    enum class CursorType : std::uint8_t
    {
        Regular,
        Move,
        Talk,
        Attack,
        Pickup,
        Door,
        Interact,
        Lock,
        Denied
    };

    [[nodiscard]] constexpr const char* CursorTypeTextureKey(const CursorType type)
    {
        switch (type)
        {
        case CursorType::Move:
            return "cursor_move";
        case CursorType::Talk:
            return "cursor_talk";
        case CursorType::Attack:
            return "cursor_attack";
        case CursorType::Pickup:
            return "cursor_pickup";
        case CursorType::Door:
            return "cursor_door";
        case CursorType::Interact:
            return "cursor_interact";
        case CursorType::Lock:
            return "cursor_lock";
        case CursorType::Denied:
            return "cursor_denied";
        case CursorType::Regular:
            break;
        }
        return "cursor_regular";
    }

    struct CursorTarget
    {
        CursorType cursor = CursorType::Regular;
        bool hoverable = false;
        bool allowNavigationClickThrough = true;
        bool deniesNavigation = false;

        template <class Archive>
        void save(Archive& archive) const
        {
            const auto cursorValue = static_cast<std::uint8_t>(cursor);
            archive(cursorValue, hoverable, allowNavigationClickThrough, deniesNavigation);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            std::uint8_t cursorValue = 0;
            archive(cursorValue, hoverable, allowNavigationClickThrough, deniesNavigation);
            cursor = static_cast<CursorType>(cursorValue);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Collideable>();
            i.field("Cursor", cursor);
            i.field("Hoverable", hoverable);
            i.field("Allow Navigation Click Through", allowNavigationClickThrough);
            i.field("Denies Navigation", deniesNavigation);
        }
    };
} // namespace sage

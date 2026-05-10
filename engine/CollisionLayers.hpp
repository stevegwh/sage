#pragma once

#include <cstdint>
#include <functional>

namespace sage
{
    struct CollisionLayer
    {
        std::uint64_t bit{};

        constexpr explicit CollisionLayer(const std::uint64_t _bit = 0) : bit(_bit)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const
        {
            return bit != 0;
        }

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(bit);
        }

        friend constexpr bool operator==(CollisionLayer, CollisionLayer) = default;
    };

    [[nodiscard]] constexpr CollisionLayer MakeCollisionLayer(const std::uint8_t index)
    {
        return CollisionLayer{1ull << index};
    }

    struct CollisionMask
    {
        std::uint64_t bits{};

        constexpr explicit CollisionMask(const std::uint64_t _bits = 0) : bits(_bits)
        {
        }

        [[nodiscard]] constexpr bool Contains(const CollisionLayer layer) const
        {
            return (bits & layer.bit) != 0;
        }

        [[nodiscard]] constexpr bool IsEmpty() const
        {
            return bits == 0;
        }

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(bits);
        }

        friend constexpr bool operator==(CollisionMask, CollisionMask) = default;
    };

    [[nodiscard]] constexpr CollisionMask operator|(const CollisionLayer lhs, const CollisionLayer rhs)
    {
        return CollisionMask{lhs.bit | rhs.bit};
    }

    [[nodiscard]] constexpr CollisionMask operator|(const CollisionMask lhs, const CollisionLayer rhs)
    {
        return CollisionMask{lhs.bits | rhs.bit};
    }

    [[nodiscard]] constexpr CollisionMask operator|(const CollisionLayer lhs, const CollisionMask rhs)
    {
        return CollisionMask{lhs.bit | rhs.bits};
    }

    [[nodiscard]] constexpr CollisionMask operator|(const CollisionMask lhs, const CollisionMask rhs)
    {
        return CollisionMask{lhs.bits | rhs.bits};
    }

    namespace collision_layers
    {
        inline constexpr CollisionLayer Default = MakeCollisionLayer(0);
        inline constexpr CollisionLayer GeometrySimple = MakeCollisionLayer(1);
        inline constexpr CollisionLayer GeometryComplex = MakeCollisionLayer(2);
        inline constexpr CollisionLayer Background = MakeCollisionLayer(3);
        inline constexpr CollisionLayer Stairs = MakeCollisionLayer(4);
        inline constexpr CollisionLayer Obstacle = MakeCollisionLayer(5);
    } // namespace collision_layers

    namespace collision_masks
    {
        inline constexpr CollisionMask None{};
        inline constexpr CollisionMask Navigation =
            collision_layers::GeometrySimple | collision_layers::GeometryComplex | collision_layers::Stairs;
        inline constexpr CollisionMask DefaultQuery = Navigation | collision_layers::Obstacle;
    } // namespace collision_masks

    [[nodiscard]] constexpr bool IsNavigationLayer(const CollisionLayer layer)
    {
        return collision_masks::Navigation.Contains(layer);
    }

    [[nodiscard]] constexpr bool RequiresMeshCollision(const CollisionLayer layer)
    {
        return layer == collision_layers::GeometryComplex || layer == collision_layers::Stairs;
    }

    [[nodiscard]] constexpr CollisionMask GetDefaultCollisionMask(const CollisionLayer layer)
    {
        if (layer == collision_layers::Default) return collision_masks::DefaultQuery;
        return collision_masks::None;
    }
} // namespace sage

template <>
struct std::hash<sage::CollisionLayer>
{
    std::size_t operator()(const sage::CollisionLayer layer) const noexcept
    {
        return std::hash<std::uint64_t>{}(layer.bit);
    }
};

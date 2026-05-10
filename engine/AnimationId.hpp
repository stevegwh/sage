#pragma once

#include <cstdint>
#include <functional>

namespace sage
{
    struct AnimationId
    {
        std::uint64_t bit{};

        constexpr explicit AnimationId(const std::uint64_t _bit = 0) : bit(_bit)
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

        friend constexpr bool operator==(AnimationId, AnimationId) = default;
    };

    [[nodiscard]] constexpr AnimationId MakeAnimationId(const std::uint8_t index)
    {
        return AnimationId{1ull << index};
    }
} // namespace sage

template <>
struct std::hash<sage::AnimationId>
{
    std::size_t operator()(const sage::AnimationId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.bit);
    }
};

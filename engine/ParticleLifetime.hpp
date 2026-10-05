#pragma once

#include "cereal/cereal.hpp"
#include "cereal/types/array.hpp"
#include "engine/raylib-cereal.hpp"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sage
{
    inline constexpr std::size_t PARTICLE_LIFETIME_SAMPLE_COUNT = 5;
    inline constexpr Color PARTICLE_OPAQUE_WHITE{.r = 255, .g = 255, .b = 255, .a = 255};

    // Five evenly spaced samples over normalized particle age, interpolated linearly.
    struct ParticleCurve
    {
        bool enabled = false;
        std::array<float, PARTICLE_LIFETIME_SAMPLE_COUNT> values{1, 1, 1, 1, 1};

        [[nodiscard]] float Evaluate(float age) const
        {
            if (!enabled) return 1.0f;
            const float position =
                std::clamp(age, 0.0f, 1.0f) * static_cast<float>(PARTICLE_LIFETIME_SAMPLE_COUNT - 1);
            const auto index = std::min(static_cast<std::size_t>(position), values.size() - 2);
            return std::max(
                0.0f, std::lerp(values.at(index), values.at(index + 1), position - static_cast<float>(index)));
        }
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("enabled", enabled), cereal::make_nvp("values", values));
        }
    };

    struct ParticleGradient
    {
        bool enabled = false;
        std::array<Color, PARTICLE_LIFETIME_SAMPLE_COUNT> colors = [] {
            std::array<Color, PARTICLE_LIFETIME_SAMPLE_COUNT> result{};
            result.fill(PARTICLE_OPAQUE_WHITE);
            for (std::size_t index = 0; index < result.size(); ++index)
                result.at(index).a = static_cast<unsigned char>(
                    static_cast<float>(PARTICLE_OPAQUE_WHITE.a) *
                    (1.0f - static_cast<float>(index) / static_cast<float>(result.size() - 1)));
            return result;
        }();

        [[nodiscard]] Color Evaluate(float age, Color start, Color end) const
        {
            float fraction = std::clamp(age, 0.0f, 1.0f);
            if (enabled)
            {
                const float position = fraction * static_cast<float>(PARTICLE_LIFETIME_SAMPLE_COUNT - 1);
                const auto index = std::min(static_cast<std::size_t>(position), colors.size() - 2);
                start = colors.at(index);
                end = colors.at(index + 1);
                fraction = position - static_cast<float>(index);
            }
            const auto channel = [fraction](unsigned char a, unsigned char b) {
                return static_cast<unsigned char>(
                    std::lerp(static_cast<float>(a), static_cast<float>(b), fraction));
            };
            return {
                .r = channel(start.r, end.r),
                .g = channel(start.g, end.g),
                .b = channel(start.b, end.b),
                .a = channel(start.a, end.a)};
        }
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("enabled", enabled), cereal::make_nvp("colors", colors));
        }
    };
} // namespace sage

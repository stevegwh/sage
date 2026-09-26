#pragma once

#include <numbers>

namespace sage::math
{
    // PI is already a macro in raylib headers.
    inline constexpr float MATH_PI = std::numbers::pi_v<float>;
    inline constexpr float DEGREES_TO_RADIANS = MATH_PI / 180.0f;
    inline constexpr float RADIANS_TO_DEGREES = 180.0f / MATH_PI;
} // namespace sage::math

#pragma once

#include "raylib.h"

namespace sage::colors
{
    // The suffix avoids collisions with raylib's color macros.
    inline constexpr Color LIGHT_GRAY_COLOR = {.r = 200, .g = 200, .b = 200, .a = 255};
    inline constexpr Color GRAY_COLOR = {.r = 130, .g = 130, .b = 130, .a = 255};
    inline constexpr Color DARK_GRAY_COLOR = {.r = 80, .g = 80, .b = 80, .a = 255};
    inline constexpr Color YELLOW_COLOR = {.r = 253, .g = 249, .b = 0, .a = 255};
    inline constexpr Color GOLD_COLOR = {.r = 255, .g = 203, .b = 0, .a = 255};
    inline constexpr Color ORANGE_COLOR = {.r = 255, .g = 161, .b = 0, .a = 255};
    inline constexpr Color PINK_COLOR = {.r = 255, .g = 109, .b = 194, .a = 255};
    inline constexpr Color RED_COLOR = {.r = 230, .g = 41, .b = 55, .a = 255};
    inline constexpr Color MAROON_COLOR = {.r = 190, .g = 33, .b = 55, .a = 255};
    inline constexpr Color GREEN_COLOR = {.r = 0, .g = 228, .b = 48, .a = 255};
    inline constexpr Color LIME_COLOR = {.r = 0, .g = 158, .b = 47, .a = 255};
    inline constexpr Color DARK_GREEN_COLOR = {.r = 0, .g = 117, .b = 44, .a = 255};
    inline constexpr Color SKY_BLUE_COLOR = {.r = 102, .g = 191, .b = 255, .a = 255};
    inline constexpr Color BLUE_COLOR = {.r = 0, .g = 121, .b = 241, .a = 255};
    inline constexpr Color DARK_BLUE_COLOR = {.r = 0, .g = 82, .b = 172, .a = 255};
    inline constexpr Color PURPLE_COLOR = {.r = 200, .g = 122, .b = 255, .a = 255};
    inline constexpr Color VIOLET_COLOR = {.r = 135, .g = 60, .b = 190, .a = 255};
    inline constexpr Color DARK_PURPLE_COLOR = {.r = 112, .g = 31, .b = 126, .a = 255};
    inline constexpr Color BEIGE_COLOR = {.r = 211, .g = 176, .b = 131, .a = 255};
    inline constexpr Color BROWN_COLOR = {.r = 127, .g = 106, .b = 79, .a = 255};
    inline constexpr Color DARK_BROWN_COLOR = {.r = 76, .g = 63, .b = 47, .a = 255};
    inline constexpr Color WHITE_COLOR = {.r = 255, .g = 255, .b = 255, .a = 255};
    inline constexpr Color BLACK_COLOR = {.r = 0, .g = 0, .b = 0, .a = 255};
    inline constexpr Color BLANK_COLOR = {.r = 0, .g = 0, .b = 0, .a = 0};
    inline constexpr Color MAGENTA_COLOR = {.r = 255, .g = 0, .b = 255, .a = 255};
    inline constexpr Color RAY_WHITE_COLOR = {.r = 245, .g = 245, .b = 245, .a = 255};
} // namespace sage::colors

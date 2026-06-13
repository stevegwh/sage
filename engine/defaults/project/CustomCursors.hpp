#pragma once

// Default empty stub used when the engine is built standalone (no consuming
// project provides project/CustomCursors.hpp). The consuming project overrides
// this by setting SAGE_PROJECT_INCLUDE_DIR in its CMake.

#include "engine/CursorTypes.hpp"

#include <array>

namespace sage
{
    inline constexpr std::array<CursorKey, 0> CustomCursors{};
} // namespace sage

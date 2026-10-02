#pragma once

#include "raylib.h"

#include <filesystem>

namespace sage
{
    // Builds a transient preview registry from the flatpack and renders all of
    // its visible meshes as one thumbnail. The caller owns the returned texture.
    [[nodiscard]] RenderTexture2D CreateFlatpackThumbnail(
        const std::filesystem::path& path,
        int size = 128,
        Color background = Color{.r = 244, .g = 247, .b = 251, .a = 255});
} // namespace sage

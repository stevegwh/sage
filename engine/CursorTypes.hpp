#pragma once

#include <string_view>

namespace sage
{
    // An opaque cursor identity. The value is a ResourceManager texture key; the
    // engine resolves a CursorTarget's key straight to a texture and never
    // enumerates the project's cursor vocabulary. Adding game cursors
    // (Talk/Attack/…) is therefore a project concern (project/CustomCursors.hpp),
    // preserving the engine→project dependency direction. Mirrors the split in
    // CollisionLayers.hpp / SceneTags.hpp.
    using CursorKey = std::string_view;

    // The only cursors the engine's own cursor/navigation logic selects itself.
    // Everything else lives in project/CustomCursors.hpp.
    namespace cursors
    {
        inline constexpr CursorKey Regular = "cursor_regular";
        inline constexpr CursorKey Move = "cursor_move";
        inline constexpr CursorKey Denied = "cursor_denied";
    } // namespace cursors
} // namespace sage

#pragma once

#include "CanvasDocument.hpp"
#include <map>

namespace sage
{
    struct CanvasLayout
    {
        std::map<unsigned int, Rectangle> bounds;
        std::vector<unsigned int> cells; // Back-to-front paint order.
        [[nodiscard]] unsigned int Hit(Vector2 point) const;
    };

    // Uses the same retained table layout and drawing code as existing game UI.
    CanvasLayout RenderCanvas(
        const CanvasDocument& document,
        Rectangle viewport,
        unsigned int hovered = 0,
        unsigned int pressed = 0,
        bool draw = true);

} // namespace sage

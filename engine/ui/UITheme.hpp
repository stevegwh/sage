//
// UITheme: the engine-wide defaults the fluent UIBuilder pulls from so call
// sites don't repeat fonts, paddings and chrome textures on every element.
// Held by GameUIEngine (see GameUIEngine::Theme()). Texture fields default to
// empty — a consumer (e.g. the game) loads its frame/close textures into the
// theme once; an unset frameTexture simply yields an unframed window.
//

#pragma once

#include "UIBase.hpp"
#include "UIElements.hpp"

#include "raylib.h"

namespace sage
{
    struct UITheme
    {
        // Default font for Labels, Buttons, TitleBars and Dropdowns.
        TextBox::FontInfo font{};

        // Window chrome.
        Texture frameTexture{};
        TextureStretchMode frameStretchMode = TextureStretchMode::STRETCH;
        Texture closeButtonTexture{};
        Padding windowPadding{20, 0, 14, 14}; // inside the window frame
        Padding bodyPadding{20, 0, 0, 0};     // body row, below the title bar
        float titleBarHeightPercent = 4.0f;

        // Auto-layout spacing applied to builder-created rows/cells.
        Padding itemSpacing{0, 8, 0, 0};       // gap below each stacked item
        Padding itemCellPadding{0, 0, 24, 24}; // left/right breathing room per item
    };
} // namespace sage

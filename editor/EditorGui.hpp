#pragma once

#include <string>

namespace sage
{
    class GameUIEngine;
    class TextBox;
    class Window;
    struct Settings;

    namespace editor
    {
        class EditorGui
        {
            Window* overlayWindow{};
            TextBox* selectedAssetText{};
            TextBox* gridText{};
            TextBox* lastPlacedText{};

          public:
            void SetPlacementStatus(
                const std::string& selectedAsset,
                const std::string& hoveredGrid,
                const std::string& lastPlaced) const;
            EditorGui(GameUIEngine* ui, Settings* settings);
        };
    } // namespace editor
} // namespace sage

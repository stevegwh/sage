#pragma once

#include "UI.hpp"

#include "raylib.h"

#include <memory>
#include <vector>

namespace sage
{
    class Cursor;
    struct Settings;

    class GameUIEngine
    {
      public:
        GameUIEngine(Settings* settings, Cursor* cursor);

        [[nodiscard]] UITheme& Theme();
        Window& AddWindow(Rectangle designBounds);
        void Update(bool inputEnabled = true);
        void Draw2D() const;
        void DrawDebug2D() const;

      private:
        struct Hit
        {
            std::optional<std::reference_wrapper<Window>> window;
            std::optional<std::reference_wrapper<Cell>> cell;
        };

        Settings* settings;
        Cursor* cursor;
        UITheme theme;
        std::vector<std::unique_ptr<Window>> windows;

        Hit hovered;
        Hit pressed;
        Vector2 pressPosition{};
        Vector2 pressedWindowPosition{};
        bool draggingWindow = false;

        void bringToFront(Window& window);
        [[nodiscard]] Hit hitTest(Vector2 point) const;
    };
} // namespace sage

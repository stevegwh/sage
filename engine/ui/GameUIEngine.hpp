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
        void Update();
        void Draw2D() const;
        void DrawDebug2D() const;

      private:
        struct Hit
        {
            Window* window = nullptr;
            Cell* cell = nullptr;
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

        void bringToFront(Window* window);
        [[nodiscard]] Hit hitTest(Vector2 point) const;
    };
} // namespace sage

//
// UIBuilder: a thin, chained-fluent facade over the retained layout tree
// (Window -> Table -> TableRow -> TableCell -> element). It auto-creates the
// row/cell scaffolding and pulls styling from the engine's UITheme, so a window
// reads as a declaration instead of hand-threaded containers:
//
//   auto* window = engine->Build()
//       .Window("Game Menu", {.width = 20_pct, .height = 33_pct})
//       .Column()
//           .Button("Resume",    onResume)
//           .Button("Save Game", onSave)
//       .End()
//       .Finalize();
//
// It is sugar, not a new layout engine: every method maps onto the existing
// Create* calls, and the explicit API remains available for cases the builder
// doesn't cover. Build once and keep the returned Window* — this is still
// retained mode, not immediate mode.
//

#pragma once

#include "UIBase.hpp"
#include "UITheme.hpp"

#include "raylib.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sage
{
    class GameUIEngine;
    class Window;
    class Table;
    class TableRow;
    class TableCell;

    struct WindowOptions
    {
        Percent width{30};
        Percent height{40};
        bool framed = true;      // draw the theme's frame texture
        bool titleBar = true;    // draggable title bar with the given title
        bool closeButton = true; // close button in the title bar
    };

    struct FramedWindow
    {
        Window* window = nullptr;
        TableCell* body = nullptr; // the empty cell below the chrome; fill this
    };

    // Builds a centred window with the theme's chrome and returns the body cell
    // to populate. Used by UIBuilder::Window, but also callable directly when
    // you want to drive the layout with the explicit Create* API.
    FramedWindow CreateFramedWindow(GameUIEngine* engine, const std::string& title, WindowOptions opts = {});

    class UIBuilder
    {
      public:
        explicit UIBuilder(GameUIEngine* _engine);

        // Opens a framed window and positions the cursor in its body.
        UIBuilder& Window(const std::string& title, WindowOptions opts = {});

        // Containers. Column stacks children vertically, Row lays them out
        // horizontally, Grid creates a rows x cols cell grid filled in order.
        // Each must be closed with a matching End().
        UIBuilder& Column();
        UIBuilder& Row();
        UIBuilder& Grid(int rows, int cols, float cellSpacing = 0);

        // Leaf widgets. A leaf emitted directly in the window body implicitly
        // opens a Column first (vertical stacking is the default, ImGui-style).
        UIBuilder& Label(const std::string& text);
        UIBuilder& Button(const std::string& label, std::function<void()> onPress = {});
        UIBuilder& Checkbox(bool checked, std::function<void(bool)> onChange = {});
        UIBuilder& Dropdown(
            std::vector<std::string> options,
            std::size_t selected = 0,
            std::function<void(std::size_t, const std::string&)> onChange = {});
        UIBuilder& Image(const Texture& tex);

        // Closes the innermost open container.
        UIBuilder& End();

        // Closes any remaining containers, finalizes the layout and returns the
        // window. Keep the pointer — Show/Hide/remove go through it.
        // (sage:: qualified: the Window() method above shadows the type name.)
        sage::Window* Finalize();

      private:
        enum class Kind
        {
            Body,
            Column,
            Row,
            Grid
        };

        struct Frame
        {
            Kind kind;
            TableCell* bodyCell = nullptr; // Body
            Table* table = nullptr;        // Column / Row / Grid container
            TableRow* row = nullptr;       // Row: the shared row
            int cols = 0;                  // Grid: columns per row
            int count = 0;                 // Grid: cells filled so far
        };

        GameUIEngine* engine;
        sage::Window* window = nullptr;
        std::vector<Frame> stack;

        Frame& top();
        TableCell* nextCell(); // the cell the next leaf/container attaches to
        void ensureContainer();
    };
} // namespace sage

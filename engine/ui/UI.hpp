#pragma once

#include "engine/Colors.hpp"
#include "raylib.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sage
{
    struct Settings;

    struct Padding
    {
        float top = 0;
        float bottom = 0;
        float left = 0;
        float right = 0;
    };

    struct Percent
    {
        float value = 0;
    };

    inline namespace literals
    {
        constexpr Percent operator""_pct(unsigned long long value)
        {
            return Percent{static_cast<float>(value)};
        }

        constexpr Percent operator""_pct(long double value)
        {
            return Percent{static_cast<float>(value)};
        }
    } // namespace literals

    using Size = std::optional<Percent>;

    enum class WindowHorizontalAlignment
    {
        LEFT,
        CENTER,
        RIGHT,
        FREE
    };

    enum class WindowVerticalAlignment
    {
        TOP,
        MIDDLE,
        BOTTOM,
        FREE
    };

    enum class HorizontalAlignment
    {
        LEFT,
        CENTER,
        RIGHT
    };

    enum class VerticalAlignment
    {
        TOP,
        MIDDLE,
        BOTTOM
    };

    struct CellStyle
    {
        Texture backgroundTexture{};
        Rectangle backgroundSource{};
        Color background = sage::colors::BLANK_COLOR;
        Color hoveredBackground = sage::colors::BLANK_COLOR;
        Color pressedBackground = sage::colors::BLANK_COLOR;
        Color border = sage::colors::BLANK_COLOR;
        float borderWidth = 0;
        Font font{};
        float fontSize = 16;
        float fontSpacing = 1;
        Color textColor = sage::colors::BLACK_COLOR;
        HorizontalAlignment horizontalAlignment = HorizontalAlignment::LEFT;
        VerticalAlignment verticalAlignment = VerticalAlignment::MIDDLE;
    };

    struct WindowStyle
    {
        Color background = sage::colors::BLANK_COLOR;
        Texture backgroundTexture{};
        Rectangle backgroundSource{};
        Padding padding{};
    };

    struct UITheme
    {
        WindowStyle window;
        CellStyle button;
        CellStyle title;
        Texture closeButtonTexture{};

        UITheme();
    };

    class Table;
    struct CellImage
    {
        Texture texture{};
        bool flipVertically = false;
    };
    using CellContent = std::variant<std::monostate, std::string, CellImage, std::unique_ptr<Table>>;

    class Cell
    {
      public:
        Size width;
        Padding padding;
        Rectangle bounds{};
        CellStyle style;
        CellContent content;
        std::function<void()> click;
        bool dragsWindow = false;

        explicit Cell(Size requestedWidth = {});
        Cell(Cell&&) = delete;
        Cell& operator=(Cell&&) = delete;
        ~Cell();
        Cell(const Cell&) = delete;
        Cell& operator=(const Cell&) = delete;

        Cell& Text(std::string value);
        Cell& Image(Texture texture, bool flipVertically = false);
        Table& AddTable();
        Cell& SetPadding(Padding value);
        Cell& UseStyle(const CellStyle& value);
        Cell& OnClick(std::function<void()> action);
        Cell& DragsWindow();
    };

    class Row
    {
      public:
        Size height;
        Rectangle bounds{};
        std::vector<std::unique_ptr<Cell>> cells;

        explicit Row(Size requestedHeight = {});

        Cell& AddCell(Size width = {});
    };

    class Table
    {
      public:
        float gap = 0;
        Rectangle bounds{};
        std::vector<std::unique_ptr<Row>> rows;

        Row& AddRow(Size height = {});
        Table& SetGap(float value);
    };

    class Window
    {
      public:
        explicit Window(Rectangle designBounds, WindowStyle style = {});

        void LayoutAt(Rectangle bounds, float scale);
        void DrawAt(
            std::optional<std::reference_wrapper<const Cell>> hovered,
            std::optional<std::reference_wrapper<const Cell>> pressed,
            float scale) const;
        Table& RootTable();
        Window& OnShow(std::function<void()> action);
        Window& OnHide(std::function<void()> action);
        void Show();
        void Hide();

      private:
        Rectangle designBounds{};
        Rectangle bounds{};
        WindowStyle style;
        Table root;
        std::function<void()> onShow;
        std::function<void()> onHide;
        bool hidden = false;

        void Layout(const Settings& settings);
        void MoveTo(Vector2 designPosition);
        void ClampToDesignViewport();
        [[nodiscard]] std::optional<std::reference_wrapper<Cell>> HitTest(Vector2 point);
        void DrawDebug() const;

        friend class GameUIEngine;
    };
} // namespace sage

#include "UI.hpp"
#include "engine/Colors.hpp"

#include "../Settings.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>
#include <vector>

namespace sage
{
    namespace
    {
        class ScissorScope
        {
          public:
            explicit ScissorScope(const Rectangle rectangle)
            {
                Rectangle effective = rectangle;
                if (!stack().empty()) effective = intersection(stack().back(), rectangle);
                stack().push_back(effective);
                BeginScissorMode(
                    static_cast<int>(effective.x),
                    static_cast<int>(effective.y),
                    static_cast<int>(std::max(0.0f, effective.width)),
                    static_cast<int>(std::max(0.0f, effective.height)));
            }
            ScissorScope(const ScissorScope&) = delete;
            ScissorScope& operator=(const ScissorScope&) = delete;
            ScissorScope(ScissorScope&&) = delete;
            ScissorScope& operator=(ScissorScope&&) = delete;

            ~ScissorScope()
            {
                stack().pop_back();
                if (stack().empty())
                {
                    EndScissorMode();
                    return;
                }

                const Rectangle previous = stack().back();
                BeginScissorMode(
                    static_cast<int>(previous.x),
                    static_cast<int>(previous.y),
                    static_cast<int>(std::max(0.0f, previous.width)),
                    static_cast<int>(std::max(0.0f, previous.height)));
            }

          private:
            static std::vector<Rectangle>& stack()
            {
                static std::vector<Rectangle> rectangles;
                return rectangles;
            }

            static Rectangle intersection(const Rectangle first, const Rectangle second)
            {
                const float left = std::max(first.x, second.x);
                const float top = std::max(first.y, second.y);
                const float right = std::min(first.x + first.width, second.x + second.width);
                const float bottom = std::min(first.y + first.height, second.y + second.height);
                return {
                    .x = left,
                    .y = top,
                    .width = std::max(0.0f, right - left),
                    .height = std::max(0.0f, bottom - top)};
            }
        };

        Padding scaled(const Padding padding, const float scale)
        {
            return {
                .top = padding.top * scale,
                .bottom = padding.bottom * scale,
                .left = padding.left * scale,
                .right = padding.right * scale};
        }

        Rectangle inset(const Rectangle rectangle, const Padding padding)
        {
            return {
                .x = rectangle.x + padding.left,
                .y = rectangle.y + padding.top,
                .width = std::max(0.0f, rectangle.width - padding.left - padding.right),
                .height = std::max(0.0f, rectangle.height - padding.top - padding.bottom)};
        }

        template <typename Item, typename SizeOf>
        std::vector<float> distribute(
            const std::vector<std::unique_ptr<Item>>& items, const float available, SizeOf sizeOf)
        {
            float requestedPercent = 0;
            std::size_t fillCount = 0;
            for (const auto& item : items)
            {
                const Size size = sizeOf(*item);
                if (size)
                    requestedPercent += std::clamp(size->value, 0.0f, 100.0f);
                else
                    ++fillCount;
            }

            requestedPercent = std::min(requestedPercent, 100.0f);
            const float remaining = available * (1.0f - requestedPercent / 100.0f);

            std::vector<float> result;
            result.reserve(items.size());
            for (const auto& item : items)
            {
                const Size size = sizeOf(*item);
                result.push_back(
                    size            ? available * (std::clamp(size->value, 0.0f, 100.0f) / 100.0f)
                    : fillCount > 0 ? remaining / static_cast<float>(fillCount)
                                    : 0.0f);
            }
            return result;
        }

        void layoutTable(Table& table, const Rectangle bounds, const float scale);

        void layoutCell(Cell& cell, const Rectangle bounds, const float scale)
        {
            cell.bounds = bounds;
            if (auto* table = std::get_if<std::unique_ptr<Table>>(&cell.content))
            {
                layoutTable(**table, inset(bounds, scaled(cell.padding, scale)), scale);
            }
        }

        void layoutRow(Row& row, const Rectangle bounds, const float gap, const float scale)
        {
            row.bounds = bounds;
            const float gaps = gap * static_cast<float>(row.cells.empty() ? 0 : row.cells.size() - 1);
            const float availableWidth = std::max(0.0f, bounds.width - gaps);
            const auto widths = distribute(row.cells, availableWidth, [](const Cell& cell) { return cell.width; });

            float x = bounds.x;
            for (std::size_t index = 0; index < row.cells.size(); ++index)
            {
                layoutCell(
                    *row.cells.at(index),
                    {.x = x, .y = bounds.y, .width = widths.at(index), .height = bounds.height},
                    scale);
                x += widths.at(index) + gap;
            }
        }

        void layoutTable(Table& table, const Rectangle bounds, const float scale)
        {
            table.bounds = bounds;
            const float gap = std::max(0.0f, table.gap * scale);
            const float gaps = gap * static_cast<float>(table.rows.empty() ? 0 : table.rows.size() - 1);
            const float availableHeight = std::max(0.0f, bounds.height - gaps);
            const auto heights =
                distribute(table.rows, availableHeight, [](const Row& row) { return row.height; });

            float y = bounds.y;
            for (std::size_t index = 0; index < table.rows.size(); ++index)
            {
                layoutRow(
                    *table.rows.at(index),
                    {.x = bounds.x, .y = y, .width = bounds.width, .height = heights.at(index)},
                    gap,
                    scale);
                y += heights.at(index) + gap;
            }
        }

        std::optional<std::reference_wrapper<Cell>> hitTest(Table& table, const Vector2 point)
        {
            for (auto& row : std::views::reverse(table.rows))
            {
                for (const auto& cell : row->cells | std::views::reverse)
                {
                    Cell& candidate = *cell;
                    if (!CheckCollisionPointRec(point, candidate.bounds)) continue;
                    if (auto* table = std::get_if<std::unique_ptr<Table>>(&candidate.content))
                    {
                        if (auto nested = hitTest(**table, point)) return nested;
                    }
                    return std::ref(candidate);
                }
            }
            return std::nullopt;
        }

        Rectangle contentBounds(const Cell& cell, const float scale)
        {
            return inset(cell.bounds, scaled(cell.padding, scale));
        }

        void drawText(const std::string& text, const Cell& cell, const float scale)
        {
            const Rectangle available = contentBounds(cell, scale);
            Font font = cell.style.font.texture.id == 0 ? GetFontDefault() : cell.style.font;
            float fontSize = std::max(1.0f, cell.style.fontSize * scale);
            const float spacing = cell.style.fontSpacing * scale;
            Vector2 measured = MeasureTextEx(font, text.c_str(), fontSize, spacing);
            if (measured.x > available.width && measured.x > 0)
            {
                fontSize = std::max(1.0f, fontSize * available.width / measured.x);
                measured = MeasureTextEx(font, text.c_str(), fontSize, spacing);
            }

            float x = available.x;
            if (cell.style.horizontalAlignment == HorizontalAlignment::CENTER)
                x += (available.width - measured.x) * 0.5f;
            else if (cell.style.horizontalAlignment == HorizontalAlignment::RIGHT)
                x += available.width - measured.x;

            float y = available.y;
            if (cell.style.verticalAlignment == VerticalAlignment::MIDDLE)
                y += (available.height - measured.y) * 0.5f;
            else if (cell.style.verticalAlignment == VerticalAlignment::BOTTOM)
                y += available.height - measured.y;

            const ScissorScope clip{available};
            DrawTextEx(font, text.c_str(), {.x = x, .y = y}, fontSize, spacing, cell.style.textColor);
        }

        void drawImage(const CellImage& imageContent, const Cell& cell, const float scale)
        {
            const Texture image = imageContent.texture;
            if (image.id == 0 || image.width <= 0 || image.height <= 0) return;
            Rectangle destination = contentBounds(cell, scale);
            const float imageRatio = static_cast<float>(image.width) / static_cast<float>(image.height);
            const float destinationRatio = destination.height > 0 ? destination.width / destination.height : 0;
            if (destinationRatio > imageRatio)
            {
                const float width = destination.height * imageRatio;
                destination.x += (destination.width - width) * 0.5f;
                destination.width = width;
            }
            else
            {
                const float height = imageRatio > 0 ? destination.width / imageRatio : 0;
                destination.y += (destination.height - height) * 0.5f;
                destination.height = height;
            }
            DrawTexturePro(
                image,
                {.x = 0,
                 .y = 0.0f,
                 .width = static_cast<float>(image.width),
                 .height = imageContent.flipVertically ? -static_cast<float>(image.height)
                                                       : static_cast<float>(image.height)},
                destination,
                {},
                0,
                sage::colors::WHITE_COLOR);
        }

        void drawTable(
            const Table& table,
            const std::optional<std::reference_wrapper<const Cell>> hovered,
            const std::optional<std::reference_wrapper<const Cell>> pressed,
            const float scale)
        {
            for (const auto& row : table.rows)
            {
                for (const auto& cell : row->cells)
                {
                    Color background = cell->style.background;
                    if (pressed && cell.get() == &pressed->get() && cell->style.pressedBackground.a > 0)
                        background = cell->style.pressedBackground;
                    else if (hovered && cell.get() == &hovered->get() && cell->style.hoveredBackground.a > 0)
                        background = cell->style.hoveredBackground;

                    if (cell->style.backgroundTexture.id != 0)
                    {
                        const auto texture = cell->style.backgroundTexture;
                        auto source = cell->style.backgroundSource;
                        if (source.width <= 0 || source.height <= 0)
                            source = {
                                .x = 0,
                                .y = 0,
                                .width = static_cast<float>(texture.width),
                                .height = static_cast<float>(texture.height)};
                        DrawTexturePro(
                            texture,
                            source,
                            cell->bounds,
                            {},
                            0,
                            background.a > 0 ? background : sage::colors::WHITE_COLOR);
                    }
                    else if (background.a > 0)
                        DrawRectangleRec(cell->bounds, background);
                    if (cell->style.borderWidth > 0 && cell->style.border.a > 0)
                        DrawRectangleLinesEx(cell->bounds, cell->style.borderWidth * scale, cell->style.border);

                    if (const auto* table = std::get_if<std::unique_ptr<Table>>(&cell->content))
                    {
                        drawTable(**table, hovered, pressed, scale);
                        continue;
                    }

                    if (const auto* text = std::get_if<std::string>(&cell->content))
                        drawText(*text, *cell, scale);
                    else if (const auto* image = std::get_if<CellImage>(&cell->content))
                        drawImage(*image, *cell, scale);
                }
            }
        }

        void drawDebugTable(const Table& table)
        {
            DrawRectangleLinesEx(table.bounds, 1, sage::colors::BLUE_COLOR);
            for (const auto& row : table.rows)
            {
                DrawRectangleLinesEx(row->bounds, 1, sage::colors::GREEN_COLOR);
                for (const auto& cell : row->cells)
                {
                    DrawRectangleLinesEx(cell->bounds, 1, sage::colors::RED_COLOR);
                    if (const auto* table = std::get_if<std::unique_ptr<Table>>(&cell->content))
                        drawDebugTable(**table);
                }
            }
        }
    } // namespace

    UITheme::UITheme()
    {
        window.background = Color{.r = 24, .g = 29, .b = 38, .a = 245};
        window.padding = {.top = 14, .bottom = 14, .left = 14, .right = 14};

        button.background = Color{.r = 246, .g = 248, .b = 251, .a = 255};
        button.hoveredBackground = Color{.r = 236, .g = 242, .b = 252, .a = 255};
        button.pressedBackground = Color{.r = 219, .g = 234, .b = 254, .a = 255};
        button.border = Color{.r = 151, .g = 164, .b = 184, .a = 255};
        button.borderWidth = 1;
        button.horizontalAlignment = HorizontalAlignment::CENTER;
        button.textColor = sage::colors::BLACK_COLOR;

        title.textColor = sage::colors::WHITE_COLOR;
        title.horizontalAlignment = HorizontalAlignment::CENTER;
        title.fontSize = 18;
    }

    Cell::Cell(const Size requestedWidth) : width(requestedWidth)
    {
    }

    Cell::~Cell() = default;

    Cell& Cell::Text(std::string value)
    {
        content = std::move(value);
        return *this;
    }

    Cell& Cell::Image(const Texture texture, const bool flipVertically)
    {
        content = CellImage{.texture = texture, .flipVertically = flipVertically};
        return *this;
    }

    Table& Cell::AddTable()
    {
        auto table = std::make_unique<Table>();
        Table& result = *table;
        content = std::move(table);
        return result;
    }

    Cell& Cell::SetPadding(const Padding value)
    {
        padding = value;
        return *this;
    }

    Cell& Cell::UseStyle(const CellStyle& value)
    {
        style = value;
        return *this;
    }

    Cell& Cell::OnClick(std::function<void()> action)
    {
        click = std::move(action);
        return *this;
    }

    Cell& Cell::DragsWindow()
    {
        dragsWindow = true;
        return *this;
    }

    Row::Row(const Size requestedHeight) : height(requestedHeight)
    {
    }

    Cell& Row::AddCell(const Size requestedWidth)
    {
        cells.push_back(std::make_unique<Cell>(requestedWidth));
        return *cells.back();
    }

    Row& Table::AddRow(const Size requestedHeight)
    {
        rows.push_back(std::make_unique<Row>(requestedHeight));
        return *rows.back();
    }

    Table& Table::SetGap(const float value)
    {
        gap = value;
        return *this;
    }

    Window::Window(const Rectangle designBounds, WindowStyle style) : designBounds(designBounds), style(style)
    {
    }

    Table& Window::RootTable()
    {
        return root;
    }

    Window& Window::OnShow(std::function<void()> action)
    {
        onShow = std::move(action);
        return *this;
    }

    Window& Window::OnHide(std::function<void()> action)
    {
        onHide = std::move(action);
        return *this;
    }

    void Window::Show()
    {
        if (!hidden) return;
        hidden = false;
        if (onShow) onShow();
    }

    void Window::Hide()
    {
        if (hidden) return;
        hidden = true;
        if (onHide) onHide();
    }

    void Window::LayoutAt(const Rectangle value, const float scale)
    {
        bounds = value;
        layoutTable(root, inset(bounds, scaled(style.padding, scale)), scale);
    }

    void Window::Layout(const Settings& settings)
    {
        LayoutAt(
            {.x = settings.ScaleValueWidth(designBounds.x),
             .y = settings.ScaleValueHeight(designBounds.y),
             .width = settings.ScaleValueWidth(designBounds.width),
             .height = settings.ScaleValueHeight(designBounds.height)},
            settings.GetCurrentScaleFactor());
    }

    void Window::MoveTo(const Vector2 designPosition)
    {
        designBounds.x = designPosition.x;
        designBounds.y = designPosition.y;
        ClampToDesignViewport();
    }

    void Window::ClampToDesignViewport()
    {
        designBounds.x = std::clamp(designBounds.x, 0.0f, Settings::TARGET_SCREEN_WIDTH - designBounds.width);
        designBounds.y = std::clamp(designBounds.y, 0.0f, Settings::TARGET_SCREEN_HEIGHT - designBounds.height);
    }

    std::optional<std::reference_wrapper<Cell>> Window::HitTest(const Vector2 point)
    {
        return hitTest(root, point);
    }

    void Window::DrawAt(
        const std::optional<std::reference_wrapper<const Cell>> hovered,
        const std::optional<std::reference_wrapper<const Cell>> pressed,
        const float scale) const
    {
        if (style.background.a > 0) DrawRectangleRec(bounds, style.background);
        if (style.backgroundTexture.id != 0)
        {
            auto source = style.backgroundSource;
            if (source.width <= 0 || source.height <= 0)
                source = {
                    .x = 0,
                    .y = 0,
                    .width = static_cast<float>(style.backgroundTexture.width),
                    .height = static_cast<float>(style.backgroundTexture.height)};
            DrawTexturePro(style.backgroundTexture, source, bounds, {}, 0, sage::colors::WHITE_COLOR);
        }

        const ScissorScope clip{bounds};
        drawTable(root, hovered, pressed, scale);
    }

    void Window::DrawDebug() const
    {
        DrawRectangleLinesEx(bounds, 2, sage::colors::YELLOW_COLOR);
        drawDebugTable(root);
    }
} // namespace sage

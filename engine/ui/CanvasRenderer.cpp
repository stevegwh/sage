#include "CanvasRenderer.hpp"

#include "engine/ResourceManager.hpp"
#include <ranges>

namespace sage
{
    namespace
    {
        Texture loadTexture(const std::string& key)
        {
            return key.empty() ? Texture{} : ResourceManager::GetInstance().TextureLoad(key);
        }

        CellStyle cellStyle(const CanvasNode& node, bool draw)
        {
            CellStyle style;
            style.background = node.background;
            style.textColor = node.foreground;
            style.border = node.border;
            style.borderWidth = node.borderWidth;
            style.hoveredBackground = node.enabled ? node.hover : Color{};
            style.pressedBackground = node.enabled ? node.pressed : Color{};
            style.fontSize = node.fontSize;
            style.horizontalAlignment = node.horizontal;
            style.verticalAlignment = node.vertical;
            style.backgroundTexture = draw ? loadTexture(node.backgroundImage) : Texture{};
            style.backgroundSource = node.backgroundSource;
            if (draw && !node.font.empty()) style.font = ResourceManager::GetInstance().FontLoad(node.font);
            return style;
        }

        // Converts saved nodes into the existing retained Table/Row/Cell layout for one window.
        // References remain valid until that window has been laid out and drawn.
        struct CanvasTableBuilder
        {
            std::reference_wrapper<const CanvasDocument> document;
            std::reference_wrapper<CanvasLayout> layout;
            std::reference_wrapper<const std::map<unsigned int, CellImage>> images;
            unsigned int hovered;
            unsigned int pressed;
            bool draw;
            std::vector<std::pair<unsigned int, std::reference_wrapper<const Rectangle>>> bounds;
            std::optional<std::reference_wrapper<const Cell>> hoveredCell;
            std::optional<std::reference_wrapper<const Cell>> pressedCell;

            void buildTable(const CanvasNode& node, Table& table)
            {
                bounds.emplace_back(node.id, std::cref(table.bounds));
                table.gap = node.gap;
                for (const auto& rowNode : document.get().nodes)
                {
                    if (rowNode.parent != node.id || !rowNode.visible) continue;
                    auto& row = table.AddRow(rowNode.percent > 0 ? Size{Percent{rowNode.percent}} : Size{});
                    bounds.emplace_back(rowNode.id, std::cref(row.bounds));
                    for (const auto& cellNode : document.get().nodes)
                    {
                        if (cellNode.parent != rowNode.id || !cellNode.visible) continue;
                        auto& cell = row.AddCell(cellNode.percent > 0 ? Size{Percent{cellNode.percent}} : Size{});
                        buildCell(cellNode, cell);
                    }
                }
            }

            void buildCell(const CanvasNode& node, Cell& cell)
            {
                bounds.emplace_back(node.id, std::cref(cell.bounds));
                layout.get().cells.push_back(node.id);
                if (node.id == hovered) hoveredCell = std::cref(cell);
                if (node.id == pressed) pressedCell = std::cref(cell);
                cell.padding = node.padding;
                cell.style = cellStyle(node, draw);
                if (draw && images.get().contains(node.id))
                {
                    const auto& image = images.get().at(node.id);
                    cell.Image(image.texture, image.flipVertically);
                }
                else if (draw && !node.image.empty())
                    cell.Image(loadTexture(node.image));
                else
                    cell.Text(node.text);
                for (const auto& child : document.get().nodes)
                    if (child.parent == node.id && child.visible) buildTable(child, cell.AddTable());
            }
        };
    } // namespace

    unsigned int CanvasLayout::Hit(Vector2 point) const
    {
        for (unsigned int cell : std::views::reverse(cells))
            if (CheckCollisionPointRec(point, bounds.at(cell))) return cell;
        return 0;
    }

    CanvasLayout RenderCanvas(
        const CanvasDocument& document,
        Rectangle viewport,
        unsigned int hovered,
        unsigned int pressed,
        bool draw,
        const std::map<unsigned int, CellImage>& images)
    {
        CanvasLayout layout;
        if (document.nodes.empty() ||
            !(document.width > 0 && document.height > 0 && viewport.width > 0 && viewport.height > 0))
            return layout;
        const float scale = std::min(viewport.width / document.width, viewport.height / document.height);
        const Vector2 origin{
            .x = viewport.x + (viewport.width - document.width * scale) / 2,
            .y = viewport.y + (viewport.height - document.height * scale) / 2};
        const auto& root = document.nodes.front();
        layout.bounds[root.id] = {
            .x = origin.x, .y = origin.y, .width = document.width * scale, .height = document.height * scale};
        if (!root.visible) return layout;

        for (const auto& node : document.nodes)
        {
            if (node.kind != UINodeKind::Window || !node.visible) continue;
            WindowStyle style;
            style.background = node.background;
            style.padding = node.padding;
            style.backgroundTexture = draw ? loadTexture(node.backgroundImage) : Texture{};
            style.backgroundSource = node.backgroundSource;
            const auto placement = node.WindowBounds({.x = document.width, .y = document.height});
            Window window(placement, style);
            CanvasTableBuilder builder{
                .document = std::cref(document),
                .layout = std::ref(layout),
                .images = std::cref(images),
                .hovered = hovered,
                .pressed = pressed,
                .draw = draw,
                .bounds = {},
                .hoveredCell = {},
                .pressedCell = {}};
            for (const auto& table : document.nodes)
                if (table.parent == node.id && table.visible) builder.buildTable(table, window.RootTable());
            const Rectangle bounds{
                .x = origin.x + placement.x * scale,
                .y = origin.y + placement.y * scale,
                .width = placement.width * scale,
                .height = placement.height * scale};
            window.LayoutAt(bounds, scale);
            if (draw) window.DrawAt(builder.hoveredCell, builder.pressedCell, scale);
            layout.bounds[node.id] = bounds;
            for (const auto& [id, rectangle] : builder.bounds)
                layout.bounds[id] = rectangle.get();
        }
        return layout;
    }
} // namespace sage

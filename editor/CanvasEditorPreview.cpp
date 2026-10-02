#include "CanvasEditor.hpp"
#include "engine/ui/CanvasRenderer.hpp"
#include "imgui.h"
#include "rlgl.h"
#include "rlImGui.h"

#include <ranges>

namespace sage::editor
{
    namespace
    {
        constexpr float PREVIEW_INPUT_WIDTH = 100.0f;
        constexpr float MIN_PREVIEW_WIDTH = 320.0f;
        constexpr float MAX_PREVIEW_WIDTH = 3840.0f;
        constexpr float MIN_PREVIEW_HEIGHT = 240.0f;
        constexpr float MAX_PREVIEW_HEIGHT = 2160.0f;
        constexpr Color PREVIEW_BACKGROUND = {.r = 30, .g = 33, .b = 39, .a = 255};
        constexpr Color SELECTION_COLOR = {.r = 240, .g = 190, .b = 65, .a = 255};
        constexpr float SELECTION_BORDER_WIDTH = 2.0f;
        constexpr int RESIZE_HANDLE_SIZE = 8;
    } // namespace

    void CanvasEditor::drawPreview()
    {
        ImGui::SetNextItemWidth(PREVIEW_INPUT_WIDTH);
        ImGui::DragFloat("Preview width", &previewWidth, 1, MIN_PREVIEW_WIDTH, MAX_PREVIEW_WIDTH);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(PREVIEW_INPUT_WIDTH);
        ImGui::DragFloat("Preview height", &previewHeight, 1, MIN_PREVIEW_HEIGHT, MAX_PREVIEW_HEIGHT);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(
            "Alt-drag windows to move. Drag the bottom-right handle to resize. Drag a selected row/cell edge to "
            "adjust its share.");
        ImGui::PopStyleColor();
        const auto available = ImGui::GetContentRegionAvail();
        previewWidth = std::clamp(previewWidth, MIN_PREVIEW_WIDTH, MAX_PREVIEW_WIDTH);
        previewHeight = std::clamp(previewHeight, MIN_PREVIEW_HEIGHT, MAX_PREVIEW_HEIGHT);
        const float fit = std::min(available.x / previewWidth, available.y / previewHeight);
        const int width = std::max(1, static_cast<int>(previewWidth * fit)),
                  height = std::max(1, static_cast<int>(previewHeight * fit));
        if (preview.texture.width != width || preview.texture.height != height)
        {
            if (preview.id) UnloadRenderTexture(preview);
            preview = LoadRenderTexture(width, height);
            SetTextureFilter(preview.texture, TEXTURE_FILTER_BILINEAR);
        }
        rlDrawRenderBatchActive();
        BeginTextureMode(preview);
        ClearBackground(PREVIEW_BACKGROUND);
        auto layout = RenderCanvas(
            document, {.x = 0, .y = 0, .width = static_cast<float>(width), .height = static_cast<float>(height)});
        if (layout.bounds.contains(selected))
        {
            const auto b = layout.bounds.at(selected);
            DrawRectangleLinesEx(b, SELECTION_BORDER_WIDTH, SELECTION_COLOR);
            if (document.Find(selected)->get().kind == UINodeKind::Window)
                DrawRectangle(
                    static_cast<int>(b.x + b.width - RESIZE_HANDLE_SIZE),
                    static_cast<int>(b.y + b.height - RESIZE_HANDLE_SIZE),
                    RESIZE_HANDLE_SIZE,
                    RESIZE_HANDLE_SIZE,
                    SELECTION_COLOR);
        }
        EndTextureMode();
        const auto origin = ImGui::GetCursorScreenPos();
        rlImGuiImageRenderTexture(&preview);
        const auto mouse = ImGui::GetMousePos();
        const Vector2 local{.x = mouse.x - origin.x, .y = mouse.y - origin.y};
        const float scale =
            std::min(static_cast<float>(width) / document.width, static_cast<float>(height) / document.height);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            beginPreviewDrag(layout, local);
        updatePreviewDrag(local, scale);
    }

    void CanvasEditor::beginPreviewDrag(const CanvasLayout& layout, Vector2 mouse)
    {
        dividerNeighbor = 0;
        const auto selectedNode = document.Find(selected);
        if (selectedNode && layout.bounds.contains(selected) &&
            (selectedNode->get().kind == UINodeKind::Cell || selectedNode->get().kind == UINodeKind::Row))
        {
            const auto b = layout.bounds.at(selected);
            const bool horizontal = selectedNode->get().kind == UINodeKind::Cell;
            const Rectangle handle =
                horizontal ? Rectangle{.x = b.x + b.width - 5, .y = b.y, .width = 10, .height = b.height}
                           : Rectangle{.x = b.x, .y = b.y + b.height - 5, .width = b.width, .height = 10};
            if (CheckCollisionPointRec(mouse, handle))
            {
                std::vector<unsigned int> siblings;
                dividerSpace = 0;
                for (const auto& sibling : document.nodes)
                    if (sibling.parent == selectedNode->get().parent && layout.bounds.contains(sibling.id))
                    {
                        siblings.push_back(sibling.id);
                        const auto rect = layout.bounds.at(sibling.id);
                        dividerSpace += horizontal ? rect.width : rect.height;
                    }
                const auto found = std::ranges::find(siblings, selected);
                if (found != siblings.end() && std::next(found) != siblings.end() && dividerSpace > 0)
                {
                    dividerNeighbor = *std::next(found);
                    for (auto id : siblings)
                    {
                        const auto rect = layout.bounds.at(id);
                        document.Find(id)->get().percent =
                            100 * (horizontal ? rect.width : rect.height) / dividerSpace;
                    }
                    dividerPercent = document.Find(selected)->get().percent;
                    dividerNeighborPercent = document.Find(dividerNeighbor)->get().percent;
                    dragStart = mouse;
                }
            }
        }
        auto n = document.Find(selected);
        if (n && n->get().kind == UINodeKind::Window && layout.bounds.contains(selected))
        {
            const auto b = layout.bounds.at(selected);
            resizing = CheckCollisionPointRec(
                mouse, {.x = b.x + b.width - 12, .y = b.y + b.height - 12, .width = 16, .height = 16});
        }
        if (!resizing && !dividerNeighbor)
        {
            auto hit = layout.Hit(mouse);
            if (hit)
                selected = hit;
            else
                for (const auto& node : document.nodes | std::views::reverse)
                    if (layout.bounds.contains(node.id) &&
                        CheckCollisionPointRec(mouse, layout.bounds.at(node.id)))
                    {
                        selected = node.id;
                        break;
                    }
            if (ImGui::GetIO().KeyAlt)
            {
                auto cell = document.Find(selected);
                while (cell && cell->get().kind != UINodeKind::Window)
                    cell = document.Find(cell->get().parent);
                if (cell) selected = cell->get().id;
            }
        }
        n = document.Find(selected);
        moving = n && n->get().kind == UINodeKind::Window && !resizing && !dividerNeighbor;
        if (moving || resizing)
        {
            dragStart = mouse;
            dragBounds = n->get().WindowBounds({.x = document.width, .y = document.height});
        }
    }

    void CanvasEditor::updatePreviewDrag(Vector2 mouse, float scale)
    {
        if ((moving || resizing) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (auto n = document.Find(selected))
            {
                auto& node = n->get();
                node.rectangle = dragBounds;
                node.windowHorizontal = WindowHorizontalAlignment::FREE;
                node.windowVertical = WindowVerticalAlignment::FREE;
                const Vector2 delta{.x = (mouse.x - dragStart.x) / scale, .y = (mouse.y - dragStart.y) / scale};
                if (moving)
                {
                    node.rectangle.x = dragBounds.x + delta.x;
                    node.rectangle.y = dragBounds.y + delta.y;
                }
                else
                {
                    node.rectangle.width = std::max(1.f, dragBounds.width + delta.x);
                    node.rectangle.height = std::max(1.f, dragBounds.height + delta.y);
                }
            }
        }
        if (dividerNeighbor && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            auto node = document.Find(selected);
            auto neighbor = document.Find(dividerNeighbor);
            if (node && neighbor)
            {
                const float delta =
                    100 * (node->get().kind == UINodeKind::Cell ? mouse.x - dragStart.x : mouse.y - dragStart.y) /
                    dividerSpace;
                const float change = std::clamp(delta, 0.1f - dividerPercent, dividerNeighborPercent - 0.1f);
                node->get().percent = dividerPercent + change;
                neighbor->get().percent = dividerNeighborPercent - change;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            moving = resizing = false;
            dividerNeighbor = 0;
        }
    }
} // namespace sage::editor

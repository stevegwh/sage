#include "CanvasEditor.hpp"
#include "engine/ui/CanvasRenderer.hpp"
#include "imgui.h"
#include "rlgl.h"
#include "rlImGui.h"

namespace sage::editor
{
    void CanvasEditor::drawPreview()
    {
        ImGui::SetNextItemWidth(100);
        ImGui::DragFloat("Preview width", &previewWidth, 1, 320, 3840);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::DragFloat("Preview height", &previewHeight, 1, 240, 2160);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(
            "Alt-drag windows to move. Drag the bottom-right handle to resize. Drag a selected row/cell edge to "
            "adjust its share.");
        ImGui::PopStyleColor();
        const auto available = ImGui::GetContentRegionAvail();
        previewWidth = std::clamp(previewWidth, 320.f, 3840.f);
        previewHeight = std::clamp(previewHeight, 240.f, 2160.f);
        const float fit = std::min(available.x / previewWidth, available.y / previewHeight);
        const int width = std::max(1, int(previewWidth * fit)), height = std::max(1, int(previewHeight * fit));
        if (preview.texture.width != width || preview.texture.height != height)
        {
            if (preview.id) UnloadRenderTexture(preview);
            preview = LoadRenderTexture(width, height);
            SetTextureFilter(preview.texture, TEXTURE_FILTER_BILINEAR);
        }
        rlDrawRenderBatchActive();
        BeginTextureMode(preview);
        ClearBackground({30, 33, 39, 255});
        auto layout = RenderCanvas(document, {0, 0, float(width), float(height)});
        if (layout.bounds.contains(selected))
        {
            const auto b = layout.bounds.at(selected);
            DrawRectangleLinesEx(b, 2, Color{240, 190, 65, 255});
            if (document.Find(selected)->get().kind == UINodeKind::Window)
                DrawRectangle(int(b.x + b.width - 8), int(b.y + b.height - 8), 8, 8, Color{240, 190, 65, 255});
        }
        EndTextureMode();
        const auto origin = ImGui::GetCursorScreenPos();
        rlImGuiImageRenderTexture(&preview);
        const auto mouse = ImGui::GetMousePos();
        const Vector2 local{mouse.x - origin.x, mouse.y - origin.y};
        const float scale = std::min(float(width) / document.width, float(height) / document.height);
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
            const Rectangle handle = horizontal ? Rectangle{b.x + b.width - 5, b.y, 10, b.height}
                                                : Rectangle{b.x, b.y + b.height - 5, b.width, 10};
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
            resizing = CheckCollisionPointRec(mouse, {b.x + b.width - 12, b.y + b.height - 12, 16, 16});
        }
        if (!resizing && !dividerNeighbor)
        {
            auto hit = layout.Hit(mouse);
            if (hit)
                selected = hit;
            else
                for (auto it = document.nodes.rbegin(); it != document.nodes.rend(); ++it)
                    if (layout.bounds.contains(it->id) && CheckCollisionPointRec(mouse, layout.bounds.at(it->id)))
                    {
                        selected = it->id;
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
            dragBounds = n->get().WindowBounds({document.width, document.height});
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
                const Vector2 delta{(mouse.x - dragStart.x) / scale, (mouse.y - dragStart.y) / scale};
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

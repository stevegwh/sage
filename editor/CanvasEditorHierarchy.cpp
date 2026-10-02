#include "CanvasEditor.hpp"
#include "imgui.h"

namespace sage::editor
{
    void CanvasEditor::hierarchy(unsigned int id)
    {
        auto node = document.Find(id);
        if (!node) return;
        const auto kind = node->get().kind;
        const auto parent = node->get().parent;
        const std::string label = node->get().name + " (" + UINodeKindName(kind) + ")";
        const bool leaf =
            std::ranges::none_of(document.nodes, [&](const auto& child) { return child.parent == id; });
        const auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen |
                           (leaf ? ImGuiTreeNodeFlags_Leaf : 0) |
                           (selected == id ? ImGuiTreeNodeFlags_Selected : 0);
        const bool open = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)), flags, "%s", label.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected = id;
        if (kind != UINodeKind::Canvas && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("CANVAS_NODE", &id, sizeof(id));
            ImGui::TextUnformatted(label.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("CANVAS_NODE"))
            {
                const auto dragged = *static_cast<const unsigned int*>(payload->Data);
                const auto source = document.Find(dragged);
                if (source &&
                    !document.Move(
                        dragged, source->get().kind == kind ? parent : id, source->get().kind == kind ? id : 0))
                    error = "That move is not valid for the table hierarchy.";
            }
            ImGui::EndDragDropTarget();
        }
        if (open)
        {
            std::vector<unsigned int> children;
            for (const auto& child : document.nodes)
                if (child.parent == id) children.push_back(child.id);
            for (auto child : children)
                hierarchy(child);
            ImGui::TreePop();
        }
    }
} // namespace sage::editor

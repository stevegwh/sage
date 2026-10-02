#include "CanvasEditor.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <array>

namespace sage::editor
{
    namespace
    {
        void editColor(const char* label, Color& color)
        {
            std::array<float, 4> value{
                static_cast<float>(color.r) / 255.f,
                static_cast<float>(color.g) / 255.f,
                static_cast<float>(color.b) / 255.f,
                static_cast<float>(color.a) / 255.f};
            if (!ImGui::ColorEdit4(label, value.data())) return;
            color = {
                .r = static_cast<unsigned char>(value.at(0) * 255),
                .g = static_cast<unsigned char>(value.at(1) * 255),
                .b = static_cast<unsigned char>(value.at(2) * 255),
                .a = static_cast<unsigned char>(value.at(3) * 255)};
        }
        void assetPicker(const char* label, std::string& value, const std::vector<std::filesystem::path>& assets)
        {
            ImGui::PushID(label);
            if (ImGui::BeginCombo(
                    label, value.empty() ? "None" : std::filesystem::path(value).filename().string().c_str()))
            {
                if (ImGui::Selectable("None", value.empty())) value.clear();
                for (const auto& asset : assets)
                {
                    if (ImGui::Selectable(asset.generic_string().c_str(), value == asset.generic_string()))
                        value = asset.generic_string();
                }
                ImGui::EndCombo();
            }
            if (!value.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", value.c_str());
            ImGui::PopID();
        }
    } // namespace
    void CanvasEditor::inspector()
    {
        const auto found = document.Find(selected);
        if (!found) return;
        auto& node = found->get();
        ImGui::Text("%s", UINodeKindName(node.kind));
        ImGui::InputText("Name", &node.name);
        ImGui::Checkbox("Visible", &node.visible);
        ImGui::SameLine();
        ImGui::Checkbox("Enabled", &node.enabled);

        switch (node.kind)
        {
        case UINodeKind::Canvas:
            drawCanvasProperties();
            break;
        case UINodeKind::Window:
            drawWindowProperties(node);
            break;
        case UINodeKind::Table:
            drawTableProperties(node);
            break;
        case UINodeKind::Row:
            drawRowProperties(node);
            break;
        case UINodeKind::Cell:
            drawCellProperties(node);
            break;
        }
        drawBehaviour(node);
        drawStructureActions(node.id);
    }

    void CanvasEditor::drawCanvasProperties()
    {
        ImGui::SeparatorText("Canvas size");
        ImGui::DragFloat("Reference width", &document.width, 1, 1, 8192);
        ImGui::DragFloat("Reference height", &document.height, 1, 1, 8192);
        ImGui::TextWrapped("Scales uniformly to fit the viewport.");
    }

    void CanvasEditor::drawWindowProperties(CanvasNode& node)
    {
        ImGui::SeparatorText("Window placement");
        int horizontal = static_cast<int>(node.windowHorizontal);
        int vertical = static_cast<int>(node.windowVertical);
        // Keep the displayed position when switching an axis back to manual placement.
        const auto bounds = node.WindowBounds({.x = document.width, .y = document.height});
        if (ImGui::Combo("Horizontal", &horizontal, "Left\0Centre\0Right\0Free\0"))
        {
            node.windowHorizontal = static_cast<WindowHorizontalAlignment>(horizontal);
            if (node.windowHorizontal == WindowHorizontalAlignment::FREE) node.rectangle.x = bounds.x;
        }
        if (ImGui::Combo("Vertical", &vertical, "Top\0Middle\0Bottom\0Free\0"))
        {
            node.windowVertical = static_cast<WindowVerticalAlignment>(vertical);
            if (node.windowVertical == WindowVerticalAlignment::FREE) node.rectangle.y = bounds.y;
        }
        auto position = node.WindowBounds({.x = document.width, .y = document.height});
        ImGui::BeginDisabled(node.windowHorizontal != WindowHorizontalAlignment::FREE);
        if (ImGui::DragFloat("X", &position.x, 1)) node.rectangle.x = position.x;
        ImGui::EndDisabled();
        ImGui::BeginDisabled(node.windowVertical != WindowVerticalAlignment::FREE);
        if (ImGui::DragFloat("Y", &position.y, 1)) node.rectangle.y = position.y;
        ImGui::EndDisabled();
        ImGui::DragFloat2("Size", &node.rectangle.width, 1, 1, 8192);
        drawBackgroundProperties(node);
    }

    void CanvasEditor::drawTableProperties(CanvasNode& node)
    {
        ImGui::SeparatorText("Table layout");
        ImGui::DragFloat("Gap", &node.gap, 0.5f, 0, 256);
    }

    void CanvasEditor::drawRowProperties(CanvasNode& node)
    {
        ImGui::SeparatorText("Row layout");
        ImGui::SliderFloat("Height %", &node.percent, 0, 100);
        ImGui::TextDisabled("0 = share remaining space");
    }

    void CanvasEditor::drawCellProperties(CanvasNode& node)
    {
        ImGui::SeparatorText("Cell layout");
        ImGui::SliderFloat("Width %", &node.percent, 0, 100);
        ImGui::TextDisabled("0 = share remaining space");
        drawBackgroundProperties(node);
        ImGui::SeparatorText("Content");
        const bool hasTable =
            std::ranges::any_of(document.nodes, [&](const auto& child) { return child.parent == node.id; });
        if (!hasTable)
        {
            if (ImGui::InputTextMultiline("Text", &node.text)) node.image.clear();
            const auto previous = node.image;
            assetPicker("Image", node.image, textures);
            if (previous != node.image && !node.image.empty()) node.text.clear();
        }
        assetPicker("Font", node.font, fonts);
        ImGui::DragFloat("Font size", &node.fontSize, 0.5f, 1, 256);
        editColor("Text colour", node.foreground);
        int horizontal = static_cast<int>(node.horizontal), vertical = static_cast<int>(node.vertical);
        if (ImGui::Combo("Horizontal", &horizontal, "Left\0Centre\0Right\0"))
            node.horizontal = static_cast<HorizontalAlignment>(horizontal);
        if (ImGui::Combo("Vertical", &vertical, "Top\0Middle\0Bottom\0"))
            node.vertical = static_cast<VerticalAlignment>(vertical);
        ImGui::SeparatorText("Interaction and border");
        editColor("Hover", node.hover);
        editColor("Pressed", node.pressed);
        editColor("Border", node.border);
        ImGui::DragFloat("Border width", &node.borderWidth, 0.25f, 0, 32);
    }

    void CanvasEditor::drawBackgroundProperties(CanvasNode& node)
    {
        if (!ImGui::CollapsingHeader("Background and spacing", ImGuiTreeNodeFlags_DefaultOpen)) return;
        editColor("Background", node.background);
        assetPicker("Background image", node.backgroundImage, textures);
        if (!node.backgroundImage.empty())
        {
            ImGui::DragFloat2("Source position", &node.backgroundSource.x, 1, 0, 8192);
            ImGui::DragFloat2("Source size", &node.backgroundSource.width, 1, 0, 8192);
        }
        ImGui::DragFloat("Padding top", &node.padding.top, 0.5f, 0, 256);
        ImGui::DragFloat("Padding bottom", &node.padding.bottom, 0.5f, 0, 256);
        ImGui::DragFloat("Padding left", &node.padding.left, 0.5f, 0, 256);
        ImGui::DragFloat("Padding right", &node.padding.right, 0.5f, 0, 256);
    }

    void CanvasEditor::drawStructureActions(unsigned int id)
    {
        const auto found = document.Find(id);
        if (!found) return;
        const auto kind = found->get().kind;
        const auto parent = found->get().parent;
        const bool hasTable =
            std::ranges::any_of(document.nodes, [&](const auto& child) { return child.parent == id; });
        ImGui::SeparatorText("Structure");
        for (auto childKind : {UINodeKind::Window, UINodeKind::Table, UINodeKind::Row, UINodeKind::Cell})
        {
            if (!CanContain(kind, childKind) || (childKind == UINodeKind::Table && hasTable)) continue;
            if (ImGui::Button((std::string("Add ") + UINodeKindName(childKind)).c_str()))
            {
                selected = document.Add(id, childKind);
                return;
            }
        }
        const auto parentNode = document.Find(parent);
        const bool requiredTable = parentNode && parentNode->get().kind == UINodeKind::Window;
        if (kind != UINodeKind::Canvas && !requiredTable && ImGui::Button("Delete node"))
        {
            document.Remove(id);
            selected = parent;
        }
    }
} // namespace sage::editor

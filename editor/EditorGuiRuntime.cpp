#include "EditorGui.hpp"

#include <utility>

namespace sage::editor
{
    namespace
    {
        void DrawScriptNode(const json::Value& node, const bool openByDefault = true)
        {
            const auto name = json::String(node, "Name");
            const auto value = json::String(node, "Value");
            const auto& children = json::Require(node, "Children");
            ImGui::PushID(name.c_str());
            bool open = false;
            if (children.Empty())
            {
                ImGui::TextWrapped("%s: %s", name.c_str(), value.c_str());
                if (ImGui::BeginPopupContextItem("copy"))
                {
                    if (ImGui::MenuItem("Copy Value")) ImGui::SetClipboardText(value.c_str());
                    ImGui::EndPopup();
                }
            }
            else
                open = ImGui::TreeNodeEx(
                    "node",
                    ImGuiTreeNodeFlags_SpanAvailWidth | (openByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                    "%s: %s",
                    name.c_str(),
                    value.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", json::String(node, "Type").c_str());
            if (open)
            {
                for (const auto& child : children.GetArray())
                    DrawScriptNode(child, false);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    } // namespace

    void EditorGui::SetRuntimeScript(json::Document snapshot)
    {
        runtimeScript = std::move(snapshot);
    }

    void EditorGui::drawRuntimeScript() const
    {
        if (runtimeScript.HasMember("Status"))
        {
            ImGui::TextWrapped("C#: %s", json::String(runtimeScript, "Status").c_str());
            return;
        }
        if (!runtimeScript.HasMember("Root")) return;
        const auto& root = runtimeScript["Root"];
        const auto label = "C# " + json::String(root, "Type");
        if (!ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::TextDisabled("Live fields and auto-properties");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Includes private fields. Computed property getters are not executed. Nested data is bounded.");
        for (const auto* flag : {"Awakened", "Enabled", "Started", "Failed"})
            ImGui::Text("%s: %s", flag, json::Require(runtimeScript, flag).GetBool() ? "true" : "false");
        ImGui::Separator();
        for (const auto& child : json::Require(root, "Children").GetArray())
            DrawScriptNode(child);
    }
} // namespace sage::editor

#include "CanvasEditor.hpp"
#include "engine/ui/CanvasSystem.hpp"
#include "imgui.h"

namespace sage::editor
{
    std::vector<std::filesystem::path> CanvasEditor::canvasFiles()
    {
        std::vector<std::filesystem::path> files;
        if (std::filesystem::exists("resources"))
            for (const auto& file : std::filesystem::recursive_directory_iterator("resources"))
                if (file.is_regular_file() && file.path().extension() == ".canvas") files.push_back(file.path());
        std::ranges::sort(files);
        return files;
    }
    void CanvasEditor::DrawSceneMenu(bool enabled)
    {
        if (!ImGui::BeginMenu("Scene", enabled)) return;
        if (ImGui::MenuItem("UI", nullptr, sceneUIOpen)) sceneUIOpen = !sceneUIOpen;
        ImGui::EndMenu();
    }

    void CanvasEditor::DrawSceneUI(entt::registry& registry, const std::function<void()>& sceneChanged)
    {
        if (!sceneUIOpen) return;
        ImGui::SetNextWindowSize({560, 340}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene UI", &sceneUIOpen))
        {
            ImGui::TextWrapped("Canvases shown when this scene starts. Save the map to keep changes.");
            ImGui::TextDisabled("Stacking order: bottom to top. Drag rows to reorder.");
            auto* settings = registry.ctx().find<InitialCanvases>();
            std::optional<std::size_t> remove;
            std::optional<std::pair<std::size_t, std::size_t>> move;
            if (settings &&
                ImGui::BeginTable("Scene canvases", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("On scene start", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 115);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < settings->assets.size(); ++i)
                {
                    const auto& asset = settings->assets.at(i);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::filesystem::path(asset).filename().string().c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.c_str());
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("SCENE_CANVAS", &i, sizeof(i));
                        ImGui::TextUnformatted(asset.c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_CANVAS"))
                            move = {*static_cast<const std::size_t*>(payload->Data), i};
                        ImGui::EndDragDropTarget();
                    }
                    const bool exists = std::filesystem::is_regular_file(asset);
                    if (!exists) ImGui::TextColored({1, .45f, .3f, 1}, "Missing asset");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Show");
                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled(!exists);
                    if (ImGui::SmallButton("Edit")) Open(asset);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) remove = i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (remove)
            {
                settings->assets.erase(settings->assets.begin() + static_cast<std::ptrdiff_t>(*remove));
                sceneChanged();
            }
            else if (move && move->first != move->second && move->first < settings->assets.size())
            {
                auto asset = settings->assets.at(move->first);
                settings->assets.erase(settings->assets.begin() + static_cast<std::ptrdiff_t>(move->first));
                settings->assets.insert(
                    settings->assets.begin() + static_cast<std::ptrdiff_t>(move->second), std::move(asset));
                sceneChanged();
            }
            if (!settings || settings->assets.empty()) ImGui::TextDisabled("No canvases added to this scene.");
            if (ImGui::Button("Add Canvas...")) ImGui::OpenPopup("Add scene canvas");
            if (ImGui::BeginPopup("Add scene canvas"))
            {
                const auto files = canvasFiles();
                if (files.empty()) ImGui::TextDisabled("Create a canvas in the resource browser first.");
                for (const auto& file : files)
                {
                    const auto asset = file.generic_string();
                    const bool included =
                        settings && std::ranges::find(settings->assets, asset) != settings->assets.end();
                    if (ImGui::MenuItem(asset.c_str(), nullptr, included, !included))
                    {
                        if (!settings) settings = &registry.ctx().emplace<InitialCanvases>();
                        settings->assets.push_back(asset);
                        sceneChanged();
                    }
                }
                ImGui::EndPopup();
            }
            if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        }
        ImGui::End();
    }
} // namespace sage::editor

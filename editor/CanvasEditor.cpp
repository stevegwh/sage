#include "CanvasEditor.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <utility>

namespace sage::editor
{
    CanvasEditor::CanvasEditor(CSharpScriptEditorConfig config) : scripts(std::move(config))
    {
    }
    CanvasEditor::~CanvasEditor()
    {
        if (preview.id) UnloadRenderTexture(preview);
    }
    std::string CanvasEditor::state() const
    {
        return json::Stringify(json::Encode(document));
    }
    void CanvasEditor::restore(const std::string& value)
    {
        document = CanvasDocument::FromJson(json::Parse(value));
        if (!document.Find(selected)) selected = document.nodes.front().id;
    }
    void CanvasEditor::beginEditing()
    {
        pathInput = path.generic_string();
        active = workspaceVisible = true;
        undo.clear();
        redo.clear();
        error.clear();
        transaction.reset();
        moving = resizing = false;
        dividerNeighbor = 0;
        scanAssets();
    }
    void CanvasEditor::New()
    {
        if (IsDirty())
        {
            pendingOpen = std::filesystem::path{};
            RequestClose();
            return;
        }
        document = CanvasDocument{};
        selected = document.Add(1, UINodeKind::Window);
        auto table = document.nodes.back().id;
        document.Add(document.Add(table, UINodeKind::Row), UINodeKind::Cell);
        path = "resources/ui/NewCanvas.canvas";
        for (int i = 2; std::filesystem::exists(path); ++i)
            path = "resources/ui/NewCanvas" + std::to_string(i) + ".canvas";
        saved.clear();
        beginEditing();
    }
    void CanvasEditor::Open(const std::filesystem::path& file)
    {
        if (active && std::filesystem::absolute(file).lexically_normal() ==
                          std::filesystem::absolute(path).lexically_normal())
        {
            Resume();
            return;
        }
        if (IsDirty())
        {
            pendingOpen = file;
            RequestClose();
            return;
        }
        try
        {
            document = CanvasDocument::Load(file);
            path = file;
            selected = document.nodes.front().id;
            saved = state();
            beginEditing();
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
    }
    void CanvasEditor::save()
    {
        try
        {
            std::filesystem::path target(pathInput);
            if (target.extension() != ".canvas") throw std::runtime_error("Use the .canvas extension");
            if (target != path && std::filesystem::exists(target))
                throw std::runtime_error("That canvas already exists. Choose another filename.");
            document.Save(target);
            path = target;
            saved = state();
            error.clear();
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
    }
    void CanvasEditor::RequestClose()
    {
        Resume();
        if (IsDirty())
            closeRequested = true;
        else
            finishClose();
    }
    void CanvasEditor::finishClose()
    {
        active = workspaceVisible = closeRequested = false;
        const auto next = std::exchange(pendingOpen, std::nullopt);
        if (next)
        {
            if (next->empty())
                New();
            else
                Open(*next);
        }
    }
    void CanvasEditor::history(bool forward)
    {
        auto& from = forward ? redo : undo;
        auto& to = forward ? undo : redo;
        if (from.empty()) return;
        to.push_back(state());
        restore(from.back());
        from.pop_back();
        transaction.reset();
        historyChanged = true;
    }
    void CanvasEditor::drawToolbar()
    {
        if (ImGui::BeginTabBar("Canvas documents"))
        {
            if (ImGui::BeginTabItem("Scene")) ImGui::EndTabItem();
            if (ImGui::IsItemClicked()) ShowScene();
            bool keepOpen = true;
            const auto label = path.filename().string() + "###canvasDocument";
            const auto flags = ImGuiTabItemFlags_SetSelected |
                               (IsDirty() ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None);
            if (ImGui::BeginTabItem(label.c_str(), &keepOpen, flags)) ImGui::EndTabItem();
            if (!keepOpen) RequestClose();
            ImGui::EndTabBar();
        }
        if (ImGui::Button("New Canvas")) New();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("Open Canvas", "Choose asset..."))
        {
            for (const auto& file : canvasFiles())
                if (ImGui::Selectable(file.generic_string().c_str())) Open(file);
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("File", &pathInput);
        ImGui::SameLine();
        if (ImGui::Button("Save")) save();
        ImGui::SameLine();
        if (ImGui::Button("Close Canvas")) RequestClose();
        ImGui::SameLine();
        if (ImGui::Button("Undo")) history(false);
        ImGui::SameLine();
        if (ImGui::Button("Redo")) history(true);
        ImGui::SameLine();
        ImGui::TextUnformatted(IsDirty() ? "Unsaved changes" : "Saved");
        if (!error.empty()) ImGui::TextColored({1, .45f, .3f, 1}, "%s", error.c_str());
    }
    void CanvasEditor::Draw()
    {
        if (!active) return;
        historyChanged = false;
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowBgAlpha(1);
        ImGui::Begin(
            "Canvas Editor",
            nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        drawToolbar();
        drawScriptIssues();
        const auto before = state();
        if (ImGui::BeginTable("workspace", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 230);
            ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 340);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginChild("hierarchy", {}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            hierarchy(document.nodes.front().id);
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("preview");
            drawPreview();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("inspector");
            inspector();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        const auto after = state();
        if (!historyChanged && before != after && !transaction) transaction = before;
        if (transaction && !ImGui::IsAnyItemActive() && !moving && !resizing && !dividerNeighbor)
        {
            if (*transaction != after)
            {
                undo.push_back(*transaction);
                if (undo.size() > 100) undo.erase(undo.begin());
                redo.clear();
            }
            transaction.reset();
        }
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_S)) save();
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) history(ImGui::GetIO().KeyShift);
        }
        drawCloseDialog();
        ImGui::End();
    }
    void CanvasEditor::drawCloseDialog()
    {
        if (closeRequested) ImGui::OpenPopup("Unsaved Canvas");
        if (ImGui::BeginPopupModal("Unsaved Canvas", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Save changes to %s?", path.filename().string().c_str());
            if (pendingOpen) ImGui::TextUnformatted("Then open the selected canvas.");
            if (ImGui::Button(pendingOpen ? "Save & Open" : "Save & Close"))
            {
                save();
                if (!IsDirty())
                {
                    ImGui::CloseCurrentPopup();
                    finishClose();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                ImGui::CloseCurrentPopup();
                finishClose();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                closeRequested = false;
                pendingOpen.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
} // namespace sage::editor

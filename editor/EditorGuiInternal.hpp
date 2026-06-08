//
// Small ImGui helpers and palette constants shared by the EditorGui window
// translation units (core, asset drawer, hierarchy). Header-only so each window
// .cpp can use them without a link dependency; the functions are tiny.
//

#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include "raylib.h"

#include <algorithm>
#include <functional>

namespace sage::editor
{
    inline constexpr float DOCK_RESIZE_HANDLE_THICKNESS = 8.0f;
    inline constexpr Color EDITOR_WINDOW_BACKGROUND = {35, 38, 43, 245};
    inline constexpr Color EDITOR_TEXT = {230, 234, 240, 255};

    inline ImVec4 ToImGuiColor(const Color color)
    {
        return {
            static_cast<float>(color.r) / 255.0f,
            static_cast<float>(color.g) / 255.0f,
            static_cast<float>(color.b) / 255.0f,
            static_cast<float>(color.a) / 255.0f};
    }

    // Draws a search field with a placeholder hint plus a trailing "x" button that
    // clears the bound filter. idPrefix must be unique per call site.
    inline void DrawSearchFilter(
        ImGuiTextFilter& filter, const char* idPrefix, const char* hint, const float width)
    {
        ImGui::PushID(idPrefix);
        const float buttonWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(std::max(1.0f, width - buttonWidth - spacing));
        if (ImGui::InputTextWithHint("##filter_input", hint, filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
        {
            filter.Build();
        }
        // Force Cmd/Ctrl+A to select all while the field is focused. The field's
        // native select-all can be pre-empted by the editor's global shortcut routes,
        // so handle it explicitly. ConfigMacOSXBehaviors swaps Cmd<>Ctrl, so testing
        // KeyCtrl||KeySuper covers both chords on every platform.
        if (ImGui::IsItemActive())
        {
            const ImGuiIO& io = ImGui::GetIO();
            if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
                {
                    state->ReloadUserBufAndSelectAll();
                }
            }
        }
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginDisabled(!filter.IsActive());
        if (ImGui::Button("x##filter_clear", ImVec2{buttonWidth, buttonWidth}))
        {
            filter.Clear();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    // Invisible drag handle for resizing a docked panel edge. resize() receives the
    // mouse delta and returns whether the layout actually changed this frame.
    inline bool DrawDockResizeHandle(
        const char* id,
        const ImVec2 pos,
        const ImVec2 size,
        const ImGuiMouseCursor cursor,
        const std::function<bool(ImVec2)>& resize)
    {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
        {
            ImGui::SetMouseCursor(cursor);
        }

        bool changed = false;
        if (active)
        {
            changed = resize(ImGui::GetIO().MouseDelta);
        }

        const ImVec4 color =
            active ? ImVec4{0.36f, 0.58f, 0.92f, 0.95f}
                   : hovered ? ImVec4{0.36f, 0.58f, 0.92f, 0.70f}
                             : ImVec4{0.28f, 0.32f, 0.38f, 0.55f};
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, ImVec2{pos.x + size.x, pos.y + size.y}, ImGui::GetColorU32(color));
        return changed;
    }
} // namespace sage::editor

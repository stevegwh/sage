#include "EditorGui.hpp"

#include "EditorGuiInternal.hpp"
#include "engine/Settings.hpp"

#include "imgui.h"

#include <algorithm>

namespace sage::editor
{
    namespace
    {
        constexpr std::size_t MAX_CONSOLE_ENTRIES = 2000;

        const char* LevelLabel(const CSharpLogLevel level)
        {
            switch (level)
            {
            case CSharpLogLevel::Info:
                return "Info";
            case CSharpLogLevel::Warning:
                return "Warning";
            case CSharpLogLevel::Error:
                return "Error";
            }
            return "Info";
        }

        ImVec4 LevelColor(const CSharpLogLevel level)
        {
            switch (level)
            {
            case CSharpLogLevel::Info:
                return ToImGuiColor(EDITOR_TEXT);
            case CSharpLogLevel::Warning:
                return ImVec4{0.96f, 0.72f, 0.24f, 1.0f};
            case CSharpLogLevel::Error:
                return ImVec4{0.96f, 0.32f, 0.32f, 1.0f};
            }
            return ToImGuiColor(EDITOR_TEXT);
        }
    } // namespace

    void EditorGui::DrawConsoleWindow()
    {
        if (!settings) return;

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float leftDockWidth = dockLayout ? dockLayout->leftDockWidth : EDITOR_LEFT_DOCK_DEFAULT_WIDTH;
        const float rightDockWidth = dockLayout ? dockLayout->rightDockWidth : EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        const float drawerHeight = dockLayout ? dockLayout->assetDrawerHeight : EDITOR_ASSET_DRAWER_DEFAULT_HEIGHT;
        const float left = settings->ScaleValueWidth(leftDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float right = settings->ScaleValueWidth(rightDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float height = settings->ScaleValueHeight(drawerHeight);
        const float bottomMargin = settings->ScaleValueHeight(EDITOR_SCENE_VIEW_PADDING);
        const ImVec2 windowPos{viewportOffset.x + left, viewportOffset.y + viewport.y - height - bottomMargin};
        const ImVec2 windowSize{std::max(1.0f, viewport.x - left - right), height};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        PushEditorWindowStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 7.0f});

        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("C# Console", nullptr, windowFlags))
        {
            if (ImGui::Button("Clear")) ClearConsole();
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &consoleAutoScroll);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu messages", consoleEntries.size());
            ImGui::Separator();

            if (ImGui::BeginChild("csharp_console_messages", ImVec2{0.0f, 0.0f}, false))
            {
                for (const auto& entry : consoleEntries)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.level));
                    ImGui::Text("[%s]", LevelLabel(entry.level));
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(entry.message.c_str());
                    ImGui::PopTextWrapPos();
                }

                if (consoleAutoScroll && consoleScrollToBottom) ImGui::SetScrollHereY(1.0f);
                consoleScrollToBottom = false;
            }
            ImGui::EndChild();

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##console_resize",
                    ImVec2{windowPos.x, handleTop},
                    ImVec2{windowSize.x, DOCK_RESIZE_HANDLE_THICKNESS},
                    ImGuiMouseCursor_ResizeNS,
                    [this, viewport](const ImVec2 delta) {
                        const float logicalDelta =
                            delta.y * Settings::TARGET_SCREEN_HEIGHT / std::max(1.0f, viewport.y);
                        return SetAssetDrawerHeight(*dockLayout, dockLayout->assetDrawerHeight - logicalDelta);
                    });
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        PopEditorWindowStyle();
    }

    void EditorGui::AddConsoleEntry(const CSharpLogLevel level, const std::string_view message)
    {
        if (consoleEntries.size() == MAX_CONSOLE_ENTRIES) consoleEntries.erase(consoleEntries.begin());
        consoleEntries.push_back({.level = level, .message = std::string{message}});
        consoleScrollToBottom = true;
    }

    void EditorGui::ClearConsole()
    {
        consoleEntries.clear();
        consoleScrollToBottom = false;
    }
} // namespace sage::editor

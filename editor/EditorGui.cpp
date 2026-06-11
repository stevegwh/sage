#include "EditorGui.hpp"

#include "EditorGuiInternal.hpp"
#include "InspectorFieldUI.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <algorithm>
#include <cfloat>
#include <utility>

namespace sage::editor
{
    namespace
    {
        void DrawTextFit(
            const Font font,
            const std::string& text,
            const Vector2 position,
            const float maxWidth,
            int fontSize,
            const Color color)
        {
            while (fontSize > 12 && MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x > maxWidth)
            {
                --fontSize;
            }

            DrawTextEx(font, text.c_str(), {position.x + 1.0f, position.y + 1.0f}, fontSize, 1.0f, BLACK);
            DrawTextEx(font, text.c_str(), position, fontSize, 1.0f, color);
        }

    } // namespace

    void EditorGui::StartImGui()
    {
        rlImGuiBegin();
    }

    void EditorGui::EndImGui()
    {
        rlImGuiEnd();
    }

    EditorGui::InspectorEditResult EditorGui::DrawInspectorWindow()
    {
        if (!settings) return {};

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float mainMenuHeight = ImGui::GetFrameHeight();
        const float rightDockWidth = dockLayout ? dockLayout->rightDockWidth : EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        const float width = settings->ScaleValueWidth(rightDockWidth);
        const ImVec2 windowPos{
            viewportOffset.x + viewport.x - width,
            viewportOffset.y + mainMenuHeight};
        const ImVec2 windowSize{width, std::max(1.0f, viewport.y - mainMenuHeight)};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImGuiColor(EDITOR_WINDOW_BACKGROUND));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EDITOR_TEXT));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.26f, 0.38f, 0.90f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.23f, 0.34f, 0.50f, 0.95f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 7.0f});

        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        InspectorComponentsResult inspectorResult;
        bool addScriptClicked = false;
        if (ImGui::Begin("Inspector", nullptr, windowFlags))
        {
            ImGui::Text("Selected: %s", inspectorSelectedEntity.c_str());
            ImGui::Separator();

            if (inspectedComponents.empty())
            {
                ImGui::TextDisabled("No component data");
            }
            else
            {
                inspectorResult = DrawInspectorComponents(inspectedComponents);
                addScriptClicked = drawAddComponentControls();
            }

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##inspector_resize",
                    ImVec2{windowPos.x, handleTop},
                    ImVec2{DOCK_RESIZE_HANDLE_THICKNESS, std::max(1.0f, windowSize.y - ImGui::GetFrameHeight())},
                    ImGuiMouseCursor_ResizeEW,
                    [this, viewport](const ImVec2 delta) {
                        const float logicalDelta =
                            delta.x * Settings::TARGET_SCREEN_WIDTH / std::max(1.0f, viewport.x);
                        return SetRightDockWidth(*dockLayout, dockLayout->rightDockWidth - logicalDelta);
                    });
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
        return {
            .changed = inspectorResult.changed,
            .began = inspectorResult.began,
            .committed = inspectorResult.committed,
            .addScriptClicked = addScriptClicked,
            .removeComponent = std::move(inspectorResult.removeComponent)};
    }

    // "Add Component" button + popup. Only Script is addable for now; the host
    // (EditorScene) opens a file dialog to pick the lua script.
    bool EditorGui::drawAddComponentControls()
    {
        bool addScriptClicked = false;

        ImGui::Spacing();
        if (ImGui::Button("Add Component", ImVec2{-FLT_MIN, 0.0f}))
        {
            ImGui::OpenPopup("##add_component");
        }
        if (!ImGui::BeginPopup("##add_component")) return addScriptClicked;

        addScriptClicked = ImGui::MenuItem("Script");
        ImGui::EndPopup();
        return addScriptClicked;
    }

    void EditorGui::DrawDeleteConfirmationModal()
    {
        if (deleteConfirmationVisible)
        {
            ImGui::OpenPopup("Confirm Delete");
        }

        bool open = deleteConfirmationVisible;
        ImGui::SetNextWindowSize(ImVec2{420.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Confirm Delete", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", deleteConfirmationPrompt.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2{120.0f, 0.0f}))
            {
                pendingDeleteConfirmationAction = DeleteConfirmationAction::Confirm;
                deleteConfirmationVisible = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                pendingDeleteConfirmationAction = DeleteConfirmationAction::Cancel;
                deleteConfirmationVisible = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!open)
        {
            deleteConfirmationVisible = false;
        }
    }

    void EditorGui::SetOverlayStatus(const std::string& mode, const std::string& cursor) const
    {
        modeStatus = mode;
        cursorStatus = cursor;
    }

    void EditorGui::SetSaveStatus(const std::string& status, const bool hasUnsavedChanges) const
    {
        saveStatus = status;
        sceneHasUnsavedChanges = hasUnsavedChanges;
    }

    void EditorGui::SetSceneName(const std::string& sceneName) const
    {
        sceneNameStatus = sceneName;
    }

    void EditorGui::SetInspector(
        const std::string& selectedEntity, const std::vector<InspectedComponent>& inspectedComponents)
    {
        inspectorSelectedEntity = selectedEntity;
        this->inspectedComponents = inspectedComponents;
    }

    void EditorGui::DrawSceneViewInfo() const
    {
        if (!settings) return;

        const auto renderViewport = settings->GetRenderViewportScreenRect();
        const float x = renderViewport.x + settings->ScaleValueWidth(16.0f);
        const float y = renderViewport.y + settings->ScaleValueHeight(14.0f);
        const float maxWidth = std::max(1.0f, renderViewport.width - settings->ScaleValueWidth(32.0f));
        const int titleSize = std::max(22, static_cast<int>(settings->ScaleValueMaintainRatio(22.0f)));
        const int metaSize = std::max(16, static_cast<int>(settings->ScaleValueMaintainRatio(16.0f)));
        const Font titleFont =
            ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-Bold.ttf");
        const Font metaFont =
            ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-SemiBold.ttf");

        const std::string title = sceneHasUnsavedChanges ? sceneNameStatus + " *" : sceneNameStatus;
        DrawTextFit(titleFont, title, {x, y}, maxWidth, titleSize, EDITOR_TEXT);
        DrawTextFit(
            metaFont,
            "Mode: " + modeStatus + "  |  Cursor: " + cursorStatus,
            {x, y + settings->ScaleValueHeight(28.0f)},
            maxWidth,
            metaSize,
            Color{202, 211, 224, 255});
        if (!saveStatus.empty())
        {
            DrawTextFit(
                metaFont,
                saveStatus,
                {x, y + settings->ScaleValueHeight(56.0f)},
                maxWidth,
                metaSize,
                sceneHasUnsavedChanges ? Color{252, 211, 77, 255} : Color{134, 239, 172, 255});
        }
    }

    void EditorGui::ShowDeleteConfirmation(const std::string& selectedEntity)
    {
        deleteConfirmationPrompt = "Delete " + selectedEntity + "?";
        deleteConfirmationVisible = true;
    }

    void EditorGui::HideDeleteConfirmation()
    {
        deleteConfirmationVisible = false;
    }

    bool EditorGui::IsDeleteConfirmationVisible() const
    {
        return deleteConfirmationVisible;
    }

    bool EditorGui::WantsMouseCapture() const
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    }

    bool EditorGui::WantsKeyboardCapture() const
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
    }

    bool EditorGui::ConsumeDockLayoutChanged()
    {
        const bool changed = dockLayoutChanged;
        dockLayoutChanged = false;
        return changed;
    }

    EditorGui::DeleteConfirmationAction EditorGui::ConsumeDeleteConfirmationAction()
    {
        const auto action = pendingDeleteConfirmationAction;
        pendingDeleteConfirmationAction = DeleteConfirmationAction::None;
        return action;
    }

    EditorGui::EditorGui(
        Settings* _settings,
        EditorDockLayout* _dockLayout,
        const std::vector<AssetEntry>& assets,
        const std::function<void(std::size_t)>& onAssetSelected,
        const std::function<AssetRenameResult(std::size_t, const std::string&)>& onAssetRename,
        const std::function<void(std::filesystem::path)>& onFlatpackSelected,
        const std::function<void(const SceneSelectionRequest&)>& onSceneObjectSelected,
        const std::function<void(const HierarchyMoveRequest&)>& onHierarchyMove,
        ModelDefaultCallbacks callbacks)
        : settings(_settings),
          dockLayout(_dockLayout),
          onAssetSelectedCb(onAssetSelected),
          onAssetRenameCb(onAssetRename),
          onFlatpackSelectedCb(onFlatpackSelected),
          onSceneObjectSelectedCb(onSceneObjectSelected),
          onHierarchyMoveCb(onHierarchyMove),
          modelDefaultCallbacks(std::move(callbacks))
    {
        assetEntries = assets;
        assetThumbnails.reserve(assetEntries.size());
        for (const auto& asset : assetEntries)
        {
            assetThumbnails.push_back(createAssetThumbnail(asset));
        }
    }

    EditorGui::~EditorGui()
    {
        for (auto& thumbnail : assetThumbnails)
        {
            if (thumbnail.id != 0)
            {
                UnloadRenderTexture(thumbnail);
            }
        }
    }
} // namespace sage::editor

#include "EditorGui.hpp"
#include "engine/Colors.hpp"

#include "EditorGuiInternal.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"
#include "InspectorFieldUI.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <algorithm>
#include <cfloat>
#include <iterator>
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
            while (fontSize > 12 &&
                   MeasureTextEx(font, text.c_str(), static_cast<float>(fontSize), 1.0f).x > maxWidth)
            {
                --fontSize;
            }

            DrawTextEx(
                font,
                text.c_str(),
                {.x = position.x + 1.0f, .y = position.y + 1.0f},
                static_cast<float>(fontSize),
                1.0f,
                sage::colors::BLACK_COLOR);
            DrawTextEx(font, text.c_str(), position, static_cast<float>(fontSize), 1.0f, color);
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

    EditorGui::InspectorEditResult EditorGui::DrawInspectorWindow(
        const std::function<void()>& drawTerrainTools,
        const std::function<void()>& frameRuntimeObject,
        const std::function<void()>& drawExtraInspectorFields)
    {
        if (!settings) return {};

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float mainMenuHeight = ImGui::GetFrameHeight();
        const float rightDockWidth = dockLayout ? dockLayout->rightDockWidth : EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        const float width = settings->ScaleValueWidth(rightDockWidth);
        ImVec2 windowPos{viewportOffset.x + viewport.x - width, viewportOffset.y + mainMenuHeight};
        ImVec2 windowSize{width, std::max(1.0f, viewport.y - mainMenuHeight)};

        PushEditorWindowStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 7.0f});

        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        if (drawTerrainTools)
        {
            // Fit the temporary tools pane to its contents, keeping room for the inspector below.
            ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2{width, 0.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2{width, ImGui::GetFrameHeight()}, ImVec2{width, windowSize.y * 0.6f});
            if (ImGui::Begin("Terrain Tools", nullptr, windowFlags | ImGuiWindowFlags_AlwaysAutoResize))
                drawTerrainTools();
            const float toolsHeight = ImGui::GetWindowHeight();
            ImGui::End();
            windowPos.y += toolsHeight;
            windowSize.y = std::max(1.0f, windowSize.y - toolsHeight);
        }
        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        InspectorComponentsResult inspectorResult;
        std::optional<EditorComponentId> addComponent;
        if (ImGui::Begin("Inspector", nullptr, windowFlags))
        {
            if (runtimeInspection) ImGui::TextDisabled("PLAY - Live values (read-only)");
            ImGui::TextWrapped("Selected: %s", inspectorSelectedEntity.c_str());
            if (runtimeInspection && !inspectedComponents.empty() && frameRuntimeObject &&
                ImGui::Button("Frame Object"))
                frameRuntimeObject();
            ImGui::Separator();
            if (runtimeInspection) drawRuntimeScript();

            if (inspectedComponents.empty())
            {
                if (runtimeInspection)
                    ImGui::TextWrapped("Select a runtime object in the Hierarchy to inspect its live values.");
                else
                    ImGui::TextDisabled("No component data");
            }
            else
            {
                inspectorResult = DrawInspectorComponents(inspectedComponents, runtimeInspection);
                if (inspectorResult.moveComponent.has_value())
                {
                    const auto& move = *inspectorResult.moveComponent;
                    moveInspectorComponent(move.dragged, move.target, move.after);
                }
                if (!runtimeInspection) addComponent = drawAddComponentControls();
            }

            if (!runtimeInspection && drawExtraInspectorFields) drawExtraInspectorFields();

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

        ImGui::PopStyleVar(2);
        PopEditorWindowStyle();
        return {
            .changed = inspectorResult.changed,
            .began = inspectorResult.began,
            .committed = inspectorResult.committed,
            .addComponent = addComponent,
            .editModelDefaultsClicked = inspectorResult.editModelDefaults,
            .selectedModelKey = std::move(inspectorResult.selectedModelKey),
            .selectedMaterial =
                inspectorResult.selectedMaterial.has_value()
                    ? std::optional<InspectorEditResult::MaterialSelection>{InspectorEditResult::MaterialSelection{
                          .materialIndex = inspectorResult.selectedMaterial->materialIndex,
                          .materialKey = std::move(inspectorResult.selectedMaterial->materialKey)}}
                    : std::nullopt,
            .selectScriptFile = inspectorResult.selectScriptFile,
            .openScriptFile = inspectorResult.openScriptFile,
            .selectShaderFile = inspectorResult.selectShaderFile,
            .removeComponent = std::move(inspectorResult.removeComponent)};
    }

    // "Add Component" button + popup. The host (EditorScene) performs the add:
    // Script opens a file dialog, Animation attaches clips from the entity's model.
    std::optional<EditorComponentId> EditorGui::drawAddComponentControls()
    {
        ImGui::Spacing();
        if (ImGui::Button("Add Component", ImVec2{-FLT_MIN, 0.0f}))
        {
            ImGui::OpenPopup("##add_component");
        }
        if (!ImGui::BeginPopup("##add_component")) return std::nullopt;

        std::optional<EditorComponentId> clicked;
        for (const auto& option : addComponentOptions)
        {
            if (option.separatorBefore) ImGui::Separator();
            if (ImGui::MenuItem(option.displayName.c_str(), nullptr, false, option.enabled))
            {
                clicked = option.componentId;
            }
            if (!option.enabled && !option.disabledReason.empty() &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", option.disabledReason.c_str());
            }
        }
        ImGui::EndPopup();
        return clicked;
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

    void EditorGui::SetOverlayStatus(
        const std::string& mode, const std::string& cursor, const std::string& camera) const
    {
        modeStatus = mode;
        cursorStatus = cursor;
        cameraStatus = camera;
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

    void EditorGui::SetRuntimeInspection(const bool enabled)
    {
        if (runtimeInspection == enabled) return;
        runtimeInspection = enabled;
        runtimeScript.SetObject();
        hierarchyFilter.Clear();
        focusedHierarchyEntity.reset();
        pendingHierarchyContextEntity.reset();
        pendingHierarchyClick.reset();
        hierarchySelectionAnchor = entt::null;
    }

    void EditorGui::SetInspector(
        const std::string& selectedEntity,
        const std::vector<InspectedComponent>& inspectedComponents,
        std::vector<AddComponentOption> addComponentOptions)
    {
        inspectorSelectedEntity = selectedEntity;
        this->inspectedComponents = inspectedComponents;
        this->addComponentOptions = std::move(addComponentOptions);
        syncInspectorComponentOrder();
        applyInspectorComponentOrder();
    }

    void EditorGui::syncInspectorComponentOrder()
    {
        std::vector<EditorComponentId> uniqueOrder;
        uniqueOrder.reserve(inspectorComponentOrder.size() + inspectedComponents.size());
        for (const auto componentId : inspectorComponentOrder)
        {
            if (std::ranges::find(uniqueOrder, componentId) == uniqueOrder.end())
            {
                uniqueOrder.push_back(componentId);
            }
        }

        for (const auto& component : inspectedComponents)
        {
            if (std::ranges::find(uniqueOrder, component.componentId) == uniqueOrder.end())
            {
                uniqueOrder.push_back(component.componentId);
            }
        }
        inspectorComponentOrder = std::move(uniqueOrder);
    }

    void EditorGui::applyInspectorComponentOrder()
    {
        auto orderPosition = [this](const EditorComponentId componentId) -> std::optional<std::size_t> {
            const auto it = std::ranges::find(inspectorComponentOrder, componentId);
            if (it == inspectorComponentOrder.end()) return std::nullopt;
            return static_cast<std::size_t>(std::distance(inspectorComponentOrder.begin(), it));
        };

        std::ranges::stable_sort(
            inspectedComponents, [&](const InspectedComponent& lhs, const InspectedComponent& rhs) {
                const auto lhsIndex = orderPosition(lhs.componentId);
                const auto rhsIndex = orderPosition(rhs.componentId);
                if (lhsIndex.has_value() && rhsIndex.has_value()) return *lhsIndex < *rhsIndex;
                if (lhsIndex.has_value()) return true;
                if (rhsIndex.has_value()) return false;
                return false;
            });
    }

    void EditorGui::moveInspectorComponent(
        const EditorComponentId dragged, const EditorComponentId target, const bool after)
    {
        if (dragged == target) return;

        syncInspectorComponentOrder();
        std::erase(inspectorComponentOrder, dragged);

        auto targetIt = std::ranges::find(inspectorComponentOrder, target);
        if (targetIt == inspectorComponentOrder.end())
        {
            inspectorComponentOrder.push_back(dragged);
        }
        else
        {
            if (after) ++targetIt;
            inspectorComponentOrder.insert(targetIt, dragged);
        }

        applyInspectorComponentOrder();
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
        DrawTextFit(titleFont, title, {.x = x, .y = y}, maxWidth, titleSize, EDITOR_TEXT);
        DrawTextFit(
            metaFont,
            "Mode: " + modeStatus + "  |  Cursor: " + cursorStatus,
            {.x = x, .y = y + settings->ScaleValueHeight(28.0f)},
            maxWidth,
            metaSize,
            Color{.r = 202, .g = 211, .b = 224, .a = 255});
        DrawTextFit(
            metaFont,
            "Camera: " + cameraStatus,
            {.x = x, .y = y + settings->ScaleValueHeight(56.0f)},
            maxWidth,
            metaSize,
            Color{.r = 202, .g = 211, .b = 224, .a = 255});
        if (!saveStatus.empty())
        {
            DrawTextFit(
                metaFont,
                saveStatus,
                {.x = x, .y = y + settings->ScaleValueHeight(84.0f)},
                maxWidth,
                metaSize,
                sceneHasUnsavedChanges ? Color{.r = 252, .g = 211, .b = 77, .a = 255}
                                       : Color{.r = 134, .g = 239, .b = 172, .a = 255});
        }
    }

    void EditorGui::SetSceneTabs(SceneTabState state)
    {
        sceneTabs = std::move(state);
    }

    EditorGui::SceneTabBarResult EditorGui::DrawSceneTabBar()
    {
        SceneTabBarResult result;
        if (!settings) return result;

        const auto renderViewport = settings->GetRenderViewportScreenRect();
        ImGui::SetNextWindowPos(
            ImVec2{renderViewport.x + renderViewport.width * 0.5f, renderViewport.y},
            ImGuiCond_Always,
            ImVec2{0.5f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.85f);
        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##sceneTabs", nullptr, windowFlags))
        {
            if (ImGui::BeginTabBar("##sceneTabBar"))
            {
                ImGuiTabItemFlags mapFlags =
                    sceneTabs.mapDirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                if (!sceneTabs.flatpackOpen && !sceneTabs.particleOpen) mapFlags |= ImGuiTabItemFlags_SetSelected;
                const std::string mapLabel =
                    (sceneTabs.mapLabel.empty() ? "Map" : sceneTabs.mapLabel) + "###mapTab";
                if (ImGui::BeginTabItem(mapLabel.c_str(), nullptr, mapFlags))
                {
                    ImGui::EndTabItem();
                }
                // While a flatpack owns the scene its tab is force-selected, so a
                // click on the map tab is read directly off the item instead.
                if ((sceneTabs.flatpackOpen || sceneTabs.particleOpen) && ImGui::IsItemClicked())
                {
                    result.mapSelected = true;
                }

                if (sceneTabs.flatpackOpen)
                {
                    bool keepOpen = true;
                    ImGuiTabItemFlags flatpackFlags =
                        sceneTabs.particleOpen ? ImGuiTabItemFlags_None : ImGuiTabItemFlags_SetSelected;
                    if (sceneTabs.flatpackDirty) flatpackFlags |= ImGuiTabItemFlags_UnsavedDocument;
                    const std::string flatpackLabel = sceneTabs.flatpackLabel + "###flatpackTab";
                    if (ImGui::BeginTabItem(flatpackLabel.c_str(), &keepOpen, flatpackFlags))
                    {
                        ImGui::EndTabItem();
                    }
                    if (ImGui::IsItemClicked()) result.flatpackSelected = true;
                    if (!keepOpen) result.flatpackCloseRequested = true;
                }
                if (sceneTabs.particleOpen)
                {
                    bool keepOpen = true;
                    ImGuiTabItemFlags flags = ImGuiTabItemFlags_SetSelected;
                    if (sceneTabs.particleDirty) flags |= ImGuiTabItemFlags_UnsavedDocument;
                    const auto label = sceneTabs.particleLabel + "###particleTab";
                    if (ImGui::BeginTabItem(label.c_str(), &keepOpen, flags)) ImGui::EndTabItem();
                    if (!keepOpen) result.particleCloseRequested = true;
                }
                if (sceneTabs.canvasOpen)
                {
                    bool keepOpen = true;
                    const auto flags =
                        sceneTabs.canvasDirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                    const auto label = sceneTabs.canvasLabel + "###canvasTab";
                    if (ImGui::BeginTabItem(label.c_str(), &keepOpen, flags)) ImGui::EndTabItem();
                    if (ImGui::IsItemClicked()) result.canvasSelected = true;
                    if (!keepOpen) result.canvasCloseRequested = true;
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        return result;
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
        const std::function<void(std::filesystem::path)>& onFlatpackEdit,
        const std::function<FlatpackRenameResult(const std::filesystem::path&, const std::string&)>&
            onFlatpackRename,
        const std::function<void(const std::filesystem::path&)>& onFlatpackDelete,
        const std::function<void(const SceneSelectionRequest&)>& onSceneObjectSelected,
        const std::function<void(const HierarchyMoveRequest&)>& onHierarchyMove,
        ModelDefaultCallbacks callbacks)
        : settings(_settings),
          dockLayout(_dockLayout),
          assetEntries(assets),
          onAssetSelectedCb(onAssetSelected),
          onAssetRenameCb(onAssetRename),
          onFlatpackSelectedCb(onFlatpackSelected),
          onFlatpackEditCb(onFlatpackEdit),
          onFlatpackRenameCb(onFlatpackRename),
          onFlatpackDeleteCb(onFlatpackDelete),
          onSceneObjectSelectedCb(onSceneObjectSelected),
          onHierarchyMoveCb(onHierarchyMove),
          modelDefaultCallbacks(std::move(callbacks))
    {
        assetThumbnails.resize(assetEntries.size());
        auto& resources = ResourceManager::GetInstance();
        materialKeys = resources.GetMaterialKeys();
        materialThumbnails.resize(materialKeys.size());
        imageKeys = resources.GetImageKeys();
        imageThumbnails.resize(imageKeys.size());
    }

    EditorGui::~EditorGui()
    {
        if (assetPreviewShader.id != 0) UnloadShader(assetPreviewShader);
        for (auto& thumbnail : assetThumbnails)
        {
            if (thumbnail.id != 0)
            {
                UnloadRenderTexture(thumbnail);
            }
        }
        for (auto& thumbnail : materialThumbnails)
            if (thumbnail.id != 0) UnloadRenderTexture(thumbnail);
        for (auto& thumbnail : imageThumbnails)
            if (thumbnail.id != 0) UnloadRenderTexture(thumbnail);
        for (auto& thumbnail : flatpackThumbnails)
        {
            if (thumbnail.id != 0)
            {
                UnloadRenderTexture(thumbnail);
            }
        }
    }
} // namespace sage::editor

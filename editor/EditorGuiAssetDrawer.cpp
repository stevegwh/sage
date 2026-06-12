#include "EditorGui.hpp"

#include "EditorGuiInternal.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "raylib.h"
#include "raymath.h"
#include "rlImGui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sage::editor
{
    namespace
    {
        constexpr int THUMBNAIL_SIZE = 128;
        constexpr float ASSET_TILE_WIDTH = 152.0f;
        constexpr float ASSET_TILE_HEIGHT = 188.0f;
        constexpr float FLATPACK_TILE_HEIGHT = 116.0f;
        constexpr float ASSET_DEFAULTS_PANEL_WIDTH = 260.0f;
        constexpr int PREVIEW_LIGHT_DIRECTIONAL = 0;
        constexpr int PREVIEW_LIGHT_POINT = 1;
        constexpr float PREVIEW_GAMMA = 1.9f;
        constexpr Color PREVIEW_LIGHT_COLOR = {255, 244, 214, 255};
        constexpr const char* ASSET_RENAME_POPUP = "Rename Asset File";
        constexpr const char* FLATPACK_RENAME_POPUP = "Rename Flatpack";
        constexpr const char* FLATPACK_DELETE_POPUP = "Delete Flatpack";

        Shader LoadThumbnailShader()
        {
            auto shader = ResourceManager::GetInstance().ShaderLoad(
                "resources/shaders/custom/ubershader.vs", "resources/shaders/custom/ubershader.fs");
            shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, "emissionMap");
            return shader;
        }

        UberShaderComponent CreateThumbnailUberComponent(const ModelView& model, const Shader shader)
        {
            UberShaderComponent uber(static_cast<unsigned int>(model.GetMaterialCount()));
            uber.shader = shader;
            uber.litLoc = GetShaderLocation(shader, "lit");
            uber.skinnedLoc = GetShaderLocation(shader, "skinned");
            uber.hasEmissiveTexLoc = GetShaderLocation(shader, "hasEmissionTex");
            uber.hasEmissiveColLoc = GetShaderLocation(shader, "hasEmissionCol");
            uber.colEmissiveLoc = GetShaderLocation(shader, "colEmission");
            uber.SetFlagAll(UberShaderComponent::Flags::Lit);

            const auto& rlmodel = model.GetRlModel();
            for (int i = 0; i < rlmodel.materialCount; ++i)
            {
                const auto emissionColor = rlmodel.materials[i].maps[MATERIAL_MAP_EMISSION].color;
                const auto emissionTexture = rlmodel.materials[i].maps[MATERIAL_MAP_EMISSION].texture.id;
                if (emissionColor.r != 0 || emissionColor.g != 0 || emissionColor.b != 0)
                {
                    uber.SetFlag(static_cast<unsigned int>(i), UberShaderComponent::Flags::EmissiveCol);
                }
                if (emissionTexture > 1)
                {
                    uber.SetFlag(static_cast<unsigned int>(i), UberShaderComponent::Flags::EmissiveTexture);
                }
            }

            return uber;
        }

        void SetThumbnailLight(
            const Shader shader,
            const int index,
            const int type,
            const bool enabled,
            const Vector3 position,
            const Vector3 target,
            const Color color,
            const float brightness,
            const float constant,
            const float linear,
            const float quadratic)
        {
            const int enabledInt = enabled ? 1 : 0;
            const float positionValue[3] = {position.x, position.y, position.z};
            const float targetValue[3] = {target.x, target.y, target.z};
            const float colorValue[4] = {
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                static_cast<float>(color.a) / 255.0f};

            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].enabled", index)),
                &enabledInt,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].type", index)),
                &type,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].position", index)),
                positionValue,
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].target", index)),
                targetValue,
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].color", index)),
                colorValue,
                SHADER_UNIFORM_VEC4);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].brightness", index)),
                &brightness,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].constant", index)),
                &constant,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].linear", index)),
                &linear,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].quadratic", index)),
                &quadratic,
                SHADER_UNIFORM_FLOAT);
        }

        void ConfigureThumbnailLighting(const Shader shader, const Camera3D& camera, const Vector3& center)
        {
            const float ambient[4] = {0.6f, 0.2f, 0.8f, 1.0f};
            SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient, SHADER_UNIFORM_VEC4);

            const int lightsCount = 2;
            SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
            SetShaderValue(shader, GetShaderLocation(shader, "gamma"), &PREVIEW_GAMMA, SHADER_UNIFORM_FLOAT);

            const float viewPos[3] = {camera.position.x, camera.position.y, camera.position.z};
            SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), viewPos, SHADER_UNIFORM_VEC3);

            SetThumbnailLight(
                shader,
                0,
                PREVIEW_LIGHT_POINT,
                true,
                camera.position,
                center,
                PREVIEW_LIGHT_COLOR,
                1.17f,
                1.0f,
                0.0f,
                0.0f);
            SetThumbnailLight(
                shader,
                1,
                PREVIEW_LIGHT_DIRECTIONAL,
                true,
                Vector3Add(center, {-3.0f, 4.0f, -4.0f}),
                center,
                Color{172, 202, 255, 255},
                0.23f,
                1.0f,
                0.0f,
                0.0f);
        }


    } // namespace

    void EditorGui::DrawAssetDrawerWindow()
    {
        if (!settings) return;

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float leftDockWidth = dockLayout ? dockLayout->leftDockWidth : EDITOR_LEFT_DOCK_DEFAULT_WIDTH;
        const float rightDockWidth = dockLayout ? dockLayout->rightDockWidth : EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        const float assetDrawerHeight =
            dockLayout ? dockLayout->assetDrawerHeight : EDITOR_ASSET_DRAWER_DEFAULT_HEIGHT;
        const float left = settings->ScaleValueWidth(leftDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float right = settings->ScaleValueWidth(rightDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float height = settings->ScaleValueHeight(assetDrawerHeight);
        const float bottomMargin = settings->ScaleValueHeight(EDITOR_SCENE_VIEW_PADDING);
        const ImVec2 windowPos{
            viewportOffset.x + left,
            viewportOffset.y + viewport.y - height - bottomMargin};
        const ImVec2 windowSize{std::max(1.0f, viewport.x - left - right), height};

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

        if (ImGui::Begin("Asset Drawer", nullptr, windowFlags))
        {
            const bool showDefaults = selectedAssetIndex.has_value();
            if (showDefaults && ImGui::BeginTable("asset_drawer_split", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Browser", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Defaults", ImGuiTableColumnFlags_WidthFixed, ASSET_DEFAULTS_PANEL_WIDTH);

                ImGui::TableNextColumn();
                if (ImGui::BeginTabBar("asset_tabs"))
                {
                    if (ImGui::BeginTabItem("Assets"))
                    {
                        currentTab = BrowserTab::Assets;
                        drawAssetGrid();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Flatpacks"))
                    {
                        currentTab = BrowserTab::Flatpacks;
                        drawFlatpackGrid();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::TableNextColumn();
                drawAssetDefaultsControls();
                ImGui::EndTable();
            }
            else if (ImGui::BeginTabBar("asset_tabs"))
            {
                if (ImGui::BeginTabItem("Assets"))
                {
                    currentTab = BrowserTab::Assets;
                    drawAssetGrid();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Flatpacks"))
                {
                    currentTab = BrowserTab::Flatpacks;
                    drawFlatpackGrid();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            drawAssetRenamePopup();
            drawFlatpackRenamePopup();
            drawFlatpackDeleteConfirmation();

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##asset_drawer_resize",
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

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
    }


    void EditorGui::SetAssetDefaultsStatus(
        const std::string& assetName,
        const std::string& modelDefaultHeight,
        const std::string& modelDefaultRotation,
        const std::string& modelDefaultScale)
    {
        assetDefaultsAssetName = assetName;
        assetDefaultsHeight = modelDefaultHeight;
        assetDefaultsRotation = modelDefaultRotation;
        assetDefaultsScale = modelDefaultScale;
    }


    void EditorGui::SetSelectedAsset(const std::optional<std::size_t> index)
    {
        selectedAssetIndex = index;
    }


    void EditorGui::SetFlatpacks(std::vector<FlatpackEntry> entries)
    {
        flatpackEntries = std::move(entries);
    }

    RenderTexture2D EditorGui::createAssetThumbnail(const AssetEntry& asset) const
    {
        auto thumbnail = LoadRenderTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        auto model = ResourceManager::GetInstance().GetModelView(asset.modelKey);
        const auto shader = LoadThumbnailShader();
        auto uber = CreateThumbnailUberComponent(model, shader);
        model.SetShader(shader);

        const auto bounds = model.CalcLocalBoundingBox();
        const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
        const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
        const float radius = std::max({std::fabs(size.x), std::fabs(size.y), std::fabs(size.z), 1.0f});

        Camera3D camera{};
        camera.position = Vector3Add(center, {radius * 1.35f, radius * 0.85f, radius * 1.65f});
        camera.target = center;
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 32.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        BeginTextureMode(thumbnail);
        ClearBackground(Color{244, 247, 251, 255});
        BeginMode3D(camera);
        ConfigureThumbnailLighting(shader, camera, center);
        model.DrawUber(&uber, Vector3Zero(), {0.0f, 1.0f, 0.0f}, 0.0f, Vector3One(), WHITE);
        EndMode3D();
        EndTextureMode();

        return thumbnail;
    }

    void EditorGui::openAssetRenamePopup(const std::size_t index)
    {
        if (index >= assetEntries.size()) return;

        const auto& asset = assetEntries[index];
        const auto renamePath = !asset.sourcePath.empty() ? asset.sourcePath : asset.defaultsPath;
        renamingAssetIndex = index;
        assetRenameInput = renamePath.filename().string();
        assetRenameStatus.clear();
        assetRenamePopupOpenRequested = true;
    }

    void EditorGui::drawAssetRenamePopup()
    {
        if (!renamingAssetIndex.has_value()) return;
        if (*renamingAssetIndex >= assetEntries.size())
        {
            renamingAssetIndex.reset();
            return;
        }

        if (assetRenamePopupOpenRequested)
        {
            ImGui::OpenPopup(ASSET_RENAME_POPUP);
            assetRenamePopupOpenRequested = false;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2{430.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(ASSET_RENAME_POPUP, &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto index = *renamingAssetIndex;
            const auto& asset = assetEntries[index];
            const auto renamePath = !asset.sourcePath.empty() ? asset.sourcePath : asset.defaultsPath;

            ImGui::TextWrapped("%s", asset.displayName.c_str());
            ImGui::TextDisabled("%s", renamePath.parent_path().string().c_str());
            ImGui::Spacing();

            ImGui::SetNextItemWidth(390.0f);
            const bool enterPressed =
                ImGui::InputText("File name", &assetRenameInput, ImGuiInputTextFlags_EnterReturnsTrue);

            if (!assetRenameStatus.empty())
            {
                ImGui::TextWrapped("%s", assetRenameStatus.c_str());
            }

            ImGui::Spacing();
            const bool renamePressed = ImGui::Button("Rename", ImVec2{120.0f, 0.0f});
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                renamingAssetIndex.reset();
                assetRenameInput.clear();
                assetRenameStatus.clear();
                assetRenamePopupOpenRequested = false;
                ImGui::CloseCurrentPopup();
            }

            if ((enterPressed || renamePressed) && onAssetRenameCb)
            {
                auto result = onAssetRenameCb(index, assetRenameInput);
                assetRenameStatus = std::move(result.message);
                if (result.renamed)
                {
                    if (result.updatedEntry.has_value())
                    {
                        assetEntries[index] = std::move(*result.updatedEntry);
                    }
                    renamingAssetIndex.reset();
                    assetRenameInput.clear();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            renamingAssetIndex.reset();
            assetRenameInput.clear();
            assetRenameStatus.clear();
            assetRenamePopupOpenRequested = false;
        }
    }

    void EditorGui::openFlatpackRenamePopup(const std::size_t index)
    {
        if (index >= flatpackEntries.size()) return;

        renamingFlatpackIndex = index;
        flatpackRenameInput = flatpackEntries[index].displayName;
        flatpackRenameStatus.clear();
        flatpackRenamePopupOpenRequested = true;
    }

    void EditorGui::drawFlatpackRenamePopup()
    {
        if (!renamingFlatpackIndex.has_value()) return;
        if (*renamingFlatpackIndex >= flatpackEntries.size())
        {
            renamingFlatpackIndex.reset();
            return;
        }

        if (flatpackRenamePopupOpenRequested)
        {
            ImGui::OpenPopup(FLATPACK_RENAME_POPUP);
            flatpackRenamePopupOpenRequested = false;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2{430.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(FLATPACK_RENAME_POPUP, &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto& flatpack = flatpackEntries[*renamingFlatpackIndex];

            ImGui::TextWrapped("%s", flatpack.displayName.c_str());
            ImGui::TextDisabled("%s", flatpack.path.parent_path().string().c_str());
            ImGui::Spacing();

            ImGui::SetNextItemWidth(390.0f);
            const bool enterPressed =
                ImGui::InputText("New name", &flatpackRenameInput, ImGuiInputTextFlags_EnterReturnsTrue);

            if (!flatpackRenameStatus.empty())
            {
                ImGui::TextWrapped("%s", flatpackRenameStatus.c_str());
            }

            ImGui::Spacing();
            const bool renamePressed = ImGui::Button("Rename", ImVec2{120.0f, 0.0f});
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                renamingFlatpackIndex.reset();
                flatpackRenameInput.clear();
                flatpackRenameStatus.clear();
                ImGui::CloseCurrentPopup();
            }

            if ((enterPressed || renamePressed) && onFlatpackRenameCb)
            {
                // The rename callback refreshes the catalog (SetFlatpacks swaps
                // out flatpackEntries), so copy the path before invoking.
                const auto path = flatpack.path;
                auto result = onFlatpackRenameCb(path, flatpackRenameInput);
                flatpackRenameStatus = std::move(result.message);
                if (result.renamed)
                {
                    renamingFlatpackIndex.reset();
                    flatpackRenameInput.clear();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            renamingFlatpackIndex.reset();
            flatpackRenameInput.clear();
            flatpackRenameStatus.clear();
            flatpackRenamePopupOpenRequested = false;
        }
    }

    void EditorGui::openFlatpackDeleteConfirmation(const std::size_t index)
    {
        if (index >= flatpackEntries.size()) return;

        deletingFlatpackIndex = index;
        flatpackDeletePopupOpenRequested = true;
    }

    void EditorGui::drawFlatpackDeleteConfirmation()
    {
        if (!deletingFlatpackIndex.has_value()) return;
        if (*deletingFlatpackIndex >= flatpackEntries.size())
        {
            deletingFlatpackIndex.reset();
            return;
        }

        if (flatpackDeletePopupOpenRequested)
        {
            ImGui::OpenPopup(FLATPACK_DELETE_POPUP);
            flatpackDeletePopupOpenRequested = false;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2{430.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(FLATPACK_DELETE_POPUP, &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto& flatpack = flatpackEntries[*deletingFlatpackIndex];

            ImGui::TextWrapped(
                "Delete '%s'? The file is removed from disk. Instances already placed in maps are unaffected.",
                flatpack.displayName.c_str());
            ImGui::TextDisabled("%s", flatpack.path.string().c_str());
            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2{120.0f, 0.0f}))
            {
                // The delete callback refreshes the catalog (SetFlatpacks swaps
                // out flatpackEntries), so copy the path before invoking.
                const auto path = flatpack.path;
                deletingFlatpackIndex.reset();
                ImGui::CloseCurrentPopup();
                if (onFlatpackDeleteCb) onFlatpackDeleteCb(path);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                deletingFlatpackIndex.reset();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            deletingFlatpackIndex.reset();
            flatpackDeletePopupOpenRequested = false;
        }
    }

    void EditorGui::drawAssetDefaultsControls()
    {
        if (ImGui::BeginChild("asset_defaults", ImVec2{0.0f, 0.0f}, true))
        {
            ImGui::TextUnformatted("Asset Defaults");
            ImGui::Separator();
            ImGui::TextWrapped("Asset: %s", assetDefaultsAssetName.c_str());

            auto adjustmentRow = [](const char* label,
                                    const std::string& value,
                                    const std::function<void()>& down,
                                    const std::function<void()>& up) {
                ImGui::PushID(label);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine(70.0f);
                if (ImGui::SmallButton("-") && down) down();
                ImGui::SameLine();
                auto displayValue = value;
                ImGui::SetNextItemWidth(72.0f);
                ImGui::InputText("##value", &displayValue, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::SmallButton("+") && up) up();
                ImGui::PopID();
            };

            adjustmentRow("Z", assetDefaultsHeight, modelDefaultCallbacks.heightDown, modelDefaultCallbacks.heightUp);
            adjustmentRow(
                "Rot Y",
                assetDefaultsRotation,
                modelDefaultCallbacks.rotationDown,
                modelDefaultCallbacks.rotationUp);
            adjustmentRow("Scale", assetDefaultsScale, modelDefaultCallbacks.scaleDown, modelDefaultCallbacks.scaleUp);

            ImGui::Spacing();
            if (ImGui::Button("Apply", ImVec2{96.0f, 0.0f}) && modelDefaultCallbacks.apply)
            {
                modelDefaultCallbacks.apply();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2{96.0f, 0.0f}) && modelDefaultCallbacks.reset)
            {
                modelDefaultCallbacks.reset();
            }
        }
        ImGui::EndChild();
    }

    void EditorGui::drawAssetGrid()
    {
        if (assetEntries.empty())
        {
            ImGui::TextDisabled("No assets loaded");
            return;
        }

        DrawSearchFilter(assetFilter, "asset_filter", "Search...", ImGui::GetContentRegionAvail().x);
        ImGui::Spacing();

        // Compact the visible entries so filtered-out tiles don't leave gaps in the grid.
        std::vector<std::size_t> visibleAssets;
        visibleAssets.reserve(assetEntries.size());
        for (std::size_t i = 0; i < assetEntries.size(); ++i)
        {
            const auto& asset = assetEntries[i];
            if (assetFilter.PassFilter(asset.displayName.c_str()) ||
                assetFilter.PassFilter(asset.modelKey.c_str()))
            {
                visibleAssets.push_back(i);
            }
        }

        if (visibleAssets.empty())
        {
            ImGui::TextDisabled("No assets match the filter");
            return;
        }

        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float columnPitch = ASSET_TILE_WIDTH + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>(availableWidth / columnPitch));

        if (!ImGui::BeginChild("asset_grid_scroll", ImVec2{0.0f, 0.0f}, false))
        {
            ImGui::EndChild();
            return;
        }

        if (ImGui::BeginTable("asset_grid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
        {
            for (int column = 0; column < columns; ++column)
            {
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, ASSET_TILE_WIDTH);
            }

            for (std::size_t slot = 0; slot < visibleAssets.size(); ++slot)
            {
                const std::size_t i = visibleAssets[slot];
                if (slot % static_cast<std::size_t>(columns) == 0)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, ASSET_TILE_HEIGHT);
                }
                ImGui::TableSetColumnIndex(static_cast<int>(slot % static_cast<std::size_t>(columns)));
                const auto& asset = assetEntries[i];
                const bool selected = selectedAssetIndex.has_value() && *selectedAssetIndex == i;

                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginGroup();
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    selected ? ImVec4{0.20f, 0.39f, 0.72f, 1.00f} : ImVec4{0.14f, 0.16f, 0.19f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.23f, 0.34f, 0.50f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});

                Texture2D* texture = i < assetThumbnails.size() ? &assetThumbnails[i].texture : nullptr;
                const bool clicked =
                    texture ? ImGui::ImageButton(
                                  "thumbnail",
                                  reinterpret_cast<ImTextureID>(texture),
                                  ImVec2{THUMBNAIL_SIZE, THUMBNAIL_SIZE},
                                  ImVec2{0.0f, 1.0f},
                                  ImVec2{1.0f, 0.0f},
                                  ImVec4{0.10f, 0.11f, 0.13f, 1.00f})
                            : ImGui::Button("No Preview", ImVec2{THUMBNAIL_SIZE, THUMBNAIL_SIZE});
                ImGui::PopStyleColor(3);

                if (clicked && onAssetSelectedCb)
                {
                    onAssetSelectedCb(i);
                }
                if (ImGui::IsItemHovered())
                {
                    const auto sourcePath = asset.sourcePath.string();
                    const auto tooltipPath = sourcePath.empty() ? asset.defaultsPath.string() : sourcePath;
                    ImGui::SetTooltip(
                        "%s\n%s\n%s",
                        asset.displayName.c_str(),
                        asset.modelKey.c_str(),
                        tooltipPath.c_str());
                }
                if (ImGui::BeginPopupContextItem("asset_context"))
                {
                    if (ImGui::MenuItem("Rename File")) openAssetRenamePopup(i);
                    if (ImGui::MenuItem("Copy Asset Name")) ImGui::SetClipboardText(asset.displayName.c_str());
                    if (ImGui::MenuItem("Copy Model Key")) ImGui::SetClipboardText(asset.modelKey.c_str());
                    const auto sourcePath = asset.sourcePath.string();
                    if (!sourcePath.empty() && ImGui::MenuItem("Copy Source Path"))
                    {
                        ImGui::SetClipboardText(sourcePath.c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ASSET_TILE_WIDTH);
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.72f, 0.83f, 1.00f, 1.00f});
                    ImGui::TextWrapped("%s", asset.displayName.c_str());
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::TextWrapped("%s", asset.displayName.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    void EditorGui::drawFlatpackGrid()
    {
        if (flatpackEntries.empty())
        {
            ImGui::TextDisabled("No flatpacks found");
            return;
        }

        DrawSearchFilter(flatpackFilter, "flatpack_filter", "Search...", ImGui::GetContentRegionAvail().x);
        ImGui::Spacing();

        // Compact the visible entries so filtered-out tiles don't leave gaps in the grid.
        std::vector<std::size_t> visibleFlatpacks;
        visibleFlatpacks.reserve(flatpackEntries.size());
        for (std::size_t i = 0; i < flatpackEntries.size(); ++i)
        {
            if (flatpackFilter.PassFilter(flatpackEntries[i].displayName.c_str()))
            {
                visibleFlatpacks.push_back(i);
            }
        }

        if (visibleFlatpacks.empty())
        {
            ImGui::TextDisabled("No flatpacks match the filter");
            return;
        }

        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float columnPitch = ASSET_TILE_WIDTH + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>(availableWidth / columnPitch));

        if (!ImGui::BeginChild("flatpack_grid_scroll", ImVec2{0.0f, 0.0f}, false))
        {
            ImGui::EndChild();
            return;
        }

        if (ImGui::BeginTable("flatpack_grid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
        {
            for (int column = 0; column < columns; ++column)
            {
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, ASSET_TILE_WIDTH);
            }

            for (std::size_t slot = 0; slot < visibleFlatpacks.size(); ++slot)
            {
                const std::size_t i = visibleFlatpacks[slot];
                if (slot % static_cast<std::size_t>(columns) == 0)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, FLATPACK_TILE_HEIGHT);
                }
                ImGui::TableSetColumnIndex(static_cast<int>(slot % static_cast<std::size_t>(columns)));
                const auto& flatpack = flatpackEntries[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginGroup();
                const bool clicked = ImGui::Button("Flatpack", ImVec2{ASSET_TILE_WIDTH, 56.0f});
                const bool doubleClicked =
                    ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                if (doubleClicked && onFlatpackEditCb)
                {
                    onFlatpackEditCb(flatpack.path);
                }
                else if (clicked && onFlatpackSelectedCb)
                {
                    onFlatpackSelectedCb(flatpack.path);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s\nClick: place  |  Double-click: edit", flatpack.path.string().c_str());
                }
                if (ImGui::BeginPopupContextItem("flatpack_context"))
                {
                    const auto path = flatpack.path.string();
                    if (ImGui::MenuItem("Edit Flatpack") && onFlatpackEditCb) onFlatpackEditCb(flatpack.path);
                    ImGui::Separator();
                    // The open flatpack's file is in use by the edit session, so
                    // renaming or deleting it from the browser is blocked.
                    const bool openForEdit =
                        sceneTabs.flatpackOpen && sceneTabs.flatpackLabel == flatpack.displayName;
                    if (ImGui::MenuItem("Rename...", nullptr, false, !openForEdit))
                    {
                        openFlatpackRenamePopup(i);
                    }
                    if (ImGui::MenuItem("Delete", nullptr, false, !openForEdit))
                    {
                        openFlatpackDeleteConfirmation(i);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Flatpack Name")) ImGui::SetClipboardText(flatpack.displayName.c_str());
                    if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(path.c_str());
                    ImGui::EndPopup();
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ASSET_TILE_WIDTH);
                ImGui::TextWrapped("%s", flatpack.displayName.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }
} // namespace sage::editor

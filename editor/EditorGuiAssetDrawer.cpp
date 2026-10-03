#include "EditorGui.hpp"
#include "engine/AssetKey.hpp"
#include "engine/Colors.hpp"
#include <array>

#include "EditorGuiInternal.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/Flatpack.hpp"
#include "engine/FlatpackThumbnail.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"
#include "ShaderPaths.hpp"

#include "extras/IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include "raylib.h"
#include "raymath.h"
#include "rlImGui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sage::editor
{
    namespace
    {
        constexpr int THUMBNAIL_SIZE = 128;
        constexpr float ASSET_DEFAULTS_PANEL_WIDTH = 260.0f;
        constexpr int PREVIEW_LIGHT_DIRECTIONAL = 0;
        constexpr int PREVIEW_LIGHT_POINT = 1;
        constexpr float PREVIEW_GAMMA = 1.9f;
        constexpr Color PREVIEW_LIGHT_COLOR = {.r = 255, .g = 244, .b = 214, .a = 255};
        constexpr const char* ASSET_RENAME_POPUP = "Rename Asset File";
        constexpr const char* FLATPACK_RENAME_POPUP = "Rename Flatpack";
        constexpr const char* FLATPACK_DELETE_POPUP = "Delete Flatpack";

        const std::filesystem::path RESOURCES_DIRECTORY{"resources"};
        constexpr float RESOURCE_TREE_WIDTH = 180.0f;

        std::string AbsoluteResourcePath(const std::filesystem::path& path)
        {
            return std::filesystem::absolute(path).lexically_normal().generic_string();
        }

        std::string Lowercase(std::string value)
        {
            for (auto& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        std::optional<std::filesystem::path> RelativeResourcePath(const std::filesystem::path& path)
        {
            if (path.empty()) return std::nullopt;
            const auto root = std::filesystem::absolute(RESOURCES_DIRECTORY).lexically_normal();
            const auto relative = std::filesystem::absolute(path).lexically_normal().lexically_relative(root);
            if (relative.empty() || relative == "." || *relative.begin() == "..") return std::nullopt;
            return relative;
        }

        bool IsVirtualResourceFolder(const std::filesystem::path& path)
        {
            return path == "Materials" || path == "Unlocated images" || path == "External models" ||
                   path == "Scripts" || path == "Canvases" || path == "Favourites" || path == "Recently used";
        }

        std::string ResourceFolderTooltip(const std::filesystem::path& path)
        {
            if (path == "Canvases") return "All saved UI canvases; files remain at their source paths";
            if (path == "Scripts") return "Virtual folder: C# source files remain in the managed project";
            if (path == "Favourites") return "Your favourite assets";
            if (path == "Recently used") return "Assets you recently selected or opened";
            if (path == "Materials") return "Packed materials have no separate source files";
            if (path == "Unlocated images") return "Imported images without a unique source path";
            if (path == "External models") return "Imported models with source paths outside resources";
            return (RESOURCES_DIRECTORY / path).generic_string();
        }

        Shader LoadThumbnailShader()
        {
            auto shader = ResourceManager::GetInstance().ShaderLoadUnique(
                ShaderPath("custom/ubershader.vs"), ShaderPath("custom/ubershader.fs"));
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
            const std::array<float, 3> positionValue = {position.x, position.y, position.z};
            const std::array<float, 3> targetValue = {target.x, target.y, target.z};
            const std::array<float, 4> colorValue = {
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
                positionValue.data(),
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].target", index)),
                targetValue.data(),
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].color", index)),
                colorValue.data(),
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
            const std::array<float, 4> ambient = {0.6f, 0.2f, 0.8f, 1.0f};
            SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient.data(), SHADER_UNIFORM_VEC4);

            const int lightsCount = 2;
            SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
            SetShaderValue(shader, GetShaderLocation(shader, "gamma"), &PREVIEW_GAMMA, SHADER_UNIFORM_FLOAT);

            const std::array<float, 3> viewPos = {camera.position.x, camera.position.y, camera.position.z};
            SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), viewPos.data(), SHADER_UNIFORM_VEC3);

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
                Vector3Add(center, {.x = -3.0f, .y = 4.0f, .z = -4.0f}),
                center,
                Color{.r = 172, .g = 202, .b = 255, .a = 255},
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
        const ImVec2 windowPos{viewportOffset.x + left, viewportOffset.y + viewport.y - height - bottomMargin};
        const ImVec2 windowSize{std::max(1.0f, viewport.x - left - right), height};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        PushEditorWindowStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 7.0f});

        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Asset Drawer", nullptr, windowFlags))
        {
            const bool splitView = showAssetDefaults && selectedAssetIndex.has_value() &&
                                   ImGui::BeginTable("asset_drawer_split", 2, ImGuiTableFlags_SizingStretchProp);
            if (splitView)
            {
                ImGui::TableSetupColumn("Browser", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Defaults", ImGuiTableColumnFlags_WidthFixed, ASSET_DEFAULTS_PANEL_WIDTH);
                ImGui::TableNextColumn();
            }

            drawResourceBrowser();

            if (splitView)
            {
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Close Defaults")) showAssetDefaults = false;
                drawAssetDefaultsControls();
                ImGui::EndTable();
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

        ImGui::PopStyleVar(2);
        PopEditorWindowStyle();
    }

    void EditorGui::SetAssetDefaultsStatus(
        const std::string& assetName,
        const float modelDefaultHeight,
        const float modelDefaultRotation,
        const float modelDefaultScale)
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
        for (auto& thumbnail : flatpackThumbnails)
        {
            if (thumbnail.id != 0) UnloadRenderTexture(thumbnail);
        }
        flatpackThumbnails.clear();

        flatpackEntries = std::move(entries);
        flatpackThumbnails.resize(flatpackEntries.size());
        resourceBrowserNeedsRefresh = true;
    }

    RenderTexture2D EditorGui::createAssetThumbnail(const AssetEntry& asset) const
    {
        auto thumbnail = LoadRenderTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        auto model = ResourceManager::GetInstance().GetModelView(asset.modelKey);
        if (assetPreviewShader.id == 0) assetPreviewShader = LoadThumbnailShader();
        const auto shader = assetPreviewShader;
        auto uber = CreateThumbnailUberComponent(model, shader);
        std::vector<Shader> originalShaders;
        for (int material = 0; material < model.GetMaterialCount(); ++material)
            originalShaders.push_back(model.GetShader(material));
        model.SetShader(shader);

        const auto bounds = model.CalcLocalBoundingBox();
        const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
        const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
        const float radius = std::max({std::fabs(size.x), std::fabs(size.y), std::fabs(size.z), 1.0f});

        Camera3D camera{};
        camera.position = Vector3Add(center, {.x = radius * 1.35f, .y = radius * 0.85f, .z = radius * 1.65f});
        camera.target = center;
        camera.up = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
        camera.fovy = 32.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        BeginTextureMode(thumbnail);
        ClearBackground(Color{.r = 244, .g = 247, .b = 251, .a = 255});
        BeginMode3D(camera);
        ConfigureThumbnailLighting(shader, camera, center);
        model.DrawUber(
            &uber,
            Vector3Zero(),
            {.x = 0.0f, .y = 1.0f, .z = 0.0f},
            0.0f,
            Vector3One(),
            sage::colors::WHITE_COLOR);
        EndMode3D();
        EndTextureMode();

        for (int material = 0; material < model.GetMaterialCount(); ++material)
            model.SetShader(originalShaders.at(static_cast<std::size_t>(material)), material);

        return thumbnail;
    }

    RenderTexture2D EditorGui::createMaterialThumbnail(const std::string& key) const
    {
        auto thumbnail = LoadRenderTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        const auto& material = ResourceManager::GetInstance().GetMaterial(key);
        auto sphere = GenMeshSphere(0.75f, 32, 20);
        Camera3D camera{};
        camera.position = {.x = 1.8f, .y = 1.1f, .z = 1.8f};
        camera.target = {.x = 0.0f, .y = 0.0f, .z = 0.0f};
        camera.up = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
        camera.fovy = 45.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        BeginTextureMode(thumbnail);
        ClearBackground(Color{.r = 244, .g = 247, .b = 251, .a = 255});
        BeginMode3D(camera);
        DrawMesh(sphere, material, MatrixIdentity());
        EndMode3D();
        EndTextureMode();
        UnloadMesh(sphere);
        return thumbnail;
    }

    RenderTexture2D EditorGui::createImageThumbnail(const std::string& key) const
    {
        const auto packedImage = ResourceManager::GetInstance().GetImage(key);
        const auto& image = packedImage.GetImage();
        if (!image.data || image.width <= 0 || image.height <= 0) return {};

        auto preview = ImageCopy(image);
        const float scale = std::min(
            1.0f, static_cast<float>(THUMBNAIL_SIZE) / static_cast<float>(std::max(image.width, image.height)));
        const int width = std::max(1, static_cast<int>(static_cast<float>(image.width) * scale));
        const int height = std::max(1, static_cast<int>(static_cast<float>(image.height) * scale));
        if (width != image.width || height != image.height) ImageResize(&preview, width, height);
        const auto texture = LoadTextureFromImage(preview);
        UnloadImage(preview);

        auto thumbnail = LoadRenderTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        BeginTextureMode(thumbnail);
        ClearBackground(Color{.r = 244, .g = 247, .b = 251, .a = 255});
        for (int y = 0; y < THUMBNAIL_SIZE; y += 16)
            for (int x = 0; x < THUMBNAIL_SIZE; x += 16)
                if (((x + y) / 16) % 2 == 0)
                    DrawRectangle(x, y, 16, 16, Color{.r = 220, .g = 225, .b = 232, .a = 255});
        DrawTexture(
            texture, (THUMBNAIL_SIZE - width) / 2, (THUMBNAIL_SIZE - height) / 2, sage::colors::WHITE_COLOR);
        EndTextureMode();
        UnloadTexture(texture);
        return thumbnail;
    }

    void EditorGui::openAssetRenamePopup(const std::size_t index)
    {
        if (index >= assetEntries.size()) return;

        const auto& asset = assetEntries.at(index);
        const auto renamePath = !asset.sourcePath.empty() ? asset.sourcePath : asset.defaultsPath;
        renamingAssetIndex = index;
        assetRenameInput = renamePath.filename().string();
        assetRenameStatus.clear();
        assetRenamePopupOpenRequested = true;
    }

    void EditorGui::drawAssetRenamePopup()
    {
        if (!renamingAssetIndex.has_value()) return;
        const auto clearRename = [this]() {
            renamingAssetIndex.reset();
            assetRenameInput.clear();
            assetRenameStatus.clear();
            assetRenamePopupOpenRequested = false;
        };
        if (*renamingAssetIndex >= assetEntries.size())
        {
            clearRename();
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
            const auto& asset = assetEntries.at(index);
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
                clearRename();
                ImGui::CloseCurrentPopup();
            }

            if ((enterPressed || renamePressed) && onAssetRenameCb)
            {
                const auto before =
                    std::to_string(static_cast<int>(BrowserAssetKind::Model)) + ":" + asset.modelKey;
                auto result = onAssetRenameCb(index, assetRenameInput);
                assetRenameStatus = std::move(result.message);
                if (result.renamed)
                {
                    if (result.updatedEntry.has_value())
                    {
                        assetBrowserHistory.Rename(
                            before,
                            std::to_string(static_cast<int>(BrowserAssetKind::Model)) + ":" +
                                result.updatedEntry->modelKey);
                        saveBrowserHistory();
                        assetEntries.at(index) = std::move(*result.updatedEntry);
                        resourceBrowserNeedsRefresh = true;
                    }
                    clearRename();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            clearRename();
        }
    }

    void EditorGui::openFlatpackRenamePopup(const std::size_t index)
    {
        if (index >= flatpackEntries.size()) return;

        renamingFlatpackIndex = index;
        flatpackRenameInput = flatpackEntries.at(index).displayName;
        flatpackRenameStatus.clear();
        flatpackRenamePopupOpenRequested = true;
    }

    void EditorGui::drawFlatpackRenamePopup()
    {
        if (!renamingFlatpackIndex.has_value()) return;
        const auto clearRename = [this]() {
            renamingFlatpackIndex.reset();
            flatpackRenameInput.clear();
            flatpackRenameStatus.clear();
            flatpackRenamePopupOpenRequested = false;
        };
        if (*renamingFlatpackIndex >= flatpackEntries.size())
        {
            clearRename();
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
            const auto& flatpack = flatpackEntries.at(*renamingFlatpackIndex);

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
                clearRename();
                ImGui::CloseCurrentPopup();
            }

            if ((enterPressed || renamePressed) && onFlatpackRenameCb)
            {
                // The rename callback refreshes the catalog (SetFlatpacks swaps
                // out flatpackEntries), so copy the path before invoking.
                const auto path = flatpack.path;
                auto requested = std::filesystem::path(flatpackRenameInput);
                if (requested.extension() == ".flatpack") requested = requested.stem();
                const auto renamedPath = path.parent_path() / (requested.string() + ".flatpack");
                auto result = onFlatpackRenameCb(path, flatpackRenameInput);
                flatpackRenameStatus = std::move(result.message);
                if (result.renamed)
                {
                    const auto prefix = std::to_string(static_cast<int>(BrowserAssetKind::Flatpack)) + ":";
                    assetBrowserHistory.Rename(
                        prefix + path.lexically_normal().generic_string(),
                        prefix + renamedPath.lexically_normal().generic_string());
                    saveBrowserHistory();
                    clearRename();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            clearRename();
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
        const auto clearDelete = [this]() {
            deletingFlatpackIndex.reset();
            flatpackDeletePopupOpenRequested = false;
        };
        if (*deletingFlatpackIndex >= flatpackEntries.size())
        {
            clearDelete();
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
            const auto& flatpack = flatpackEntries.at(*deletingFlatpackIndex);

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
                clearDelete();
                ImGui::CloseCurrentPopup();
                if (onFlatpackDeleteCb) onFlatpackDeleteCb(path);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                clearDelete();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            clearDelete();
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
                                    float& value,
                                    const char* format,
                                    const std::function<void()>& down,
                                    const std::function<void()>& up,
                                    const std::function<void(float)>& set) {
                ImGui::PushID(label);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine(70.0f);
                if (ImGui::SmallButton("-") && down) down();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(72.0f);
                if (ImGui::InputFloat(
                        "##value",
                        &value,
                        0.0f,
                        0.0f,
                        format,
                        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsScientific) &&
                    set)
                {
                    set(value);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("+") && up) up();
                ImGui::PopID();
            };

            adjustmentRow(
                "Y Offset",
                assetDefaultsHeight,
                "%.2f",
                modelDefaultCallbacks.heightDown,
                modelDefaultCallbacks.heightUp,
                modelDefaultCallbacks.setHeight);
            adjustmentRow(
                "Rot Y",
                assetDefaultsRotation,
                "%.0f",
                modelDefaultCallbacks.rotationDown,
                modelDefaultCallbacks.rotationUp,
                modelDefaultCallbacks.setRotation);
            adjustmentRow(
                "Scale",
                assetDefaultsScale,
                "%.2f",
                modelDefaultCallbacks.scaleDown,
                modelDefaultCallbacks.scaleUp,
                modelDefaultCallbacks.setScale);

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

    void EditorGui::refreshResourceBrowser()
    {
        resourceBrowserNeedsRefresh = false;
        resourceEntries.clear();
        std::set<std::filesystem::path> directories;
        const auto addDirectories = [&directories](std::filesystem::path path) {
            for (path = path.parent_path(); !path.empty(); path = path.parent_path())
                directories.insert(path);
        };

        for (std::size_t i = 0; i < assetEntries.size(); ++i)
        {
            const auto relative = RelativeResourcePath(assetEntries.at(i).sourcePath);
            // Keep imported keys visible even when an older pack records a path outside resources.
            const auto path = relative.value_or(std::filesystem::path{"External models"} / std::to_string(i));
            resourceEntries.push_back({.path = path, .modelIndex = i});
            addDirectories(path);
        }

        // Older packs store image basenames only. Use source files to recover their
        // folders, but never add a file unless its key is present in the loaded pack.
        std::unordered_map<std::string, std::filesystem::path> legacyImagePaths;
        std::set<std::string> ambiguousImageNames;
        for (const auto& folder : {"textures", "icons"})
        {
            std::error_code error;
            const auto root = RESOURCES_DIRECTORY / folder;
            std::filesystem::recursive_directory_iterator it(
                root, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && it != end)
            {
                if (it->is_regular_file(error) && it->path().extension() == ".png")
                {
                    const auto name = it->path().stem().string();
                    if (!ambiguousImageNames.contains(name) && !legacyImagePaths.emplace(name, it->path()).second)
                    {
                        legacyImagePaths.erase(name);
                        ambiguousImageNames.insert(name);
                    }
                }
                if (!error) it.increment(error);
            }
        }
        for (std::size_t i = 0; i < imageKeys.size(); ++i)
        {
            const auto& key = imageKeys.at(i);
            std::optional<std::filesystem::path> source;
            if (std::filesystem::path{key}.has_parent_path())
                source = RelativeResourcePath(RESOURCES_DIRECTORY / key);
            else if (const auto found = legacyImagePaths.find(key); found != legacyImagePaths.end())
                source = RelativeResourcePath(found->second);
            const auto path = source.value_or(std::filesystem::path{"Unlocated images"} / std::to_string(i));
            resourceEntries.push_back(
                {.path = path,
                 .imageIndex = i,
                 .sourcePath = source ? RESOURCES_DIRECTORY / *source : std::filesystem::path{}});
            addDirectories(path);
        }

        // Packed materials have keys but no independent source files. Keep them
        // together instead of inventing a path to one of their referencing models.
        for (std::size_t i = 0; i < materialKeys.size(); ++i)
        {
            const auto path = std::filesystem::path{"Materials"} / std::to_string(i);
            resourceEntries.push_back({.path = path, .materialIndex = i});
            addDirectories(path);
        }
        for (std::size_t i = 0; i < flatpackEntries.size(); ++i)
        {
            const auto relative = RelativeResourcePath(flatpackEntries.at(i).path);
            if (!relative) continue;
            resourceEntries.push_back({.path = *relative, .flatpackIndex = i});
            addDirectories(*relative);
        }
        // Canvas discovery is independent of packed assets. Keep the virtual folder
        // visible even before the first canvas is saved, and retain real source paths.
        directories.insert("Canvases");
        if (std::filesystem::exists(RESOURCES_DIRECTORY))
            for (auto it = std::filesystem::recursive_directory_iterator(RESOURCES_DIRECTORY);
                 it != std::filesystem::recursive_directory_iterator();
                 ++it)
            {
                if (it->is_directory() && it->path().filename().string().starts_with('.'))
                {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() && it->path().extension() == ".canvas")
                    resourceEntries.push_back(
                        {.path = std::filesystem::path{"Canvases"} / it->path().filename(),
                         .canvas = true,
                         .sourcePath = it->path()});
            }
        scriptEntries = DiscoverScriptSources(scriptConfig);
        directories.insert("Scripts");
        directories.insert("Favourites");
        directories.insert("Recently used");
        for (std::size_t i = 0; i < scriptEntries.size(); ++i)
        {
            const auto path = std::filesystem::path{"Scripts"} /
                              scriptEntries.at(i).path.lexically_relative(scriptConfig.sourceDirectory);
            resourceEntries.push_back({.path = path, .scriptIndex = i, .sourcePath = scriptEntries.at(i).path});
            addDirectories(path);
        }
        for (const auto& directory : directories)
            resourceEntries.push_back({.path = directory, .directory = true});
        std::ranges::sort(resourceEntries, [](const ResourceEntry& a, const ResourceEntry& b) {
            if (a.directory != b.directory) return a.directory;
            const auto left = Lowercase(a.path.generic_string());
            const auto right = Lowercase(b.path.generic_string());
            if (left != right) return left < right;
            if (a.path != b.path) return a.path < b.path;
            if (a.modelIndex != b.modelIndex) return a.modelIndex < b.modelIndex;
            if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
            return a.imageIndex < b.imageIndex;
        });
        if (!resourceDirectory.empty() &&
            !std::ranges::any_of(resourceEntries, [this](const ResourceEntry& entry) {
                return entry.directory && entry.path == resourceDirectory;
            }))
            navigateResourceFolder({});
    }

    void EditorGui::navigateResourceFolder(const std::filesystem::path& path)
    {
        resourceDirectory = path;
        resourceFilter.Clear();
        if (path == "Canvases") resourceTypeFilter = 0;
        showAssetDefaults = false;
    }

    void EditorGui::drawResourceFolderTree(const std::filesystem::path& path)
    {
        const bool hasChildren = std::ranges::any_of(resourceEntries, [&path](const ResourceEntry& entry) {
            return entry.directory && entry.path.parent_path() == path;
        });
        const auto key = path.empty() ? std::string("resource_root") : path.generic_string();
        const auto selected = resourceDirectory.generic_string();
        if (path.empty() || (!selected.empty() && selected.starts_with(key + '/')))
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (path == resourceDirectory) flags |= ImGuiTreeNodeFlags_Selected;
        const auto label = path.empty() ? std::string("resources") : path.filename().string();
        const bool open = ImGui::TreeNodeEx(key.c_str(), flags, ICON_FA_FOLDER " %s", label.c_str());
        if (ImGui::IsItemClicked()) navigateResourceFolder(path);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ResourceFolderTooltip(path).c_str());
        if (open && hasChildren)
        {
            for (const auto& entry : resourceEntries)
                if (entry.directory && entry.path.parent_path() == path) drawResourceFolderTree(entry.path);
            ImGui::TreePop();
        }
    }

    void EditorGui::drawResourceBrowser()
    {
        if (resourceBrowserNeedsRefresh) refreshResourceBrowser();
        if (!browserHistoryError.empty()) ImGui::TextWrapped("%s", browserHistoryError.c_str());
        if (ImGui::Button("New Canvas") && onCanvasEditCb) onCanvasEditCb({});
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Refresh"))
        {
            std::vector<FlatpackEntry> flatpacks;
            for (const auto& entry : ListFlatpacks(RESOURCES_DIRECTORY))
                flatpacks.push_back({.displayName = entry.displayName, .path = entry.path});
            SetFlatpacks(std::move(flatpacks));
            refreshResourceBrowser();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(resourceDirectory.empty());
        if (ImGui::Button(ICON_FA_ARROW_UP " Up")) navigateResourceFolder(resourceDirectory.parent_path());
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo(
            "##resource_type",
            &resourceTypeFilter,
            "All assets\0Models\0Materials\0Images\0Flatpacks\0Canvases\0Scripts\0");
        ImGui::SameLine();
        DrawSearchFilter(
            resourceFilter, "resource_filter", "Search resources...", ImGui::GetContentRegionAvail().x);

        // Keep a copy because a breadcrumb click changes the current folder.
        const auto directory = resourceDirectory;
        if (ImGui::SmallButton("resources")) navigateResourceFolder({});
        std::filesystem::path breadcrumb;
        for (const auto& part : directory)
        {
            breadcrumb /= part;
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
            ImGui::PushID(breadcrumb.generic_string().c_str());
            if (ImGui::SmallButton(part.string().c_str())) navigateResourceFolder(breadcrumb);
            ImGui::PopID();
        }
        ImGui::Separator();

        if (ImGui::BeginTable(
                "resource_browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, RESOURCE_TREE_WIDTH);
            ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            if (ImGui::BeginChild(
                    "resource_folders", ImVec2{0.0f, 0.0f}, false, ImGuiWindowFlags_HorizontalScrollbar))
                drawResourceFolderTree({});
            ImGui::EndChild();
            ImGui::TableNextColumn();
            drawResourceGrid();
            ImGui::EndTable();
        }
    }

    void EditorGui::drawResourceGrid()
    {
        std::vector<std::size_t> visible;
        for (std::size_t i = 0; i < resourceEntries.size(); ++i)
        {
            const auto& entry = resourceEntries.at(i);
            const bool favourites = resourceDirectory == "Favourites";
            const bool recent = resourceDirectory == "Recently used";
            if (favourites || recent)
            {
                if (entry.directory) continue;
                const auto id = browserAssetId(entry);
                if (favourites && !assetBrowserHistory.IsFavourite(id)) continue;
                if (recent &&
                    std::ranges::find(assetBrowserHistory.recent, id) == assetBrowserHistory.recent.end())
                    continue;
            }
            if (resourceFilter.IsActive())
            {
                const bool pathMatches = resourceFilter.PassFilter(entry.path.generic_string().c_str()) ||
                                         (!entry.sourcePath.empty() &&
                                          resourceFilter.PassFilter(entry.sourcePath.generic_string().c_str()));
                const bool keyMatches =
                    entry.modelIndex &&
                    resourceFilter.PassFilter(assetEntries.at(*entry.modelIndex).modelKey.c_str());
                const bool materialMatches =
                    entry.materialIndex &&
                    resourceFilter.PassFilter(materialKeys.at(*entry.materialIndex).c_str());
                const bool imageMatches =
                    entry.imageIndex && resourceFilter.PassFilter(imageKeys.at(*entry.imageIndex).c_str());
                const bool scriptMatches =
                    entry.scriptIndex &&
                    std::ranges::any_of(scriptEntries.at(*entry.scriptIndex).types, [this](const auto& type) {
                        return resourceFilter.PassFilter(type.c_str());
                    });
                if (!pathMatches && !keyMatches && !materialMatches && !imageMatches && !scriptMatches) continue;
            }
            else if (!favourites && !recent && entry.path.parent_path() != resourceDirectory)
                continue;
            if (!entry.directory && resourceTypeFilter == 1 && !entry.modelIndex) continue;
            if (!entry.directory && resourceTypeFilter == 2 && !entry.materialIndex) continue;
            if (!entry.directory && resourceTypeFilter == 3 && !entry.imageIndex) continue;
            if (!entry.directory && resourceTypeFilter == 4 && !entry.flatpackIndex) continue;
            if (!entry.directory && resourceTypeFilter == 5 && !entry.canvas) continue;
            if (!entry.directory && resourceTypeFilter == 6 && !entry.scriptIndex) continue;
            visible.push_back(i);
        }

        if (resourceDirectory == "Recently used")
            std::ranges::sort(visible, [&](std::size_t a, std::size_t b) {
                return std::ranges::find(assetBrowserHistory.recent, browserAssetId(resourceEntries.at(a))) <
                       std::ranges::find(assetBrowserHistory.recent, browserAssetId(resourceEntries.at(b)));
            });
        if (ImGui::BeginChild("resource_grid_scroll", ImVec2{0.0f, 0.0f}, false))
        {
            if (resourceFilter.IsActive()) ImGui::TextDisabled("Results from all resources");
            if (visible.empty())
                ImGui::TextDisabled(resourceFilter.IsActive() ? "No matching resources" : "This folder is empty");
            const auto& style = ImGui::GetStyle();
            const float tileExtraHeight =
                ImGui::GetTextLineHeightWithSpacing() * 2.0f + style.FramePadding.y * 2.0f + style.ItemSpacing.y;
            const float previewSize = std::clamp(
                std::min(
                    settings->ScaleValueWidth(THUMBNAIL_SIZE), ImGui::GetContentRegionAvail().y - tileExtraHeight),
                48.0f,
                static_cast<float>(THUMBNAIL_SIZE));
            const float tileWidth = previewSize + 24.0f;
            const float tileHeight = previewSize + tileExtraHeight;
            const float pitch = tileWidth + style.ItemSpacing.x;
            const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / pitch));
            if (ImGui::BeginTable(
                    "resource_grid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
            {
                for (int column = 0; column < columns; ++column)
                    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, tileWidth);
                std::optional<std::filesystem::path> navigateTo;
                for (std::size_t slot = 0; slot < visible.size(); ++slot)
                {
                    if (slot % columns == 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, tileHeight);
                    ImGui::TableSetColumnIndex(static_cast<int>(slot % columns));
                    // Skip offscreen previews rather than rendering every model on startup.
                    if (!ImGui::IsRectVisible(ImVec2{tileWidth, tileHeight})) continue;
                    const auto entry = resourceEntries.at(visible.at(slot));
                    ImGui::PushID(static_cast<int>(visible.at(slot)));
                    if (drawResourceTile(entry, previewSize)) navigateTo = entry.path;
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (navigateTo) navigateResourceFolder(*navigateTo);
            }
        }
        ImGui::EndChild();
    }

    bool EditorGui::drawResourceTile(const ResourceEntry& entry, const float previewSize)
    {
        ImGui::BeginGroup();
        const bool selected = entry.modelIndex && selectedAssetIndex == entry.modelIndex;
        if (entry.modelIndex)
        {
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                selected ? ImVec4{0.20f, 0.39f, 0.72f, 1.00f} : ImVec4{0.14f, 0.16f, 0.19f, 1.00f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.23f, 0.34f, 0.50f, 1.00f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        }
        std::optional<std::reference_wrapper<RenderTexture2D>> thumbnail;
        std::string fallbackLabel = "No Preview";
        if (entry.modelIndex)
        {
            const auto i = *entry.modelIndex;
            thumbnail = std::ref(assetThumbnails.at(i));
            if (thumbnail->get().id == 0) thumbnail->get() = createAssetThumbnail(assetEntries.at(i));
        }
        else if (entry.materialIndex)
        {
            const auto i = *entry.materialIndex;
            thumbnail = std::ref(materialThumbnails.at(i));
            if (thumbnail->get().id == 0) thumbnail->get() = createMaterialThumbnail(materialKeys.at(i));
        }
        else if (entry.imageIndex)
        {
            const auto i = *entry.imageIndex;
            thumbnail = std::ref(imageThumbnails.at(i));
            if (thumbnail->get().id == 0) thumbnail->get() = createImageThumbnail(imageKeys.at(i));
        }
        else if (entry.flatpackIndex)
        {
            const auto i = *entry.flatpackIndex;
            thumbnail = std::ref(flatpackThumbnails.at(i));
            if (thumbnail->get().id == 0)
                thumbnail->get() = CreateFlatpackThumbnail(flatpackEntries.at(i).path, THUMBNAIL_SIZE);
        }
        else
        {
            fallbackLabel = ICON_FA_FOLDER "\nFolder";
        }
        if (entry.canvas) fallbackLabel = "UI\nCanvas";
        if (entry.scriptIndex) fallbackLabel = "C#\nScript";
        std::optional<std::reference_wrapper<Texture2D>> texture;
        if (thumbnail && thumbnail->get().id != 0) texture = std::ref(thumbnail->get().texture);
        const bool clicked = texture ? ImGui::ImageButton(
                                           "thumbnail",
                                           reinterpret_cast<ImTextureID>(&texture->get()),
                                           ImVec2{previewSize, previewSize},
                                           ImVec2{0.0f, 1.0f},
                                           ImVec2{1.0f, 0.0f},
                                           entry.modelIndex ? ImVec4{0.10f, 0.11f, 0.13f, 1.00f} : ImVec4{})
                                     : ImGui::Button(fallbackLabel.c_str(), ImVec2{previewSize, previewSize});
        if (entry.modelIndex) ImGui::PopStyleColor(3);

        if (!entry.directory && clicked)
        {
            assetBrowserHistory.Use(browserAssetId(entry));
            saveBrowserHistory();
        }
        const char* type = "Folder";
        if (entry.modelIndex)
        {
            type = "Model";
            const auto i = *entry.modelIndex;
            const auto asset = assetEntries.at(i);
            if (clicked && onAssetSelectedCb)
            {
                onAssetSelectedCb(i);
            }
            if (ImGui::IsItemHovered())
            {
                const auto sourcePath = asset.sourcePath.string();
                const auto tooltipPath = sourcePath.empty() ? asset.defaultsPath.string() : sourcePath;
                ImGui::SetTooltip(
                    "%s\n%s\n%s", asset.displayName.c_str(), asset.modelKey.c_str(), tooltipPath.c_str());
            }
            if (ImGui::BeginPopupContextItem("asset_context"))
            {
                drawResourceActions(entry);
                if (ImGui::MenuItem("Placement Defaults"))
                {
                    if (onAssetSelectedCb) onAssetSelectedCb(i);
                    showAssetDefaults = true;
                }
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
        }
        else if (entry.materialIndex)
        {
            type = "Material";
            const auto& key = materialKeys.at(*entry.materialIndex);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to preview", key.c_str());
            if (ImGui::BeginPopupContextItem("material_context"))
            {
                drawResourceActions(entry);
                if (ImGui::MenuItem("Copy Material Key")) ImGui::SetClipboardText(key.c_str());
                ImGui::EndPopup();
            }
        }
        else if (entry.imageIndex)
        {
            type = "Image";
            const auto& key = imageKeys.at(*entry.imageIndex);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s\nClick to preview", key.c_str(), entry.sourcePath.string().c_str());
            if (ImGui::BeginPopupContextItem("image_context"))
            {
                drawResourceActions(entry);
                if (ImGui::MenuItem("Copy Image Key")) ImGui::SetClipboardText(key.c_str());
                if (!entry.sourcePath.empty() && ImGui::MenuItem("Copy Source Path"))
                    ImGui::SetClipboardText(entry.sourcePath.string().c_str());
                ImGui::EndPopup();
            }
        }
        else if (entry.flatpackIndex)
        {
            type = "Flatpack";
            const auto i = *entry.flatpackIndex;
            const auto flatpack = flatpackEntries.at(i);
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
                drawResourceActions(entry);
                const auto path = flatpack.path.string();
                if (ImGui::MenuItem("Edit Flatpack") && onFlatpackEditCb) onFlatpackEditCb(flatpack.path);
                ImGui::Separator();
                // The open flatpack's file is in use by the edit session, so
                // renaming or deleting it from the browser is blocked.
                const bool openForEdit = sceneTabs.flatpackOpen && AbsoluteResourcePath(sceneTabs.flatpackPath) ==
                                                                       AbsoluteResourcePath(flatpack.path);
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
        }
        else if (entry.scriptIndex)
        {
            type = "Script";
            if (ImGui::IsItemHovered())
            {
                std::string tooltip = entry.sourcePath.string();
                for (const auto& name : scriptEntries.at(*entry.scriptIndex).types)
                    tooltip += "\n" + name;
                ImGui::SetTooltip("%s\nDouble-click: open source", tooltip.c_str());
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                OpenURL(("file://" + std::filesystem::absolute(entry.sourcePath).string()).c_str());
            if (ImGui::BeginPopupContextItem("script_context"))
            {
                drawResourceActions(entry);
                if (ImGui::MenuItem("Open Script"))
                    OpenURL(("file://" + std::filesystem::absolute(entry.sourcePath).string()).c_str());
                if (ImGui::MenuItem("Copy Source Path"))
                    ImGui::SetClipboardText(entry.sourcePath.string().c_str());
                ImGui::EndPopup();
            }
        }
        else if (entry.canvas)
        {
            type = "Canvas";
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nDouble-click: edit", entry.sourcePath.string().c_str());
            if (ImGui::BeginPopupContextItem("canvas_context"))
            {
                drawResourceActions(entry);
                if (ImGui::MenuItem("Edit Canvas") && onCanvasEditCb) onCanvasEditCb(entry.sourcePath);
                if (ImGui::MenuItem("New Canvas") && onCanvasEditCb) onCanvasEditCb({});
                if (ImGui::MenuItem("Copy Source Path"))
                    ImGui::SetClipboardText(entry.sourcePath.string().c_str());
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && onCanvasEditCb)
                onCanvasEditCb(entry.sourcePath);
        }
        else
        {
            const auto path = ResourceFolderTooltip(entry.path);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            if (!IsVirtualResourceFolder(entry.path) && ImGui::BeginPopupContextItem("resource_context"))
            {
                if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(path.c_str());
                ImGui::EndPopup();
            }
        }

        if (entry.modelIndex && selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.72f, 0.83f, 1.00f, 1.00f});
        const auto label = entry.modelIndex      ? assetEntries.at(*entry.modelIndex).displayName
                           : entry.materialIndex ? materialKeys.at(*entry.materialIndex)
                           : entry.imageIndex    ? AssetNameFromKey(imageKeys.at(*entry.imageIndex))
                                                 : entry.path.filename().string();
        const auto displayLabel = !entry.directory && assetBrowserHistory.IsFavourite(browserAssetId(entry))
                                      ? std::string(ICON_FA_STAR " ") + label
                                      : label;
        ImGui::TextUnformatted(displayLabel.c_str());
        if (entry.modelIndex && selected) ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", type);
        ImGui::EndGroup();

        if (entry.materialIndex || entry.imageIndex)
        {
            if (clicked) ImGui::OpenPopup("asset_preview");
            if (ImGui::BeginPopup("asset_preview"))
            {
                ImGui::TextUnformatted(label.c_str());
                if (!entry.sourcePath.empty()) ImGui::TextDisabled("%s", entry.sourcePath.string().c_str());
                if (texture)
                    ImGui::Image(
                        reinterpret_cast<ImTextureID>(&texture->get()),
                        ImVec2{256.0f, 256.0f},
                        ImVec2{0.0f, 1.0f},
                        ImVec2{1.0f, 0.0f});
                ImGui::EndPopup();
            }
        }
        return entry.directory && clicked;
    }
} // namespace sage::editor

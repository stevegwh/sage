#include "EditorScene.hpp"

#include "EditorComponents.hpp"
#include "EditorFlatpack.hpp"
#include "EditorMapLoader.hpp"
#include "EditorTransformMath.hpp"
#include "engine/AudioManager.hpp"
#include "engine/Camera.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/components/TriggerVolume.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/Cursor.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Light.hpp"
#include "engine/LightManager.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/systems/RenderSystem.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/UserInput.hpp"

#include "imgui.h"

#include "imfilebrowser.h"
#include "raylib.h"
#include "raymath.h"
#include "rlImGui.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <iostream>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace sage
{
    namespace
    {
        constexpr float GRID_SURFACE_Y_STEP = 1.0f;
        constexpr float EDITOR_FOCUS_CAMERA_DISTANCE = 38.0f;
        constexpr float EDITOR_FOCUS_RADIUS_PADDING = 2.4f;
        constexpr const char* UNTITLED_SCENE_NAME = "Untitled";
        constexpr const char* DEFAULT_SAVE_FILENAME = "untitled.map";
        constexpr const char* DEFAULT_MAP_BASE_NAME = "_MAPBASE_EDITOR_BASE";
        constexpr const char* DEFAULT_MAP_BASE_MODEL_KEY = "primitive_plane";
        constexpr float DEFAULT_MAP_BASE_SIZE = 1000.0f;
        constexpr float DEFAULT_MAP_BASE_HALF_HEIGHT = 0.02f;
        constexpr float DEFAULT_LIGHT_HEIGHT_OFFSET = 6.0f;
        constexpr float DEFAULT_LIGHT_BRIGHTNESS = 3.0f;
        constexpr Color DEFAULT_LIGHT_COLOR = {255, 244, 214, 255};
        constexpr ImGuiFileBrowserFlags LOAD_BROWSER_FLAGS =
            ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SkipItemsCausingError;
        constexpr ImGuiFileBrowserFlags SAVE_BROWSER_FLAGS =
            LOAD_BROWSER_FLAGS | ImGuiFileBrowserFlags_EnterNewFilename;
        constexpr float SAVE_FEEDBACK_SECONDS = 2.5f;

        struct FocusTarget
        {
            Vector3 position{};
            float radius = 1.0f;
        };

        FocusTarget focusTargetFromBounds(const BoundingBox& bounds)
        {
            const Vector3 center = editor::BoundingBoxCenter(bounds);
            const Vector3 halfSize = Vector3Scale(Vector3Subtract(bounds.max, bounds.min), 0.5f);
            return {.position = center, .radius = std::max(1.0f, Vector3Length(halfSize))};
        }

        BoundingBox boundsFromPoint(const Vector3 point)
        {
            return BoundingBox{.min = point, .max = point};
        }

        void expandBounds(BoundingBox& bounds, const BoundingBox& other)
        {
            bounds.min.x = std::min(bounds.min.x, other.min.x);
            bounds.min.y = std::min(bounds.min.y, other.min.y);
            bounds.min.z = std::min(bounds.min.z, other.min.z);
            bounds.max.x = std::max(bounds.max.x, other.max.x);
            bounds.max.y = std::max(bounds.max.y, other.max.y);
            bounds.max.z = std::max(bounds.max.z, other.max.z);
        }

        std::optional<BoundingBox> focusBoundsForEntity(entt::registry& registry, const entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.any_of<sgTransform>(entity)) return std::nullopt;

            if (registry.any_of<Collideable>(entity))
            {
                return registry.get<Collideable>(entity).worldBoundingBox;
            }

            if (registry.any_of<Renderable>(entity))
            {
                const auto& transform = registry.get<sgTransform>(entity);
                const auto& renderable = registry.get<Renderable>(entity);
                if (const auto* model = renderable.GetModel(); model != nullptr)
                {
                    const Matrix entityMatrix = editor::BuildRenderableEntityMatrix(
                        transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale());
                    const Matrix worldMatrix = MatrixMultiply(model->GetTransform(), entityMatrix);
                    return editor::TransformBoundingBoxByCorners(model->CalcLocalBoundingBox(), worldMatrix);
                }
            }

            return boundsFromPoint(registry.get<sgTransform>(entity).GetWorldPos());
        }

        std::filesystem::path ensureMapExtension(std::filesystem::path path)
        {
            if (path.extension() != ".map")
            {
                path.replace_extension(".map");
            }
            return path;
        }

        std::filesystem::path defaultBrowserDirectory(
            const std::filesystem::path& currentMapPath, const EditorSettings* editorSettings)
        {
            if (!currentMapPath.empty() && !currentMapPath.parent_path().empty())
            {
                return currentMapPath.parent_path();
            }

            if (editorSettings != nullptr && !editorSettings->lastVisitedDirectory.empty())
            {
                const std::filesystem::path lastVisited{editorSettings->lastVisitedDirectory};
                if (std::filesystem::is_directory(lastVisited))
                {
                    return lastVisited;
                }
            }

            const std::filesystem::path resources{"resources"};
            if (std::filesystem::is_directory(resources))
            {
                return resources;
            }

            return std::filesystem::current_path();
        }

        std::string sceneNameFromPath(const std::filesystem::path& path)
        {
            const auto stem = path.stem().string();
            return stem.empty() ? UNTITLED_SCENE_NAME : stem;
        }

        bool isMapBaseRenderable(const sgTransform& transform)
        {
            return transform.name.find("_MAPBASE_") != std::string::npos;
        }

        bool modelKeyAvailable(const std::string& key)
        {
            const auto keys = ResourceManager::GetInstance().GetModelKeys(true);
            return std::ranges::find(keys, key) != keys.end();
        }

        std::string ToUpperAscii(std::string value)
        {
            for (auto& ch : value)
            {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool EqualsIgnoreAsciiCase(std::string lhs, std::string rhs)
        {
            return ToUpperAscii(std::move(lhs)) == ToUpperAscii(std::move(rhs));
        }

        bool IsReservedWindowsDeviceName(const std::string& fileName)
        {
            const auto dot = fileName.find('.');
            const auto base = ToUpperAscii(fileName.substr(0, dot));
            if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL")
            {
                return true;
            }

            if (base.size() == 4)
            {
                const auto prefix = base.substr(0, 3);
                const auto suffix = base[3];
                return (prefix == "COM" || prefix == "LPT") && suffix >= '1' && suffix <= '9';
            }

            return false;
        }

        std::optional<std::string> ValidatePortableFileName(const std::string& fileName)
        {
            if (fileName.empty()) return "File name cannot be empty.";
            if (fileName == "." || fileName == "..") return "File name cannot be '.' or '..'.";
            if (fileName.size() > 255) return "File name must be 255 bytes or fewer.";
            if (fileName.back() == ' ' || fileName.back() == '.')
            {
                return "File name cannot end with a space or a dot.";
            }
            if (IsReservedWindowsDeviceName(fileName))
            {
                return "File name uses a reserved Windows device name.";
            }

            for (const unsigned char ch : fileName)
            {
                if (ch == '\0') return "File name cannot contain NUL.";
                if (ch < 32) return "File name cannot contain control characters.";
                switch (ch)
                {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    return "File name contains a character that is illegal on Windows, macOS, or Linux.";
                default:
                    break;
                }
            }

            return std::nullopt;
        }

        bool EquivalentPath(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
        {
            std::error_code ec;
            if (!std::filesystem::exists(lhs, ec) || ec) return false;
            if (!std::filesystem::exists(rhs, ec) || ec) return false;
            const bool equivalent = std::filesystem::equivalent(lhs, rhs, ec);
            return !ec && equivalent;
        }

        std::optional<std::string> CheckTargetCollision(
            const std::filesystem::path& source,
            const std::filesystem::path& target)
        {
            std::error_code ec;
            if (std::filesystem::exists(target, ec) && !EquivalentPath(source, target))
            {
                return std::format("A file named {} already exists.", target.filename().string());
            }
            if (ec)
            {
                return std::format("Could not check target path: {}", ec.message());
            }
            return std::nullopt;
        }

        std::filesystem::path BuildRenameTargetPath(
            const std::filesystem::path& sourcePath,
            const std::string& requestedFileName,
            std::string& error)
        {
            if (const auto validationError = ValidatePortableFileName(requestedFileName);
                validationError.has_value())
            {
                error = *validationError;
                return {};
            }

            std::filesystem::path requestedPath{requestedFileName};
            auto finalFileName = requestedFileName;
            const auto requiredExtension = sourcePath.extension().string();
            if (requestedPath.extension().empty() && !requiredExtension.empty())
            {
                finalFileName += requiredExtension;
            }
            else if (!requiredExtension.empty() &&
                     !EqualsIgnoreAsciiCase(requestedPath.extension().string(), requiredExtension))
            {
                error = std::format("File extension must stay {}.", requiredExtension);
                return {};
            }

            if (const auto validationError = ValidatePortableFileName(finalFileName);
                validationError.has_value())
            {
                error = *validationError;
                return {};
            }

            return sourcePath.parent_path() / finalFileName;
        }

        std::string lightLabel(const entt::entity entity)
        {
            return std::format("light_{}", entt::to_integral(entity));
        }

        std::string spawnerLabel(const entt::entity entity)
        {
            return std::format("spawner_{}", entt::to_integral(entity));
        }

        std::string triggerLabel(const entt::entity entity)
        {
            return std::format("trigger_{}", entt::to_integral(entity));
        }

        Color spawnerColor(const SpawnerType type)
        {
            switch (type)
            {
            case SpawnerType::PLAYER:
                return BLUE;
            case SpawnerType::ENEMY:
                return RED;
            case SpawnerType::NPC:
                return GREEN;
            case SpawnerType::DIALOG_CUTSCENE:
                return PURPLE;
            }
            return WHITE;
        }
    } // namespace

    const editor::PlaceableAsset& EditorScene::selectedPlaceable() const
    {
        return assetCatalog->Selected();
    }

    bool EditorScene::isPlaceState() const
    {
        return editorModes->IsPlaceMode();
    }

    bool EditorScene::isEditState() const
    {
        return editorModes->IsEditMode();
    }

    std::string EditorScene::describeSelectedAsset() const
    {
        if (isPlaceState()) return selectedPlaceable().displayName;
        if (selection->HasSelection()) return describeSelectedSceneEntity();
        return "None";
    }

    std::string EditorScene::describeCursorPosition() const
    {
        if (const auto& position = placementController->SnappedPlacementPosition(); position.has_value())
        {
            return std::format("{:.2f}, {:.2f}, {:.2f}", position->x, position->y, position->z);
        }
        if (const auto& square = placementController->HoveredGridSquare(); square.has_value())
        {
            return std::format("row {}, col {}", square->row, square->col);
        }
        return "-";
    }

    std::string EditorScene::describeSelectedSceneEntity() const
    {
        const auto entities = selection->Selected();
        if (entities.empty()) return "None";
        if (entities.size() == 1) return hierarchyTree->GetEntityName(entities.front());
        return std::format("{} selected", entities.size());
    }

    void EditorScene::applyLitShaderToLoadedRenderables() const
    {
        for (const auto entity : sys->registry->view<Renderable>())
        {
            if (sys->registry->any_of<UberShaderComponent>(entity)) continue;
            auto& renderable = sys->registry->get<Renderable>(entity);
            if (renderable.GetModel() == nullptr) continue;
            auto& uber =
                sys->registry->emplace<UberShaderComponent>(entity, renderable.GetModel()->GetMaterialCount());
            uber.SetFlagAll(UberShaderComponent::Flags::Lit);
        }
    }

    void EditorScene::giveTransformsToLights() const
    {
        for (const auto entity : sys->registry->view<Light>())
        {
            if (!sys->registry->any_of<sgTransform>(entity))
            {
                const auto position = sys->registry->get<Light>(entity).position;
                auto& transform = sys->registry->emplace<sgTransform>(entity);
                transform.name = lightLabel(entity);
                transform.position.world = position;
            }
        }
    }

    void EditorScene::refreshOverlay() const
    {
        const auto defaultsStatus = modelDefaults->Status(describeSelectedAsset());
        gui->SetOverlayStatus(editorModes->GetStateName(), describeCursorPosition());
        gui->SetSaveStatus(currentSaveStatus(), hasUnsavedChanges());
        gui->SetAssetDefaultsStatus(
            defaultsStatus.assetName, defaultsStatus.height, defaultsStatus.rotation, defaultsStatus.scale);
        gui->SetSelectedAsset(
            isPlaceState() ? std::optional<std::size_t>{assetCatalog->SelectedIndex()} : std::nullopt);
    }

    void EditorScene::refreshSceneWindows() const
    {
        const auto selectedRoots = selection->Selected();
        auto inspectedComponents = !selectedRoots.empty()
                                       ? inspectorRegistry.Inspect(*sys->registry, selectedRoots)
                                       : std::vector<editor::InspectedComponent>{};

        gui->SetHierarchy(
            hierarchyTree->CollectSceneObjectEntries(), selection->SelectedWithChildren(), selection->Anchor());
        gui->SetInspector(describeSelectedSceneEntity(), inspectedComponents);
    }

    void EditorScene::focusSelectedObject() const
    {
        const auto selectedEntities = selection->SelectedWithChildren();
        if (selectedEntities.empty()) return;

        std::optional<BoundingBox> combinedBounds;
        for (const auto entity : selectedEntities)
        {
            const auto bounds = focusBoundsForEntity(*sys->registry, entity);
            if (!bounds.has_value()) continue;

            if (combinedBounds.has_value())
                expandBounds(*combinedBounds, *bounds);
            else
                combinedBounds = bounds;
        }

        FocusTarget target{};
        if (combinedBounds.has_value())
        {
            target = focusTargetFromBounds(*combinedBounds);
        }
        else
        {
            const auto primary = selection->Active();
            if (!primary.has_value()) return;
            target = {.position = sys->registry->get<sgTransform>(*primary).GetWorldPos()};
        }

        const float focusDistance =
            std::max(EDITOR_FOCUS_CAMERA_DISTANCE, target.radius * EDITOR_FOCUS_RADIUS_PADDING);
        sys->camera->FocusPoint(target.position, focusDistance);
    }

    void EditorScene::focusSelectedObjectInHierarchy() const
    {
        const auto selectedEntity = selection->Active();
        if (!selectedEntity.has_value()) return;
        gui->FocusHierarchyOnEntity(*selectedEntity);
    }

    void EditorScene::Update() const
    {
        if (saveFeedbackRemaining > 0.0f)
        {
            saveFeedbackRemaining = std::max(0.0f, saveFeedbackRemaining - GetFrameTime());
        }

        // TODO: Fullscreen game viewport (switch state)
        sys->collisionSystem->Update();
        sys->audioManager->Update();
        sys->userInput->ListenForInput();
        const bool uiBlocksScroll = !viewportFullscreen && gui && gui->WantsMouseCapture();
        if (uiBlocksScroll || !sys->settings->IsPointInRenderViewport(GetMousePosition()))
        {
            sys->camera->ScrollDisable();
        }
        else
        {
            sys->camera->ScrollEnable();
        }

        const bool uiBlocksCameraControls = !viewportFullscreen && gui && gui->WantsKeyboardCapture();
        if (uiBlocksCameraControls)
        {
            sys->camera->LockInput();
        }
        else if (!transformEditor || !transformEditor->IsGizmoDragging())
        {
            sys->camera->UnlockInput();
        }

        sys->camera->Update();
        sys->cursor->Update();
        editorModes->RefreshPlacementTarget();
        // TODO: Should be part of some mode
        if (!uiBlocksCameraControls)
        {
            if (IsKeyPressed(KEY_EQUAL))
            {
                editorModes->AdjustGridSurfaceY(GRID_SURFACE_Y_STEP);
            }
            if (IsKeyPressed(KEY_MINUS))
            {
                editorModes->AdjustGridSurfaceY(-GRID_SURFACE_Y_STEP);
            }
        }
        editorModes->Update();
        syncLightTransforms();
        sys->lightSubSystem->Update();
        sys->lightSubSystem->RefreshLights();
        refreshOverlay();
        refreshSceneWindows();
    }

    void EditorScene::Draw3D() const
    {
        sys->renderSystem->Draw();
        sys->lightSubSystem->DrawDebugLights();
        placementController->DrawGridAndAxes();
        editorModes->Draw3D();

        // Markers have no mesh, so draw a stand-in: a sphere per spawner (coloured by
        // type) and a wireframe box per trigger volume (from its halfExtents).
        for (const auto entity : sys->registry->view<Spawner, sgTransform>())
        {
            const auto& transform = sys->registry->get<sgTransform>(entity);
            const auto position = transform.GetWorldPos();
            const auto color = spawnerColor(sys->registry->get<Spawner>(entity).type);
            DrawSphereEx(position, 0.5f, 8, 8, color);
            // Facing line: a stick from the sphere out along the spawner's forward.
            DrawLine3D(position, Vector3Add(position, Vector3Scale(transform.forward(), 1.5f)), color);
        }
        for (const auto entity : sys->registry->view<TriggerVolume, sgTransform>())
        {
            const auto& trigger = sys->registry->get<TriggerVolume>(entity);
            const auto position = sys->registry->get<sgTransform>(entity).GetWorldPos();
            DrawBoundingBox(
                BoundingBox{
                    Vector3Subtract(position, trigger.halfExtents),
                    Vector3Add(position, trigger.halfExtents)},
                GREEN);
        }

        for (const auto entity : selection->SelectedWithChildren())
        {
            if (sys->registry->valid(entity) && sys->registry->any_of<Collideable>(entity))
            {
                DrawBoundingBox(sys->registry->get<Collideable>(entity).worldBoundingBox, ORANGE);
            }
        }
    }

    void EditorScene::setSnapToGrid(const bool enabled) const
    {
        snapToGrid = enabled;
        if (placementController) placementController->SetSnapToGrid(enabled);
        if (transformEditor) transformEditor->SetSnapToGrid(enabled);
        refreshOverlay();
    }

    void EditorScene::markSceneSaved(const std::filesystem::path& path) const
    {
        if (history) history->MarkSaved();
        const auto fileName = path.filename().string();
        saveFeedbackStatus = fileName.empty() ? "Saved map" : std::format("Saved {}", fileName);
        saveFeedbackRemaining = SAVE_FEEDBACK_SECONDS;
    }

    bool EditorScene::hasUnsavedChanges() const
    {
        return history && history->HasUnsavedChanges();
    }

    std::string EditorScene::currentSaveStatus() const
    {
        if (hasUnsavedChanges()) return "Unsaved changes";
        if (saveFeedbackRemaining > 0.0f) return saveFeedbackStatus;
        return {};
    }

    void EditorScene::DrawOverlay2D() const
    {
        if (viewportFullscreen) return;
        gui->DrawSceneViewInfo();
    }

    void EditorScene::DrawImGui(bool& exitRequested, bool& exitConfirmed) const
    {
        if (viewportFullscreen)
        {
            // Still drive the exit confirmation modal so the editor can be closed
            // while a viewport is fullscreen (the rest of the UI is hidden).
            gui->StartImGui();
            drawExitConfirmationModal(exitRequested, exitConfirmed);
            gui->EndImGui();
            return;
        }
        gui->StartImGui();
        drawMainMenuBar(exitRequested);

        // Snapshot the selection's clean state on idle frames so an inspector edit
        // that mutates on its activation frame (checkbox/combo) still has a correct
        // "before" to undo to. See EditorHistory::CaptureBaseline.
        if (history && !history->HasActiveTransaction() && !ImGui::IsAnyItemActive())
        {
            history->CaptureBaseline(selection->Selected());
        }
        const auto inspectorEdit = gui->DrawInspectorWindow();
        handleInspectorEdit(inspectorEdit);

        gui->DrawHierarchyWindow();
        gui->DrawAssetDrawerWindow();
        handleFileShortcuts();
        drawFileBrowsers();
        handleClipboardShortcuts();
        handleHistoryShortcuts();
        drawHierarchyContextMenu();
        gui->DrawDeleteConfirmationModal();
        drawExitConfirmationModal(exitRequested, exitConfirmed);
        gui->EndImGui();
    }

    void EditorScene::drawExitConfirmationModal(bool& exitRequested, bool& exitConfirmed) const
    {
        constexpr const char* kPopupId = "Exit Editor";

        if (exitRequested && !hasUnsavedChanges())
        {
            exitRequested = false;
            exitConfirmed = true;
            return;
        }

        if (exitRequested && !ImGui::IsPopupOpen(kPopupId))
        {
            ImGui::OpenPopup(kPopupId);
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
        ImGui::SetNextWindowSize(ImVec2{440.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::TextWrapped("You have unsaved changes. Exit without saving? [Y/N]");
            ImGui::Spacing();
            constexpr float exitButtonWidth = 180.0f;
            constexpr float cancelButtonWidth = 120.0f;
            const float buttonsWidth = exitButtonWidth + cancelButtonWidth + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonsWidth) * 0.5f);
            const bool confirm =
                ImGui::Button("Exit Without Saving (Y)", ImVec2{exitButtonWidth, 0.0f}) ||
                ImGui::IsKeyPressed(ImGuiKey_Y);
            ImGui::SameLine();
            const bool cancel =
                ImGui::Button("Cancel (N)", ImVec2{cancelButtonWidth, 0.0f}) ||
                ImGui::IsKeyPressed(ImGuiKey_N);
            if (confirm)
            {
                exitRequested = false;
                exitConfirmed = true;
                ImGui::CloseCurrentPopup();
            }
            else if (cancel)
            {
                exitRequested = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorScene::drawHierarchyContextMenu() const
    {
        constexpr const char* kPopupId = "hierarchy_context_menu";

        if (const auto entity = gui->ConsumeHierarchyContextEntity(); entity.has_value())
        {
            hierarchyContextEntity = *entity;
            ImGui::OpenPopup(kPopupId);
        }

        if (ImGui::BeginPopup(kPopupId))
        {
            if (!sys->registry->valid(hierarchyContextEntity))
            {
                ImGui::CloseCurrentPopup();
            }
            else
            {
                if (ImGui::MenuItem("Copy", "Ctrl+C"))
                {
                    const auto selected = selection->Selected();
                    if (std::ranges::find(selected, hierarchyContextEntity) != selected.end())
                    {
                        copyEntitiesToClipboard(selected);
                    }
                    else
                    {
                        copyEntitiesToClipboard({hierarchyContextEntity});
                    }
                }
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboard()))
                {
                    PasteClipboard();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Create Flatpack"))
                {
                    createFlatpackFromEntity(hierarchyContextEntity);
                }
            }
            ImGui::EndPopup();
        }
    }

    void EditorScene::handleFileShortcuts() const
    {
        constexpr ImGuiInputFlags shortcutFlags = ImGuiInputFlags_RouteGlobal;
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, shortcutFlags))
        {
            saveMap();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, shortcutFlags))
        {
            openLoadMapBrowser();
        }
    }

    void EditorScene::handleClipboardShortcuts() const
    {
        // ImGuiMod_Ctrl maps to Cmd on macOS, and RouteGlobal yields to an active
        // text field that already owns Ctrl+C/V, so this needs no manual modifier
        // or focus handling.
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal))
        {
            CopySelection();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal))
        {
            PasteClipboard();
        }
    }

    void EditorScene::handleHistoryShortcuts() const
    {
        if (!history) return;
        // Ignore while a transaction is mid-flight (gizmo edit session, inspector
        // drag): the open transaction owns the registry state right now.
        if (history->HasActiveTransaction()) return;

        // Redo first: Ctrl+Z is a prefix of Ctrl+Shift+Z.
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
        {
            history->Redo();
        }
        else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
        {
            history->Undo();
        }
    }

    void EditorScene::handleInspectorEdit(const editor::EditorGui::InspectorEditResult& result) const
    {
        if (!history) return;

        if (result.began && !history->HasActiveTransaction())
        {
            history->BeginFromBaseline(editor::EditAction::EditField);
        }
        if (result.changed)
        {
            refreshSceneWindows();
        }
        if (result.committed && history->HasActiveTransaction())
        {
            history->Commit();
        }
    }

    void EditorScene::onHistoryApplied(const std::vector<entt::entity>& restored) const
    {
        // Mirror the post-load/post-paste fixups: re-hook the lit shader and re-derive
        // collision bounds from the restored world transforms.
        applyLitShaderToLoadedRenderables();
        if (transformEditor)
        {
            for (const auto entity : restored)
            {
                if (sys->registry->valid(entity)) transformEditor->RefreshCollisionBoundsRecursive(entity);
            }
        }
        if (sys->lightSubSystem) sys->lightSubSystem->RefreshLights();

        if (selection)
        {
            selection->Clear();
            bool first = true;
            for (const auto entity : restored)
            {
                if (!sys->registry->valid(entity)) continue;
                if (first)
                {
                    (void)selection->Select(entity);
                    first = false;
                }
                else
                {
                    (void)selection->Toggle(entity);
                }
            }
        }

        refreshSceneWindows();
        refreshOverlay();
    }

    void EditorScene::CopySelection() const
    {
        copyEntitiesToClipboard(selection->Selected());
    }

    void EditorScene::copyEntitiesToClipboard(const std::vector<entt::entity>& roots) const
    {
        if (roots.empty()) return;
        entityOperations->CopyEntities(roots);
    }

    bool EditorScene::HasClipboard() const
    {
        return entityOperations->HasClipboard();
    }

    void EditorScene::PasteClipboard() const
    {
        if (!entityOperations->HasClipboard()) return;

        const auto newRoots = entityOperations->PasteClipboard();
        if (newRoots.empty()) return;

        // Match the post-placement fixups PlaceFlatpackAt relies on: the pasted
        // renderables need the lit shader hooked up and their collision bounds
        // re-derived from the freshly applied world transforms.
        applyLitShaderToLoadedRenderables();
        if (transformEditor)
        {
            for (const auto root : newRoots)
            {
                transformEditor->RefreshCollisionBoundsRecursive(root);
            }
        }

        if (history) history->RecordCreate(editor::EditAction::Paste, newRoots);

        selection->Clear();
        bool first = true;
        for (const auto root : newRoots)
        {
            if (first)
            {
                (void)selection->Select(root);
                first = false;
            }
            else
            {
                (void)selection->Toggle(root);
            }
        }

        refreshSceneWindows();
        refreshOverlay();
    }

    void EditorScene::createFlatpackFromEntity(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) return;

        const auto safeName = hierarchyTree ? hierarchyTree->GetEntityName(entity)
                                            : std::format("entity_{}", entt::to_integral(entity));
        const std::filesystem::path flatpacksDir{"resources/flatpacks"};
        const auto outputPath = flatpacksDir / (safeName + ".flatpack");
        if (editor::SaveFlatpack(*sys->registry, entity, outputPath.string().c_str()))
        {
            std::cout << "Flatpack saved: " << outputPath << std::endl;
            refreshFlatpackCatalog();
        }
        else
        {
            std::cerr << "ERROR: Failed to save flatpack: " << outputPath << std::endl;
        }
    }

    void EditorScene::refreshFlatpackCatalog() const
    {
        auto catalog = editor::ListFlatpacks(std::filesystem::path{"resources/flatpacks"});
        std::vector<editor::EditorGui::FlatpackEntry> entries;
        entries.reserve(catalog.size());
        for (auto& item : catalog)
        {
            entries.push_back({.displayName = std::move(item.displayName), .path = std::move(item.path)});
        }
        if (gui) gui->SetFlatpacks(std::move(entries));
    }

    std::optional<entt::entity> EditorScene::PlaceFlatpackAt(
        const std::filesystem::path& path, const Vector3 anchor) const
    {
        const auto root = editor::LoadFlatpack(*sys->registry, path.string().c_str(), anchor);
        if (root == entt::null) return std::nullopt;

        // The loaded subtree has Renderables without an UberShaderComponent;
        // applyLitShaderToLoadedRenderables attaches one with Lit set, matching
        // the shader hookup the rest of the editor relies on. Bounds need to be
        // re-derived from the new world transforms (the saved boxes are stale).
        applyLitShaderToLoadedRenderables();
        if (transformEditor) transformEditor->RefreshCollisionBoundsRecursive(root);
        return root;
    }

    void EditorScene::drawMainMenuBar(bool& exitRequested) const
    {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load Map", "Ctrl+O"))
            {
                openLoadMapBrowser();
            }
            if (ImGui::MenuItem("Save Map", "Ctrl+S"))
            {
                saveMap();
            }
            if (ImGui::MenuItem("Save Map As..."))
            {
                openSaveMapBrowser();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                exitRequested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            const bool historyBusy = history && history->HasActiveTransaction();
            const bool canUndo = history && !historyBusy && history->CanUndo();
            const bool canRedo = history && !historyBusy && history->CanRedo();
            const std::string undoLabel = history ? history->UndoLabel() : "Undo";
            const std::string redoLabel = history ? history->RedoLabel() : "Redo";
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo))
            {
                history->Undo();
            }
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Shift+Z", false, canRedo))
            {
                history->Redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Snap To Grid", nullptr, snapToGrid))
            {
                setSnapToGrid(!snapToGrid);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add"))
        {
            if (ImGui::MenuItem("Light"))
            {
                addLight();
            }
            if (ImGui::MenuItem("Spawner"))
            {
                addSpawner();
            }
            if (ImGui::MenuItem("Trigger Volume"))
            {
                addTriggerVolume();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    void EditorScene::drawFileBrowsers() const
    {
        if (loadMapBrowser)
        {
            loadMapBrowser->Display();
            if (loadMapBrowser->HasSelected())
            {
                loadMap(loadMapBrowser->GetSelected());
                loadMapBrowser->ClearSelected();
            }
        }

        if (saveMapBrowser)
        {
            saveMapBrowser->Display();
            if (saveMapBrowser->HasSelected())
            {
                saveMapAs(saveMapBrowser->GetSelected());
                saveMapBrowser->ClearSelected();
            }
        }
    }

    void EditorScene::openLoadMapBrowser() const
    {
        if (!loadMapBrowser) return;
        loadMapBrowser->SetDirectory(browserDirectory());
        loadMapBrowser->Open();
    }

    void EditorScene::openSaveMapBrowser() const
    {
        if (!saveMapBrowser) return;
        saveMapBrowser->SetDirectory(browserDirectory());
        saveMapBrowser->SetInputName(
            currentMapPath.empty() ? DEFAULT_SAVE_FILENAME : currentMapPath.filename().string());
        saveMapBrowser->Open();
    }

    std::filesystem::path EditorScene::browserDirectory() const
    {
        return defaultBrowserDirectory(currentMapPath, editorSettings);
    }

    void EditorScene::addLight() const
    {
        Vector3 position =
            Vector3Add(sys->camera->getRaylibCam()->target, {0.0f, DEFAULT_LIGHT_HEIGHT_OFFSET, 0.0f});
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = Vector3Add(*snappedPosition, {0.0f, DEFAULT_LIGHT_HEIGHT_OFFSET, 0.0f});
        }

        const auto entity = sys->registry->create();
        sys->registry->emplace<editor::EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = lightLabel(entity);

        sys->registry->emplace<Light>(
            entity,
            Light{
                .type = LIGHT_POINT,
                .enabled = true,
                .position = position,
                .target = Vector3Zero(),
                .color = DEFAULT_LIGHT_COLOR,
                .brightness = DEFAULT_LIGHT_BRIGHTNESS});

        sys->lightSubSystem->RefreshLights();
        if (history) history->RecordCreate(editor::EditAction::AddLight, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::addSpawner() const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = *snappedPosition;
        }

        const auto entity = sys->registry->create();
        sys->registry->emplace<editor::EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = spawnerLabel(entity);

        sys->registry->emplace<Spawner>(
            entity,
            Spawner{.name = "", .type = SpawnerType::ENEMY, .pos = position, .rot = Vector3Zero()});

        if (history) history->RecordCreate(editor::EditAction::AddSpawner, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::addTriggerVolume() const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = *snappedPosition;
        }

        const auto entity = sys->registry->create();
        sys->registry->emplace<editor::EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = triggerLabel(entity);

        sys->registry->emplace<TriggerVolume>(entity);

        if (history) history->RecordCreate(editor::EditAction::AddTriggerVolume, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::loadMap(const std::filesystem::path& path) const
    {
        const auto selectedPath = ensureMapExtension(path);
        const auto pathString = selectedPath.string();
        if (!editor::IsEditorLayoutMap(pathString.c_str()))
        {
            std::cerr << "ERROR: Not an editor layout map: " << pathString << std::endl;
            return;
        }

        clearCurrentMap();
        if (!editor::LoadMap(sys->registry, pathString.c_str())) return;
        currentMapPath = selectedPath;
        SetSceneName(sceneNameFromPath(currentMapPath));
        rememberCurrentMapPath();
        refreshAfterMapLoad();
        if (history) history->MarkSaved();
        saveFeedbackRemaining = 0.0f;
        saveFeedbackStatus.clear();
    }

    void EditorScene::saveMap() const
    {
        if (currentMapPath.empty())
        {
            openSaveMapBrowser();
            return;
        }
        saveMapAs(currentMapPath);
    }

    void EditorScene::saveMapAs(const std::filesystem::path& path) const
    {
        ensureDefaultMapBase();
        currentMapPath = ensureMapExtension(path);
        const auto pathString = currentMapPath.string();
        std::vector<entt::entity> hierarchyOrder;
        if (hierarchyTree)
        {
            const auto entries = hierarchyTree->CollectSceneObjectEntries();
            hierarchyOrder.reserve(entries.size());
            for (const auto& entry : entries)
            {
                hierarchyOrder.push_back(entry.entity);
            }
        }
        editor::SaveMap(*sys->registry, pathString.c_str(), hierarchyOrder);
        SetSceneName(sceneNameFromPath(currentMapPath));
        rememberCurrentMapPath();
        markSceneSaved(currentMapPath);
    }

    void EditorScene::restoreLastOpenedMap() const
    {
        if (editorSettings == nullptr || editorSettings->lastOpenedMap.empty()) return;

        const std::filesystem::path path{editorSettings->lastOpenedMap};
        if (!std::filesystem::is_regular_file(path))
        {
            std::cerr << "WARNING: Last opened editor map no longer exists: " << path << std::endl;
            return;
        }

        loadMap(path);
    }

    void EditorScene::rememberCurrentMapPath() const
    {
        if (editorSettings == nullptr || currentMapPath.empty()) return;

        editorSettings->lastOpenedMap = currentMapPath.string();
        if (!currentMapPath.parent_path().empty())
        {
            editorSettings->lastVisitedDirectory = currentMapPath.parent_path().string();
        }
        if (onEditorSettingsChanged)
        {
            onEditorSettingsChanged();
        }
    }

    void EditorScene::clearCurrentMap() const
    {
        if (history)
        {
            history->Clear();
        }
        if (editorModes)
        {
            editorModes->ChangeState<editor::EditorSelectState>();
        }
        if (selection)
        {
            selection->Clear();
        }
        if (gui)
        {
            gui->HideDeleteConfirmation();
        }

        std::vector<entt::entity> mapEntities;
        for (const auto entity : sys->registry->view<editor::EditorMapEntity>())
        {
            mapEntities.push_back(entity);
        }

        for (const auto entity : mapEntities)
        {
            if (sys->registry->valid(entity))
            {
                sys->registry->destroy(entity);
            }
        }
    }

    void EditorScene::ensureDefaultMapBase() const
    {
        // TODO: What?
        bool hasMapBase = false;
        const auto existingBaseView =
            sys->registry->view<editor::EditorMapEntity, sgTransform, Renderable, Collideable>();
        for (const auto entity : existingBaseView)
        {
            auto& transform = existingBaseView.get<sgTransform>(entity);
            if (!isMapBaseRenderable(transform)) continue;
            auto& renderable = existingBaseView.get<Renderable>(entity);
            hasMapBase = true;
            if (!sys->registry->any_of<editor::EditorMapBase>(entity))
            {
                sys->registry->emplace<editor::EditorMapBase>(entity);
            }
            if (!sys->registry->any_of<editor::AssetReference>(entity))
            {
                if (const auto* model = renderable.GetModel(); model != nullptr)
                {
                    sys->registry->emplace<editor::AssetReference>(
                        entity, editor::AssetReference{.assetKey = model->GetKey()});
                }
            }
            auto& collideable = existingBaseView.get<Collideable>(entity);
            collideable.SetCollisionLayer(collision_layers::Background);
            collideable.isStatic = true;
            collideable.blocksNavigation = false;
            collideable.active = true;
            renderable.active = false;
        }

        if (hasMapBase) return;

        if (!modelKeyAvailable(DEFAULT_MAP_BASE_MODEL_KEY))
        {
            std::cerr << "ERROR: Cannot create default map base. Missing model key: " << DEFAULT_MAP_BASE_MODEL_KEY
                      << std::endl;
            return;
        }

        const auto entity = sys->registry->create();
        sys->registry->emplace<editor::EditorMapEntity>(entity);
        sys->registry->emplace<MetaData>(entity);
        sys->registry->emplace<editor::EditorMapBase>(entity);
        sys->registry->emplace<editor::AssetReference>(
            entity, editor::AssetReference{.assetKey = DEFAULT_MAP_BASE_MODEL_KEY});

        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.scale.world = {DEFAULT_MAP_BASE_SIZE, 1.0f, DEFAULT_MAP_BASE_SIZE};
        transform.name = DEFAULT_MAP_BASE_NAME;

        auto model = ResourceManager::GetInstance().GetModelView(DEFAULT_MAP_BASE_MODEL_KEY);
        auto& renderable = sys->registry->emplace<Renderable>(entity, std::move(model), MatrixIdentity());
        renderable.active = false;

        const BoundingBox localBounds = {
            {-0.5f, -DEFAULT_MAP_BASE_HALF_HEIGHT, -0.5f}, {0.5f, DEFAULT_MAP_BASE_HALF_HEIGHT, 0.5f}};
        auto& collideable = sys->registry->emplace<Collideable>(entity, localBounds, transform.GetMatrixNoRot());
        collideable.SetCollisionLayer(collision_layers::Background);
        collideable.isStatic = true;
        collideable.blocksNavigation = false;
        collideable.active = true;
    }

    void EditorScene::syncLightTransforms() const
    {
        const auto view = sys->registry->view<Light, sgTransform>();
        for (const auto entity : view)
        {
            auto& light = view.get<Light>(entity);
            light.position = view.get<sgTransform>(entity).GetWorldPos();
        }
    }

    void EditorScene::refreshAfterMapLoad() const
    {
        ensureDefaultMapBase();
        applyLitShaderToLoadedRenderables();
        giveTransformsToLights();
        placementController->Initialize();
        selection->Clear();
        editorModes->ChangeState<editor::EditorSelectState>();
        refreshOverlay();
        refreshSceneWindows();
    }

    editor::EditorGui::AssetRenameResult EditorScene::handleAssetFileRename(
        const std::size_t index,
        const std::string& requestedFileName) const
    {
        if (!assetCatalog || index >= assetCatalog->Size())
        {
            return {.message = "Asset no longer exists."};
        }

        const auto entries = assetCatalog->AssetEntries();
        if (index >= entries.size())
        {
            return {.message = "Asset no longer exists."};
        }

        const auto& entry = entries[index];
        if (entry.sourcePath.empty())
        {
            return {.message = "This asset has no source model file to rename."};
        }

        const auto oldSourcePath = entry.sourcePath;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(oldSourcePath, ec))
        {
            return {.message = std::format("Source file is missing: {}", oldSourcePath.string())};
        }

        std::string error;
        const auto newSourcePath = BuildRenameTargetPath(oldSourcePath, requestedFileName, error);
        if (!error.empty())
        {
            return {.message = error};
        }
        if (EquivalentPath(oldSourcePath, newSourcePath))
        {
            return {.message = "File name is unchanged."};
        }
        if (const auto collision = CheckTargetCollision(oldSourcePath, newSourcePath); collision.has_value())
        {
            return {.message = *collision};
        }

        const auto oldKey = entry.modelKey;
        const auto newKey = StripPath(newSourcePath.string());
        if (newKey.empty())
        {
            return {.message = "File name must have a non-empty stem."};
        }
        if (oldKey != newKey && ResourceManager::GetInstance().HasModelKey(newKey))
        {
            return {.message = std::format("An asset named {} is already loaded.", newKey)};
        }

        const auto oldDefaultsPath = entry.defaultsPath;
        const auto newDefaultsPath = editor::EditorAssetCatalog::AssetDefaultsPathForModelKey(newKey);
        if (const auto collision = CheckTargetCollision(oldDefaultsPath, newDefaultsPath); collision.has_value())
        {
            return {.message = *collision};
        }

        std::filesystem::rename(oldSourcePath, newSourcePath, ec);
        if (ec)
        {
            return {.message = std::format("Could not rename source file: {}", ec.message())};
        }

        bool movedDefaults = false;
        if (std::filesystem::exists(oldDefaultsPath, ec) && !EquivalentPath(oldDefaultsPath, newDefaultsPath))
        {
            std::filesystem::rename(oldDefaultsPath, newDefaultsPath, ec);
            if (ec)
            {
                std::error_code rollbackEc;
                std::filesystem::rename(newSourcePath, oldSourcePath, rollbackEc);
                return {.message = std::format("Could not rename defaults file: {}", ec.message())};
            }
            movedDefaults = true;
        }

        if (!ResourceManager::GetInstance().RenameModelAsset(oldKey, newKey, newSourcePath.string()))
        {
            std::error_code rollbackEc;
            if (movedDefaults)
            {
                std::filesystem::rename(newDefaultsPath, oldDefaultsPath, rollbackEc);
            }
            std::filesystem::rename(newSourcePath, oldSourcePath, rollbackEc);
            return {.message = "Could not update the loaded asset registry."};
        }

        assetCatalog->RenameAsset(index, newKey);

        for (const auto entity : sys->registry->view<editor::AssetReference>())
        {
            auto& assetReference = sys->registry->get<editor::AssetReference>(entity);
            if (assetReference.assetKey == oldKey)
            {
                assetReference.assetKey = newKey;
            }
        }

        for (const auto entity : sys->registry->view<Renderable>())
        {
            auto& renderable = sys->registry->get<Renderable>(entity);
            const auto* model = renderable.GetModel();
            if (model == nullptr || model->GetKey() != oldKey) continue;

            auto replacement = ResourceManager::GetInstance().GetModelView(newKey);
            replacement.SetTransform(renderable.initialTransform);
            renderable.SetModel(std::move(replacement));
            if (sys->registry->any_of<UberShaderComponent>(entity))
            {
                auto& uber = sys->registry->get<UberShaderComponent>(entity);
                if (auto* replacementModel = renderable.GetModel(); replacementModel != nullptr)
                {
                    replacementModel->SetShader(uber.shader);
                }
            }
        }

        if (history) history->MarkDirty();
        refreshOverlay();
        refreshSceneWindows();

        const auto updatedEntries = assetCatalog->AssetEntries();
        return {
            .renamed = true,
            .message = std::format("Renamed to {}.", newSourcePath.filename().string()),
            .updatedEntry = updatedEntries.at(index)};
    }

    void EditorScene::moveHierarchyEntity(const editor::EditorGui::HierarchyMoveRequest& request) const
    {
        const auto dragged = request.dragged;
        auto newParent = request.newParent;
        auto insertBefore = request.insertBefore;

        if (!sys->registry->valid(dragged) || !sys->registry->any_of<sgTransform>(dragged)) return;
        if (newParent != entt::null &&
            (!sys->registry->valid(newParent) || !sys->registry->any_of<sgTransform>(newParent)))
        {
            return;
        }
        if (dragged == newParent) return;

        if (insertBefore != entt::null)
        {
            if (!sys->registry->valid(insertBefore) || !sys->registry->any_of<sgTransform>(insertBefore) ||
                insertBefore == dragged)
            {
                return;
            }

            const auto insertParent = sys->registry->get<sgTransform>(insertBefore).GetParent();
            if (insertParent != newParent)
            {
                return;
            }
        }

        // Refuse cycles: walk newParent's ancestor chain. If dragged appears, the
        // requested parenting would put dragged below itself.
        for (auto cur = newParent; cur != entt::null;)
        {
            if (!sys->registry->any_of<sgTransform>(cur)) break;
            if (cur == dragged) return;
            cur = sys->registry->get<sgTransform>(cur).GetParent();
        }

        if (history) history->Begin(editor::EditAction::Reparent, {dragged});
        sys->transformSystem->SetParent(dragged, newParent, insertBefore);
        hierarchyTree->NoteHierarchyMove(dragged, newParent, insertBefore);
        if (history) history->Commit();
        refreshSceneWindows();
    }

    bool EditorScene::HandleEscapePressed() const
    {
        return editorModes->HandleEscapePressed();
    }

    bool EditorScene::ConsumeDockLayoutChanged() const
    {
        return gui && gui->ConsumeDockLayoutChanged();
    }

    void EditorScene::SetViewportFullscreen(const bool fullscreen)
    {
        viewportFullscreen = fullscreen;
    }

    void EditorScene::SetSceneName(const std::string& sceneName) const
    {
        gui->SetSceneName(sceneName);
    }

    EditorScene::EditorScene(
        EngineSystems* _sys,
        editor::EditorDockLayout* dockLayout,
        EditorSettings* _editorSettings,
        std::function<void()> _onEditorSettingsChanged)
        : sys(_sys),
          editorSettings(_editorSettings),
          onEditorSettingsChanged(std::move(_onEditorSettingsChanged))
    {
        editor::RegisterDefaultInspectorComponents(inspectorRegistry);
        assetCatalog =
            std::make_unique<editor::EditorAssetCatalog>(editor::EditorAssetCatalog::FromLoadedModels());
        assetCatalog->LoadDefaults();
        modelDefaults = std::make_unique<editor::EditorModelDefaultsController>(
            *assetCatalog, [this]() { return isPlaceState(); }, [this]() { refreshOverlay(); });
        selection = std::make_unique<editor::EditorSelection>(sys);
        pickingService = std::make_unique<editor::EditorPickingService>(sys);
        entityOperations = std::make_unique<editor::EditorEntityOperations>(sys);
        hierarchyTree = std::make_unique<editor::EditorHierarchyTree>(sys);
        placementController = std::make_unique<editor::EditorPlacementController>(sys, *assetCatalog);

        ensureDefaultMapBase();
        applyLitShaderToLoadedRenderables();
        giveTransformsToLights();
        placementController->Initialize();

        transformEditor = std::make_unique<editor::EditorTransformEditor>(sys, [this](const entt::entity entity) {
            if (editorModes)
            {
                editorModes->OnTransformApplied(entity);
            }
        });
        history = std::make_unique<editor::EditorHistory>(
            sys, [this](const std::vector<entt::entity>& restored) { onHistoryApplied(restored); });
        editorModes = std::make_unique<editor::EditorModeStateMachine>(*this, *transformEditor);

        gui = std::make_unique<editor::EditorGui>(
            sys->settings,
            dockLayout,
            assetCatalog->AssetEntries(),
            [this](const std::size_t index) { editorModes->SelectPlaceable(index); },
            [this](const std::size_t index, const std::string& requestedFileName) {
                return handleAssetFileRename(index, requestedFileName);
            },
            [this](std::filesystem::path path) { editorModes->SelectFlatpack(std::move(path)); },
            [this](const editor::EditorGui::SceneSelectionRequest& request) {
                editorModes->SelectSceneFromHierarchy(request);
            },
            [this](const editor::EditorGui::HierarchyMoveRequest& request) { moveHierarchyEntity(request); },
            modelDefaults->Callbacks());
        SetSceneName(UNTITLED_SCENE_NAME);
        restoreLastOpenedMap();
        refreshOverlay();
        refreshSceneWindows();
        refreshFlatpackCatalog();
        loadMapBrowser = std::make_unique<ImGui::FileBrowser>(LOAD_BROWSER_FLAGS);
        loadMapBrowser->SetTitle("Load map");
        loadMapBrowser->SetTypeFilters({".map"});
        loadMapBrowser->SetDirectory(browserDirectory());

        saveMapBrowser = std::make_unique<ImGui::FileBrowser>(SAVE_BROWSER_FLAGS);
        saveMapBrowser->SetTitle("Save map as");
        saveMapBrowser->SetTypeFilters({".map"});
        saveMapBrowser->SetDirectory(browserDirectory());
        saveMapBrowser->SetInputName(DEFAULT_SAVE_FILENAME);
    }

    EditorScene::~EditorScene() = default;
} // namespace sage

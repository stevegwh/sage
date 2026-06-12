#include "EditorScene.hpp"

#include "EditorAssetRename.hpp"
#include "EditorComponents.hpp"
#include "EditorFlatpack.hpp"
#include "EditorFocus.hpp"
#include "engine/AudioManager.hpp"
#include "engine/Camera.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/DynamicRenderable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/Cursor.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Light.hpp"
#include "engine/LightManager.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/TerrainMesh.hpp"
#include "engine/systems/CollisionSystem.hpp"
#include "engine/systems/RenderSystem.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/UserInput.hpp"

#include "imfilebrowser.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "rlImGui.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
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
        constexpr const char* SCRIPTS_DIRECTORY = "resources/scripts";
        constexpr const char* DEFAULT_MAP_BASE_NAME = "_MAPBASE_EDITOR_BASE";
        constexpr const char* DEFAULT_MAP_BASE_MODEL_KEY = "primitive_plane";
        constexpr float DEFAULT_MAP_BASE_SIZE = 1000.0f;
        constexpr float DEFAULT_MAP_BASE_HALF_HEIGHT = 0.02f;
        constexpr float DEFAULT_LIGHT_HEIGHT_OFFSET = 6.0f;

        bool isMapBaseRenderable(const sgTransform& transform)
        {
            return transform.name.find("_MAPBASE_") != std::string::npos;
        }

        bool modelKeyAvailable(const std::string& key)
        {
            const auto keys = ResourceManager::GetInstance().GetModelKeys(true);
            return std::ranges::find(keys, key) != keys.end();
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
            auto& renderable = sys->registry->get<Renderable>(entity);
            if (renderable.GetModel() == nullptr) continue;
            if (!sys->registry->any_of<UberShaderComponent>(entity))
            {
                auto& uber = sys->registry->emplace<UberShaderComponent>(
                    entity, renderable.GetModel()->GetMaterialCount());
                uber.SetFlagAll(UberShaderComponent::Flags::Lit);
            }
            // Undo/redo restores Animation and the Renderable independently of the
            // shader component, so re-derive the Skinned flag from Animation presence.
            auto& uber = sys->registry->get<UberShaderComponent>(entity);
            if (sys->registry->any_of<Animation>(entity))
            {
                uber.SetFlagAll(UberShaderComponent::Flags::Skinned);
            }
            else
            {
                uber.ClearFlagAll(UberShaderComponent::Flags::Skinned);
            }
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
                transform.name = std::format("light_{}", entt::to_integral(entity));
                transform.position.world = position;
            }
        }
    }

    void EditorScene::refreshOverlay() const
    {
        const auto defaultsStatus = modelDefaults->Status(describeSelectedAsset());
        gui->SetOverlayStatus(editorModes->GetStateName(), describeCursorPosition());
        const bool flatpackOpen = flatpackSession && flatpackSession->IsActive();
        if (flatpackOpen)
        {
            gui->SetSaveStatus(flatpackSession->CurrentSaveStatus(), flatpackSession->HasUnsavedChanges());
        }
        else
        {
            gui->SetSaveStatus(mapController->CurrentSaveStatus(), mapController->HasUnsavedChanges());
        }
        gui->SetSceneTabs(
            {.mapLabel = mapController->CurrentSceneName(),
             .mapDirty = flatpackOpen ? flatpackSession->StashedMapHadUnsavedChanges()
                                      : mapController->HasUnsavedChanges(),
             .flatpackOpen = flatpackOpen,
             .flatpackLabel = flatpackOpen ? flatpackSession->FlatpackName() : std::string{},
             .flatpackDirty = flatpackOpen && flatpackSession->HasUnsavedChanges()});
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

        auto target = editor::ComputeFocusTarget(*sys->registry, selectedEntities);
        if (!target.has_value())
        {
            const auto primary = selection->Active();
            if (!primary.has_value()) return;
            target = editor::FocusTarget{.position = sys->registry->get<sgTransform>(*primary).GetWorldPos()};
        }

        const float focusDistance =
            std::max(EDITOR_FOCUS_CAMERA_DISTANCE, target->radius * EDITOR_FOCUS_RADIUS_PADDING);
        sys->camera->FocusPoint(target->position, focusDistance);
    }

    void EditorScene::focusSelectedObjectInHierarchy() const
    {
        const auto selectedEntity = selection->Active();
        if (!selectedEntity.has_value()) return;
        gui->FocusHierarchyOnEntity(*selectedEntity);
    }

    // Middle mouse orbits the camera around its target; right mouse drags (pans) it across the
    // ground plane. A drag may only begin while the cursor is over the render viewport and the
    // editor UI is not capturing the mouse, but continues until the button is released so the
    // motion is not interrupted when the cursor leaves the viewport.
    void EditorScene::handleMouseCameraControls() const
    {
        const bool uiBlocksMouse = !viewportFullscreen && gui && gui->WantsMouseCapture();
        const bool gizmoDragging = transformEditor && transformEditor->IsGizmoDragging();
        const bool canBeginDrag = !uiBlocksMouse && !gizmoDragging &&
                                  sys->settings->IsPointInRenderViewport(GetMousePosition());

        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && canBeginDrag)
        {
            orbitingCamera = true;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && canBeginDrag)
        {
            panningCamera = true;
        }

        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        {
            orbitingCamera = false;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            panningCamera = false;
        }

        const Vector2 delta = GetMouseDelta();
        if (orbitingCamera)
        {
            sys->camera->RotateByMouseDelta(delta);
        }
        if (panningCamera)
        {
            sys->camera->PanByMouseDelta(delta);
        }
    }

    void EditorScene::Update() const
    {
        mapController->Update();
        flatpackSession->Update();

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

        handleMouseCameraControls();

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
        // Trigger volumes are Collideables flagged isTrigger; draw their world box so the
        // otherwise-invisible region is visible and editable.
        for (const auto entity : sys->registry->view<Collideable>())
        {
            const auto& collideable = sys->registry->get<Collideable>(entity);
            if (collideable.isTrigger) DrawBoundingBox(collideable.worldBoundingBox, GREEN);
        }

        for (const auto entity : selection->SelectedWithChildren())
        {
            if (!sys->registry->valid(entity) || !sys->registry->any_of<Collideable>(entity)) continue;
            const auto& collideable = sys->registry->get<Collideable>(entity);

            // A mesh collider is the render mesh itself, so visualize it by
            // re-drawing the same GPU buffers in wireframe (selection-only, so
            // cheap); its box is only the broad-phase, shown de-emphasised.
            if (RequiresMeshCollision(collideable.collisionLayer) &&
                sys->registry->any_of<sgTransform>(entity))
            {
                const auto& transform = sys->registry->get<sgTransform>(entity);
                rlEnableWireMode();
                if (sys->registry->any_of<Renderable>(entity))
                {
                    if (auto* model = sys->registry->get<Renderable>(entity).GetModel(); model != nullptr)
                    {
                        model->Draw(
                            transform.GetWorldPos(), transform.GetWorldRot(), transform.GetScale(), GREEN);
                    }
                }
                else if (sys->registry->any_of<DynamicRenderable>(entity))
                {
                    const auto& renderable = sys->registry->get<DynamicRenderable>(entity);
                    if (renderable.GetModel() != nullptr)
                    {
                        renderable.Draw(
                            transform.GetWorldPos(),
                            {0.0f, 1.0f, 0.0f},
                            transform.GetWorldRot().y,
                            transform.GetScale(),
                            GREEN);
                    }
                }
                rlDisableWireMode();
                DrawBoundingBox(collideable.worldBoundingBox, Fade(ORANGE, 0.35f));
            }
            else
            {
                DrawBoundingBox(collideable.worldBoundingBox, ORANGE);
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
        const auto sceneTabAction = gui->DrawSceneTabBar();
        if (sceneTabAction.mapSelected || sceneTabAction.flatpackCloseRequested)
        {
            flatpackSession->RequestClose();
        }
        flatpackSession->DrawCloseConfirmationModal();
        handleFileShortcuts();
        mapController->DrawBrowsers();
        drawScriptBrowser();
        drawCollisionMatrixWindow();
        drawTerrainBrushWindow();
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

        // While a flatpack is open the live history belongs to it, and the map's
        // own dirty flag is parked in the session stash — check both.
        const bool hasUnsavedChanges = mapController->HasUnsavedChanges() ||
                                       flatpackSession->HasUnsavedChanges() ||
                                       flatpackSession->StashedMapHadUnsavedChanges();
        if (exitRequested && !hasUnsavedChanges)
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
        // Save must fire no matter what currently owns the keyboard. Plain RouteGlobal
        // yields to a focused window or an active item (e.g. a search/inspector field),
        // so OverFocused|OverActive force Ctrl/Cmd+S to win in every context.
        constexpr ImGuiInputFlags saveFlags = ImGuiInputFlags_RouteGlobal |
            ImGuiInputFlags_RouteOverFocused | ImGuiInputFlags_RouteOverActive;
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, saveFlags))
        {
            if (flatpackSession->IsActive())
            {
                flatpackSession->Save();
            }
            else
            {
                mapController->SaveMap();
            }
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal) &&
            !flatpackSession->IsActive())
        {
            mapController->OpenLoadBrowser();
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

    // A mesh collider's box is the derived broad-phase, so whenever an inspector
    // edit leaves a selected Collideable on a mesh-collision layer (e.g. the
    // layer was just switched to GeometryComplex), refit the box from the meshes
    // so rays keep reaching them. Idempotent, and it runs inside the active edit
    // transaction so the refit undoes together with the field change.
    void EditorScene::refitMeshColliderBounds() const
    {
        for (const auto entity : selection->Selected())
        {
            if (!sys->registry->valid(entity) || !sys->registry->all_of<Collideable>(entity)) continue;
            if (!RequiresMeshCollision(sys->registry->get<Collideable>(entity).collisionLayer)) continue;

            if (sys->registry->all_of<Terrain>(entity))
            {
                UpdateTerrainCollideableBounds(*sys->registry, entity);
            }
            else if (transformEditor)
            {
                transformEditor->RefreshCollisionBounds(entity);
            }
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
            refitMeshColliderBounds();
            refreshSceneWindows();
        }
        if (result.committed && history->HasActiveTransaction())
        {
            history->Commit();
        }

        if (!history->HasActiveTransaction())
        {
            if (result.addScriptClicked && scriptBrowser)
            {
                std::error_code ec;
                std::filesystem::create_directories(SCRIPTS_DIRECTORY, ec);
                scriptBrowser->SetDirectory(SCRIPTS_DIRECTORY);
                scriptBrowser->Open();
            }
            if (result.addAnimationClicked) addAnimationToSelection();
            if (result.addMoveableActorClicked) addMoveableActorToSelection();
            if (result.removeComponent == "Script") removeScriptFromSelection();
            if (result.removeComponent == "Animation") removeAnimationFromSelection();
            if (result.removeComponent == "Moveable Actor") removeMoveableActorFromSelection();
        }
    }

    void EditorScene::drawScriptBrowser() const
    {
        if (!scriptBrowser) return;
        scriptBrowser->Display();
        if (scriptBrowser->HasSelected())
        {
            attachScriptToSelection(scriptBrowser->GetSelected());
            scriptBrowser->ClearSelected();
        }
    }

    void EditorScene::attachScriptToSelection(const std::filesystem::path& scriptFile) const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        auto file = scriptFile;
        if (file.extension() != ".lua") file += ".lua";

        // Typed as a new filename in the dialog: write a template script there.
        if (!std::filesystem::exists(file))
        {
            std::ofstream out{file};
            out << "-- Lifecycle callbacks are optional globals; delete the ones you don't need.\n"
                   "-- API: entity, GetTransform([entity]), GetCollideable([entity]), GetAnimation([entity]),\n"
                   "--      Vec3(x,y,z), Log(msg)\n"
                   "\n"
                   "function Awake()\n"
                   "end\n"
                   "\n"
                   "function Start()\n"
                   "end\n"
                   "\n"
                   "function Update(dt)\n"
                   "end\n";
        }

        // ScriptSystem resolves paths against the working directory, so store the
        // path relative to it (forward slashes); keep the absolute path only if
        // the file lives outside the project tree.
        std::error_code ec;
        auto relative = std::filesystem::relative(file, std::filesystem::current_path(), ec);
        const bool outsideProject = ec || relative.empty() || relative.native().starts_with("..");
        const auto path = outsideProject ? file.generic_string() : relative.generic_string();

        history->Begin(editor::EditAction::AddScript, selected);
        for (const auto entity : selected)
        {
            if (sys->registry->any_of<ScriptComponent>(entity))
            {
                sys->registry->get<ScriptComponent>(entity).scriptPath = path;
            }
            else
            {
                sys->registry->emplace<ScriptComponent>(entity, path);
            }
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::removeScriptFromSelection() const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        history->Begin(editor::EditAction::RemoveScript, selected);
        for (const auto entity : selected)
        {
            sys->registry->remove<ScriptComponent>(entity);
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::addAnimationToSelection() const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        auto& reg = *sys->registry;
        auto& resources = ResourceManager::GetInstance();

        // Only entities whose model has packed animation data qualify; the rest of
        // the selection is left untouched rather than crashing the GetModelAnimation
        // assert downstream.
        std::vector<entt::entity> targets;
        for (const auto entity : selected)
        {
            if (reg.any_of<Animation>(entity)) continue;
            const auto* renderable = reg.try_get<Renderable>(entity);
            if (renderable == nullptr || renderable->GetModel() == nullptr) continue;
            if (!resources.HasModelAnimation(renderable->GetModel()->GetKey())) continue;
            targets.push_back(entity);
        }
        if (targets.empty())
        {
            std::cout << "EditorScene: no selected entity has a model with animation data.\n";
            return;
        }

        history->Begin(editor::EditAction::AddAnimation, targets);
        for (const auto entity : targets)
        {
            auto& renderable = reg.get<Renderable>(entity);
            const auto key = renderable.GetModel()->GetKey();

            // Skinned animation writes the animated pose into the mesh data each
            // frame, so the shared ModelView must become this entity's own copy.
            if (renderable.GetMutable() == nullptr)
            {
                auto mutableModel = resources.CreateModelMutable(key);
                mutableModel.SetTransform(renderable.initialTransform);
                renderable.SetModel(std::move(mutableModel));
            }
            reg.emplace<Animation>(entity, key);
            if (auto* uber = reg.try_get<UberShaderComponent>(entity))
            {
                uber->SetFlagAll(UberShaderComponent::Flags::Skinned);
            }
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::removeAnimationFromSelection() const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        auto& reg = *sys->registry;
        auto& resources = ResourceManager::GetInstance();

        history->Begin(editor::EditAction::RemoveAnimation, selected);
        for (const auto entity : selected)
        {
            if (!reg.any_of<Animation>(entity)) continue;
            reg.remove<Animation>(entity);

            // Return the renderable to the shared view; the private mutable copy
            // only existed for skinning.
            if (auto* renderable = reg.try_get<Renderable>(entity);
                renderable != nullptr && renderable->GetMutable() != nullptr)
            {
                auto view = resources.GetModelView(renderable->GetModel()->GetKey());
                view.SetTransform(renderable->initialTransform);
                renderable->SetModel(std::move(view));
            }
            if (auto* uber = reg.try_get<UberShaderComponent>(entity))
            {
                uber->ClearFlagAll(UberShaderComponent::Flags::Skinned);
            }
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::addMoveableActorToSelection() const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        auto& reg = *sys->registry;
        std::vector<entt::entity> targets;
        for (const auto entity : selected)
        {
            if (!reg.any_of<MoveableActor>(entity)) targets.push_back(entity);
        }
        if (targets.empty()) return;

        history->Begin(editor::EditAction::AddMoveableActor, targets);
        for (const auto entity : targets)
        {
            reg.emplace<MoveableActor>(entity);
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::removeMoveableActorFromSelection() const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        history->Begin(editor::EditAction::RemoveMoveableActor, selected);
        for (const auto entity : selected)
        {
            sys->registry->remove<MoveableActor>(entity);
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::onHistoryApplied(const std::vector<entt::entity>& restored) const
    {
        // Mirror the post-load/post-paste fixups: re-hook the lit shader and re-derive
        // collision bounds from the restored world transforms.
        applyLitShaderToLoadedRenderables();
        // Terrain restores only the authored height field; rebuild the derived
        // mesh and bounds.
        for (const auto entity : restored)
        {
            if (sys->registry->valid(entity) && sys->registry->all_of<Terrain, sgTransform>(entity))
            {
                AttachTerrainRenderable(*sys->registry, entity, *sys->lightSubSystem);
            }
        }
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
        adoptIntoFlatpackRoot(newRoots);

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

    editor::EditorGui::FlatpackRenameResult EditorScene::renameFlatpackFile(
        const std::filesystem::path& path, const std::string& requestedName) const
    {
        // The new name is the display name (file stem); a typed ".flatpack"
        // extension is tolerated rather than doubled up.
        std::filesystem::path newName{requestedName};
        if (newName.extension() == ".flatpack") newName = newName.stem();
        const auto stem = newName.string();
        if (stem.empty())
        {
            return {.message = "Name cannot be empty."};
        }
        if (stem.find('/') != std::string::npos || stem.find('\\') != std::string::npos)
        {
            return {.message = "Name cannot contain path separators."};
        }
        if (flatpackSession->IsActive() && flatpackSession->Path() == path)
        {
            return {.message = "Close the flatpack before renaming it."};
        }

        const auto target = path.parent_path() / (stem + ".flatpack");
        if (target == path)
        {
            return {.renamed = true, .message = "Name unchanged."};
        }
        if (std::filesystem::exists(target))
        {
            return {.message = std::format("'{}' already exists.", stem)};
        }

        std::error_code error;
        std::filesystem::rename(path, target, error);
        if (error)
        {
            return {.message = std::format("Rename failed: {}", error.message())};
        }

        refreshFlatpackCatalog();
        return {.renamed = true};
    }

    void EditorScene::deleteFlatpackFile(const std::filesystem::path& path) const
    {
        // The browser disables Delete for the open flatpack, but guard anyway:
        // the session would recreate the file on its next save.
        if (flatpackSession->IsActive() && flatpackSession->Path() == path)
        {
            std::cerr << "ERROR: Cannot delete a flatpack that is open for editing: " << path << std::endl;
            return;
        }

        std::error_code error;
        if (!std::filesystem::remove(path, error) || error)
        {
            std::cerr << "ERROR: Failed to delete flatpack: " << path
                      << (error ? " (" + error.message() + ")" : "") << std::endl;
        }
        refreshFlatpackCatalog();
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

    void EditorScene::adoptIntoFlatpackRoot(const std::vector<entt::entity>& roots) const
    {
        if (!flatpackSession || !flatpackSession->IsActive()) return;

        const auto sessionRoot = flatpackSession->Root();
        for (const auto entity : roots)
        {
            if (entity == sessionRoot) continue;
            if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) continue;
            auto& transform = sys->registry->get<sgTransform>(entity);
            if (transform.GetParent() != entt::null) continue;
            transform.SetParent(sessionRoot);
        }
    }

    void EditorScene::drawMainMenuBar(bool& exitRequested) const
    {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File"))
        {
            const bool flatpackOpen = flatpackSession->IsActive();
            if (flatpackOpen)
            {
                if (ImGui::MenuItem("Save Flatpack", "Ctrl+S"))
                {
                    flatpackSession->Save();
                }
                if (ImGui::MenuItem("Close Flatpack"))
                {
                    flatpackSession->RequestClose();
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Load Map", "Ctrl+O", false, !flatpackOpen))
            {
                mapController->OpenLoadBrowser();
            }
            if (ImGui::MenuItem("Save Map", flatpackOpen ? nullptr : "Ctrl+S", false, !flatpackOpen))
            {
                mapController->SaveMap();
            }
            if (ImGui::MenuItem("Save Map As...", nullptr, false, !flatpackOpen))
            {
                mapController->OpenSaveBrowser();
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
            ImGui::Separator();
            if (ImGui::MenuItem("Sculpt Terrain", "G", false, editorModes->CanBeginTerrainSculpt()))
            {
                editorModes->BeginTerrainSculptOnSelection();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add"))
        {
            if (ImGui::MenuItem("Light"))
            {
                addLight();
            }
            // Spawners, trigger volumes and terrains persist via map-only
            // streams, so a flatpack can't carry them — keep them map-only.
            const bool flatpackOpen = flatpackSession->IsActive();
            if (ImGui::MenuItem("Spawner", nullptr, false, !flatpackOpen))
            {
                addSpawner();
            }
            if (ImGui::MenuItem("Trigger Volume", nullptr, false, !flatpackOpen))
            {
                addTriggerVolume();
            }
            if (ImGui::MenuItem("Terrain", nullptr, false, !flatpackOpen))
            {
                addTerrain();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project"))
        {
            if (ImGui::MenuItem("Collision Matrix", nullptr, collisionMatrixWindowOpen))
            {
                collisionMatrixWindowOpen = !collisionMatrixWindowOpen;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    void EditorScene::drawCollisionMatrixWindow() const
    {
        if (!collisionMatrixWindowOpen) return;

        bool open = collisionMatrixWindowOpen;
        ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Collision Matrix", &open))
        {
            auto& matrix = sys->collisionSystem->matrix;
            const auto& layers = GetCollisionLayers();
            const int count = static_cast<int>(layers.size());

            ImGui::TextWrapped(
                "Layers only interact where their checkbox is ticked (the matrix is "
                "symmetric). Trigger volumes detect the ticked layers; Default governs "
                "what mouse picking and movement queries hit. Changes save immediately.");
            ImGui::Spacing();

            // Unity-style triangular grid: one row per layer, columns in reverse
            // order, the redundant half omitted.
            constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollX |
                                                   ImGuiTableFlags_ScrollY |
                                                   ImGuiTableFlags_HighlightHoveredColumn;
            const float gridHeight =
                ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f;
            if (ImGui::BeginTable("##collisionMatrix", count + 1, tableFlags, ImVec2(0.0f, gridHeight)))
            {
                ImGui::TableSetupColumn(
                    "", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel);
                for (int c = 0; c < count; ++c)
                {
                    const std::string header{layers[count - 1 - c].layerName};
                    ImGui::TableSetupColumn(
                        header.c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
                }
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableAngledHeadersRow();

                bool changed = false;
                for (int r = 0; r < count; ++r)
                {
                    const auto rowLayer = layers[r];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(std::string{rowLayer.layerName}.c_str());
                    for (int c = 0; c < count - r; ++c)
                    {
                        const auto colLayer = layers[count - 1 - c];
                        if (!ImGui::TableSetColumnIndex(c + 1)) continue;
                        ImGui::PushID(r * MAX_COLLISION_LAYERS + c);
                        bool collides = matrix.GetPair(rowLayer, colLayer);
                        if (ImGui::Checkbox("##cell", &collides))
                        {
                            matrix.SetPair(rowLayer, colLayer, collides);
                            changed = true;
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(
                                "%.*s / %.*s",
                                static_cast<int>(rowLayer.layerName.size()),
                                rowLayer.layerName.data(),
                                static_cast<int>(colLayer.layerName.size()),
                                colLayer.layerName.data());
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
                if (changed) matrix.Save();
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(220.0f);
            const bool submitted = ImGui::InputTextWithHint(
                "##newCollisionLayer",
                "New layer name",
                &newCollisionLayerName,
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Add Layer") || submitted)
            {
                if (const auto layer = matrix.AddUserLayer(newCollisionLayerName); layer.IsValid())
                {
                    newCollisionLayerName.clear();
                    matrix.Save();
                }
                else
                {
                    TraceLog(
                        LOG_WARNING,
                        "Collision Matrix: cannot add layer '%s' (empty, duplicate, or no free bits)",
                        newCollisionLayerName.c_str());
                }
            }
        }
        ImGui::End();
        collisionMatrixWindowOpen = open;
    }

    void EditorScene::drawTerrainBrushWindow() const
    {
        auto* sculpt = editorModes->CurrentTerrainSculptState();
        if (sculpt == nullptr) return;

        ImGui::SetNextWindowSize(ImVec2{280.0f, 0.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Terrain Brush"))
        {
            ImGui::SliderFloat("Radius", &sculpt->brushRadius, 0.5f, 50.0f, "%.1f");
            ImGui::SliderFloat("Strength", &sculpt->brushStrength, 0.5f, 30.0f, "%.1f");
            ImGui::Spacing();
            ImGui::TextUnformatted("LMB raise - Shift+LMB lower");
            ImGui::TextUnformatted("[ / ] adjust radius - Esc to finish");
        }
        ImGui::End();
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

        const auto entity = entityOperations->CreateLight(position);
        adoptIntoFlatpackRoot({entity});
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

        const auto entity = entityOperations->CreateSpawner(position);
        if (history) history->RecordCreate(editor::EditAction::AddSpawner, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::addTerrain() const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = *snappedPosition;
        }

        const auto entity = entityOperations->CreateTerrain(position);
        if (history) history->RecordCreate(editor::EditAction::AddTerrain, {entity});
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

        const auto entity = entityOperations->CreateTriggerVolume(position);
        if (history) history->RecordCreate(editor::EditAction::AddTriggerVolume, {entity});
        editorModes->SelectSceneEntity(entity);
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

    std::vector<entt::entity> EditorScene::collectMapHierarchyOrder() const
    {
        // The map base must exist before its entry is collected, so it is serialised
        // alongside the rest of the scene. Run immediately before writing the map.
        ensureDefaultMapBase();

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
        return hierarchyOrder;
    }

    void EditorScene::refreshAfterMapLoad() const
    {
        ensureDefaultMapBase();
        applyLitShaderToLoadedRenderables();
        // The map only stores terrain height fields; build their meshes.
        for (const auto entity : sys->registry->view<Terrain, sgTransform>())
        {
            AttachTerrainRenderable(*sys->registry, entity, *sys->lightSubSystem);
        }
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
        if (!assetCatalog)
        {
            return {.message = "Asset no longer exists."};
        }

        auto result = editor::RenameAssetFile(*sys->registry, *assetCatalog, index, requestedFileName);
        if (result.renamed)
        {
            if (history) history->MarkDirty();
            refreshOverlay();
            refreshSceneWindows();
        }
        return result;
    }

    void EditorScene::moveHierarchyEntity(const editor::EditorGui::HierarchyMoveRequest& request) const
    {
        const auto dragged = request.dragged;
        auto newParent = request.newParent;
        auto insertBefore = request.insertBefore;

        if (flatpackSession && flatpackSession->IsActive())
        {
            // The session root anchors the open flatpack: it stays at the top,
            // and anything dropped at the top level belongs under it.
            if (dragged == flatpackSession->Root()) return;
            if (newParent == entt::null)
            {
                newParent = flatpackSession->Root();
                insertBefore = entt::null;
            }
        }

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
        : sys(_sys)
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
            [this](std::filesystem::path path) {
                if (flatpackSession) flatpackSession->Open(std::move(path));
            },
            [this](const std::filesystem::path& path, const std::string& requestedName) {
                return renameFlatpackFile(path, requestedName);
            },
            [this](const std::filesystem::path& path) { deleteFlatpackFile(path); },
            [this](const editor::EditorGui::SceneSelectionRequest& request) {
                editorModes->SelectSceneFromHierarchy(request);
            },
            [this](const editor::EditorGui::HierarchyMoveRequest& request) { moveHierarchyEntity(request); },
            modelDefaults->Callbacks());

        scriptBrowser = std::make_unique<ImGui::FileBrowser>(
            ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SkipItemsCausingError |
            ImGuiFileBrowserFlags_EnterNewFilename);
        scriptBrowser->SetTitle("Select script");
        scriptBrowser->SetTypeFilters({".lua"});

        mapController = std::make_unique<editor::EditorMapController>(
            sys,
            _editorSettings,
            std::move(_onEditorSettingsChanged),
            history.get(),
            editor::EditorMapController::Callbacks{
                .prepareForLoad = [this]() { clearCurrentMap(); },
                .finishLoad = [this]() { refreshAfterMapLoad(); },
                .setSceneName = [this](const std::string& name) { SetSceneName(name); },
                .prepareSave = [this]() { return collectMapHierarchyOrder(); }});

        flatpackSession = std::make_unique<editor::EditorFlatpackEditSession>(
            sys,
            history.get(),
            editor::EditorFlatpackEditSession::Callbacks{
                .clearScene = [this]() { clearCurrentMap(); },
                .loadFlatpack =
                    [this](const std::filesystem::path& path) {
                        return PlaceFlatpackAt(path, Vector3{0.0f, 0.0f, 0.0f}).value_or(entt::null);
                    },
                // The flatpack scene needs the same fixups as a freshly loaded
                // map: default base (placement raycast target), shaders, light
                // transforms, placement grid, selection/mode reset.
                .finishOpen = [this]() { refreshAfterMapLoad(); },
                .prepareMapStash = [this]() { return collectMapHierarchyOrder(); },
                .finishMapRestore =
                    [this]() {
                        refreshAfterMapLoad();
                        SetSceneName(mapController->CurrentSceneName());
                    },
                .setSceneName = [this](const std::string& name) { SetSceneName(name); },
                .catalogChanged = [this]() { refreshFlatpackCatalog(); }});

        SetSceneName(UNTITLED_SCENE_NAME);
        mapController->RestoreLastOpenedMap();
        refreshOverlay();
        refreshSceneWindows();
        refreshFlatpackCatalog();
    }

    EditorScene::~EditorScene() = default;
} // namespace sage

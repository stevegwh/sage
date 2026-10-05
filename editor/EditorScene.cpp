#include "EditorScene.hpp"
#include "engine/Colors.hpp"
#include "engine/components/EntityVisibility.hpp"
#include "engine/ui/CanvasSystem.hpp"

#include <iterator>

#include "EditorAssetRename.hpp"
#include "EditorComponents.hpp"
#include "EditorFocus.hpp"
#include "EditorMapLoader.hpp"
#include "engine/Archetypes.hpp"
#include "engine/AudioManager.hpp"
#include "engine/Camera.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/CustomShaderComponent.hpp"
#include "engine/components/DynamicRenderable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/ParticleSystemComponent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/Cursor.hpp"
#include "engine/EditorLayoutMapFormat.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Flatpack.hpp"
#include "engine/IGameRuntime.hpp"
#include "engine/Light.hpp"
#include "engine/LightManager.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/systems/CollisionSystem.hpp"
#include "engine/systems/NavigationGridSystem.hpp"
#include "engine/systems/ParticleEmitterSystem.hpp"
#include "engine/systems/RenderSystem.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/systems/UberShaderSystem.hpp"
#include "engine/TerrainMesh.hpp"
#include "engine/UserInput.hpp"

#include "imfilebrowser.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "rlImGui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sage
{
    namespace
    {
        constexpr float GRID_SURFACE_Y_STEP = 1.0f;
        constexpr std::array<const char*, 3> CAMERA_MODE_NAMES = {"In-game", "Free", "Object Focused"};
        constexpr const char* UNTITLED_SCENE_NAME = "Untitled";
        constexpr const char* SHADERS_DIRECTORY = "resources/shaders";
        // Temp map the editor snapshots the editor scene into when entering
        // play mode; the game runtime loads it, and Stop deletes it.
        constexpr const char* PLAY_SESSION_MAP_PATH = "resources/.play_session.map";
        constexpr const char* DEFAULT_MAP_BASE_NAME = "_MAPBASE_EDITOR_BASE";
        constexpr const char* DEFAULT_MAP_BASE_MODEL_KEY = "primitive_plane";
        constexpr float DEFAULT_MAP_BASE_SIZE = 1000.0f;
        constexpr float DEFAULT_MAP_BASE_HALF_HEIGHT = 0.02f;
        constexpr float DEFAULT_LIGHT_HEIGHT_OFFSET = 6.0f;

        std::string ToPascalCase(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            bool capitalizeNext = true;
            for (const unsigned char character : value)
            {
                if (!std::isalnum(character))
                {
                    capitalizeNext = true;
                    continue;
                }
                result +=
                    capitalizeNext ? static_cast<char>(std::toupper(character)) : static_cast<char>(character);
                capitalizeNext = false;
            }
            return result;
        }
        constexpr float DEFAULT_COMPONENT_LIGHT_BRIGHTNESS = 3.0f;
        constexpr Color DEFAULT_COMPONENT_LIGHT_COLOR = {.r = 255, .g = 244, .b = 214, .a = 255};
        constexpr Color SPAWN_POINT_MARKER_COLOR = {.r = 80, .g = 180, .b = 255, .a = 255};

        bool modelKeyAvailable(const std::string& key)
        {
            const auto keys = ResourceManager::GetInstance().GetModelKeys(true);
            return std::ranges::find(keys, key) != keys.end();
        }

        std::string projectRelativePath(const std::filesystem::path& file)
        {
            std::error_code error;
            const auto relative = std::filesystem::relative(file, std::filesystem::current_path(), error);
            const bool outsideProject = error || relative.empty() || relative.native().starts_with("..");
            return outsideProject ? file.generic_string() : relative.generic_string();
        }

        void openFileWithDefaultApplication(const std::filesystem::path& file)
        {
#if defined(__linux__)
            // Raylib's OpenURL() runs xdg-open synchronously through system().
            // Launch VS Code independently when available and retain xdg-open as
            // the fallback for other desktop associations. Prepare the path
            // before forking; the child must not allocate from a multithreaded
            // GUI process before exec.
            const std::string path = file.string();
            const pid_t child = fork();
            if (child == 0)
            {
                const pid_t launcher = fork();
                if (launcher == 0)
                {
                    setsid();
                    execlp("code", "code", "--reuse-window", path.c_str(), static_cast<char*>(nullptr));
                    execlp("xdg-open", "xdg-open", path.c_str(), static_cast<char*>(nullptr));
                    _exit(127);
                }
                _exit(launcher < 0 ? 127 : 0);
            }

            if (child < 0)
            {
                std::cerr << "EditorScene: could not start the default application for '" << file.string()
                          << "'.\n";
                return;
            }

            int status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                std::cerr << "EditorScene: could not start xdg-open for '" << file.string() << "'.\n";
            }
#else
            OpenURL(file.string().c_str());
#endif
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
            if (!renderable.GetModel()) continue;
            if (sys->registry->any_of<CustomShaderComponent>(entity))
            {
                if (sys->registry->any_of<UberShaderComponent>(entity))
                    sys->registry->remove<UberShaderComponent>(entity);
                continue;
            }
            if (!sys->registry->any_of<UberShaderComponent>(entity))
            {
                auto& uber = sys->registry->emplace<UberShaderComponent>(
                    entity, renderable.GetModel()->get().GetMaterialCount());
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
            sys->uberShaderSystem->RebindRenderable(entity);
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
        gui->SetOverlayStatus(
            editorModes->GetStateName(),
            describeCursorPosition(),
            CAMERA_MODE_NAMES.at(static_cast<int>(editorCamera.mode)));
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
             .flatpackPath = flatpackOpen ? flatpackSession->Path() : std::filesystem::path{},
             .flatpackDirty = flatpackOpen && flatpackSession->HasUnsavedChanges(),
             .particleOpen = particleEditorRoot.has_value(),
             .particleLabel = particleEditorRoot
                                  ? "Particles: " + hierarchyTree->GetEntityName(*particleEditorRoot)
                                  : std::string{},
             .particleDirty = particleEditorRoot.has_value() && history->HasUnsavedChanges(),
             .canvasOpen = canvasEditor && canvasEditor->HasDocument() && !IsPlaying(),
             .canvasLabel = canvasEditor ? canvasEditor->Path().filename().string() : std::string{},
             .canvasDirty = canvasEditor && canvasEditor->IsDirty()});
        gui->SetAssetDefaultsStatus(
            defaultsStatus.assetName, defaultsStatus.height, defaultsStatus.rotation, defaultsStatus.scale);
        gui->SetSelectedAsset(
            isPlaceState() ? std::optional<std::size_t>{assetCatalog->SelectedIndex()} : std::nullopt);
    }

    void EditorScene::refreshSceneWindows() const
    {
        if (gameRuntime)
        {
            refreshRuntimeInspection();
            return;
        }
        gui->SetRuntimeInspection(false);
        const auto selectedRoots = selection->Selected();
        auto inspectedComponents = !selectedRoots.empty()
                                       ? inspectorRegistry.Inspect(*sys->registry, selectedRoots)
                                       : std::vector<editor::InspectedComponent>{};
        std::vector<editor::EditorGui::AddComponentOption> addComponentOptions;
        for (const auto& component : inspectorRegistry.AddableComponents())
        {
            addComponentOptions.push_back(
                {.componentId = component.componentId, .displayName = component.displayName});
        }
        for (auto& option : addComponentOptions)
        {
            const auto state = inspectorRegistry.CanAdd(*sys->registry, option.componentId, selectedRoots);
            option.enabled = state.allowed;
            option.disabledReason = state.blockedReason;
        }

        gui->SetHierarchy(
            hierarchyTree->CollectSceneObjectEntries(),
            selection->SelectedWithChildren(),
            selectedRoots,
            selection->Anchor());
        gui->SetInspector(describeSelectedSceneEntity(), inspectedComponents, std::move(addComponentOptions));
    }

    void EditorScene::focusSelectedObject() const
    {
        const auto target = editor::ComputeFocusTarget(*sys->registry, selection->SelectedWithChildren());
        if (!target) return;

        const auto viewport = gameViewportScreenRect();
        editorCamera.Focus(
            *sys->camera->getRaylibCam(), *target, viewport.width / std::max(1.0f, viewport.height));
        middleCameraDrag = rightCameraDrag = false;
    }

    void EditorScene::focusSelectedObjectInHierarchy() const
    {
        const auto selectedEntity = selection->Active();
        if (!selectedEntity.has_value()) return;
        gui->FocusHierarchyOnEntity(*selectedEntity);
    }

    void EditorScene::setCameraMode(const editor::CameraMode mode) const
    {
        if (mode == editor::CameraMode::Focused)
        {
            focusSelectedObject();
            return;
        }
        editorCamera.mode = mode;
        middleCameraDrag = rightCameraDrag = false;
        // Synchronize the game's height smoothing and discard old scroll momentum.
        const auto* camera = sys->camera->getRaylibCam();
        sys->camera->SetCamera(camera->position, camera->target);
    }

    // Drags start only in the viewport and continue until the button is released.
    void EditorScene::handleMouseCameraControls(const bool canBeginDrag) const
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && canBeginDrag)
        {
            middleCameraDrag = true;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && canBeginDrag)
        {
            rightCameraDrag = true;
        }

        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        {
            middleCameraDrag = false;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            rightCameraDrag = false;
        }

        const Vector2 delta = GetMouseDelta();
        if (editorCamera.mode != editor::CameraMode::Game)
        {
            auto& camera = *sys->camera->getRaylibCam();
            if (rightCameraDrag || (middleCameraDrag && editorCamera.mode == editor::CameraMode::Focused))
                editorCamera.Look(camera, delta);
            else if (middleCameraDrag)
                editorCamera.Pan(camera, delta);
            return;
        }
        if (middleCameraDrag)
        {
            sys->camera->RotateByMouseDelta(delta);
        }
        if (rightCameraDrag)
        {
            sys->camera->PanByMouseDelta(delta);
        }
    }

    void EditorScene::Update() const
    {
        automation.Poll([this](const json::Value& request) { return automationCommand(request); });
        if (canvasEditor->IsActive()) return;
        // While playing, the game runtime drives its own registry; the editor's
        // own systems are idle so the two worlds don't fight over input/state.
        if (gameRuntime)
        {
            // Keep the game's viewport pinned to the (possibly resized/redocked)
            // scene view so its UI stays aligned with the play area.
            gameRuntime->SetViewport(gameViewportScreenRect());
            gameRuntime->Update(viewportFullscreen || (!gui->WantsMouseCapture() && !gui->WantsKeyboardCapture()));
            return;
        }

        mapController->Update();
        flatpackSession->Update();

        // TODO: Fullscreen game viewport (switch state)
        sys->collisionSystem->Update();
        sys->audioManager->Update();
        sys->userInput->ListenForInput();
        const bool uiBlocksScroll = !viewportFullscreen && gui && gui->WantsMouseCapture();
        const bool inViewport = sys->settings->IsPointInRenderViewport(GetMousePosition());
        const bool gizmoDragging = transformEditor && transformEditor->IsGizmoDragging();
        const bool uiBlocksCameraControls = !viewportFullscreen && gui && gui->WantsKeyboardCapture();
        const bool cameraInputBlocked = uiBlocksCameraControls || gizmoDragging;
        if (uiBlocksScroll || !inViewport)
        {
            sys->camera->ScrollDisable();
        }
        else
        {
            sys->camera->ScrollEnable();
        }

        if (cameraInputBlocked)
        {
            sys->camera->LockInput();
        }
        else
        {
            sys->camera->UnlockInput();
        }

        if (!cameraInputBlocked)
            handleMouseCameraControls(!uiBlocksScroll && inViewport);
        else
            middleCameraDrag = rightCameraDrag = false;

        if (editorCamera.mode == editor::CameraMode::Game)
            sys->camera->Update();
        else
        {
            auto& camera = *sys->camera->getRaylibCam();
            if (editorCamera.mode == editor::CameraMode::Focused)
            {
                const auto target = editor::ComputeFocusTarget(*sys->registry, selection->SelectedWithChildren());
                if (target)
                    editorCamera.Follow(camera, *target);
                else
                    setCameraMode(editor::CameraMode::Free);
            }
            if (!cameraInputBlocked && !IsMetaKeyDown() && !IsKeyDown(KEY_LEFT_ALT) && !IsKeyDown(KEY_RIGHT_ALT))
            {
                if (inViewport || rightCameraDrag || middleCameraDrag)
                {
                    if (editorCamera.mode == editor::CameraMode::Focused)
                    {
                        editorCamera.RotateFocused(
                            camera,
                            {.x = static_cast<float>(IsKeyDown(KEY_E) - IsKeyDown(KEY_Q)),
                             .y = static_cast<float>(IsKeyDown(KEY_S) - IsKeyDown(KEY_W))},
                            GetFrameTime());
                    }
                    editorCamera.Move(
                        camera,
                        {.x = static_cast<float>(IsKeyDown(KEY_D) - IsKeyDown(KEY_A)),
                         .y = static_cast<float>(IsKeyDown(KEY_E) - IsKeyDown(KEY_Q)),
                         .z = static_cast<float>(IsKeyDown(KEY_W) - IsKeyDown(KEY_S))},
                        GetFrameTime(),
                        IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
                }
                if (!uiBlocksScroll && inViewport) editorCamera.Zoom(camera, GetMouseWheelMove());
            }
        }
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
        const auto selectedEntities = selection->SelectedWithChildren();
        sys->particleEmitterSystem->Update(GetFrameTime(), std::span<const entt::entity>{selectedEntities});
        syncLightTransforms();
        sys->lightSubSystem->Update();
        sys->lightSubSystem->RefreshLights();
        refreshOverlay();
        refreshSceneWindows();
    }

    void EditorScene::Draw3D() const
    {
        if (gameRuntime)
        {
            gameRuntime->Draw3D();
            auto& registry = gameRuntime->InspectionRegistry();
            if (runtimeSelection && registry.valid(*runtimeSelection))
            {
                if (const auto* collider = registry.try_get<Collideable>(*runtimeSelection))
                    DrawBoundingBox(collider->worldBoundingBox, sage::colors::ORANGE_COLOR);
                if (const auto* actor = registry.try_get<MoveableActor>(*runtimeSelection);
                    actor && registry.any_of<sgTransform>(*runtimeSelection))
                {
                    auto previous = registry.get<sgTransform>(*runtimeSelection).GetWorldPos();
                    for (const auto& point : actor->path)
                    {
                        DrawLine3D(previous, point, sage::colors::ORANGE_COLOR);
                        previous = point;
                    }
                }
            }
            return;
        }

        sys->renderSystem->Draw();
        sys->particleEmitterSystem->Draw(*sys->camera->getRaylibCam());
        sys->lightSubSystem->DrawDebugLights();
        placementController->DrawGridAndAxes();
        if (navigationGridVisible)
        {
            sys->navigationGridSystem->DrawDebugGrid();
        }
        editorModes->Draw3D();

        // Marker entities have no mesh, so draw a stand-in sphere for tagged spawn points.
        for (const auto entity : sys->registry->view<sgTransform, MetaData>())
        {
            if (!IsEntityVisible(*sys->registry, entity) ||
                !HasTag(sys->registry->get<MetaData>(entity), editor::SPAWN_POINT_TAG))
                continue;
            const auto& transform = sys->registry->get<sgTransform>(entity);
            const auto position = transform.GetWorldPos();
            const auto color = SPAWN_POINT_MARKER_COLOR;
            DrawSphereEx(position, 0.5f, 8, 8, color);
            // Facing line: a stick from the sphere out along the marker's forward.
            DrawLine3D(position, Vector3Add(position, Vector3Scale(transform.forward(), 1.5f)), color);
        }
        // Trigger volumes are Collideables with TriggerVolume intent; draw their world box so the
        // otherwise-invisible region is visible and editable.
        for (const auto entity : sys->registry->view<Collideable, TriggerVolume>())
        {
            if (!IsEntityVisible(*sys->registry, entity)) continue;
            const auto& collideable = sys->registry->get<Collideable>(entity);
            DrawBoundingBox(collideable.worldBoundingBox, sage::colors::GREEN_COLOR);
        }

        for (const auto entity : selection->SelectedWithChildren())
        {
            if (!IsEntityVisible(*sys->registry, entity)) continue;
            // The brush ring marks the active terrain; dense selection wires obscure painted textures.
            if (const auto brush = editorModes->CurrentTerrainSculptState();
                brush && brush->get().terrain == entity && brush->get().brushMode == TerrainBrushMode::Texture)
                continue;
            if (!sys->registry->valid(entity) || !sys->registry->any_of<Collideable>(entity)) continue;
            const auto& collideable = sys->registry->get<Collideable>(entity);

            // A mesh collider is the render mesh itself, so visualize it by
            // re-drawing the same GPU buffers in wireframe (selection-only, so
            // cheap); its box is only the broad-phase, shown de-emphasised.
            if (collideable.shape == ColliderShape::RenderMesh && sys->registry->any_of<sgTransform>(entity))
            {
                const auto& transform = sys->registry->get<sgTransform>(entity);
                rlEnableWireMode();
                if (sys->registry->any_of<Renderable>(entity))
                {
                    if (auto model = sys->registry->get<Renderable>(entity).GetModel(); model.has_value())
                    {
                        model->get().Draw(
                            transform.GetWorldPos(),
                            transform.GetWorldRot(),
                            transform.GetScale(),
                            sage::colors::GREEN_COLOR);
                    }
                }
                else if (sys->registry->any_of<DynamicRenderable>(entity))
                {
                    const auto& renderable = sys->registry->get<DynamicRenderable>(entity);
                    if (renderable.GetModel().has_value())
                    {
                        renderable.Draw(
                            transform.GetWorldPos(),
                            {.x = 0.0f, .y = 1.0f, .z = 0.0f},
                            transform.GetWorldRot().y,
                            transform.GetScale(),
                            sage::colors::GREEN_COLOR);
                    }
                }
                rlDisableWireMode();
                DrawBoundingBox(collideable.worldBoundingBox, Fade(sage::colors::ORANGE_COLOR, 0.35f));
            }
            else
            {
                DrawBoundingBox(collideable.worldBoundingBox, sage::colors::ORANGE_COLOR);
            }
        }
    }

    void EditorScene::DrawBloomMask() const
    {
        if (gameRuntime)
            gameRuntime->DrawBloomMask();
        else
            sys->renderSystem->DrawBloomMask();
    }

    void EditorScene::DrawShadowMap() const
    {
        if (gameRuntime)
            gameRuntime->DrawShadowMap();
        else
            sys->lightSubSystem->DrawShadowMap(*sys->renderSystem);
    }

    void EditorScene::setSnapToGrid(const bool enabled) const
    {
        snapToGrid = enabled;
        if (placementController) placementController->SetSnapToGrid(enabled);
        if (transformEditor) transformEditor->SetSnapToGrid(enabled);
        refreshOverlay();
    }

    void EditorScene::rebuildNavigationGrid() const
    {
        // Initialize clears stale height/occupancy data, then samples source
        // navigation surfaces and stamps active obstacles exactly as play mode does.
        placementController->Initialize();
        sys->navigationGridSystem->InitGridHeightAndNormals();
    }

    void EditorScene::DrawOverlay2D() const
    {
        // During play the game's 2D UI is composited separately (DrawGame2D into
        // a viewport-sized texture), so skip the editor's scene-view overlay.
        if (gameRuntime) return;
        if (viewportFullscreen) return;
        gui->DrawSceneViewInfo();
    }

    void EditorScene::DrawGame2D() const
    {
        if (gameRuntime) gameRuntime->Draw2D();
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
        if (canvasEditor->IsActive())
        {
            canvasEditor->Draw();
            gui->DrawAssetUsages();
            if (!canvasEditor->IsActive()) gui->RefreshResourceBrowser();
            drawExitConfirmationModal(exitRequested, exitConfirmed);
            gui->EndImGui();
            return;
        }
        gui->SetCanvasEditCallback([this](const std::filesystem::path& path) {
            if (path.empty())
                canvasEditor->New();
            else
                canvasEditor->Open(path);
        });
        drawMainMenuBar(exitRequested);

        if (gameRuntime)
        {
            refreshRuntimeInspection();
            gui->DrawHierarchyWindow();
            static_cast<void>(gui->DrawInspectorWindow({}, [this]() { frameRuntimeObject(); }));
            gui->DrawConsoleWindow();
            drawGraphicsSettingsWindow();
            drawExitConfirmationModal(exitRequested, exitConfirmed);
            gui->EndImGui();
            return;
        }

        // Keep the selection's clean state cached so an inspector edit
        // that mutates on its activation frame (checkbox/combo) still has a correct
        // "before" to undo to. See EditorHistory::CaptureBaseline.
        if (history && !history->HasActiveTransaction() && !ImGui::IsAnyItemActive())
        {
            history->CaptureBaseline(selection->Selected());
        }
        std::function<void()> terrainTools;
        if (editorModes->CurrentTerrainSculptState()) terrainTools = [this]() { drawTerrainBrushTools(); };
        const auto inspectorEdit = gui->DrawInspectorWindow(terrainTools, {}, [this]() {
            particleEditor.DrawInspectorModules(*sys->registry, selection->Active(), *history);
        });
        handleInspectorEdit(inspectorEdit);
        drawParticleEditorTab();
        if (!IsPlaying() && !flatpackSession->IsActive())
            canvasEditor->DrawSceneUI(*sys->registry, [this]() { history->MarkDirty(); });

        gui->DrawHierarchyWindow();
        if (gameRuntime)
            gui->DrawConsoleWindow();
        else
            gui->DrawAssetDrawerWindow();
        gui->DrawAssetUsages();
        const auto sceneTabAction = gui->DrawSceneTabBar();
        if (sceneTabAction.canvasSelected) canvasEditor->Resume();
        if (sceneTabAction.canvasCloseRequested) canvasEditor->RequestClose();
        if (sceneTabAction.particleCloseRequested || sceneTabAction.flatpackSelected ||
            sceneTabAction.mapSelected || sceneTabAction.flatpackCloseRequested)
            closeParticleEditor();
        if (sceneTabAction.mapSelected || sceneTabAction.flatpackCloseRequested)
        {
            flatpackSession->RequestClose();
        }
        flatpackSession->DrawCloseConfirmationModal();
        handleFileShortcuts();
        mapController->DrawBrowsers();
        drawScriptBrowser();
        drawShaderBrowser();
        drawCollisionMatrixWindow();
        drawGraphicsSettingsWindow();
        handleClipboardShortcuts();
        handleHistoryShortcuts();
        drawHierarchyContextMenu();
        gui->DrawDeleteConfirmationModal();
        drawExitConfirmationModal(exitRequested, exitConfirmed);
        gui->EndImGui();
    }

    void EditorScene::drawExitConfirmationModal(bool& exitRequested, bool& exitConfirmed) const
    {
        constexpr const char* popupId = "Unsaved Changes";

        // While a flatpack is open the live history belongs to it, and the map's
        // own dirty flag is parked in the session stash — check both.
        const bool hasUnsavedChanges = mapController->HasUnsavedChanges() ||
                                       flatpackSession->HasUnsavedChanges() ||
                                       flatpackSession->StashedMapHadUnsavedChanges() || canvasEditor->IsDirty();
        if (exitRequested && !hasUnsavedChanges)
        {
            exitRequested = false;
            exitConfirmed = true;
            return;
        }

        if (newMapRequested && !mapController->HasUnsavedChanges())
        {
            newMapRequested = false;
            mapController->NewMap();
            return;
        }

        if ((exitRequested || newMapRequested) && !ImGui::IsPopupOpen(popupId))
        {
            ImGui::OpenPopup(popupId);
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
        ImGui::SetNextWindowSize(ImVec2{440.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            const bool creatingNewMap = newMapRequested && !exitRequested;
            ImGui::TextWrapped(
                creatingNewMap ? "You have unsaved changes. Create a new map without saving? [Y/N]"
                               : "You have unsaved changes. Exit without saving? [Y/N]");
            ImGui::Spacing();
            constexpr float confirmButtonWidth = 210.0f;
            constexpr float cancelButtonWidth = 120.0f;
            const float buttonsWidth = confirmButtonWidth + cancelButtonWidth + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonsWidth) * 0.5f);
            const char* confirmLabel = creatingNewMap ? "New Map Without Saving (Y)" : "Exit Without Saving (Y)";
            const bool confirm =
                ImGui::Button(confirmLabel, ImVec2{confirmButtonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Y);
            ImGui::SameLine();
            const bool cancel =
                ImGui::Button("Cancel (N)", ImVec2{cancelButtonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_N);
            if (confirm)
            {
                if (creatingNewMap)
                {
                    newMapRequested = false;
                    mapController->NewMap();
                }
                else
                {
                    exitRequested = false;
                    exitConfirmed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancel)
            {
                exitRequested = false;
                newMapRequested = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorScene::drawHierarchyContextMenu() const
    {
        constexpr const char* popupId = "hierarchy_context_menu";

        if (const auto entity = gui->ConsumeHierarchyContextEntity(); entity.has_value())
        {
            hierarchyContextEntity = *entity;
            ImGui::OpenPopup(popupId);
        }

        std::optional<entt::entity> openParticleRequest;
        if (ImGui::BeginPopup(popupId))
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
                const auto& registry = *sys->registry;
                const bool locallyVisible = !registry.all_of<EntityVisibility>(hierarchyContextEntity) ||
                                            registry.get<EntityVisibility>(hierarchyContextEntity).visible;
                if (locallyVisible && !IsEntityVisible(registry, hierarchyContextEntity))
                    ImGui::TextDisabled("Hidden by parent");
                if (ImGui::MenuItem(locallyVisible ? "Hide" : "Show"))
                {
                    history->Begin(editor::EditAction::EditField, {hierarchyContextEntity});
                    sys->registry->get_or_emplace<EntityVisibility>(hierarchyContextEntity).visible =
                        !locallyVisible;
                    history->Commit();
                    sys->lightSubSystem->RefreshLights();
                }
                ImGui::Separator();
                const auto particleRoot = ParticleEffectRoot(*sys->registry, hierarchyContextEntity);
                if (!GatherParticleEmitters(*sys->registry, particleRoot).empty() &&
                    ImGui::MenuItem("Open Particle Editor"))
                    openParticleRequest = particleRoot;
                if (ImGui::MenuItem("Create Flatpack"))
                {
                    createFlatpackFromEntity(hierarchyContextEntity);
                }
            }
            ImGui::EndPopup();
        }
        if (openParticleRequest) openParticleEditor(*openParticleRequest);
    }

    void EditorScene::handleFileShortcuts() const
    {
        // Save must fire no matter what currently owns the keyboard. Plain RouteGlobal
        // yields to a focused window or an active item (e.g. a search/inspector field),
        // so OverFocused|OverActive force Ctrl/Cmd+S to win in every context.
        constexpr ImGuiInputFlags saveFlags =
            ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused | ImGuiInputFlags_RouteOverActive;
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
    // edit leaves a selected Collideable using mesh collision, refit the box from the meshes
    // so rays keep reaching them. Idempotent, and it runs inside the active edit
    // transaction so the refit undoes together with the field change.
    void EditorScene::refitMeshColliderBounds() const
    {
        for (const auto entity : selection->Selected())
        {
            if (!sys->registry->valid(entity) || !sys->registry->all_of<Collideable>(entity)) continue;
            if (sys->registry->get<Collideable>(entity).shape != ColliderShape::RenderMesh) continue;

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

        if (result.selectScriptFile) openScriptBrowser();
        if (result.openScriptFile) openSelectedScript();

        if (result.selectShaderFile.has_value() && shaderBrowser)
        {
            pendingShaderFileSlot = result.selectShaderFile;
            shaderBrowser->SetDirectory(SHADERS_DIRECTORY);
            shaderBrowser->SetTypeFilters(
                *pendingShaderFileSlot == editor::ShaderFileSlot::Vertex
                    ? std::vector<std::string>{".vs", ".glsl"}
                    : std::vector<std::string>{".fs", ".glsl"});
            shaderBrowser->Open();
        }

        if (!history->HasActiveTransaction())
        {
            const auto addRegisteredComponent = [&]<class Component>() {
                const auto selected = selection->Selected();
                if (selected.empty()) return;

                const auto availability =
                    inspectorRegistry.CanAdd(*sys->registry, editor::ComponentIdOf<Component>(), selected);
                if (!availability.allowed)
                {
                    if (!availability.blockedReason.empty())
                    {
                        std::cout << "EditorScene: cannot add component: " << availability.blockedReason << '\n';
                    }
                    return;
                }

                history->Begin(editor::EditAction::AddComponent, selected);
                for (const auto entity : selected)
                {
                    if (!sys->registry->valid(entity) || sys->registry->any_of<Component>(entity)) continue;
                    if constexpr (std::is_same_v<Component, Renderable>)
                    {
                        auto& renderable = sys->registry->emplace<Renderable>(entity);
                        renderable.initialTransform = MatrixIdentity();
                    }
                    else if constexpr (std::is_same_v<Component, Collideable>)
                    {
                        constexpr BoundingBox localBounds{
                            .min = {.x = -1.0f, .y = -1.0f, .z = -1.0f}, .max = {.x = 1.0f, .y = 1.0f, .z = 1.0f}};
                        const auto* transform = sys->registry->try_get<sgTransform>(entity);
                        sys->registry->emplace<Collideable>(
                            entity,
                            localBounds,
                            transform != nullptr ? transform->GetMatrixNoRot() : MatrixIdentity());
                    }
                    else if constexpr (std::is_same_v<Component, Light>)
                    {
                        const auto* transform = sys->registry->try_get<sgTransform>(entity);
                        const Vector3 position = transform != nullptr ? transform->GetWorldPos() : Vector3Zero();
                        sys->registry->emplace<Light>(
                            entity,
                            Light{
                                .type = LightType::Point,
                                .enabled = true,
                                .position = position,
                                .target = Vector3Zero(),
                                .color = DEFAULT_COMPONENT_LIGHT_COLOR,
                                .brightness = DEFAULT_COMPONENT_LIGHT_BRIGHTNESS});
                    }
                    else if constexpr (std::is_same_v<Component, CustomShaderComponent>)
                    {
                        static_cast<void>(sys->registry->get<Renderable>(entity).EnsureMutable());
                        if (sys->registry->any_of<UberShaderComponent>(entity))
                            sys->registry->remove<UberShaderComponent>(entity);
                        sys->registry->emplace<CustomShaderComponent>(entity);
                    }
                    else
                    {
                        auto& component = sys->registry->emplace<Component>(entity);
                        if constexpr (std::is_same_v<Component, NavigationObstacle>)
                        {
                            if (const auto* collideable = sys->registry->try_get<Collideable>(entity);
                                collideable != nullptr && component.active)
                            {
                                sys->navigationGridSystem->MarkSquareAreaOccupied(
                                    collideable->worldBoundingBox, true, entity);
                            }
                        }
                    }
                }
                if constexpr (std::is_same_v<Component, Light>)
                {
                    if (sys->lightSubSystem) sys->lightSubSystem->RefreshLights();
                }
                history->Commit();
                refreshSceneWindows();
            };
            if (const auto requested = result.addComponent)
            {
                const auto requestedComponent = *requested;
                if (requestedComponent == editor::ComponentIdOf<ScriptComponent>())
                {
                    if (!scriptBrowser)
                    {
                        addRegisteredComponent.template operator()<ScriptComponent>();
                    }
                    else
                    {
                        const auto selected = selection->Selected();
                        const auto availability = inspectorRegistry.CanAdd(
                            *sys->registry, editor::ComponentIdOf<ScriptComponent>(), selected);
                        if (availability.allowed)
                            openScriptBrowser();
                        else if (!availability.blockedReason.empty())
                            std::cout << "EditorScene: cannot add component: " << availability.blockedReason
                                      << '\n';
                    }
                }
                else if (requestedComponent == editor::ComponentIdOf<Animation>())
                    addAnimationToSelection();
                else if (requestedComponent == editor::ComponentIdOf<MoveableActor>())
                    addMoveableActorToSelection();
                else if (requestedComponent == editor::ComponentIdOf<Renderable>())
                    addRegisteredComponent.template operator()<Renderable>();
                else if (requestedComponent == editor::ComponentIdOf<Collideable>())
                    addRegisteredComponent.template operator()<Collideable>();
                else if (requestedComponent == editor::ComponentIdOf<Light>())
                    addRegisteredComponent.template operator()<Light>();
                else if (requestedComponent == editor::ComponentIdOf<CustomShaderComponent>())
                    addRegisteredComponent.template operator()<CustomShaderComponent>();
                else if (requestedComponent == editor::ComponentIdOf<NavigationSurface>())
                    addRegisteredComponent.template operator()<NavigationSurface>();
                else if (requestedComponent == editor::ComponentIdOf<NavigationObstacle>())
                    addRegisteredComponent.template operator()<NavigationObstacle>();
                else if (requestedComponent == editor::ComponentIdOf<TriggerVolume>())
                    addRegisteredComponent.template operator()<TriggerVolume>();
                else if (requestedComponent == editor::ComponentIdOf<CursorTarget>())
                    addRegisteredComponent.template operator()<CursorTarget>();
                else if (requestedComponent == editor::ComponentIdOf<Archetype>())
                    addRegisteredComponent.template operator()<Archetype>();
                else
                {
                    const auto selected = selection->Selected();
                    history->Begin(editor::EditAction::AddComponent, selected);
                    for (const auto entity : selected)
                    {
                        if (sys->registry->valid(entity))
                            inspectorRegistry.Add(*sys->registry, requestedComponent, entity);
                    }
                    history->Commit();
                    refreshSceneWindows();
                }
            }

            if (result.selectedModelKey.has_value()) changeSelectedModels(*result.selectedModelKey);
            if (result.selectedMaterial.has_value())
            {
                changeSelectedMaterials(
                    result.selectedMaterial->materialIndex, result.selectedMaterial->materialKey);
            }

            if (result.editModelDefaultsClicked)
            {
                const auto active = selection->Active();
                if (active.has_value() && sys->registry->valid(*active))
                {
                    if (const auto* renderable = sys->registry->try_get<Renderable>(*active);
                        renderable != nullptr && renderable->GetModel().has_value())
                    {
                        if (const auto index =
                                assetCatalog->FindByModelKey(renderable->GetModel()->get().GetKey()))
                        {
                            editorModes->SelectPlaceable(*index);
                        }
                    }
                }
            }

            const auto removeSelectedComponent = [&](const editor::EditorComponentId componentId) {
                const auto selected = selection->Selected();
                if (selected.empty()) return;

                const auto availability = inspectorRegistry.CanRemove(*sys->registry, componentId, selected);
                if (!availability.allowed)
                {
                    if (!availability.blockedReason.empty())
                    {
                        std::cout << "EditorScene: cannot remove component: " << availability.blockedReason
                                  << '\n';
                    }
                    return;
                }

                if (componentId == editor::ComponentIdOf<Animation>())
                {
                    removeAnimationFromSelection();
                    return;
                }
                if (componentId == editor::ComponentIdOf<MoveableActor>())
                {
                    removeMoveableActorFromSelection();
                    return;
                }

                const auto removeRegisteredComponent = [&]<class Component>() {
                    history->Begin(editor::EditAction::RemoveComponent, selected);
                    for (const auto entity : selected)
                    {
                        if (!sys->registry->valid(entity) || !sys->registry->any_of<Component>(entity)) continue;

                        if constexpr (
                            std::is_same_v<Component, Collideable> ||
                            std::is_same_v<Component, NavigationObstacle>)
                        {
                            if (const auto* collideable = sys->registry->try_get<Collideable>(entity);
                                collideable != nullptr)
                            {
                                if (const auto* obstacle = sys->registry->try_get<NavigationObstacle>(entity);
                                    obstacle != nullptr && obstacle->active)
                                {
                                    sys->navigationGridSystem->MarkSquareAreaOccupied(
                                        collideable->worldBoundingBox, false, entity);
                                }
                            }
                        }

                        if constexpr (std::is_same_v<Component, Renderable>)
                        {
                            if (sys->registry->any_of<UberShaderComponent>(entity))
                            {
                                sys->registry->remove<UberShaderComponent>(entity);
                            }
                        }

                        if constexpr (std::is_same_v<Component, CustomShaderComponent>)
                        {
                            sys->registry->remove<CustomShaderComponent>(entity);
                            if (const auto* renderable = sys->registry->try_get<Renderable>(entity);
                                renderable != nullptr && renderable->GetModel().has_value())
                            {
                                auto& uber = sys->registry->emplace<UberShaderComponent>(
                                    entity, renderable->GetModel()->get().GetMaterialCount());
                                uber.SetFlagAll(UberShaderComponent::Flags::Lit);
                            }
                            continue;
                        }

                        sys->registry->remove<Component>(entity);
                    }

                    if constexpr (std::is_same_v<Component, Light>)
                    {
                        if (sys->lightSubSystem) sys->lightSubSystem->RefreshLights();
                    }
                    history->Commit();
                    refreshSceneWindows();
                };

                if (componentId == editor::ComponentIdOf<editor::AssetReference>())
                {
                    removeRegisteredComponent.template operator()<editor::AssetReference>();
                }
                else if (componentId == editor::ComponentIdOf<Renderable>())
                {
                    removeRegisteredComponent.template operator()<Renderable>();
                }
                else if (componentId == editor::ComponentIdOf<Collideable>())
                {
                    removeRegisteredComponent.template operator()<Collideable>();
                }
                else if (componentId == editor::ComponentIdOf<NavigationSurface>())
                {
                    removeRegisteredComponent.template operator()<NavigationSurface>();
                }
                else if (componentId == editor::ComponentIdOf<NavigationObstacle>())
                {
                    removeRegisteredComponent.template operator()<NavigationObstacle>();
                }
                else if (componentId == editor::ComponentIdOf<TriggerVolume>())
                {
                    removeRegisteredComponent.template operator()<TriggerVolume>();
                }
                else if (componentId == editor::ComponentIdOf<CursorTarget>())
                {
                    removeRegisteredComponent.template operator()<CursorTarget>();
                }
                else if (componentId == editor::ComponentIdOf<Light>())
                {
                    removeRegisteredComponent.template operator()<Light>();
                }
                else if (componentId == editor::ComponentIdOf<CustomShaderComponent>())
                {
                    removeRegisteredComponent.template operator()<CustomShaderComponent>();
                }
                else
                {
                    history->Begin(editor::EditAction::RemoveComponent, selected);
                    for (const auto entity : selected)
                    {
                        if (sys->registry->valid(entity))
                            inspectorRegistry.Remove(*sys->registry, componentId, entity);
                    }
                    history->Commit();
                    refreshSceneWindows();
                }
            };

            if (result.removeComponent.has_value()) removeSelectedComponent(*result.removeComponent);
        }
    }

    void EditorScene::drawScriptBrowser() const
    {
        if (!scriptBrowser) return;
        scriptBrowser->Display();
        if (!scriptBrowser->HasSelected()) return;
        auto sourceFile = scriptBrowser->GetSelected();
        if (sourceFile.extension().empty()) sourceFile += ".cs";
        std::error_code ec;
        if (!std::filesystem::exists(sourceFile, ec) && !ec)
        {
            const auto typeName = ToPascalCase(sourceFile.stem().string());
            sourceFile.replace_filename(typeName + ".cs");
        }
        if (createScriptSource(sourceFile)) attachScriptToSelection(sourceFile);
        scriptBrowser->ClearSelected();
    }

    void EditorScene::drawShaderBrowser() const
    {
        if (!shaderBrowser) return;
        shaderBrowser->Display();
        if (shaderBrowser->HasSelected() && pendingShaderFileSlot.has_value())
        {
            setShaderFileOnSelection(*pendingShaderFileSlot, shaderBrowser->GetSelected());
            shaderBrowser->ClearSelected();
            pendingShaderFileSlot.reset();
        }
    }

    void EditorScene::openScriptBrowser() const
    {
        if (!scriptBrowser) return;
        scriptBrowser->SetDirectory(csharpScripts.sourceDirectory);
        scriptBrowser->Open();
    }

    std::optional<std::string> EditorScene::managedClassForSource(const std::filesystem::path& sourceFile) const
    {
        if (!csharpScripts.IsConfigured() || sourceFile.extension() != ".cs") return std::nullopt;

        std::error_code ec;
        const auto root = std::filesystem::weakly_canonical(csharpScripts.sourceDirectory, ec);
        if (ec) return std::nullopt;
        const auto source = std::filesystem::weakly_canonical(sourceFile, ec);
        if (ec) return std::nullopt;
        auto relative = std::filesystem::relative(source, root, ec);
        if (ec || relative.empty() || *relative.begin() == "..") return std::nullopt;

        relative.replace_extension();
        std::string className = csharpScripts.rootNamespace;
        for (const auto& segment : relative)
        {
            className += ".";
            className += segment.string();
        }
        return className;
    }

    std::filesystem::path EditorScene::sourceForManagedClass(const std::string& className) const
    {
        if (!csharpScripts.IsConfigured()) return {};
        const std::string prefix = csharpScripts.rootNamespace + ".";
        if (!className.starts_with(prefix)) return {};

        std::string relative = className.substr(prefix.size());
        std::ranges::replace(relative, '.', std::filesystem::path::preferred_separator);
        auto path = csharpScripts.sourceDirectory / relative;
        path += ".cs";
        return path;
    }

    bool EditorScene::createScriptSource(const std::filesystem::path& sourceFile) const
    {
        std::error_code ec;
        if (std::filesystem::exists(sourceFile, ec))
            return !ec && std::filesystem::is_regular_file(sourceFile, ec);
        if (ec || sourceFile.extension() != ".cs") return false;

        const auto className = managedClassForSource(sourceFile);
        if (!className) return false;

        const std::string typeName = sourceFile.stem().string();
        const auto isIdentifierStart = [](const unsigned char value) {
            return std::isalpha(value) || value == '_';
        };
        const auto isIdentifierPart = [](const unsigned char value) {
            return std::isalnum(value) || value == '_';
        };
        if (typeName.empty() || !isIdentifierStart(typeName.front()) ||
            !std::ranges::all_of(typeName.substr(1), isIdentifierPart))
        {
            std::cout << "EditorScene: C# script name must be a valid identifier: '" << typeName << "'.\n";
            return false;
        }

        const auto separator = className->rfind('.');
        if (separator == std::string::npos) return false;
        const std::string scriptNamespace = className->substr(0, separator);

        std::ofstream output(sourceFile, std::ios::out | std::ios::trunc);
        if (!output)
        {
            std::cout << "EditorScene: could not create C# script '" << sourceFile.string() << "'.\n";
            return false;
        }
        output << "using Sage;\n\n"
               << "namespace " << scriptNamespace << ";\n\n"
               << "public sealed class " << typeName << " : Script\n"
               << "{\n"
               << "    protected override void Start()\n"
               << "    {\n"
               << "    }\n\n"
               << "    protected override void Update(float deltaTime)\n"
               << "    {\n"
               << "    }\n"
               << "}\n";
        if (output) return true;

        output.close();
        std::filesystem::remove(sourceFile, ec);
        std::cout << "EditorScene: could not write C# script '" << sourceFile.string() << "'.\n";
        return false;
    }

    void EditorScene::attachScriptToSelection(const std::filesystem::path& sourceFile) const
    {
        const auto className = managedClassForSource(sourceFile);
        const auto selected = selection->Selected();
        if (!className || selected.empty())
        {
            std::cout << "EditorScene: could not derive a managed class from '" << sourceFile.string() << "'.\n";
            return;
        }

        const bool adding = std::ranges::any_of(selected, [this](const entt::entity entity) {
            return sys->registry->valid(entity) && !sys->registry->any_of<ScriptComponent>(entity);
        });
        history->Begin(adding ? editor::EditAction::AddScript : editor::EditAction::EditField, selected);
        for (const auto entity : selected)
        {
            if (!sys->registry->valid(entity)) continue;
            if (auto* script = sys->registry->try_get<ScriptComponent>(entity))
                script->className = *className;
            else
                sys->registry->emplace<ScriptComponent>(entity, *className, true);
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::openSelectedScript() const
    {
        for (const auto entity : selection->Selected())
        {
            if (!sys->registry->valid(entity)) continue;
            const auto* script = sys->registry->try_get<ScriptComponent>(entity);
            if (script == nullptr) continue;

            const auto source = sourceForManagedClass(script->className);
            std::error_code ec;
            if (!source.empty() && std::filesystem::is_regular_file(source, ec))
            {
                openFileWithDefaultApplication(source);
                return;
            }
            std::cout << "EditorScene: no C# source file found for '" << script->className << "'.\n";
            return;
        }
    }

    void EditorScene::setShaderFileOnSelection(
        const editor::ShaderFileSlot slot, const std::filesystem::path& shaderFile) const
    {
        const auto selected = selection->Selected();
        if (selected.empty()) return;

        const auto path = projectRelativePath(shaderFile);
        history->Begin(editor::EditAction::EditField, selected);
        for (const auto entity : selected)
        {
            auto* shader = sys->registry->try_get<CustomShaderComponent>(entity);
            if (shader == nullptr) continue;
            if (slot == editor::ShaderFileSlot::Vertex)
                shader->vertexShaderPath = path;
            else
                shader->fragmentShaderPath = path;
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
            if (renderable == nullptr || !renderable->GetModel()) continue;
            if (!resources.HasModelAnimation(renderable->GetModel()->get().GetKey())) continue;
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
            const auto key = renderable.GetModel()->get().GetKey();

            // Skinned animation writes the animated pose into the mesh data each
            // frame, so the shared ModelView must become this entity's own copy.
            if (!renderable.GetMutable())
            {
                auto mutableModel = resources.CreateModelMutable(key);
                mutableModel.SetTransform(renderable.initialTransform);
                renderable.SetModel(std::move(mutableModel));
            }
            reg.emplace<Animation>(entity, key);
            if (auto* uber = reg.try_get<UberShaderComponent>(entity))
            {
                uber->SetFlagAll(UberShaderComponent::Flags::Skinned);
                sys->uberShaderSystem->RebindRenderable(entity);
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

            // Return to the shared view only when the private copy existed solely
            // for skinning. Material overrides also require private storage.
            if (auto* renderable = reg.try_get<Renderable>(entity);
                renderable != nullptr && renderable->GetMutable().has_value())
            {
                const auto& defaults = resources.GetModelMaterialKeys(renderable->GetModel()->get().GetKey());
                if (renderable->GetMaterialKeys() == defaults)
                {
                    auto view = resources.GetModelView(renderable->GetModel()->get().GetKey());
                    view.SetTransform(renderable->initialTransform);
                    renderable->SetModel(std::move(view));
                }
            }
            if (auto* uber = reg.try_get<UberShaderComponent>(entity))
            {
                uber->ClearFlagAll(UberShaderComponent::Flags::Skinned);
                sys->uberShaderSystem->RebindRenderable(entity);
            }
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::changeSelectedModels(const std::string& modelKey) const
    {
        const auto catalogIndex = assetCatalog->FindByModelKey(modelKey);
        if (!catalogIndex.has_value()) return;

        auto& registry = *sys->registry;
        auto& resources = ResourceManager::GetInstance();
        std::vector<entt::entity> targets;
        for (const auto entity : selection->Selected())
        {
            if (!registry.valid(entity) || !registry.all_of<Renderable>(entity)) continue;
            if (registry.any_of<Animation>(entity) && !resources.HasModelAnimation(modelKey))
            {
                std::cout << "EditorScene: animated renderables require a model with animation data.\n";
                return;
            }
            targets.push_back(entity);
        }
        if (targets.empty()) return;

        const Matrix defaultTransform = assetCatalog->DefaultTransform(assetCatalog->At(*catalogIndex));
        history->Begin(editor::EditAction::EditField, targets);
        for (const auto entity : targets)
        {
            auto& renderable = registry.get<Renderable>(entity);
            if (renderable.GetModel().has_value() && renderable.GetModel()->get().GetKey() == modelKey) continue;

            const auto* oldAnimation = registry.try_get<Animation>(entity);
            const bool animated = oldAnimation != nullptr;
            const std::string oldClip = animated ? oldAnimation->GetClipName(oldAnimation->current.index) : "";
            const int oldSpeed = animated ? oldAnimation->current.speed : 1;
            const float blendDuration = animated ? oldAnimation->blendDuration : 0.2f;

            const bool hadUberShader = registry.any_of<UberShaderComponent>(entity);
            bool wasLit = false;
            if (const auto* uber = registry.try_get<UberShaderComponent>(entity))
            {
                wasLit = std::ranges::any_of(uber->materialMap, [](const auto flags) {
                    return (flags & static_cast<std::uint32_t>(UberShaderComponent::Flags::Lit)) != 0;
                });
                registry.remove<UberShaderComponent>(entity);
            }
            if (animated) registry.remove<Animation>(entity);

            renderable.initialTransform = defaultTransform;
            if (animated)
            {
                auto model = resources.CreateModelMutable(modelKey);
                model.SetTransform(defaultTransform);
                renderable.SetModel(std::move(model));

                auto& animation = registry.emplace<Animation>(entity, modelKey);
                animation.blendDuration = blendDuration;
                animation.current.speed = oldSpeed;
                if (!oldClip.empty()) static_cast<void>(animation.ChangeAnimationByName(oldClip, oldSpeed));
            }
            else
            {
                auto model = resources.GetModelView(modelKey);
                model.SetTransform(defaultTransform);
                renderable.SetModel(std::move(model));
            }

            if (auto* reference = registry.try_get<editor::AssetReference>(entity))
            {
                reference->assetKey = modelKey;
            }

            if (hadUberShader)
            {
                auto& uber = registry.emplace<UberShaderComponent>(
                    entity, static_cast<unsigned int>(renderable.GetModel()->get().GetMaterialCount()));
                if (wasLit) uber.SetFlagAll(UberShaderComponent::Flags::Lit);
                if (animated) uber.SetFlagAll(UberShaderComponent::Flags::Skinned);
            }

            if (transformEditor) transformEditor->RefreshCollisionBounds(entity);
        }
        history->Commit();
        refreshSceneWindows();
    }

    void EditorScene::changeSelectedMaterials(
        const unsigned int materialIndex, const std::string& materialKey) const
    {
        auto& registry = *sys->registry;
        std::vector<entt::entity> targets;
        for (const auto entity : selection->Selected())
        {
            const auto* renderable = registry.valid(entity) ? registry.try_get<Renderable>(entity) : nullptr;
            if (renderable == nullptr || !renderable->GetModel() ||
                std::cmp_greater_equal(materialIndex, renderable->GetModel()->get().GetMaterialCount()))
            {
                continue;
            }
            targets.push_back(entity);
        }
        if (targets.empty()) return;

        history->Begin(editor::EditAction::EditField, targets);
        for (const auto entity : targets)
        {
            auto& renderable = registry.get<Renderable>(entity);
            if (!renderable.SetMaterialKey(materialIndex, materialKey)) continue;
            if (auto* shader = registry.try_get<CustomShaderComponent>(entity)) shader->RebindOnNextUpdate();
            if (registry.any_of<UberShaderComponent>(entity)) sys->uberShaderSystem->RebindRenderable(entity);
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
        // Terrain restores the saved heights and texture weights; rebuild the derived
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
        syncLightTransforms();
        if (sys->lightSubSystem) sys->lightSubSystem->RefreshLights();

        if (selection)
        {
            static_cast<void>(selection->ReplaceWith(restored));
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

        if (editorModes) editorModes->ChangeState<editor::EditorSelectState>();
        if (gui) gui->HideDeleteConfirmation();

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

        if (selection)
        {
            static_cast<void>(selection->ReplaceWith(newRoots));
            if (const auto active = selection->Active(); active.has_value() && gui)
            {
                gui->FocusHierarchyOnEntity(*active);
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
        if (sage::SaveFlatpack(*sys->registry, entity, outputPath.string().c_str()))
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
        auto catalog = sage::ListFlatpacks(std::filesystem::path{"resources"});
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
        auto instance = sage::LoadFlatpack(*sys->registry, path.string().c_str(), anchor);
        if (!instance) return std::nullopt;
        const auto document = content::ReadDocument(path);
        for (std::size_t i = 0; i < instance.entities.size(); ++i)
        {
            const auto entity = instance.entities.at(i);
            sys->registry->emplace<editor::EditorMapEntity>(entity);
            const auto& node = json::At(document["entities"], i);
            if (node.HasMember("editorAssetKey"))
                sys->registry->emplace_or_replace<editor::AssetReference>(
                    entity, json::String(node, "editorAssetKey"));
        }

        sys->registry->emplace_or_replace<editor::AssetReference>(
            instance.root, path.lexically_normal().generic_string());

        // The loaded subtree has Renderables without an UberShaderComponent;
        // applyLitShaderToLoadedRenderables attaches one with Lit set, matching
        // the shader hookup the rest of the editor relies on. Bounds need to be
        // re-derived from the new world transforms (the saved boxes are stale).
        applyLitShaderToLoadedRenderables();
        if (transformEditor) transformEditor->RefreshCollisionBoundsRecursive(instance.root);
        return instance.root;
    }

    void EditorScene::adoptIntoFlatpackRoot(const std::vector<entt::entity>& roots) const
    {
        if (!particleEditorRoot && (!flatpackSession || !flatpackSession->IsActive())) return;

        const auto sessionRoot = particleEditorRoot.value_or(flatpackSession->Root());
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
        if (ImGui::BeginMenu("File", !IsPlaying()))
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
            if (ImGui::MenuItem("New Map", nullptr, false, !flatpackOpen))
            {
                newMapRequested = true;
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
        if (ImGui::BeginMenu("Edit", !IsPlaying()))
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
            const auto selected = selection->Selected();
            const bool canSnapToFloor = std::ranges::any_of(selected, [this](const entt::entity entity) {
                return sys->registry->valid(entity) && sys->registry->any_of<sgTransform>(entity);
            });
            if (ImGui::MenuItem("Snap to floor", nullptr, false, canSnapToFloor))
            {
                const bool beginTransaction = history && !history->HasActiveTransaction();
                if (beginTransaction) history->Begin(editor::EditAction::Transform, selected);
                transformEditor->SnapToFloor(selected, placementController->GridSurfaceY());
                if (beginTransaction) history->Commit();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Sculpt / Paint Terrain", "G", false, editorModes->CanBeginTerrainSculpt()))
            {
                editorModes->BeginTerrainSculptOnSelection();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View", !IsPlaying()))
        {
            if (ImGui::MenuItem("Navigation Grid", nullptr, navigationGridVisible))
            {
                navigationGridVisible = !navigationGridVisible;
                if (navigationGridVisible) rebuildNavigationGrid();
            }
            if (ImGui::MenuItem("Refresh Navigation Grid", nullptr, false, navigationGridVisible))
            {
                rebuildNavigationGrid();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera", !IsPlaying()))
        {
            if (ImGui::MenuItem(CAMERA_MODE_NAMES.at(0), nullptr, editorCamera.mode == editor::CameraMode::Game))
                setCameraMode(editor::CameraMode::Game);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Existing WASD/QE movement, MMB orbit, RMB ground pan");

            if (ImGui::MenuItem(CAMERA_MODE_NAMES.at(1), nullptr, editorCamera.mode == editor::CameraMode::Free))
                setCameraMode(editor::CameraMode::Free);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("WASD fly, Q/E down/up, Shift faster, RMB look, MMB pan");

            if (ImGui::MenuItem(
                    CAMERA_MODE_NAMES.at(2),
                    "F",
                    editorCamera.mode == editor::CameraMode::Focused,
                    !selection->Selected().empty()))
                setCameraMode(editor::CameraMode::Focused);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Q/E orbit sideways, W/S orbit up/down, RMB/MMB drag, wheel zoom; Esc returns to In-game");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add", !IsPlaying()))
        {
            if (ImGui::BeginMenu("Mesh"))
            {
                struct PrimitiveMenuItem
                {
                    const char* label;
                    const char* key;
                };
                constexpr std::array<PrimitiveMenuItem, 9> primitives = {{
                    {.label = "Sphere", .key = "primitive_sphere"},
                    {.label = "Hemisphere", .key = "primitive_hemisphere"},
                    {.label = "Plane", .key = "primitive_plane"},
                    {.label = "Cube", .key = "primitive_cube"},
                    {.label = "Cylinder", .key = "primitive_cylinder"},
                    {.label = "Cone", .key = "primitive_cone"},
                    {.label = "Torus", .key = "primitive_torus"},
                    {.label = "Knot", .key = "primitive_knot"},
                    {.label = "Polygon", .key = "primitive_poly"},
                }};
                for (const auto& primitive : primitives)
                {
                    if (ImGui::MenuItem(primitive.label)) addMesh(primitive.key, primitive.label);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Light"))
            {
                addLight();
            }
            // Spawn point markers, trigger volumes and terrains persist via map-only
            // streams, so a flatpack can't carry them — keep them map-only.
            const bool flatpackOpen = flatpackSession->IsActive();
            if (ImGui::MenuItem("Spawn Point", nullptr, false, !flatpackOpen))
            {
                addSpawnPoint();
            }
            if (ImGui::MenuItem("Trigger Volume", nullptr, false, !flatpackOpen))
            {
                addTriggerVolume();
            }
            if (ImGui::MenuItem("Terrain", nullptr, false, !flatpackOpen))
            {
                addTerrain();
            }
            if (ImGui::MenuItem("Empty Transform", nullptr, false, !flatpackOpen))
            {
                addEmptyTransform();
            }
            if (ImGui::MenuItem("Particle System"))
            {
                addParticleSystem();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project"))
        {
            if (ImGui::MenuItem(
                    "Graphics Settings", nullptr, graphicsSettingsWindowOpen, !graphicsSettingsWindowOpen))
            {
                graphicsSettingsWindowOpen = true;
                lightSettingsBeforeEdit = sys->settings->GetLightSettings();
                graphicsSettingsBeforeEdit = sys->settings->GetGraphicsSettings();
                lightSettingsDraft = lightSettingsBeforeEdit;
                graphicsSettingsDraft = graphicsSettingsBeforeEdit;
            }
            if (ImGui::MenuItem("Collision Matrix", nullptr, collisionMatrixWindowOpen))
            {
                collisionMatrixWindowOpen = !collisionMatrixWindowOpen;
            }
            ImGui::EndMenu();
        }
        canvasEditor->DrawSceneMenu(!flatpackSession->IsActive() && !IsPlaying());
        drawPlayStopButton();
        ImGui::EndMainMenuBar();
    }

    void EditorScene::drawPlayStopButton() const
    {
        bool playing = IsPlaying();
        // No factory means this is the standalone editor (no game linked in);
        // a flatpack edit session is its own isolated scene, so disallow play.
        const bool canPlay = HasGameRuntimeFactory() && !flatpackSession->IsActive();

        // F5 toggles play (Unity-style), ignored while typing in a field.
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !ImGui::GetIO().WantTextInput)
        {
            if (playing)
                stopPlay();
            else if (canPlay)
                startPlay();
            playing = IsPlaying();
        }

        constexpr float buttonWidth = 80.0f;
        const float controlsWidth =
            playing ? 3.0f * buttonWidth + 2.0f * ImGui::GetStyle().ItemSpacing.x : buttonWidth;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - controlsWidth - 8.0f));
        if (playing)
        {
            if (ImGui::Button(gameRuntime->IsPaused() ? "Resume" : "Pause", ImVec2{buttonWidth, 0.0f}))
                gameRuntime->SetPaused(!gameRuntime->IsPaused());
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameRuntime->IsPaused());
            if (ImGui::Button("Step", ImVec2{buttonWidth, 0.0f})) gameRuntime->Step(1.0f / 60.0f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Advance one simulation frame (1/60 second) while paused");
            ImGui::SameLine();
        }
        const bool disabled = !playing && !canPlay;
        if (disabled) ImGui::BeginDisabled();
        if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(buttonWidth, 0.0f)))
        {
            if (playing)
                stopPlay();
            else
                startPlay();
        }
        if (disabled) ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && !disabled)
        {
            ImGui::SetTooltip(playing ? "Stop play session (Esc/F5)" : "Play this map in-engine (F5)");
        }
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
            constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInner |
                                                   ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                                   ImGuiTableFlags_HighlightHoveredColumn;
            const float gridHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f;
            if (ImGui::BeginTable("##collisionMatrix", count + 1, tableFlags, ImVec2(0.0f, gridHeight)))
            {
                ImGui::TableSetupColumn(
                    "", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel);
                for (int c = 0; c < count; ++c)
                {
                    const std::string header{layers.at(count - 1 - c).layerName};
                    ImGui::TableSetupColumn(
                        header.c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
                }
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableAngledHeadersRow();

                bool changed = false;
                for (int r = 0; r < count; ++r)
                {
                    const auto rowLayer = layers.at(r);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(std::string{rowLayer.layerName}.c_str());
                    for (int c = 0; c < count - r; ++c)
                    {
                        const auto colLayer = layers.at(count - 1 - c);
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

    void EditorScene::applyGraphicsSettings(const LightSettings& light, const GraphicsSettings& graphics) const
    {
        sys->settings->SetLightSettings(light);
        sys->settings->SetGraphicsSettings(graphics);
        sys->lightSubSystem->ApplyLightSettings(light);
        sys->lightSubSystem->SetShadowsEnabled(graphics.shadows);
        if (gameRuntime) gameRuntime->ApplyProjectSettings(light, graphics);
    }

    void EditorScene::drawGraphicsSettingsWindow() const
    {
        if (!graphicsSettingsWindowOpen) return;
        ImGui::SetNextWindowSize(ImVec2{380.0f, 0.0f}, ImGuiCond_FirstUseEver);
        bool open = graphicsSettingsWindowOpen;
        bool saved = false;
        if (ImGui::Begin("Graphics Settings", &open))
        {
            ImGui::TextUnformatted("Changes preview immediately. Save keeps them for this project.");
            ImGui::TextUnformatted("The scene viewport previews these effects.");
            bool changed = false;
            ImGui::SeparatorText("Lighting");
            changed |= ImGui::ColorEdit3("Ambient Color", &lightSettingsDraft.ambient.x);
            changed |= ImGui::SliderFloat("Gamma", &lightSettingsDraft.gamma, 0.1f, 4.0f, "%.2f");
            changed |= ImGui::Checkbox("Shadows", &graphicsSettingsDraft.shadows);

            ImGui::SeparatorText("Bloom");
            changed |= ImGui::Checkbox("Enable Bloom", &graphicsSettingsDraft.bloom);
            ImGui::BeginDisabled(!graphicsSettingsDraft.bloom);
            changed |=
                ImGui::SliderFloat("Bloom Strength", &graphicsSettingsDraft.bloomStrength, 0.0f, 2.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::SeparatorText("Ambient Occlusion");
            changed |= ImGui::Checkbox("Enable Ambient Occlusion", &graphicsSettingsDraft.ambientOcclusion);
            ImGui::BeginDisabled(!graphicsSettingsDraft.ambientOcclusion);
            changed |=
                ImGui::SliderFloat("Occlusion Radius", &graphicsSettingsDraft.occlusionRadius, 0.1f, 4.0f, "%.2f");
            changed |= ImGui::SliderFloat(
                "Occlusion Strength", &graphicsSettingsDraft.occlusionStrength, 0.0f, 1.5f, "%.2f");
            ImGui::EndDisabled();

            ImGui::SeparatorText("Depth of Field");
            changed |= ImGui::Checkbox("Enable Depth of Field", &graphicsSettingsDraft.depthOfField);
            ImGui::BeginDisabled(!graphicsSettingsDraft.depthOfField);
            changed |= ImGui::Checkbox("Focus Camera Target", &graphicsSettingsDraft.focusCameraTarget);
            if (graphicsSettingsDraft.focusCameraTarget)
                ImGui::TextDisabled(
                    "Target distance: %.1f", Vector3Distance(ActiveCamera()->position, ActiveCamera()->target));
            ImGui::BeginDisabled(graphicsSettingsDraft.focusCameraTarget);
            changed |=
                ImGui::SliderFloat("Focus Distance", &graphicsSettingsDraft.focusDistance, 0.5f, 500.0f, "%.1f");
            ImGui::EndDisabled();
            ImGui::TextDisabled("Focus Range is the sharp half-width in world units.");
            changed |= ImGui::SliderFloat("Focus Range", &graphicsSettingsDraft.focusRange, 0.1f, 50.0f, "%.1f");
            changed |= ImGui::SliderFloat(
                "Max Blur (pixels at 720p)", &graphicsSettingsDraft.maxBlurRadius, 0.0f, 12.0f, "%.1f");
            ImGui::EndDisabled();

            ImGui::SeparatorText("Post Processing");
            changed |= ImGui::Checkbox("FXAA", &graphicsSettingsDraft.fxaa);
            changed |= ImGui::Checkbox("Color Grading", &graphicsSettingsDraft.colorGrading);
            ImGui::BeginDisabled(!graphicsSettingsDraft.colorGrading);
            changed |= ImGui::SliderFloat("Saturation", &graphicsSettingsDraft.saturation, 0.0f, 2.0f, "%.2f");
            changed |= ImGui::SliderFloat("Contrast", &graphicsSettingsDraft.contrast, 0.5f, 2.0f, "%.2f");
            ImGui::EndDisabled();

            if (ImGui::Button("Restore Defaults"))
            {
                lightSettingsDraft = LightSettings{};
                graphicsSettingsDraft = GraphicsSettings{};
                changed = true;
            }
            if (changed) applyGraphicsSettings(lightSettingsDraft, graphicsSettingsDraft);
            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                applyGraphicsSettings(lightSettingsDraft, graphicsSettingsDraft);
                if (sys->settings->SaveProjectSettings())
                {
                    saved = true;
                    open = false;
                }
                else
                {
                    applyGraphicsSettings(lightSettingsBeforeEdit, graphicsSettingsBeforeEdit);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) open = false;
        }
        ImGui::End();
        if (!open && !saved) applyGraphicsSettings(lightSettingsBeforeEdit, graphicsSettingsBeforeEdit);
        graphicsSettingsWindowOpen = open;
    }

    void EditorScene::drawTerrainBrushTools() const
    {
        const auto currentBrush = editorModes->CurrentTerrainSculptState();
        if (!currentBrush) return;
        auto& sculpt = currentBrush->get();

        ImGui::BeginDisabled(history && history->HasActiveTransaction());
        // Order must match TerrainBrushMode (engine/TerrainMesh.hpp).
        static constexpr std::array<const char*, 7> BRUSH_MODE_NAMES = {
            "Raise / Lower", "Smooth", "Flatten", "Noise", "Erosion", "Ramp", "Texture Paint"};
        int mode = static_cast<int>(sculpt.brushMode);
        if (ImGui::Combo("Brush", &mode, BRUSH_MODE_NAMES.data(), static_cast<int>(BRUSH_MODE_NAMES.size())))
        {
            sculpt.brushMode = static_cast<TerrainBrushMode>(mode);
        }

        if (sculpt.brushMode == TerrainBrushMode::Texture && sys->registry->valid(sculpt.terrain))
        {
            auto& terrain = sys->registry->get<Terrain>(sculpt.terrain);
            ImGui::TextUnformatted("Texture slots");
            for (int layer = 0; layer < static_cast<int>(TERRAIN_TEXTURE_LAYERS); ++layer)
            {
                if (layer > 0) ImGui::SameLine();
                const auto label = std::to_string(layer + 1);
                ImGui::RadioButton(label.c_str(), &sculpt.textureLayer, layer);
            }
            auto& texturePath = terrain.textures.at(static_cast<std::size_t>(sculpt.textureLayer));
            const auto label = texturePath.empty() ? std::string("Choose texture...")
                                                   : std::filesystem::path(texturePath).stem().string();
            if (ImGui::BeginCombo("Texture", label.c_str()))
            {
                if (ImGui::Selectable("Empty", texturePath.empty()))
                {
                    history->Begin(editor::EditAction::PaintTerrain, {sculpt.terrain});
                    texturePath.clear();
                    for (auto& weights : terrain.textureWeights)
                        weights.at(static_cast<std::size_t>(sculpt.textureLayer)) = 0.0f;
                    auto& renderable = sys->registry->get<DynamicRenderable>(sculpt.terrain);
                    if (auto model = renderable.GetModel())
                    {
                        UpdateTerrainTextures(model->get(), terrain);
                        UpdateTerrainTextureRegion(
                            model->get(), terrain, {0, 0, terrain.resolution - 1, terrain.resolution - 1});
                    }
                    history->Commit();
                }
                for (const auto& path : sculpt.terrainTextures)
                {
                    const auto name = std::filesystem::path(path).stem().string();
                    if (ImGui::Selectable(name.c_str(), path == texturePath))
                    {
                        history->Begin(editor::EditAction::PaintTerrain, {sculpt.terrain});
                        texturePath = path;
                        auto& renderable = sys->registry->get<DynamicRenderable>(sculpt.terrain);
                        if (auto model = renderable.GetModel()) UpdateTerrainTextures(model->get(), terrain);
                        history->Commit();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
                }
                ImGui::EndCombo();
            }
            if (ImGui::SmallButton("Refresh textures")) sculpt.RefreshTerrainTextures();
            if (sculpt.terrainTextures.empty())
                ImGui::TextWrapped("No base-color textures found in resources/textures/terrain.");
            if (!texturePath.empty())
            {
                auto& renderable = sys->registry->get<DynamicRenderable>(sculpt.terrain);
                if (auto model = renderable.GetModel())
                {
                    auto& texture = model->get().materials[0].maps[sculpt.textureLayer].texture;
                    if (texture.id != 0)
                        ImGui::Image(reinterpret_cast<ImTextureID>(&texture), ImVec2{72.0f, 72.0f});
                }
            }
            float tileSize = terrain.textureTileSize;
            if (ImGui::InputFloat(
                    "Tile size", &tileSize, 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue) &&
                std::isfinite(tileSize) && tileSize > 0.0f && tileSize != terrain.textureTileSize)
            {
                history->Begin(editor::EditAction::PaintTerrain, {sculpt.terrain});
                terrain.textureTileSize = tileSize;
                auto& renderable = sys->registry->get<DynamicRenderable>(sculpt.terrain);
                if (auto model = renderable.GetModel()) UpdateTerrainTextures(model->get(), terrain);
                history->Commit();
            }
            ImGui::BeginDisabled(texturePath.empty());
            if (ImGui::Button("Fill terrain"))
            {
                history->Begin(editor::EditAction::PaintTerrain, {sculpt.terrain});
                std::array<float, TERRAIN_TEXTURE_LAYERS> weights{};
                weights.at(static_cast<std::size_t>(sculpt.textureLayer)) = 1.0f;
                terrain.textureWeights.assign(terrain.heights.size(), weights);
                auto& renderable = sys->registry->get<DynamicRenderable>(sculpt.terrain);
                if (auto model = renderable.GetModel())
                    UpdateTerrainTextureRegion(
                        model->get(), terrain, {0, 0, terrain.resolution - 1, terrain.resolution - 1});
                history->Commit();
            }
            ImGui::EndDisabled();
        }

        const bool ramp = sculpt.brushMode == TerrainBrushMode::Ramp;
        ImGui::SliderFloat(ramp ? "Width" : "Radius", &sculpt.brushRadius, 0.5f, 50.0f, "%.1f");
        if (!ramp)
        {
            ImGui::SliderFloat("Strength", &sculpt.brushStrength, 0.5f, 30.0f, "%.1f");
        }

        ImGui::Spacing();
        switch (sculpt.brushMode)
        {
        case TerrainBrushMode::RaiseLower:
            ImGui::TextUnformatted("LMB raise - Shift+LMB lower");
            break;
        case TerrainBrushMode::Smooth:
            ImGui::TextUnformatted("LMB to average toward neighbours");
            break;
        case TerrainBrushMode::Flatten:
            ImGui::TextUnformatted("LMB flattens toward the click height");
            break;
        case TerrainBrushMode::Noise:
            ImGui::TextUnformatted("LMB to add randomized roughness");
            break;
        case TerrainBrushMode::Erosion:
            ImGui::TextUnformatted("LMB to wear ridges down");
            break;
        case TerrainBrushMode::Texture:
            ImGui::TextUnformatted("LMB paint - Shift+LMB erase layer");
            break;
        case TerrainBrushMode::Ramp:
            ImGui::TextUnformatted("Click start, then end - Esc cancels");
            break;
        }
        ImGui::TextWrapped("Keys 1-7 pick brush - [ / ] size - Esc to finish");
        ImGui::EndDisabled();
    }

    void EditorScene::addLight() const
    {
        Vector3 position = Vector3Add(
            sys->camera->getRaylibCam()->target, {.x = 0.0f, .y = DEFAULT_LIGHT_HEIGHT_OFFSET, .z = 0.0f});
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = Vector3Add(*snappedPosition, {.x = 0.0f, .y = DEFAULT_LIGHT_HEIGHT_OFFSET, .z = 0.0f});
        }

        const auto entity = entityOperations->CreateLight(position);
        adoptIntoFlatpackRoot({entity});
        sys->lightSubSystem->RefreshLights();
        if (history) history->RecordCreate(editor::EditAction::AddLight, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::addSpawnPoint() const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = *snappedPosition;
        }

        const auto entity = entityOperations->CreateSpawnPoint(position);
        if (history) history->RecordCreate(editor::EditAction::AddSpawnPoint, {entity});
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

    void EditorScene::addEmptyTransform() const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = *snappedPosition;
        }

        const auto entity = entityOperations->CreateEmptyTransform(position);
        if (history) history->RecordCreate(editor::EditAction::AddEmptyTransform, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::addParticleSystem() const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snapped = placementController->SnappedPlacementPosition()) position = *snapped;
        const auto root = entityOperations->CreateEmptyTransform(position);
        sys->registry->get<sgTransform>(root).name = "Particle System";
        sys->registry->emplace<ParticleSystemComponent>(root);
        const auto emitter = entityOperations->CreateEmptyTransform(position);
        auto& transform = sys->registry->get<sgTransform>(emitter);
        transform.name = "Emitter";
        transform.SetParent(root);
        transform.position.local = Vector3{};
        sys->registry->emplace<ParticleEmitterComponent>(emitter);
        adoptIntoFlatpackRoot({root});
        if (history) history->RecordCreate(editor::EditAction::AddEmptyTransform, {root});
        editorModes->SelectSceneEntity(root);
    }

    void EditorScene::drawParticleEditorTab() const
    {
        if (!particleEditorRoot) return;
        if (!sys->registry->valid(*particleEditorRoot) ||
            !particleEditor.DrawPreview(
                *sys->registry, *particleEditorRoot, sys->settings->GetRenderViewportScreenRect()))
            closeParticleEditor();
    }

    void EditorScene::openParticleEditor(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity)) return;
        if (!particleEditorRoot)
        {
            particleSceneCamera = *sys->camera->getRaylibCam();
            particleSceneSelection = selection->Selected();
        }
        particleEditorRoot = entity;
        sys->registry->ctx().insert_or_assign<EntityViewScope>(EntityViewScope{entity});
        editorModes->SelectSceneEntity(entity);
        sys->camera->FocusEntity(entity);
        refreshSceneWindows();
        refreshOverlay();
    }

    void EditorScene::closeParticleEditor() const
    {
        if (!particleEditorRoot) return;
        sys->registry->ctx().erase<EntityViewScope>();
        particleEditorRoot.reset();
        if (particleSceneCamera)
            sys->camera->SetCamera(particleSceneCamera->position, particleSceneCamera->target);
        particleSceneCamera.reset();
        static_cast<void>(selection->ReplaceWith(particleSceneSelection));
        particleSceneSelection.clear();
        refreshSceneWindows();
        refreshOverlay();
    }

    void EditorScene::addMesh(const char* modelKey, const char* name) const
    {
        Vector3 position = sys->camera->getRaylibCam()->target;
        if (const auto snappedPosition = placementController->SnappedPlacementPosition();
            snappedPosition.has_value())
        {
            position = *snappedPosition;
        }

        const auto entity = entityOperations->CreateMesh(position, modelKey, name);
        adoptIntoFlatpackRoot({entity});
        if (history) history->RecordCreate(editor::EditAction::AddMesh, {entity});
        editorModes->SelectSceneEntity(entity);
    }

    void EditorScene::clearCurrentMap() const
    {
        closeParticleEditor();
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

        sys->registry->ctx().erase<InitialCanvases>();
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
            if (!editor_layout::IsMapBaseTransform(transform)) continue;
            auto& renderable = existingBaseView.get<Renderable>(entity);
            hasMapBase = true;
            if (!sys->registry->any_of<editor::EditorMapBase>(entity))
            {
                sys->registry->emplace<editor::EditorMapBase>(entity);
            }
            if (!sys->registry->any_of<editor::AssetReference>(entity))
            {
                if (const auto model = renderable.GetModel(); model.has_value())
                {
                    sys->registry->emplace<editor::AssetReference>(
                        entity, editor::AssetReference{.assetKey = model->get().GetKey()});
                }
            }
            auto& collideable = existingBaseView.get<Collideable>(entity);
            collideable.isStatic = true;
            collideable.active = false;
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
        transform.scale.world = {.x = DEFAULT_MAP_BASE_SIZE, .y = 1.0f, .z = DEFAULT_MAP_BASE_SIZE};
        transform.name = DEFAULT_MAP_BASE_NAME;

        auto model = ResourceManager::GetInstance().GetModelView(DEFAULT_MAP_BASE_MODEL_KEY);
        auto& renderable = sys->registry->emplace<Renderable>(entity, std::move(model), MatrixIdentity());
        renderable.active = false;

        const BoundingBox localBounds = {
            .min = {.x = -0.5f, .y = -DEFAULT_MAP_BASE_HALF_HEIGHT, .z = -0.5f},
            .max = {.x = 0.5f, .y = DEFAULT_MAP_BASE_HALF_HEIGHT, .z = 0.5f}};
        auto& collideable = sys->registry->emplace<Collideable>(entity, localBounds, transform.GetMatrixNoRot());
        collideable.isStatic = true;
        collideable.active = false;
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
        if (navigationGridVisible)
        {
            sys->navigationGridSystem->InitGridHeightAndNormals();
        }
        selection->Clear();
        editorModes->ChangeState<editor::EditorSelectState>();
        refreshOverlay();
        refreshSceneWindows();
    }

    editor::EditorGui::AssetRenameResult EditorScene::handleAssetFileRename(
        const std::size_t index, const std::string& requestedFileName) const
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
        std::vector<entt::entity> draggedEntities;
        draggedEntities.reserve(request.draggedEntities.size());
        for (const auto dragged : request.draggedEntities)
        {
            if (std::ranges::find(draggedEntities, dragged) == draggedEntities.end())
            {
                draggedEntities.push_back(dragged);
            }
        }
        auto newParent = request.newParent;
        auto insertBefore = request.insertBefore;

        if (draggedEntities.empty()) return;

        if (particleEditorRoot || (flatpackSession && flatpackSession->IsActive()))
        {
            const auto sessionRoot = particleEditorRoot.value_or(flatpackSession->Root());
            // The session root anchors the open flatpack: it stays at the top,
            // and anything dropped at the top level belongs under it.
            std::erase(draggedEntities, sessionRoot);
            if (draggedEntities.empty()) return;
            if (newParent == entt::null)
            {
                newParent = sessionRoot;
                insertBefore = entt::null;
            }
        }

        for (const auto dragged : draggedEntities)
        {
            if (!sys->registry->valid(dragged) || !sys->registry->any_of<sgTransform>(dragged)) return;
        }
        if (newParent != entt::null &&
            (!sys->registry->valid(newParent) || !sys->registry->any_of<sgTransform>(newParent)))
        {
            return;
        }
        if (std::ranges::find(draggedEntities, newParent) != draggedEntities.end()) return;

        if (insertBefore != entt::null)
        {
            if (!sys->registry->valid(insertBefore) || !sys->registry->any_of<sgTransform>(insertBefore) ||
                std::ranges::find(draggedEntities, insertBefore) != draggedEntities.end())
            {
                return;
            }

            const auto insertParent = sys->registry->get<sgTransform>(insertBefore).GetParent();
            if (insertParent != newParent)
            {
                return;
            }
        }

        // Refuse cycles: none of the moved roots may be an ancestor of the new parent.
        for (auto cur = newParent; cur != entt::null;)
        {
            if (!sys->registry->any_of<sgTransform>(cur)) break;
            if (std::ranges::find(draggedEntities, cur) != draggedEntities.end()) return;
            cur = sys->registry->get<sgTransform>(cur).GetParent();
        }

        if (history) history->Begin(editor::EditAction::Reparent, draggedEntities);
        for (const auto dragged : draggedEntities)
        {
            sys->transformSystem->SetParent(dragged, newParent, insertBefore);
            hierarchyTree->NoteHierarchyMove(dragged, newParent, insertBefore);
        }
        if (history) history->Commit();
        refreshSceneWindows();
    }

    bool EditorScene::HandleEscapePressed() const
    {
        if (canvasEditor->IsActive()) return true;
        // Esc exits a play session first, then leaves object focus, then cancels editor modes.
        if (gameRuntime)
        {
            stopPlay();
            return true;
        }
        if (editorCamera.mode == editor::CameraMode::Focused && !ImGui::GetIO().WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            setCameraMode(editor::CameraMode::Game);
            return true;
        }
        return editorModes->HandleEscapePressed();
    }

    bool EditorScene::IsPlaying() const
    {
        return gameRuntime != nullptr;
    }

    Camera3D* EditorScene::ActiveCamera() const
    {
        if (gameRuntime) return gameRuntime->GetCamera();
        return sys->camera->getRaylibCam();
    }

    void EditorScene::startPlay() const
    {
        closeParticleEditor();
        if (gameRuntime) return;
        if (!HasGameRuntimeFactory())
        {
            TraceLog(LOG_WARNING, "Play: no game runtime registered (standalone editor build).");
            return;
        }

        // Snapshot the editor scene (including unsaved edits) to a temp map the
        // runtime loads into its own registry. collectMapHierarchyOrder() also
        // ensures the default map base exists before serialising.
        const auto hierarchyOrder = collectMapHierarchyOrder();
        if (!editor::SaveMap(*sys->registry, PLAY_SESSION_MAP_PATH, hierarchyOrder)) return;

        GameRuntimeContext context;
        context.audioManager = std::ref(*sys->audioManager);
        context.windowSize = sys->settings->GetScreenSize();
        context.viewportScreenRect = gameViewportScreenRect();
        context.mapPath = PLAY_SESSION_MAP_PATH;
        gui->ClearConsole();
        context.managedLogSink = [this](const CSharpLogLevel level, const std::string_view message) {
            gui->AddConsoleEntry(level, message);
        };
        gameRuntime = CreateGameRuntime(context);
        if (gameRuntime)
        {
            runtimeSelection.reset();
            runtimeHierarchy = std::make_unique<editor::EditorHierarchyTree>(gameRuntime->InspectionRegistry());
            if (const auto selected = selection->Active(); selected && sys->registry->valid(*selected))
            {
                if (const auto* id = sys->registry->try_get<PersistentEntityId>(*selected))
                {
                    const auto live = content::FindEntityById(gameRuntime->InspectionRegistry(), id->id);
                    if (live != entt::null) runtimeSelection = live;
                }
            }
            refreshRuntimeInspection();
            gameRuntime->ApplyProjectSettings(
                sys->settings->GetLightSettings(), sys->settings->GetGraphicsSettings());
        }
        if (!gameRuntime)
        {
            TraceLog(LOG_WARNING, "Play: failed to create game runtime; staying in edit mode.");
        }
    }

    void EditorScene::stopPlay() const
    {
        if (!gameRuntime) return;
        // The runtime owned its own registry, so tearing it down leaves the
        // editor's scene exactly as it was — no restore needed.
        gui->SetInspector("None", {}, {});
        runtimeHierarchy.reset();
        runtimeSelection.reset();
        gameRuntime.reset();
        refreshSceneWindows();
        std::error_code ec;
        std::filesystem::remove(PLAY_SESSION_MAP_PATH, ec);
    }

    Rectangle EditorScene::gameViewportScreenRect() const
    {
        const auto offset = sys->settings->GetViewportOffset();
        const auto renderOffset = sys->settings->GetRenderViewportOffset();
        const auto renderSize = sys->settings->GetRenderViewPort();
        return {
            .x = offset.x + renderOffset.x,
            .y = offset.y + renderOffset.y,
            .width = renderSize.x,
            .height = renderSize.y};
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
        std::function<void()> _onEditorSettingsChanged,
        std::function<void(editor::InspectorRegistry&)> registerGameComponents,
        editor::CSharpScriptEditorConfig _csharpScripts,
        std::function<void()> updateLoadingScreen)
        : sys(_sys), csharpScripts(std::move(_csharpScripts))
    {
        canvasEditor = std::make_unique<editor::CanvasEditor>(csharpScripts);
        editor::RegisterDefaultInspectorComponents(inspectorRegistry);
        if (registerGameComponents) registerGameComponents(inspectorRegistry);
        assetCatalog =
            std::make_unique<editor::EditorAssetCatalog>(editor::EditorAssetCatalog::FromLoadedModels());
        assetCatalog->LoadDefaults();
        modelDefaults = std::make_unique<editor::EditorModelDefaultsController>(
            *assetCatalog, [this]() { return isPlaceState(); }, [this]() { refreshOverlay(); });
        selection = std::make_unique<editor::EditorSelection>(sys);
        pickingService = std::make_unique<editor::EditorPickingService>(sys);
        entityOperations = std::make_unique<editor::EditorEntityOperations>(sys, &inspectorRegistry);
        hierarchyTree = std::make_unique<editor::EditorHierarchyTree>(*sys->registry);
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
            sys, &inspectorRegistry, [this](const std::vector<entt::entity>& restored) {
                onHistoryApplied(restored);
            });
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
                closeParticleEditor();
                if (flatpackSession) flatpackSession->Open(std::move(path));
            },
            [this](const std::filesystem::path& path, const std::string& requestedName) {
                return renameFlatpackFile(path, requestedName);
            },
            [this](const std::filesystem::path& path) { deleteFlatpackFile(path); },
            [this](const editor::EditorGui::SceneSelectionRequest& request) {
                if (gameRuntime)
                {
                    if (gameRuntime->InspectionRegistry().valid(request.entity)) runtimeSelection = request.entity;
                    refreshRuntimeInspection();
                }
                else
                    editorModes->SelectSceneFromHierarchy(request);
            },
            [this](const editor::EditorGui::HierarchyMoveRequest& request) { moveHierarchyEntity(request); },
            modelDefaults->Callbacks());

        gui->ConfigureAssetBrowser(
            csharpScripts,
            [this](const editor::BrowserAsset& asset) { return findAssetUsages(asset); },
            [this](const editor::AssetUsage& usage) { return navigateAssetUsage(usage); });

        if (csharpScripts.IsConfigured())
        {
            scriptBrowser = std::make_unique<ImGui::FileBrowser>(
                ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SkipItemsCausingError |
                ImGuiFileBrowserFlags_EnterNewFilename);
            scriptBrowser->SetTitle("Select or create C# script");
            scriptBrowser->SetTypeFilters({".cs"});
        }

        shaderBrowser = std::make_unique<ImGui::FileBrowser>(
            ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SkipItemsCausingError);
        shaderBrowser->SetTitle("Select shader");

        mapController = std::make_unique<editor::EditorMapController>(
            sys,
            _editorSettings,
            std::move(_onEditorSettingsChanged),
            history.get(),
            &inspectorRegistry,
            editor::EditorMapController::Callbacks{
                .prepareForLoad = [this]() { clearCurrentMap(); },
                .finishLoad = [this]() { refreshAfterMapLoad(); },
                .setSceneName = [this](const std::string& name) { SetSceneName(name); },
                .prepareSave = [this]() { return collectMapHierarchyOrder(); }});

        flatpackSession = std::make_unique<editor::EditorFlatpackEditSession>(
            sys,
            history.get(),
            &inspectorRegistry,
            editor::EditorFlatpackEditSession::Callbacks{
                .clearScene = [this]() { clearCurrentMap(); },
                .loadFlatpack =
                    [this](const std::filesystem::path& path) {
                        const auto root = PlaceFlatpackAt(path, Vector3{.x = 0.0f, .y = 0.0f, .z = 0.0f});
                        flatpackSourceEntities.clear();
                        if (root)
                        {
                            sys->registry->remove<editor::AssetReference>(*root);
                            const auto source = content::ReadDocument(path);
                            std::vector<entt::entity> loaded;
                            for (auto entity : sys->registry->view<PersistentEntityId>())
                                loaded.push_back(entity);
                            std::ranges::sort(loaded, [this](auto a, auto b) {
                                return sys->registry->get<PersistentEntityId>(a).id <
                                       sys->registry->get<PersistentEntityId>(b).id;
                            });
                            for (std::size_t i = 0; i < loaded.size(); ++i)
                                flatpackSourceEntities.emplace(
                                    json::Id(json::At(source["entities"], i), "id"), loaded.at(i));
                        }
                        return root.value_or(entt::null);
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
        mapController->RestoreLastOpenedMap(updateLoadingScreen);
        refreshOverlay();
        refreshSceneWindows();
        refreshFlatpackCatalog();
    }

    EditorScene::~EditorScene() = default;
} // namespace sage

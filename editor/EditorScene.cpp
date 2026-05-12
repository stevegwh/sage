#include "EditorScene.hpp"

#include "EditorGui.hpp"
#include "engine/AudioManager.hpp"
#include "engine/Camera.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/Cursor.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/GameUiEngine.hpp"
#include "engine/LightManager.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/UserInput.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/systems/RenderSystem.hpp"
#include "engine/systems/TransformSystem.hpp"

#include "engine/systems/NavigationGridSystem.hpp"
#include "raylib.h"
#include "raymath.h"

#include <format>

namespace sage
{
    namespace
    {
        constexpr float kGridHalfExtent = 50.0f;
        constexpr float kGridPickSurfaceHalfHeight = 0.02f;
        constexpr float kPlacementMarkerHeight = 0.16f;
    } // namespace

    const EditorScene::PlaceableMesh& EditorScene::selectedPlaceable() const
    {
        return placeables.at(selectedPlaceableIndex);
    }

    std::string EditorScene::describeHoveredGrid() const
    {
        if (!hoveredGridSquare.has_value()) return "None";
        return std::format("{}, {}", hoveredGridSquare->row, hoveredGridSquare->col);
    }

    std::string EditorScene::makePlacedLabel(const PlaceableMesh& placeable) const
    {
        return std::format("_EDITOR_{}_{:03}", placeable.labelStem, placedMeshCount + 1);
    }

    void EditorScene::createGridPickSurface() const
    {
        const auto entity = sys->registry->create();
        const BoundingBox gridBounds = {
            {-kGridHalfExtent, -kGridPickSurfaceHalfHeight, -kGridHalfExtent},
            {kGridHalfExtent, kGridPickSurfaceHalfHeight, kGridHalfExtent}};
        auto& collideable = sys->registry->emplace<Collideable>(entity, gridBounds, MatrixIdentity());
        collideable.SetCollisionLayer(collision_layers::GeometrySimple);
        sys->registry->emplace<StaticCollideable>(entity);
    }

    void EditorScene::refreshPlacementTarget()
    {
        hoveredGridSquare.reset();
        snappedPlacementPosition.reset();

        const auto& collision = sys->cursor->getFirstNaviCollision();
        if (!collision.hit) return;

        GridSquare square{};
        if (!sys->navigationGridSystem->WorldToGridSpace(collision.point, square)) return;

        const auto* gridSquare = sys->navigationGridSystem->GetGridSquare(square.row, square.col);
        if (!gridSquare) return;

        hoveredGridSquare = square;
        snappedPlacementPosition = {
            gridSquare->worldPosCentre.x,
            collision.point.y,
            gridSquare->worldPosCentre.z};
    }

    void EditorScene::refreshOverlay() const
    {
        gui->SetPlacementStatus(selectedPlaceable().displayName, describeHoveredGrid(), lastPlacedLabel);
    }

    void EditorScene::cyclePlaceable()
    {
        selectedPlaceableIndex = (selectedPlaceableIndex + 1) % placeables.size();
        refreshOverlay();
    }

    void EditorScene::placeSelectedMesh()
    {
        if (!snappedPlacementPosition.has_value()) return;

        const auto& placeable = selectedPlaceable();
        const auto entity = sys->registry->create();
        sys->registry->emplace<sgTransform>(entity);
        sys->transformSystem->SetPosition(entity, *snappedPlacementPosition);
        sys->transformSystem->SetScale(entity, 1.0f);
        sys->transformSystem->SetRotation(entity, Vector3Zero());

        auto model = ResourceManager::GetInstance().GetModelCopy(placeable.modelKey);
        auto& renderable = sys->registry->emplace<Renderable>(entity, std::move(model), MatrixIdentity());
        lastPlacedLabel = makePlacedLabel(placeable);
        renderable.SetName(lastPlacedLabel);

        const auto localBounds = renderable.GetModel()->CalcLocalBoundingBox();
        auto& collideable = sys->registry->emplace<Collideable>(
            entity, localBounds, sys->registry->get<sgTransform>(entity).GetMatrixNoRot());
        collideable.SetCollisionLayer(collision_layers::Obstacle);
        collideable.blocksNavigation = true;
        sys->registry->emplace<StaticCollideable>(entity);
        sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);

        ++placedMeshCount;
        refreshOverlay();
    }

    void EditorScene::Update()
    {
        sys->audioManager->Update();
        sys->userInput->ListenForInput();
        sys->camera->Update();
        sys->cursor->Update();
        refreshPlacementTarget();

        if (IsKeyPressed(KEY_TAB))
        {
            cyclePlaceable();
        }
        if (IsKeyPressed(KEY_P))
        {
            placeSelectedMesh();
        }

        refreshOverlay();
        sys->UI().Update();
    }

    void EditorScene::Draw3D() const
    {
        sys->renderSystem->Draw();
        DrawGrid(120, 1.0f);
        DrawLine3D({0, 0.02f, 0}, {8, 0.02f, 0}, RED);
        DrawLine3D({0, 0.02f, 0}, {0, 8, 0}, GREEN);
        DrawLine3D({0, 0.02f, 0}, {0, 0.02f, 8}, BLUE);

        if (snappedPlacementPosition.has_value())
        {
            const Vector3 marker = {
                snappedPlacementPosition->x,
                snappedPlacementPosition->y + kPlacementMarkerHeight,
                snappedPlacementPosition->z};
            DrawCubeWires(marker, 1.0f, kPlacementMarkerHeight, 1.0f, GOLD);
            DrawSphere(marker, 0.08f, GOLD);
        }
    }

    void EditorScene::Draw2D() const
    {
        sys->UI().Draw2D();
    }

    EditorScene::EditorScene(EngineSystems* _sys)
        : sys(_sys), gui(std::make_unique<editor::EditorGui>(&sys->UI(), sys->settings))
    {
        sys->navigationGridSystem->Init(100, 1.0f);
        createGridPickSurface();
        placeables = {
            PlaceableMesh{"Sphere", "vfx_sphere", "SPHERE"},
            PlaceableMesh{"Flat Torus", "vfx_flattorus", "FLAT_TORUS"},
            PlaceableMesh{"Sword", "mdl_sword", "SWORD"},
        };
        refreshOverlay();
    }

    EditorScene::~EditorScene() = default;
} // namespace sage

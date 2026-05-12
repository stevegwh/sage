#pragma once

#include "engine/components/NavigationGridSquare.hpp"
#include "raylib.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sage
{
    class EngineSystems;

    namespace editor
    {
        class EditorGui;
    }

    class EditorScene
    {
        struct PlaceableMesh
        {
            std::string displayName;
            std::string modelKey;
            std::string labelStem;
        };

        EngineSystems* sys{};
        std::unique_ptr<editor::EditorGui> gui;
        std::vector<PlaceableMesh> placeables;
        std::size_t selectedPlaceableIndex = 0;
        unsigned int placedMeshCount = 0;
        std::optional<GridSquare> hoveredGridSquare;
        std::optional<Vector3> snappedPlacementPosition;
        std::string lastPlacedLabel = "None";

        void createGridPickSurface() const;
        void refreshPlacementTarget();
        void refreshOverlay() const;
        void cyclePlaceable();
        void placeSelectedMesh();

        [[nodiscard]] const PlaceableMesh& selectedPlaceable() const;
        [[nodiscard]] std::string describeHoveredGrid() const;
        [[nodiscard]] std::string makePlacedLabel(const PlaceableMesh& placeable) const;

      public:
        void Update();
        void Draw3D() const;
        void Draw2D() const;

        explicit EditorScene(EngineSystems* _sys);
        ~EditorScene();
    };
} // namespace sage

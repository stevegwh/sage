#pragma once

#include "cereal/types/vector.hpp"
#include "raylib.h"

#include <vector>

namespace sage
{
    // Editor-authored height-field terrain. The grid is local to the owning
    // entity: vertex (row, col) sits at (col * cellSize, height, row * cellSize)
    // relative to the entity's world position. Rotation and scale are not
    // supported. The DynamicRenderable mesh is derived from this data and never
    // serializes; loaders rebuild it (see engine/TerrainMesh.hpp).
    struct Terrain
    {
        int resolution = 129;       // vertices per side
        float cellSize = 1.0f;      // world units between adjacent vertices
        std::vector<float> heights; // resolution * resolution, row-major

        Terrain();
        Terrain(int _resolution, float _cellSize);

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] float WorldSize() const;
        // Out-of-range indices clamp to the field edge.
        [[nodiscard]] float GetHeight(int row, int col) const;
        void SetHeight(int row, int col, float height);
        // Bilinear sample at a local x/z position, clamped to the field.
        [[nodiscard]] float SampleHeight(float localX, float localZ) const;
        [[nodiscard]] Vector3 GetNormal(int row, int col) const;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(resolution, cellSize, heights);
        }
    };
} // namespace sage

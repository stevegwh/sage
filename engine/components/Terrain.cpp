#include "Terrain.hpp"

#include "raymath.h"

#include <algorithm>

namespace sage
{
    Terrain::Terrain() : Terrain(129, 1.0f)
    {
    }

    Terrain::Terrain(const int _resolution, const float _cellSize)
        : resolution(_resolution),
          cellSize(_cellSize),
          heights(static_cast<std::size_t>(_resolution) * _resolution, 0.0f)
    {
    }

    bool Terrain::IsValid() const
    {
        return resolution >= 2 && cellSize > 0.0f &&
               heights.size() == static_cast<std::size_t>(resolution) * resolution;
    }

    float Terrain::WorldSize() const
    {
        return static_cast<float>(resolution - 1) * cellSize;
    }

    float Terrain::GetHeight(int row, int col) const
    {
        row = std::clamp(row, 0, resolution - 1);
        col = std::clamp(col, 0, resolution - 1);
        return heights[static_cast<std::size_t>(row) * resolution + col];
    }

    void Terrain::SetHeight(const int row, const int col, const float height)
    {
        if (row < 0 || row >= resolution || col < 0 || col >= resolution) return;
        heights[static_cast<std::size_t>(row) * resolution + col] = height;
    }

    float Terrain::SampleHeight(const float localX, const float localZ) const
    {
        const float maxCoord = WorldSize();
        const float x = std::clamp(localX, 0.0f, maxCoord) / cellSize;
        const float z = std::clamp(localZ, 0.0f, maxCoord) / cellSize;
        const int col = std::min(static_cast<int>(x), resolution - 2);
        const int row = std::min(static_cast<int>(z), resolution - 2);
        const float fx = x - static_cast<float>(col);
        const float fz = z - static_cast<float>(row);
        const float north = Lerp(GetHeight(row, col), GetHeight(row, col + 1), fx);
        const float south = Lerp(GetHeight(row + 1, col), GetHeight(row + 1, col + 1), fx);
        return Lerp(north, south, fz);
    }

    Vector3 Terrain::GetNormal(const int row, const int col) const
    {
        const float left = GetHeight(row, col - 1);
        const float right = GetHeight(row, col + 1);
        const float near = GetHeight(row - 1, col);
        const float far = GetHeight(row + 1, col);
        return Vector3Normalize({left - right, 2.0f * cellSize, near - far});
    }
} // namespace sage

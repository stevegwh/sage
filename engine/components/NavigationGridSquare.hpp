//
// Created by Steve Wheeler on 25/02/2024.
//

#pragma once

#include "entt/entt.hpp"
#include "raylib.h"
#include <compare>

namespace sage
{
    struct GridSquare
    {
        int row = 0;
        int col = 0;

        // Defaulted comparisons order by row then col (declaration order), matching the
        // std::tie-based operators this replaced.
        auto operator<=>(const GridSquare&) const = default;
        bool operator==(const GridSquare&) const = default;

        GridSquare operator-(const GridSquare& other) const
        {
            return {row - other.row, col - other.col};
        }

        void operator-=(const GridSquare& other)
        {
            row -= other.row;
            col -= other.col;
        }

        GridSquare operator+(const GridSquare& other) const
        {
            return {row + other.row, col + other.col};
        }

        void operator+=(const GridSquare& other)
        {
            row += other.row;
            col += other.col;
        }
    };

    class TerrainTile
    {
        float height = -1;
        Vector3 normal = Vector3{0, 1, 0};
        bool isSet = false;

      public:
        void Set(const float _height, const Vector3& _normal)
        {
            if (!isSet || height < _height)
            {
                height = _height;
                normal = _normal;
                isSet = true;
            }
        }
        [[nodiscard]] Vector3 GetNormal() const
        {
            return normal;
        }
        [[nodiscard]] float GetHeight() const
        {
            return height;
        }
    };
    struct NavigationGridSquare
    {
        TerrainTile heightMap{};
        int pathfindingCost = 1;
        bool drawDebug = false;
        Color debugColor = RED;
        GridSquare gridSquareIndex;
        Vector3 worldPosMin; // Top Left
        Vector3 worldPosMax; // Bottom Right
        Vector3 worldPosCentre;
        Vector3 debugBox;
        entt::entity occupant = entt::null;
        bool occupied = false;

        NavigationGridSquare(
            GridSquare _gridSquareIndex, Vector3 _worldPosMin, Vector3 _worldPosMax, Vector3 _worldPosCentre)
            : gridSquareIndex(_gridSquareIndex),
              worldPosMin(_worldPosMin),
              worldPosMax(_worldPosMax),
              worldPosCentre(_worldPosCentre),
              debugBox({fabsf(worldPosMax.x - worldPosMin.x), 0.1f, fabsf(worldPosMax.z - worldPosMin.z)})
        {
        }
 // Used for vector resize
        NavigationGridSquare() = default;
    };
} // namespace sage

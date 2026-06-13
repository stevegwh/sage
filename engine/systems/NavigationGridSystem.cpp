#include "NavigationGridSystem.hpp"

#include "CollisionSystem.hpp"
#include "components/MoveableActor.hpp"
#include "components/NavigationGridSquare.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "components/Terrain.hpp"
#include <Serializer.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>

Vector3 calculateGridsquareCentre(Vector3 min, Vector3 max)
{
    Vector3 size = {0};

    size.x = fabsf(max.x - min.x);
    size.y = fabsf(max.y - min.y);
    size.z = fabsf(max.z - min.z);

    return {min.x + size.x / 2.0f, min.y + size.y / 2.0f, min.z + size.z / 2.0f};
}

namespace sage
{
    namespace
    {
        bool isNavigationBlocker(const Collideable& collideable)
        {
            return collideable.blocksNavigation;
        }
    } // namespace

    inline double heuristic(GridSquare a, GridSquare b)
    {
        return std::abs(a.row - b.row) + std::abs(a.col - b.col);
    }

    inline double heuristic_favourRight(GridSquare a, GridSquare b, const Vector3& currentDir)
    {
        double dx = std::abs(a.row - b.row);
        double dy = std::abs(a.col - b.col);
        double diagonal_distance = dx + dy;

        int currentX = std::round(currentDir.x);
        int currentZ = std::round(currentDir.z);

        if (currentZ > 0)
        {
            if (a.col < b.col)
            {
                diagonal_distance += 1.0;
            }
        }
        else if (currentZ < 0)
        {
            if (a.col > b.col)
            {
                diagonal_distance += 1.0;
            }
        }
        else if (currentX > 0)
        {
            if (a.row > b.row)
            {
                diagonal_distance += 1.0;
            }
        }
        else if (currentX < 0)
        {
            if (a.row < b.row)
            {
                diagonal_distance += 1.0;
            }
        }

        return diagonal_distance;
    }

    void NavigationGridSystem::Init(int _slices, float _spacing)
    {
        slices = _slices;
        spacing = _spacing;

        int halfSlices = slices / 2;

        gridSquares.clear();
        gridSquares.resize(slices);
        for (int i = 0; i < slices; ++i)
        {
            gridSquares[i].resize(slices);
        }

        for (int j = -halfSlices; j < halfSlices; j++)
        {
            for (int i = -halfSlices; i < halfSlices; i++)
            {
                Vector3 v1 = {static_cast<float>(i) * spacing, 0, static_cast<float>(j) * spacing};
                Vector3 v3 = {static_cast<float>(i + 1) * spacing, 1.0f, static_cast<float>(j + 1) * spacing};

                GridSquare gridSquareIndex = {i + halfSlices, j + halfSlices};
                gridSquares[j + halfSlices][i + halfSlices] = {
                    gridSquareIndex, v1, v3, calculateGridsquareCentre(v1, v3)};
            }
        }
    }

    void NavigationGridSystem::DrawDebugPathfinding(const GridSquare& minRange, const GridSquare& maxRange)
    {
        // return;
        for (int i = 0; i < gridSquares.size(); i++)
        {
            for (int j = 0; j < gridSquares.at(0).size(); j++)
            {
                gridSquares[i][j].drawDebug = false;
            }
        }
        for (int i = minRange.row; i < maxRange.row; i++)
        {
            for (int j = minRange.col; j < maxRange.col; j++)
            {
                gridSquares[i][j].drawDebug = true;
            }
        }
    }

    void NavigationGridSystem::MarkSquareAreaOccupiedIfSteep(const BoundingBox& occupant, bool occupied)
    {
        GridSquare topLeftIndex{};
        GridSquare bottomRightIndex{};
        if (!WorldToGridSpace(occupant.min, topLeftIndex) || !WorldToGridSpace(occupant.max, bottomRightIndex))
        {
            return;
        }

        int min_col = std::min(topLeftIndex.col, bottomRightIndex.col);
        int max_col = std::max(topLeftIndex.col, bottomRightIndex.col);
        int min_row = std::min(topLeftIndex.row, bottomRightIndex.row);
        int max_row = std::max(topLeftIndex.row, bottomRightIndex.row);
        Vector3 up = {0.0f, 1.0f, 0.0f};

        for (int row = min_row; row <= max_row; ++row)
        {
            for (int col = min_col; col <= max_col; ++col)
            {
                auto normal = gridSquares[row][col].heightMap.GetNormal();
                // Calculate the angle between the normal and the up vector
                float dotProduct = normal.x * up.x + normal.y * up.y + normal.z * up.z;
                float angle = std::acos(dotProduct) * RAD2DEG; // Convert to degrees

                // If the angle is greater than the max slope angle, return a very high
                // cost
                if (angle > 45.0f)
                {
                    gridSquares[row][col].occupied = occupied;
                    gridSquares[row][col].drawDebug = occupied;
                }
            }
        }
    }

    void NavigationGridSystem::MarkSquareAreaOccupied(
        const BoundingBox& occupant, bool occupied, entt::entity occupantEntity)
    {
        GridSquare minRange{};
        GridSquare maxRange{};
        if (!getGridRangeForBounds(occupant, minRange, maxRange))
        {
            return;
        }

        for (int row = minRange.row; row <= maxRange.row; ++row)
        {
            for (int col = minRange.col; col <= maxRange.col; ++col)
            {
                if (!occupied && occupantEntity != entt::null && gridSquares[row][col].occupant != occupantEntity)
                {
                    continue;
                }

                gridSquares[row][col].occupied = occupied;
                gridSquares[row][col].drawDebug = occupied;
                if (occupied)
                {
                    gridSquares[row][col].occupant = occupantEntity;
                }
                else
                {
                    gridSquares[row][col].occupant = entt::null;
                }
            }
        }
    }

    void NavigationGridSystem::MarkSquaresOccupied(const std::vector<GridSquare>& squares, bool occupied)
    {
        for (const auto& square : squares)
        {
            gridSquares[square.row][square.col].occupied = occupied;
        }
    }

    void NavigationGridSystem::MarkSquaresDebug(const std::vector<GridSquare>& squares, Color color, bool occupied)
    {
        for (const auto& square : squares)
        {
            gridSquares[square.row][square.col].drawDebug = occupied;
            if (occupied)
            {
                gridSquares[square.row][square.col].debugColor = color;
            }
        }
    }

    bool NavigationGridSystem::CheckSingleSquareOccupied(Vector3 worldPos) const
    {
        GridSquare squareIndex{};
        if (!WorldToGridSpace(worldPos, squareIndex))
        {
            return false;
        }
        return CheckSingleSquareOccupied(squareIndex);
    }

    bool NavigationGridSystem::CheckSingleSquareOccupied(GridSquare position) const
    {
        return gridSquares[position.row][position.col].occupied;
    }

    /**
     * Checks whether the bounding box fits at the given world position.
     * @param worldPos
     * @param bb
     * @return
     */
    bool NavigationGridSystem::CheckBoundingBoxAreaUnoccupied(Vector3 worldPos, const BoundingBox& bb) const
    {
        const Vector3 center = {
            (bb.min.x + bb.max.x) * 0.5f,
            (bb.min.y + bb.max.y) * 0.5f,
            (bb.min.z + bb.max.z) * 0.5f};
        const Vector3 offset = Vector3Subtract(worldPos, center);
        const BoundingBox translated = {Vector3Add(bb.min, offset), Vector3Add(bb.max, offset)};

        GridSquare minRange{};
        GridSquare maxRange{};
        if (!getGridRangeForBounds(translated, minRange, maxRange))
        {
            return false;
        }

        for (int row = minRange.row; row <= maxRange.row; ++row)
        {
            for (int col = minRange.col; col <= maxRange.col; ++col)
            {
                if (gridSquares[row][col].occupied) return false;
            }
        }
        return true;
    }

    bool NavigationGridSystem::CheckBoundingBoxAreaUnoccupied(GridSquare square, const BoundingBox& bb) const
    {
        GridSquare extents{};
        {
            GridSquare bb_min{};
            if (!WorldToGridSpace(bb.min, bb_min) || !WorldToGridSpace(bb.max, extents))
            {
                return false;
            }

            extents -= bb_min;
        }
        return checkExtents(square, extents);
    }

    bool NavigationGridSystem::CheckEntityAreaUnoccupied(const entt::entity entity, const Vector3 worldPos) const
    {
        GridSquare square{};
        BoundingBox footprintOffsets{};
        return WorldToGridSpace(worldPos, square) && getFootprintOffsets(entity, footprintOffsets) &&
               checkFootprint(square, footprintOffsets);
    }

    entt::entity NavigationGridSystem::CheckSingleSquareOccupant(Vector3 worldPos) const
    {
        GridSquare squareIndex{};
        if (!WorldToGridSpace(worldPos, squareIndex))
        {
            return entt::null;
        }
        return CheckSingleSquareOccupant(squareIndex);
    }

    entt::entity NavigationGridSystem::CheckSingleSquareOccupant(GridSquare position) const
    {
        return gridSquares[position.row][position.col].occupant;
    }

    entt::entity NavigationGridSystem::CheckSquareAreaOccupant(Vector3 worldPos, const BoundingBox& bb) const
    {
        GridSquare gridPos{};
        {
            if (!WorldToGridSpace(worldPos, gridPos))
            {
                return entt::null;
            }
        }
        return CheckSquareAreaOccupant(gridPos, bb);
    }

    entt::entity NavigationGridSystem::CheckSquareAreaOccupant(GridSquare square, const BoundingBox& bb) const
    {
        GridSquare extents{};
        {
            GridSquare bb_min{};
            WorldToGridSpace(bb.min, bb_min);
            WorldToGridSpace(bb.max, extents);
            extents -= bb_min;
        }

        if (!checkExtents(square, extents))
        {
            return entt::null;
        }

        if (gridSquares[square.row - extents.row][square.col - extents.col].occupied)
        {
            return gridSquares[square.row - extents.row][square.col - extents.col].occupant;
        }
        if (gridSquares[square.row + extents.row][square.col + extents.col].occupied)
        {
            return gridSquares[square.row + extents.row][square.col + extents.col].occupant;
        }
        if (gridSquares[square.row - extents.row][square.col + extents.col].occupied)
        {
            return gridSquares[square.row - extents.row][square.col + extents.col].occupant;
        }
        if (gridSquares[square.row + extents.row][square.col - extents.col].occupied)
        {
            return gridSquares[square.row + extents.row][square.col - extents.col].occupant;
        }
        return entt::null;
    }

    bool NavigationGridSystem::IsValidMove(Vector3 point, entt::entity actor) const
    {
        if (CheckWithinGridBounds(point))
        {
            const auto& moveable = registry->get<MoveableActor>(actor);
            GridSquare minRange{};
            GridSquare maxRange{};
            GetPathfindRange(actor, moveable.pathfindingBounds, minRange, maxRange);

            if (!CheckWithinBounds(point, minRange, maxRange))
            {
                // Out of player's movement range
                return false;
            }
        }
        else
        {
            return false;
        }
        GridSquare dest{};
        WorldToGridSpace(point, dest);
        if (GetGridSquare(dest.row, dest.col)->occupied)
        {
            return false;
        }
        return true;
    }

    bool NavigationGridSystem::CompareSquareAreaOccupant(entt::entity entity, const BoundingBox& bb) const
    {
        return false;
    }

    bool NavigationGridSystem::CompareSingleSquareOccupant(entt::entity entity, const BoundingBox& bb) const
    {
        return false;
    }

    float calculateTerrainCost(const Vector3& normal, float maxSlopeAngle)
    {
        // The up vector
        Vector3 up = {0.0f, 1.0f, 0.0f};

        // Calculate the angle between the normal and the up vector
        float dotProduct = normal.x * up.x + normal.y * up.y + normal.z * up.z;
        float angle = std::acos(dotProduct) * RAD2DEG; // Convert to degrees

        // If the angle is greater than the max slope angle, return a very high cost
        if (angle > maxSlopeAngle)
        {
            return std::numeric_limits<float>::max();
        }

        // Otherwise, calculate a cost based on the angle
        // This will return 1.0 for flat ground, and increase as the slope increases
        return 1.0f + (angle / maxSlopeAngle);
    }

    void NavigationGridSystem::calculateHeightAndNormalsFromTerrain(const entt::entity& entity)
    {
        const auto& terrain = registry->get<Terrain>(entity);
        if (!terrain.IsValid()) return;

        const auto origin = registry->get<sgTransform>(entity).GetWorldPos();
        const float worldSize = terrain.WorldSize();

        // WorldToGridSpace fills the indices even for off-grid points, so a
        // terrain that sticks out past the grid still stamps the squares the
        // grid does cover — the clamps below trim its footprint.
        GridSquare topLeftIndex{}, bottomRightIndex{};
        const bool minInside = WorldToGridSpace(origin, topLeftIndex);
        const bool maxInside =
            WorldToGridSpace({origin.x + worldSize, origin.y, origin.z + worldSize}, bottomRightIndex);
        if (!minInside || !maxInside)
        {
            std::cout << "WARNING: Terrain at (" << origin.x << ", " << origin.z << ") size " << worldSize
                      << " extends beyond the navigation grid; heights outside the grid are unwalkable.\n";
        }

        const int min_col = std::max(0, std::min(topLeftIndex.col, bottomRightIndex.col));
        const int max_col = std::min(
            static_cast<int>(gridSquares[0].size()) - 1, std::max(topLeftIndex.col, bottomRightIndex.col));
        const int min_row = std::max(0, std::min(topLeftIndex.row, bottomRightIndex.row));
        const int max_row =
            std::min(static_cast<int>(gridSquares.size()) - 1, std::max(topLeftIndex.row, bottomRightIndex.row));
        if (min_col > max_col || min_row > max_row) return; // entirely off-grid

        for (int row = min_row; row <= max_row; ++row)
        {
            for (int col = min_col; col <= max_col; ++col)
            {
                const float localX = gridSquares[row][col].worldPosMin.x - origin.x;
                const float localZ = gridSquares[row][col].worldPosMin.z - origin.z;
                if (localX < 0.0f || localX > worldSize || localZ < 0.0f || localZ > worldSize) continue;

                const int terrainRow = static_cast<int>(std::lround(localZ / terrain.cellSize));
                const int terrainCol = static_cast<int>(std::lround(localX / terrain.cellSize));
                gridSquares[row][col].heightMap.Set(
                    origin.y + terrain.SampleHeight(localX, localZ), terrain.GetNormal(terrainRow, terrainCol));
            }
        }

        std::cout << "Stamped terrain heights into nav grid: rows " << min_row << ".." << max_row << ", cols "
                  << min_col << ".." << max_col << "\n";
    }

    void NavigationGridSystem::calculateTerrainHeightAndNormals(const entt::entity& entity)
    {
        const auto& area = registry->get<Collideable>(entity).worldBoundingBox;

        // Same clamping rationale as calculateHeightAndNormalsFromTerrain:
        // geometry partially outside the grid still stamps the covered squares.
        GridSquare topLeftIndex{}, bottomRightIndex{};
        const bool minInside = WorldToGridSpace(area.min, topLeftIndex);
        const bool maxInside = WorldToGridSpace(area.max, bottomRightIndex);
        if (!minInside && !maxInside &&
            (std::max(topLeftIndex.col, bottomRightIndex.col) < 0 ||
             std::max(topLeftIndex.row, bottomRightIndex.row) < 0 ||
             std::min(topLeftIndex.col, bottomRightIndex.col) >= static_cast<int>(gridSquares[0].size()) ||
             std::min(topLeftIndex.row, bottomRightIndex.row) >= static_cast<int>(gridSquares.size())))
        {
            return; // entirely off-grid
        }

        const auto& renderable = registry->get<Renderable>(entity);
        const auto& transform = registry->get<sgTransform>(entity);
        const auto& collideable = registry->get<Collideable>(entity);

        const int min_col = std::max(0, std::min(topLeftIndex.col, bottomRightIndex.col));
        const int max_col = std::min(
            static_cast<int>(gridSquares[0].size()) - 1, std::max(topLeftIndex.col, bottomRightIndex.col));
        const int min_row = std::max(0, std::min(topLeftIndex.row, bottomRightIndex.row));
        const int max_row =
            std::min(static_cast<int>(gridSquares.size()) - 1, std::max(topLeftIndex.row, bottomRightIndex.row));

        for (int row = min_row; row <= max_row; ++row)
        {
            for (int col = min_col; col <= max_col; ++col)
            {
                if (collideable.collisionLayer == collision_layers::Stairs)
                {
                    float relativeX =
                        (gridSquares[row][col].worldPosMin.x - area.min.x) / (area.max.x - area.min.x);
                    float relativeZ =
                        (gridSquares[row][col].worldPosMin.z - area.min.z) / (area.max.z - area.min.z);
                    Vector3 stairDirection = Vector3Normalize(Vector3Subtract(area.max, area.min));
                    float relativePosition = relativeX * stairDirection.x + relativeZ * stairDirection.z;
                    float interpolatedHeight = area.min.y + (area.max.y - area.min.y) * relativePosition;
                    gridSquares[row][col].heightMap.Set(
                        interpolatedHeight, Vector3Normalize(Vector3{-stairDirection.x, 1, -stairDirection.z}));
                }
                else if (collideable.collisionLayer == collision_layers::GeometrySimple)
                {
                    gridSquares[row][col].heightMap.Set(area.max.y, {0, 1, 0});
                }
                else if (collideable.collisionLayer == collision_layers::GeometryComplex)
                {
                    Vector3 gridCenter = {
                        (gridSquares[row][col].worldPosMin.x + gridSquares[row][col].worldPosMax.x) * 0.5f,
                        area.max.y + 1.0f, // Start slightly above the terrain
                        (gridSquares[row][col].worldPosMin.z + gridSquares[row][col].worldPosMax.z) * 0.5f};

                    Ray ray = {gridCenter, {0, -1, 0}}; // Cast ray down

                    RayCollision getFirstCollision =
                        renderable.GetModel()->GetRayMeshCollision(ray, 0, transform.GetMatrix());

                    if (getFirstCollision.hit)
                    {
                        gridSquares[row][col].heightMap.Set(getFirstCollision.point.y, getFirstCollision.normal);
                    }
                }
            }
        }
    }
    /**
     * Checks a position in the world for an occupant. If an occupant is found, the
     * extents of the occupant are returned.
     * @param worldPos The position in the world to check for an occupant.
     * @param extents The extents of the occupant.
     * @return Whether an occupant was found
     */
    bool NavigationGridSystem::getExtents(Vector3 worldPos, GridSquare& extents) const
    {
        GridSquare gridPos{};
        if (!WorldToGridSpace(worldPos, gridPos))
        {
            return false;
        }
        const auto entity = CheckSingleSquareOccupant(worldPos);
        if (entity == entt::null)
        {
            return false;
        }

        if (!getExtents(entity, extents))
        {
            return false;
        }

        return true;
    }

    /**
     * Takes an entity and returns the extents of the entity in grid space.
     * @param entity The entity to get the extents of.
     * @param extents The extents of the entity.
     * @return Whether the extents were successfully retrieved.
     */
    bool NavigationGridSystem::getExtents(const entt::entity entity, GridSquare& extents) const
    {
        GridSquare bb_min{};
        auto& bb = registry->get<Collideable>(entity).localBoundingBox;
        if (!WorldToGridSpace(bb.min, bb_min) || !WorldToGridSpace(bb.max, extents))
        {
            return false;
        }

        extents -= bb_min;

        if (!CheckWithinGridBounds(extents))
        {
            return false;
        }

        return true;
    }

    bool NavigationGridSystem::getGridRangeForBounds(
        const BoundingBox& bounds, GridSquare& minRange, GridSquare& maxRange) const
    {
        if (gridSquares.empty() || gridSquares.front().empty()) return false;

        const float minX = std::min(bounds.min.x, bounds.max.x);
        const float minZ = std::min(bounds.min.z, bounds.max.z);
        const float maxX = std::max(bounds.min.x, bounds.max.x);
        const float maxZ = std::max(bounds.min.z, bounds.max.z);

        // Treat max edges as exclusive so a box ending exactly on a grid line
        // does not occupy the cell on the other side of that line.
        const Vector3 minPoint{minX, 0.0f, minZ};
        const Vector3 maxPoint{
            std::nextafter(maxX, minX),
            0.0f,
            std::nextafter(maxZ, minZ)};

        GridSquare minIndex{};
        GridSquare maxIndex{};
        WorldToGridSpace(minPoint, minIndex);
        WorldToGridSpace(maxPoint, maxIndex);

        const int rawMinCol = std::min(minIndex.col, maxIndex.col);
        const int rawMaxCol = std::max(minIndex.col, maxIndex.col);
        const int rawMinRow = std::min(minIndex.row, maxIndex.row);
        const int rawMaxRow = std::max(minIndex.row, maxIndex.row);

        const int gridWidth = static_cast<int>(gridSquares.front().size());
        const int gridHeight = static_cast<int>(gridSquares.size());
        if (rawMaxCol < 0 || rawMaxRow < 0 || rawMinCol >= gridWidth || rawMinRow >= gridHeight)
        {
            return false;
        }

        minRange = {std::max(0, rawMinRow), std::max(0, rawMinCol)};
        maxRange = {std::min(gridHeight - 1, rawMaxRow), std::min(gridWidth - 1, rawMaxCol)};
        return minRange.row <= maxRange.row && minRange.col <= maxRange.col;
    }

    bool NavigationGridSystem::getFootprintOffsets(const entt::entity entity, BoundingBox& offsets) const
    {
        if (!registry->valid(entity) || !registry->all_of<Collideable, sgTransform>(entity))
        {
            return false;
        }

        const auto& collideable = registry->get<Collideable>(entity);
        const auto& transform = registry->get<sgTransform>(entity);
        const Vector3 origin = transform.GetWorldPos();
        offsets = {
            Vector3Subtract(collideable.worldBoundingBox.min, origin),
            Vector3Subtract(collideable.worldBoundingBox.max, origin)};
        return true;
    }

    bool NavigationGridSystem::GetPathfindRange(
        const entt::entity& actorId, int bounds, GridSquare& minRange, GridSquare& maxRange) const
    {
        auto bb = registry->get<Collideable>(actorId).worldBoundingBox;
        return GetGridRange(bb, bounds, minRange, maxRange);
    }

    bool NavigationGridSystem::GetGridRange(
        BoundingBox bb, int bounds, GridSquare& minRange, GridSquare& maxRange) const
    {
        Vector3 center = {
            (bb.min.x + bb.max.x) / 2.0f, (bb.min.y + bb.max.y) / 2.0f, (bb.min.z + bb.max.z) / 2.0f};
        return GetGridRange(center, bounds, minRange, maxRange);
    }

    bool NavigationGridSystem::GetGridRange(
        Vector3 center, int bounds, GridSquare& minRange, GridSquare& maxRange) const
    {
        if (!CheckWithinGridBounds(center))
        {
            return false;
        }

        Vector3 topLeft = {center.x - bounds * spacing, center.y, center.z - bounds * spacing};
        Vector3 bottomRight = {center.x + bounds * spacing, center.y, center.z + bounds * spacing};

        GridSquare topLeftIndex{};
        GridSquare bottomRightIndex{};

        WorldToGridSpace(topLeft, topLeftIndex);
        WorldToGridSpace(bottomRight, bottomRightIndex);

        // Clamp to grid
        topLeftIndex.col = std::max(topLeftIndex.col, 0);
        topLeftIndex.row = std::max(topLeftIndex.row, 0);
        bottomRightIndex.col = std::min(bottomRightIndex.col, static_cast<int>(gridSquares.at(0).size() - 1));
        bottomRightIndex.row = std::min(bottomRightIndex.row, static_cast<int>(gridSquares.size() - 1));

        minRange = {topLeftIndex.row, topLeftIndex.col};
        maxRange = {bottomRightIndex.row, bottomRightIndex.col};

        return true;
    }

    bool NavigationGridSystem::GridToWorldSpace(GridSquare gridPos, Vector3& out) const
    {
        GridSquare maxRange = {static_cast<int>(gridSquares.at(0).size()), static_cast<int>(gridSquares.size())};
        if (!CheckWithinBounds(gridPos, {0, 0}, maxRange))
        {
            return false;
        }
        out = gridSquares[gridPos.row][gridPos.col].worldPosCentre;
        out.y = gridSquares[gridPos.row][gridPos.col].heightMap.GetHeight();
        return true;
    }

    bool NavigationGridSystem::WorldToGridSpace(Vector3 worldPos, GridSquare& out) const
    {
        return WorldToGridSpace(
            worldPos,
            out,
            {0, 0},
            {static_cast<int>(gridSquares.at(0).size()), static_cast<int>(gridSquares.size())});
    }

    bool NavigationGridSystem::WorldToGridSpace(
        Vector3 worldPos, GridSquare& out, const GridSquare& minRange, const GridSquare& maxRange) const
    {
        int x = std::floor(worldPos.x / spacing) + (slices / 2);
        int y = std::floor(worldPos.z / spacing) + (slices / 2);
        out = {y, x};

        return out.row < maxRange.row && out.col < maxRange.col && out.col >= minRange.col &&
               out.row >= minRange.row;
    }

    void NavigationGridSystem::DrawDebug() const
    {
        return;
        for (const auto& gridSquareRow : gridSquares)
        {
            for (const auto& gridSquare : gridSquareRow)
            {
                if (!gridSquare.drawDebug) continue;
                DrawCubeWires(
                    gridSquare.worldPosCentre,
                    gridSquare.debugBox.x,
                    gridSquare.debugBox.y,
                    gridSquare.debugBox.z,
                    gridSquare.debugColor);
            }
        }
    }

    std::vector<Vector3> NavigationGridSystem::tracebackPath(
        const std::vector<std::vector<GridSquare>>& came_from, const GridSquare& start, const GridSquare& finish)
    {
        auto combineWorldPosTerrainHeight = [this](auto gridPos) {
            Vector3 worldPos = gridSquares[gridPos.row][gridPos.col].worldPosCentre;
            worldPos.y = gridSquares[gridPos.row][gridPos.col].heightMap.GetHeight();
            return worldPos;
        };
        std::vector<Vector3> path;
        GridSquare current = {finish.row, finish.col};
        GridSquare previous{};
        std::pair<int, int> currentDir = {0, 0};

        path.push_back(combineWorldPosTerrainHeight(current));
        while (current.row != start.row || current.col != start.col)
        {
            previous = current;
            current = came_from[current.row][current.col];
            for (const auto& dir : directions)
            {
                int row = previous.row + dir.first;
                int col = previous.col + dir.second;
                if (row == current.row && col == current.col)
                {
                    if (currentDir.first == 0 && currentDir.second == 0)
                    {
                        currentDir = dir;
                        break;
                    }
                    if (dir != currentDir)
                    {
                        currentDir = dir;
                        path.push_back(combineWorldPosTerrainHeight(previous));
                        path.push_back(combineWorldPosTerrainHeight(current));
                    }
                    break;
                }
            }
        }
        // Commented out the first node to stop "stuttering" bug when holding left click
        // path.push_back(combineWorldPosTerrainHeight(current));
        std::ranges::reverse(path);
        return path;
    }

    bool NavigationGridSystem::CheckWithinGridBounds(Vector3 worldPos) const
    {
        GridSquare tmp{};
        return WorldToGridSpace(worldPos, tmp);
    }

    bool NavigationGridSystem::CheckWithinGridBounds(GridSquare square) const
    {
        return CheckWithinBounds(
            square,
            GridSquare{0, 0},
            GridSquare{static_cast<int>(gridSquares.at(0).size()), static_cast<int>(gridSquares.size())});
    }

    bool NavigationGridSystem::CheckWithinBounds(Vector3 worldPos, GridSquare minRange, GridSquare maxRange) const
    {
        GridSquare tmp{};
        return WorldToGridSpace(worldPos, tmp, minRange, maxRange);
    }

    bool NavigationGridSystem::CheckWithinBounds(GridSquare square, GridSquare minRange, GridSquare maxRange)
    {
        return minRange.row <= square.row && square.row < maxRange.row && minRange.col <= square.col &&
               square.col < maxRange.col;
    }

    bool NavigationGridSystem::checkExtents(const GridSquare square, const GridSquare extents) const
    {
        const auto min = square - extents;
        const auto max = square + extents;

        for (int row = min.row; row < max.row; ++row)
        {
            for (int col = min.col; col < max.col; ++col)
            {
                if (!CheckWithinGridBounds(GridSquare{row, col}) || gridSquares[row][col].occupied)
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool NavigationGridSystem::checkFootprint(const GridSquare square, const BoundingBox& footprintOffsets) const
    {
        if (!CheckWithinGridBounds(square)) return false;

        const Vector3 origin = gridSquares[square.row][square.col].worldPosCentre;
        const BoundingBox footprint = {
            Vector3Add(origin, footprintOffsets.min),
            Vector3Add(origin, footprintOffsets.max)};

        GridSquare minRange{};
        GridSquare maxRange{};
        if (!getGridRangeForBounds(footprint, minRange, maxRange))
        {
            return false;
        }

        for (int row = minRange.row; row <= maxRange.row; ++row)
        {
            for (int col = minRange.col; col <= maxRange.col; ++col)
            {
                if (gridSquares[row][col].occupied)
                {
                    return false;
                }
            }
        }

        return true;
    }

    NavigationGridSquare* NavigationGridSystem::CastRay(
        int currentRow, int currentCol, Vector2 direction, float distance, std::vector<GridSquare>& debugLines)
    {
        int dist = std::round(distance);
        direction = Vector2Normalize(direction);
        int dirRow = std::round(direction.y);
        int dirCol = std::round(direction.x);

        for (int i = 0; i < dist; ++i)
        {
            GridSquare square = {currentRow + (dirRow * i), currentCol + (dirCol * i)};
            debugLines.push_back(square);

            if (!CheckWithinGridBounds(square))
            {
                continue;
            }

            auto& cell = gridSquares[square.row][square.col];
            cell.drawDebug = true;
            cell.debugColor = PURPLE;

            if (cell.occupant != entt::null)
            {
                return &cell;
            }
        }
        return nullptr;
    }

    GridSquare NavigationGridSystem::FindNextBestLocation(entt::entity entity, GridSquare target) const
    {
        BoundingBox footprintOffsets{};
        if (!getFootprintOffsets(entity, footprintOffsets))
        {
            return {};
        }
        GridSquare minRange{}, maxRange{};
        int bounds = 250;
        if (!GetPathfindRange(entity, bounds, minRange, maxRange))
        {
            return {};
        }
        GridSquare currentPos{};
        if (const auto& trans = registry->get<sgTransform>(entity);
            !WorldToGridSpace(trans.GetWorldPos(), currentPos))
        {
            return {};
        }

        return FindNextBestLocation(currentPos, target, minRange, maxRange, footprintOffsets);
    }

    GridSquare NavigationGridSystem::FindNextBestLocation(
        const GridSquare currentPos,
        GridSquare target,
        const GridSquare minRange,
        const GridSquare maxRange,
        const BoundingBox& footprintOffsets) const
    {
        struct Compare
        {
            bool operator()(const std::pair<int, GridSquare>& a, const std::pair<int, GridSquare>& b) const
            {
                return a.first > b.first;
            }
        };
        std::vector<std::vector<bool>> visited(maxRange.row, std::vector<bool>(maxRange.col, false));
        std::priority_queue<std::pair<int, GridSquare>, std::vector<std::pair<int, GridSquare>>, Compare> frontier;

        frontier.emplace(0, currentPos);

        GridSquare bestSquare{};
        int bestScore = std::numeric_limits<int>::max();

        while (!frontier.empty())
        {
            auto currentPair = frontier.top();
            frontier.pop();
            auto current = currentPair.second;

            // Check if this is a valid and better square
            if (checkFootprint(current, footprintOffsets))
            {
                int score = heuristic(current, target);
                if (score < bestScore)
                {
                    bestScore = score;
                    bestSquare = current;
                    if (current == target) break; // Found the exact target
                }
            }

            for (const auto& dir : directions)
            {
                GridSquare next = {current.row + dir.second, current.col + dir.first};

                if (!CheckWithinBounds(next, minRange, maxRange) || visited[next.row][next.col]) continue;

                visited[next.row][next.col] = true;
                int priority = heuristic(next, target) + heuristic(currentPos, next); // f = g + h
                frontier.emplace(priority, next);
            }
        }

        return bestSquare;
    }

    std::vector<Vector3> NavigationGridSystem::AStarPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const AStarHeuristic heuristicType)
    {
        return AStarPathfind(
            entity,
            startPos,
            finishPos,
            {0, 0},
            {static_cast<int>(gridSquares.at(0).size()), static_cast<int>(gridSquares.size())},
            heuristicType);
    }

    std::vector<Vector3> NavigationGridSystem::AStarPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const GridSquare& minRange,
        const GridSquare& maxRange,
        AStarHeuristic heuristicType)
    {
        GridSquare startGridSquare{};
        GridSquare finishGridSquare{};
        BoundingBox footprintOffsets{};

        if (!WorldToGridSpace(startPos, startGridSquare) || !WorldToGridSpace(finishPos, finishGridSquare) ||
            !getFootprintOffsets(entity, footprintOffsets))
            return {};

        if (!checkFootprint(finishGridSquare, footprintOffsets))
        {
            // TODO: Should try to find next best location to "original" destination
            finishGridSquare =
                FindNextBestLocation(startGridSquare, finishGridSquare, minRange, maxRange, footprintOffsets);
        }

        struct Compare
        {
            bool operator()(const std::pair<int, GridSquare>& a, const std::pair<int, GridSquare>& b) const
            {
                return a.first > b.first;
            }
        };

        std::vector<std::vector<bool>> visited(maxRange.row, std::vector<bool>(maxRange.col, false));
        std::vector<std::vector<GridSquare>> came_from(
            maxRange.row, std::vector<GridSquare>(maxRange.col, {-1, -1}));
        std::vector<std::vector<double>> cost_so_far(maxRange.row, std::vector<double>(maxRange.col, 0.0));

        std::priority_queue<std::pair<int, GridSquare>, std::vector<std::pair<int, GridSquare>>, Compare> frontier;

        frontier.emplace(0, startGridSquare);
        visited[startGridSquare.row][startGridSquare.col] = true;

        bool pathFound = false;

        while (!frontier.empty())
        {
            const auto currentPair = frontier.top();
            frontier.pop();
            const auto current = currentPair.second;

            if (current.row == finishGridSquare.row && current.col == finishGridSquare.col)
            {
                pathFound = true;
                break;
            }

            for (const auto& [dirX, dirY] : directions)
            {
                GridSquare next = {current.row + dirX, current.col + dirY};

                const auto current_cost = gridSquares[current.row][current.col].pathfindingCost;
                const auto next_cost = gridSquares[next.row][next.col].pathfindingCost;
                const double new_cost = current_cost + next_cost;

                if (CheckWithinBounds(next, minRange, maxRange) && checkFootprint(next, footprintOffsets) &&
                    (!visited[next.row][next.col] ||
                     (visited[next.row][next.col] && new_cost < cost_so_far[next.row][next.col])) &&
                    !gridSquares.at(next.row).at(next.col).occupied)
                {
                    cost_so_far[next.row][next.col] = new_cost;
                    const double heuristic_cost = heuristic(next, finishGridSquare);
                    const double priority = new_cost + heuristic_cost;
                    frontier.emplace(priority, next);
                    came_from[next.row][next.col] = current;
                    visited[next.row][next.col] = true;
                }
            }
        }

        if (!pathFound)
        {
            return {};
        }

        return tracebackPath(came_from, startGridSquare, finishGridSquare);
    }

    /**
     * Generates a sequence of nodes that should be the "optimal" route from point A to
     * point B. Checks entire grid.
     * @return A vector of "nodes" to travel to in sequential order. Empty if path is
     * invalid (OOB or no path available).
     */
    std::vector<Vector3> NavigationGridSystem::BFSPathfind(
        const entt::entity& entity, const Vector3& startPos, const Vector3& finishPos)
    {
        return BFSPathfind(
            entity,
            startPos,
            finishPos,
            {0, 0},
            {static_cast<int>(gridSquares.at(0).size()), static_cast<int>(gridSquares.size())});
    }

    /**
     * Generates a sequence of nodes that should be the "optimal" route from point A to
     * point B. Checks path within a range. Use "GetPathfindRange" to calculate
     * minRange/maxRange if needed.
     * @minRange The minimum grid index in the pathfinding range.
     * @maxRange The maximum grid index in the pathfinding range.
     * @return A vector of "nodes" to travel to in sequential order. Empty if path is
     * invalid (OOB or no path available).
     */
    std::vector<Vector3> NavigationGridSystem::BFSPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const GridSquare& minRange,
        const GridSquare& maxRange)
    {
        GridSquare start{};
        GridSquare finish{};
        BoundingBox footprintOffsets{};
        if (!WorldToGridSpace(startPos, start) || !WorldToGridSpace(finishPos, finish) ||
            !getFootprintOffsets(entity, footprintOffsets))
            return {};

        if (!checkFootprint(finish, footprintOffsets))
        {
            // TODO: Should actually try to find next best location to original
            // destination
            finish = FindNextBestLocation(start, finish, minRange, maxRange, footprintOffsets);
        }

        std::vector<std::vector<bool>> visited(maxRange.row, std::vector<bool>(maxRange.col, false));
        std::vector<std::vector<GridSquare>> came_from(
            maxRange.row, std::vector<GridSquare>(maxRange.col, {-1, -1}));

        std::queue<GridSquare> frontier;

        frontier.emplace(start);
        visited[start.row][start.col] = true;

        bool pathFound = false;

        while (!frontier.empty())
        {
            const auto current = frontier.front();
            frontier.pop();

            if (current.row == finish.row && current.col == finish.col)
            {
                pathFound = true;
                break;
            }

            for (const auto& [dirX, dirY] : directions)
            {
                if (GridSquare next = {current.row + dirX, current.col + dirY};
                    CheckWithinBounds(next, minRange, maxRange) && !visited[next.row][next.col] &&
                    checkFootprint(next, footprintOffsets) && !gridSquares[next.row][next.col].occupied)
                {
                    frontier.emplace(next);
                    visited[next.row][next.col] = true;
                    came_from[next.row][next.col] = current;
                }
            }
        }

        if (!pathFound)
        {
            return {};
        }

        return tracebackPath(came_from, start, finish);
    }

    void NavigationGridSystem::InitGridHeightAndNormals()
    {
        std::cout << "START: Initialising grid height and normals \n";
        const auto& view = registry->view<Collideable, Renderable>();
        for (const auto& entity : view)
        {
            const auto& bb = view.get<Collideable>(entity);

            if (IsNavigationLayer(bb.collisionLayer))
            {
                calculateTerrainHeightAndNormals(entity);
            }
        }
        // Height-field terrains sample exactly (no raycasts). Pass order is
        // irrelevant: TerrainTile::Set keeps the highest height per square, so
        // wherever terrain overlaps other walkable geometry the taller surface
        // wins — which also means a terrain valley dug below an overlapping
        // floor mesh will not register in the grid.
        for (const auto& entity : registry->view<Terrain, sgTransform>())
        {
            calculateHeightAndNormalsFromTerrain(entity);
        }
        for (const auto& entity : registry->view<Collideable>(entt::exclude<MoveableActor>))
        {
            const auto& collideable = registry->get<Collideable>(entity);
            if (isNavigationBlocker(collideable))
            {
                MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);
            }
        }
        std::cout << "FINISH: Initialising grid height and normals \n";
    }

    const std::vector<std::vector<NavigationGridSquare>>& NavigationGridSystem::GetGridSquares()
    {
        return gridSquares;
    }

    const NavigationGridSquare* NavigationGridSystem::GetGridSquare(int row, int col) const
    {
        return &gridSquares[row][col];
    }

    NavigationGridSystem::NavigationGridSystem(entt::registry* _registry, CollisionSystem* _collisionSystem)
        : registry(_registry), collisionSystem(_collisionSystem)
    {
    }
} // namespace sage

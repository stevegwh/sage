#include "NavigationGridSystem.hpp"
#include "engine/Colors.hpp"
#include "engine/MathConstants.hpp"

#include "CollisionSystem.hpp"
#include "components/CollisionIntent.hpp"
#include "components/DynamicRenderable.hpp"
#include "components/MoveableActor.hpp"
#include "components/NavigationGridSquare.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "components/Terrain.hpp"
#include "TerrainMesh.hpp"
#include <Serializer.hpp>

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <utility>

namespace sage
{
    static constexpr float MAX_WALKABLE_SLOPE_DEGREES = 45.0f;

    static Vector3 calculateGridsquareCentre(Vector3 min, Vector3 max)
    {
        return {.x = (min.x + max.x) * 0.5f, .y = (min.y + max.y) * 0.5f, .z = (min.z + max.z) * 0.5f};
    }

    // Angle in degrees between a surface normal and straight up.
    static float slopeAngleDegrees(const Vector3& normal)
    {
        const Vector3 up = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
        const float dotProduct = normal.x * up.x + normal.y * up.y + normal.z * up.z;
        return std::acos(dotProduct) * sage::math::RADIANS_TO_DEGREES;
    }

    inline double heuristic(GridSquare a, GridSquare b)
    {
        return std::abs(a.row - b.row) + std::abs(a.col - b.col);
    }

    inline double heuristic_favourRight(GridSquare a, GridSquare b, const Vector3& currentDir)
    {
        double dx = std::abs(a.row - b.row);
        double dy = std::abs(a.col - b.col);
        double diagonal_distance = dx + dy;

        int currentX = static_cast<int>(std::round(currentDir.x));
        int currentZ = static_cast<int>(std::round(currentDir.z));

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
            gridSquares.at(i).resize(slices);
        }

        for (int j = -halfSlices; j < halfSlices; j++)
        {
            for (int i = -halfSlices; i < halfSlices; i++)
            {
                Vector3 v1 = {.x = static_cast<float>(i) * spacing, .y = 0, .z = static_cast<float>(j) * spacing};
                Vector3 v3 = {
                    .x = static_cast<float>(i + 1) * spacing, .y = 1.0f, .z = static_cast<float>(j + 1) * spacing};

                GridSquare gridSquareIndex = {.row = i + halfSlices, .col = j + halfSlices};
                gridSquares.at(j + halfSlices).at(i + halfSlices) = {
                    gridSquareIndex, v1, v3, calculateGridsquareCentre(v1, v3)};
            }
        }
    }

    void NavigationGridSystem::DrawDebugPathfinding(const GridSquare& minRange, const GridSquare& maxRange)
    {
        if (hasDebugRange)
        {
            for (int row = debugRangeMin.row; row < debugRangeMax.row; ++row)
            {
                for (int col = debugRangeMin.col; col < debugRangeMax.col; ++col)
                    gridSquares.at(row).at(col).drawDebug = false;
            }
        }
        for (int row = minRange.row; row < maxRange.row; ++row)
        {
            for (int col = minRange.col; col < maxRange.col; ++col)
                gridSquares.at(row).at(col).drawDebug = true;
        }
        debugRangeMin = minRange;
        debugRangeMax = maxRange;
        hasDebugRange = true;
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

        for (int row = min_row; row <= max_row; ++row)
        {
            for (int col = min_col; col <= max_col; ++col)
            {
                const auto normal = gridSquares.at(row).at(col).heightMap.GetNormal();
                if (slopeAngleDegrees(normal) > MAX_WALKABLE_SLOPE_DEGREES)
                {
                    gridSquares.at(row).at(col).occupied = occupied;
                    gridSquares.at(row).at(col).drawDebug = occupied;
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
                if (!occupied && occupantEntity != entt::null &&
                    gridSquares.at(row).at(col).occupant != occupantEntity)
                {
                    continue;
                }

                gridSquares.at(row).at(col).occupied = occupied;
                gridSquares.at(row).at(col).drawDebug = occupied;
                if (occupied)
                {
                    gridSquares.at(row).at(col).occupant = occupantEntity;
                }
                else
                {
                    gridSquares.at(row).at(col).occupant = entt::null;
                }
            }
        }
    }

    void NavigationGridSystem::MarkSquaresOccupied(const std::vector<GridSquare>& squares, bool occupied)
    {
        for (const auto& square : squares)
        {
            gridSquares.at(square.row).at(square.col).occupied = occupied;
        }
    }

    void NavigationGridSystem::MarkSquaresDebug(const std::vector<GridSquare>& squares, Color color, bool occupied)
    {
        for (const auto& square : squares)
        {
            gridSquares.at(square.row).at(square.col).drawDebug = occupied;
            if (occupied)
            {
                gridSquares.at(square.row).at(square.col).debugColor = color;
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
        return gridSquares.at(position.row).at(position.col).occupied;
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
            .x = (bb.min.x + bb.max.x) * 0.5f,
            .y = (bb.min.y + bb.max.y) * 0.5f,
            .z = (bb.min.z + bb.max.z) * 0.5f};
        const Vector3 offset = Vector3Subtract(worldPos, center);
        const BoundingBox translated = {.min = Vector3Add(bb.min, offset), .max = Vector3Add(bb.max, offset)};

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
                if (gridSquares.at(row).at(col).occupied) return false;
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

    bool NavigationGridSystem::CheckEntityAreaUnoccupied(
        const entt::entity entity, const Vector3 worldPos, const bool ignoreActors) const
    {
        BoundingBox footprintOffsets{};
        return getFootprintOffsets(entity, footprintOffsets) &&
               checkBounds(
                   {.min = Vector3Add(worldPos, footprintOffsets.min),
                    .max = Vector3Add(worldPos, footprintOffsets.max)},
                   entity,
                   ignoreActors);
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
        return gridSquares.at(position.row).at(position.col).occupant;
    }

    entt::entity NavigationGridSystem::GetSurfaceAt(const Vector3 worldPos) const
    {
        GridSquare position{};
        if (!WorldToGridSpace(worldPos, position))
        {
            return entt::null;
        }
        return GetSurfaceAt(position);
    }

    entt::entity NavigationGridSystem::GetSurfaceAt(const GridSquare position) const
    {
        if (!CheckWithinGridBounds(position))
        {
            return entt::null;
        }
        return gridSquares.at(position.row).at(position.col).heightMap.GetSurface();
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

        if (gridSquares.at(square.row - extents.row).at(square.col - extents.col).occupied)
        {
            return gridSquares.at(square.row - extents.row).at(square.col - extents.col).occupant;
        }
        if (gridSquares.at(square.row + extents.row).at(square.col + extents.col).occupied)
        {
            return gridSquares.at(square.row + extents.row).at(square.col + extents.col).occupant;
        }
        if (gridSquares.at(square.row - extents.row).at(square.col + extents.col).occupied)
        {
            return gridSquares.at(square.row - extents.row).at(square.col + extents.col).occupant;
        }
        if (gridSquares.at(square.row + extents.row).at(square.col - extents.col).occupied)
        {
            return gridSquares.at(square.row + extents.row).at(square.col - extents.col).occupant;
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
        GridSquare minRange{};
        GridSquare maxRange{};
        if (!getGridRangeForBounds(bb, minRange, maxRange)) return false;
        for (int row = minRange.row; row <= maxRange.row; ++row)
        {
            for (int col = minRange.col; col <= maxRange.col; ++col)
            {
                if (gridSquares.at(row).at(col).occupant == entity) return true;
            }
        }
        return false;
    }

    bool NavigationGridSystem::CompareSingleSquareOccupant(entt::entity entity, const BoundingBox& bb) const
    {
        const Vector3 center = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
        GridSquare square{};
        return WorldToGridSpace(center, square) && gridSquares.at(square.row).at(square.col).occupant == entity;
    }

    float calculateTerrainCost(const Vector3& normal, float maxSlopeAngle)
    {
        const float angle = slopeAngleDegrees(normal);

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

        const auto& transform = registry->get<sgTransform>(entity);
        const auto& area = registry->get<Collideable>(entity).worldBoundingBox;
        const float worldSize = terrain.WorldSize();
        const Matrix terrainToWorld = GetTerrainWorldMatrix(transform);
        const Matrix worldToTerrain = MatrixInvert(terrainToWorld);
        const Matrix normalToWorld = MatrixTranspose(worldToTerrain);

        // WorldToGridSpace fills the indices even for off-grid points, so a
        // terrain that sticks out past the grid still stamps the squares the
        // grid does cover — the clamps below trim its footprint.
        GridSquare topLeftIndex{}, bottomRightIndex{};
        const bool minInside = WorldToGridSpace(area.min, topLeftIndex);
        const bool maxInside = WorldToGridSpace(area.max, bottomRightIndex);
        if (!minInside || !maxInside)
        {
            std::cout << "WARNING: Terrain bounds (" << area.min.x << ", " << area.min.z << ") to (" << area.max.x
                      << ", " << area.max.z
                      << ") extend beyond the navigation grid; heights outside the grid are unwalkable.\n";
        }

        const int min_col = std::max(0, std::min(topLeftIndex.col, bottomRightIndex.col));
        const int max_col = std::min(
            static_cast<int>(gridSquares.at(0).size()) - 1, std::max(topLeftIndex.col, bottomRightIndex.col));
        const int min_row = std::max(0, std::min(topLeftIndex.row, bottomRightIndex.row));
        const int max_row =
            std::min(static_cast<int>(gridSquares.size()) - 1, std::max(topLeftIndex.row, bottomRightIndex.row));
        if (min_col > max_col || min_row > max_row) return; // entirely off-grid

        for (int row = min_row; row <= max_row; ++row)
        {
            for (int col = min_col; col <= max_col; ++col)
            {
                const auto worldGridPoint = gridSquares.at(row).at(col).worldPosMin;
                const auto localPoint = Vector3Transform(
                    {.x = worldGridPoint.x, .y = transform.GetWorldPos().y, .z = worldGridPoint.z},
                    worldToTerrain);
                const float localX = localPoint.x;
                const float localZ = localPoint.z;
                if (localX < 0.0f || localX > worldSize || localZ < 0.0f || localZ > worldSize) continue;

                const int terrainRow = std::clamp(
                    static_cast<int>(std::lround(localZ / terrain.cellSize)), 0, terrain.resolution - 1);
                const int terrainCol = std::clamp(
                    static_cast<int>(std::lround(localX / terrain.cellSize)), 0, terrain.resolution - 1);
                const float localHeight = terrain.SampleHeight(localX, localZ);
                const auto worldSurface =
                    Vector3Transform({.x = localX, .y = localHeight, .z = localZ}, terrainToWorld);
                const auto worldNormal =
                    Vector3Normalize(Vector3Transform(terrain.GetNormal(terrainRow, terrainCol), normalToWorld));
                gridSquares.at(row).at(col).heightMap.Set(worldSurface.y, worldNormal, entity);
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
             std::cmp_greater_equal(std::min(topLeftIndex.col, bottomRightIndex.col), gridSquares.at(0).size()) ||
             std::cmp_greater_equal(std::min(topLeftIndex.row, bottomRightIndex.row), gridSquares.size())))
        {
            return; // entirely off-grid
        }

        const auto& surface = registry->get<NavigationSurface>(entity);

        const int min_col = std::max(0, std::min(topLeftIndex.col, bottomRightIndex.col));
        const int max_col = std::min(
            static_cast<int>(gridSquares.at(0).size()) - 1, std::max(topLeftIndex.col, bottomRightIndex.col));
        const int min_row = std::max(0, std::min(topLeftIndex.row, bottomRightIndex.row));
        const int max_row =
            std::min(static_cast<int>(gridSquares.size()) - 1, std::max(topLeftIndex.row, bottomRightIndex.row));

        for (int row = min_row; row <= max_row; ++row)
        {
            for (int col = min_col; col <= max_col; ++col)
            {
                if (surface.heightSource == NavigationHeightSource::Ramp)
                {
                    float relativeX =
                        (gridSquares.at(row).at(col).worldPosMin.x - area.min.x) / (area.max.x - area.min.x);
                    float relativeZ =
                        (gridSquares.at(row).at(col).worldPosMin.z - area.min.z) / (area.max.z - area.min.z);
                    Vector3 stairDirection = Vector3Normalize(Vector3Subtract(area.max, area.min));
                    float relativePosition = relativeX * stairDirection.x + relativeZ * stairDirection.z;
                    float interpolatedHeight = area.min.y + (area.max.y - area.min.y) * relativePosition;
                    gridSquares.at(row).at(col).heightMap.Set(
                        interpolatedHeight,
                        Vector3Normalize(Vector3{.x = -stairDirection.x, .y = 1, .z = -stairDirection.z}),
                        entity);
                }
                else if (surface.heightSource == NavigationHeightSource::FlatTop)
                {
                    gridSquares.at(row).at(col).heightMap.Set(area.max.y, {.x = 0, .y = 1, .z = 0}, entity);
                }
                else if (surface.heightSource == NavigationHeightSource::RenderMesh)
                {
                    Vector3 gridCenter = {
                        .x = (gridSquares.at(row).at(col).worldPosMin.x +
                              gridSquares.at(row).at(col).worldPosMax.x) *
                             0.5f,
                        .y = area.max.y + 1.0f, // Start slightly above the terrain
                        .z = (gridSquares.at(row).at(col).worldPosMin.z +
                              gridSquares.at(row).at(col).worldPosMax.z) *
                             0.5f};

                    Ray ray = {.position = gridCenter, .direction = {.x = 0, .y = -1, .z = 0}}; // Cast ray down

                    RayCollision getFirstCollision{};
                    if (registry->any_of<Renderable>(entity))
                    {
                        const auto& renderable = registry->get<Renderable>(entity);
                        if (renderable.GetModel().has_value() && registry->any_of<sgTransform>(entity))
                        {
                            const auto& transform = registry->get<sgTransform>(entity);
                            getFirstCollision =
                                renderable.GetModel()->get().GetRayMeshCollision(ray, 0, transform.GetMatrix());
                        }
                    }
                    else if (registry->any_of<DynamicRenderable>(entity))
                    {
                        const auto& renderable = registry->get<DynamicRenderable>(entity);
                        if (const auto model = renderable.GetModel();
                            model.has_value() && registry->any_of<sgTransform>(entity))
                        {
                            const auto& transform = registry->get<sgTransform>(entity);
                            const Matrix worldMatrix =
                                MatrixMultiply(model->get().transform, transform.GetMatrix());
                            getFirstCollision.distance = std::numeric_limits<float>::max();
                            for (int meshIndex = 0; meshIndex < model->get().meshCount; ++meshIndex)
                            {
                                const auto meshCollision =
                                    GetRayCollisionMesh(ray, model->get().meshes[meshIndex], worldMatrix);
                                if (meshCollision.hit && meshCollision.distance < getFirstCollision.distance)
                                {
                                    getFirstCollision = meshCollision;
                                }
                            }
                        }
                    }

                    if (getFirstCollision.hit)
                    {
                        gridSquares.at(row).at(col).heightMap.Set(
                            getFirstCollision.point.y, getFirstCollision.normal, entity);
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
        const Vector3 minPoint{.x = minX, .y = 0.0f, .z = minZ};
        const Vector3 maxPoint{.x = std::nextafter(maxX, minX), .y = 0.0f, .z = std::nextafter(maxZ, minZ)};

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

        minRange = {.row = std::max(0, rawMinRow), .col = std::max(0, rawMinCol)};
        maxRange = {.row = std::min(gridHeight - 1, rawMaxRow), .col = std::min(gridWidth - 1, rawMaxCol)};
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
        const auto bounds = TransformAabbNoRotation(collideable.localBoundingBox, transform.GetMatrixNoRot());
        offsets = {.min = Vector3Subtract(bounds.min, origin), .max = Vector3Subtract(bounds.max, origin)};
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
            .x = (bb.min.x + bb.max.x) / 2.0f,
            .y = (bb.min.y + bb.max.y) / 2.0f,
            .z = (bb.min.z + bb.max.z) / 2.0f};
        return GetGridRange(center, bounds, minRange, maxRange);
    }

    bool NavigationGridSystem::GetGridRange(
        Vector3 center, int bounds, GridSquare& minRange, GridSquare& maxRange) const
    {
        if (!CheckWithinGridBounds(center))
        {
            return false;
        }

        Vector3 topLeft = {
            .x = center.x - static_cast<float>(bounds) * spacing,
            .y = center.y,
            .z = center.z - static_cast<float>(bounds) * spacing};
        Vector3 bottomRight = {
            .x = center.x + static_cast<float>(bounds) * spacing,
            .y = center.y,
            .z = center.z + static_cast<float>(bounds) * spacing};

        GridSquare topLeftIndex{};
        GridSquare bottomRightIndex{};

        WorldToGridSpace(topLeft, topLeftIndex);
        WorldToGridSpace(bottomRight, bottomRightIndex);

        // Clamp to grid
        topLeftIndex.col = std::max(topLeftIndex.col, 0);
        topLeftIndex.row = std::max(topLeftIndex.row, 0);
        bottomRightIndex.col = std::min(bottomRightIndex.col, static_cast<int>(gridSquares.at(0).size() - 1));
        bottomRightIndex.row = std::min(bottomRightIndex.row, static_cast<int>(gridSquares.size() - 1));

        minRange = {.row = topLeftIndex.row, .col = topLeftIndex.col};
        maxRange = {.row = bottomRightIndex.row, .col = bottomRightIndex.col};

        return true;
    }

    bool NavigationGridSystem::GridToWorldSpace(GridSquare gridPos, Vector3& out) const
    {
        GridSquare maxRange = {
            .row = static_cast<int>(gridSquares.at(0).size()), .col = static_cast<int>(gridSquares.size())};
        if (!CheckWithinBounds(gridPos, {.row = 0, .col = 0}, maxRange))
        {
            return false;
        }
        out = gridSquares.at(gridPos.row).at(gridPos.col).worldPosCentre;
        out.y = gridSquares.at(gridPos.row).at(gridPos.col).heightMap.GetHeight();
        return true;
    }

    bool NavigationGridSystem::WorldToGridSpace(const Vector3& worldPos, GridSquare& out) const
    {
        return WorldToGridSpace(
            worldPos,
            out,
            {.row = 0, .col = 0},
            {.row = static_cast<int>(gridSquares.at(0).size()), .col = static_cast<int>(gridSquares.size())});
    }

    bool NavigationGridSystem::WorldToGridSpace(
        const Vector3& worldPos, GridSquare& out, const GridSquare& minRange, const GridSquare& maxRange) const
    {
        int x = static_cast<int>(std::floor(worldPos.x / spacing)) + (slices / 2);
        int y = static_cast<int>(std::floor(worldPos.z / spacing)) + (slices / 2);
        out = {.row = y, .col = x};

        return out.row < maxRange.row && out.col < maxRange.col && out.col >= minRange.col &&
               out.row >= minRange.row;
    }

    void NavigationGridSystem::DrawDebug() const
    {
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

    void NavigationGridSystem::DrawDebugGrid() const
    {
        if (gridSquares.empty() || gridSquares.front().empty()) return;

        constexpr Color walkableColor = {.r = 40, .g = 220, .b = 80, .a = 65};
        constexpr Color blockedColor = {.r = 235, .g = 55, .b = 55, .a = 150};
        constexpr float surfaceOffset = 0.06f;
        constexpr float blockedSurfaceOffset = 0.01f;
        constexpr float cellInsetRatio = 0.04f;

        // Free space is the common case, so represent the whole grid with one
        // green quad and only emit per-cell geometry for occupied squares.
        // This keeps submitted geometry proportional to obstacle count rather
        // than total grid area.
        const auto& first = gridSquares.front().front();
        const auto& last = gridSquares.back().back();
        const float firstSampledHeight = first.heightMap.GetHeight();
        const float baseHeight =
            (firstSampledHeight != -1.0f ? firstSampledHeight : first.worldPosMin.y) + surfaceOffset;

        rlBegin(RL_QUADS);
        rlColor4ub(walkableColor.r, walkableColor.g, walkableColor.b, walkableColor.a);
        rlVertex3f(first.worldPosMin.x, baseHeight, first.worldPosMin.z);
        rlVertex3f(first.worldPosMin.x, baseHeight, last.worldPosMax.z);
        rlVertex3f(last.worldPosMax.x, baseHeight, last.worldPosMax.z);
        rlVertex3f(last.worldPosMax.x, baseHeight, first.worldPosMin.z);

        for (const auto& row : gridSquares)
        {
            for (const auto& square : row)
            {
                if (!square.occupied) continue;

                const float sampledHeight = square.heightMap.GetHeight();
                const float height = (sampledHeight != -1.0f ? sampledHeight : square.worldPosCentre.y) +
                                     surfaceOffset + blockedSurfaceOffset;
                const float insetX = square.debugBox.x * cellInsetRatio;
                const float insetZ = square.debugBox.z * cellInsetRatio;
                const float minX = square.worldPosMin.x + insetX;
                const float maxX = square.worldPosMax.x - insetX;
                const float minZ = square.worldPosMin.z + insetZ;
                const float maxZ = square.worldPosMax.z - insetZ;

                rlColor4ub(blockedColor.r, blockedColor.g, blockedColor.b, blockedColor.a);
                rlVertex3f(minX, height, minZ);
                rlVertex3f(minX, height, maxZ);
                rlVertex3f(maxX, height, maxZ);
                rlVertex3f(maxX, height, minZ);
            }
        }
        rlEnd();
    }

    std::vector<Vector3> NavigationGridSystem::tracebackPath(
        const std::vector<std::vector<GridSquare>>& came_from,
        const GridSquare& start,
        const GridSquare& finish,
        const GridSquare minRange)
    {
        auto combineWorldPosTerrainHeight = [this](auto gridPos) {
            Vector3 worldPos = gridSquares[gridPos.row][gridPos.col].worldPosCentre;
            worldPos.y = gridSquares[gridPos.row][gridPos.col].heightMap.GetHeight();
            return worldPos;
        };
        std::vector<Vector3> path;
        GridSquare current = {.row = finish.row, .col = finish.col};
        GridSquare previous{};
        std::pair<int, int> currentDir = {0, 0};

        path.push_back(combineWorldPosTerrainHeight(current));
        while (current.row != start.row || current.col != start.col)
        {
            previous = current;
            current = came_from.at(current.row - minRange.row).at(current.col - minRange.col);
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
        // This emits only direction changes, collapsing collinear grid runs into corners.
        // The start cell is omitted because it duplicates the actor's current position and
        // makes repeated move commands stutter before advancing to the first actual corner.
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
            GridSquare{.row = 0, .col = 0},
            GridSquare{
                .row = static_cast<int>(gridSquares.at(0).size()), .col = static_cast<int>(gridSquares.size())});
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
                if (!CheckWithinGridBounds(GridSquare{.row = row, .col = col}) ||
                    gridSquares.at(row).at(col).occupied)
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool NavigationGridSystem::checkFootprint(
        const GridSquare square,
        const BoundingBox& footprintOffsets,
        const entt::entity ignoreEntity,
        const bool ignoreActors) const
    {
        if (!CheckWithinGridBounds(square)) return false;

        const Vector3 origin = gridSquares.at(square.row).at(square.col).worldPosCentre;
        const BoundingBox footprint = {
            .min = Vector3Add(origin, footprintOffsets.min), .max = Vector3Add(origin, footprintOffsets.max)};
        return checkBounds(footprint, ignoreEntity, ignoreActors);
    }

    bool NavigationGridSystem::checkBounds(
        const BoundingBox& footprint, const entt::entity ignoreEntity, const bool ignoreActors) const
    {
        // Unlike occupancy stamping, a valid footprint must fit entirely inside the grid.
        if (!CheckWithinGridBounds(footprint.min) ||
            !CheckWithinGridBounds(
                Vector3{
                    .x = std::nextafter(footprint.max.x, footprint.min.x),
                    .y = 0.0f,
                    .z = std::nextafter(footprint.max.z, footprint.min.z)}))
            return false;

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
                const auto& cell = gridSquares.at(row).at(col);
                if (!cell.occupied) continue;
                if (cell.occupant != entt::null)
                {
                    if (cell.occupant == ignoreEntity || !registry->valid(cell.occupant)) continue;
                    if (ignoreActors && registry->any_of<MoveableActor>(cell.occupant)) continue;
                }
                return false;
            }
        }

        return true;
    }

    NavigationGridSquare* NavigationGridSystem::CastRay(
        int currentRow, int currentCol, Vector2 direction, float distance, std::vector<GridSquare>& debugLines)
    {
        int dist = static_cast<int>(std::round(distance));
        direction = Vector2Normalize(direction);
        int dirRow = static_cast<int>(std::round(direction.y));
        int dirCol = static_cast<int>(std::round(direction.x));

        for (int i = 0; i < dist; ++i)
        {
            GridSquare square = {.row = currentRow + (dirRow * i), .col = currentCol + (dirCol * i)};
            debugLines.push_back(square);

            if (!CheckWithinGridBounds(square))
            {
                continue;
            }

            auto& cell = gridSquares.at(square.row).at(square.col);
            cell.drawDebug = true;
            cell.debugColor = sage::colors::PURPLE_COLOR;

            if (cell.occupant != entt::null)
            {
                return &cell;
            }
        }
        return nullptr;
    }

    std::vector<Vector3> NavigationGridSystem::searchPath(
        const entt::entity entity,
        const Vector3 startPos,
        const Vector3 finishPos,
        const GridSquare minRange,
        const GridSquare maxRange,
        const bool useAStar,
        const bool findNextBestIfInvalid,
        const AStarHeuristic heuristicType)
    {
        GridSquare start{};
        GridSquare finish{};
        BoundingBox footprintOffsets{};
        if (!WorldToGridSpace(startPos, start) || !WorldToGridSpace(finishPos, finish) ||
            !getFootprintOffsets(entity, footprintOffsets))
            return {};
        if (minRange.row < 0 || minRange.col < 0 || maxRange.row > slices || maxRange.col > slices ||
            !CheckWithinBounds(start, minRange, maxRange))
            return {};
        const bool finishCanStop = checkFootprint(finish, footprintOffsets, entity);
        if (!findNextBestIfInvalid && !finishCanStop) return {};

        // A blocked destination is common for building roots and occupied actor
        // positions. Find the nearest free ring first, so the search can finish
        // as soon as it reaches that ring instead of visiting the whole window.
        int nearestFreeDistance = 0;
        if (findNextBestIfInvalid && !finishCanStop)
        {
            const int maximumDistance = (maxRange.row - minRange.row) + (maxRange.col - minRange.col);
            for (int distance = 1; distance <= maximumDistance; ++distance)
            {
                bool foundFree = false;
                for (int rowOffset = -distance; rowOffset <= distance; ++rowOffset)
                {
                    const int colOffset = distance - std::abs(rowOffset);
                    for (const int sign : {-1, 1})
                    {
                        if (colOffset == 0 && sign == 1) continue;
                        const GridSquare candidate{
                            .row = finish.row + rowOffset, .col = finish.col + sign * colOffset};
                        if (CheckWithinBounds(candidate, minRange, maxRange) &&
                            checkFootprint(candidate, footprintOffsets, entity))
                            foundFree = true;
                    }
                }
                if (foundFree)
                {
                    nearestFreeDistance = distance;
                    break;
                }
            }
        }

        struct FrontierNode
        {
            double priority = 0.0;
            std::uint64_t sequence = 0;
            GridSquare square{};
        };
        struct Compare
        {
            bool operator()(const FrontierNode& lhs, const FrontierNode& rhs) const
            {
                if (lhs.priority != rhs.priority) return lhs.priority > rhs.priority;
                return lhs.sequence > rhs.sequence;
            }
        };

        // Scratch storage is relative to the search window, not the map origin.
        const int rows = maxRange.row - minRange.row;
        const int cols = maxRange.col - minRange.col;
        std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));
        std::vector<std::vector<GridSquare>> cameFrom(rows, std::vector<GridSquare>(cols, {.row = -1, .col = -1}));
        std::vector<std::vector<double>> costs(
            rows, std::vector<double>(cols, std::numeric_limits<double>::infinity()));
        std::priority_queue<FrontierNode, std::vector<FrontierNode>, Compare> frontier;
        std::uint64_t sequence = 0;
        frontier.push({.priority = 0.0, .sequence = sequence++, .square = start});
        const GridSquare startIndex = start - minRange;
        visited.at(startIndex.row).at(startIndex.col) = true;
        costs.at(startIndex.row).at(startIndex.col) = 0.0;
        const Vector3 pathDirection = Vector3Subtract(finishPos, startPos);
        std::optional<GridSquare> closestReachable;
        double closestDistance = std::numeric_limits<double>::infinity();
        double closestPathCost = std::numeric_limits<double>::infinity();

        while (!frontier.empty())
        {
            const GridSquare current = frontier.top().square;
            const GridSquare currentIndex = current - minRange;
            frontier.pop();

            // NPCs can be crossed, but only unoccupied footprints are stopping places.
            // In particular, an occupied start must not become a successful fallback.
            const bool canStop = checkFootprint(current, footprintOffsets, entity);
            if (findNextBestIfInvalid && canStop)
            {
                const double distance = heuristic(current, finish);
                const double pathCost = costs.at(currentIndex.row).at(currentIndex.col);
                if (nearestFreeDistance > 0 && distance == nearestFreeDistance)
                    return tracebackPath(cameFrom, start, current, minRange);
                if (distance < closestDistance || (distance == closestDistance && pathCost < closestPathCost))
                {
                    closestReachable = current;
                    closestDistance = distance;
                    closestPathCost = pathCost;
                }
            }

            if (current == finish && canStop) return tracebackPath(cameFrom, start, finish, minRange);

            for (const auto& [dirRow, dirCol] : directions)
            {
                const GridSquare next{.row = current.row + dirRow, .col = current.col + dirCol};
                if (!CheckWithinBounds(next, minRange, maxRange)) continue;
                const GridSquare nextIndex = next - minRange;

                const double newCost = costs.at(currentIndex.row).at(currentIndex.col) +
                                       gridSquares.at(next.row).at(next.col).pathfindingCost;
                if (visited.at(nextIndex.row).at(nextIndex.col) &&
                    (!useAStar || newCost >= costs.at(nextIndex.row).at(nextIndex.col)))
                    continue;

                // Most neighbours already have an equal or cheaper route. Avoid checking
                // their collision footprint again unless this edge can improve the route.
                if (!checkFootprint(next, footprintOffsets, entity, true)) continue;
                visited.at(nextIndex.row).at(nextIndex.col) = true;
                costs.at(nextIndex.row).at(nextIndex.col) = newCost;
                cameFrom.at(nextIndex.row).at(nextIndex.col) = current;
                const double estimate = heuristicType == AStarHeuristic::FAVOUR_RIGHT
                                            ? heuristic_favourRight(next, finish, pathDirection)
                                            : heuristic(next, finish);
                frontier.push(
                    {.priority = useAStar ? newCost + estimate : 0.0, .sequence = sequence++, .square = next});
            }
        }

        if (findNextBestIfInvalid && closestReachable)
            return tracebackPath(cameFrom, start, *closestReachable, minRange);
        return {};
    }

    std::vector<Vector3> NavigationGridSystem::AStarPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const AStarHeuristic heuristicType,
        const bool findNextBestIfInvalid)
    {
        return AStarPathfind(
            entity,
            startPos,
            finishPos,
            {.row = 0, .col = 0},
            {.row = static_cast<int>(gridSquares.at(0).size()), .col = static_cast<int>(gridSquares.size())},
            heuristicType,
            findNextBestIfInvalid);
    }

    std::vector<Vector3> NavigationGridSystem::AStarPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const GridSquare& minRange,
        const GridSquare& maxRange,
        const AStarHeuristic heuristicType,
        const bool findNextBestIfInvalid)
    {
        return searchPath(
            entity, startPos, finishPos, minRange, maxRange, true, findNextBestIfInvalid, heuristicType);
    }

    /**
     * Generates a sequence of nodes that should be the "optimal" route from point A to
     * point B. Checks entire grid.
     * @return A route to the destination, or to the closest reachable cell when
     * findNextBestIfInvalid is true. Empty if no permitted route exists.
     */
    std::vector<Vector3> NavigationGridSystem::BFSPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const bool findNextBestIfInvalid)
    {
        return BFSPathfind(
            entity,
            startPos,
            finishPos,
            {.row = 0, .col = 0},
            {.row = static_cast<int>(gridSquares.at(0).size()), .col = static_cast<int>(gridSquares.size())},
            findNextBestIfInvalid);
    }

    /**
     * Generates a sequence of nodes that should be the "optimal" route from point A to
     * point B. Checks path within a range. Use "GetPathfindRange" to calculate
     * minRange/maxRange if needed.
     * @minRange The minimum grid index in the pathfinding range.
     * @maxRange The maximum grid index in the pathfinding range.
     * @return A route to the destination, or to the closest reachable cell when
     * findNextBestIfInvalid is true. Empty if no permitted route exists.
     */
    std::vector<Vector3> NavigationGridSystem::BFSPathfind(
        const entt::entity& entity,
        const Vector3& startPos,
        const Vector3& finishPos,
        const GridSquare& minRange,
        const GridSquare& maxRange,
        const bool findNextBestIfInvalid)
    {
        return searchPath(
            entity,
            startPos,
            finishPos,
            minRange,
            maxRange,
            false,
            findNextBestIfInvalid,
            AStarHeuristic::DEFAULT);
    }

    void NavigationGridSystem::InitGridHeightAndNormals()
    {
        std::cout << "START: Initialising grid height and normals \n";
        const auto& view = registry->view<Collideable, NavigationSurface>();
        for (const auto& entity : view)
        {
            const auto& surface = view.get<NavigationSurface>(entity);
            if (!surface.active || surface.heightSource == NavigationHeightSource::TerrainHeightField) continue;
            calculateTerrainHeightAndNormals(entity);
        }
        for (const auto& entity : registry->view<Terrain, sgTransform, NavigationSurface>())
        {
            const auto& surface = registry->get<NavigationSurface>(entity);
            if (!surface.active || surface.heightSource != NavigationHeightSource::TerrainHeightField) continue;
            calculateHeightAndNormalsFromTerrain(entity);
        }
        for (const auto& entity : registry->view<Collideable, NavigationObstacle>(entt::exclude<MoveableActor>))
        {
            const auto& collideable = registry->get<Collideable>(entity);
            const auto& obstacle = registry->get<NavigationObstacle>(entity);
            if (obstacle.active)
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
        return &gridSquares.at(row).at(col);
    }

    NavigationGridSystem::NavigationGridSystem(entt::registry* _registry, CollisionSystem* _collisionSystem)
        : registry(_registry), collisionSystem(_collisionSystem)
    {
    }
} // namespace sage

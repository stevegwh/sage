#include "TerrainMesh.hpp"
#include "engine/MathConstants.hpp"
#include "engine/SimulationClock.hpp"
#include "RaylibMemory.hpp"

#include "components/Collideable.hpp"
#include "components/CollisionIntent.hpp"
#include "components/DynamicRenderable.hpp"
#include "components/sgTransform.hpp"
#include "components/Terrain.hpp"
#include "LightManager.hpp"
#include "ResourceManager.hpp"
#include "rlgl.h"
#include "ShaderPaths.hpp"

#include "raymath.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace sage
{
    namespace
    {
        constexpr int TERRAIN_CHUNK_QUADS = 64;
        // Minimum half-thickness so a freshly created flat terrain still has a
        // pickable bounding box.
        constexpr float TERRAIN_BOUNDS_PADDING = 0.05f;

        int chunksPerSide(const Terrain& terrain)
        {
            return (terrain.resolution - 2 + TERRAIN_CHUNK_QUADS) / TERRAIN_CHUNK_QUADS;
        }

        // Inclusive vertex rows/cols covered by one chunk. Adjacent chunks share
        // their boundary vertices (each chunk owns duplicated copies).
        struct ChunkRange
        {
            int firstRow = 0;
            int lastRow = 0;
            int firstCol = 0;
            int lastCol = 0;

            [[nodiscard]] int VertsX() const
            {
                return lastCol - firstCol + 1;
            }

            [[nodiscard]] int VertsZ() const
            {
                return lastRow - firstRow + 1;
            }
        };

        ChunkRange getChunkRange(const Terrain& terrain, const int chunkRow, const int chunkCol)
        {
            const int firstRow = chunkRow * TERRAIN_CHUNK_QUADS;
            const int firstCol = chunkCol * TERRAIN_CHUNK_QUADS;
            return {
                .firstRow = firstRow,
                .lastRow = std::min(firstRow + TERRAIN_CHUNK_QUADS, terrain.resolution - 1),
                .firstCol = firstCol,
                .lastCol = std::min(firstCol + TERRAIN_CHUNK_QUADS, terrain.resolution - 1)};
        }

        void writeTextureWeights(const Terrain& terrain, const int row, const int col, unsigned char* colors)
        {
            const std::array<float, TERRAIN_TEXTURE_LAYERS> weights =
                terrain.textureWeights.empty()
                    ? std::array<float, TERRAIN_TEXTURE_LAYERS>{}
                    : terrain.textureWeights.at(static_cast<std::size_t>(row) * terrain.resolution + col);
            for (std::size_t layer = 0; layer < weights.size(); ++layer)
                colors[layer] = static_cast<unsigned char>(weights[layer] * 255.0f);
        }

        void fillChunkTexcoords(const Terrain& terrain, const ChunkRange& range, Mesh& mesh)
        {
            const float uvScale = terrain.cellSize / terrain.textureTileSize;
            int vertex = 0;
            for (int row = range.firstRow; row <= range.lastRow; ++row)
                for (int col = range.firstCol; col <= range.lastCol; ++col, ++vertex)
                {
                    mesh.texcoords[vertex * 2] = static_cast<float>(col) * uvScale;
                    mesh.texcoords[vertex * 2 + 1] = static_cast<float>(row) * uvScale;
                }
        }

        void fillChunkVertexData(const Terrain& terrain, const ChunkRange& range, Mesh& mesh)
        {
            fillChunkTexcoords(terrain, range, mesh);
            int vertexIndex = 0;
            for (int row = range.firstRow; row <= range.lastRow; ++row)
            {
                for (int col = range.firstCol; col <= range.lastCol; ++col, ++vertexIndex)
                {
                    mesh.vertices[vertexIndex * 3] = static_cast<float>(col) * terrain.cellSize;
                    mesh.vertices[vertexIndex * 3 + 1] = terrain.GetHeight(row, col);
                    mesh.vertices[vertexIndex * 3 + 2] = static_cast<float>(row) * terrain.cellSize;

                    const auto normal = terrain.GetNormal(row, col);
                    mesh.normals[vertexIndex * 3] = normal.x;
                    mesh.normals[vertexIndex * 3 + 1] = normal.y;
                    mesh.normals[vertexIndex * 3 + 2] = normal.z;

                    writeTextureWeights(terrain, row, col, mesh.colors + vertexIndex * 4);
                }
            }
        }

        Mesh createChunkMesh(const Terrain& terrain, const ChunkRange& range)
        {
            const int vertsX = range.VertsX();
            const int vertsZ = range.VertsZ();
            const int vertexCount = vertsX * vertsZ;

            Mesh mesh{};
            mesh.vertexCount = vertexCount;
            mesh.triangleCount = (vertsX - 1) * (vertsZ - 1) * 2;
            mesh.vertices = static_cast<float*>(MemAlloc(vertexCount * 3 * sizeof(float)));
            mesh.normals = static_cast<float*>(MemAlloc(vertexCount * 3 * sizeof(float)));
            mesh.texcoords = static_cast<float*>(MemAlloc(vertexCount * 2 * sizeof(float)));
            mesh.colors = static_cast<unsigned char*>(MemAlloc(vertexCount * 4 * sizeof(unsigned char)));
            mesh.indices = static_cast<unsigned short*>(MemAlloc(mesh.triangleCount * 3 * sizeof(unsigned short)));

            fillChunkVertexData(terrain, range, mesh);

            int indexCount = 0;
            for (int row = 0; row < vertsZ - 1; ++row)
            {
                for (int col = 0; col < vertsX - 1; ++col)
                {
                    const int topLeft = row * vertsX + col;
                    const int topRight = topLeft + 1;
                    const int bottomLeft = (row + 1) * vertsX + col;
                    const int bottomRight = bottomLeft + 1;

                    mesh.indices[indexCount++] = topLeft;
                    mesh.indices[indexCount++] = bottomLeft;
                    mesh.indices[indexCount++] = topRight;

                    mesh.indices[indexCount++] = topRight;
                    mesh.indices[indexCount++] = bottomLeft;
                    mesh.indices[indexCount++] = bottomRight;
                }
            }

            return mesh;
        }
    } // namespace

    Model GenerateTerrainModel(const Terrain& terrain)
    {
        assert(terrain.IsValid());
        const int chunks = chunksPerSide(terrain);
        const int meshCount = chunks * chunks;

        Model model{};
        model.transform = MatrixIdentity();
        model.meshCount = meshCount;
        model.meshes = static_cast<Mesh*>(sage::AllocateZeroedMemory(meshCount, sizeof(Mesh)));
        model.materialCount = 1;
        model.materials = static_cast<Material*>(sage::AllocateZeroedMemory(1, sizeof(Material)));
        model.materials[0] = LoadMaterialDefault();
        model.meshMaterial = static_cast<int*>(sage::AllocateZeroedMemory(meshCount, sizeof(int)));

        for (int chunkRow = 0; chunkRow < chunks; ++chunkRow)
        {
            for (int chunkCol = 0; chunkCol < chunks; ++chunkCol)
            {
                const int meshIndex = chunkRow * chunks + chunkCol;
                model.meshes[meshIndex] = createChunkMesh(terrain, getChunkRange(terrain, chunkRow, chunkCol));
                UploadMesh(&model.meshes[meshIndex], true);
                model.meshMaterial[meshIndex] = 0;
            }
        }

        return model;
    }

    void UpdateTerrainModelRegion(Model& model, const Terrain& terrain, const TerrainRegion& region)
    {
        const int chunks = chunksPerSide(terrain);
        if (model.meshCount != chunks * chunks) return;

        // raylib's default vertex-buffer slot numbers (mirror the rlgl macros
        // RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION / _NORMAL, which aren't exposed
        // through the public headers). UpdateMeshBuffer addresses VBOs by these.
        constexpr int vboPositionSlot = 0;
        constexpr int vboNormalSlot = 2;

        // Normals of vertices adjacent to the edited range change too.
        const int minRow = region.minRow - 1;
        const int maxRow = region.maxRow + 1;
        const int minCol = region.minCol - 1;
        const int maxCol = region.maxCol + 1;

        for (int chunkRow = 0; chunkRow < chunks; ++chunkRow)
        {
            for (int chunkCol = 0; chunkCol < chunks; ++chunkCol)
            {
                const auto range = getChunkRange(terrain, chunkRow, chunkCol);
                if (range.lastRow < minRow || range.firstRow > maxRow || range.lastCol < minCol ||
                    range.firstCol > maxCol)
                {
                    continue;
                }

                auto& mesh = model.meshes[chunkRow * chunks + chunkCol];
                fillChunkVertexData(terrain, range, mesh);
                UpdateMeshBuffer(mesh, 3, mesh.colors, mesh.vertexCount * 4, 0);
                UpdateMeshBuffer(
                    mesh,
                    vboPositionSlot,
                    mesh.vertices,
                    static_cast<int>(mesh.vertexCount * 3 * sizeof(float)),
                    0);
                UpdateMeshBuffer(
                    mesh, vboNormalSlot, mesh.normals, static_cast<int>(mesh.vertexCount * 3 * sizeof(float)), 0);
            }
        }
    }

    void UpdateTerrainTextureRegion(Model& model, const Terrain& terrain, const TerrainRegion& region)
    {
        const int chunks = chunksPerSide(terrain);
        if (model.meshCount != chunks * chunks) return;
        constexpr int VBO_COLOR_SLOT = 3;
        for (int chunkRow = 0; chunkRow < chunks; ++chunkRow)
            for (int chunkCol = 0; chunkCol < chunks; ++chunkCol)
            {
                const auto range = getChunkRange(terrain, chunkRow, chunkCol);
                const int firstRow = std::max(range.firstRow, region.minRow);
                const int lastRow = std::min(range.lastRow, region.maxRow);
                const int firstCol = std::max(range.firstCol, region.minCol);
                const int lastCol = std::min(range.lastCol, region.maxCol);
                if (firstRow > lastRow || firstCol > lastCol) continue;
                auto& mesh = model.meshes[chunkRow * chunks + chunkCol];
                for (int row = firstRow; row <= lastRow; ++row)
                {
                    for (int col = firstCol; col <= lastCol; ++col)
                    {
                        const int vertex = (row - range.firstRow) * range.VertsX() + col - range.firstCol;
                        writeTextureWeights(terrain, row, col, mesh.colors + vertex * 4);
                    }
                }
                // One contiguous upload per touched chunk avoids a GL call for each painted row.
                const int firstVertex = (firstRow - range.firstRow) * range.VertsX() + firstCol - range.firstCol;
                const int lastVertex = (lastRow - range.firstRow) * range.VertsX() + lastCol - range.firstCol;
                UpdateMeshBuffer(
                    mesh,
                    VBO_COLOR_SLOT,
                    mesh.colors + firstVertex * 4,
                    (lastVertex - firstVertex + 1) * 4,
                    firstVertex * 4);
            }
    }

    void UpdateTerrainTextures(Model& model, const Terrain& terrain)
    {
        for (std::size_t layer = 0; layer < TERRAIN_TEXTURE_LAYERS; ++layer)
        {
            Texture texture{
                .id = rlGetTextureIdDefault(),
                .width = 1,
                .height = 1,
                .mipmaps = 1,
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            if (!terrain.textures[layer].empty())
            {
                texture = ResourceManager::GetInstance().TextureLoad(terrain.textures[layer]);
                if (texture.id != 0)
                {
                    if (texture.mipmaps == 1) GenTextureMipmaps(&texture);
                    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
                    SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
                }
            }
            model.materials[0].maps[layer].texture = texture;
        }
        const int chunks = chunksPerSide(terrain);
        for (int chunkRow = 0; chunkRow < chunks; ++chunkRow)
            for (int chunkCol = 0; chunkCol < chunks; ++chunkCol)
            {
                auto& mesh = model.meshes[chunkRow * chunks + chunkCol];
                fillChunkTexcoords(terrain, getChunkRange(terrain, chunkRow, chunkCol), mesh);
                UpdateMeshBuffer(
                    mesh, 1, mesh.texcoords, static_cast<int>(mesh.vertexCount * 2 * sizeof(float)), 0);
            }
    }

    BoundingBox GetTerrainLocalBounds(const Terrain& terrain)
    {
        const auto [minHeight, maxHeight] = std::ranges::minmax_element(terrain.heights);
        const float worldSize = terrain.WorldSize();
        return {
            .min = {.x = 0.0f, .y = *minHeight - TERRAIN_BOUNDS_PADDING, .z = 0.0f},
            .max = {.x = worldSize, .y = *maxHeight + TERRAIN_BOUNDS_PADDING, .z = worldSize}};
    }

    Matrix GetTerrainWorldMatrix(const sgTransform& transform)
    {
        const auto position = transform.GetWorldPos();
        const auto scale = transform.GetScale();
        return MatrixMultiply(
            MatrixMultiply(
                MatrixScale(scale.x, scale.y, scale.z),
                MatrixRotateY(transform.GetWorldRot().y * sage::math::DEGREES_TO_RADIANS)),
            MatrixTranslate(position.x, position.y, position.z));
    }

    namespace
    {
        // Inclusive vertex range covered by a brush circle, clamped to the field.
        TerrainRegion brushRegion(const Terrain& terrain, const Vector2 center, const float radius)
        {
            return {
                .minRow = std::max(0, static_cast<int>(std::floor((center.y - radius) / terrain.cellSize))),
                .minCol = std::max(0, static_cast<int>(std::floor((center.x - radius) / terrain.cellSize))),
                .maxRow = std::min(
                    terrain.resolution - 1, static_cast<int>(std::ceil((center.y + radius) / terrain.cellSize))),
                .maxCol = std::min(
                    terrain.resolution - 1, static_cast<int>(std::ceil((center.x + radius) / terrain.cellSize)))};
        }

        float smoothstepFalloff(const float distance, const float radius)
        {
            const float t = distance / radius;
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }

        // Cheap, deterministic-per-call hash in [-1, 1] for the Noise brush.
        float noiseAt(const int row, const int col, const unsigned int seed)
        {
            unsigned int h = seed;
            h ^= static_cast<unsigned int>(row) * 374761393u;
            h ^= static_cast<unsigned int>(col) * 668265263u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            return static_cast<float>(h) / 2147483647.5f - 1.0f;
        }
    } // namespace

    TerrainRegion ApplyTerrainBrush(
        Terrain& terrain,
        const Vector2 localCenter,
        const float radius,
        const float amount,
        const TerrainBrushMode mode,
        const float reference)
    {
        const TerrainRegion region = brushRegion(terrain, localCenter, radius);

        // Smooth and Erosion read neighbour heights; sample from an immutable
        // snapshot of the region so the result is independent of write order.
        std::vector<float> snapshot;
        const int snapCols = region.maxCol - region.minCol + 1;
        const bool needsSnapshot = mode == TerrainBrushMode::Smooth || mode == TerrainBrushMode::Erosion;
        if (needsSnapshot)
        {
            snapshot.reserve(static_cast<std::size_t>(region.maxRow - region.minRow + 1) * snapCols);
            for (int row = region.minRow; row <= region.maxRow; ++row)
                for (int col = region.minCol; col <= region.maxCol; ++col)
                    snapshot.push_back(terrain.GetHeight(row, col));
        }
        // Neighbour height from the snapshot, falling back to the live field at
        // the region edge (so brush borders blend into untouched terrain).
        const auto sample = [&](const int row, const int col) -> float {
            if (row >= region.minRow && row <= region.maxRow && col >= region.minCol && col <= region.maxCol)
                return snapshot.at(
                    static_cast<std::size_t>(row - region.minRow) * snapCols + (col - region.minCol));
            return terrain.GetHeight(row, col);
        };

        // A per-stroke-frame seed keeps Noise from layering the same pattern.
        const auto seed = static_cast<unsigned int>(sage::Time() * 1000.0) + 1u;

        for (int row = region.minRow; row <= region.maxRow; ++row)
        {
            for (int col = region.minCol; col <= region.maxCol; ++col)
            {
                const float dx = static_cast<float>(col) * terrain.cellSize - localCenter.x;
                const float dz = static_cast<float>(row) * terrain.cellSize - localCenter.y;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance >= radius) continue;

                const float falloff = smoothstepFalloff(distance, radius);
                const float height = terrain.GetHeight(row, col);
                float next = height;

                switch (mode)
                {
                case TerrainBrushMode::Texture:
                    break;
                case TerrainBrushMode::RaiseLower:
                    next = height + amount * falloff;
                    break;
                case TerrainBrushMode::Flatten:
                    next = height + (reference - height) * std::clamp(amount, 0.0f, 1.0f) * falloff;
                    break;
                case TerrainBrushMode::Smooth: {
                    const float avg = 0.25f * (sample(row - 1, col) + sample(row + 1, col) + sample(row, col - 1) +
                                               sample(row, col + 1));
                    next = height + (avg - height) * std::clamp(amount, 0.0f, 1.0f) * falloff;
                    break;
                }
                case TerrainBrushMode::Noise:
                    next = height + noiseAt(row, col, seed) * amount * falloff;
                    break;
                case TerrainBrushMode::Erosion: {
                    const float avg = 0.25f * (sample(row - 1, col) + sample(row + 1, col) + sample(row, col - 1) +
                                               sample(row, col + 1));
                    const float diff = avg - height;
                    // Peaks (diff < 0) wear faster than pits fill, so the field
                    // loses material overall — the thermal-erosion look.
                    const float bias = diff < 0.0f ? 1.0f : 0.3f;
                    next = height + diff * bias * std::clamp(amount, 0.0f, 1.0f) * falloff;
                    break;
                }
                case TerrainBrushMode::Ramp:
                    break; // not a paint brush; see ApplyTerrainRamp
                }

                terrain.SetHeight(row, col, next);
            }
        }

        return region;
    }

    TerrainRegion ApplyTerrainTextureBrush(
        Terrain& terrain,
        const Vector2 localCenter,
        const float radius,
        const float amount,
        const std::size_t layer,
        const bool erase)
    {
        const auto region = brushRegion(terrain, localCenter, radius);
        if (layer >= TERRAIN_TEXTURE_LAYERS || terrain.textures[layer].empty() || radius <= 0.0f || amount <= 0.0f)
            return region;
        if (terrain.textureWeights.empty()) terrain.textureWeights.resize(terrain.heights.size());
        for (int row = region.minRow; row <= region.maxRow; ++row)
            for (int col = region.minCol; col <= region.maxCol; ++col)
            {
                const float dx = static_cast<float>(col) * terrain.cellSize - localCenter.x;
                const float dz = static_cast<float>(row) * terrain.cellSize - localCenter.y;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance >= radius) continue;
                const float blend = 1.0f - std::exp(-amount * smoothstepFalloff(distance, radius));
                auto& weights =
                    terrain.textureWeights.at(static_cast<std::size_t>(row) * terrain.resolution + col);
                if (erase)
                    weights[layer] *= 1.0f - blend;
                else
                {
                    for (auto& weight : weights)
                        weight *= 1.0f - blend;
                    weights[layer] += blend;
                }
            }
        return region;
    }

    TerrainRegion ApplyTerrainRamp(
        Terrain& terrain, const Vector2 localStart, const Vector2 localEnd, const float halfWidth)
    {
        const TerrainRegion region{
            .minRow = std::max(
                0,
                static_cast<int>(std::floor((std::min(localStart.y, localEnd.y) - halfWidth) / terrain.cellSize))),
            .minCol = std::max(
                0,
                static_cast<int>(std::floor((std::min(localStart.x, localEnd.x) - halfWidth) / terrain.cellSize))),
            .maxRow = std::min(
                terrain.resolution - 1,
                static_cast<int>(std::ceil((std::max(localStart.y, localEnd.y) + halfWidth) / terrain.cellSize))),
            .maxCol = std::min(
                terrain.resolution - 1,
                static_cast<int>(std::ceil((std::max(localStart.x, localEnd.x) + halfWidth) / terrain.cellSize)))};

        const Vector2 axis = Vector2Subtract(localEnd, localStart);
        const float axisLenSq = Vector2LengthSqr(axis);
        if (axisLenSq < 1e-4f) return region;

        const float startHeight = terrain.SampleHeight(localStart.x, localStart.y);
        const float endHeight = terrain.SampleHeight(localEnd.x, localEnd.y);

        for (int row = region.minRow; row <= region.maxRow; ++row)
        {
            for (int col = region.minCol; col <= region.maxCol; ++col)
            {
                const Vector2 point = {
                    .x = static_cast<float>(col) * terrain.cellSize,
                    .y = static_cast<float>(row) * terrain.cellSize};
                const float t = std::clamp(
                    Vector2DotProduct(Vector2Subtract(point, localStart), axis) / axisLenSq, 0.0f, 1.0f);
                const Vector2 projected = Vector2Add(localStart, Vector2Scale(axis, t));
                const float distance = Vector2Distance(point, projected);
                if (distance >= halfWidth) continue;

                const float falloff = smoothstepFalloff(distance, halfWidth);
                const float target = Lerp(startHeight, endHeight, t);
                terrain.SetHeight(row, col, Lerp(terrain.GetHeight(row, col), target, falloff));
            }
        }

        return region;
    }

    std::optional<RayCollision> GetTerrainRayCollision(
        const Terrain& terrain, const Matrix terrainToWorld, const Ray& ray)
    {
        // Full asset validation scans texture weights; geometry dimensions suffice for this hot path.
        if (terrain.resolution < 2 || !std::isfinite(terrain.cellSize) || terrain.cellSize <= 0.0f ||
            terrain.heights.size() != static_cast<std::size_t>(terrain.resolution) * terrain.resolution)
            return std::nullopt;

        const float determinant = MatrixDeterminant(terrainToWorld);
        if (!std::isfinite(determinant) || determinant == 0.0f) return std::nullopt;

        const Matrix worldToTerrain = MatrixInvert(terrainToWorld);
        const Vector3 localOrigin = Vector3Transform(ray.position, worldToTerrain);
        // Transform a direction without translation or subtracting nearly equal positions.
        const Vector3 localDirection = {
            .x = worldToTerrain.m0 * ray.direction.x + worldToTerrain.m4 * ray.direction.y +
                 worldToTerrain.m8 * ray.direction.z,
            .y = worldToTerrain.m1 * ray.direction.x + worldToTerrain.m5 * ray.direction.y +
                 worldToTerrain.m9 * ray.direction.z,
            .z = worldToTerrain.m2 * ray.direction.x + worldToTerrain.m6 * ray.direction.y +
                 worldToTerrain.m10 * ray.direction.z};
        if (!std::isfinite(localOrigin.x) || !std::isfinite(localOrigin.y) || !std::isfinite(localOrigin.z) ||
            !std::isfinite(localDirection.x) || !std::isfinite(localDirection.y) ||
            !std::isfinite(localDirection.z) || Vector3LengthSqr(localDirection) == 0.0f)
            return std::nullopt;

        const double worldSize = terrain.WorldSize();
        // Inverting a float matrix can move a ray on the outer edge slightly outside the grid.
        // Expand traversal bounds only; the triangle tests still decide whether there is a hit.
        const double edgeTolerance = terrain.cellSize * 0.00001;
        double entry = 0.0;
        double exit = std::numeric_limits<double>::infinity();
        const auto clipAxis = [&](const double origin, const double direction) {
            if (direction == 0.0) return origin >= -edgeTolerance && origin <= worldSize + edgeTolerance;
            const double first = (-edgeTolerance - origin) / direction;
            const double last = (worldSize + edgeTolerance - origin) / direction;
            entry = std::max(entry, std::min(first, last));
            exit = std::min(exit, std::max(first, last));
            return entry <= exit;
        };
        if (!clipAxis(localOrigin.x, localDirection.x) || !clipAxis(localOrigin.z, localDirection.z))
            return std::nullopt;

        const int cells = terrain.resolution - 1;
        const auto cellAtEntry = [&](const double origin, const double direction, const double offset) {
            const double coordinate =
                std::clamp(origin + direction * entry + offset, 0.0, worldSize) / terrain.cellSize;
            return std::clamp(static_cast<int>(std::floor(coordinate)), 0, cells - 1);
        };
        int col = cellAtEntry(localOrigin.x, localDirection.x, 0.0);
        int row = cellAtEntry(localOrigin.z, localDirection.z, 0.0);
        const int colStep = (localDirection.x > 0.0f) - (localDirection.x < 0.0f);
        const int rowStep = (localDirection.z > 0.0f) - (localDirection.z < 0.0f);
        const auto nextBoundary =
            [&](const int cell, const int step, const double origin, const double direction) {
                if (step == 0) return std::numeric_limits<double>::infinity();
                return (static_cast<double>(cell + (step > 0 ? 1 : 0)) * terrain.cellSize - origin) / direction;
            };
        double nextCol = nextBoundary(col, colStep, localOrigin.x, localDirection.x);
        double nextRow = nextBoundary(row, rowStep, localOrigin.z, localDirection.z);
        const double colDelta = colStep == 0 ? std::numeric_limits<double>::infinity()
                                             : terrain.cellSize / std::abs(static_cast<double>(localDirection.x));
        const double rowDelta = rowStep == 0 ? std::numeric_limits<double>::infinity()
                                             : terrain.cellSize / std::abs(static_cast<double>(localDirection.z));

        const auto vertex = [&](const int vertexRow, const int vertexCol) {
            return Vector3Transform(
                {.x = static_cast<float>(vertexCol) * terrain.cellSize,
                 .y = terrain.GetHeight(vertexRow, vertexCol),
                 .z = static_cast<float>(vertexRow) * terrain.cellSize},
                terrainToWorld);
        };
        const double timeTolerance = edgeTolerance / Vector3Length(localDirection);
        while (row >= 0 && row < cells && col >= 0 && col < cells)
        {
            const double next = std::min(nextCol, nextRow);
            const double segmentEnd = std::min(next, exit);
            // At grid boundaries, float transform rounding can put the intersection in either
            // adjacent cell. Include those cells, but accept hits only in this traversal segment.
            const int minCol = std::min(col, cellAtEntry(localOrigin.x, localDirection.x, -edgeTolerance));
            const int maxCol = std::max(col, cellAtEntry(localOrigin.x, localDirection.x, edgeTolerance));
            const int minRow = std::min(row, cellAtEntry(localOrigin.z, localDirection.z, -edgeTolerance));
            const int maxRow = std::max(row, cellAtEntry(localOrigin.z, localDirection.z, edgeTolerance));
            std::optional<RayCollision> closest;
            const auto consider = [&](const RayCollision collision) {
                if (collision.hit && collision.distance >= entry - timeTolerance &&
                    collision.distance <= segmentEnd + timeTolerance &&
                    (!closest || collision.distance < closest->distance))
                    closest = collision;
            };
            for (int candidateRow = minRow; candidateRow <= maxRow; ++candidateRow)
                for (int candidateCol = minCol; candidateCol <= maxCol; ++candidateCol)
                {
                    const auto topLeft = vertex(candidateRow, candidateCol);
                    const auto topRight = vertex(candidateRow, candidateCol + 1);
                    const auto bottomLeft = vertex(candidateRow + 1, candidateCol);
                    const auto bottomRight = vertex(candidateRow + 1, candidateCol + 1);
                    // Match createChunkMesh's diagonal and winding, including transformed normals.
                    consider(GetRayCollisionTriangle(ray, topLeft, bottomLeft, topRight));
                    consider(GetRayCollisionTriangle(ray, topRight, bottomLeft, bottomRight));
                }
            if (closest) return closest;
            if (!std::isfinite(next) || next > exit) break;
            entry = next;
            if (nextCol <= nextRow)
            {
                col += colStep;
                nextCol += colDelta;
            }
            else
            {
                row += rowStep;
                nextRow += rowDelta;
            }
        }

        return std::nullopt;
    }

    std::optional<Vector3> GetTerrainRayHit(const Terrain& terrain, const sgTransform& transform, const Ray& ray)
    {
        if (const auto collision = GetTerrainRayCollision(terrain, GetTerrainWorldMatrix(transform), ray))
            return collision->point;
        return std::nullopt;
    }

    void AttachTerrainRenderable(entt::registry& registry, const entt::entity entity, LightManager& lightManager)
    {
        const auto& terrain = registry.get<Terrain>(entity);
        assert(terrain.IsValid());

        auto& renderable = registry.get_or_emplace<DynamicRenderable>(entity);
        renderable.SetModel(GenerateTerrainModel(terrain));
        renderable.SetName("Terrain");
        renderable.hint = sage::colors::WHITE_COLOR;

        Shader lighting = ResourceManager::GetInstance().ShaderLoad(
            ShaderPath("custom/terrain.vs"), ShaderPath("custom/terrain.fs"));
        lighting.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(lighting, "texture3");
        lightManager.LinkShaderToLights(lighting);
        renderable.SetShader(lighting);
        if (auto model = renderable.GetModel()) UpdateTerrainTextures(model->get(), terrain);

        if (!registry.all_of<Collideable>(entity))
        {
            auto& collideable = registry.emplace<Collideable>(entity);
            collideable.shape = ColliderShape::RenderMesh;
            collideable.isStatic = true;
        }
        auto& surface = registry.get_or_emplace<NavigationSurface>(entity);
        surface.heightSource = NavigationHeightSource::TerrainHeightField;
        auto& cursorTarget = registry.get_or_emplace<CursorTarget>(entity);
        cursorTarget.cursor = cursors::MOVE;
        cursorTarget.allowNavigationClickThrough = true;
        UpdateTerrainCollideableBounds(registry, entity);
    }

    void UpdateTerrainCollideableBounds(entt::registry& registry, const entt::entity entity)
    {
        auto* collideable = registry.try_get<Collideable>(entity);
        if (collideable == nullptr) return;

        const auto& terrain = registry.get<Terrain>(entity);
        const auto& transform = registry.get<sgTransform>(entity);
        collideable->localBoundingBox = GetTerrainLocalBounds(terrain);
        collideable->worldBoundingBox =
            TransformBoundingBoxByCorners(collideable->localBoundingBox, GetTerrainWorldMatrix(transform));
    }
} // namespace sage

#include "TerrainMesh.hpp"

#include "components/Collideable.hpp"
#include "components/DynamicRenderable.hpp"
#include "components/sgTransform.hpp"
#include "components/Terrain.hpp"
#include "LightManager.hpp"
#include "ResourceManager.hpp"

#include "raymath.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace sage
{
    namespace
    {
        constexpr int TERRAIN_CHUNK_QUADS = 64;
        constexpr const char* TERRAIN_LIGHTING_VS = "resources/shaders/custom/lighting.vs";
        constexpr const char* TERRAIN_LIGHTING_FS = "resources/shaders/custom/lighting.fs";
        constexpr Color TERRAIN_TINT = {126, 138, 102, 255};
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
                firstRow,
                std::min(firstRow + TERRAIN_CHUNK_QUADS, terrain.resolution - 1),
                firstCol,
                std::min(firstCol + TERRAIN_CHUNK_QUADS, terrain.resolution - 1)};
        }

        void fillChunkVertexData(const Terrain& terrain, const ChunkRange& range, Mesh& mesh)
        {
            const float uvScale = 1.0f / static_cast<float>(terrain.resolution - 1);
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

                    mesh.texcoords[vertexIndex * 2] = static_cast<float>(col) * uvScale;
                    mesh.texcoords[vertexIndex * 2 + 1] = static_cast<float>(row) * uvScale;
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
            mesh.vertices = static_cast<float*>(RL_MALLOC(vertexCount * 3 * sizeof(float)));
            mesh.normals = static_cast<float*>(RL_MALLOC(vertexCount * 3 * sizeof(float)));
            mesh.texcoords = static_cast<float*>(RL_MALLOC(vertexCount * 2 * sizeof(float)));
            mesh.indices =
                static_cast<unsigned short*>(RL_MALLOC(mesh.triangleCount * 3 * sizeof(unsigned short)));

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
        model.meshes = static_cast<Mesh*>(RL_CALLOC(meshCount, sizeof(Mesh)));
        model.materialCount = 1;
        model.materials = static_cast<Material*>(RL_CALLOC(1, sizeof(Material)));
        model.materials[0] = LoadMaterialDefault();
        model.meshMaterial = static_cast<int*>(RL_CALLOC(meshCount, sizeof(int)));

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
                // Buffer indices follow raylib's default attribute order:
                // 0 = positions, 2 = normals.
                UpdateMeshBuffer(mesh, 0, mesh.vertices, mesh.vertexCount * 3 * sizeof(float), 0);
                UpdateMeshBuffer(mesh, 2, mesh.normals, mesh.vertexCount * 3 * sizeof(float), 0);
            }
        }
    }

    BoundingBox GetTerrainLocalBounds(const Terrain& terrain)
    {
        const auto [minHeight, maxHeight] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
        const float worldSize = terrain.WorldSize();
        return {
            {0.0f, *minHeight - TERRAIN_BOUNDS_PADDING, 0.0f},
            {worldSize, *maxHeight + TERRAIN_BOUNDS_PADDING, worldSize}};
    }

    namespace
    {
        // Inclusive vertex range covered by a brush circle, clamped to the field.
        TerrainRegion brushRegion(const Terrain& terrain, const Vector2 center, const float radius)
        {
            return {
                std::max(0, static_cast<int>(std::floor((center.y - radius) / terrain.cellSize))),
                std::max(0, static_cast<int>(std::floor((center.x - radius) / terrain.cellSize))),
                std::min(
                    terrain.resolution - 1, static_cast<int>(std::ceil((center.y + radius) / terrain.cellSize))),
                std::min(
                    terrain.resolution - 1,
                    static_cast<int>(std::ceil((center.x + radius) / terrain.cellSize)))};
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
                return snapshot[static_cast<std::size_t>(row - region.minRow) * snapCols + (col - region.minCol)];
            return terrain.GetHeight(row, col);
        };

        // A per-stroke-frame seed keeps Noise from layering the same pattern.
        const auto seed = static_cast<unsigned int>(GetTime() * 1000.0) + 1u;

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
                case TerrainBrushMode::RaiseLower:
                    next = height + amount * falloff;
                    break;
                case TerrainBrushMode::Flatten:
                    next = height + (reference - height) * std::clamp(amount, 0.0f, 1.0f) * falloff;
                    break;
                case TerrainBrushMode::Smooth:
                {
                    const float avg = 0.25f * (sample(row - 1, col) + sample(row + 1, col) +
                                               sample(row, col - 1) + sample(row, col + 1));
                    next = height + (avg - height) * std::clamp(amount, 0.0f, 1.0f) * falloff;
                    break;
                }
                case TerrainBrushMode::Noise:
                    next = height + noiseAt(row, col, seed) * amount * falloff;
                    break;
                case TerrainBrushMode::Erosion:
                {
                    const float avg = 0.25f * (sample(row - 1, col) + sample(row + 1, col) +
                                               sample(row, col - 1) + sample(row, col + 1));
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

    TerrainRegion ApplyTerrainRamp(
        Terrain& terrain, const Vector2 localStart, const Vector2 localEnd, const float halfWidth)
    {
        const TerrainRegion region{
            std::max(
                0,
                static_cast<int>(
                    std::floor((std::min(localStart.y, localEnd.y) - halfWidth) / terrain.cellSize))),
            std::max(
                0,
                static_cast<int>(
                    std::floor((std::min(localStart.x, localEnd.x) - halfWidth) / terrain.cellSize))),
            std::min(
                terrain.resolution - 1,
                static_cast<int>(
                    std::ceil((std::max(localStart.y, localEnd.y) + halfWidth) / terrain.cellSize))),
            std::min(
                terrain.resolution - 1,
                static_cast<int>(
                    std::ceil((std::max(localStart.x, localEnd.x) + halfWidth) / terrain.cellSize)))};

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
                    static_cast<float>(col) * terrain.cellSize, static_cast<float>(row) * terrain.cellSize};
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

    std::optional<Vector3> GetTerrainRayHit(const Terrain& terrain, const Vector3 worldOrigin, const Ray& ray)
    {
        if (!terrain.IsValid()) return std::nullopt;

        const Ray localRay = {Vector3Subtract(ray.position, worldOrigin), Vector3Normalize(ray.direction)};
        const auto bounds = GetTerrainLocalBounds(terrain);
        const auto entry = GetRayCollisionBox(localRay, bounds);
        if (!entry.hit) return std::nullopt;

        const float step = terrain.cellSize * 0.5f;
        const float worldSize = terrain.WorldSize();
        float t = std::max(entry.distance, 0.0f);
        const float tEnd = t + Vector3Distance(bounds.min, bounds.max) + step;

        Vector3 previous = Vector3Add(localRay.position, Vector3Scale(localRay.direction, t));
        bool previousAbove = previous.y > terrain.SampleHeight(previous.x, previous.z);
        for (t += step; t <= tEnd; t += step)
        {
            const Vector3 point = Vector3Add(localRay.position, Vector3Scale(localRay.direction, t));
            const bool inField =
                point.x >= 0.0f && point.x <= worldSize && point.z >= 0.0f && point.z <= worldSize;
            const bool above = point.y > terrain.SampleHeight(point.x, point.z);
            if (inField && previousAbove && !above)
            {
                Vector3 high = previous;
                Vector3 low = point;
                for (int i = 0; i < 8; ++i)
                {
                    const Vector3 mid = Vector3Scale(Vector3Add(high, low), 0.5f);
                    if (mid.y > terrain.SampleHeight(mid.x, mid.z))
                    {
                        high = mid;
                    }
                    else
                    {
                        low = mid;
                    }
                }
                const Vector3 hit = Vector3Scale(Vector3Add(high, low), 0.5f);
                return Vector3Add(worldOrigin, Vector3{hit.x, terrain.SampleHeight(hit.x, hit.z), hit.z});
            }
            previous = point;
            previousAbove = above;
        }

        return std::nullopt;
    }

    void AttachTerrainRenderable(entt::registry& registry, const entt::entity entity, LightManager& lightManager)
    {
        const auto& terrain = registry.get<Terrain>(entity);
        assert(terrain.IsValid());

        auto& renderable = registry.get_or_emplace<DynamicRenderable>(entity);
        renderable.SetModel(GenerateTerrainModel(terrain));
        renderable.SetName("Terrain");
        renderable.hint = TERRAIN_TINT;

        Shader lighting = ResourceManager::GetInstance().ShaderLoad(TERRAIN_LIGHTING_VS, TERRAIN_LIGHTING_FS);
        lightManager.LinkShaderToLights(lighting);
        renderable.SetShader(lighting);

        // Only seed defaults on first creation — the layer (and other flags) are
        // authored in the inspector and persist with the map, so rebuilds must
        // not stomp them. GeometryComplex gives mesh-accurate cursor hits on the
        // sculpted surface (the nav grid samples the height field either way).
        if (!registry.all_of<Collideable>(entity))
        {
            auto& collideable = registry.emplace<Collideable>(entity);
            collideable.SetCollisionLayer(collision_layers::GeometryComplex);
            collideable.isStatic = true;
            collideable.blocksNavigation = false;
        }
        UpdateTerrainCollideableBounds(registry, entity);
    }

    void UpdateTerrainCollideableBounds(entt::registry& registry, const entt::entity entity)
    {
        auto* collideable = registry.try_get<Collideable>(entity);
        if (collideable == nullptr) return;

        const auto& terrain = registry.get<Terrain>(entity);
        const auto origin = registry.get<sgTransform>(entity).GetWorldPos();
        collideable->localBoundingBox = GetTerrainLocalBounds(terrain);
        collideable->worldBoundingBox = {
            Vector3Add(collideable->localBoundingBox.min, origin),
            Vector3Add(collideable->localBoundingBox.max, origin)};
    }
} // namespace sage

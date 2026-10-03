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

        void fillChunkVertexData(const Terrain& terrain, const ChunkRange& range, Mesh& mesh)
        {
            const float uvScale = terrain.cellSize / terrain.textureTileSize;
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
                    for (std::size_t layer = 0; layer < TERRAIN_TEXTURE_LAYERS; ++layer)
                    {
                        const float weight =
                            terrain.textureWeights.empty()
                                ? 0.0f
                                : terrain.textureWeights.at(
                                      static_cast<std::size_t>(row) * terrain.resolution + col)[layer];
                        mesh.colors[vertexIndex * 4 + layer] = static_cast<unsigned char>(weight * 255.0f);
                    }
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
                        for (std::size_t layer = 0; layer < TERRAIN_TEXTURE_LAYERS; ++layer)
                        {
                            const float weight =
                                terrain.textureWeights.empty()
                                    ? 0.0f
                                    : terrain.textureWeights.at(
                                          static_cast<std::size_t>(row) * terrain.resolution + col)[layer];
                            mesh.colors[vertex * 4 + layer] = static_cast<unsigned char>(weight * 255.0f);
                        }
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
        for (int chunkRow = 0; chunkRow < chunksPerSide(terrain); ++chunkRow)
            for (int chunkCol = 0; chunkCol < chunksPerSide(terrain); ++chunkCol)
            {
                auto& mesh = model.meshes[chunkRow * chunksPerSide(terrain) + chunkCol];
                fillChunkVertexData(terrain, getChunkRange(terrain, chunkRow, chunkCol), mesh);
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

    std::optional<Vector3> GetTerrainRayHit(const Terrain& terrain, const sgTransform& transform, const Ray& ray)
    {
        if (!terrain.IsValid()) return std::nullopt;

        const Matrix terrainToWorld = GetTerrainWorldMatrix(transform);
        if (std::abs(MatrixDeterminant(terrainToWorld)) < 1e-6f) return std::nullopt;

        const Matrix worldToTerrain = MatrixInvert(terrainToWorld);
        const Vector3 localOrigin = Vector3Transform(ray.position, worldToTerrain);
        const Vector3 localRayPoint = Vector3Transform(Vector3Add(ray.position, ray.direction), worldToTerrain);
        const Vector3 localDirection = Vector3Subtract(localRayPoint, localOrigin);
        if (Vector3LengthSqr(localDirection) < 1e-8f) return std::nullopt;

        const Ray localRay = {.position = localOrigin, .direction = Vector3Normalize(localDirection)};
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
                return Vector3Transform(
                    Vector3{.x = hit.x, .y = terrain.SampleHeight(hit.x, hit.z), .z = hit.z}, terrainToWorld);
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

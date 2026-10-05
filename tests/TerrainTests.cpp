#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/TerrainMesh.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr float RAY_HIT_TOLERANCE = 0.0002f;
    constexpr unsigned int RAY_TEST_SEED = 4517;
    constexpr int RANDOM_RAYS_PER_TRANSFORM = 500;

    void Check(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::vector<float> TerrainTriangleVertices(const sage::Terrain& terrain)
    {
        std::vector<float> vertices;
        const auto append = [&](const int row, const int col) {
            vertices.push_back(static_cast<float>(col) * terrain.cellSize);
            vertices.push_back(terrain.GetHeight(row, col));
            vertices.push_back(static_cast<float>(row) * terrain.cellSize);
        };
        for (int row = 0; row < terrain.resolution - 1; ++row)
            for (int col = 0; col < terrain.resolution - 1; ++col)
            {
                append(row, col);
                append(row + 1, col);
                append(row, col + 1);
                append(row, col + 1);
                append(row + 1, col);
                append(row + 1, col + 1);
            }
        return vertices;
    }

    void CheckTerrainRay(
        const sage::Terrain& terrain,
        const Mesh mesh,
        const Matrix transform,
        const Ray ray,
        const bool checkNormal)
    {
        const auto expected = GetRayCollisionMesh(ray, mesh, transform);
        const auto actual = sage::GetTerrainRayCollision(terrain, transform, ray);
        if (actual.has_value() != expected.hit)
            std::cerr << "Ray origin: " << ray.position.x << ", " << ray.position.y << ", " << ray.position.z
                      << "; direction: " << ray.direction.x << ", " << ray.direction.y << ", " << ray.direction.z
                      << "; expected hit: " << expected.hit << "; grid hit: " << actual.has_value() << '\n';
        Check(actual.has_value() == expected.hit, "Terrain grid picking disagrees with exhaustive mesh picking");
        if (!actual) return;
        Check(std::abs(actual->distance - expected.distance) < RAY_HIT_TOLERANCE, "Terrain hit distance changed");
        Check(Vector3Distance(actual->point, expected.point) < RAY_HIT_TOLERANCE, "Terrain hit position changed");
        if (checkNormal)
            Check(
                Vector3Distance(actual->normal, expected.normal) < RAY_HIT_TOLERANCE,
                "Terrain hit normal changed");
    }

    void TestRayPicking()
    {
        sage::Terrain terrain(9, 1.0f);
        auto vertices = TerrainTriangleVertices(terrain);
        Mesh mesh{};
        mesh.vertices = vertices.data();
        mesh.vertexCount = static_cast<int>(vertices.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        const std::array<Ray, 12> rays = {
            Ray{.position = {.x = 4.0f, .y = 10.0f, .z = 4.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}},
            Ray{.position = {.x = 4.0f, .y = -10.0f, .z = 4.0f}, .direction = {.x = 0.0f, .y = 1.0f, .z = 0.0f}},
            Ray{.position = {.x = 0.0f, .y = 10.0f, .z = 0.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}},
            Ray{.position = {.x = 8.0f, .y = 10.0f, .z = 8.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}},
            Ray{.position = {.x = 4.0f, .y = 0.01f, .z = 4.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}},
            Ray{.position = {.x = -1.0f, .y = 10.0f, .z = 4.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}},
            Ray{.position = {.x = 4.0f, .y = 10.0f, .z = 4.0f}, .direction = {.x = 0.0f, .y = 1.0f, .z = 0.0f}},
            Ray{.position = {.x = 4.0f, .y = 1.0f, .z = 4.0f}, .direction = {.x = 1.0f, .y = 0.0f, .z = 0.0f}},
            Ray{.position = {.x = 4.0f, .y = 1.0f, .z = 4.0f}, .direction = {.x = 0.0f, .y = 0.0f, .z = 0.0f}},
            Ray{.position = {.x = -1.0f, .y = 0.4f, .z = -1.0f},
                .direction = Vector3Normalize({.x = 1.0f, .y = -0.05f, .z = 1.0f})},
            Ray{.position = {.x = 9.0f, .y = 0.4f, .z = 9.0f},
                .direction = Vector3Normalize({.x = -1.0f, .y = -0.05f, .z = -1.0f})},
            Ray{.position = {.x = 4.0f, .y = 0.0f, .z = 4.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}}};
        for (const auto ray : rays)
            CheckTerrainRay(terrain, mesh, MatrixIdentity(), ray, false);

        // Non-planar cells distinguish the rendered diagonal from bilinear height sampling.
        std::mt19937 random(RAY_TEST_SEED);
        std::uniform_real_distribution<float> heights(-2.0f, 2.0f);
        for (auto& height : terrain.heights)
            height = heights(random);
        vertices = TerrainTriangleVertices(terrain);
        mesh.vertices = vertices.data();
        const std::array<Matrix, 3> transforms = {
            MatrixIdentity(),
            MatrixMultiply(
                MatrixMultiply(MatrixScale(2.0f, 0.7f, 3.0f), MatrixRotateY(0.7f)),
                MatrixTranslate(-13.0f, 5.0f, 7.0f)),
            MatrixMultiply(
                MatrixMultiply(MatrixScale(-1.5f, 2.0f, 0.8f), MatrixRotateX(0.3f)),
                MatrixTranslate(4.0f, -3.0f, 8.0f))};
        std::uniform_real_distribution<float> origins(-4.0f, 12.0f);
        std::uniform_real_distribution<float> directions(-1.0f, 1.0f);
        for (const auto transform : transforms)
        {
            for (const float x : {0.0f, 4.0f, 8.0f})
                for (const float z : {0.0f, 4.0f, 8.0f})
                {
                    const auto origin = Vector3Transform({.x = x, .y = 10.0f, .z = z}, transform);
                    const auto target = Vector3Transform({.x = x, .y = 0.0f, .z = z}, transform);
                    CheckTerrainRay(
                        terrain,
                        mesh,
                        transform,
                        {.position = origin, .direction = Vector3Normalize(Vector3Subtract(target, origin))},
                        false);
                }
            for (int index = 0; index < RANDOM_RAYS_PER_TRANSFORM; ++index)
            {
                const Vector3 localOrigin{.x = origins(random), .y = origins(random), .z = origins(random)};
                const Vector3 localDirection{
                    .x = directions(random), .y = directions(random), .z = directions(random)};
                const auto origin = Vector3Transform(localOrigin, transform);
                const auto target = Vector3Transform(Vector3Add(localOrigin, localDirection), transform);
                const Ray ray{.position = origin, .direction = Vector3Normalize(Vector3Subtract(target, origin))};
                CheckTerrainRay(terrain, mesh, transform, ray, true);
            }
        }

        // Shared chunk edges/corners and live height edits require no acceleration-cache rebuild.
        sage::Terrain large(129, 1.0f);
        large.SetHeight(64, 64, 2.0f);
        vertices = TerrainTriangleVertices(large);
        mesh.vertices = vertices.data();
        mesh.vertexCount = static_cast<int>(vertices.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        for (const float coordinate : {0.0f, 63.75f, 64.0f, 64.25f, 128.0f})
            CheckTerrainRay(
                large,
                mesh,
                MatrixIdentity(),
                {.position = {.x = coordinate, .y = 5.0f, .z = coordinate},
                 .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}},
                false);
        const std::array<Ray, 4> shallowRays = {
            Ray{.position = {.x = -1.0f, .y = 0.1f, .z = 64.1f},
                .direction = Vector3Normalize({.x = 1.0f, .y = -0.001f, .z = 0.0f})},
            Ray{.position = {.x = 129.0f, .y = 0.1f, .z = 64.1f},
                .direction = Vector3Normalize({.x = -1.0f, .y = -0.001f, .z = 0.0f})},
            Ray{.position = {.x = 64.1f, .y = 0.1f, .z = -1.0f},
                .direction = Vector3Normalize({.x = 0.0f, .y = -0.001f, .z = 1.0f})},
            Ray{.position = {.x = 64.1f, .y = 0.1f, .z = 129.0f},
                .direction = Vector3Normalize({.x = 0.0f, .y = -0.001f, .z = -1.0f})}};
        for (const auto ray : shallowRays)
            CheckTerrainRay(large, mesh, MatrixIdentity(), ray, true);
        large.SetHeight(64, 64, 3.0f);
        sage::sgTransform transform;
        const auto hit = sage::GetTerrainRayHit(
            large,
            transform,
            {.position = {.x = 64.0f, .y = 5.0f, .z = 64.0f}, .direction = {.x = 0.0f, .y = -1.0f, .z = 0.0f}});
        Check(hit && std::abs(hit->y - 3.0f) < 0.00001f, "Terrain picking did not use edited height data");
        Check(
            !sage::GetTerrainRayCollision(large, MatrixScale(0.0f, 1.0f, 1.0f), rays.front()),
            "Singular terrain transform accepted");
        large.heights.pop_back();
        Check(
            !sage::GetTerrainRayCollision(large, MatrixIdentity(), rays.front()),
            "Invalid terrain dimensions accepted");
    }

    void TestPainting()
    {
        sage::Terrain terrain(129, 1.0f);
        terrain.textures[0] = "resources/textures/terrain/Grass/Grass_01/Grass_01_basecolor.png";
        terrain.textures[3] = "resources/textures/terrain/Dirt/Dirt_01/Dirt_01_basecolor.png";
        const auto originalHeights = terrain.heights;
        // Cross both chunk boundaries, including their shared corner.
        const auto region = sage::ApplyTerrainTextureBrush(terrain, {64.0f, 64.0f}, 6.0f, 1.0f, 3, false);
        Check(
            region.minRow < 64 && region.maxRow > 64 && region.minCol < 64 && region.maxCol > 64,
            "Paint brush does not span chunk boundaries");
        const auto center = static_cast<std::size_t>(64 * terrain.resolution + 64);
        const float weight = terrain.textureWeights[center][3];
        Check(weight > 0.6f && weight < 0.7f, "Fourth texture layer was not painted");
        Check(terrain.textureWeights[0][3] == 0.0f, "Brush painted outside its radius");
        Check(terrain.textureWeights[center + 5][3] < weight, "Brush has no soft falloff");
        sage::ApplyTerrainTextureBrush(terrain, {64.0f, 64.0f}, 6.0f, 1.0f, 3, true);
        Check(terrain.textureWeights[center][3] < weight, "Erase did not remove selected layer");
        sage::ApplyTerrainTextureBrush(terrain, {64.0f, 64.0f}, 6.0f, 100.0f, 0, false);
        Check(
            terrain.textureWeights[center][0] > 0.99f && terrain.textureWeights[center][3] < 0.01f,
            "New paint did not replace previous blend");
        Check(
            terrain.heights == originalHeights && terrain.IsValid(),
            "Painting changed terrain geometry or validity");
        for (const auto& weights : terrain.textureWeights)
            Check(std::accumulate(weights.begin(), weights.end(), 0.0f) <= 1.0001f, "Paint weights exceed one");

        sage::Terrain whole(9, 1.0f);
        whole.textures[0] = terrain.textures[0];
        auto split = whole;
        sage::ApplyTerrainTextureBrush(whole, {4.0f, 4.0f}, 3.0f, 1.0f, 0, false);
        for (int i = 0; i < 10; ++i)
            sage::ApplyTerrainTextureBrush(split, {4.0f, 4.0f}, 3.0f, 0.1f, 0, false);
        for (std::size_t i = 0; i < whole.textureWeights.size(); ++i)
            Check(
                std::abs(whole.textureWeights[i][0] - split.textureWeights[i][0]) < 0.00001f,
                "Painting depends on frame rate");
        sage::ApplyTerrainTextureBrush(whole, {0.0f, 0.0f}, 3.0f, 1.0f, 0, false);
        Check(whole.textureWeights[0][0] > 0.0f && whole.IsValid(), "Painting at terrain edge failed");
    }

    void TestPersistence()
    {
        sage::Terrain terrain(9, 2.0f);
        terrain.textures[2] = "resources/textures/terrain/Sand/Sand_01/Sand_01_basecolor.png";
        terrain.textureTileSize = 7.5f;
        sage::ApplyTerrainTextureBrush(terrain, {8.0f, 8.0f}, 5.0f, 2.0f, 2, false);
        sage::Terrain loaded;
        sage::json::Decode(sage::json::Encode(terrain), loaded);
        Check(
            loaded.textures == terrain.textures && loaded.textureWeights == terrain.textureWeights &&
                loaded.textureTileSize == terrain.textureTileSize && loaded.IsValid(),
            "Paint JSON round trip failed");
        auto incomplete = sage::json::Encode(terrain);
        incomplete.RemoveMember("textures");
        bool rejected = false;
        try
        {
            sage::json::Decode(incomplete, loaded);
        }
        catch (const cereal::Exception&)
        {
            rejected = true;
        }
        Check(rejected, "Terrain documents without required texture fields were accepted");

        entt::registry registry;
        sage::TransformSystem transforms(&registry);
        const auto entity = registry.create();
        registry.emplace<sage::sgTransform>(entity);
        registry.emplace<sage::Terrain>(entity, terrain);
        auto document = sage::content::Capture(registry, {entity}, "map");
        constexpr std::array<std::string_view, 1> EXCLUDED_COMPONENTS{"sage.Terrain"};
        const auto snapshot =
            sage::content::Capture(registry, {entity}, "map", entt::null, true, EXCLUDED_COMPONENTS);
        Check(
            !sage::json::At(sage::json::Require(snapshot, "entities"), 0)["components"].HasMember("sage.Terrain"),
            "Excluded terrain was serialized in the history snapshot");
        Check(
            registry.get<sage::Terrain>(entity).textureWeights == terrain.textureWeights &&
                sage::json::At(sage::json::Require(document, "entities"), 0)["components"].HasMember(
                    "sage.Terrain"),
            "History capture changed the live terrain or normal map capture");
        Check(sage::content::Validate(document).empty(), "Painted map did not validate");
        const auto dependencies = sage::content::Dependencies(document);
        Check(
            std::ranges::find(dependencies, terrain.textures[2]) != dependencies.end(),
            "Terrain texture dependency missing");
        entt::registry restored;
        sage::TransformSystem restoredTransforms(&restored);
        sage::content::Instantiate(restored, document);
        const auto view = restored.view<sage::Terrain>();
        Check(!view.empty(), "Painted terrain missing after map restore");
        const auto& restoredTerrain = view.get<sage::Terrain>(*view.begin());
        Check(
            restoredTerrain.textureWeights == terrain.textureWeights &&
                restoredTerrain.textures == terrain.textures,
            "Map restore lost painted terrain");
        loaded = terrain;
        loaded.textureWeights.pop_back();
        Check(!loaded.IsValid(), "Invalid weight dimensions accepted");
        loaded = terrain;
        loaded.textureWeights[0][0] = 1.1f;
        Check(!loaded.IsValid(), "Invalid layer weight accepted");
        loaded = terrain;
        loaded.textureTileSize = 0.0f;
        Check(!loaded.IsValid(), "Zero texture tile size accepted");
    }
} // namespace

int main()
{
    try
    {
        TestPainting();
        TestPersistence();
        TestRayPicking();
        std::cout << "Terrain tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Terrain test failed: " << error.what() << '\n';
        return 1;
    }
}

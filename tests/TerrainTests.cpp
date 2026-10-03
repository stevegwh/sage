#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/TerrainMesh.hpp"

#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace
{
    void Check(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
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
        std::cout << "Terrain tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Terrain test failed: " << error.what() << '\n';
        return 1;
    }
}

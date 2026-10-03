#pragma once

#include "cereal/cereal.hpp"

#include "cereal/archives/json.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "raylib.h"

#include <array>
#include <string>
#include <type_traits>
#include <vector>

namespace sage
{
    inline constexpr std::size_t TERRAIN_TEXTURE_LAYERS = 4;

    // Editor-created height-field terrain. The grid is local to the owning
    // entity: vertex (row, col) sits at (col * cellSize, height, row * cellSize)
    // relative to the entity's world position. Y rotation and scale are supported. The DynamicRenderable mesh is
    // derived from this data and never serializes; loaders rebuild it (see engine/TerrainMesh.hpp).
    struct Terrain
    {
        int resolution = 129;       // vertices per side
        float cellSize = 1.0f;      // world units between adjacent vertices
        std::vector<float> heights; // resolution * resolution, row-major

        // Empty weights leave the original green terrain surface visible.
        std::array<std::string, TERRAIN_TEXTURE_LAYERS> textures{};
        std::vector<std::array<float, TERRAIN_TEXTURE_LAYERS>> textureWeights;
        float textureTileSize = 4.0f; // terrain-local units per repeat

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
        void save(Archive& archive) const
        {
            archive(
                cereal::make_nvp("resolution", resolution),
                cereal::make_nvp("cellSize", cellSize),
                cereal::make_nvp("heights", heights),
                cereal::make_nvp("textures", textures),
                cereal::make_nvp("textureWeights", textureWeights),
                cereal::make_nvp("textureTileSize", textureTileSize));
        }
        template <class Archive>
        void load(Archive& archive)
        {
            archive(
                cereal::make_nvp("resolution", resolution),
                cereal::make_nvp("cellSize", cellSize),
                cereal::make_nvp("heights", heights));
            textures = {};
            textureWeights.clear();
            textureTileSize = 4.0f;
            // Terrain documents authored before painting have no texture fields.
            // Only the missing first field is optional; incomplete paint data must fail.
            if constexpr (std::is_same_v<Archive, cereal::JSONInputArchive>)
            {
                try
                {
                    archive(cereal::make_nvp("textures", textures));
                }
                catch (const cereal::Exception& error)
                {
                    if (std::string(error.what()) != "JSON Parsing failed - provided NVP (textures) not found")
                        throw;
                    return;
                }
            }
            else
            {
                archive(cereal::make_nvp("textures", textures));
            }
            archive(
                cereal::make_nvp("textureWeights", textureWeights),
                cereal::make_nvp("textureTileSize", textureTileSize));
        }
    };
} // namespace sage

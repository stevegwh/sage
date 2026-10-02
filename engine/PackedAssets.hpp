#pragma once

#include "raylib-cereal.hpp"
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/vector.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sage
{
    inline constexpr int SAMPLER_NEAREST = 9728;
    inline constexpr int SAMPLER_LINEAR = 9729;
    inline constexpr int SAMPLER_FIRST_MIP_FILTER = 9984;
    inline constexpr int SAMPLER_LAST_MIP_FILTER = 9987;
    inline constexpr int SAMPLER_REPEAT = 10497;
    inline constexpr int SAMPLER_CLAMP = 33071;
    inline constexpr int SAMPLER_MIRROR = 33648;

    // CPU archive data. No GPU handles or allocations belong in these records.
    struct PackedImage
    {
        std::string extension;
        std::vector<unsigned char> bytes;
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(extension, bytes);
        }
    };

    struct PackedTexture
    {
        std::size_t image = 0;
        int minFilter = SAMPLER_NEAREST; // Standard OpenGL sampler enum values, also used by glTF.
        int magFilter = SAMPLER_NEAREST;
        int wrapS = SAMPLER_REPEAT;
        int wrapT = SAMPLER_REPEAT;
        bool operator==(const PackedTexture&) const = default;
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(image, minFilter, magFilter, wrapS, wrapT);
        }
    };

    struct PackedMaterialMap
    {
        std::optional<std::size_t> texture;
        Color color{};
        float value = 0;
        bool operator==(const PackedMaterialMap& other) const
        {
            return texture == other.texture && color.r == other.color.r && color.g == other.color.g &&
                   color.b == other.color.b && color.a == other.color.a && value == other.value;
        }
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(texture, color, value);
        }
    };

    struct PackedMaterial
    {
        std::string key;
        std::array<PackedMaterialMap, MAX_MATERIAL_MAPS> maps{};
        std::array<float, 4> params{};
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(key, maps, params);
        }
    };

    struct PackedMesh
    {
        std::vector<float> vertices, normals, texcoords, texcoords2, tangents;
        std::vector<unsigned char> colors, boneIds;
        std::vector<unsigned short> indices;
        std::vector<float> boneWeights;
        std::size_t material = 0; // Local model material slot.
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(
                vertices,
                normals,
                texcoords,
                texcoords2,
                tangents,
                colors,
                indices,
                boneIds,
                boneWeights,
                material);
        }
    };

    struct PackedAnimation
    {
        std::string name;
        std::vector<BoneInfo> bones;
        std::vector<std::vector<Transform>> frames;
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(name, bones, frames);
        }
    };

    struct PackedModel
    {
        std::string key, sourcePath;
        std::optional<std::string> primitive;
        std::vector<PackedMesh> meshes;
        std::vector<std::size_t> materials; // Shared archive material IDs.
        std::vector<BoneInfo> bones;
        std::vector<Transform> bindPose;
        std::vector<PackedAnimation> animations;
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(key, sourcePath, primitive, meshes, materials, bones, bindPose, animations);
        }
    };

    struct PackedAssets
    {
        std::vector<PackedImage> images;
        std::vector<PackedTexture> textures;
        std::vector<PackedMaterial> materials;
        std::vector<PackedModel> models;
        std::map<std::string, std::size_t> imageKeys; // Asset keys point to texture records.
        std::map<std::string, std::size_t> materialKeys;
        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(images, textures, materials, models, imageKeys, materialKeys);
        }
        void Validate() const;
    };

    Mesh CreatePackedMesh(const PackedMesh& source, std::size_t boneCount);
    ModelAnimation CreatePackedAnimation(const PackedAnimation& source);
} // namespace sage

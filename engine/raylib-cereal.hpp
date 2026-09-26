#pragma once

#include "RaylibMemory.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "magic_enum.hpp"
#include "raylib.h"
#include "raylib/src/config.h"
#include "raymath.h"
#include "rlgl.h"
#include <array>
#include <cstring>

template <typename Archive>
void serialize(Archive& archive, Vector2& v2)
{
    archive(cereal::make_nvp("x", v2.x), cereal::make_nvp("y", v2.y));
};

template <typename Archive>
void serialize(Archive& archive, Vector3& v3)
{
    archive(cereal::make_nvp("x", v3.x), cereal::make_nvp("y", v3.y), cereal::make_nvp("z", v3.z));
};

template <typename Archive>
void serialize(Archive& archive, Vector4& v4)
{
    archive(cereal::make_nvp("x", v4.x), cereal::make_nvp("y", v4.y), cereal::make_nvp("z", v4.z), cereal::make_nvp("w", v4.w));
};

template <typename Archive>
void serialize(Archive& archive, Transform& transform)
{
    archive(cereal::make_nvp("translation", transform.translation), cereal::make_nvp("rotation", transform.rotation), cereal::make_nvp("scale", transform.scale));
};

template <typename Archive>
void serialize(Archive& archive, Matrix& m)
{
    archive(cereal::make_nvp("m0", m.m0), cereal::make_nvp("m1", m.m1), cereal::make_nvp("m2", m.m2), cereal::make_nvp("m3", m.m3), cereal::make_nvp("m4", m.m4), cereal::make_nvp("m5", m.m5), cereal::make_nvp("m6", m.m6), cereal::make_nvp("m7", m.m7), cereal::make_nvp("m8", m.m8), cereal::make_nvp("m9", m.m9), cereal::make_nvp("m10", m.m10), cereal::make_nvp("m11", m.m11), cereal::make_nvp("m12", m.m12), cereal::make_nvp("m13", m.m13), cereal::make_nvp("m14", m.m14), cereal::make_nvp("m15", m.m15));
};

template <typename Archive>
void serialize(Archive& archive, BoundingBox& bb)
{
    archive(cereal::make_nvp("min", bb.min), cereal::make_nvp("max", bb.max));
};

template <typename Archive>
void save(Archive& archive, ModelAnimation const& modelAnimation)
{
    std::vector<BoneInfo> bones(modelAnimation.bones, modelAnimation.bones + modelAnimation.boneCount);
    std::vector<std::vector<Transform>> framePoses(modelAnimation.frameCount);

    for (int i = 0; i < modelAnimation.frameCount; i++)
    {
        framePoses[i].assign(
            modelAnimation.framePoses[i], modelAnimation.framePoses[i] + modelAnimation.boneCount);
    }

    archive(cereal::make_nvp("modelAnimation.boneCount", modelAnimation.boneCount), cereal::make_nvp("modelAnimation.frameCount", modelAnimation.frameCount), cereal::make_nvp("bones", bones), cereal::make_nvp("framePoses", framePoses), cereal::make_nvp("modelAnimation.name", modelAnimation.name));
}

template <typename Archive>
void load(Archive& archive, ModelAnimation& modelAnimation)
{
    std::vector<BoneInfo> bones;
    std::vector<std::vector<Transform>> framePoses;

    archive(cereal::make_nvp("modelAnimation.boneCount", modelAnimation.boneCount), cereal::make_nvp("modelAnimation.frameCount", modelAnimation.frameCount), cereal::make_nvp("bones", bones), cereal::make_nvp("framePoses", framePoses), cereal::make_nvp("modelAnimation.name", modelAnimation.name));

    modelAnimation.bones = static_cast<BoneInfo*>(MemAlloc(modelAnimation.boneCount * sizeof(BoneInfo)));
    std::copy(bones.begin(), bones.end(), modelAnimation.bones);

    modelAnimation.framePoses =
        static_cast<Transform**>(MemAlloc(modelAnimation.frameCount * sizeof(Transform*)));
    for (int i = 0; i < modelAnimation.frameCount; i++)
    {
        modelAnimation.framePoses[i] =
            static_cast<Transform*>(MemAlloc(modelAnimation.boneCount * sizeof(Transform)));
        std::copy(framePoses[i].begin(), framePoses[i].end(), modelAnimation.framePoses[i]);
    }
}

template <typename Archive>
void save(Archive& archive, Mesh const& mesh)
{
    std::vector<float> vertices(mesh.vertices, mesh.vertices + mesh.vertexCount * 3); // vec3

    std::vector<float> texcoords;
    if (mesh.texcoords)
    {
        texcoords.assign(mesh.texcoords, mesh.texcoords + mesh.vertexCount * 2); // vec2
    }

    std::vector<float> texcoords2;
    if (mesh.texcoords2)
    {
        texcoords2.assign(mesh.texcoords2, mesh.texcoords2 + mesh.vertexCount * 2); // vec2
    }

    std::vector<float> normals;
    if (mesh.normals)
    {
        normals.assign(mesh.normals, mesh.normals + mesh.vertexCount * 3); // vec3
    }

    std::vector<float> tangents;
    if (mesh.tangents)
    {
        tangents.assign(mesh.tangents, mesh.tangents + mesh.vertexCount * 4); // vec4
    }

    std::vector<unsigned char> colors;
    if (mesh.colors)
    {
        colors.assign(mesh.colors, mesh.colors + mesh.vertexCount * 4); // vec4
    }

    std::vector<unsigned short> indices;
    if (mesh.indices)
    {
        indices.assign(mesh.indices, mesh.indices + mesh.triangleCount * 3);
    }

    // Animations
    std::vector<unsigned char> boneIds;
    if (mesh.boneIds)
    {
        boneIds.assign(mesh.boneIds, mesh.boneIds + mesh.vertexCount * 4);
    }
    std::vector<float> boneWeights;
    if (mesh.boneWeights)
    {
        boneWeights.assign(mesh.boneWeights, mesh.boneWeights + mesh.vertexCount * 4); // vec4
    }

    archive(cereal::make_nvp("mesh.vertexCount", mesh.vertexCount), cereal::make_nvp("mesh.triangleCount", mesh.triangleCount), cereal::make_nvp("mesh.boneCount", mesh.boneCount), cereal::make_nvp("vertices", vertices), cereal::make_nvp("texcoords", texcoords), cereal::make_nvp("texcoords2", texcoords2), cereal::make_nvp("normals", normals), cereal::make_nvp("tangents", tangents), cereal::make_nvp("colors", colors), cereal::make_nvp("indices", indices), cereal::make_nvp("boneIds", boneIds), cereal::make_nvp("boneWeights", boneWeights));
}

template <typename Archive>
void load(Archive& archive, Mesh& mesh)
{
    std::vector<float> vertices;
    std::vector<float> texcoords;
    std::vector<float> texcoords2;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<unsigned char> colors;
    std::vector<unsigned short> indices;
    std::vector<unsigned char> boneIds;
    std::vector<float> boneWeights;

    archive(cereal::make_nvp("mesh.vertexCount", mesh.vertexCount), cereal::make_nvp("mesh.triangleCount", mesh.triangleCount), cereal::make_nvp("mesh.boneCount", mesh.boneCount), cereal::make_nvp("vertices", vertices), cereal::make_nvp("texcoords", texcoords), cereal::make_nvp("texcoords2", texcoords2), cereal::make_nvp("normals", normals), cereal::make_nvp("tangents", tangents), cereal::make_nvp("colors", colors), cereal::make_nvp("indices", indices), cereal::make_nvp("boneIds", boneIds), cereal::make_nvp("boneWeights", boneWeights));

    bool animations = !boneIds.empty();

    mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
    std::memcpy(mesh.vertices, vertices.data(), mesh.vertexCount * 3 * sizeof(float));

    if (!texcoords.empty())
    {
        mesh.texcoords = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
        std::memcpy(mesh.texcoords, texcoords.data(), mesh.vertexCount * 2 * sizeof(float));
    }
    if (!texcoords2.empty())
    {
        mesh.texcoords2 = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
        std::memcpy(mesh.texcoords2, texcoords2.data(), mesh.vertexCount * 2 * sizeof(float));
    }
    if (!normals.empty())
    {
        mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
        std::memcpy(mesh.normals, normals.data(), mesh.vertexCount * 3 * sizeof(float));
    }
    if (!tangents.empty())
    {
        mesh.tangents = static_cast<float*>(MemAlloc(mesh.vertexCount * 4 * sizeof(float)));
        std::memcpy(mesh.tangents, tangents.data(), mesh.vertexCount * 4 * sizeof(float));
    }
    if (!colors.empty())
    {
        mesh.colors = static_cast<unsigned char*>(MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char)));
        std::memcpy(mesh.colors, colors.data(), mesh.vertexCount * 4 * sizeof(unsigned char));
    }
    if (!indices.empty())
    {
        mesh.indices = static_cast<unsigned short*>(MemAlloc(mesh.triangleCount * 3 * sizeof(unsigned short)));
        std::memcpy(mesh.indices, indices.data(), mesh.triangleCount * 3 * sizeof(unsigned short));
    }

    // Animations
    if (animations)
    {
        mesh.animVertices = static_cast<float*>(sage::AllocateZeroedMemory(mesh.vertexCount * 3, sizeof(float)));
        std::memcpy(mesh.animVertices, vertices.data(), mesh.vertexCount * 3 * sizeof(float));
        mesh.animNormals = static_cast<float*>(sage::AllocateZeroedMemory(mesh.vertexCount * 3, sizeof(float)));
        std::memcpy(mesh.animNormals, normals.data(), mesh.vertexCount * 3 * sizeof(float));

        mesh.boneIds = static_cast<unsigned char*>(sage::AllocateZeroedMemory(mesh.vertexCount * 4, sizeof(unsigned char)));
        std::memcpy(mesh.boneIds, boneIds.data(), mesh.vertexCount * 4 * sizeof(unsigned char));

        mesh.boneWeights = static_cast<float*>(sage::AllocateZeroedMemory(mesh.vertexCount * 4, sizeof(float)));
        std::memcpy(mesh.boneWeights, boneWeights.data(), mesh.vertexCount * 4 * sizeof(float));

        mesh.boneMatrices = static_cast<Matrix*>(sage::AllocateZeroedMemory(mesh.boneCount, sizeof(Matrix)));
        for (int j = 0; j < mesh.boneCount; j++)
        {
            mesh.boneMatrices[j] = MatrixIdentity();
            // Gets updated per animation, no need to copy info over.
        }
    }
};

// Image is serialized PNG-encoded when its format is an 8-bit uncompressed layout
// (the formats PNG can express), otherwise as raw pixel bytes. Encoded images are
// dramatically smaller for typical color/icon textures; raw fallback handles
// compressed/HDR/empty images. A boolean prefix tells the loader which form to expect.
template <typename Archive>
void save(Archive& archive, Image const& image)
{
    const bool canEncode = (image.data != nullptr) && image.width > 0 && image.height > 0 &&
                           image.format >= PIXELFORMAT_UNCOMPRESSED_GRAYSCALE &&
                           image.format <= PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    if (canEncode)
    {
        int encodedSize = 0;
        unsigned char* encoded = ExportImageToMemory(image, ".png", &encodedSize);
        if (encoded != nullptr && encodedSize > 0)
        {
            std::vector<unsigned char> data(encoded, encoded + encodedSize);
            MemFree(encoded);
            bool isEncoded = true;
            archive(cereal::make_nvp("isEncoded", isEncoded), cereal::make_nvp("data", data));
            return;
        }
        if (encoded != nullptr) MemFree(encoded);
        // Fall through to raw path on encode failure.
    }

    bool isEncoded = false;
    int len = (image.data != nullptr) ? GetPixelDataSize(image.width, image.height, image.format) : 0;
    std::vector<unsigned char> data(
        static_cast<unsigned char*>(image.data), static_cast<unsigned char*>(image.data) + len);
    archive(cereal::make_nvp("isEncoded", isEncoded), cereal::make_nvp("image.format", image.format), cereal::make_nvp("image.height", image.height), cereal::make_nvp("image.width", image.width), cereal::make_nvp("image.mipmaps", image.mipmaps), cereal::make_nvp("data", data));
}

template <typename Archive>
void load(Archive& archive, Image& image)
{
    bool isEncoded = false;
    archive(cereal::make_nvp("isEncoded", isEncoded));
    if (isEncoded)
    {
        std::vector<unsigned char> data;
        archive(cereal::make_nvp("data", data));
        image = LoadImageFromMemory(".png", data.data(), static_cast<int>(data.size()));
        assert(image.data != nullptr && "raylib-cereal: PNG decode failed (corrupt bin?)");
        return;
    }
    std::vector<unsigned char> data;
    archive(cereal::make_nvp("image.format", image.format), cereal::make_nvp("image.height", image.height), cereal::make_nvp("image.width", image.width), cereal::make_nvp("image.mipmaps", image.mipmaps), cereal::make_nvp("data", data));
    int len = GetPixelDataSize(image.width, image.height, image.format);
    image.data = static_cast<unsigned char*>(MemAlloc(len * sizeof(unsigned char)));
    if (len > 0) std::memcpy(image.data, data.data(), len * sizeof(unsigned char));
}

template <typename Archive>
void save(Archive& archive, Shader const& shader)
{
    std::vector<int> locs(shader.locs, shader.locs + RL_MAX_SHADER_LOCATIONS);
    archive(cereal::make_nvp("shader.id", shader.id), cereal::make_nvp("locs", locs));
};

template <typename Archive>
void load(Archive& archive, Shader& shader)
{
    std::vector<int> locs;
    archive(cereal::make_nvp("shader.id", shader.id), cereal::make_nvp("locs", locs));
    shader.locs = static_cast<int*>(MemAlloc(RL_MAX_SHADER_LOCATIONS * sizeof(int)));
    std::memcpy(shader.locs, locs.data(), RL_MAX_SHADER_LOCATIONS * sizeof(int));
};

template <typename Archive>
void serialize(Archive& archive, Color& color)
{
    archive(cereal::make_nvp("r", color.r), cereal::make_nvp("g", color.g), cereal::make_nvp("b", color.b), cereal::make_nvp("a", color.a));
};

template <typename Archive>
void save(Archive& archive, MaterialMap const& map)
{
    Image image{};
    image.format = map.texture.format;

    if (map.texture.format < PIXELFORMAT_COMPRESSED_DXT1_RGB && map.texture.id != rlGetTextureIdDefault() &&
        map.texture.id != 0)
    {
        image = LoadImageFromTexture(map.texture);
    }

    archive(cereal::make_nvp("image", image), cereal::make_nvp("map.color", map.color), cereal::make_nvp("map.value", map.value));
    UnloadImage(image);
};

template <typename Archive>
void load(Archive& archive, MaterialMap& map)
{
    Image image;
    archive(cereal::make_nvp("image", image), cereal::make_nvp("map.color", map.color), cereal::make_nvp("map.value", map.value));
    if (!image.data || (image.width == 0 && image.height == 0) || image.format >= PIXELFORMAT_COMPRESSED_DXT1_RGB)
    {
        map.texture = Texture2D{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

        if (image.data)
        {
            UnloadImage(image);
        }

        return;
    }
    map.texture = LoadTextureFromImage(image);
    UnloadImage(image);
};

template <typename Archive>
void save(Archive& archive, Material const& material)
{
    std::vector<MaterialMap> maps;
    maps.resize(MAX_MATERIAL_MAPS);
    std::array<float, 4> params{};

    for (size_t i = 0; i < MAX_MATERIAL_MAPS; i++)
    {
        if (maps[i].texture.format >= PIXELFORMAT_COMPRESSED_DXT1_RGB ||
            maps[i].texture.id == rlGetTextureIdDefault())
            continue;
        maps[i] = material.maps[i];
    }
    for (size_t i = 0; i < 4; i++)
    {
        params[i] = material.params[i];
    }
    archive(cereal::make_nvp("maps", maps), cereal::make_nvp("params", params));
};

template <typename Archive>
void load(Archive& archive, Material& material)
{
    std::vector<MaterialMap> maps;
    maps.resize(MAX_MATERIAL_MAPS);
    std::array<float, 4> params{};

    archive(cereal::make_nvp("maps", maps), cereal::make_nvp("params", params));

    material = LoadMaterialDefault();
    //  material.maps[MATERIAL_MAP_DIFFUSE] = maps.at(0);
    std::memcpy(material.maps, maps.data(), MAX_MATERIAL_MAPS * sizeof(MaterialMap));
};

template <typename Archive>
void serialize(Archive& archive, BoneInfo& boneInfo)
{
    archive(cereal::make_nvp("boneInfo.name", boneInfo.name), cereal::make_nvp("boneInfo.parent", boneInfo.parent));
};

template <typename Archive>
void save(Archive& archive, Model const& model)
{
    std::vector<Mesh> meshes(model.meshes, model.meshes + model.meshCount);
    // std::vector<Material> materials(model.materials, model.materials+model.materialCount);
    std::vector<BoneInfo> bones(model.bones, model.bones + model.boneCount);
    std::vector<Transform> bindPose(model.bindPose, model.bindPose + model.boneCount);
    std::vector<int> meshMaterial(model.meshMaterial, model.meshMaterial + model.meshCount);

    archive(
        model.transform,
        model.meshCount,
        model.materialCount,
        meshes,
        // materials,
        meshMaterial,
        model.boneCount,
        bones,
        bindPose);
};

template <typename Archive>
void load(Archive& archive, Model& model)
{
    std::vector<Mesh> meshes;
    // std::vector<Material> materials;
    std::vector<int> meshMaterial;
    std::vector<BoneInfo> bones;
    std::vector<Transform> bindPose;

    archive(
        model.transform,
        model.meshCount,
        model.materialCount,
        meshes,
        // materials,
        meshMaterial,
        model.boneCount,
        bones,
        bindPose);

    model.meshes = static_cast<Mesh*>(sage::AllocateZeroedMemory(model.meshCount, sizeof(Mesh)));
    model.materials = static_cast<Material*>(sage::AllocateZeroedMemory(model.materialCount, sizeof(Material)));

    // for (unsigned int i = 0; i < model.materialCount; ++i)
    // {
    //     model.materials[i] = LoadMaterialDefault();
    // }

    model.meshMaterial = static_cast<int*>(sage::AllocateZeroedMemory(model.meshCount, sizeof(int)));
    model.bones = static_cast<BoneInfo*>(MemAlloc(model.boneCount * sizeof(BoneInfo)));
    model.bindPose = static_cast<Transform*>(MemAlloc(model.boneCount * sizeof(Transform)));

    std::memcpy(model.meshes, meshes.data(), model.meshCount * sizeof(Mesh));
    // std::memcpy(model.materials, materials.data(), model.materialCount * sizeof(Material);
    std::memcpy(model.meshMaterial, meshMaterial.data(), model.meshCount * sizeof(int));
    std::memcpy(model.bones, bones.data(), model.boneCount * sizeof(BoneInfo));
    std::memcpy(model.bindPose, bindPose.data(), model.boneCount * sizeof(Transform));

    // Below taken from raylib's LoadModel().
    model.transform = MatrixIdentity();
    if ((model.meshCount != 0) && (model.meshes != nullptr))
    {
        // Upload vertex data to GPU (static meshes)
        for (int i = 0; i < model.meshCount; i++)
            UploadMesh(&model.meshes[i], false);
    }
    else
        TraceLog(LOG_WARNING, "MESH: [%s] Failed to load model mesh(es) data", "Cereal Model Import");
};
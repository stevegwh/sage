#include "PackedAssets.hpp"
#include "raymath.h"

#include <algorithm>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>

namespace sage
{
    namespace
    {
        template <typename T>
        T* CopyArray(const std::vector<T>& values)
        {
            if (values.empty()) return nullptr; // raylib requires null for absent attributes.
            auto* data = static_cast<T*>(MemAlloc(static_cast<unsigned int>(values.size() * sizeof(T))));
            if (!data) throw std::bad_alloc();
            std::ranges::copy(values, data);
            return data;
        }
        void Require(const bool valid)
        {
            if (!valid) throw std::runtime_error("Invalid packed asset references or dimensions");
        }
    } // namespace

    void PackedAssets::Validate() const
    {
        std::set<std::string> materialNames, modelNames;
        for (const auto& image : images)
            Require(
                !image.bytes.empty() &&
                image.bytes.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                !image.extension.empty());
        for (const auto& texture : textures)
        {
            Require(texture.image < images.size());
            Require(
                texture.minFilter == SAMPLER_NEAREST || texture.minFilter == SAMPLER_LINEAR ||
                (texture.minFilter >= SAMPLER_FIRST_MIP_FILTER && texture.minFilter <= SAMPLER_LAST_MIP_FILTER));
            Require(texture.magFilter == SAMPLER_NEAREST || texture.magFilter == SAMPLER_LINEAR);
            const auto validWrap = [](int wrap) {
                return wrap == SAMPLER_REPEAT || wrap == SAMPLER_CLAMP || wrap == SAMPLER_MIRROR;
            };
            Require(validWrap(texture.wrapS) && validWrap(texture.wrapT));
        }
        for (const auto& [key, id] : imageKeys)
            Require(!key.empty() && id < textures.size());
        for (const auto& [key, id] : materialKeys)
            Require(!key.empty() && id < materials.size());
        for (const auto& material : materials)
        {
            Require(!material.key.empty() && materialNames.insert(material.key).second);
            for (const auto& map : material.maps)
                Require(!map.texture || *map.texture < textures.size());
        }
        for (const auto& model : models)
        {
            Require(!model.key.empty() && modelNames.insert(model.key).second && !model.materials.empty());
            Require(model.primitive.has_value() != !model.meshes.empty());
            if (model.primitive)
            {
                const std::set<std::string> primitives = {
                    "primitive_sphere",
                    "primitive_hemisphere",
                    "primitive_plane",
                    "primitive_cube",
                    "primitive_cylinder",
                    "primitive_cone",
                    "primitive_torus",
                    "primitive_knot",
                    "primitive_poly"};
                Require(primitives.contains(*model.primitive) && model.materials.size() == 1);
            }
            Require(model.bones.size() == model.bindPose.size());
            for (const auto material : model.materials)
                Require(material < materials.size());
            for (const auto& mesh : model.meshes)
            {
                const auto count = mesh.vertices.size() / 3;
                Require(
                    count > 0 && count <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                    mesh.vertices.size() % 3 == 0 && mesh.material < model.materials.size());
                Require(mesh.indices.empty() ? count % 3 == 0 : mesh.indices.size() % 3 == 0);
                const auto attribute = [count](const auto& values, const std::size_t components) {
                    return values.empty() || values.size() == count * components;
                };
                Require(
                    attribute(mesh.normals, 3) && attribute(mesh.texcoords, 2) && attribute(mesh.texcoords2, 2));
                Require(attribute(mesh.tangents, 4) && attribute(mesh.colors, 4));
                Require(attribute(mesh.boneIds, 4) && attribute(mesh.boneWeights, 4));
                Require(mesh.boneIds.empty() == mesh.boneWeights.empty());
                for (const auto index : mesh.indices)
                    Require(index < count);
                for (const auto bone : mesh.boneIds)
                    Require(bone < model.bones.size());
            }
            for (const auto& animation : model.animations)
            {
                Require(animation.bones.size() == model.bones.size() && !animation.frames.empty());
                for (const auto& frame : animation.frames)
                    Require(frame.size() == model.bones.size());
            }
        }
    }

    Mesh CreatePackedMesh(const PackedMesh& source, const std::size_t boneCount)
    {
        Mesh mesh{};
        mesh.vertexCount = static_cast<int>(source.vertices.size() / 3);
        mesh.triangleCount =
            static_cast<int>((source.indices.empty() ? source.vertices.size() / 3 : source.indices.size()) / 3);
        mesh.vertices = CopyArray(source.vertices);
        mesh.normals = CopyArray(source.normals);
        mesh.texcoords = CopyArray(source.texcoords);
        mesh.texcoords2 = CopyArray(source.texcoords2);
        mesh.tangents = CopyArray(source.tangents);
        mesh.colors = CopyArray(source.colors);
        mesh.indices = CopyArray(source.indices);
        mesh.boneCount = static_cast<int>(boneCount);
        if (!source.boneIds.empty())
        {
            mesh.boneIds = CopyArray(source.boneIds);
            mesh.boneWeights = CopyArray(source.boneWeights);
            mesh.animVertices = CopyArray(source.vertices);
            mesh.animNormals =
                CopyArray(source.normals.empty() ? std::vector<float>(source.vertices.size(), 0) : source.normals);
            mesh.boneMatrices = CopyArray(std::vector<Matrix>(boneCount, MatrixIdentity()));
        }
        UploadMesh(&mesh, false);
        return mesh;
    }

    ModelAnimation CreatePackedAnimation(const PackedAnimation& source)
    {
        ModelAnimation animation{};
        animation.boneCount = static_cast<int>(source.bones.size());
        animation.frameCount = static_cast<int>(source.frames.size());
        const auto length = std::min(source.name.size(), sizeof(animation.name) - 1);
        std::memcpy(std::span(animation.name).data(), source.name.data(), length);
        animation.bones = CopyArray(source.bones);
        std::vector<Transform*> frames;
        for (const auto& frame : source.frames)
            frames.push_back(CopyArray(frame));
        animation.framePoses = CopyArray(frames);
        return animation;
    }
} // namespace sage

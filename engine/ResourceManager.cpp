//
// Created by Steve Wheeler on 16/07/2024.
//

#include "ResourceManager.hpp"
#include "AssetKey.hpp"
#include "ShaderPaths.hpp"

#include "components/Renderable.hpp"

#include "raylib/src/config.h"
#include "raymath.h"
#include "rlgl.h"
// stb_include requires an implementation switch in exactly one translation unit.
#define STB_INCLUDE_IMPLEMENTATION
// Its default C-style #line filenames are rejected by our GLSL compiler.
#define STB_INCLUDE_LINE_NONE

#include <algorithm>
#include <ranges>
#include <stb_include.h>
#include <stdexcept>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace sage
{
    namespace
    {

        void RegisterSourcePath(
            std::unordered_map<std::string, std::string>& sources,
            const std::string& kind,
            const std::string& key,
            const std::string& path)
        {
            if (const auto existing = sources.find(key); existing != sources.end())
            {
                if (existing->second == path) return;
                throw std::runtime_error(
                    kind + " resource key collision for '" + key + "': '" + existing->second + "' and '" + path +
                    "'");
            }
            sources.emplace(key, path);
        }

        template <typename Assets>
        void BuildAliases(const Assets& assets, std::unordered_map<std::string, std::string>& aliases)
        {
            aliases.clear();
            std::unordered_map<std::string, int> counts;
            for (const auto& [key, asset] : assets)
            {
                if (key.find('/') == std::string::npos) continue;
                const auto name = AssetNameFromKey(key);
                ++counts[name];
                aliases[name] = key;
            }
            for (const auto& [name, count] : counts)
                if (count != 1) aliases.erase(name);
        }

        std::string ResolveKey(const std::string& key, const std::unordered_map<std::string, std::string>& aliases)
        {
            if (const auto found = aliases.find(key); found != aliases.end()) return found->second;
            return key;
        }

    } // namespace

    Shader ResourceManager::gpuShaderLoad(const char* vs, const char* fs)
    {
        std::string vs_str = vs == nullptr ? "" : std::string(vs);
        std::string fs_str = fs == nullptr ? "" : std::string(fs);
        std::string concat = vs_str + fs_str;

        if (!shaders.contains(concat))
        {
            shaders[concat] = LoadShaderFromMemory(vs, fs);
        }

        return shaders[concat];
    }

    Music ResourceManager::GetMusic(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
                                    // from QUEST_BONE.obj)
        if (!music.contains(key))
        {
            RegisterSourcePath(musicSourcePaths, "Music", key, path);
            music[key] = LoadMusicStream(path.c_str());
        }
        else
            RegisterSourcePath(musicSourcePaths, "Music", key, path);
        return music.at(key);
    }

    Sound ResourceManager::GetSFX(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
                                    // from QUEST_BONE.obj)
        if (!sfx.contains(key))
        {
            RegisterSourcePath(sfxSourcePaths, "Sound", key, path);
            // NB: Currently, the resource packer does not support serializing sound/music.
            sfx[key] = LoadSound(path.c_str());
        }
        else
            RegisterSourcePath(sfxSourcePaths, "Sound", key, path);
        return sfx.at(key);
    }

    /*
    * @brief Stores the shader's text file in memory, saving on reading the file multiple
    times.
     *
     * @param vShaderStr
     * @param fShaderStr
     * @return Shader
    */
    Shader ResourceManager::ShaderLoad(
        const std::optional<std::string>& vsFileName, const std::optional<std::string>& fsFileName)
    {
        const bool noShaderFiles = !vsFileName && !fsFileName;
        const bool vertexShaderMissing = vsFileName && !FileExists(vsFileName->c_str());
        const bool fragmentShaderMissing = fsFileName && !FileExists(fsFileName->c_str());
        if (noShaderFiles || vertexShaderMissing || fragmentShaderMissing)
        {
            std::cout << "WARNING: Requested shader files do not exist. Loading default shader. \n";
            return shaders["DEFAULT"];
        }

        auto shaderIncludePath = ShaderPath("custom/include");
        const auto sourceText = [&](const std::optional<std::string>& path,
                                    auto& cache) -> std::optional<std::reference_wrapper<const std::string>> {
            if (!path) return std::nullopt;
            if (!cache.contains(*path))
            {
                // raylib and stb own their respective C allocations; keep each matching deleter.
                const std::unique_ptr<char, decltype(&UnloadFileText)> source(
                    LoadFileText(path->c_str()), UnloadFileText);
                std::optional<std::string> text;
                if (source)
                {
                    const std::unique_ptr<char, decltype(&std::free)> preprocessed(
                        stb_include_string(source.get(), nullptr, shaderIncludePath.data(), nullptr, nullptr),
                        std::free);
                    if (preprocessed) text = preprocessed.get();
                }
                cache.emplace(*path, std::move(text));
            }
            const auto& cached = cache.at(*path);
            return cached ? std::make_optional(std::cref(*cached)) : std::nullopt;
        };
        const auto vertexSource = sourceText(vsFileName, vertShaderFileText);
        const auto fragmentSource = sourceText(fsFileName, fragShaderFileText);
        return gpuShaderLoad(
            vertexSource ? vertexSource->get().c_str() : nullptr,
            fragmentSource ? fragmentSource->get().c_str() : nullptr);
    }

    Shader ResourceManager::ShaderLoadUnique(
        const std::optional<std::string>& vsFileName, const std::optional<std::string>& fsFileName)
    {
        const bool noShaderFiles = !vsFileName && !fsFileName;
        const bool vertexShaderMissing = vsFileName && !FileExists(vsFileName->c_str());
        const bool fragmentShaderMissing = fsFileName && !FileExists(fsFileName->c_str());
        if (noShaderFiles || vertexShaderMissing || fragmentShaderMissing) return {};

        // ShaderLoad performs and caches the include preprocessing. Only the GPU
        // program below is unique; source text can remain shared.
        static_cast<void>(ShaderLoad(vsFileName, fsFileName));
        const auto vertexSource = vsFileName ? vertShaderFileText.at(*vsFileName) : std::nullopt;
        const auto fragmentSource = fsFileName ? fragShaderFileText.at(*fsFileName) : std::nullopt;
        return LoadShaderFromMemory(
            vertexSource ? vertexSource->c_str() : nullptr, fragmentSource ? fragmentSource->c_str() : nullptr);
    }

    Texture ResourceManager::TextureLoad(const std::string& path)
    {
        const auto requestedKey = AssetKeyForPath(path);
        const auto key = requestedKey.find('/') != std::string::npos || images.contains(requestedKey) ||
                                 packedImageKeys.contains(requestedKey)
                             ? requestedKey
                             : ResolveImageKey(StripPath(path));
        if (packedImageKeys.contains(key)) return LoadPackedTexture(packedImageKeys.at(key));
        if (!nonModelTextures.contains(key))
        {
            if (!images.contains(key))
            {
                const auto fileKey = AssetKeyForPath(path);
                registerImageKey(fileKey, path);
                images.emplace(fileKey, LoadImage(path.c_str()));
                RebuildAssetAliases();
                nonModelTextures[fileKey] = LoadTextureFromImage(images[fileKey]);
                return nonModelTextures[fileKey];
            }
            nonModelTextures[key] = LoadTextureFromImage(images[key]);
        }
        return nonModelTextures[key];
    }

    Texture ResourceManager::TextureLoadFromImage(const std::string& name, Image image)
    {
        if (!images.contains(name))
        {
            registerImageKey(name, name);
            images.emplace(name, image);
            nonModelTextures[name] = LoadTextureFromImage(images[name]);
        }
        return nonModelTextures[name];
    }

    Font ResourceManager::FontLoad(const std::string& path)
    {
        // assert(fonts.contains(path));
        if (!fonts.contains(path))
        {
            FontLoadFromFile(path);
        }
        return fonts[path];
    }

    void ResourceManager::ImageUnload(const std::string& key)
    {
        const auto resolved = ResolveImageKey(key);
        if (packedImageKeys.erase(resolved))
        {
            RebuildAssetAliases();
            return;
        }
        if (images.contains(resolved))
        {
            UnloadImage(images.at(resolved));
            images.erase(resolved);
            imageSourcePaths.erase(resolved);
            RebuildAssetAliases();
        }
    }

    ImageSafe ResourceManager::GetImage(const std::string& key)
    {
        const auto resolved = ResolveImageKey(key);
        if (packedImageKeys.contains(resolved))
            return ImageSafe(packedImages.at(packedTextureData.at(packedImageKeys.at(resolved)).image), false);
        assert(images.contains(resolved));
        return ImageSafe(images.at(resolved), false);
    }

    void ResourceManager::FontLoadFromFile(const std::string& path)
    {
        assert(FileExists(path.c_str()));
        if (!fonts.contains(path))
        {
            auto font = LoadFontEx(path.c_str(), 96, nullptr, 0);
            // Canvas previews and docked Play views can shrink glyphs substantially.
            GenTextureMipmaps(&font.texture);
            SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
            for (size_t i = 0; std::cmp_less(i, font.glyphCount); i++)
            {
                assert(font.glyphs[i].image.data != nullptr);
            }

            fonts[path] = font;
        }
    }

    void ResourceManager::registerImageKey(const std::string& key, const std::string& sourcePath)
    {
        RegisterSourcePath(imageSourcePaths, "Image", key, sourcePath);
        if (images.contains(key))
        {
            throw std::runtime_error(
                "Image resource key collision for '" + key + "' while loading '" + sourcePath + "'");
        }
        imageSourcePaths.emplace(key, sourcePath);
    }

    void ResourceManager::RebuildAssetAliases()
    {
        BuildAliases(modelCopies, modelAliases);
        auto imageKeys = packedImageKeys;
        for (const auto& [key, image] : images)
            imageKeys.emplace(key, 0);
        BuildAliases(imageKeys, imageAliases);
        BuildAliases(modelAnimations, animationAliases);
    }

    std::string ResourceManager::ResolveModelKey(const std::string& key) const
    {
        if (modelCopies.contains(key)) return key;
        return ResolveKey(key, modelAliases);
    }

    std::string ResourceManager::ResolveImageKey(const std::string& key) const
    {
        if (images.contains(key) || packedImageKeys.contains(key)) return key;
        return ResolveKey(key, imageAliases);
    }

    std::string ResourceManager::ResolveAnimationKey(const std::string& key) const
    {
        if (modelAnimations.contains(key)) return key;
        return ResolveKey(key, animationAliases);
    }

    namespace
    {
        // Registry of generators for primitives baked into the asset pack. CreateModelMutable
        // uses these to regenerate a fresh mesh when the source asset has no on-disk file.
        // Hardcoded list — raylib's GenMesh* functions, parameterized at canonical unit sizes
        // (callers scale via the entity's transform).
        using PrimitiveGenerator = Mesh (*)();

        Mesh GenSphere()
        {
            return GenMeshSphere(1.0f, 32, 32);
        }
        Mesh GenHemiSphere()
        {
            return GenMeshHemiSphere(1.0f, 16, 32);
        }
        Mesh GenPlane()
        {
            return GenMeshPlane(1.0f, 1.0f, 1, 1);
        }
        Mesh GenCube()
        {
            return GenMeshCube(1.0f, 1.0f, 1.0f);
        }
        Mesh GenCylinder()
        {
            return GenMeshCylinder(1.0f, 1.0f, 32);
        }
        Mesh GenCone()
        {
            return GenMeshCone(1.0f, 1.0f, 32);
        }
        Mesh GenTorus()
        {
            return GenMeshTorus(0.25f, 1.0f, 16, 32);
        }
        Mesh GenKnot()
        {
            return GenMeshKnot(1.0f, 2.0f, 16, 128);
        }
        Mesh GenPoly()
        {
            return GenMeshPoly(5, 1.0f);
        }

        const std::unordered_map<std::string, PrimitiveGenerator>& PrimitiveGenerators()
        {
            static const std::unordered_map<std::string, PrimitiveGenerator> generators = {
                {"primitive_sphere", &GenSphere},
                {"primitive_hemisphere", &GenHemiSphere},
                {"primitive_plane", &GenPlane},
                {"primitive_cube", &GenCube},
                {"primitive_cylinder", &GenCylinder},
                {"primitive_cone", &GenCone},
                {"primitive_torus", &GenTorus},
                {"primitive_knot", &GenKnot},
                {"primitive_poly", &GenPoly},
            };
            return generators;
        }
    } // namespace

    Texture ResourceManager::LoadPackedTexture(const std::size_t id)
    {
        auto& texture = packedTextures.at(id);
        if (texture.id == 0)
        {
            const auto& descriptor = packedTextureData.at(id);
            texture = LoadTextureFromImage(packedImages.at(descriptor.image));
            if (descriptor.minFilter >= RL_TEXTURE_FILTER_MIP_NEAREST) GenTextureMipmaps(&texture);
            rlTextureParameters(texture.id, RL_TEXTURE_MIN_FILTER, descriptor.minFilter);
            rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER, descriptor.magFilter);
            rlTextureParameters(texture.id, RL_TEXTURE_WRAP_S, descriptor.wrapS);
            rlTextureParameters(texture.id, RL_TEXTURE_WRAP_T, descriptor.wrapT);
        }
        return texture;
    }

    void ResourceManager::LoadPackedAssets(const PackedAssets& assets, const std::function<void()>& progress)
    {
        assets.Validate();
        auto lastUpdate = std::chrono::steady_clock::now();
        const auto reportProgress = [&] {
            if (progress && std::chrono::steady_clock::now() - lastUpdate >= std::chrono::milliseconds(50))
            {
                progress();
                lastUpdate = std::chrono::steady_clock::now();
            }
        };
        if (!packedTextureData.empty()) throw std::runtime_error("Packed assets already loaded");
        for (const auto& source : assets.models)
            if (modelCopies.contains(source.key)) throw std::runtime_error("Duplicate model key: " + source.key);
        packedTextureData = assets.textures;
        packedTextures.resize(assets.textures.size());
        for (const auto& source : assets.images)
        {
            Image image = LoadImageFromMemory(
                source.extension.c_str(), source.bytes.data(), static_cast<int>(source.bytes.size()));
            if (!image.data) throw std::runtime_error("Cannot decode packed image");
            packedImages.push_back(image);
            reportProgress();
        }
        for (const auto& [key, id] : assets.imageKeys)
            packedImageKeys.emplace(key, id);
        for (const auto& source : assets.materials)
        {
            Material material = LoadMaterialDefault();
            for (std::size_t i = 0; i < source.maps.size(); ++i)
            {
                material.maps[i].color = source.maps[i].color;
                material.maps[i].value = source.maps[i].value;
                if (source.maps[i].texture) material.maps[i].texture = LoadPackedTexture(*source.maps[i].texture);
            }
            std::ranges::copy(source.params, material.params);
            materialMap.emplace(source.key, material);
            reportProgress();
        }
        for (const auto& [key, id] : assets.materialKeys)
            materialAliases.emplace(key, assets.materials.at(id).key);
        for (const auto& source : assets.models)
        {
            Model model{};
            if (source.primitive)
            {
                model = LoadModelFromMesh(PrimitiveGenerators().at(*source.primitive)());
                for (int i = 0; i < model.materialCount; ++i)
                    MemFree(model.materials[i].maps);
                MemFree(model.materials);
            }
            else
            {
                model.transform = MatrixIdentity();
                model.meshCount = static_cast<int>(source.meshes.size());
                model.meshes =
                    static_cast<Mesh*>(MemAlloc(static_cast<unsigned int>(source.meshes.size() * sizeof(Mesh))));
                model.meshMaterial =
                    static_cast<int*>(MemAlloc(static_cast<unsigned int>(source.meshes.size() * sizeof(int))));
                for (std::size_t i = 0; i < source.meshes.size(); ++i)
                {
                    model.meshes[i] = CreatePackedMesh(source.meshes.at(i), source.bones.size());
                    model.meshMaterial[i] = static_cast<int>(source.meshes.at(i).material);
                }
                model.boneCount = static_cast<int>(source.bones.size());
                if (!source.bones.empty())
                {
                    model.bones = static_cast<BoneInfo*>(
                        MemAlloc(static_cast<unsigned int>(source.bones.size() * sizeof(BoneInfo))));
                    model.bindPose = static_cast<Transform*>(
                        MemAlloc(static_cast<unsigned int>(source.bindPose.size() * sizeof(Transform))));
                    std::ranges::copy(source.bones, model.bones);
                    std::ranges::copy(source.bindPose, model.bindPose);
                }
            }
            model.materialCount = static_cast<int>(source.materials.size());
            model.materials = static_cast<Material*>(
                MemAlloc(static_cast<unsigned int>(source.materials.size() * sizeof(Material))));
            std::vector<std::string> names;
            for (std::size_t i = 0; i < source.materials.size(); ++i)
            {
                names.push_back(assets.materials.at(source.materials.at(i)).key);
                model.materials[i] = materialMap.at(names.back());
            }
            modelCopies.emplace(
                source.key,
                ModelInfo{.model = model, .materialNames = std::move(names), .sourcePath = source.sourcePath});
            if (!source.animations.empty())
            {
                auto* animations = static_cast<ModelAnimation*>(
                    MemAlloc(static_cast<unsigned int>(source.animations.size() * sizeof(ModelAnimation))));
                for (std::size_t i = 0; i < source.animations.size(); ++i)
                    animations[i] = CreatePackedAnimation(source.animations.at(i));
                modelAnimations.emplace(
                    source.key, std::make_pair(animations, static_cast<int>(source.animations.size())));
            }
            reportProgress();
        }
        RebuildAssetAliases();
    }

    /* Non-owning view onto the shared model entry stored under viewKey. Read-only API.
    The returned ModelView's lifetime is independent of RM: it just borrows; the
    underlying entry stays alive until UnloadAll (i.e. scene tear-down). */
    bool ResourceManager::HasModelKey(const std::string& key) const
    {
        return modelCopies.contains(ResolveModelKey(key));
    }

    std::vector<std::string> ResourceManager::GetModelKeys(const bool includeGenerated) const
    {
        std::vector<std::string> keys;
        keys.reserve(modelCopies.size());

        for (const auto& [key, info] : modelCopies)
        {
            if (key.find("#mut_") != std::string::npos) continue;
            if (!includeGenerated && info.sourcePath.empty()) continue;
            keys.push_back(key);
        }

        std::ranges::sort(keys);
        return keys;
    }

    std::string ResourceManager::GetModelSourcePath(const std::string& key) const
    {
        const auto resolved = ResolveModelKey(key);
        if (!modelCopies.contains(resolved)) return {};
        return modelCopies.at(resolved).sourcePath;
    }

    bool ResourceManager::RenameModelAsset(
        const std::string& oldKey, const std::string& newKey, const std::string& newSourcePath)
    {
        if (oldKey.empty() || newKey.empty()) return false;
        if (!modelCopies.contains(oldKey)) return false;
        if (oldKey != newKey && modelCopies.contains(newKey)) return false;

        auto modelNode = modelCopies.extract(oldKey);
        modelNode.key() = newKey;
        modelNode.mapped().sourcePath = newSourcePath;
        modelCopies.insert(std::move(modelNode));

        if (auto animationNode = modelAnimations.extract(oldKey); !animationNode.empty())
        {
            animationNode.key() = newKey;
            modelAnimations.insert(std::move(animationNode));
        }

        RebuildAssetAliases();

        return true;
    }

    ModelView ResourceManager::GetModelView(const std::string& viewKey) const
    {
        const auto key = ResolveModelKey(viewKey);
        assert(modelCopies.contains(key));
        ModelView view;
        view.rlmodel = modelCopies.at(key).model;
        view.renderBounds = modelCopies.at(key).renderBounds;
        view.assetKey = key;
        return view;
    }

    std::vector<std::string> ResourceManager::GetMaterialKeys() const
    {
        std::vector<std::string> keys;
        keys.reserve(materialMap.size());
        for (const auto& [key, material] : materialMap)
            keys.push_back(key);
        for (const auto& [key, canonical] : materialAliases)
            if (!materialMap.contains(key)) keys.push_back(key);
        std::ranges::sort(keys);
        return keys;
    }

    std::vector<std::string> ResourceManager::GetImageKeys(const std::string& prefix) const
    {
        std::vector<std::string> keys;
        keys.reserve(images.size() + packedImageKeys.size());
        for (const auto& [key, texture] : packedImageKeys)
            if (prefix.empty() || AssetNameFromKey(key).starts_with(prefix)) keys.push_back(key);
        for (const auto& key : images | std::views::keys)
        {
            if (prefix.empty() || AssetNameFromKey(key).starts_with(prefix)) keys.push_back(key);
        }
        std::ranges::sort(keys);
        return keys;
    }

    const std::vector<std::string>& ResourceManager::GetModelMaterialKeys(const std::string& modelKey) const
    {
        const auto key = ResolveModelKey(modelKey);
        assert(modelCopies.contains(key));
        return modelCopies.at(key).materialNames;
    }

    const Material& ResourceManager::GetMaterial(const std::string& key) const
    {
        const auto canonical = materialAliases.contains(key) ? materialAliases.at(key) : key;
        return materialMap.at(canonical);
    }

    // Mutable instances own mesh and material-map allocations, while borrowing pooled textures.
    ModelMutable ResourceManager::CreateModelMutable(const std::string& viewKey)
    {
        const auto key = ResolveModelKey(viewKey);
        assert(modelCopies.contains(key));
        const auto& info = modelCopies.at(key);

        const std::string instanceKey = key + "#mut_" + std::to_string(mutableInstanceCounter++);
        assert(!modelCopies.contains(instanceKey) && "CreateModelMutable: instanceKey collision");

        const Model& shared = info.model;
        Model model = shared;
        const auto copy = []<typename T>(const T* source, const std::size_t count) -> T* {
            if (!source || count == 0) return nullptr; // raylib optional array boundary.
            auto* destination = static_cast<T*>(MemAlloc(static_cast<unsigned int>(count * sizeof(T))));
            if (!destination) throw std::bad_alloc();
            std::copy_n(source, count, destination);
            return destination;
        };
        model.materials = copy(shared.materials, static_cast<std::size_t>(shared.materialCount));
        for (int i = 0; i < model.materialCount; ++i)
            model.materials[i].maps = copy(shared.materials[i].maps, MAX_MATERIAL_MAPS);
        model.meshMaterial = copy(shared.meshMaterial, static_cast<std::size_t>(shared.meshCount));
        model.bones = copy(shared.bones, static_cast<std::size_t>(shared.boneCount));
        model.bindPose = copy(shared.bindPose, static_cast<std::size_t>(shared.boneCount));
        model.meshes = static_cast<Mesh*>(MemAlloc(static_cast<unsigned int>(shared.meshCount * sizeof(Mesh))));
        const auto capture = []<typename T>(const T* source, const std::size_t count) {
            return source ? std::vector<T>(source, source + count) : std::vector<T>{};
        };
        for (int i = 0; i < shared.meshCount; ++i)
        {
            const auto& mesh = shared.meshes[i];
            const auto count = static_cast<std::size_t>(mesh.vertexCount);
            PackedMesh packed;
            packed.vertices = capture(mesh.vertices, count * 3);
            packed.normals = capture(mesh.normals, count * 3);
            packed.texcoords = capture(mesh.texcoords, count * 2);
            packed.texcoords2 = capture(mesh.texcoords2, count * 2);
            packed.tangents = capture(mesh.tangents, count * 4);
            packed.colors = capture(mesh.colors, count * 4);
            packed.indices = capture(mesh.indices, static_cast<std::size_t>(mesh.triangleCount) * 3);
            packed.boneIds = capture(mesh.boneIds, count * 4);
            packed.boneWeights = capture(mesh.boneWeights, count * 4);
            model.meshes[i] = CreatePackedMesh(packed, static_cast<std::size_t>(shared.boneCount));
        }

        modelCopies.emplace(
            instanceKey,
            ModelInfo{
                .model = model,
                .materialNames = info.materialNames,
                .sourcePath = info.sourcePath,
                /*privateMaterials=*/.privateMaterials = true});

        ModelMutable mut;
        mut.rlmodel = modelCopies.at(instanceKey).model;
        mut.renderBounds = modelCopies.at(instanceKey).renderBounds;
        mut.assetKey = key;
        mut.instanceKey = instanceKey;
        return mut;
    }

    bool ResourceManager::HasModelAnimation(const std::string& key) const
    {
        return modelAnimations.contains(ResolveAnimationKey(key));
    }

    ModelAnimation* ResourceManager::GetModelAnimation(const std::string& key, int* animsCount) const
    {
        const auto resolved = ResolveAnimationKey(key);
        if (!modelAnimations.contains(resolved))
        {
            TraceLog(
                LOG_FATAL, "ResourceManager::GetModelAnimation: animation '%s' was not pre-loaded.", key.c_str());
            assert(false && "missing model animation");
        }
        const auto& pair = modelAnimations.at(resolved);
        *animsCount = pair.second;
        return pair.first;
    }

    void ResourceManager::UnloadImages()
    {
        for (const auto& image : images | std::views::values)
        {
            UnloadImage(image);
        }
        images.clear();
        for (const auto& image : packedImages)
            UnloadImage(image);
        packedImages.clear();
        packedImageKeys.clear();
        imageSourcePaths.clear();
        RebuildAssetAliases();
    }

    void ResourceManager::UnloadShaderFileText()
    {
        vertShaderFileText.clear();
        fragShaderFileText.clear();
    }

    void sgUnloadModel(const Model& model)
    {
        // Unload meshes
        for (int i = 0; i < model.meshCount; i++)
            UnloadMesh(model.meshes[i]);

        // Unload arrays
        MemFree(model.meshes);
        MemFree(model.materials);
        MemFree(model.meshMaterial);

        // Unload animation data
        MemFree(model.bones);
        MemFree(model.bindPose);

        TraceLog(LOG_INFO, "MODEL: Unloaded model (and meshes) from RAM and VRAM");
    }

    void ResourceManager::UnloadAll()
    {
        for (auto& s : sfx | std::views::values)
        {
            UnloadSound(s);
        }
        for (auto& mus : music | std::views::values)
        {
            UnloadMusicStream(mus);
        }
        std::unordered_set<unsigned int> textureIds;
        const auto releaseTexture = [&textureIds](Texture texture) {
            if (texture.id != 0 && texture.id != rlGetTextureIdDefault() && textureIds.insert(texture.id).second)
                UnloadTexture(texture);
        };
        for (const auto& texture : packedTextures)
            releaseTexture(texture);
        for (auto& [key, mat] : materialMap)
        {
            for (int i = 0; i < MAX_MATERIAL_MAPS; i++)
            {
                releaseTexture(mat.maps[i].texture);
            }
            MemFree(mat.maps);
        }
        for (auto& info : modelCopies | std::views::values)
        {
            if (info.privateMaterials)
            {
                // Deep-copy entry: free the per-material maps allocations, but don't
                // UnloadMaterial — textures and shaders inside are RM-cached and still
                // in use by other entries.
                Model& m = info.model;
                for (int i = 0; i < m.materialCount; ++i)
                {
                    MemFree(m.materials[i].maps);
                }
                for (int i = 0; i < m.meshCount; ++i)
                {
                    UnloadMesh(m.meshes[i]);
                }
                MemFree(m.meshes);
                MemFree(m.materials);
                MemFree(m.meshMaterial);
                MemFree(m.bones);
                MemFree(m.bindPose);
            }
            else
            {
                sgUnloadModel(info.model);
            }
        }
        for (const auto& tex : nonModelTextures | std::views::values)
        {
            releaseTexture(tex);
        }
        for (const auto& image : images | std::views::values)
        {
            UnloadImage(image);
        }
        for (const auto& [fst, snd] : modelAnimations | std::views::values)
        {
            UnloadModelAnimations(fst, snd);
        }
        for (const auto& shader : shaders | std::views::values)
        {
            UnloadShader(shader);
        }
        for (const auto& font : fonts | std::views::values)
        {
            UnloadFont(font);
        }
        for (const auto& image : packedImages)
            UnloadImage(image);
        packedImages.clear();
        packedTextureData.clear();
        packedTextures.clear();
        packedImageKeys.clear();
        materialAliases.clear();
        fonts.clear();
        shaders.clear();
        materialMap.clear();
        images.clear();
        nonModelTextures.clear();
        modelCopies.clear();
        modelAnimations.clear();
        modelAliases.clear();
        imageAliases.clear();
        animationAliases.clear();
        vertShaderFileText.clear();
        fragShaderFileText.clear();
        music.clear();
        sfx.clear();
        musicSourcePaths.clear();
        sfxSourcePaths.clear();
    }

    void ResourceManager::Reset()
    {
        UnloadAll();
        init();
    }

    void ResourceManager::init()
    {
        Shader shader;
        shader.id = rlGetShaderIdDefault();
        shader.locs = rlGetShaderLocsDefault();
        shaders.emplace("DEFAULT", shader);
    }

    ResourceManager::~ResourceManager()
    {
        UnloadAll();
    }

    ResourceManager::ResourceManager()
    {
        init();
    }
} // namespace sage

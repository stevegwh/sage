//
// Created by Steve Wheeler on 16/07/2024.
//

#include "ResourceManager.hpp"

#include "components/Renderable.hpp"
#include "Slibmodel.hpp"

#include "raylib/src/config.h"
#include "raymath.h"
#include "rlgl.h"
#define STB_INCLUDE_IMPLEMENTATION
#define STB_INCLUDE_LINE_NONE

#include <stb_include.h>

#include <cstring>
#include <sstream>
#include <unordered_map>

namespace sage
{

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

    // Replaces a freshly-loaded model's materials with shared singletons in materialMap.
    // raylib-allocated materials that get displaced are released via UnloadMaterial.
    // Materials whose names are not yet pooled are donated to materialMap (first-write wins).
    void ResourceManager::dedupeAndShareMaterials(Model& model, std::vector<std::string>& materialNames)
    {
        if (materialNames.empty())
        {
            materialNames.emplace_back("Default");
            if (!materialMap.contains("Default"))
            {
                materialMap.emplace("Default", LoadMaterialDefault());
            }
            // raylib's LoadModel always allocates at least one material slot; release the
            // freshly-loaded copy before swapping in the shared singleton.
            if (model.materialCount > 0)
            {
                UnloadMaterial(model.materials[0]);
            }
            model.materials[0] = materialMap.at("Default");
            return;
        }

        for (unsigned int i = 0; i < materialNames.size(); ++i)
        {
            const auto& name = materialNames[i];
            if (!materialMap.contains(name))
            {
                // First sighting of this name: donate the freshly-loaded material to the shared pool.
                materialMap[name] = model.materials[i];
            }
            else
            {
                // Already pooled: release raylib's freshly-allocated copy, swap in the shared one.
                UnloadMaterial(model.materials[i]);
                model.materials[i] = materialMap.at(name);
            }
        }
    }

    Music ResourceManager::GetMusic(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
                                    // from QUEST_BONE.obj)
        if (!music.contains(key))
        {
            music[key] = LoadMusicStream(path.c_str());
        }
        return music.at(key);
    }

    Sound ResourceManager::GetSFX(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
                                    // from QUEST_BONE.obj)
        if (!sfx.contains(key))
        {
            // NB: Currently, the resource packer does not support serializing sound/music.
            sfx[key] = LoadSound(path.c_str());
        }
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
    Shader ResourceManager::ShaderLoad(const char* vsFileName, const char* fsFileName)
    {
        if (vsFileName == nullptr && fsFileName == nullptr || (vsFileName != nullptr && !FileExists(vsFileName) &&
                                                               (fsFileName != nullptr && !FileExists(fsFileName))))
        {
            std::cout << "WARNING: Both files nullptr or do not exist. Loading default shader. \n";
            return shaders["DEFAULT"];
        }

        char* vShaderStr = nullptr;
        char* fShaderStr = nullptr;

        const char* SHADER_INCLUDE_PATH = "resources/shaders/custom/include";

        if (vsFileName != nullptr)
        {
            if (!vertShaderFileText.contains(vsFileName))
            {
                assert(FileExists(vsFileName));
                // Load and preprocess vertex shader with stb_include
                char* vertexSource = LoadFileText(vsFileName);
                char* preprocessed =
                    stb_include_string(vertexSource, nullptr, (char*)SHADER_INCLUDE_PATH, nullptr, nullptr);
                free(vertexSource);
                vertShaderFileText[vsFileName] = preprocessed;
            }
            vShaderStr = vertShaderFileText[vsFileName];
        }

        if (fsFileName != nullptr)
        {
            if (!fragShaderFileText.contains(fsFileName))
            {
                assert(FileExists(fsFileName));
                // Load and preprocess fragment shader with stb_include
                char* fragmentSource = LoadFileText(fsFileName);
                char* preprocessed =
                    stb_include_string(fragmentSource, nullptr, (char*)SHADER_INCLUDE_PATH, nullptr, nullptr);
                free(fragmentSource);
                fragShaderFileText[fsFileName] = preprocessed;
            }
            fShaderStr = fragShaderFileText[fsFileName];
        }

        return gpuShaderLoad(vShaderStr, fShaderStr);
    }

    Texture ResourceManager::TextureLoad(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
        //  from QUEST_BONE.obj)
        if (!nonModelTextures.contains(key))
        {
            if (!images.contains(key))
            {
                images.emplace(key, LoadImage(path.c_str()));
            }
            nonModelTextures[key] = LoadTextureFromImage(images[key]);
        }
        return nonModelTextures[key];
    }

    Texture ResourceManager::TextureLoadFromImage(const std::string& name, Image image)
    {
        if (!images.contains(name))
        {
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
        if (images.contains(key))
        {
            UnloadImage(images.at(key));
            images.erase(key);
        }
    }

    ImageSafe ResourceManager::GetImage(const std::string& key)
    {
        assert(images.contains(key));
        return ImageSafe(images[key], false);
    }

    void ResourceManager::FontLoadFromFile(const std::string& path)
    {
        assert(FileExists(path.c_str()));
        if (!fonts.contains(path))
        {
            auto font = LoadFont(path.c_str());
            for (size_t i = 0; i < font.glyphCount; i++)
            {
                assert(font.glyphs[i].image.data != nullptr);
            }

            fonts[path] = font;
        }
    }

    void ResourceManager::ImageLoadFromFile(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
        // from QUEST_BONE.obj)
        assert(FileExists(path.c_str()));
        // As the path is stripped of any relative information, file name overlap is very possible.
        assert(!images.contains(key));
        images[key] = LoadImage(path.c_str());
    }

    void ResourceManager::ImageLoadFromFile(const std::string& path, Image image)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
        // from QUEST_BONE.obj)
        assert(!images.contains(key));
        images[key] = image;
        image = {};
    }

    void ResourceManager::ModelLoadFromFile(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
                                    // from QUEST_BONE.obj)
        ModelLoadFromFile(path, key);
    }

    void ResourceManager::ModelLoadFromFile(const std::string& path, const std::string& key)
    {
        assert(!key.empty());
        if (modelCopies.contains(key)) return;
        assert(FileExists(path.c_str()));

        auto materialNames = GetMaterialNames(path.c_str());
        Model model = LoadModel(path.c_str());
        dedupeAndShareMaterials(model, materialNames);
        modelCopies.emplace(key, ModelInfo{model, std::move(materialNames), path});
    }

    void ResourceManager::StoreModel(const ModelInfo& modelInfo, const std::string& key)
    {
        modelCopies.emplace(key, modelInfo);
    }

    /**
     * @brief Returns a shallow copy of the loaded model
     * NB: Caller should not free the memory.
     * @param key the *key* of the model, not its path. This is either an alias defined in the JSON file, or the
     * mesh name in Blender minus its format extension
     * @return Model
     */
    ModelSafe ResourceManager::GetModelCopy(const std::string& key)
    {
        assert(modelCopies.contains(key));
        ModelSafe modelsafe;
        modelsafe.rlmodel = modelCopies.at(key).model;
        modelsafe.SetKey(key);
        return modelsafe;
    }

    ModelSafeManaged ResourceManager::GetModelDeepCopy(const std::string& srcKey, const std::string& dstKey)
    {
        assert(modelCopies.contains(srcKey));
        assert(!modelCopies.contains(dstKey) && "GetModelDeepCopy: dstKey already registered");
        const auto& info = modelCopies.at(srcKey);
        assert(!info.sourcePath.empty() && "GetModelDeepCopy: sourcePath missing (re-pack assets)");
        assert(FileExists(info.sourcePath.c_str()) && "GetModelDeepCopy: source file missing at runtime");

        // Fresh load: the resulting Model has its own private mesh data AND its own
        // private material allocations (raylib LoadModel allocates materials per call).
        // We deliberately skip dedupeAndShareMaterials so mutations on this entry
        // (SetTexture, SetMaterial, SetShader) stay local to this dstKey.
        Model model = LoadModel(info.sourcePath.c_str());

        modelCopies.emplace(
            dstKey, ModelInfo{model, info.materialNames, info.sourcePath, /*privateMaterials=*/true});

        ModelSafeManaged safe;
        safe.rlmodel = modelCopies.at(dstKey).model;
        safe.SetKey(dstKey);
        return safe;
    }

    void ResourceManager::ReleaseDeepCopy(const std::string& dstKey)
    {
        auto it = modelCopies.find(dstKey);
        if (it == modelCopies.end()) return;
        assert(it->second.privateMaterials && "ReleaseDeepCopy: cannot release a shared-material entry");

        Model& m = it->second.model;
        // Free the per-material maps allocations made by raylib's LoadModel, but DO NOT
        // call UnloadMaterial — that would UnloadShader / rlUnloadTexture the textures
        // and shaders inside, which are shared/cached via ResourceManager (TextureLoad,
        // ShaderLoad) and still in use by other entries.
        for (int i = 0; i < m.materialCount; ++i)
        {
            RL_FREE(m.materials[i].maps);
        }
        for (int i = 0; i < m.meshCount; ++i)
        {
            UnloadMesh(m.meshes[i]);
        }
        RL_FREE(m.meshes);
        RL_FREE(m.materials);
        RL_FREE(m.meshMaterial);
        RL_FREE(m.bones);
        RL_FREE(m.bindPose);

        modelCopies.erase(it);
    }

    void ResourceManager::ModelAnimationLoadFromFile(const std::string& path)
    {
        auto key = StripPath(path); // Will either be a mesh alias (MDL_GOBLIN) or a mesh name (e.g., QUEST_BONE
        // from QUEST_BONE.obj)
        if (!modelAnimations.contains(key))
        {
            int animsCount;
            auto animations = LoadModelAnimations(path.c_str(), &animsCount);
            if (animations == nullptr)
            {
                std::cout << "ResourceManager: Model does not contain animation data, or was unable to be loaded. "
                             "Aborting... \n";
                return;
            }
            modelAnimations[key] = std::make_pair(animations, animsCount);
        }
    }

    ModelAnimation* ResourceManager::GetModelAnimation(const std::string& key, int* animsCount)
    {
        if (!modelAnimations.contains(key))
        {
            TraceLog(
                LOG_FATAL, "ResourceManager::GetModelAnimation: animation '%s' was not pre-loaded.", key.c_str());
            assert(false && "missing model animation");
        }
        const auto& pair = modelAnimations.at(key);
        *animsCount = pair.second;
        return pair.first;
    }

    void ResourceManager::UnloadImages()
    {
        for (const auto& [key, image] : images)
        {
            UnloadImage(image);
        }
        images.clear();
    }

    void ResourceManager::UnloadShaderFileText()
    {
        for (const auto& [key, vs] : vertShaderFileText)
        {
            UnloadFileText(vs);
        }
        for (const auto& [key, fs] : fragShaderFileText)
        {
            UnloadFileText(fs);
        }
        vertShaderFileText.clear();
        fragShaderFileText.clear();
    }

    void sgUnloadModel(Model model)
    {
        // Unload meshes
        for (int i = 0; i < model.meshCount; i++)
            UnloadMesh(model.meshes[i]);

        // Unload arrays
        RL_FREE(model.meshes);
        RL_FREE(model.materials);
        RL_FREE(model.meshMaterial);

        // Unload animation data
        RL_FREE(model.bones);
        RL_FREE(model.bindPose);

        TRACELOG(LOG_INFO, "MODEL: Unloaded model (and meshes) from RAM and VRAM");
    }

    void ResourceManager::UnloadAll()
    {
        for (auto& [key, s] : sfx)
        {
            UnloadSound(s);
        }
        for (auto& [key, mus] : music)
        {
            UnloadMusicStream(mus);
        }
        for (auto& [key, mat] : materialMap)
        {
            for (int i = 0; i < MAX_MATERIAL_MAPS; i++)
            {
                if (mat.maps[i].texture.id != rlGetTextureIdDefault()) rlUnloadTexture(mat.maps[i].texture.id);
            }
            std::cout << "Material key: " << key << std::endl;
            std::cout << "Material maps address : " << &mat.maps << std::endl;
            RL_FREE(mat.maps);
        }
        std::cout << "Unloading models" << std::endl;
        for (auto& [path, info] : modelCopies)
        {
            if (info.privateMaterials)
            {
                // Deep-copy entry: free the per-material maps allocations, but don't
                // UnloadMaterial — textures and shaders inside are RM-cached and still
                // in use by other entries.
                Model& m = info.model;
                for (int i = 0; i < m.materialCount; ++i)
                {
                    RL_FREE(m.materials[i].maps);
                }
                for (int i = 0; i < m.meshCount; ++i)
                {
                    UnloadMesh(m.meshes[i]);
                }
                RL_FREE(m.meshes);
                RL_FREE(m.materials);
                RL_FREE(m.meshMaterial);
                RL_FREE(m.bones);
                RL_FREE(m.bindPose);
            }
            else
            {
                sgUnloadModel(info.model);
            }
        }
        for (const auto& [key, tex] : nonModelTextures)
        {
            UnloadTexture(tex);
        }
        for (const auto& [key, image] : images)
        {
            UnloadImage(image);
        }
        for (const auto& [key, p] : modelAnimations)
        {
            UnloadModelAnimations(p.first, p.second);
        }
        for (const auto& [key, shader] : shaders)
        {
            UnloadShader(shader);
        }
        for (const auto& [key, text] : vertShaderFileText)
        {
            UnloadFileText(text);
        }
        for (const auto& [key, text] : fragShaderFileText)
        {
            UnloadFileText(text);
        }
        for (const auto& [key, font] : fonts)
        {
            UnloadFont(font);
        }
        fonts.clear();
        shaders.clear();
        materialMap.clear();
        images.clear();
        nonModelTextures.clear();
        modelCopies.clear();
        modelAnimations.clear();
        vertShaderFileText.clear();
        fragShaderFileText.clear();
        music.clear();
        sfx.clear();
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
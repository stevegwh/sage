#pragma once
#include "ContentDocument.hpp"
#include "engine/ResourceManager.hpp"
#include <set>

namespace sage::content
{
    // Export once while packed resources are loaded; all subsequent checks are GPU-free.
    inline json::Document CaptureAssetIndex()
    {
        auto& resources = ResourceManager::GetInstance();
        json::Document result(rapidjson::kObjectType);
        auto& allocator = result.GetAllocator();
        json::Put(result, "format", "sage-asset-index", allocator);
        json::Put(result, "version", std::uint64_t(1), allocator);
        for (const auto& [name, keys] : std::vector<std::pair<std::string, std::vector<std::string>>>{
                 {"models", resources.GetModelKeys(true)},
                 {"materials", resources.GetMaterialKeys()},
                 {"images", resources.GetImageKeys()}})
        {
            json::Value values(rapidjson::kArrayType);
            for (const auto& key : keys)
                values.PushBack(json::Value(key.c_str(), allocator), allocator);
            json::Put(result, name.c_str(), values, allocator);
        }
        return result;
    }
    inline void WriteAssetIndex(const std::filesystem::path& path)
    {
        std::ofstream output(path);
        output << json::Stringify(CaptureAssetIndex());
        if (!output) throw std::runtime_error("Cannot write asset index: " + path.string());
    }
    inline std::vector<std::string> ValidateAssets(const json::Value& document, const json::Value& index)
    {
        if (json::String(index, "format") != "sage-asset-index" || json::Id(index, "version") != 1)
            throw std::runtime_error("Unsupported asset index");
        std::vector<std::string> errors;
        const auto has = [&](const char* group, const std::string& key) {
            const auto& values = json::Require(index, group);
            if (!values.IsArray()) throw std::runtime_error("Invalid asset index group");
            return std::any_of(values.Begin(), values.End(), [&](const auto& value) {
                return value.IsString() && key == value.GetString();
            });
        };
        const auto check = [&](const char* group, const std::string& key) {
            if (!key.empty() && !has(group, key))
                errors.push_back(std::string("Missing ") + group + " asset: " + key);
        };
        for (const auto& node : document["entities"].GetArray())
        {
            const auto& components = node["components"];
            const auto supported = [&](const char* key) {
                return components.HasMember(key) && json::Id(components[key], "version") == 1 &&
                       !components[key].HasMember("encoding");
            };
            if (supported("sage.Renderable"))
            {
                const auto& renderable = components["sage.Renderable"]["data"];
                const auto kind = json::Id(renderable, "kind");
                auto key = json::String(renderable, "key");
                // Mutable models store material overrides after a separator in legacy resource keys.
                key = key.substr(0, key.find('\x1f'));
                if (kind == 1 || kind == 2) check("models", key);
            }
            if (supported("sage.Animation"))
                check("models", json::String(components["sage.Animation"]["data"], "modelKey"));
            for (const char* name : {"sage.CustomShader", "sage.ParticleEmitter"})
                if (supported(name))
                {
                    const auto& data = components[name]["data"];
                    for (const char* field : {"texture", "texture0Key", "texture1Key"})
                        if (data.HasMember(field) && data[field].IsString())
                            check("images", data[field].GetString());
                    for (const char* field : {"vertexShaderPath", "fragmentShaderPath"})
                        if (data.HasMember(field) && data[field].IsString() &&
                            data[field].GetString()[0] != '\0' &&
                            !std::filesystem::is_regular_file(data[field].GetString()))
                            errors.push_back("Missing shader file: " + std::string(data[field].GetString()));
                }
        }
        return errors;
    }
} // namespace sage::content

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sage
{
    // Packed file assets use their path below resources as their stable, inspectable key.
    // Generated assets (for example primitive_cube) retain their explicit names.
    inline std::string AssetKeyForPath(const std::filesystem::path& path)
    {
        const auto normalized = path.lexically_normal().generic_string();
        constexpr std::string_view prefix = "resources/";
        return normalized.starts_with(prefix) ? normalized.substr(prefix.size()) : normalized;
    }

    inline std::string AssetNameFromKey(const std::string& key)
    {
        const std::filesystem::path path{key};
        return path.has_extension() ? path.stem().string() : path.filename().string();
    }

    inline std::vector<std::string> AssetLabels(const std::vector<std::string>& keys)
    {
        std::unordered_map<std::string, std::size_t> names;
        std::unordered_map<std::string, std::size_t> folders;
        for (const auto& key : keys)
        {
            const auto name = AssetNameFromKey(key);
            ++names[name];
            ++folders[name + "\n" + std::filesystem::path(key).parent_path().generic_string()];
        }

        std::vector<std::string> labels;
        labels.reserve(keys.size());
        for (const auto& key : keys)
        {
            const auto name = AssetNameFromKey(key);
            if (names[name] == 1)
            {
                labels.push_back(name);
                continue;
            }
            const std::filesystem::path path{key};
            const auto folder = path.parent_path().generic_string();
            const auto extension = folders[name + "\n" + folder] > 1 ? path.extension().string() : "";
            labels.push_back(name + " (" + folder + extension + ")");
        }
        return labels;
    }
} // namespace sage

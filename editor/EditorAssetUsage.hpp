#pragma once

#include "CSharpScriptEditorConfig.hpp"
#include "engine/content/Json.hpp"
#include "entt/entt.hpp"
#include <optional>

namespace sage::editor
{
    enum class BrowserAssetKind
    {
        Model,
        Material,
        Image,
        Flatpack,
        Canvas,
        Script
    };

    struct BrowserAsset
    {
        BrowserAssetKind kind = BrowserAssetKind::Model;
        std::string key;
        std::vector<std::string> aliases;
    };

    struct ScriptSource
    {
        std::filesystem::path path;
        std::vector<std::string> types;
    };

    struct AssetUsage
    {
        std::filesystem::path path;
        std::string location;
        std::string field;
        std::optional<std::uint64_t> entityId;
        std::optional<unsigned int> nodeId;
        bool live = false;
    };

    struct AssetUsageResults
    {
        std::vector<AssetUsage> usages;
        std::vector<std::string> errors;
    };

    struct UsageDocument
    {
        std::filesystem::path path;
        json::Document document;
    };

    void AddEditorAssetReferences(
        json::Document& document, const entt::registry& registry, const std::vector<entt::entity>& entities);
    // Reads source declarations; folder names are not C# namespaces.
    std::vector<ScriptSource> DiscoverScriptSources(const CSharpScriptEditorConfig& config);
    void FindDocumentUsages(
        const BrowserAsset& asset,
        const json::Value& document,
        const std::filesystem::path& path,
        bool live,
        AssetUsageResults& result);
    AssetUsageResults FindAssetUsages(
        const BrowserAsset& asset,
        const std::filesystem::path& resources,
        const CSharpScriptEditorConfig& scripts,
        const std::vector<UsageDocument>& liveDocuments);

    // Stable asset identities, independent of catalog ordering and virtual folders.
    struct AssetBrowserHistory
    {
        std::vector<std::string> favourites;
        std::vector<std::string> recent;
        void Use(const std::string& id);
        void ToggleFavourite(const std::string& id);
        bool IsFavourite(const std::string& id) const;
        void Rename(const std::string& before, const std::string& after);
        void Load(const std::filesystem::path& path);
        void Save(const std::filesystem::path& path) const;
    };
} // namespace sage::editor

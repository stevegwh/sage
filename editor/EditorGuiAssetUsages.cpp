#include "EditorGui.hpp"
#include "engine/AssetKey.hpp"
#include "engine/ResourceManager.hpp"
#include <algorithm>

namespace sage::editor
{
    namespace
    {
        const std::filesystem::path BROWSER_HISTORY_PATH{"resources/Editor/asset-browser.json"};
        std::string Identity(const BrowserAsset& asset)
        {
            return std::to_string(static_cast<int>(asset.kind)) + ":" + asset.key;
        }
    } // namespace

    void EditorGui::ConfigureAssetBrowser(
        CSharpScriptEditorConfig config,
        std::function<AssetUsageResults(const BrowserAsset&)> find,
        std::function<std::string(const AssetUsage&)> navigate)
    {
        scriptConfig = std::move(config);
        findUsages = std::move(find);
        navigateUsage = std::move(navigate);
        try
        {
            assetBrowserHistory.Load(BROWSER_HISTORY_PATH);
        }
        catch (const std::exception& e)
        {
            browserHistoryError = e.what();
        }
        resourceBrowserNeedsRefresh = true;
    }

    BrowserAsset EditorGui::browserAsset(const ResourceEntry& entry) const
    {
        BrowserAsset result;
        if (entry.modelIndex)
        {
            result.kind = BrowserAssetKind::Model;
            result.key = assetEntries.at(*entry.modelIndex).modelKey;
            const auto& sourcePath = assetEntries.at(*entry.modelIndex).sourcePath;
            if (!sourcePath.empty()) result.aliases.push_back(sourcePath.generic_string());
            result.aliases.push_back("resources/" + result.key);
            // Only include a basename alias when it resolves unambiguously.
            const auto alias = AssetNameFromKey(result.key);
            if (std::ranges::count_if(assetEntries, [&](const auto& asset) {
                    return AssetNameFromKey(asset.modelKey) == alias;
                }) == 1)
                result.aliases.push_back(alias);
        }
        else if (entry.materialIndex)
        {
            result.kind = BrowserAssetKind::Material;
            result.key = materialKeys.at(*entry.materialIndex);
            const auto& resources = ResourceManager::GetInstance();
            for (const auto& key : materialKeys)
                if (&resources.GetMaterial(key) == &resources.GetMaterial(result.key))
                    result.aliases.push_back(key);
        }
        else if (entry.imageIndex)
        {
            result.kind = BrowserAssetKind::Image;
            result.key = imageKeys.at(*entry.imageIndex);
            if (!entry.sourcePath.empty()) result.aliases.push_back(entry.sourcePath.generic_string());
            const auto alias = AssetNameFromKey(result.key);
            if (std::ranges::count_if(
                    imageKeys, [&](const auto& key) { return AssetNameFromKey(key) == alias; }) == 1)
                result.aliases.push_back(alias);
        }
        else if (entry.flatpackIndex)
        {
            result.kind = BrowserAssetKind::Flatpack;
            result.key = flatpackEntries.at(*entry.flatpackIndex).path.lexically_normal().generic_string();
        }
        else if (entry.scriptIndex)
        {
            result.kind = BrowserAssetKind::Script;
            result.key = entry.path.generic_string();
            result.aliases = scriptEntries.at(*entry.scriptIndex).types;
        }
        else
        {
            result.kind = BrowserAssetKind::Canvas;
            result.key = entry.sourcePath.lexically_normal().generic_string();
        }
        return result;
    }
    std::string EditorGui::browserAssetId(const ResourceEntry& entry) const
    {
        // Collections only need identity; avoid resolving aliases for every tile each frame.
        if (entry.modelIndex)
            return Identity({.kind = BrowserAssetKind::Model, .key = assetEntries.at(*entry.modelIndex).modelKey});
        if (entry.materialIndex)
            return Identity({.kind = BrowserAssetKind::Material, .key = materialKeys.at(*entry.materialIndex)});
        if (entry.imageIndex)
            return Identity({.kind = BrowserAssetKind::Image, .key = imageKeys.at(*entry.imageIndex)});
        if (entry.flatpackIndex)
            return Identity(
                {.kind = BrowserAssetKind::Flatpack,
                 .key = flatpackEntries.at(*entry.flatpackIndex).path.lexically_normal().generic_string()});
        if (entry.scriptIndex)
            return Identity({.kind = BrowserAssetKind::Script, .key = entry.path.generic_string()});
        return Identity(
            {.kind = BrowserAssetKind::Canvas, .key = entry.sourcePath.lexically_normal().generic_string()});
    }
    void EditorGui::saveBrowserHistory()
    {
        try
        {
            assetBrowserHistory.Save(BROWSER_HISTORY_PATH);
            browserHistoryError.clear();
        }
        catch (const std::exception& e)
        {
            browserHistoryError = e.what();
        }
    }
    void EditorGui::drawResourceActions(const ResourceEntry& entry)
    {
        const auto id = browserAssetId(entry);
        if (ImGui::MenuItem(assetBrowserHistory.IsFavourite(id) ? "Remove from Favourites" : "Add to Favourites"))
        {
            assetBrowserHistory.ToggleFavourite(id);
            saveBrowserHistory();
        }
        if (ImGui::MenuItem("Find Usages", nullptr, false, static_cast<bool>(findUsages)))
        {
            usageAsset = browserAsset(entry);
            assetBrowserHistory.Use(id);
            saveBrowserHistory();
            usageWindowOpen = true;
            refreshAssetUsages();
        }
        ImGui::Separator();
    }
    void EditorGui::refreshAssetUsages()
    {
        usageStatus.clear();
        usageResults = {};
        if (!usageAsset || !findUsages) return;
        try
        {
            usageResults = findUsages(*usageAsset);
        }
        catch (const std::exception& e)
        {
            usageResults.errors.push_back(e.what());
        }
    }
    void EditorGui::DrawAssetUsages()
    {
        if (!usageWindowOpen || !usageAsset) return;
        ImGui::SetNextWindowSize({760, 400}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Asset Usages", &usageWindowOpen))
        {
            ImGui::TextWrapped("%s", usageAsset->key.c_str());
            if (ImGui::Button("Refresh")) refreshAssetUsages();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu references", usageResults.usages.size());
            ImGui::TextWrapped(
                "Open documents include unsaved changes. Results cover saved maps, flatpacks, canvases, and "
                "literal asset references in scripts.");
            if (!usageStatus.empty()) ImGui::TextWrapped("%s", usageStatus.c_str());
            if (usageResults.usages.empty())
                ImGui::TextUnformatted(
                    usageResults.errors.empty() ? "No references found."
                                                : "No references found in the files that could be scanned.");
            std::optional<AssetUsage> navigateTo;
            if (ImGui::BeginTable(
                    "usages",
                    3,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                    {0, 220}))
            {
                ImGui::TableSetupColumn("Document");
                ImGui::TableSetupColumn("Object / node");
                ImGui::TableSetupColumn("Reference");
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < usageResults.usages.size(); ++i)
                {
                    const auto& usage = usageResults.usages.at(i);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const auto label =
                        (usage.path.empty() ? std::string("Untitled") : usage.path.generic_string()) +
                        (usage.live ? " (open)" : "");
                    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                        navigateTo = usage;
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(usage.location.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(usage.field.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            for (const auto& error : usageResults.errors)
                ImGui::TextWrapped("Scan issue: %s", error.c_str());
            if (navigateTo && navigateUsage)
            {
                try
                {
                    usageStatus = navigateUsage(*navigateTo);
                }
                catch (const std::exception& e)
                {
                    usageStatus = e.what();
                }
            }
        }
        ImGui::End();
    }
} // namespace sage::editor

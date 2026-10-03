#include "EditorAssetUsage.hpp"
#include "cereal/types/vector.hpp"
#include "EditorComponents.hpp"
#include "engine/content/ContentDocument.hpp"
#include <algorithm>
#include <regex>
#include <set>

namespace sage::editor
{
    namespace
    {
        constexpr std::size_t RECENT_LIMIT = 32;
        std::string Normalized(std::string value)
        {
            std::ranges::replace(value, '\\', '/');
            return std::filesystem::path(value).lexically_normal().generic_string();
        }
        bool Matches(const BrowserAsset& asset, std::string value)
        {
            // Mutable model resource keys append material overrides after this separator.
            if (asset.kind == BrowserAssetKind::Model) value = value.substr(0, value.find('\x1f'));
            const auto normalized = Normalized(value);
            const auto matches = [&](const std::string& key) { return normalized == Normalized(key); };
            return matches(asset.key) || std::ranges::any_of(asset.aliases, matches);
        }
        bool ReferenceField(BrowserAssetKind kind, std::string_view field)
        {
            switch (kind)
            {
            case BrowserAssetKind::Script:
                return field == "className" || field == "script";
            case BrowserAssetKind::Model:
                return field == "key" || field == "modelKey" || field == "assetKey" || field == "editorAssetKey";
            case BrowserAssetKind::Material:
                return field == "materials" || field == "materialKey";
            case BrowserAssetKind::Image:
                return field == "image" || field == "backgroundImage" || field == "texture" ||
                       field == "texture0Key" || field == "texture1Key" || field == "textures";
            case BrowserAssetKind::Flatpack:
                return field == "assetKey" || field == "flatpack" || field == "flatpackPath" ||
                       field == "editorAssetKey";
            case BrowserAssetKind::Canvas:
                return field == "initialCanvases" || field == "canvasPath";
            }
            return false;
        }
        std::string Name(const json::Value& value, const std::string& fallback)
        {
            return value.HasMember("name") && value["name"].IsString() ? value["name"].GetString() : fallback;
        }
        bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return !a.empty() && !b.empty() &&
                   std::filesystem::absolute(a).lexically_normal() ==
                       std::filesystem::absolute(b).lexically_normal();
        }
    } // namespace

    std::string BrowserAsset::Id() const
    {
        return std::to_string(static_cast<int>(kind)) + ":" + key;
    }

    void AddEditorAssetReferences(
        json::Document& document, const entt::registry& registry, const std::vector<entt::entity>& entities)
    {
        std::unordered_map<std::uint64_t, std::string> references;
        for (auto entity : entities)
            if (registry.all_of<PersistentEntityId, AssetReference>(entity))
                references.emplace(
                    registry.get<PersistentEntityId>(entity).id, registry.get<AssetReference>(entity).assetKey);
        for (auto& node : document["entities"].GetArray())
            if (const auto found = references.find(json::Id(node, "id")); found != references.end())
                json::Put(node, "editorAssetKey", found->second, document.GetAllocator());
    }

    std::vector<ScriptSource> DiscoverScriptSources(const CSharpScriptEditorConfig& config)
    {
        std::vector<ScriptSource> result;
        if (!config.IsConfigured() || !std::filesystem::is_directory(config.sourceDirectory)) return result;
        const std::regex nsExpression(R"(\bnamespace\s+([\w.]+))");
        const std::regex typeExpression(R"(\b(?:class|struct|record)\s+(\w+))");
        for (auto it = std::filesystem::recursive_directory_iterator(config.sourceDirectory);
             it != std::filesystem::recursive_directory_iterator();
             ++it)
        {
            if (it->is_directory() && (it->path().filename() == "bin" || it->path().filename() == "obj" ||
                                       it->path().filename().string().starts_with('.')))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file() || it->path().extension() != ".cs") continue;
            ScriptSource entry{.path = it->path()};
            const auto source = json::Read(entry.path);
            std::smatch ns;
            if (std::regex_search(source, ns, nsExpression))
                for (auto match = std::sregex_iterator(source.begin(), source.end(), typeExpression);
                     match != std::sregex_iterator();
                     ++match)
                    entry.types.push_back(ns.str(1) + "." + match->str(1));
            result.push_back(std::move(entry));
        }
        std::ranges::sort(result, {}, &ScriptSource::path);
        return result;
    }

    void FindDocumentUsages(
        const BrowserAsset& asset,
        const json::Value& document,
        const std::filesystem::path& path,
        bool live,
        AssetUsageResults& result)
    {
        const auto visit = [&](auto& self,
                               const json::Value& value,
                               const AssetUsage& context,
                               const std::string& field,
                               bool reference) -> void {
            if (value.IsString())
            {
                if (asset.kind == BrowserAssetKind::Material && field == "sage.Renderable.data.key")
                {
                    const std::string key = value.GetString();
                    for (auto start = key.find('\x1f'); start != std::string::npos;)
                    {
                        ++start;
                        const auto end = key.find('\x1e', start);
                        if (Matches(asset, key.substr(start, end - start)))
                        {
                            auto usage = context;
                            usage.field = "Material override";
                            result.usages.push_back(std::move(usage));
                        }
                        start = end;
                    }
                }
                if (reference && Matches(asset, value.GetString()))
                {
                    auto usage = context;
                    usage.field = field;
                    result.usages.push_back(std::move(usage));
                }
            }
            else if (value.IsArray())
                for (const auto& child : value.GetArray())
                    self(self, child, context, field, reference);
            else if (value.IsObject())
                for (const auto& member : value.GetObject())
                {
                    const std::string key = member.name.GetString();
                    self(
                        self,
                        member.value,
                        context,
                        field.empty() ? key : field + "." + key,
                        ReferenceField(asset.kind, key) || reference);
                }
        };
        if (!document.IsObject()) return;
        AssetUsage context{.path = path, .location = "Scene", .live = live};
        if (document.HasMember("entities") && document["entities"].IsArray())
        {
            if (document.HasMember("initialCanvases"))
                visit(
                    visit,
                    document["initialCanvases"],
                    context,
                    "initialCanvases",
                    asset.kind == BrowserAssetKind::Canvas);
            for (const auto& entity : document["entities"].GetArray())
            {
                if (!entity.IsObject()) continue;
                context.location = Name(entity, "Entity");
                context.entityId = json::Id(entity, "id");
                if (entity.HasMember("editorAssetKey"))
                    visit(
                        visit,
                        entity["editorAssetKey"],
                        context,
                        "editorAssetKey",
                        asset.kind == BrowserAssetKind::Flatpack);
                if (entity.HasMember("components")) visit(visit, entity["components"], context, "", false);
            }
        }
        else if (document.HasMember("nodes") && document["nodes"].IsArray())
            for (const auto& node : document["nodes"].GetArray())
            {
                if (!node.IsObject()) continue;
                context.location = Name(node, "Canvas node");
                context.nodeId = static_cast<unsigned int>(json::Id(node, "id"));
                visit(visit, node, context, "", false);
            }
    }

    AssetUsageResults FindAssetUsages(
        const BrowserAsset& asset,
        const std::filesystem::path& resources,
        const CSharpScriptEditorConfig& scripts,
        const std::vector<UsageDocument>& liveDocuments)
    {
        AssetUsageResults result;
        for (const auto& live : liveDocuments)
            FindDocumentUsages(asset, live.document, live.path, true, result);
        if (std::filesystem::is_directory(resources))
            for (auto it = std::filesystem::recursive_directory_iterator(resources);
                 it != std::filesystem::recursive_directory_iterator();
                 ++it)
            {
                if (it->is_directory() &&
                    (it->path().filename().string().starts_with('.') || it->path().filename() == "Editor"))
                {
                    it.disable_recursion_pending();
                    continue;
                }
                const auto path = it->path();
                const auto extension = path.extension();
                if (!it->is_regular_file() ||
                    (extension != ".map" && extension != ".flatpack" && extension != ".canvas"))
                    continue;
                if (std::ranges::any_of(
                        liveDocuments, [&](const auto& live) { return SamePath(path, live.path); }))
                    continue; // Unsaved state is authoritative for an open document.
                try
                {
                    const auto document =
                        extension == ".canvas" ? json::Parse(json::Read(path)) : content::ReadDocument(path);
                    FindDocumentUsages(asset, document, path, false, result);
                }
                catch (const std::exception& e)
                {
                    result.errors.push_back(path.string() + ": " + e.what());
                }
            }
        // Literal asset references in managed source, such as UI.Instantiate and texture paths.
        if (asset.kind != BrowserAssetKind::Script)
        {
            const std::regex literal(R"re("([^"\r\n]*)")re");
            for (const auto& script : DiscoverScriptSources(scripts))
            {
                const auto source = json::Read(script.path);
                for (auto match = std::sregex_iterator(source.begin(), source.end(), literal);
                     match != std::sregex_iterator();
                     ++match)
                    if (Matches(asset, match->str(1)))
                    {
                        const auto end = source.begin() + match->position();
                        const auto line = std::count(source.begin(), end, '\n') + 1;
                        result.usages.push_back(
                            {.path = script.path,
                             .location = "Line " + std::to_string(line),
                             .field = "Source literal"});
                    }
            }
        }
        return result;
    }

    void AssetBrowserHistory::Use(const std::string& id)
    {
        std::erase(recent, id);
        recent.insert(recent.begin(), id);
        if (recent.size() > RECENT_LIMIT) recent.resize(RECENT_LIMIT);
    }
    bool AssetBrowserHistory::IsFavourite(const std::string& id) const
    {
        return std::ranges::find(favourites, id) != favourites.end();
    }
    void AssetBrowserHistory::ToggleFavourite(const std::string& id)
    {
        if (IsFavourite(id))
            std::erase(favourites, id);
        else
            favourites.push_back(id);
    }
    void AssetBrowserHistory::Rename(const std::string& before, const std::string& after)
    {
        for (auto* list : {&recent, &favourites})
        {
            for (auto& id : *list)
                if (id == before) id = after;
            std::set<std::string> seen;
            std::erase_if(*list, [&](const auto& id) { return !seen.insert(id).second; });
        }
    }
    void AssetBrowserHistory::Load(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path)) return;
        const auto value = json::Parse(json::Read(path));
        json::Decode(json::Require(value, "favourites"), favourites);
        json::Decode(json::Require(value, "recent"), recent);
        if (recent.size() > RECENT_LIMIT) recent.resize(RECENT_LIMIT);
    }
    void AssetBrowserHistory::Save(const std::filesystem::path& path) const
    {
        json::Document value(rapidjson::kObjectType);
        json::Put(value, "favourites", json::Encode(favourites), value.GetAllocator());
        json::Put(value, "recent", json::Encode(recent), value.GetAllocator());
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".writing";
        std::ofstream output(temporary);
        output << json::Stringify(value);
        output.close();
        if (!output) throw std::runtime_error("Could not save asset browser history");
        std::filesystem::rename(temporary, path);
    }
} // namespace sage::editor

#include "EditorAssetUsage.hpp"
#include "EditorComponents.hpp"
#include "EditorMapLoader.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/ui/CanvasDocument.hpp"
#include <chrono>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
} // namespace
int main()
{
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("sage-asset-usages-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try
    {
        using namespace sage;
        using namespace sage::editor;
        std::filesystem::create_directories(temporary / "resources/ui");
        std::filesystem::create_directories(temporary / "managed/states");
        std::filesystem::create_directories(temporary / "managed/obj");
        {
            std::ofstream source(temporary / "managed/states/Resident.cs");
            source << "namespace Example.Scripts; public class Resident : Sage.Script {}";
            std::ofstream loader(temporary / "managed/Loader.cs");
            loader << "namespace Example.Scripts; public class Loader { string path = "
                      "\"resources/ui/Screen.canvas\"; }";
            std::ofstream generated(temporary / "managed/obj/Generated.cs");
            generated << "class Ignored {}";
        }
        CSharpScriptEditorConfig scripts{
            .sourceDirectory = temporary / "managed", .rootNamespace = "Example.Scripts"};
        const auto sources = DiscoverScriptSources(scripts);
        require(sources.size() == 2, "Generated script files were included");
        require(
            sources.back().types.front() == "Example.Scripts.Resident", "Folder was mistaken for C# namespace");
        entt::registry registry;
        TransformSystem transforms(&registry);
        const auto entity = registry.create();
        registry.emplace<sgTransform>(entity).name = "Resident";
        registry.emplace<ScriptComponent>(entity).className = "Example.Scripts.Resident";
        registry.emplace<AssetReference>(entity).assetKey = "resources/flatpacks/Home.flatpack";
        auto map = content::Capture(registry, {entity});
        AddEditorAssetReferences(map, registry, {entity});
        content::WriteDocument(temporary / "resources/test.map", map);
        const BrowserAsset script{
            .kind = BrowserAssetKind::Script,
            .key = "Scripts/states/Resident.cs",
            .aliases = {"Example.Scripts.Resident"}};
        auto usages = FindAssetUsages(script, temporary / "resources", scripts, {});
        require(
            usages.usages.size() == 1 && usages.usages.front().location == "Resident" &&
                usages.usages.front().entityId.has_value(),
            "Script attachment was not located");
        CanvasDocument canvas;
        canvas.nodes.front().script = "Example.Scripts.Resident";
        canvas.nodes.front().name = "Controller";
        canvas.Save(temporary / "resources/ui/Screen.canvas");
        usages = FindAssetUsages(script, temporary / "resources", scripts, {});
        require(
            usages.usages.size() == 2 &&
                std::ranges::count_if(usages.usages, [](const auto& usage) { return usage.nodeId == 1; }) == 1,
            "Canvas attachment was not located");
        registry.get<ScriptComponent>(entity).className = "Example.Scripts.Other";
        std::vector<UsageDocument> live;
        live.push_back(
            {.path = temporary / "resources/test.map", .document = content::Capture(registry, {entity})});
        usages = FindAssetUsages(script, temporary / "resources", scripts, live);
        require(
            usages.usages.size() == 1 && usages.usages.front().nodeId,
            "Stale saved attachment overrode unsaved state");
        const BrowserAsset screen{.kind = BrowserAssetKind::Canvas, .key = "resources/ui/Screen.canvas"};
        usages = FindAssetUsages(screen, temporary / "resources", scripts, {});
        require(
            usages.usages.size() == 1 && usages.usages.front().field == "Source literal",
            "Script asset literal was not found");
        const BrowserAsset flatpack{
            .kind = BrowserAssetKind::Flatpack, .key = "resources/flatpacks/Home.flatpack"};
        usages = FindAssetUsages(flatpack, temporary / "resources", scripts, {});
        require(usages.usages.size() == 1, "Saved flatpack source identity was lost");
        entt::registry loaded;
        TransformSystem loadedTransforms(&loaded);
        require(LoadMap(loaded, (temporary / "resources/test.map").c_str()), "Map could not be loaded");
        const auto restored = content::FindEntityById(loaded, json::Id(map["entities"][0], "id"));
        require(
            loaded.get<AssetReference>(restored).assetKey == flatpack.key,
            "Map reload lost flatpack source identity");
        auto custom = json::Parse(
            R"({"entities":[{"id":1,"name":"Material user","components":{"sage.Renderable":{"data":{"key":"models/rock.obj\u001fStone\u001eWood"}},"Text":{"data":{"text":"Stone"}}}}]})");
        AssetUsageResults overrides;
        FindDocumentUsages({.kind = BrowserAssetKind::Material, .key = "Stone"}, custom, {}, true, overrides);
        require(
            overrides.usages.size() == 1 && overrides.usages.front().field == "Material override",
            "Material override search matched prose or missed serialized key");
        {
            std::ofstream broken(temporary / "resources/broken.canvas");
            broken << "{broken";
        }
        usages = FindAssetUsages(script, temporary / "resources", scripts, {});
        require(usages.errors.size() == 1, "Unreadable content was silently ignored");
        AssetBrowserHistory history;
        history.ToggleFavourite("model:old");
        history.Use("model:old");
        history.Use("image:other");
        history.Use("model:old");
        require(
            history.recent.size() == 2 && history.recent.front() == "model:old",
            "Recent assets were not unique and newest first");
        history.Rename("model:old", "model:new");
        history.Save(temporary / "preferences/history.json");
        AssetBrowserHistory restoredHistory;
        restoredHistory.Load(temporary / "preferences/history.json");
        require(
            restoredHistory.IsFavourite("model:new") && restoredHistory.recent == history.recent,
            "Favourites or recents did not survive rename and reload");
        restoredHistory.ToggleFavourite("model:new");
        require(!restoredHistory.IsFavourite("model:new"), "Favourite could not be removed");
        std::cout << "Asset usages, script discovery, live drafts, provenance, and browser history passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(temporary);
    return result;
}

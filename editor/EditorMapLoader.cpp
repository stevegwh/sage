#include "EditorMapLoader.hpp"
#include "EditorAssetUsage.hpp"
#include "EditorComponents.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/EditorLayoutMapFormat.hpp"
#include <iostream>

namespace sage::editor
{
    bool IsEditorLayoutMap(const char* path)
    {
        try
        {
            return json::String(content::ReadDocument(path), "kind") == "map";
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    bool LoadMap(entt::registry& destination, const char* path, const std::function<void()>& updateLoadingScreen)
    {
        try
        {
            auto document = content::ReadDocument(path);
            if (json::String(document, "kind") != "map") throw std::runtime_error("Expected a map");
            const auto result = content::Instantiate(destination, document, {}, false, updateLoadingScreen);
            for (auto entity : result.entities)
            {
                destination.emplace<EditorMapEntity>(entity);
                auto& transform = destination.get<sgTransform>(entity);
                if (editor_layout::IsMapBaseTransform(transform))
                {
                    destination.emplace<EditorMapBase>(entity);
                    if (auto* renderable = destination.try_get<Renderable>(entity)) renderable->active = false;
                }
            }
            for (const auto& node : document["entities"].GetArray())
                if (node.HasMember("editorAssetKey"))
                    destination.emplace_or_replace<AssetReference>(
                        content::FindEntityById(destination, json::Id(node, "id")),
                        json::String(node, "editorAssetKey"));
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "LoadMap: " << error.what() << '\n';
            return false;
        }
    }
    bool SaveMap(entt::registry& source, const char* path)
    {
        return SaveMap(source, path, {});
    }
    bool SaveMap(entt::registry& source, const char* path, const std::vector<entt::entity>& hierarchyOrder)
    {
        try
        {
            auto entities = hierarchyOrder;
            for (auto entity : source.view<EditorMapEntity>())
                if (std::ranges::find(entities, entity) == entities.end()) entities.push_back(entity);
            auto document = content::Capture(source, entities);
            AddEditorAssetReferences(document, source, entities);
            content::WriteDocument(path, document);
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "SaveMap: " << error.what() << '\n';
            return false;
        }
    }
} // namespace sage::editor

#include "EditorMapLoader.hpp"
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
    bool LoadMap(entt::registry* destination, const char* path, const InspectorRegistry*)
    {
        try
        {
            auto document = content::ReadDocument(path);
            if (json::String(document, "kind") != "map") throw std::runtime_error("Expected a map");
            const auto result = content::Instantiate(*destination, document);
            for (auto entity : result.entities)
            {
                destination->emplace<EditorMapEntity>(entity);
                auto& transform = destination->get<sgTransform>(entity);
                if (editor_layout::IsMapBaseTransform(transform))
                {
                    destination->emplace<EditorMapBase>(entity);
                    if (auto* renderable = destination->try_get<Renderable>(entity)) renderable->active = false;
                }
            }
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "LoadMap: " << error.what() << '\n';
            return false;
        }
    }
    bool SaveMap(entt::registry& source, const char* path, const InspectorRegistry* components)
    {
        return SaveMap(source, path, {}, components);
    }
    bool SaveMap(
        entt::registry& source,
        const char* path,
        const std::vector<entt::entity>& hierarchyOrder,
        const InspectorRegistry*)
    {
        try
        {
            auto entities = hierarchyOrder;
            for (auto entity : source.view<EditorMapEntity>())
                if (std::find(entities.begin(), entities.end(), entity) == entities.end())
                    entities.push_back(entity);
            content::WriteDocument(path, content::Capture(source, entities));
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "SaveMap: " << error.what() << '\n';
            return false;
        }
    }
} // namespace sage::editor

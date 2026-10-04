#include "EditorHierarchyTree.hpp"

#include "EditorComponents.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/DoorBehaviorComponent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/SpatialAudioComponent.hpp"
#include "engine/Light.hpp"
#include "engine/SceneTags.hpp"
#include "engine/ui/CanvasSystem.hpp"

#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <format>

namespace sage::editor
{
    namespace
    {
        std::string entityName(const entt::entity entity)
        {
            return std::format("entity_{}", entt::to_integral(entity));
        }
    } // namespace

    EditorHierarchyTree::EditorHierarchyTree(entt::registry& source) : registry(source)
    {
    }

    std::string EditorHierarchyTree::GetEntityName(const entt::entity entity) const
    {
        if (!registry.valid(entity))
        {
            return entityName(entity);
        }

        if (registry.any_of<sgTransform>(entity))
        {
            const auto& transform = registry.get<sgTransform>(entity);
            return transform.name.empty() ? entityName(entity) : transform.name;
        }
        if (registry.any_of<Light>(entity))
        {
            return std::format("light_{}", entt::to_integral(entity));
        }
        if (const auto* node = registry.try_get<UINode>(entity)) return "UI: " + node->data.name;
        return entityName(entity);
    }

    const char* EditorHierarchyTree::GetEntityIcon(const entt::entity entity) const
    {
        if (!registry.valid(entity))
        {
            return ICON_FA_CIRCLE;
        }

        // Ordered most-specific first: an entity may carry several of these components,
        // and the first match wins so the icon reflects the entity's primary role.
        if (registry.any_of<Light>(entity)) return ICON_FA_LIGHTBULB;
        if (registry.any_of<DoorBehaviorComponent>(entity)) return ICON_FA_DOOR_OPEN;
        if (registry.any_of<SpatialAudioComponent>(entity)) return ICON_FA_VOLUME_HIGH;
        if (const auto* meta = registry.try_get<MetaData>(entity);
            meta != nullptr && HasTag(*meta, SPAWN_POINT_TAG))
        {
            return ICON_FA_LOCATION_DOT;
        }
        if (registry.any_of<Renderable>(entity)) return ICON_FA_CUBE;
        if (registry.any_of<Collideable>(entity)) return ICON_FA_VECTOR_SQUARE;

        return ICON_FA_CIRCLE;
    }

    std::vector<EditorGui::SceneObjectEntry> EditorHierarchyTree::CollectSceneObjectEntries(
        const bool includeRuntimeEntities) const
    {
        std::vector<entt::entity> roots;
        auto view = registry.view<sgTransform>();
        for (const auto entity : view)
        {
            const auto parent = view.get<sgTransform>(entity).GetParent();
            if (parent == entt::null || !registry.valid(parent) || !registry.any_of<sgTransform>(parent))
            {
                roots.push_back(entity);
            }
        }

        if (includeRuntimeEntities)
        {
            for (const auto [entity] : registry.storage<entt::entity>().each())
                if (!registry.any_of<sgTransform>(entity)) roots.push_back(entity);
        }

        std::ranges::sort(roots, [](const entt::entity lhs, const entt::entity rhs) {
            return entt::to_integral(lhs) < entt::to_integral(rhs);
        });
        syncRootOrder(roots);

        std::vector<EditorGui::SceneObjectEntry> entries;
        entries.reserve(roots.size());
        for (const auto root : roots)
        {
            appendSceneObjectEntry(entries, root, entt::null, 0);
        }
        return entries;
    }

    void EditorHierarchyTree::appendSceneObjectEntry(
        std::vector<EditorGui::SceneObjectEntry>& entries,
        const entt::entity entity,
        const entt::entity parent,
        const int depth) const
    {
        if (!registry.valid(entity)) return;

        entries.push_back(
            {.entity = entity,
             .parent = parent,
             .displayName = GetEntityName(entity),
             .icon = GetEntityIcon(entity),
             .depth = depth});

        if (!registry.any_of<sgTransform>(entity)) return;
        for (const auto child : registry.get<sgTransform>(entity).GetChildren())
        {
            appendSceneObjectEntry(entries, child, entity, depth + 1);
        }
    }

    void EditorHierarchyTree::syncRootOrder(std::vector<entt::entity>& roots) const
    {
        auto isCurrentRoot = [&roots](const entt::entity entity) {
            return std::ranges::find(roots, entity) != roots.end();
        };

        std::erase_if(rootOrder, [&](const entt::entity entity) {
            return !registry.valid(entity) || !isCurrentRoot(entity);
        });

        for (const auto root : roots)
        {
            if (std::ranges::find(rootOrder, root) == rootOrder.end())
            {
                rootOrder.push_back(root);
            }
        }

        roots = rootOrder;
    }

    void EditorHierarchyTree::NoteHierarchyMove(
        const entt::entity dragged, const entt::entity newParent, const entt::entity insertBefore)
    {
        std::erase(rootOrder, dragged);
        if (newParent != entt::null) return;

        if (insertBefore != entt::null)
        {
            const auto insertAt = std::ranges::find(rootOrder, insertBefore);
            if (insertAt != rootOrder.end())
            {
                rootOrder.insert(insertAt, dragged);
                return;
            }
        }
        rootOrder.push_back(dragged);
    }
} // namespace sage::editor

#include "EditorHierarchyTree.hpp"

#include "EditorComponents.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/DoorBehaviorComponent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/SpatialAudioComponent.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/components/TriggerVolume.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Light.hpp"

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

    EditorHierarchyTree::EditorHierarchyTree(EngineSystems* _sys) : sys(_sys)
    {
    }

    std::string EditorHierarchyTree::GetEntityName(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity))
        {
            return entityName(entity);
        }

        if (sys->registry->any_of<sgTransform>(entity))
        {
            const auto& transform = sys->registry->get<sgTransform>(entity);
            return transform.name.empty() ? entityName(entity) : transform.name;
        }
        if (sys->registry->any_of<Light>(entity))
        {
            return std::format("light_{}", entt::to_integral(entity));
        }
        return entityName(entity);
    }

    const char* EditorHierarchyTree::GetEntityIcon(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity))
        {
            return ICON_FA_CIRCLE;
        }

        // Ordered most-specific first: an entity may carry several of these components,
        // and the first match wins so the icon reflects the entity's primary role.
        if (sys->registry->any_of<Light>(entity)) return ICON_FA_LIGHTBULB;
        if (sys->registry->any_of<DoorBehaviorComponent>(entity)) return ICON_FA_DOOR_OPEN;
        if (sys->registry->any_of<SpatialAudioComponent>(entity)) return ICON_FA_VOLUME_HIGH;
        if (sys->registry->any_of<Spawner>(entity)) return ICON_FA_LOCATION_DOT;
        if (sys->registry->any_of<TriggerVolume>(entity)) return ICON_FA_VECTOR_SQUARE;
        if (sys->registry->any_of<Renderable>(entity)) return ICON_FA_CUBE;
        if (sys->registry->any_of<Collideable>(entity)) return ICON_FA_VECTOR_SQUARE;

        return ICON_FA_CIRCLE;
    }

    std::vector<EditorGui::SceneObjectEntry> EditorHierarchyTree::CollectSceneObjectEntries() const
    {
        std::vector<entt::entity> roots;
        auto view = sys->registry->view<sgTransform>();
        for (const auto entity : view)
        {
            const auto parent = view.get<sgTransform>(entity).GetParent();
            if (parent == entt::null || !sys->registry->valid(parent) ||
                !sys->registry->any_of<sgTransform>(parent))
            {
                roots.push_back(entity);
            }
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
        if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) return;

        entries.push_back(
            {.entity = entity,
             .parent = parent,
             .displayName = GetEntityName(entity),
             .icon = GetEntityIcon(entity),
             .depth = depth});

        for (const auto child : sys->registry->get<sgTransform>(entity).GetChildren())
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
            return !sys->registry->valid(entity) || !isCurrentRoot(entity);
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

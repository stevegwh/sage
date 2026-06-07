#include "EditorSelection.hpp"

#include "engine/EngineSystems.hpp"
#include "engine/components/sgTransform.hpp"

#include <algorithm>
#include <utility>

namespace sage::editor
{
    EditorSelection::EditorSelection(EngineSystems* _sys) : sys(_sys)
    {
    }

    bool EditorSelection::isSelectable(const entt::entity entity) const
    {
        return sys != nullptr && sys->registry != nullptr && sys->registry->valid(entity) &&
               sys->registry->any_of<sgTransform>(entity);
    }

    bool EditorSelection::isAncestorOf(const entt::entity ancestor, entt::entity entity) const
    {
        if (!isSelectable(ancestor) || !isSelectable(entity)) return false;

        while (entity != entt::null && isSelectable(entity))
        {
            entity = sys->registry->get<sgTransform>(entity).GetParent();
            if (entity == ancestor) return true;
        }
        return false;
    }

    void EditorSelection::appendEffective(
        const entt::entity entity, std::vector<entt::entity>& out) const
    {
        if (!isSelectable(entity)) return;
        if (std::ranges::find(out, entity) != out.end()) return;

        out.push_back(entity);
        for (const auto child : sys->registry->get<sgTransform>(entity).GetChildren())
        {
            appendEffective(child, out);
        }
    }

    void EditorSelection::normalizeRoots()
    {
        std::erase_if(selectedRoots, [this](const entt::entity entity) {
            return !isSelectable(entity);
        });

        std::vector<entt::entity> normalized;
        normalized.reserve(selectedRoots.size());
        for (const auto entity : selectedRoots)
        {
            if (std::ranges::find(normalized, entity) != normalized.end()) continue;

            const bool coveredByAncestor = std::ranges::any_of(normalized, [this, entity](const entt::entity root) {
                return isAncestorOf(root, entity);
            });
            if (coveredByAncestor) continue;

            std::erase_if(normalized, [this, entity](const entt::entity root) {
                return isAncestorOf(entity, root);
            });
            normalized.push_back(entity);
        }

        selectedRoots = std::move(normalized);
    }

    bool EditorSelection::Select(const entt::entity entity)
    {
        return Select(entity, false);
    }

    bool EditorSelection::Select(const entt::entity entity, const bool additive)
    {
        return additive ? Toggle(entity) : [&]() {
            if (!isSelectable(entity)) return false;
            selectedRoots = {entity};
            normalizeRoots();
            return true;
        }();
    }

    bool EditorSelection::Add(const entt::entity entity)
    {
        if (!isSelectable(entity)) return false;
        if (ContainsEffective(entity)) return true;

        selectedRoots.push_back(entity);
        normalizeRoots();
        return true;
    }

    bool EditorSelection::Toggle(const entt::entity entity)
    {
        if (!isSelectable(entity)) return false;

        if (const auto it = std::ranges::find(selectedRoots, entity); it != selectedRoots.end())
        {
            selectedRoots.erase(it);
            return true;
        }

        return Add(entity);
    }

    void EditorSelection::Clear()
    {
        selectedRoots.clear();
    }

    bool EditorSelection::HasSelection() const
    {
        return !selectedRoots.empty();
    }

    std::optional<entt::entity> EditorSelection::Current() const
    {
        if (selectedRoots.empty()) return std::nullopt;
        const auto entity = selectedRoots.back();
        return isSelectable(entity) ? std::optional{entity} : std::nullopt;
    }

    std::optional<entt::entity> EditorSelection::ActiveTransformEntity() const
    {
        return Current();
    }

    const std::vector<entt::entity>& EditorSelection::Roots() const
    {
        return selectedRoots;
    }

    std::vector<entt::entity> EditorSelection::TransformTargets() const
    {
        std::vector<entt::entity> targets;
        targets.reserve(selectedRoots.size());
        for (const auto entity : selectedRoots)
        {
            if (isSelectable(entity)) targets.push_back(entity);
        }
        return targets;
    }

    std::vector<entt::entity> EditorSelection::EffectiveEntities() const
    {
        std::vector<entt::entity> result;
        for (const auto entity : selectedRoots)
        {
            appendEffective(entity, result);
        }
        return result;
    }

    bool EditorSelection::ContainsRoot(const entt::entity entity) const
    {
        return std::ranges::find(selectedRoots, entity) != selectedRoots.end();
    }

    bool EditorSelection::ContainsEffective(const entt::entity entity) const
    {
        for (const auto root : selectedRoots)
        {
            if (root == entity || isAncestorOf(root, entity)) return true;
        }
        return false;
    }
} // namespace sage::editor

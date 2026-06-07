#include "EditorSelection.hpp"

#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"

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

    bool EditorSelection::coveredByAncestor(const entt::entity entity) const
    {
        return std::ranges::any_of(
            selectedRoots, [this, entity](const entt::entity root) { return isAncestorOf(root, entity); });
    }

    void EditorSelection::appendWithChildren(const entt::entity entity, std::vector<entt::entity>& out) const
    {
        if (!isSelectable(entity)) return;
        if (std::ranges::find(out, entity) != out.end()) return;

        out.push_back(entity);
        for (const auto child : sys->registry->get<sgTransform>(entity).GetChildren())
        {
            appendWithChildren(child, out);
        }
    }

    void EditorSelection::normalizeRoots()
    {
        std::erase_if(selectedRoots, [this](const entt::entity entity) { return !isSelectable(entity); });

        std::vector<entt::entity> normalized;
        normalized.reserve(selectedRoots.size());
        for (const auto entity : selectedRoots)
        {
            if (std::ranges::find(normalized, entity) != normalized.end()) continue;

            const bool covered = std::ranges::any_of(
                normalized, [this, entity](const entt::entity root) { return isAncestorOf(root, entity); });
            if (covered) continue;

            std::erase_if(
                normalized, [this, entity](const entt::entity root) { return isAncestorOf(entity, root); });
            normalized.push_back(entity);
        }

        selectedRoots = std::move(normalized);
    }

    bool EditorSelection::Select(const entt::entity entity)
    {
        if (!isSelectable(entity)) return false;

        selectedRoots = {entity};
        selectionAnchor = entity;
        normalizeRoots();
        return true;
    }

    bool EditorSelection::Toggle(const entt::entity entity)
    {
        if (!isSelectable(entity)) return false;
        selectionAnchor = entity;

        // Already a selected root: toggle it off.
        if (const auto it = std::ranges::find(selectedRoots, entity); it != selectedRoots.end())
        {
            selectedRoots.erase(it);
            return true;
        }

        // Already covered by a selected ancestor: nothing to add.
        if (coveredByAncestor(entity)) return true;

        selectedRoots.push_back(entity);
        normalizeRoots();
        return true;
    }

    bool EditorSelection::SelectRange(const std::vector<entt::entity>& orderedRange)
    {
        std::vector<entt::entity> next;
        next.reserve(orderedRange.size());
        for (const auto entity : orderedRange)
        {
            if (isSelectable(entity)) next.push_back(entity);
        }
        if (next.empty()) return false;

        selectedRoots = std::move(next);
        normalizeRoots();
        return true;
    }

    void EditorSelection::Clear()
    {
        selectedRoots.clear();
        selectionAnchor = entt::null;
    }

    bool EditorSelection::HasSelection() const
    {
        return !selectedRoots.empty();
    }

    std::optional<entt::entity> EditorSelection::Active() const
    {
        if (selectedRoots.empty()) return std::nullopt;
        const auto entity = selectedRoots.back();
        return isSelectable(entity) ? std::optional{entity} : std::nullopt;
    }

    std::vector<entt::entity> EditorSelection::Selected() const
    {
        std::vector<entt::entity> result;
        result.reserve(selectedRoots.size());
        for (const auto entity : selectedRoots)
        {
            if (isSelectable(entity)) result.push_back(entity);
        }
        return result;
    }

    std::vector<entt::entity> EditorSelection::SelectedWithChildren() const
    {
        std::vector<entt::entity> result;
        for (const auto entity : selectedRoots)
        {
            appendWithChildren(entity, result);
        }
        return result;
    }

    entt::entity EditorSelection::Anchor() const
    {
        return isSelectable(selectionAnchor) ? selectionAnchor : entt::null;
    }
} // namespace sage::editor

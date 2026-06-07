#pragma once

#include "entt/entt.hpp"

#include <optional>
#include <vector>

namespace sage
{
    class EngineSystems;
}

namespace sage::editor
{
    class EditorSelection
    {
        EngineSystems* sys;
        std::vector<entt::entity> selectedRoots;
        // Pivot used for range (shift) selection; tracks the last entity that was
        // selected or toggled directly.
        entt::entity selectionAnchor = entt::null;

        [[nodiscard]] bool isSelectable(entt::entity entity) const;
        [[nodiscard]] bool isAncestorOf(entt::entity ancestor, entt::entity entity) const;
        [[nodiscard]] bool coveredByAncestor(entt::entity entity) const;
        void appendWithChildren(entt::entity entity, std::vector<entt::entity>& out) const;
        void normalizeRoots();

      public:
        explicit EditorSelection(EngineSystems* sys);

        // Mutators return true when the selection changed in a meaningful way.
        // Select the entity exclusively, discarding any previous selection.
        [[nodiscard]] bool Select(entt::entity entity);
        // Add the entity if absent, remove it if already selected.
        [[nodiscard]] bool Toggle(entt::entity entity);
        // Replace the selection with the given ordered run. The anchor is left
        // untouched so successive shift-clicks pivot around the same entity.
        [[nodiscard]] bool SelectRange(const std::vector<entt::entity>& orderedRange);
        void Clear();

        [[nodiscard]] bool HasSelection() const;
        // The most recently selected entity (range/transform pivot for the UI).
        [[nodiscard]] std::optional<entt::entity> Active() const;
        // The explicitly selected entities, normalized so no entry is a
        // descendant of another.
        [[nodiscard]] std::vector<entt::entity> Selected() const;
        // Selected() plus every descendant of each selected entity.
        [[nodiscard]] std::vector<entt::entity> SelectedWithChildren() const;
        // The pivot for range (shift) selection.
        [[nodiscard]] entt::entity Anchor() const;
    };
} // namespace sage::editor

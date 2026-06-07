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

        [[nodiscard]] bool isSelectable(entt::entity entity) const;
        [[nodiscard]] bool isAncestorOf(entt::entity ancestor, entt::entity entity) const;
        void appendEffective(entt::entity entity, std::vector<entt::entity>& out) const;
        void normalizeRoots();

      public:
        explicit EditorSelection(EngineSystems* sys);

        bool Select(entt::entity entity);
        bool Select(entt::entity entity, bool additive);
        bool Add(entt::entity entity);
        bool Toggle(entt::entity entity);
        void Clear();

        [[nodiscard]] bool HasSelection() const;
        [[nodiscard]] std::optional<entt::entity> Current() const;
        [[nodiscard]] std::optional<entt::entity> ActiveTransformEntity() const;
        [[nodiscard]] const std::vector<entt::entity>& Roots() const;
        [[nodiscard]] std::vector<entt::entity> TransformTargets() const;
        [[nodiscard]] std::vector<entt::entity> EffectiveEntities() const;
        [[nodiscard]] bool ContainsRoot(entt::entity entity) const;
        [[nodiscard]] bool ContainsEffective(entt::entity entity) const;
    };
} // namespace sage::editor

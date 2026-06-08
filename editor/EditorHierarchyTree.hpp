#pragma once

#include "EditorGui.hpp"

#include "entt/entt.hpp"

#include <string>
#include <vector>

namespace sage
{
    class EngineSystems;
}

namespace sage::editor
{
    class EditorHierarchyTree
    {
        EngineSystems* sys;
        mutable std::vector<entt::entity> rootOrder;

        void appendSceneObjectEntry(
            std::vector<EditorGui::SceneObjectEntry>& entries,
            entt::entity entity,
            entt::entity parent,
            int depth) const;
        void syncRootOrder(std::vector<entt::entity>& roots) const;

      public:
        explicit EditorHierarchyTree(EngineSystems* sys);

        [[nodiscard]] std::string GetEntityName(entt::entity entity) const;
        [[nodiscard]] const char* GetEntityIcon(entt::entity entity) const;
        [[nodiscard]] std::vector<EditorGui::SceneObjectEntry> CollectSceneObjectEntries() const;
        void NoteHierarchyMove(entt::entity dragged, entt::entity newParent, entt::entity insertBefore);
    };
} // namespace sage::editor

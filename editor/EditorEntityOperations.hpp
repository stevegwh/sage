#pragma once

#include "EditorInspector.hpp"
#include "engine/content/ContentDocument.hpp"

#include "engine/Archetypes.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/Light.hpp"
#include "engine/SceneTags.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sage
{
    class EngineSystems;
}

namespace sage::editor
{
    // TODO: This should be part of the engine, I believe
    class EditorEntityOperations
    {
        EngineSystems* sys;
        const InspectorRegistry* components;

        struct ClipboardSubtree
        {
            entt::entity originalParent = entt::null;
            Vector3 origin{};
            std::string document;
        };

        std::vector<ClipboardSubtree> clipboard;

        void releaseNavigationOccupation(entt::entity entity) const;
        void captureSubtree(entt::entity root, ClipboardSubtree& subtree) const;
        [[nodiscard]] entt::entity instantiateSubtree(const ClipboardSubtree& subtree) const;

      public:
        EditorEntityOperations(EngineSystems* sys, const InspectorRegistry* components);

        void DeleteEntityAndChildren(entt::entity entity) const;

        // "Add" menu factories. Each creates a tagged EditorMapEntity at the given
        // world position and returns it; the caller records history and selects.
        [[nodiscard]] entt::entity CreateLight(Vector3 position) const;
        [[nodiscard]] entt::entity CreateSpawnPoint(Vector3 position) const;
        [[nodiscard]] entt::entity CreateTriggerVolume(Vector3 position) const;
        [[nodiscard]] entt::entity CreateTerrain(Vector3 position) const;
        [[nodiscard]] entt::entity CreateEmptyTransform(Vector3 position) const;
        [[nodiscard]] entt::entity CreateMesh(
            Vector3 position, const std::string& modelKey, const std::string& name) const;

        // Captures the given entities (and their descendants) into the clipboard,
        // replacing any previous contents. Entries that are descendants of another
        // given entity are skipped to avoid duplicating a subtree twice.
        void CopyEntities(const std::vector<entt::entity>& roots);
        [[nodiscard]] bool HasClipboard() const;
        // Instantiates a fresh copy of every clipboard subtree. Root names gain a
        // " (Copy)" suffix. Returns the new root entities.
        [[nodiscard]] std::vector<entt::entity> PasteClipboard() const;
    };
} // namespace sage::editor

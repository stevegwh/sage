#pragma once

#include "EditorInspector.hpp"

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

        // A single entity captured into the clipboard. parentLocalId points into
        // the records vector of the owning ClipboardSubtree (-1 for the subtree
        // root). World transforms are stored absolutely so paste reproduces the
        // copied objects in place. Renderable is kept as its serialized blob so
        // each paste re-loads a fresh model handle from the ResourceManager
        // rather than aliasing the source's GPU resources.
        struct ClipboardRecord
        {
            std::int32_t parentLocalId = -1;
            std::string name;
            Vector3 worldPos{};
            Vector3 worldRot{};
            Vector3 worldScale{1.0f, 1.0f, 1.0f};
            bool hasCollideable = false;
            Collideable collideable{};
            bool hasNavigationSurface = false;
            NavigationSurface navigationSurface{};
            bool hasNavigationObstacle = false;
            NavigationObstacle navigationObstacle{};
            bool hasTriggerVolume = false;
            TriggerVolume triggerVolume{};
            bool hasCursorTarget = false;
            CursorTarget cursorTarget{};
            bool hasRenderable = false;
            std::string renderableBlob;
            bool renderableActive = true;
            bool renderableSerializable = true;
            Color renderableHint = WHITE;
            bool hasLight = false;
            Light light{};
            bool hasAssetReference = false;
            std::string assetKey;
            bool hasMetaData = false;
            MetaData metaData{};
            bool hasScript = false;
            ScriptComponent script{};
            // Animation owns live subscriptions and cannot be copied. Its model key
            // is the authored state used to reconstruct a fresh component on paste.
            bool hasAnimation = false;
            std::string animationModelKey;
            // MoveableActor also owns live events and runtime path state. Only the
            // authored inspector fields are copied.
            bool hasMoveableActor = false;
            float moveableActorSpeed = 0.0f;
            float moveableActorTurnSpeed = 240.0f;
            int moveableActorPathfindingBounds = 0;
            std::string moveableActorMoveClip;
            std::string moveableActorIdleClip;
            // Terrain's DynamicRenderable is derived and rebuilt on paste.
            bool hasTerrain = false;
            int terrainResolution = 0;
            float terrainCellSize = 1.0f;
            std::vector<float> terrainHeights;
            bool hasArchetype = false;
            Archetype archetype{};
            std::vector<InspectorRegistry::PersistentComponent> persistentComponents;
        };

        // One copied subtree. records are in depth-first order with the root at
        // index 0. originalParent is the source root's parent so paste can re-home
        // the duplicate as a sibling of the original when that parent still exists.
        struct ClipboardSubtree
        {
            entt::entity originalParent = entt::null;
            std::vector<ClipboardRecord> records;
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

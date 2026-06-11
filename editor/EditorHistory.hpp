//
// Undo/redo for the editor. Records incremental, per-operation mementos of
// only the entities an operation touches (not the whole scene), so each undo
// step costs O(edit) rather than O(scene). Entities are keyed by a stable
// PersistentEntityId rather than the recycled entt handle, so create/delete
// can be reversed and the rest of the history stack stays valid.
//
// All editor mutations funnel through the same representation:
//   - structural ops (place / paste / add light / delete) use RecordCreate /
//     RecordDestroy,
//   - in-place edits (gizmo/keyboard transform, inspector fields, reparent)
//     use the Begin / Commit / Rollback transaction pair.
//

#pragma once

#include "engine/components/Collideable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/Light.hpp"
#include "engine/SceneTags.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sage
{
    class EngineSystems;
}

namespace sage::editor
{
    enum class EditAction
    {
        Place,
        Paste,
        Delete,
        AddLight,
        AddSpawner,
        AddTriggerVolume,
        Transform,
        EditField,
        Reparent,
        AddScript,
        RemoveScript
    };

    class EditorHistory
    {
      public:
        // Invoked after an undo/redo materializes the scene. The argument lists
        // the entities that exist after the apply (for shader/bounds re-derivation
        // and re-selection). Implemented by EditorScene.
        using OnApplied = std::function<void(const std::vector<entt::entity>& restored)>;

        EditorHistory(EngineSystems* sys, OnApplied onApplied);

        // --- In-place edit transactions -----------------------------------------
        // Begin captures the before-state of the affected entities; Commit captures
        // their after-state and pushes an entry (no-op edits are discarded);
        // Rollback restores the before-state and discards the transaction.
        void Begin(EditAction action, const std::vector<entt::entity>& affected);
        void Commit();
        void Rollback();
        [[nodiscard]] bool HasActiveTransaction() const;

        // Inspector helper: the inspector mutates the live component while a widget
        // is active, and some widgets (checkbox/combo) mutate on the very frame
        // they activate. CaptureBaseline is called each idle frame to keep a clean
        // pre-edit snapshot; BeginFromBaseline promotes it into a transaction once
        // an edit actually starts.
        void CaptureBaseline(const std::vector<entt::entity>& entities);
        void BeginFromBaseline(EditAction action);

        // --- One-shot structural ops --------------------------------------------
        // RecordCreate is called after the entities exist; RecordDestroy is called
        // while they are still alive, just before the caller destroys them. Both
        // expand the given roots to their full subtrees.
        void RecordCreate(EditAction action, const std::vector<entt::entity>& roots);
        void RecordDestroy(EditAction action, const std::vector<entt::entity>& roots);

        // --- Undo / redo ---------------------------------------------------------
        [[nodiscard]] bool CanUndo() const;
        [[nodiscard]] bool CanRedo() const;
        [[nodiscard]] std::string UndoLabel() const;
        [[nodiscard]] std::string RedoLabel() const;
        void Undo();
        void Redo();
        void MarkDirty();
        void MarkSaved();
        [[nodiscard]] bool HasUnsavedChanges() const;

        // Drops all history (e.g. on map load). Handles stay valid; ids keep counting.
        void Clear();

      private:
        // A captured snapshot of one entity's editable state. parentId/component
        // payloads are only meaningful when `exists` is true. Renderable is held as
        // its cereal blob so restore re-loads a fresh model handle from the
        // ResourceManager rather than aliasing GPU resources.
        struct EntityState
        {
            std::uint64_t persistentId = 0;
            bool exists = false;
            std::uint64_t parentId = 0; // 0 = no parent (root)
            std::uint64_t nextSiblingId = 0; // 0 = append within parent/root
            std::string name;
            Vector3 worldPos{};
            Vector3 worldRot{};
            Vector3 worldScale{1.0f, 1.0f, 1.0f};
            bool isMapEntity = false;
            bool isMapBase = false;
            bool hasCollideable = false;
            Collideable collideable{};
            bool hasRenderable = false;
            std::string renderableBlob;
            bool hasLight = false;
            Light light{};
            bool hasSpawner = false;
            Spawner spawner{};
            bool hasAssetReference = false;
            std::string assetKey;
            bool hasMetaData = false;
            MetaData metaData{};
            bool hasScript = false;
            ScriptComponent script{};
        };

        struct EntityDelta
        {
            EntityState before;
            EntityState after;
        };

        struct HistoryEntry
        {
            EditAction action{};
            std::vector<EntityDelta> deltas;
        };

        EngineSystems* sys;
        OnApplied onApplied;

        std::vector<HistoryEntry> undoStack;
        std::vector<HistoryEntry> redoStack;
        std::uint64_t nextPersistentId = 1;
        bool dirty = false;

        bool active = false;
        EditAction activeAction{};
        std::vector<EntityState> activeBefore;

        std::vector<EntityState> baseline;

        [[nodiscard]] static bool statesEqual(const EntityState& a, const EntityState& b);
        [[nodiscard]] std::uint64_t ensureId(entt::entity entity);
        [[nodiscard]] entt::registry& registry() const;
        [[nodiscard]] std::unordered_map<std::uint64_t, entt::entity> buildIdMap() const;
        [[nodiscard]] EntityState capture(entt::entity entity);
        [[nodiscard]] std::vector<EntityState> captureAll(const std::vector<entt::entity>& entities);
        void gatherSubtree(entt::entity root, std::vector<entt::entity>& out) const;
        void pushEntry(EditAction action, std::vector<EntityState> before, std::vector<EntityState> after);

        void applyEntry(const HistoryEntry& entry, bool undo);
        void materialize(
            const EntityState& target, std::unordered_map<std::uint64_t, entt::entity>& idMap);
        void applyParent(
            const EntityState& target, const std::unordered_map<std::uint64_t, entt::entity>& idMap) const;
        void destroySingle(entt::entity entity) const;
        void releaseNavigation(entt::entity entity) const;
        void markNavigation(entt::entity entity) const;
    };
} // namespace sage::editor

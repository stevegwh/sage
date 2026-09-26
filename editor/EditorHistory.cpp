#include "EditorHistory.hpp"

#include "EditorComponents.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/DynamicRenderable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/SceneTags.hpp"
#include "engine/systems/NavigationGridSystem.hpp"
#include "engine/systems/TransformSystem.hpp"

#include "cereal/archives/binary.hpp"
#include "magic_enum.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace sage::editor
{
    EditorHistory::EditorHistory(EngineSystems* _sys, const InspectorRegistry* _components, OnApplied _onApplied)
        : sys(_sys), components(_components), onApplied(std::move(_onApplied))
    {
    }

    entt::registry& EditorHistory::registry() const
    {
        return *sys->registry;
    }

    bool EditorHistory::statesEqual(const EntityState& a, const EntityState& b)
    {
        return a.exists == b.exists &&
               (!a.exists || (a.parentId == b.parentId && a.nextSiblingId == b.nextSiblingId &&
                              a.isMapEntity == b.isMapEntity && a.isMapBase == b.isMapBase &&
                              a.hasAssetReference == b.hasAssetReference && a.assetKey == b.assetKey &&
                              a.contentJson == b.contentJson));
    }

    std::uint64_t EditorHistory::ensureId(const entt::entity entity)
    {
        auto& reg = registry();
        if (auto* pid = reg.try_get<PersistentEntityId>(entity))
        {
            if (pid->id == 0) pid->id = nextPersistentId++;
            nextPersistentId = std::max(nextPersistentId, pid->id + 1);
            return pid->id;
        }
        for (const auto other : reg.view<PersistentEntityId>())
            nextPersistentId = std::max(nextPersistentId, reg.get<PersistentEntityId>(other).id + 1);
        const auto id = nextPersistentId++;
        reg.emplace<PersistentEntityId>(entity, PersistentEntityId{id});
        return id;
    }

    std::unordered_map<std::uint64_t, entt::entity> EditorHistory::buildIdMap() const
    {
        std::unordered_map<std::uint64_t, entt::entity> map;
        for (const auto entity : registry().view<PersistentEntityId>())
        {
            const auto id = registry().get<PersistentEntityId>(entity).id;
            if (id != 0) map[id] = entity;
        }
        return map;
    }

    void EditorHistory::gatherSubtree(const entt::entity root, std::vector<entt::entity>& out) const
    {
        auto& reg = registry();
        if (!reg.valid(root) || !reg.all_of<sgTransform>(root)) return;
        if (std::ranges::find(out, root) != out.end()) return;
        out.push_back(root);
        for (const auto child : reg.get<sgTransform>(root).GetChildren())
        {
            gatherSubtree(child, out);
        }
    }

    std::vector<entt::entity> EditorHistory::gatherSubtrees(const std::vector<entt::entity>& roots) const
    {
        std::vector<entt::entity> entities;
        for (const auto root : roots)
            gatherSubtree(root, entities);
        return entities;
    }

    EditorHistory::EntityState EditorHistory::capture(const entt::entity entity)
    {
        auto& reg = registry();
        EntityState s;
        s.persistentId = ensureId(entity);
        s.exists = true;

        const auto& transform = reg.get<sgTransform>(entity);

        if (const auto parent = transform.GetParent();
            parent != entt::null && reg.valid(parent) && reg.all_of<sgTransform>(parent))
        {
            s.parentId = ensureId(parent);
            const auto& siblings = reg.get<sgTransform>(parent).GetChildren();
            const auto current = std::ranges::find(siblings, entity);
            for (auto next = current == siblings.end() ? siblings.end() : current + 1; next != siblings.end();
                 ++next)
            {
                if (reg.valid(*next) && reg.all_of<sgTransform>(*next))
                {
                    s.nextSiblingId = ensureId(*next);
                    break;
                }
            }
        }

        s.isMapEntity = reg.all_of<EditorMapEntity>(entity);
        s.isMapBase = reg.all_of<EditorMapBase>(entity);

        if (auto* asset = reg.try_get<AssetReference>(entity))
        {
            s.hasAssetReference = true;
            s.assetKey = asset->assetKey;
        }
        auto document = content::Capture(reg, {entity}, "map", entt::null, true);
        s.contentJson = json::Stringify(document["entities"][0]);
        return s;
    }

    std::vector<EditorHistory::EntityState> EditorHistory::captureAll(const std::vector<entt::entity>& entities)
    {
        auto& reg = registry();
        std::vector<EntityState> states;
        states.reserve(entities.size());
        for (const auto entity : entities)
        {
            if (reg.valid(entity) && reg.all_of<sgTransform>(entity)) states.push_back(capture(entity));
        }
        return states;
    }

    // ===== Capture API ==============================================================

    void EditorHistory::Begin(const EditAction action, const std::vector<entt::entity>& affected)
    {
        if (active) Commit(); // finalize any leaked transaction rather than lose it
        active = true;
        activeAction = action;
        activeBefore = captureAll(affected);
    }

    void EditorHistory::Commit()
    {
        if (!active) return;

        auto idMap = buildIdMap();
        std::vector<EntityState> after;
        after.reserve(activeBefore.size());
        for (const auto& before : activeBefore)
        {
            const auto it = idMap.find(before.persistentId);
            if (it != idMap.end() && registry().valid(it->second) && registry().all_of<sgTransform>(it->second))
            {
                after.push_back(capture(it->second));
            }
            else
            {
                after.push_back(EntityState{.persistentId = before.persistentId});
            }
        }

        auto before = std::move(activeBefore);
        active = false;
        activeBefore.clear();
        pushEntry(activeAction, std::move(before), std::move(after));
    }

    void EditorHistory::Rollback()
    {
        if (!active) return;

        HistoryEntry restore;
        restore.action = activeAction;
        for (const auto& before : activeBefore)
        {
            restore.deltas.push_back({.before = before, .after = before});
        }

        active = false;
        activeBefore.clear();
        applyEntry(restore, /*undo=*/true);
    }

    bool EditorHistory::HasActiveTransaction() const
    {
        return active;
    }

    void EditorHistory::CaptureBaseline(const std::vector<entt::entity>& entities)
    {
        baseline = captureAll(entities);
    }

    void EditorHistory::BeginFromBaseline(const EditAction action)
    {
        if (active) Commit();
        active = true;
        activeAction = action;
        activeBefore = baseline;
    }

    void EditorHistory::RecordCreate(const EditAction action, const std::vector<entt::entity>& roots)
    {
        const auto entities = gatherSubtrees(roots);
        if (entities.empty()) return;

        std::vector<EntityState> before;
        std::vector<EntityState> after;
        before.reserve(entities.size());
        after.reserve(entities.size());
        for (const auto entity : entities)
        {
            before.push_back(EntityState{.persistentId = ensureId(entity)});
            after.push_back(capture(entity));
        }
        pushEntry(action, std::move(before), std::move(after));
    }

    void EditorHistory::RecordDestroy(const EditAction action, const std::vector<entt::entity>& roots)
    {
        const auto entities = gatherSubtrees(roots);
        if (entities.empty()) return;

        std::vector<EntityState> before;
        std::vector<EntityState> after;
        before.reserve(entities.size());
        after.reserve(entities.size());
        for (const auto entity : entities)
        {
            before.push_back(capture(entity));
            after.push_back(EntityState{.persistentId = before.back().persistentId});
        }
        pushEntry(action, std::move(before), std::move(after));
    }

    void EditorHistory::pushEntry(
        const EditAction action, std::vector<EntityState> before, std::vector<EntityState> after)
    {
        HistoryEntry entry;
        entry.action = action;
        const auto count = std::min(before.size(), after.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!statesEqual(before[i], after[i]))
            {
                entry.deltas.push_back({.before = std::move(before[i]), .after = std::move(after[i])});
            }
        }
        if (entry.deltas.empty()) return; // no-op edit

        undoStack.push_back(std::move(entry));
        redoStack.clear();
        MarkDirty();
    }

    // ===== Undo / redo ==============================================================

    bool EditorHistory::CanUndo() const
    {
        return !undoStack.empty();
    }

    bool EditorHistory::CanRedo() const
    {
        return !redoStack.empty();
    }

    std::string EditorHistory::UndoLabel() const
    {
        if (undoStack.empty()) return "Undo";
        return "Undo " + std::string(magic_enum::enum_name(undoStack.back().action));
    }

    std::string EditorHistory::RedoLabel() const
    {
        if (redoStack.empty()) return "Redo";
        return "Redo " + std::string(magic_enum::enum_name(redoStack.back().action));
    }

    void EditorHistory::Undo()
    {
        if (undoStack.empty()) return;
        auto entry = std::move(undoStack.back());
        undoStack.pop_back();
        applyEntry(entry, /*undo=*/true);
        redoStack.push_back(std::move(entry));
        MarkDirty();
    }

    void EditorHistory::Redo()
    {
        if (redoStack.empty()) return;
        auto entry = std::move(redoStack.back());
        redoStack.pop_back();
        applyEntry(entry, /*undo=*/false);
        undoStack.push_back(std::move(entry));
        MarkDirty();
    }

    void EditorHistory::MarkDirty()
    {
        dirty = true;
    }

    void EditorHistory::MarkSaved()
    {
        dirty = false;
    }

    bool EditorHistory::HasUnsavedChanges() const
    {
        return dirty || active;
    }

    void EditorHistory::Clear()
    {
        undoStack.clear();
        redoStack.clear();
        active = false;
        activeBefore.clear();
        baseline.clear();
        MarkSaved();
    }

    // ===== Apply ====================================================================

    void EditorHistory::applyEntry(const HistoryEntry& entry, const bool undo)
    {
        auto& reg = registry();
        auto idMap = buildIdMap();
        const auto pick = [undo](const EntityDelta& delta) -> const EntityState& {
            return undo ? delta.before : delta.after;
        };

        for (const auto& delta : entry.deltas)
        {
            const auto& target = pick(delta);
            if (!target.exists || idMap.contains(target.persistentId)) continue;
            const auto entity = reg.create();
            reg.emplace<PersistentEntityId>(entity, target.persistentId);
            idMap[target.persistentId] = entity;
        }
        // Pass 1: create / update every entity the target state says should exist.
        for (const auto& delta : entry.deltas)
        {
            if (const auto& target = pick(delta); target.exists) materialize(target, idMap);
        }
        // Pass 2: re-link parents now that every entity in the entry exists.
        for (const auto& delta : entry.deltas)
        {
            if (const auto& target = pick(delta); target.exists) applyParent(target, idMap);
        }
        // Pass 3: destroy entities the target state says should be gone.
        for (const auto& delta : entry.deltas)
        {
            const auto& target = pick(delta);
            if (target.exists) continue;
            if (const auto it = idMap.find(target.persistentId); it != idMap.end() && reg.valid(it->second))
            {
                destroySingle(it->second);
                idMap.erase(it);
            }
        }

        std::vector<entt::entity> restored;
        for (const auto& delta : entry.deltas)
        {
            const auto& target = pick(delta);
            if (!target.exists) continue;
            if (const auto it = idMap.find(target.persistentId); it != idMap.end() && reg.valid(it->second))
            {
                restored.push_back(it->second);
            }
        }

        if (onApplied) onApplied(restored);
    }

    void EditorHistory::materialize(
        const EntityState& target, std::unordered_map<std::uint64_t, entt::entity>& idMap)
    {
        auto& reg = registry();

        entt::entity entity;
        if (const auto it = idMap.find(target.persistentId); it != idMap.end() && reg.valid(it->second))
        {
            entity = it->second;
        }
        else
        {
            entity = reg.create();
            idMap[target.persistentId] = entity;
        }
        reg.emplace_or_replace<PersistentEntityId>(entity, PersistentEntityId{target.persistentId});

        if (target.isMapEntity)
            reg.emplace_or_replace<EditorMapEntity>(entity);
        else if (reg.all_of<EditorMapEntity>(entity))
            reg.remove<EditorMapEntity>(entity);

        if (target.isMapBase)
            reg.emplace_or_replace<EditorMapBase>(entity);
        else if (reg.all_of<EditorMapBase>(entity))
            reg.remove<EditorMapBase>(entity);

        setNavigationOccupied(entity, false);
        std::unordered_map<std::uint32_t, entt::entity> references;
        for (const auto& [id, handle] : idMap)
            references.emplace(static_cast<std::uint32_t>(id), handle);
        content::RestoreEntity(reg, entity, json::Parse(target.contentJson), references);
        if (target.hasAssetReference)
            reg.emplace_or_replace<AssetReference>(entity, AssetReference{target.assetKey});
        else
            reg.remove<AssetReference>(entity);
        setNavigationOccupied(entity, true);
    }

    void EditorHistory::applyParent(
        const EntityState& target, const std::unordered_map<std::uint64_t, entt::entity>& idMap) const
    {
        auto& reg = registry();
        const auto it = idMap.find(target.persistentId);
        if (it == idMap.end() || !reg.valid(it->second)) return;

        entt::entity parent = entt::null;
        if (target.parentId != 0)
        {
            if (const auto p = idMap.find(target.parentId);
                p != idMap.end() && reg.valid(p->second) && reg.all_of<sgTransform>(p->second))
            {
                parent = p->second;
            }
        }

        entt::entity insertBefore = entt::null;
        if (target.nextSiblingId != 0)
        {
            if (const auto sibling = idMap.find(target.nextSiblingId);
                sibling != idMap.end() && reg.valid(sibling->second) && reg.all_of<sgTransform>(sibling->second) &&
                reg.get<sgTransform>(sibling->second).GetParent() == parent)
            {
                insertBefore = sibling->second;
            }
        }
        sys->transformSystem->SetParent(it->second, parent, insertBefore);
    }

    void EditorHistory::destroySingle(const entt::entity entity) const
    {
        // TransformSystem's on_destroy hook detaches this entity from its parent and
        // orphans its children with valid-guards, so order across a subtree is safe.
        setNavigationOccupied(entity, false);
        registry().destroy(entity);
    }

    void EditorHistory::setNavigationOccupied(const entt::entity entity, const bool occupied) const
    {
        auto& reg = registry();
        if (!reg.valid(entity) || !reg.all_of<Collideable>(entity)) return;
        const auto& collideable = reg.get<Collideable>(entity);
        const auto* obstacle = reg.try_get<NavigationObstacle>(entity);
        if (obstacle != nullptr && obstacle->active)
        {
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, occupied, entity);
        }
    }
} // namespace sage::editor

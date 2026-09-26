#include "EditorEntityOperations.hpp"

#include "EditorComponents.hpp"
#include "engine/Archetypes.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Light.hpp"
#include "engine/LightManager.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/systems/NavigationGridSystem.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "engine/TerrainMesh.hpp"

#include "cereal/archives/binary.hpp"

#include <format>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace sage::editor
{
    namespace
    {
        constexpr float DEFAULT_LIGHT_BRIGHTNESS = 3.0f;
        constexpr Color DEFAULT_LIGHT_COLOR = {255, 244, 214, 255};

        std::string lightLabel(const entt::entity entity)
        {
            return std::format("light_{}", entt::to_integral(entity));
        }

        std::string spawnPointLabel(const entt::entity entity)
        {
            return std::format("spawn_point_{}", entt::to_integral(entity));
        }

        std::string triggerLabel(const entt::entity entity)
        {
            return std::format("trigger_{}", entt::to_integral(entity));
        }

        std::string terrainLabel(const entt::entity entity)
        {
            return std::format("terrain_{}", entt::to_integral(entity));
        }

        std::string emptyTransformLabel(const entt::entity entity)
        {
            return std::format("empty_{}", entt::to_integral(entity));
        }
    } // namespace

    EditorEntityOperations::EditorEntityOperations(EngineSystems* _sys, const InspectorRegistry* _components)
        : sys(_sys), components(_components)
    {
    }

    entt::entity EditorEntityOperations::CreateLight(const Vector3 position) const
    {
        const auto entity = sys->registry->create();
        sys->registry->emplace<EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = lightLabel(entity);

        sys->registry->emplace<Light>(
            entity,
            Light{
                .type = LightType::Point,
                .enabled = true,
                .position = position,
                .target = Vector3Zero(),
                .color = DEFAULT_LIGHT_COLOR,
                .brightness = DEFAULT_LIGHT_BRIGHTNESS});
        return entity;
    }

    entt::entity EditorEntityOperations::CreateSpawnPoint(const Vector3 position) const
    {
        const auto entity = sys->registry->create();
        sys->registry->emplace<EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = spawnPointLabel(entity);

        AddTag(*sys->registry, entity, SpawnPointTag);
        return entity;
    }

    entt::entity EditorEntityOperations::CreateTriggerVolume(const Vector3 position) const
    {
        const auto entity = sys->registry->create();
        sys->registry->emplace<EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = triggerLabel(entity);

        // A trigger is a collider plus TriggerVolume intent. Left non-static so
        // the box tracks the transform as the user drags it in the editor.
        const BoundingBox localBox{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        auto& collideable = sys->registry->emplace<Collideable>(entity, localBox, transform.GetMatrixNoRot());
        collideable.isStatic = false;
        sys->registry->emplace<TriggerVolume>(entity);
        return entity;
    }

    entt::entity EditorEntityOperations::CreateTerrain(const Vector3 position) const
    {
        const auto entity = sys->registry->create();
        sys->registry->emplace<EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        const auto& terrain = sys->registry->emplace<Terrain>(entity);
        // The height field's local origin is its min corner; centre it on the
        // requested position.
        const float halfSize = terrain.WorldSize() * 0.5f;
        transform.position.world = {position.x - halfSize, position.y, position.z - halfSize};
        transform.name = terrainLabel(entity);
        AttachTerrainRenderable(*sys->registry, entity, *sys->lightSubSystem);
        return entity;
    }

    entt::entity EditorEntityOperations::CreateEmptyTransform(const Vector3 position) const
    {
        const auto entity = sys->registry->create();
        sys->registry->emplace<EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = emptyTransformLabel(entity);
        return entity;
    }

    entt::entity EditorEntityOperations::CreateMesh(
        const Vector3 position, const std::string& modelKey, const std::string& name) const
    {
        const auto entity = sys->registry->create();
        sys->registry->emplace<EditorMapEntity>(entity);
        auto& transform = sys->registry->emplace<sgTransform>(entity);
        transform.position.world = position;
        transform.name = name;

        auto model = ResourceManager::GetInstance().GetModelView(modelKey);
        auto& renderable = sys->registry->emplace<Renderable>(entity, std::move(model), MatrixIdentity());
        auto& uber =
            sys->registry->emplace<UberShaderComponent>(entity, renderable.GetModel()->GetMaterialCount());
        uber.SetFlagAll(UberShaderComponent::Flags::Lit);
        return entity;
    }

    void EditorEntityOperations::DeleteEntityAndChildren(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity)) return;

        std::vector<entt::entity> children;
        if (sys->registry->any_of<sgTransform>(entity))
        {
            children = sys->registry->get<sgTransform>(entity).GetChildren();
        }

        for (const auto child : children)
        {
            DeleteEntityAndChildren(child);
        }

        if (sys->registry->valid(entity) && sys->registry->any_of<sgTransform>(entity))
        {
            sys->registry->get<sgTransform>(entity).SetParent(entt::null);
        }

        releaseNavigationOccupation(entity);

        if (sys->registry->valid(entity))
        {
            sys->registry->destroy(entity);
        }
    }

    void EditorEntityOperations::releaseNavigationOccupation(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity) || !sys->registry->any_of<Collideable>(entity)) return;

        const auto& collideable = sys->registry->get<Collideable>(entity);
        const auto* obstacle = sys->registry->try_get<NavigationObstacle>(entity);
        if (obstacle != nullptr && obstacle->active)
        {
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, false, entity);
        }
    }

    void EditorEntityOperations::CopyEntities(const std::vector<entt::entity>& roots)
    {
        clipboard.clear();

        auto isDescendantOfAnotherRoot = [&](const entt::entity candidate) {
            for (auto cur = sys->registry->valid(candidate) && sys->registry->any_of<sgTransform>(candidate)
                                ? sys->registry->get<sgTransform>(candidate).GetParent()
                                : entt::null;
                 cur != entt::null;)
            {
                if (std::ranges::find(roots, cur) != roots.end()) return true;
                if (!sys->registry->valid(cur) || !sys->registry->any_of<sgTransform>(cur)) break;
                cur = sys->registry->get<sgTransform>(cur).GetParent();
            }
            return false;
        };

        for (const auto root : roots)
        {
            if (!sys->registry->valid(root) || !sys->registry->any_of<sgTransform>(root)) continue;
            if (isDescendantOfAnotherRoot(root)) continue;

            auto& subtree = clipboard.emplace_back();
            captureSubtree(root, subtree);
        }
    }

    bool EditorEntityOperations::HasClipboard() const
    {
        return !clipboard.empty();
    }

    void EditorEntityOperations::captureSubtree(entt::entity root, ClipboardSubtree& subtree) const
    {
        auto& registry = *sys->registry;
        const auto& transform = registry.get<sgTransform>(root);
        subtree.originalParent = transform.GetParent();
        subtree.origin = transform.GetWorldPos();
        std::vector<entt::entity> entities;
        auto visit = [&](auto& self, entt::entity entity) -> void {
            entities.push_back(entity);
            for (auto child : registry.get<sgTransform>(entity).GetChildren())
                self(self, child);
        };
        visit(visit, root);
        auto document = content::Capture(registry, entities, "flatpack", root);
        for (auto& node : document["entities"].GetArray())
            for (auto entity : entities)
                if (registry.get<PersistentEntityId>(entity).id == json::Id(node, "id"))
                    if (auto* asset = registry.try_get<AssetReference>(entity))
                        json::Put(node, "editorAssetKey", asset->assetKey, document.GetAllocator());
        subtree.document = json::Stringify(document);
    }

    std::vector<entt::entity> EditorEntityOperations::PasteClipboard() const
    {
        std::vector<entt::entity> newRoots;
        newRoots.reserve(clipboard.size());

        for (const auto& subtree : clipboard)
        {
            const auto root = instantiateSubtree(subtree);
            if (root != entt::null) newRoots.push_back(root);
        }
        return newRoots;
    }

    entt::entity EditorEntityOperations::instantiateSubtree(const ClipboardSubtree& subtree) const
    {
        if (subtree.document.empty()) return entt::null;
        auto document = json::Parse(subtree.document);
        auto result = content::Instantiate(*sys->registry, document, subtree.origin, true);
        for (std::size_t index = 0; index < result.entities.size(); ++index)
        {
            auto entity = result.entities[index];
            sys->registry->emplace<EditorMapEntity>(entity);
            const auto& node = document["entities"][static_cast<rapidjson::SizeType>(index)];
            if (node.HasMember("editorAssetKey"))
                sys->registry->emplace<AssetReference>(entity, json::String(node, "editorAssetKey"));
            if (sys->registry->all_of<Terrain>(entity))
                AttachTerrainRenderable(*sys->registry, entity, *sys->lightSubSystem);
        }
        sys->registry->get<sgTransform>(result.root).name += " (Copy)";
        if (subtree.originalParent != entt::null && sys->registry->valid(subtree.originalParent) &&
            sys->registry->all_of<sgTransform>(subtree.originalParent))
            sys->registry->get<sgTransform>(result.root).SetParent(subtree.originalParent);
        return result.root;
    }
} // namespace sage::editor

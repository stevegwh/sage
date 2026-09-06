#include "EditorMapLoader.hpp"

#include "EditorComponents.hpp"
#include "EditorInspector.hpp"
#include "engine/Archetypes.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/EditorLayoutMapFormat.hpp"
#include "engine/Light.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/Serializer.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sage::editor
{
    namespace
    {
        using namespace sage::editor_layout;

        // Emits any map entity as a unified record carrying whatever components it
        // has. Light and Terrain entities are skipped: they own dedicated sections.
        void appendEntityRecord(
            entt::registry& source,
            const entt::entity entityHandle,
            std::vector<EntityRecord>& entities,
            std::unordered_set<entt::entity>& emittedEntities)
        {
            if (emittedEntities.contains(entityHandle)) return;
            if (!source.all_of<EditorMapEntity, sgTransform>(entityHandle)) return;
            if (source.any_of<Light, Terrain>(entityHandle)) return;

            emittedEntities.insert(entityHandle);
            auto& record = entities.emplace_back();
            record.entity.id = entt::entt_traits<entt::entity>::to_entity(entityHandle);
            record.transform =
                TransformWithSerializedNameFallback(source.get<sgTransform>(entityHandle), record.entity.id);
            if (const auto* component = source.try_get<Renderable>(entityHandle);
                component != nullptr && component->serializable)
            {
                record.hasRenderable = true;
                record.renderable = *component;
            }
            if (const auto* component = source.try_get<Collideable>(entityHandle))
            {
                record.hasCollideable = true;
                record.collideable = *component;
            }
            if (const auto* component = source.try_get<NavigationSurface>(entityHandle))
            {
                record.hasNavigationSurface = true;
                record.navigationSurface = *component;
            }
            if (const auto* component = source.try_get<NavigationObstacle>(entityHandle))
            {
                record.hasNavigationObstacle = true;
                record.navigationObstacle = *component;
            }
            if (const auto* component = source.try_get<TriggerVolume>(entityHandle))
            {
                record.hasTriggerVolume = true;
                record.triggerVolume = *component;
            }
            if (const auto* component = source.try_get<CursorTarget>(entityHandle))
            {
                record.hasCursorTarget = true;
                record.cursorTarget = *component;
            }
            if (const auto* component = source.try_get<MetaData>(entityHandle);
                component != nullptr && !component->tags.empty())
            {
                record.hasMetaData = true;
                record.metaData = *component;
            }
        }
    } // namespace

    bool IsEditorLayoutMap(const char* path)
    {
        std::ifstream storage(path, std::ios::binary);
        if (!storage.is_open()) return false;

        char fileMagic[4]{};
        storage.read(fileMagic, sizeof(fileMagic));
        return storage.gcount() == sizeof(fileMagic) &&
               (std::memcmp(fileMagic, MapMagic, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, PreviousMapMagic, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, LegacyMapMagic, sizeof(fileMagic)) == 0);
    }

    bool LoadMap(entt::registry* destination, const char* path, const InspectorRegistry* components)
    {
        assert(destination != nullptr);
        if (!IsEditorLayoutMap(path))
        {
            std::cerr << "ERROR: Not an editor layout map: " << path << std::endl;
            return false;
        }

        std::cout << "START: Loading layout map data from file (editor)." << std::endl;

        std::ifstream header(path, std::ios::binary);
        char fileMagic[4]{};
        header.read(fileMagic, sizeof(fileMagic));
        const bool previousFormat = std::memcmp(fileMagic, PreviousMapMagic, sizeof(fileMagic)) == 0;
        const bool legacyFormat = std::memcmp(fileMagic, LegacyMapMagic, sizeof(fileMagic)) == 0;

        std::unordered_map<std::uint32_t, entt::entity> idMap;
        std::vector<entt::entity> loadedEntities;

        sage::serializer::ReadCompressedBinary(
            path,
            legacyFormat ? LegacyMapMagic : (previousFormat ? PreviousMapMagic : MapMagic),
            [&](cereal::BinaryInputArchive& input, std::istream&) {
                std::vector<Light> lights;
                input(lights);
                for (const auto& light : lights)
                {
                    const auto entity = destination->create();
                    auto loadedLight = light;
                    loadedLight.enabled = true;
                    destination->emplace<EditorMapEntity>(entity);
                    destination->emplace<Light>(entity, loadedLight);
                }

                std::vector<EntityRecord> entities;
                input(entities);
                for (const auto& record : entities)
                {
                    const auto entity = destination->create();
                    auto transform = TransformWithSerializedNameFallback(record.transform, record.entity.id);
                    destination->emplace<EditorMapEntity>(entity);
                    destination->emplace<sgTransform>(entity, transform);
                    destination->emplace<MetaData>(entity, record.hasMetaData ? record.metaData : MetaData{});
                    if (record.hasCollideable)
                    {
                        auto& collideable = destination->emplace<Collideable>(entity, record.collideable);
                        // Triggers stay non-static so their box follows the transform
                        // when dragged; other colliders are static map geometry.
                        collideable.isStatic = !record.hasTriggerVolume;
                    }
                    if (record.hasNavigationSurface)
                        destination->emplace<NavigationSurface>(entity, record.navigationSurface);
                    if (record.hasNavigationObstacle)
                        destination->emplace<NavigationObstacle>(entity, record.navigationObstacle);
                    if (record.hasTriggerVolume)
                        destination->emplace<TriggerVolume>(entity, record.triggerVolume);
                    if (record.hasCursorTarget)
                        destination->emplace<CursorTarget>(entity, record.cursorTarget);
                    if (record.hasRenderable)
                    {
                        auto renderable = record.renderable;
                        if (IsMapBaseTransform(transform)) renderable.active = false;
                        destination->emplace<Renderable>(entity, renderable);
                    }
                    idMap[record.entity.id] = entity;
                    loadedEntities.push_back(entity);
                }

                const auto resolveTarget = [&](const std::uint32_t targetId) {
                    const auto iter = idMap.find(targetId);
                    return iter != idMap.end() ? iter->second : entt::null;
                };

                std::vector<EntityScriptRecord> scripts;
                input(scripts);
                for (const auto& record : scripts)
                {
                    const entt::entity target = resolveTarget(record.targetId);
                    if (target == entt::null) continue;
                    destination->emplace_or_replace<ScriptComponent>(target, record.script);
                }

                std::vector<EntityAnimationRecord> animations;
                input(animations);
                for (const auto& record : animations)
                {
                    const entt::entity target = resolveTarget(record.targetId);
                    if (target == entt::null) continue;
                    if (!ResourceManager::GetInstance().HasModelAnimation(record.modelKey))
                    {
                        std::cerr << "EditorMapLoader: no packed animation data for '"
                                  << record.modelKey << "', skipping Animation component.\n";
                        continue;
                    }
                    // Animation is neither copyable nor movable (live Subscriptions
                    // hold its address), so replace by remove + emplace.
                    destination->remove<Animation>(target);
                    destination->emplace<Animation>(target, record.modelKey);
                }

                const auto loadMoveables = [&](const auto& moveables) {
                    for (const auto& record : moveables)
                    {
                        const entt::entity target = resolveTarget(record.targetId);
                        if (target == entt::null) continue;
                        auto& moveable = destination->get_or_emplace<MoveableActor>(target);
                        moveable.movementSpeed = record.movementSpeed;
                        if constexpr (requires { record.turnSpeed; }) moveable.turnSpeed = record.turnSpeed;
                        moveable.pathfindingBounds = record.pathfindingBounds;
                        moveable.moveClip = record.moveClip;
                        moveable.idleClip = record.idleClip;
                    }
                };
                if (legacyFormat)
                {
                    std::vector<LegacyEntityMoveableActorRecord> moveables;
                    input(moveables);
                    loadMoveables(moveables);
                }
                else
                {
                    std::vector<EntityMoveableActorRecord> moveables;
                    input(moveables);
                    loadMoveables(moveables);
                }

                const auto loadTerrains = [&](auto& terrains) {
                    for (auto& record : terrains)
                    {
                        Terrain terrain;
                        terrain.resolution = record.resolution;
                        terrain.cellSize = record.cellSize;
                        terrain.heights = std::move(record.heights);
                        if (!terrain.IsValid())
                        {
                            std::cerr << "EditorMapLoader: invalid terrain record, skipping.\n";
                            continue;
                        }

                        const auto entity = destination->create();
                        destination->emplace<EditorMapEntity>(entity);
                        sgTransform transform;
                        if constexpr (requires { record.transform; })
                            transform = std::move(record.transform);
                        else
                            transform.position.world = record.position;
                        if (transform.name.empty())
                            transform.name = "terrain_" + std::to_string(entt::to_integral(entity));
                        destination->emplace<sgTransform>(entity, std::move(transform));
                        destination->emplace<Terrain>(entity, std::move(terrain));
                        auto& collideable = destination->emplace<Collideable>(entity, record.collideable);
                        collideable.isStatic = true;
                        // The mesh, shader and collision bounds are derived after
                        // load (EditorScene::refreshAfterMapLoad).
                    }
                };
                if (previousFormat || legacyFormat)
                {
                    std::vector<LegacyTerrainRecord> terrains;
                    input(terrains);
                    loadTerrains(terrains);
                }
                else
                {
                    std::vector<TerrainRecord> terrains;
                    input(terrains);
                    loadTerrains(terrains);
                }

                // Trailing optional sections. Older maps may end after terrain or
                // archetypes, so missing data degrades to no archetypes/game components.
                try
                {
                    std::vector<EntityArchetypeRecord> archetypes;
                    input(archetypes);
                    for (const auto& record : archetypes)
                    {
                        const entt::entity target = resolveTarget(record.targetId);
                        if (target == entt::null) continue;
                        destination->emplace_or_replace<Archetype>(target, record.archetype);
                    }

                    std::vector<EntityGameComponentRecord> gameComponents;
                    input(gameComponents);
                    if (components != nullptr)
                    {
                        std::unordered_map<entt::entity, std::vector<InspectorRegistry::PersistentComponent>> byEntity;
                        for (auto& record : gameComponents)
                        {
                            const entt::entity target = resolveTarget(record.targetId);
                            if (target == entt::null) continue;
                            byEntity[target].push_back({std::move(record.key), std::move(record.data)});
                        }
                        for (const auto& [entity, states] : byEntity)
                            components->RestorePersistent(*destination, entity, states);
                    }
                }
                catch (const std::exception&)
                {
                }
            });

        // Resolved after every section so any entity can parent onto any other.
        for (const auto entity : loadedEntities)
        {
            if (destination->valid(entity) && destination->any_of<EditorMapEntity, sgTransform>(entity))
            {
                destination->get<sgTransform>(entity).ResolveSerializedParent(idMap);
                if (components) components->ResolvePersistentReferences(*destination, entity, idMap);
            }
        }

        std::cout << "FINISH: Loading layout map data from file (editor)." << std::endl;
        return true;
    }

    void SaveMap(entt::registry& source, const char* path, const InspectorRegistry* components)
    {
        SaveMap(source, path, {}, components);
    }

    void SaveMap(
        entt::registry& source,
        const char* path,
        const std::vector<entt::entity>& hierarchyOrder,
        const InspectorRegistry* components)
    {
        std::cout << "START: Saving layout map data to file (editor)." << std::endl;

        const std::filesystem::path outputPath{path};
        if (const auto parent = outputPath.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        sage::serializer::WriteCompressedBinary(
            path, MapMagic, [&](cereal::BinaryOutputArchive& output) {
                std::vector<Light> lights;
                for (const auto entity : source.view<EditorMapEntity, Light>())
                {
                    auto light = source.get<Light>(entity);
                    if (source.any_of<sgTransform>(entity))
                    {
                        light.position = source.get<sgTransform>(entity).GetWorldPos();
                    }
                    lights.push_back(light);
                }
                output(lights);

                // Unified entity section: every map entity except Light/Terrain,
                // carrying whatever components it has. hierarchyOrder first so parents
                // serialize before children, then the view sweeps up anything else.
                std::vector<EntityRecord> entities;
                std::unordered_set<entt::entity> emittedEntities;
                emittedEntities.reserve(hierarchyOrder.size());
                for (const auto entityHandle : hierarchyOrder)
                {
                    appendEntityRecord(source, entityHandle, entities, emittedEntities);
                }
                for (const auto entityHandle : source.view<EditorMapEntity, sgTransform>())
                {
                    appendEntityRecord(source, entityHandle, entities, emittedEntities);
                }
                output(entities);

                std::vector<EntityScriptRecord> scripts;
                for (const auto entity : emittedEntities)
                {
                    if (!source.all_of<ScriptComponent>(entity)) continue;
                    const auto& script = source.get<ScriptComponent>(entity);
                    if (script.className.empty()) continue;
                    scripts.push_back(
                        EntityScriptRecord{entt::entt_traits<entt::entity>::to_entity(entity), script});
                }
                output(scripts);

                std::vector<EntityAnimationRecord> animations;
                for (const auto entity : emittedEntities)
                {
                    if (!source.all_of<Animation>(entity)) continue;
                    const auto& animation = source.get<Animation>(entity);
                    if (animation.modelKey.empty()) continue;
                    animations.push_back(
                        EntityAnimationRecord{
                            entt::entt_traits<entt::entity>::to_entity(entity), animation.modelKey});
                }
                output(animations);

                std::vector<EntityMoveableActorRecord> moveables;
                for (const auto entity : emittedEntities)
                {
                    if (!source.all_of<MoveableActor>(entity)) continue;
                    const auto& moveable = source.get<MoveableActor>(entity);
                    moveables.push_back(
                        EntityMoveableActorRecord{
                            entt::entt_traits<entt::entity>::to_entity(entity),
                            moveable.movementSpeed,
                            moveable.turnSpeed,
                            moveable.pathfindingBounds,
                            moveable.moveClip,
                            moveable.idleClip});
                }
                output(moveables);

                std::vector<TerrainRecord> terrains;
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Terrain>())
                {
                    const auto& terrain = source.get<Terrain>(entity);
                    Collideable collideable{};
                    if (const auto* existing = source.try_get<Collideable>(entity)) collideable = *existing;
                    terrains.push_back(
                        TerrainRecord{
                            source.get<sgTransform>(entity),
                            terrain.resolution,
                            terrain.cellSize,
                            collideable,
                            terrain.heights});
                }
                output(terrains);

                // Trailing (final) section, wrapped in try/catch on load so older
                // maps without it degrade to "no archetypes".
                std::vector<EntityArchetypeRecord> archetypes;
                for (const auto entity : emittedEntities)
                {
                    const auto* archetype = source.try_get<Archetype>(entity);
                    if (archetype == nullptr || !archetype->IsValid()) continue;
                    archetypes.push_back(
                        EntityArchetypeRecord{
                            entt::entt_traits<entt::entity>::to_entity(entity), *archetype});
                }
                output(archetypes);

                std::vector<EntityGameComponentRecord> gameComponents;
                if (components != nullptr)
                {
                    for (const auto entity : emittedEntities)
                    {
                        for (auto component : components->CapturePersistent(source, entity))
                        {
                            gameComponents.push_back(
                                {entt::entt_traits<entt::entity>::to_entity(entity),
                                 std::move(component.key),
                                 std::move(component.data)});
                        }
                    }
                }
                output(gameComponents);
            });

        std::cout << "FINISH: Saving layout map data to file (editor)." << std::endl;
    }
} // namespace sage::editor

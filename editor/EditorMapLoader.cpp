#include "EditorMapLoader.hpp"

#include "EditorComponents.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/Light.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/Serializer.hpp"

#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"

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
        constexpr char kEditorLayoutMapMagic[4] = {'L', 'Q', 'E', '2'};

        struct LayoutEntityRecord
        {
            sage::serializer::entity entity{};
            sgTransform transform{};
            Collideable collideable{};
            bool hasNavigationSurface = false;
            NavigationSurface navigationSurface{};
            bool hasNavigationObstacle = false;
            NavigationObstacle navigationObstacle{};
            bool hasTriggerVolume = false;
            TriggerVolume triggerVolume{};
            bool hasCursorTarget = false;
            CursorTarget cursorTarget{};
            Renderable renderable{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(
                    entity,
                    transform,
                    collideable,
                    hasNavigationSurface,
                    navigationSurface,
                    hasNavigationObstacle,
                    navigationObstacle,
                    hasTriggerVolume,
                    triggerVolume,
                    hasCursorTarget,
                    cursorTarget,
                    renderable);
            }
        };

        struct LightEditorObjectRecord
        {
            std::uint32_t lightIndex = 0;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(lightIndex);
            }
        };

        struct EntityEditorObjectRecord
        {
            sage::serializer::entity entity{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(entity);
            }
        };

        struct EntityMetaDataRecord
        {
            sage::serializer::entity entity{};
            MetaData metaData{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(entity, metaData);
            }
        };

        // A trigger is a Collideable + TriggerVolume with no mesh.
        // It carries its own box, so we save the Collideable alongside the world position
        // (taken from the transform). Must stay in sync with the game loader's TriggerRecord
        // (LevelLayoutLoader.cpp).
        struct TriggerRecord
        {
            Vector3 position{};
            Collideable collideable{};
            TriggerVolume triggerVolume{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(position, collideable, triggerVolume);
            }
        };

        // Attaches a ScriptComponent to an entity from one of the three entity
        // streams above; the owner is addressed by stream + id/index because only
        // layout entities carry a saved entity id. Trailing section — old maps
        // simply lack it. Must stay in sync with the game loader's
        // EntityScriptRecord (LevelLayoutLoader.cpp).
        struct EntityScriptRecord
        {
            // 0 = layout entity (targetId = saved entity id), 1 = spawner / 2 =
            // trigger (targetId = index into that section, in saved order).
            std::uint8_t targetKind = 0;
            std::uint32_t targetId = 0;
            ScriptComponent script{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(targetKind, targetId, script);
            }
        };

        // Attaches an Animation to an entity, addressed the same way as
        // EntityScriptRecord. Only the model key is saved; clips are derived from
        // packed animation data on load. Trailing section — old maps simply lack
        // it. Must stay in sync with the game loader's EntityAnimationRecord
        // (LevelLayoutLoader.cpp).
        struct EntityAnimationRecord
        {
            std::uint8_t targetKind = 0;
            std::uint32_t targetId = 0;
            std::string modelKey;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(targetKind, targetId, modelKey);
            }
        };

        // A terrain is its authored height field plus a world anchor (the field's
        // min corner) and its Collideable (the layer is authored in the
        // inspector); the mesh, shader and collision bounds are derived on load
        // (AttachTerrainRenderable). Must stay in sync with the game loader's
        // TerrainRecord (LevelLayoutLoader.cpp).
        struct TerrainRecord
        {
            Vector3 position{};
            std::int32_t resolution = 0;
            float cellSize = 1.0f;
            Collideable collideable{};
            std::vector<float> heights;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(position, resolution, cellSize, collideable, heights);
            }
        };

        // Attaches a MoveableActor to an entity, addressed the same way as
        // EntityScriptRecord. Only the authored fields are saved; the rest of the
        // component is runtime state. Trailing section — old maps simply lack it.
        // Must stay in sync with the game loader's EntityMoveableActorRecord
        // (LevelLayoutLoader.cpp).
        struct EntityMoveableActorRecord
        {
            std::uint8_t targetKind = 0;
            std::uint32_t targetId = 0;
            float movementSpeed = 0.0f;
            std::int32_t pathfindingBounds = 0;
            std::string moveClip;
            std::string idleClip;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(targetKind, targetId, movementSpeed, pathfindingBounds, moveClip, idleClip);
            }
        };

        bool hasMoreSerializedData(std::istream& stream)
        {
            return stream.peek() != std::char_traits<char>::eof();
        }

        bool isMapBaseTransform(const sgTransform& transform)
        {
            return transform.name.find("_MAPBASE_") != std::string::npos;
        }

        std::string serializedEntityName(const std::uint32_t entityId)
        {
            return "entity_" + std::to_string(entityId);
        }

        sgTransform transformWithSerializedNameFallback(sgTransform transform, const std::uint32_t entityId)
        {
            if (transform.name.empty()) transform.name = serializedEntityName(entityId);
            return transform;
        }

        void appendLayoutEntityRecord(
            entt::registry& source,
            const entt::entity entityHandle,
            std::vector<LayoutEntityRecord>& layoutEntities,
            std::unordered_set<entt::entity>& emittedEntities)
        {
            if (emittedEntities.contains(entityHandle)) return;
            if (!source.all_of<EditorMapEntity, sgTransform, Renderable, Collideable>(entityHandle)) return;

            emittedEntities.insert(entityHandle);
            auto& record = layoutEntities.emplace_back();
            record.entity.id = entt::entt_traits<entt::entity>::to_entity(entityHandle);
            record.transform =
                transformWithSerializedNameFallback(source.get<sgTransform>(entityHandle), record.entity.id);
            record.collideable = source.get<Collideable>(entityHandle);
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
            record.renderable = source.get<Renderable>(entityHandle);
        }
    } // namespace

    bool IsEditorLayoutMap(const char* path)
    {
        std::ifstream storage(path, std::ios::binary);
        if (!storage.is_open()) return false;

        char fileMagic[4]{};
        storage.read(fileMagic, sizeof(fileMagic));
        return storage.gcount() == sizeof(fileMagic) &&
               std::memcmp(fileMagic, kEditorLayoutMapMagic, sizeof(fileMagic)) == 0;
    }

    bool LoadMap(entt::registry* destination, const char* path)
    {
        assert(destination != nullptr);
        if (!IsEditorLayoutMap(path))
        {
            std::cerr << "ERROR: Not an editor layout map: " << path << std::endl;
            return false;
        }

        std::cout << "START: Loading layout map data from file (editor)." << std::endl;

        std::unordered_map<std::uint32_t, entt::entity> idMap;
        std::vector<entt::entity> loadedLayoutEntities;

        sage::serializer::ReadCompressedBinary(
            path, kEditorLayoutMapMagic, [&](cereal::BinaryInputArchive& input, std::istream& stream) {
                std::vector<entt::entity> loadedLights;
                std::vector<Light> lights;
                input(lights);
                loadedLights.reserve(lights.size());
                for (const auto& light : lights)
                {
                    const auto entity = destination->create();
                    auto loadedLight = light;
                    loadedLight.enabled = true;
                    destination->emplace<EditorMapEntity>(entity);
                    destination->emplace<Light>(entity, loadedLight);
                    loadedLights.push_back(entity);
                }

                std::vector<LayoutEntityRecord> layoutEntities;
                input(layoutEntities);
                for (const auto& record : layoutEntities)
                {
                    const auto entity = destination->create();
                    auto transform = transformWithSerializedNameFallback(record.transform, record.entity.id);
                    destination->emplace<EditorMapEntity>(entity);
                    destination->emplace<MetaData>(entity);
                    destination->emplace<sgTransform>(entity, transform);
                    destination->emplace<Collideable>(entity, record.collideable);
                    destination->get<Collideable>(entity).isStatic = true;
                    if (record.hasNavigationSurface)
                        destination->emplace<NavigationSurface>(entity, record.navigationSurface);
                    if (record.hasNavigationObstacle)
                        destination->emplace<NavigationObstacle>(entity, record.navigationObstacle);
                    if (record.hasTriggerVolume)
                        destination->emplace<TriggerVolume>(entity, record.triggerVolume);
                    if (record.hasCursorTarget)
                        destination->emplace<CursorTarget>(entity, record.cursorTarget);
                    auto renderable = record.renderable;
                    if (isMapBaseTransform(transform)) renderable.active = false;
                    destination->emplace<Renderable>(entity, renderable);
                    idMap[record.entity.id] = entity;
                    loadedLayoutEntities.push_back(entity);
                }

                if (hasMoreSerializedData(stream))
                {
                    std::vector<LightEditorObjectRecord> lightEditorObjects;
                    input(lightEditorObjects);
                    for (const auto& record : lightEditorObjects)
                    {
                        if (record.lightIndex >= loadedLights.size()) continue;
                        const auto entity = loadedLights[record.lightIndex];
                    }
                }

                if (hasMoreSerializedData(stream))
                {
                    std::vector<EntityEditorObjectRecord> entityEditorObjects;
                    input(entityEditorObjects);
                    for (const auto& record : entityEditorObjects)
                    {
                        const auto iter = idMap.find(record.entity.id);
                        if (iter == idMap.end()) continue;
                    }
                }

                if (hasMoreSerializedData(stream))
                {
                    std::vector<EntityMetaDataRecord> entityMetaData;
                    input(entityMetaData);
                    for (const auto& record : entityMetaData)
                    {
                        const auto iter = idMap.find(record.entity.id);
                        if (iter == idMap.end()) continue;
                        destination->emplace_or_replace<MetaData>(iter->second, record.metaData);
                    }
                }

                std::vector<entt::entity> loadedSpawnerEntities;
                if (hasMoreSerializedData(stream))
                {
                    std::vector<Spawner> spawners;
                    input(spawners);
                    for (const auto& spawner : spawners)
                    {
                        const auto entity = destination->create();
                        destination->emplace<EditorMapEntity>(entity);
                        auto& transform = destination->emplace<sgTransform>(entity);
                        transform.position.world = spawner.pos;
                        transform.rotation.world = spawner.rot;
                        transform.name = "spawner_" + spawner.name;
                        destination->emplace<Spawner>(entity, spawner);
                        loadedSpawnerEntities.push_back(entity);
                    }
                }

                std::vector<entt::entity> loadedTriggerEntities;
                if (hasMoreSerializedData(stream))
                {
                    std::vector<TriggerRecord> triggers;
                    input(triggers);
                    for (const auto& record : triggers)
                    {
                        const auto entity = destination->create();
                        destination->emplace<EditorMapEntity>(entity);
                        auto& transform = destination->emplace<sgTransform>(entity);
                        transform.position.world = record.position;
                        transform.name = "trigger";
                        destination->emplace<Collideable>(entity, record.collideable);
                        destination->emplace<TriggerVolume>(entity, record.triggerVolume);
                        loadedTriggerEntities.push_back(entity);
                    }
                }

                if (hasMoreSerializedData(stream))
                {
                    std::vector<EntityScriptRecord> scripts;
                    input(scripts);
                    for (const auto& record : scripts)
                    {
                        entt::entity target = entt::null;
                        switch (record.targetKind)
                        {
                        case 0:
                            if (const auto iter = idMap.find(record.targetId); iter != idMap.end())
                                target = iter->second;
                            break;
                        case 1:
                            if (record.targetId < loadedSpawnerEntities.size())
                                target = loadedSpawnerEntities[record.targetId];
                            break;
                        case 2:
                            if (record.targetId < loadedTriggerEntities.size())
                                target = loadedTriggerEntities[record.targetId];
                            break;
                        default:
                            break;
                        }
                        if (target == entt::null) continue;
                        destination->emplace_or_replace<ScriptComponent>(target, record.script);
                    }
                }

                if (hasMoreSerializedData(stream))
                {
                    std::vector<EntityAnimationRecord> animations;
                    input(animations);
                    for (const auto& record : animations)
                    {
                        entt::entity target = entt::null;
                        switch (record.targetKind)
                        {
                        case 0:
                            if (const auto iter = idMap.find(record.targetId); iter != idMap.end())
                                target = iter->second;
                            break;
                        case 1:
                            if (record.targetId < loadedSpawnerEntities.size())
                                target = loadedSpawnerEntities[record.targetId];
                            break;
                        case 2:
                            if (record.targetId < loadedTriggerEntities.size())
                                target = loadedTriggerEntities[record.targetId];
                            break;
                        default:
                            break;
                        }
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
                }

                if (hasMoreSerializedData(stream))
                {
                    std::vector<EntityMoveableActorRecord> moveables;
                    input(moveables);
                    for (const auto& record : moveables)
                    {
                        const auto iter = idMap.find(record.targetId);
                        if (record.targetKind != 0 || iter == idMap.end()) continue;
                        auto& moveable = destination->get_or_emplace<MoveableActor>(iter->second);
                        moveable.movementSpeed = record.movementSpeed;
                        moveable.pathfindingBounds = record.pathfindingBounds;
                        moveable.moveClip = record.moveClip;
                        moveable.idleClip = record.idleClip;
                    }
                }

                {
                    std::vector<TerrainRecord> terrains;
                    input(terrains);
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
                        auto& transform = destination->emplace<sgTransform>(entity);
                        transform.position.world = record.position;
                        transform.name = "terrain_" + std::to_string(entt::to_integral(entity));
                        destination->emplace<Terrain>(entity, std::move(terrain));
                        auto& collideable =
                            destination->emplace<Collideable>(entity, record.collideable);
                        collideable.isStatic = true;
                        // The mesh, shader and collision bounds are derived after
                        // load (EditorScene::refreshAfterMapLoad).
                    }
                }

            });

        for (const auto entity : loadedLayoutEntities)
        {
            if (destination->valid(entity) && destination->any_of<EditorMapEntity, sgTransform>(entity))
            {
                destination->get<sgTransform>(entity).ResolveSerializedParent(idMap);
            }
        }

        RebuildSceneTagIndex(*destination);

        std::cout << "FINISH: Loading layout map data from file (editor)." << std::endl;
        return true;
    }

    void SaveMap(entt::registry& source, const char* path)
    {
        SaveMap(source, path, {});
    }

    void SaveMap(entt::registry& source, const char* path, const std::vector<entt::entity>& hierarchyOrder)
    {
        std::cout << "START: Saving layout map data to file (editor)." << std::endl;

        const std::filesystem::path outputPath{path};
        if (const auto parent = outputPath.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        sage::serializer::WriteCompressedBinary(
            path, kEditorLayoutMapMagic, [&](cereal::BinaryOutputArchive& output) {
                std::vector<Light> lights;
                std::vector<entt::entity> lightEntities;
                for (const auto entity : source.view<EditorMapEntity, Light>())
                {
                    auto light = source.get<Light>(entity);
                    if (source.any_of<sgTransform>(entity))
                    {
                        light.position = source.get<sgTransform>(entity).GetWorldPos();
                    }
                    lights.push_back(light);
                    lightEntities.push_back(entity);
                }
                output(lights);

                std::vector<LayoutEntityRecord> layoutEntities;
                std::unordered_set<entt::entity> emittedEntities;
                emittedEntities.reserve(hierarchyOrder.size());
                for (const auto entityHandle : hierarchyOrder)
                {
                    appendLayoutEntityRecord(source, entityHandle, layoutEntities, emittedEntities);
                }
                for (const auto entityHandle : source.view<EditorMapEntity, sgTransform, Renderable, Collideable>())
                {
                    appendLayoutEntityRecord(source, entityHandle, layoutEntities, emittedEntities);
                }
                output(layoutEntities);

                std::vector<LightEditorObjectRecord> lightEditorObjects;
                lightEditorObjects.reserve(lightEntities.size());
                for (std::size_t i = 0; i < lightEntities.size(); ++i)
                {
                    // TODO
                    const auto entity = lightEntities[i];
                }
                output(lightEditorObjects);

                std::vector<EntityEditorObjectRecord> entityEditorObjects;
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Renderable, Collideable>())
                {
                    entityEditorObjects.push_back(
                        EntityEditorObjectRecord{.entity = {entt::entt_traits<entt::entity>::to_entity(entity)}});
                }
                output(entityEditorObjects);

                std::vector<EntityMetaDataRecord> entityMetaData;
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Renderable, Collideable, MetaData>())
                {
                    const auto& metaData = source.get<MetaData>(entity);
                    if (metaData.tags.empty()) continue;

                    entityMetaData.push_back(
                        EntityMetaDataRecord{
                            .entity = {entt::entt_traits<entt::entity>::to_entity(entity)},
                            .metaData = metaData});
                }
                output(entityMetaData);

                std::vector<Spawner> spawners;
                std::vector<entt::entity> spawnerEntities;
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Spawner>())
                {
                    auto spawner = source.get<Spawner>(entity);
                    const auto& transform = source.get<sgTransform>(entity);
                    spawner.pos = transform.GetWorldPos();
                    spawner.rot = transform.GetWorldRot();
                    spawners.push_back(spawner);
                    spawnerEntities.push_back(entity);
                }
                output(spawners);

                std::vector<TriggerRecord> triggers;
                std::vector<entt::entity> triggerEntities;
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Collideable, TriggerVolume>())
                {
                    const auto& collideable = source.get<Collideable>(entity);
                    // Mesh-bearing triggers round-trip via the layout-entity stream;
                    // only meshless trigger markers go here.
                    if (source.all_of<Renderable>(entity)) continue;
                    triggers.push_back(
                        TriggerRecord{
                            .position = source.get<sgTransform>(entity).GetWorldPos(),
                            .collideable = collideable,
                            .triggerVolume = source.get<TriggerVolume>(entity)});
                    triggerEntities.push_back(entity);
                }
                output(triggers);

                std::vector<EntityScriptRecord> scripts;
                const auto appendScript =
                    [&](const entt::entity entity, const std::uint8_t kind, const std::uint32_t id) {
                        if (!source.all_of<ScriptComponent>(entity)) return;
                        const auto& script = source.get<ScriptComponent>(entity);
                        if (script.scriptPath.empty()) return;
                        scripts.push_back(EntityScriptRecord{kind, id, script});
                    };
                for (const auto entity : emittedEntities)
                {
                    appendScript(entity, 0, entt::entt_traits<entt::entity>::to_entity(entity));
                }
                for (std::size_t i = 0; i < spawnerEntities.size(); ++i)
                {
                    appendScript(spawnerEntities[i], 1, static_cast<std::uint32_t>(i));
                }
                for (std::size_t i = 0; i < triggerEntities.size(); ++i)
                {
                    appendScript(triggerEntities[i], 2, static_cast<std::uint32_t>(i));
                }
                output(scripts);

                // Animation requires a Renderable, so in practice only layout
                // entities (kind 0) carry one; the record keeps the same addressing
                // as scripts so the loaders stay uniform.
                std::vector<EntityAnimationRecord> animations;
                for (const auto entity : emittedEntities)
                {
                    if (!source.all_of<Animation>(entity)) continue;
                    const auto& animation = source.get<Animation>(entity);
                    if (animation.modelKey.empty()) continue;
                    animations.push_back(
                        EntityAnimationRecord{
                            0, entt::entt_traits<entt::entity>::to_entity(entity), animation.modelKey});
                }
                output(animations);

                // Like Animation, only layout entities (kind 0) carry a MoveableActor.
                std::vector<EntityMoveableActorRecord> moveables;
                for (const auto entity : emittedEntities)
                {
                    if (!source.all_of<MoveableActor>(entity)) continue;
                    const auto& moveable = source.get<MoveableActor>(entity);
                    moveables.push_back(
                        EntityMoveableActorRecord{
                            0,
                            entt::entt_traits<entt::entity>::to_entity(entity),
                            moveable.movementSpeed,
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
                            source.get<sgTransform>(entity).GetWorldPos(),
                            terrain.resolution,
                            terrain.cellSize,
                            collideable,
                            terrain.heights});
                }
                output(terrains);

            });

        std::cout << "FINISH: Saving layout map data to file (editor)." << std::endl;
    }
} // namespace sage::editor

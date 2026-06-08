#include "EditorMapLoader.hpp"

#include "EditorComponents.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/Light.hpp"
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
            Renderable renderable{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(entity, transform, collideable, renderable);
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

        // A trigger is just a non-blocking Collideable with isTrigger=true and no mesh.
        // It carries its own box, so we save the Collideable alongside the world position
        // (taken from the transform). Must stay in sync with the game loader's TriggerRecord
        // (LevelLayoutLoader.cpp).
        struct TriggerRecord
        {
            Vector3 position{};
            Collideable collideable{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(position, collideable);
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
                    }
                }

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
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Spawner>())
                {
                    auto spawner = source.get<Spawner>(entity);
                    const auto& transform = source.get<sgTransform>(entity);
                    spawner.pos = transform.GetWorldPos();
                    spawner.rot = transform.GetWorldRot();
                    spawners.push_back(spawner);
                }
                output(spawners);

                std::vector<TriggerRecord> triggers;
                for (const auto entity : source.view<EditorMapEntity, sgTransform, Collideable>())
                {
                    const auto& collideable = source.get<Collideable>(entity);
                    // Mesh-bearing triggers round-trip via the layout-entity stream (their
                    // Collideable carries isTrigger); only meshless trigger markers go here.
                    if (!collideable.isTrigger || source.all_of<Renderable>(entity)) continue;
                    triggers.push_back(
                        TriggerRecord{
                            .position = source.get<sgTransform>(entity).GetWorldPos(),
                            .collideable = collideable});
                }
                output(triggers);
            });

        std::cout << "FINISH: Saving layout map data to file (editor)." << std::endl;
    }
} // namespace sage::editor

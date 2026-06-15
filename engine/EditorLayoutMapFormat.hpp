#pragma once

#include "engine/Archetypes.hpp"
#include "engine/SceneTags.hpp"
#include "engine/Serializer.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"

#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "raylib.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sage::editor_layout
{
    inline constexpr char MapMagic[4] = {'L', 'Q', 'E', '2'};
    inline constexpr std::string_view MapBaseNameMarker = "_MAPBASE_";

    [[nodiscard]] inline bool IsMapBaseTransformName(const std::string_view name)
    {
        return name.find(MapBaseNameMarker) != std::string_view::npos;
    }

    [[nodiscard]] inline bool IsMapBaseTransform(const sgTransform& transform)
    {
        return IsMapBaseTransformName(transform.name);
    }

    [[nodiscard]] inline std::string SerializedEntityName(const std::uint32_t entityId)
    {
        return "entity_" + std::to_string(entityId);
    }

    [[nodiscard]] inline sgTransform TransformWithSerializedNameFallback(
        sgTransform transform, const std::uint32_t entityId)
    {
        if (transform.name.empty()) transform.name = SerializedEntityName(entityId);
        return transform;
    }

    struct LayoutEntityRecord
    {
        serializer::entity entity{};
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

    struct EntityMetaDataRecord
    {
        serializer::entity entity{};
        MetaData metaData{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(entity, metaData);
        }
    };

    // Compatibility record for the old map marker section. Runtime/editor code
    // now materializes these as ordinary entities tagged SpawnPoint.
    enum class LegacySpawnPointType
    {
        PLAYER,
        ENEMY,
        DIALOG_CUTSCENE,
        NPC
    };

    struct LegacySpawnPointRecord
    {
        std::string name;
        LegacySpawnPointType type = LegacySpawnPointType::ENEMY;
        Vector3 pos{};
        Vector3 rot{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(type, name, pos, rot);
        }
    };

    [[nodiscard]] inline std::string_view LegacySpawnPointTypeName(const LegacySpawnPointType type)
    {
        switch (type)
        {
        case LegacySpawnPointType::PLAYER:
            return "player";
        case LegacySpawnPointType::ENEMY:
            return "enemy";
        case LegacySpawnPointType::DIALOG_CUTSCENE:
            return "dialog";
        case LegacySpawnPointType::NPC:
            return "npc";
        }
        return "point";
    }

    [[nodiscard]] inline std::string LegacySpawnPointEntityName(
        const LegacySpawnPointRecord& record, const std::size_t index)
    {
        const std::string suffix = record.name.empty() ? std::to_string(index) : record.name;
        return "spawn_" + std::string{LegacySpawnPointTypeName(record.type)} + "_" + suffix;
    }

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

    inline constexpr std::uint8_t LayoutEntityTargetKind = 0;
    inline constexpr std::uint8_t SpawnPointTargetKind = 1;
    inline constexpr std::uint8_t TriggerTargetKind = 2;

    [[nodiscard]] inline entt::entity ResolveEntityTarget(
        const std::uint8_t targetKind,
        const std::uint32_t targetId,
        const std::unordered_map<std::uint32_t, entt::entity>& layoutEntities,
        const std::vector<entt::entity>& spawnPointEntities,
        const std::vector<entt::entity>& triggerEntities)
    {
        switch (targetKind)
        {
        case LayoutEntityTargetKind:
            if (const auto iter = layoutEntities.find(targetId); iter != layoutEntities.end()) return iter->second;
            break;
        case SpawnPointTargetKind:
            if (targetId < spawnPointEntities.size()) return spawnPointEntities[targetId];
            break;
        case TriggerTargetKind:
            if (targetId < triggerEntities.size()) return triggerEntities[targetId];
            break;
        default:
            break;
        }
        return entt::null;
    }

    struct EntityScriptRecord
    {
        // targetId is a saved entity id for layout entities, or a section index
        // for spawn points and triggers.
        std::uint8_t targetKind = LayoutEntityTargetKind;
        std::uint32_t targetId = 0;
        ScriptComponent script{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetKind, targetId, script);
        }
    };

    struct EntityAnimationRecord
    {
        std::uint8_t targetKind = LayoutEntityTargetKind;
        std::uint32_t targetId = 0;
        std::string modelKey;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetKind, targetId, modelKey);
        }
    };

    struct EntityMoveableActorRecord
    {
        std::uint8_t targetKind = LayoutEntityTargetKind;
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

    struct EntityArchetypeRecord
    {
        // Addressed like EntityScriptRecord.
        std::uint8_t targetKind = LayoutEntityTargetKind;
        std::uint32_t targetId = 0;
        Archetype archetype{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetKind, targetId, archetype);
        }
    };
} // namespace sage::editor_layout

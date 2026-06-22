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
    inline constexpr char MapMagic[4] = {'L', 'Q', 'E', '4'};
    inline constexpr char LegacyMapMagic[4] = {'L', 'Q', 'E', '3'};
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

    // A single map entity is just a transform plus whatever components it was
    // authored with (Unity-style composition). Every optional component is gated by
    // a presence flag, so an empty grouping node, a collider-only box, a renderable
    // prop, a trigger marker and a full layout object all round-trip through this one
    // record. Light and Terrain keep dedicated sections (engine-managed / bulk data).
    struct EntityRecord
    {
        serializer::entity entity{};
        sgTransform transform{};
        bool hasRenderable = false;
        Renderable renderable{};
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
        bool hasMetaData = false;
        MetaData metaData{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                entity,
                transform,
                hasRenderable,
                renderable,
                hasCollideable,
                collideable,
                hasNavigationSurface,
                navigationSurface,
                hasNavigationObstacle,
                navigationObstacle,
                hasTriggerVolume,
                triggerVolume,
                hasCursorTarget,
                cursorTarget,
                hasMetaData,
                metaData);
        }
    };

    // Attachment records (script/animation/moveable/archetype) name their owning
    // entity by its saved id, resolved through the load's id map.
    struct EntityScriptRecord
    {
        std::uint32_t targetId = 0;
        ScriptComponent script{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetId, script);
        }
    };

    struct EntityAnimationRecord
    {
        std::uint32_t targetId = 0;
        std::string modelKey;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetId, modelKey);
        }
    };

    struct EntityMoveableActorRecord
    {
        std::uint32_t targetId = 0;
        float movementSpeed = 0.0f;
        // Matches MoveableActor::turnSpeed's default; only used pre-load (the archive overwrites it).
        float turnSpeed = 540.0f;
        std::int32_t pathfindingBounds = 0;
        std::string moveClip;
        std::string idleClip;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetId, movementSpeed, turnSpeed, pathfindingBounds, moveClip, idleClip);
        }
    };

    struct LegacyEntityMoveableActorRecord
    {
        std::uint32_t targetId = 0;
        float movementSpeed = 0.0f;
        std::int32_t pathfindingBounds = 0;
        std::string moveClip;
        std::string idleClip;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetId, movementSpeed, pathfindingBounds, moveClip, idleClip);
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
        std::uint32_t targetId = 0;
        Archetype archetype{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetId, archetype);
        }
    };

    struct EntityGameComponentRecord
    {
        std::uint32_t targetId = 0;
        std::string key;
        std::string data;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(targetId, key, data);
        }
    };
} // namespace sage::editor_layout

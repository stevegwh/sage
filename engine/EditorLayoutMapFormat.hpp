#pragma once

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

#include <cstdint>
#include <string>
#include <vector>

namespace sage::editor_layout
{
    inline constexpr char MapMagic[4] = {'L', 'Q', 'E', '2'};

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
} // namespace sage::editor_layout

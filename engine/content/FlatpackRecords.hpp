#pragma once

#include "cereal/types/vector.hpp"
#include "engine/Colors.hpp"
#include "engine/Archetypes.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/CustomShaderComponent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/Flatpack.hpp"
#include "engine/Light.hpp"
#include "engine/raylib-cereal.hpp"

namespace sage::content_binary
{
    struct StoredRenderableRecord
    {
        std::uint8_t kind = 0;
        std::string key;
        Matrix initialTransform{};
        bool active = true;
        Color hint = sage::colors::WHITE_COLOR;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("kind", kind),
                cereal::make_nvp("key", key),
                cereal::make_nvp("initialTransform", initialTransform));
            if constexpr (
                std::is_same_v<Archive, cereal::JSONInputArchive> ||
                std::is_same_v<Archive, cereal::JSONOutputArchive>)
                archive(cereal::make_nvp("active", active), cereal::make_nvp("hint", hint));
        }
    };

    struct LegacyCursorTargetRecord
    {
        std::string cursor{cursors::Regular};
        bool hoverable = false;
        bool allowNavigationClickThrough = true;

        template <class Archive>
        void save(Archive& archive) const
        {
            const bool ignoredCursorFlag = false;
            archive(cursor, hoverable, allowNavigationClickThrough, ignoredCursorFlag);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            bool ignoredCursorFlag = false;
            archive(cursor, hoverable, allowNavigationClickThrough, ignoredCursorFlag);
        }

        [[nodiscard]] CursorTarget Current() const
        {
            return {cursor, hoverable, allowNavigationClickThrough};
        }
    };

    template <class RenderableRecord, class CursorTargetRecord>
    struct BasicFlatpackEntityRecord
    {
        std::int32_t parentLocalId = -1;
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
        CursorTargetRecord cursorTarget{};
        bool hasRenderable = false;
        RenderableRecord renderable{};
        bool hasLight = false;
        Light light{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(parentLocalId, worldPos, worldRot, worldScale);
            archive(hasCollideable);
            if (hasCollideable) archive(collideable);
            archive(hasNavigationSurface);
            if (hasNavigationSurface) archive(navigationSurface);
            archive(hasNavigationObstacle);
            if (hasNavigationObstacle) archive(navigationObstacle);
            archive(hasTriggerVolume);
            if (hasTriggerVolume) archive(triggerVolume);
            archive(hasCursorTarget);
            if (hasCursorTarget) archive(cursorTarget);
            archive(hasRenderable);
            if (hasRenderable) archive(renderable);
            archive(hasLight);
            if (hasLight) archive(light);
        }
    };

    using FlatpackEntityRecord = BasicFlatpackEntityRecord<Renderable, CursorTarget>;
    using LegacyFlatpackEntityRecord = BasicFlatpackEntityRecord<Renderable, LegacyCursorTargetRecord>;
    using MigrationFlatpackEntityRecord = BasicFlatpackEntityRecord<StoredRenderableRecord, CursorTarget>;
    using LegacyMigrationFlatpackEntityRecord =
        BasicFlatpackEntityRecord<StoredRenderableRecord, LegacyCursorTargetRecord>;

    inline FlatpackEntityRecord CurrentRecord(LegacyFlatpackEntityRecord&& legacy)
    {
        FlatpackEntityRecord current;
        current.parentLocalId = legacy.parentLocalId;
        current.worldPos = legacy.worldPos;
        current.worldRot = legacy.worldRot;
        current.worldScale = legacy.worldScale;
        current.hasCollideable = legacy.hasCollideable;
        current.collideable = std::move(legacy.collideable);
        current.hasNavigationSurface = legacy.hasNavigationSurface;
        current.navigationSurface = legacy.navigationSurface;
        current.hasNavigationObstacle = legacy.hasNavigationObstacle;
        current.navigationObstacle = legacy.navigationObstacle;
        current.hasTriggerVolume = legacy.hasTriggerVolume;
        current.triggerVolume = legacy.triggerVolume;
        current.hasCursorTarget = legacy.hasCursorTarget;
        current.cursorTarget = legacy.cursorTarget.Current();
        current.hasRenderable = legacy.hasRenderable;
        current.renderable = std::move(legacy.renderable);
        current.hasLight = legacy.hasLight;
        current.light = legacy.light;
        return current;
    }

    // Component sections reference entities by localId, which indexes the
    // records vector.
    struct FlatpackScriptRecord
    {
        std::uint32_t localId = 0;
        ScriptComponent script{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, script);
        }
    };

    // Only the model key is saved; clips are derived from packed animation
    // data on load.
    struct FlatpackAnimationRecord
    {
        std::uint32_t localId = 0;
        std::string modelKey;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, modelKey);
        }
    };

    // Only the persistent fields are saved; the rest of the component is
    // runtime state.
    struct FlatpackMoveableActorRecord
    {
        std::uint32_t localId = 0;
        float movementSpeed = 0.0f;
        float turnSpeed = 240.0f;
        std::int32_t pathfindingBounds = 0;
        std::string moveClip;
        std::string idleClip;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, movementSpeed, turnSpeed, pathfindingBounds, moveClip, idleClip);
        }
    };

    struct FlatpackArchetypeRecord
    {
        std::uint32_t localId = 0;
        Archetype archetype{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, archetype);
        }
    };

    struct FlatpackCustomShaderRecord
    {
        std::uint32_t localId = 0;
        CustomShaderComponent shader{};

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, shader);
        }
    };

    struct FlatpackCustomComponentRecord
    {
        std::uint32_t localId = 0;
        std::string key;
        std::string data;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, key, data);
        }
    };

    struct LegacyFlatpackMoveableActorRecord
    {
        std::uint32_t localId = 0;
        float movementSpeed = 0.0f;
        std::int32_t pathfindingBounds = 0;
        std::string moveClip;
        std::string idleClip;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(localId, movementSpeed, pathfindingBounds, moveClip, idleClip);
        }
    };

    template <class EntityRecord>
    struct BasicMigrationFlatpackData
    {
        std::vector<EntityRecord> records;
        std::vector<std::string> names;
        std::vector<FlatpackScriptRecord> scripts;
        std::vector<FlatpackAnimationRecord> animations;
        std::vector<FlatpackMoveableActorRecord> moveables;
        std::vector<FlatpackArchetypeRecord> archetypes;
        std::vector<FlatpackCustomShaderRecord> customShaders;
        std::vector<FlatpackCustomComponentRecord> customComponents;

        template <class Archive>
        void archive(Archive& value)
        {
            value(records, names, scripts, animations, moveables, archetypes, customShaders, customComponents);
        }
    };

    using MigrationFlatpackData = BasicMigrationFlatpackData<MigrationFlatpackEntityRecord>;
    using LegacyMigrationFlatpackData = BasicMigrationFlatpackData<LegacyMigrationFlatpackEntityRecord>;

} // namespace sage::content_binary

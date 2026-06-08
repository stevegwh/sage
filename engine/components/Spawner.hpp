//
// Created by Steve Wheeler on 17/09/2024.
//

#pragma once

#include "engine/raylib-cereal.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#include <string_view>
#include <vector>

namespace sage
{
    enum class SpawnerType
    {
        PLAYER,
        ENEMY,
        DIALOG_CUTSCENE,
        NPC
    };

    struct Spawner
    {
        // Can add a name for named/important mobs
        std::string name;
        SpawnerType type;
        Vector3 pos;
        Vector3 rot;
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(type, name, pos, rot);
        }

        template <class Inspector>
        void define_editor_fields(Inspector& i)
        {
            i.field("Name", name);
            i.field("Type", type);
            i.field("Position", pos);
            i.field("Rotation", rot);
        }
    };

    // Returns the first spawner entity of the given type, or entt::null if none exist.
    [[nodiscard]] inline entt::entity FindFirstSpawnerOfType(
        entt::registry& registry, const SpawnerType type)
    {
        for (auto view = registry.view<Spawner>(); const auto entity : view)
        {
            if (view.get<Spawner>(entity).type == type) return entity;
        }
        return entt::null;
    }

    // Returns every spawner entity of the given type.
    [[nodiscard]] inline std::vector<entt::entity> GetSpawnersOfType(
        entt::registry& registry, const SpawnerType type)
    {
        std::vector<entt::entity> result;
        for (auto view = registry.view<Spawner>(); const auto entity : view)
        {
            if (view.get<Spawner>(entity).type == type) result.push_back(entity);
        }
        return result;
    }

    // Returns the first spawner entity matching both type and name, or entt::null if none match.
    [[nodiscard]] inline entt::entity FindSpawnerOfTypeWithName(
        entt::registry& registry, const SpawnerType type, const std::string_view name)
    {
        for (auto view = registry.view<Spawner>(); const auto entity : view)
        {
            const auto& spawner = view.get<Spawner>(entity);
            if (spawner.type == type && spawner.name == name) return entity;
        }
        return entt::null;
    }
} // namespace sage

//
// Created by Steve Wheeler on 02/05/2024.
//

#include "Collideable.hpp"

#include "systems/ScriptSystem.hpp"
#include "raymath.h"
#include "sol/sol.hpp"

#include <algorithm>
#include <array>

namespace sage
{
    void Collideable::define_lua_bindings(ScriptSystem& scripts)
    {
        scripts.GetLuaState().new_usertype<Collideable>(
            "Collideable",
            sol::no_constructor,
            "active",
            &Collideable::active,
            "debugDraw",
            &Collideable::debugDraw);
        scripts.RegisterApiExtension("sage", [](sol::table& api, entt::entity, entt::registry& registry) {
            api.set_function("GetCollideable", [&registry](const std::uint32_t id) {
                return registry.try_get<Collideable>(static_cast<entt::entity>(id));
            });
        });
    }

    BoundingBox TransformBoundingBox(const BoundingBox& local, const Matrix& worldMat)
    {
        return {Vector3Transform(local.min, worldMat), Vector3Transform(local.max, worldMat)};
    }

    BoundingBox TransformBoundingBoxByCorners(const BoundingBox& local, const Matrix& worldMat)
    {
        const std::array<Vector3, 8> corners = {
            Vector3{local.min.x, local.min.y, local.min.z},
            Vector3{local.min.x, local.min.y, local.max.z},
            Vector3{local.min.x, local.max.y, local.min.z},
            Vector3{local.min.x, local.max.y, local.max.z},
            Vector3{local.max.x, local.min.y, local.min.z},
            Vector3{local.max.x, local.min.y, local.max.z},
            Vector3{local.max.x, local.max.y, local.min.z},
            Vector3{local.max.x, local.max.y, local.max.z},
        };

        BoundingBox transformed{};
        transformed.min = transformed.max = Vector3Transform(corners.front(), worldMat);

        for (const auto& corner : corners)
        {
            const auto worldCorner = Vector3Transform(corner, worldMat);
            transformed.min.x = std::min(transformed.min.x, worldCorner.x);
            transformed.min.y = std::min(transformed.min.y, worldCorner.y);
            transformed.min.z = std::min(transformed.min.z, worldCorner.z);
            transformed.max.x = std::max(transformed.max.x, worldCorner.x);
            transformed.max.y = std::max(transformed.max.y, worldCorner.y);
            transformed.max.z = std::max(transformed.max.z, worldCorner.z);
        }

        return transformed;
    }

    Collideable::Collideable(const BoundingBox& local, const Matrix& worldMat)
    {
        localBoundingBox = local;
        worldBoundingBox = TransformBoundingBox(local, worldMat);
    }

} // namespace sage

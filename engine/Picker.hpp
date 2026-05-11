#pragma once

#include "Event.hpp"
#include "systems/CollisionSystem.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <string>

namespace sage
{
    class EngineSystems;

    class Picker
    {
        entt::registry* registry;
        EngineSystems* sys;

        CollisionInfo mouseHitInfo{};
        CollisionInfo navigationHitInfo{};
        Ray ray{};

        void updateMouseRayCollision();
        static void resetHitInfo(CollisionInfo& hitInfo);
        [[nodiscard]] bool findMeshCollision(CollisionInfo& hitInfo) const;

      public:
        std::string hitObjectName{};
        Event<entt::entity> onCollisionHit{};

        void Update();
        [[nodiscard]] const CollisionInfo& GetMouseHitInfo() const;
        [[nodiscard]] const RayCollision& GetFirstNavigationCollision() const;
        [[nodiscard]] const RayCollision& GetFirstCollision() const;

        Picker(entt::registry* _registry, EngineSystems* _sys);
    };
} // namespace sage

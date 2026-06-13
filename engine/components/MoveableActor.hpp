//
// Created by Steve Wheeler on 16/06/2024.
//

#pragma once

#include "NavigationGridSquare.hpp"

#include "entt/entt.hpp"
#include "engine/Event.hpp"
#include "raylib.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sage
{

    struct MoveableActorCollision
    {
        entt::entity hitEntityId = entt::null;
        Vector3 hitLastPos{};
    };

    struct MoveableActor
    {
        float movementSpeed = 0.35f;
        // The max range the actor can pathfind at one time.
        int pathfindingBounds = 50;
        // GLB clip names played while moving / stopped (AnimationSystem polls
        // IsMoving). Unknown clip names leave the current animation untouched.
        std::string moveClip = "Walk";
        std::string idleClip = "Idle";
        // Max turn rate in degrees per second; 0 snaps to the movement direction
        // instantly. Runtime-only, not serialized.
        float turnSpeed = 240.0f;

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("Movement Speed", movementSpeed);
            i.field("Pathfinding Bounds", pathfindingBounds);
            i.clipDropdown("Move Clip", moveClip);
            i.clipDropdown("Idle Clip", idleClip);
        }

        // std::optional<MoveableActorCollision> moveableActorCollision;
        entt::entity hitEntityId = entt::null;
        Vector3 hitLastPos{};
        // Keeps collision rerouting from fighting deliberate movement toward another moveable entity.
        std::optional<entt::entity> movementCollisionTarget;
        std::deque<Vector3> path{};

        Event<entt::entity> onStartMovement{};
        Event<entt::entity> onDestinationReached{};
        Event<entt::entity, Vector3> onDestinationUnreachable{}; // self, original dest
        Event<entt::entity> onPathChanged{};    // Was previously moving, now moving somewhere else
        Event<entt::entity> onMovementCancel{}; // Was previously moving, now cancelled

        [[nodiscard]] bool IsMoving() const
        {
            return !path.empty();
        }

        [[nodiscard]] Vector3 GetDestination() const
        {
            assert(IsMoving()); // Check this independently before calling this function.
            return path.back();
        }

        std::vector<GridSquare> debugRay;
    };
} // namespace sage

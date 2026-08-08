//
// Created by Steve Wheeler on 16/06/2024.
//

#pragma once

#include "NavigationGridSquare.hpp"

#include "engine/Event.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace sage
{
    struct MoveableActor
    {
        float movementSpeed = 0.35f;
        // The max range the actor can pathfind at one time.
        int pathfindingBounds = 50;
        // GLB clip names played while moving / stopped (AnimationSystem polls
        // IsMoving). Unknown clip names leave the current animation untouched.
        std::string moveClip = "Walking";
        std::string idleClip = "Idle";
        // Max turn rate in degrees per second; 0 snaps to the movement direction instantly.
        float turnSpeed = 540.0f;
        // A path may be active while the actor turns in place. This tracks when
        // translation has actually begun so animation does not walk during the turn.
        bool isWalking = false;
        // Runtime guard for aligning the entity origin with the horizontal centre
        // of its rendered bounds, so yaw rotates the actor in place.
        bool hasCenteredTurnPivot = false;

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("Movement Speed", movementSpeed);
            i.field("Turn Speed", turnSpeed);
            i.field("Pathfinding Bounds", pathfindingBounds);
            i.clipDropdown("Move Clip", moveClip);
            i.clipDropdown("Idle Clip", idleClip);
        }

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

        [[nodiscard]] bool IsWalking() const
        {
            return isWalking;
        }

        [[nodiscard]] Vector3 GetDestination() const
        {
            assert(IsMoving()); // Check this independently before calling this function.
            return path.back();
        }

        template <class Api>
        static void define_script_api(Api& api)
        {
            api.event("OnMovementStarted", &MoveableActor::onStartMovement);
            api.event("OnDestinationReached", &MoveableActor::onDestinationReached);
            api.event("OnDestinationUnreachable", &MoveableActor::onDestinationUnreachable);
            api.event("OnPathChanged", &MoveableActor::onPathChanged);
            api.event("OnMovementCancelled", &MoveableActor::onMovementCancel);
        }

        std::vector<GridSquare> debugRay;
    };
} // namespace sage

//
// Created by Steve Wheeler on 16/06/2024.
//

#pragma once

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "engine/Event.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#include <deque>
#include <string>

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

        // Persist configured movement settings; routes, flags and subscriptions are runtime state.
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("movementSpeed", movementSpeed),
                cereal::make_nvp("turnSpeed", turnSpeed),
                cereal::make_nvp("pathfindingBounds", pathfindingBounds),
                cereal::make_nvp("moveClip", moveClip),
                cereal::make_nvp("idleClip", idleClip));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("movement_speed", "Movement Speed", movementSpeed);
            i.field("turn_speed", "Turn Speed", turnSpeed);
            i.field("pathfinding_bounds", "Pathfinding Bounds", pathfindingBounds);
            i.clipDropdown("move_clip", "Move Clip", moveClip);
            i.clipDropdown("idle_clip", "Idle Clip", idleClip);
        }

        std::deque<Vector3> path{};
        // A cancelled route may still need a short move to an unoccupied stopping place.
        bool needsStopPosition = false;
        bool notifyOnArrival = true;
        float stopRetryTime = 0.0f;

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

        void ClearRoute(const entt::entity entity)
        {
            const bool wasMoving = IsMoving();
            path.clear();
            isWalking = false;
            notifyOnArrival = false;
            needsStopPosition = true;
            stopRetryTime = 0.0f;
            if (wasMoving) onMovementCancel.Publish(entity);
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
    };
} // namespace sage

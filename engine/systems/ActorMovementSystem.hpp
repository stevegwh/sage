//
// Created by Steve Wheeler on 21/02/2024.
//
#pragma once

#include "engine/Event.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace sage
{
    enum class PathfindFailureReason
    {
        DestinationOutOfGrid,
        ActorOutOfGrid,
        DestinationOutOfRange,
        DestinationUnreachable
    };

    // Forward declarations
    class NavigationGridSystem;
    struct MoveableActor;
    struct sgTransform;

    class ActorMovementSystem
    {
        struct RouteSearchResult
        {
            std::vector<Vector3> route{};
            std::optional<PathfindFailureReason> failure{};
        };

        NavigationGridSystem* navigationGrid;
        entt::registry* registry;
        // Remember the actual stamped bounds so removal/teleport can release the old cells.
        mutable std::unordered_map<entt::entity, BoundingBox> stoppedFootprints;

        [[nodiscard]] RouteSearchResult findRouteToLocation(
            entt::entity entity, const Vector3& destination, bool astar, bool findNextBestIfInvalid) const;
        void updateActor(
            entt::entity entity, MoveableActor& moveableActor, sgTransform& transform, float deltaTime, float speed);
        static bool hasReachedNextPoint(const sgTransform& transform, const MoveableActor& moveableActor);
        void handlePointReached(entt::entity entity, MoveableActor& moveableActor);
        void releaseStoppedFootprint(entt::entity entity) const;
        bool claimStoppingPosition(entt::entity entity, Vector3 position);
        bool rerouteToStoppingPosition(entt::entity entity, MoveableActor& actor, Vector3 destination) const;
        void setActorPosition(entt::entity entity, sgTransform& transform, Vector3 position) const;
        void updateActorTransform(
            entt::entity entity,
            sgTransform& transform,
            MoveableActor& moveableActor,
            float deltaTime,
            float speed) const;
        void centerTurnPivot(entt::entity entity, MoveableActor& moveableActor, sgTransform& transform) const;
        static void updateActorDirection(sgTransform& transform, const MoveableActor& moveableActor);
        [[nodiscard]] static bool updateActorRotation(
            sgTransform& transform, const MoveableActor& moveableActor, float deltaTime);

      public:
        Event<entt::entity, Vector3, PathfindFailureReason> onPathfindFailed{};

        [[nodiscard]] bool ReachedDestination(entt::entity entity) const;
        void PruneMoveCommands(const entt::entity& entity) const;
        [[nodiscard]] bool TryPathfindToLocation(
            const entt::entity& entity,
            const Vector3& destination,
            bool astar = false,
            bool findNextBestIfInvalid = true) const;
        [[nodiscard]] std::vector<Vector3> FindRouteToLocation(
            entt::entity entity,
            const Vector3& destination,
            bool astar = false,
            bool findNextBestIfInvalid = true) const;
        [[nodiscard]] bool SetRoute(entt::entity entity, std::span<const Vector3> route) const;
        void PathfindToLocation(
            const entt::entity& entity,
            const Vector3& destination,
            bool astar = false,
            bool findNextBestIfInvalid = true) const;
        void MoveToLocation(const entt::entity& entity, Vector3 location) const;
        void CancelMovement(const entt::entity& entity) const;
        void Update(float deltaTime = GetFrameTime(), float speed = 1.0f);
        void DrawDebug() const;
        ActorMovementSystem(entt::registry* _registry, NavigationGridSystem* navigationGrid);
    };

} // namespace sage

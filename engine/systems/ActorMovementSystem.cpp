#include "ActorMovementSystem.hpp"
#include "engine/Colors.hpp"
#include "engine/MathConstants.hpp"

#include "components/Collideable.hpp"
#include "components/MoveableActor.hpp"
#include "components/NavigationGridSquare.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "NavigationGridSystem.hpp"
#include "slib.hpp"

#include <algorithm>

namespace sage
{

    void ActorMovementSystem::PruneMoveCommands(const entt::entity& entity) const
    {
        releaseStoppedFootprint(entity);
        auto& actor = registry->get<MoveableActor>(entity);
        actor.path.clear();
        actor.isWalking = false;
        actor.needsStopPosition = true;
        actor.notifyOnArrival = false;
        actor.stopRetryTime = 0.0f;
    }

    void ActorMovementSystem::CancelMovement(const entt::entity& entity) const
    {
        registry->get<MoveableActor>(entity).ClearRoute(entity);
    }

    // TODO: If an object has a collideable, this is completely pointless, as it will inevitably use pathfinding if
    // it encounters any issues or collides with something. It would need to be updated differently, maybe the same
    // as the "non collideable" update. Moves to a location without pathfinding
    void ActorMovementSystem::MoveToLocation(const entt::entity& entity, Vector3 location) const
    {
        if (!registry->any_of<MoveableActor>(entity))
        {
            registry->emplace<MoveableActor>(entity);
        }
        const bool wasWalking = registry->get<MoveableActor>(entity).isWalking;
        PruneMoveCommands(entity);
        const auto& transform = registry->get<sgTransform>(entity);
        auto& moveableActor = registry->get<MoveableActor>(entity);
        moveableActor.path.emplace_back(transform.GetWorldPos());
        moveableActor.path.emplace_back(location);
        moveableActor.needsStopPosition = false;
        moveableActor.notifyOnArrival = true;
        moveableActor.isWalking = wasWalking;
        moveableActor.onStartMovement.Publish(entity);
    }

    bool ActorMovementSystem::TryPathfindToLocation(
        const entt::entity& entity,
        const Vector3& destination,
        const bool astar,
        const bool findNextBestIfInvalid) const
    {
        PathfindToLocation(entity, destination, astar, findNextBestIfInvalid);
        auto& moveable = registry->get<MoveableActor>(entity);
        return moveable.IsMoving();
    }

    ActorMovementSystem::RouteSearchResult ActorMovementSystem::findRouteToLocation(
        const entt::entity entity,
        const Vector3& destination,
        const bool astar,
        const bool findNextBestIfInvalid) const
    {
        if (!navigationGrid->CheckWithinGridBounds(destination))
            return {.failure = PathfindFailureReason::DestinationOutOfGrid};

        const auto& moveable = registry->get<MoveableActor>(entity);
        GridSquare minRange{};
        GridSquare maxRange{};
        if (!navigationGrid->GetPathfindRange(entity, moveable.pathfindingBounds, minRange, maxRange))
            return {.failure = PathfindFailureReason::ActorOutOfGrid};
        if (!navigationGrid->CheckWithinBounds(destination, minRange, maxRange))
            return {.failure = PathfindFailureReason::DestinationOutOfRange};

        const auto& actorTransform = registry->get<sgTransform>(entity);
        auto route = astar ? navigationGrid->AStarPathfind(
                                 entity,
                                 actorTransform.GetWorldPos(),
                                 destination,
                                 minRange,
                                 maxRange,
                                 AStarHeuristic::DEFAULT,
                                 findNextBestIfInvalid)
                           : navigationGrid->BFSPathfind(
                                 entity,
                                 actorTransform.GetWorldPos(),
                                 destination,
                                 minRange,
                                 maxRange,
                                 findNextBestIfInvalid);

        if (route.empty()) return {.failure = PathfindFailureReason::DestinationUnreachable};
        return {.route = std::move(route)};
    }

    std::vector<Vector3> ActorMovementSystem::FindRouteToLocation(
        const entt::entity entity,
        const Vector3& destination,
        const bool astar,
        const bool findNextBestIfInvalid) const
    {
        return findRouteToLocation(entity, destination, astar, findNextBestIfInvalid).route;
    }

    bool ActorMovementSystem::SetRoute(const entt::entity entity, const std::span<const Vector3> route) const
    {
        if (route.empty()) return false;

        auto& moveable = registry->get<MoveableActor>(entity);
        const bool wasMoving = moveable.IsMoving();
        const bool wasWalking = moveable.isWalking;
        if (wasMoving)
        {
            PruneMoveCommands(entity);
            moveable.onPathChanged.Publish(entity);
        }
        else releaseStoppedFootprint(entity);
        moveable.path.insert(moveable.path.end(), route.begin(), route.end());
        moveable.needsStopPosition = false;
        moveable.notifyOnArrival = true;
        moveable.stopRetryTime = 0.0f;
        updateActorDirection(registry->get<sgTransform>(entity), moveable);
        moveable.isWalking = wasWalking;
        moveable.onStartMovement.Publish(entity);
        return true;
    }

    void ActorMovementSystem::PathfindToLocation(
        const entt::entity& entity,
        const Vector3& destination,
        const bool astar,
        const bool findNextBestIfInvalid) const
    {
        auto& moveable = registry->get<MoveableActor>(entity);
        const bool wasWalking = moveable.isWalking;
        auto result = findRouteToLocation(entity, destination, astar, findNextBestIfInvalid);
        if (result.failure)
        {
            switch (*result.failure)
            {
            case PathfindFailureReason::DestinationOutOfGrid:
                TraceLog(LOG_TRACE, "Actor %u: destination is outside the grid", static_cast<unsigned>(entity));
                break;
            case PathfindFailureReason::ActorOutOfGrid:
                TraceLog(
                    LOG_TRACE, "Actor %u: current position is outside the grid", static_cast<unsigned>(entity));
                break;
            case PathfindFailureReason::DestinationOutOfRange:
                TraceLog(
                    LOG_TRACE,
                    "Actor %u: destination is outside pathfinding range",
                    static_cast<unsigned>(entity));
                break;
            case PathfindFailureReason::DestinationUnreachable:
                TraceLog(LOG_TRACE, "Actor %u: destination is unreachable", static_cast<unsigned>(entity));
                break;
            }
            moveable.onDestinationUnreachable.Publish(entity, destination);
            onPathfindFailed.Publish(entity, destination, *result.failure);
            return;
        }

        (void)SetRoute(entity, result.route);
        // A reroute while already walking should steer into the new path;
        // only an actor starting from rest must turn in place first.
        moveable.isWalking = wasWalking;
    }

    bool ActorMovementSystem::ReachedDestination(entt::entity entity) const
    {
        const auto& actor = registry->get<MoveableActor>(entity);
        return actor.path.empty() && !actor.needsStopPosition;
    }

    void ActorMovementSystem::DrawDebug() const
    {
        auto view = registry->view<MoveableActor, sgTransform>();
        for (auto& entity : view)
        {
            auto& actor = registry->get<MoveableActor>(entity);
            if (actor.path.empty()) continue;
            for (auto p : actor.path)
            {
                DrawCube({p.x, p.y + 1, p.z}, 1, 1, 1, sage::colors::GREEN_COLOR);
            }
        }
    }

    bool ActorMovementSystem::hasReachedNextPoint(
        const sgTransform& transform, const MoveableActor& moveableActor)
    {
        // Arrival tolerance is horizontal; grid height is applied when snapping to the waypoint.
        return Vector2Distance(
                   {moveableActor.path.front().x, moveableActor.path.front().z},
                   {transform.GetWorldPos().x, transform.GetWorldPos().z}) < 0.5f;
    }

    void ActorMovementSystem::handlePointReached(entt::entity entity, MoveableActor& moveableActor)
    {
        GridSquare index{};
        Vector3 position{};
        if (!navigationGrid->WorldToGridSpace(moveableActor.path.front(), index) ||
            !navigationGrid->GridToWorldSpace(index, position)) return;

        if (moveableActor.path.size() == 1 && registry->all_of<Collideable>(entity) &&
            !claimStoppingPosition(entity, position))
        {
            (void)rerouteToStoppingPosition(entity, moveableActor, moveableActor.path.back());
            return;
        }
        setActorPosition(entity, registry->get<sgTransform>(entity), position);
        moveableActor.path.pop_front();
        if (!moveableActor.path.empty()) return;

        moveableActor.isWalking = false;
        moveableActor.needsStopPosition = false;
        if (moveableActor.notifyOnArrival) moveableActor.onDestinationReached.Publish(entity);
    }

    void ActorMovementSystem::setActorPosition(
        const entt::entity entity, sgTransform& transform, const Vector3 position) const
    {
        transform.position.world = position;
        if (auto* collider = registry->try_get<Collideable>(entity))
            collider->worldBoundingBox = TransformAabbNoRotation(collider->localBoundingBox, transform.GetMatrixNoRot());
    }

    void ActorMovementSystem::releaseStoppedFootprint(const entt::entity entity) const
    {
        const auto found = stoppedFootprints.find(entity);
        if (found == stoppedFootprints.end()) return;
        navigationGrid->MarkSquareAreaOccupied(found->second, false, entity);
        stoppedFootprints.erase(found);
    }

    bool ActorMovementSystem::claimStoppingPosition(const entt::entity entity, const Vector3 position)
    {
        if (!navigationGrid->CheckEntityAreaUnoccupied(entity, position)) return false;
        const auto& transform = registry->get<sgTransform>(entity);
        const auto& collider = registry->get<Collideable>(entity);
        const auto bounds = TransformAabbNoRotation(collider.localBoundingBox, transform.GetMatrixNoRot());
        const auto offset = Vector3Subtract(position, transform.GetWorldPos());
        const BoundingBox stoppedBounds{Vector3Add(bounds.min, offset), Vector3Add(bounds.max, offset)};
        releaseStoppedFootprint(entity);
        navigationGrid->MarkSquareAreaOccupied(stoppedBounds, true, entity);
        stoppedFootprints[entity] = stoppedBounds;
        return true;
    }

    bool ActorMovementSystem::rerouteToStoppingPosition(
        const entt::entity entity, MoveableActor& actor, const Vector3 destination) const
    {
        auto result = findRouteToLocation(entity, destination, true, true);
        actor.isWalking = false;
        if (result.route.empty())
        {
            // Keep the pending destination, and retry without publishing a false arrival.
            actor.needsStopPosition = true;
            actor.stopRetryTime = 0.25f;
            return false;
        }
        actor.path.assign(result.route.begin(), result.route.end());
        actor.needsStopPosition = false;
        return true;
    }

    void ActorMovementSystem::updateActorDirection(sgTransform& transform, const MoveableActor& moveableActor)
    {
        if (moveableActor.path.empty()) return;
        transform.direction =
            Vector3Normalize(Vector3Subtract(moveableActor.path.front(), transform.GetWorldPos()));
    }

    void ActorMovementSystem::centerTurnPivot(
        const entt::entity entity, MoveableActor& moveableActor, sgTransform& transform) const
    {
        if (moveableActor.hasCenteredTurnPivot) return;
        moveableActor.hasCenteredTurnPivot = true;

        BoundingBox localBounds{};
        bool hasBounds = false;
        Renderable* renderable = registry->try_get<Renderable>(entity);
        if (renderable != nullptr)
        {
            if (const auto* model = renderable->GetModel(); model != nullptr)
            {
                localBounds = model->CalcLocalBoundingBox();
                hasBounds = true;
            }
        }
        if (!hasBounds)
        {
            if (const auto* collideable = registry->try_get<Collideable>(entity); collideable != nullptr)
            {
                localBounds = collideable->localBoundingBox;
                hasBounds = true;
            }
        }
        if (!hasBounds) return;

        const Vector3 localPivot = {
            (localBounds.min.x + localBounds.max.x) * 0.5f,
            0.0f,
            (localBounds.min.z + localBounds.max.z) * 0.5f};
        if (fabsf(localPivot.x) < 0.0001f && fabsf(localPivot.z) < 0.0001f) return;

        // Move the entity origin to the existing world-space pivot, then offset
        // its model and collider back by the same local amount. The actor does
        // not visibly move, but subsequent yaw now occurs around its centre.
        const Vector3 pivotWorld = Vector3Transform(localPivot, transform.GetMatrix());
        const Vector3 currentPosition = transform.GetWorldPos();
        transform.position.world = {
            pivotWorld.x,
            currentPosition.y,
            pivotWorld.z};

        const Matrix pivotOffset = MatrixTranslate(-localPivot.x, 0.0f, -localPivot.z);
        if (renderable != nullptr)
        {
            if (auto* model = renderable->GetModel(); model != nullptr)
            {
                const Matrix centeredTransform = MatrixMultiply(model->GetTransform(), pivotOffset);
                model->SetTransform(centeredTransform);
                renderable->initialTransform = centeredTransform;
            }
        }

        if (auto* collideable = registry->try_get<Collideable>(entity); collideable != nullptr)
        {
            collideable->localBoundingBox.min.x -= localPivot.x;
            collideable->localBoundingBox.max.x -= localPivot.x;
            collideable->localBoundingBox.min.z -= localPivot.z;
            collideable->localBoundingBox.max.z -= localPivot.z;
        }
    }

    bool ActorMovementSystem::updateActorRotation(
        sgTransform& transform, const MoveableActor& moveableActor, const float deltaTime)
    {
        const float target = atan2f(transform.direction.x, transform.direction.z) * sage::math::RADIANS_TO_DEGREES;
        const Vector3 currentRotation = transform.GetWorldRot();
        float angle = target;
        if (moveableActor.turnSpeed > 0.0f)
        {
            // Shortest signed angular difference, mapped into [-180, 180).
            const float delta = fmodf(target - currentRotation.y + 540.0f, 360.0f) - 180.0f;
            constexpr float facingTolerance = 0.01f;
            if (fabsf(delta) <= facingTolerance)
            {
                transform.rotation.world = {currentRotation.x, target, currentRotation.z};
                return true;
            }

            const float maxStep = moveableActor.turnSpeed * deltaTime;
            angle = currentRotation.y + Clamp(delta, -maxStep, maxStep);
        }
        transform.rotation.world = {currentRotation.x, angle, currentRotation.z};
        // If this update contained any gradual rotation, remain stationary until
        // the following update observes that the actor is fully facing the path.
        return moveableActor.turnSpeed <= 0.0f;
    }

    void ActorMovementSystem::updateActorTransform(
        entt::entity entity,
        sgTransform& transform,
        MoveableActor& moveableActor,
        const float deltaTime,
        const float speed) const
    {
        updateActorDirection(transform, moveableActor);
        const bool isFacingMovementDirection = updateActorRotation(transform, moveableActor, deltaTime);
        if (!moveableActor.isWalking && !isFacingMovementDirection) return;
        moveableActor.isWalking = true;

        GridSquare actorIndex{};
        navigationGrid->WorldToGridSpace(transform.GetWorldPos(), actorIndex);
        const auto* gridSquare = navigationGrid->GetGridSquare(actorIndex.row, actorIndex.col);
        const Vector3 currentPosition = transform.GetWorldPos();
        const Vector3 nextPoint = moveableActor.path.front();
        const float distance = Vector2Distance(
            {currentPosition.x, currentPosition.z}, {nextPoint.x, nextPoint.z});
        const float step = std::min(moveableActor.movementSpeed * speed, distance);
        setActorPosition(entity, transform, {
            currentPosition.x + transform.direction.x * step,
            gridSquare->heightMap.GetHeight(),
            currentPosition.z + transform.direction.z * step});
    }

    void ActorMovementSystem::updateActor(
        entt::entity entity,
        MoveableActor& moveableActor,
        sgTransform& transform,
        const float deltaTime,
        const float speed)
    {
        if (moveableActor.stopRetryTime > 0.0f) return;
        const bool hasCollider = registry->all_of<Collideable>(entity);
        if (moveableActor.path.empty())
        {
            if (!hasCollider || !moveableActor.needsStopPosition) return;
            // Cancellation/spawning in occupied space: move clear without completing an old activity.
            moveableActor.notifyOnArrival = false;
            if (!rerouteToStoppingPosition(entity, moveableActor, transform.GetWorldPos())) return;
        }

        if (hasCollider && !navigationGrid->CheckEntityAreaUnoccupied(entity, moveableActor.path.front(), true))
        {
            (void)rerouteToStoppingPosition(entity, moveableActor, moveableActor.GetDestination());
            return;
        }

        if (hasReachedNextPoint(transform, moveableActor))
        {
            handlePointReached(entity, moveableActor);
            // Arrival callbacks can cancel, replace the route, or remove the actor.
            return;
        }

        updateActorTransform(entity, transform, moveableActor, deltaTime, speed);
    }

    void ActorMovementSystem::Update(const float deltaTime, const float speed)
    {
        auto fullView = registry->view<MoveableActor, sgTransform, Collideable>();
        for (auto [entity, actor, transform, collider] : fullView.each())
        {
            centerTurnPivot(entity, actor, transform);
            collider.worldBoundingBox = TransformAabbNoRotation(collider.localBoundingBox, transform.GetMatrixNoRot());
            actor.stopRetryTime = std::max(0.0f, actor.stopRetryTime - deltaTime);
        }

        // Keep existing stopped actors' claims, so a cancellation or spawn cannot
        // take their space. Release only removed, teleported, or moving owners.
        for (auto it = stoppedFootprints.begin(); it != stoppedFootprints.end();)
        {
            const auto entity = it->first;
            const auto* actor = registry->try_get<MoveableActor>(entity);
            const auto* collider = registry->try_get<Collideable>(entity);
            if (!actor || !collider || !registry->all_of<sgTransform>(entity) || !actor->path.empty() ||
                !Vector3Equals(it->second.min, collider->worldBoundingBox.min) ||
                !Vector3Equals(it->second.max, collider->worldBoundingBox.max))
            {
                navigationGrid->MarkSquareAreaOccupied(it->second, false, entity);
                it = stoppedFootprints.erase(it);
            }
            else ++it;
        }

        for (auto [entity, actor, transform, collider] : fullView.each())
        {
            if (actor.path.empty())
            {
                actor.needsStopPosition = !claimStoppingPosition(entity, transform.GetWorldPos());
                if (actor.needsStopPosition) releaseStoppedFootprint(entity);
            }
        }

        // All existing stopped actors are marked before any travelling actor can arrive.
        for (auto [entity, actor, transform, collider] : fullView.each())
            updateActor(entity, actor, transform, deltaTime, speed);

        // Entities without collision footprints retain unrestricted movement.
        auto partialView = registry->view<MoveableActor, sgTransform>(entt::exclude<Collideable>);
        for (auto [entity, actor, transform] : partialView.each())
        {
            centerTurnPivot(entity, actor, transform);
            actor.needsStopPosition = false;
            actor.stopRetryTime = 0.0f;
            updateActor(entity, actor, transform, deltaTime, speed);
        }
    }

    ActorMovementSystem::ActorMovementSystem(entt::registry* _registry, NavigationGridSystem* _navigationGrid)
        : navigationGrid(_navigationGrid), registry(_registry)
    {
    }

} // namespace sage

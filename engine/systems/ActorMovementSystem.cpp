#include "ActorMovementSystem.hpp"

#include "CollisionSystem.hpp"
#include "components/MoveableActor.hpp"
#include "components/NavigationGridSquare.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "EngineSystems.hpp"
#include "NavigationGridSystem.hpp"
#include "Serializer.hpp"
#include "slib.hpp"
#include "ScriptSystem.hpp"
#include "TransformSystem.hpp"
#include "sol/sol.hpp"

#include <format>
#include <ranges>
#include <tuple>

namespace sage
{

    void ActorMovementSystem::PruneMoveCommands(const entt::entity& entity) const
    {
        auto& actor = registry->get<MoveableActor>(entity);
        std::deque<Vector3> empty;
        std::swap(actor.path, empty);
        actor.isWalking = false;
    }

    void ActorMovementSystem::CancelMovement(const entt::entity& entity) const
    {
        PruneMoveCommands(entity);
        auto& moveable = registry->get<MoveableActor>(entity);
        moveable.onMovementCancel.Publish(entity);
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

    void ActorMovementSystem::PathfindToLocation(
        const entt::entity& entity,
        const Vector3& destination,
        const bool astar,
        const bool findNextBestIfInvalid) const
    {
        auto& moveable = registry->get<MoveableActor>(entity);
        const bool wasWalking = moveable.isWalking;

        if (!sys->navigationGridSystem->CheckWithinGridBounds(destination))
        {
            moveable.onDestinationUnreachable.Publish(entity, destination);
            onPathfindFailed.Publish(entity, destination, PathfindFailureReason::DestinationOutOfGrid);
            TraceLog(LOG_TRACE, "Actor %u: destination is outside the grid", static_cast<unsigned>(entity));
            return;
        }

        GridSquare minRange{};
        GridSquare maxRange{};
        if (!sys->navigationGridSystem->GetPathfindRange(entity, moveable.pathfindingBounds, minRange, maxRange))
        {
            TraceLog(LOG_TRACE, "Actor %u: current position is outside the grid", static_cast<unsigned>(entity));
            moveable.onDestinationUnreachable.Publish(entity, destination);
            onPathfindFailed.Publish(entity, destination, PathfindFailureReason::ActorOutOfGrid);
            return;
        }

        if (!sys->navigationGridSystem->CheckWithinBounds(destination, minRange, maxRange))
        {
            TraceLog(LOG_TRACE, "Actor %u: destination is outside pathfinding range", static_cast<unsigned>(entity));
            moveable.onDestinationUnreachable.Publish(entity, destination);
            onPathfindFailed.Publish(entity, destination, PathfindFailureReason::DestinationOutOfRange);
            return;
        }

        const auto& collideable = registry->get<Collideable>(entity);
        sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, false, entity);

        const auto& actorTrans = registry->get<sgTransform>(entity);
        std::vector<Vector3> path;
        if (astar)
        {
            path = sys->navigationGridSystem->AStarPathfind(
                entity,
                actorTrans.GetWorldPos(),
                destination,
                minRange,
                maxRange,
                AStarHeuristic::DEFAULT,
                findNextBestIfInvalid);
        }
        else
        {
            path = sys->navigationGridSystem->BFSPathfind(
                entity,
                actorTrans.GetWorldPos(),
                destination,
                minRange,
                maxRange,
                findNextBestIfInvalid);
        }

        if (moveable.IsMoving()) // Was previously moving
        {
            PruneMoveCommands(entity);
            moveable.onPathChanged.Publish(entity);
        }

        for (auto n : path)
        {
            moveable.path.emplace_back(n);
        }

        if (!path.empty())
        {
            auto& transform = registry->get<sgTransform>(entity);
            updateActorDirection(transform, moveable);
            // A reroute while already walking should steer into the new path;
            // only an actor starting from rest must turn in place first.
            moveable.isWalking = wasWalking;
            moveable.onStartMovement.Publish(entity);
        }
        else
        {
            TraceLog(LOG_TRACE, "Actor %u: destination is unreachable", static_cast<unsigned>(entity));
            moveable.onDestinationUnreachable.Publish(entity, destination);
            onPathfindFailed.Publish(entity, destination, PathfindFailureReason::DestinationUnreachable);
        }

        sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);
    }

    bool ActorMovementSystem::ReachedDestination(entt::entity entity) const
    {
        const auto& actor = registry->get<MoveableActor>(entity);
        return actor.path.empty();
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
                DrawCube({p.x, p.y + 1, p.z}, 1, 1, 1, GREEN);
            }
        }

        for (auto& ray : debugRays)
        {
            DrawLine3D(ray.position, Vector3Add(ray.position, Vector3Multiply(ray.direction, {5, 1, 5})), RED);
        }

        for (auto& col : debugCollisions)
        {
            DrawSphere(col.point, 1.5, GREEN);
        }
    }

    void ActorMovementSystem::clearDebugData()
    {
        debugRays.erase(debugRays.begin(), debugRays.end());
        debugCollisions.erase(debugCollisions.begin(), debugCollisions.end());
    }

    bool ActorMovementSystem::isNextPointOccupied(
        const entt::entity entity, const MoveableActor& moveableActor) const
    {
        return !sys->navigationGridSystem->CheckEntityAreaUnoccupied(entity, moveableActor.path.front());
    }

    void ActorMovementSystem::recalculatePath(
        const entt::entity entity, const MoveableActor& moveableActor) const
    {
        PathfindToLocation(entity, moveableActor.GetDestination());
    }

    bool ActorMovementSystem::hasReachedNextPoint(entt::entity entity, const MoveableActor& moveableActor) const
    {
        const auto& transform = registry->get<sgTransform>(entity);
        // I do not believe that height should matter for this (could be very wrong)
        return Vector2Distance(
                   {moveableActor.path.front().x, moveableActor.path.front().z},
                   {transform.GetWorldPos().x, transform.GetWorldPos().z}) < 0.5f;
    }

    void ActorMovementSystem::handlePointReached(entt::entity entity, MoveableActor& moveableActor)
    {
        setPositionToGridCenter(entity, moveableActor);
        moveableActor.path.pop_front();

        if (moveableActor.path.empty())
        {
            handleDestinationReached(entity, moveableActor);
        }
    }

    void ActorMovementSystem::setPositionToGridCenter(
        entt::entity entity, const MoveableActor& moveableActor) const
    {
        // Set continuous pos to grid/discrete pos
        GridSquare targetGridPos{};
        sys->navigationGridSystem->WorldToGridSpace(moveableActor.path.front(), targetGridPos);
        const auto square = sys->navigationGridSystem->GetGridSquare(targetGridPos.row, targetGridPos.col);
        registry->get<sgTransform>(entity).position.world =
            {square->worldPosCentre.x, square->heightMap.GetHeight(), square->worldPosCentre.z};
    }

    void ActorMovementSystem::handleDestinationReached(const entt::entity entity, MoveableActor& moveableActor)
    {
        moveableActor.isWalking = false;
        moveableActor.onDestinationReached.Publish(entity);
    }

    bool ActorMovementSystem::CheckCollisionWithOtherMoveable(
        const entt::entity entity, const sgTransform& transform, MoveableActor& moveableActor) const
    {
        constexpr float avoidanceDistance = 10;
        GridSquare actorIndex{};
        sys->navigationGridSystem->WorldToGridSpace(transform.GetWorldPos(), actorIndex);

        // navigationGridSystem->MarkSquaresDebug(moveableActor.debugRay, PURPLE, false);

        NavigationGridSquare* hitCell =
            castCollisionRay(actorIndex, transform.direction, avoidanceDistance, moveableActor);

        // If we haven't hit anything, or the object is static, then we don't need to worry about it.
        if (hitCell == nullptr || !registry->any_of<MoveableActor>(hitCell->occupant)) return false;

        const auto& hitTransform = registry->get<sgTransform>(hitCell->occupant);

        // Going same direction, ignore.
        auto dot = Vector3DotProduct(transform.direction, hitTransform.direction);
        if (dot >= 0)
        {
            return false;
        }

        if (registry->any_of<Collideable>(hitCell->occupant) &&
            (!moveableActor.movementCollisionTarget.has_value() ||
             hitCell->occupant != moveableActor.movementCollisionTarget.value()) &&
            moveableActor.hitEntityId != entity)
        {
            if (!AlmostEquals(hitTransform.GetWorldPos(), moveableActor.hitLastPos))
            {
                moveableActor.hitEntityId = hitCell->occupant;
                moveableActor.hitLastPos = hitTransform.GetWorldPos();

                auto& hitCol = registry->get<Collideable>(hitCell->occupant);

                if (Vector3Distance(hitTransform.GetWorldPos(), transform.GetWorldPos()) <
                    Vector3Distance(moveableActor.path.back(), transform.GetWorldPos()))
                {
                    TraceLog(LOG_TRACE, "Actor %u: moving obstacle detected; rerouting", static_cast<unsigned>(entity));
                    PathfindToLocation(entity, moveableActor.GetDestination());
                    hitCol.debugDraw = true;
                    return true;
                }
            }
        }
        return false;
    }

    NavigationGridSquare* ActorMovementSystem::castCollisionRay(
        const GridSquare& actorIndex, const Vector3& direction, float distance, MoveableActor& moveableActor) const
    {
        return sys->navigationGridSystem->CastRay(
            actorIndex.row, actorIndex.col, {direction.x, direction.z}, distance, moveableActor.debugRay);
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

    bool ActorMovementSystem::updateActorRotation(sgTransform& transform, const MoveableActor& moveableActor)
    {
        const float target = atan2f(transform.direction.x, transform.direction.z) * RAD2DEG;
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

            const float maxStep = moveableActor.turnSpeed * GetFrameTime();
            angle = currentRotation.y + Clamp(delta, -maxStep, maxStep);
        }
        transform.rotation.world = {currentRotation.x, angle, currentRotation.z};
        // If this update contained any gradual rotation, remain stationary until
        // the following update observes that the actor is fully facing the path.
        return moveableActor.turnSpeed <= 0.0f;
    }

    void ActorMovementSystem::updateActorWorldPosition(entt::entity entity) const
    {
        GridSquare actorIndex{};
        auto& transform = registry->get<sgTransform>(entity);
        sys->navigationGridSystem->WorldToGridSpace(transform.GetWorldPos(), actorIndex);
        auto gridSquare = sys->navigationGridSystem->GetGridSquare(actorIndex.row, actorIndex.col);
        auto& moveable = registry->get<MoveableActor>(entity);
        Vector3 newPos = {
            transform.GetWorldPos().x + transform.direction.x * moveable.movementSpeed,
            gridSquare->heightMap.GetHeight(),
            transform.GetWorldPos().z + transform.direction.z * moveable.movementSpeed};
        transform.position.world = newPos;
    }

    void ActorMovementSystem::updateActorTransform(
        entt::entity entity, sgTransform& transform, MoveableActor& moveableActor) const
    {
        updateActorDirection(transform, moveableActor);
        const bool isFacingMovementDirection = updateActorRotation(transform, moveableActor);
        if (moveableActor.isWalking || isFacingMovementDirection)
        {
            moveableActor.isWalking = true;
            updateActorWorldPosition(entity);
        }
    }

    void ActorMovementSystem::updateActor(
        entt::entity entity, MoveableActor& moveableActor, sgTransform& transform, Collideable* collideable)
    {
        if (moveableActor.path.empty())
        {
            return;
        }

        if (hasReachedNextPoint(entity, moveableActor))
        {
            handlePointReached(entity, moveableActor);
            if (moveableActor.path.empty()) return;
        }

        if (collideable != nullptr && isNextPointOccupied(entity, moveableActor))
        {
            TraceLog(LOG_TRACE, "Actor %u: next point occupied; rerouting", static_cast<unsigned>(entity));
            recalculatePath(entity, moveableActor);
            return;
        }

        if (collideable == nullptr || !CheckCollisionWithOtherMoveable(entity, transform, moveableActor))
        {
            // TODO: Distance of the ray cast should be from the current pos to the next node
            updateActorTransform(entity, transform, moveableActor);
        }
    }

    void ActorMovementSystem::Update()
    {
        clearDebugData();

        auto fullView = registry->view<MoveableActor, sgTransform, Collideable>();
        for (auto [entity, moveableActor, transform, collideable] : fullView.each())
        {
            centerTurnPivot(entity, moveableActor, transform);
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, false, entity);
            updateActor(entity, moveableActor, transform, &collideable);
            // updateActor mutated the transform; refresh the world bbox so the re-mark
            // uses the post-move position (CollisionSystem::Update only runs once per frame).
            collideable.worldBoundingBox =
                TransformAabbNoRotation(collideable.localBoundingBox, transform.GetMatrixNoRot());
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, true, entity);
        }

        // Process entities without Collideable component (e.g., some abilities etc)
        auto partialView = registry->view<MoveableActor, sgTransform>(entt::exclude<Collideable>);
        for (auto [entity, moveableActor, transform] : partialView.each())
        {
            centerTurnPivot(entity, moveableActor, transform);
            updateActor(entity, moveableActor, transform);
        }
    }

    ActorMovementSystem::ActorMovementSystem(entt::registry* _registry, EngineSystems* _sys)
        : registry(_registry), sys(_sys)
    {
    }

    void ActorMovementSystem::RegisterLuaBindings(ScriptSystem& scripts)
    {
        const auto registerMovementEvent = [this, &scripts](
                                               const std::string& name,
                                               Event<entt::entity> MoveableActor::* event) {
            scripts.RegisterEventLuaBinding<MoveableActor>(
                name, [this, event](const entt::entity source, const LuaEventCallback& callback) {
                    auto* moveable = registry->try_get<MoveableActor>(source);
                    if (moveable == nullptr) return Subscription{};
                    return (moveable->*event).Subscribe([callback](const entt::entity /*actor*/) { callback(); });
                });
        };

        registerMovementEvent("MovementStarted", &MoveableActor::onStartMovement);
        registerMovementEvent("DestinationReached", &MoveableActor::onDestinationReached);
        registerMovementEvent("MovementCancelled", &MoveableActor::onMovementCancel);
        registerMovementEvent("PathChanged", &MoveableActor::onPathChanged);

        scripts.RegisterEventLuaBinding<MoveableActor>(
            "DestinationUnreachable", [this](const entt::entity source, const LuaEventCallback& callback) {
                auto* moveable = registry->try_get<MoveableActor>(source);
                if (moveable == nullptr) return Subscription{};
                return moveable->onDestinationUnreachable.Subscribe(
                    [callback](const entt::entity /*actor*/, const Vector3 destination) {
                        callback(destination);
                    });
            });

        scripts.RegisterApiExtension(
            "sage", [this](sol::table& api, const entt::entity owner, entt::registry& registry) {
                api.set_function("MoveToLocation", [this, owner, &registry](const Vector3& destination) {
                    if (!registry.all_of<sgTransform>(owner)) return false;
                    MoveToLocation(owner, destination);
                    return true;
                });

                api.set_function("HasRoute", [owner, &registry] {
                    const auto* moveable = registry.try_get<MoveableActor>(owner);
                    return moveable != nullptr && moveable->IsMoving();
                });

                api.set_function(
                    "TryPathfindToLocation",
                    sol::overload(
                        [this, owner, &registry](const Vector3& destination) {
                            if (!registry.all_of<sgTransform, MoveableActor, Collideable>(owner)) return false;
                            return TryPathfindToLocation(owner, destination);
                        },
                        [this, owner, &registry](const Vector3& destination, const bool astar) {
                            if (!registry.all_of<sgTransform, MoveableActor, Collideable>(owner)) return false;
                            return TryPathfindToLocation(owner, destination, astar);
                        },
                        [this, owner, &registry](
                            const Vector3& destination, const bool astar, const bool findNextBestIfInvalid) {
                            if (!registry.all_of<sgTransform, MoveableActor, Collideable>(owner)) return false;
                            return TryPathfindToLocation(owner, destination, astar, findNextBestIfInvalid);
                        }));
            });
    }
} // namespace sage

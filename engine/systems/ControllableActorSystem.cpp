//
// Created by Steve Wheeler on 11/06/2026.
//
#include "systems/ControllableActorSystem.hpp"

#include "components/Collideable.hpp"
#include "components/MoveableActor.hpp"
#include "components/sgTransform.hpp"
#include "Cursor.hpp"
#include "EngineSystems.hpp"
#include "systems/ActorMovementSystem.hpp"

namespace sage
{
    void ControllableActorSystem::onNavigationClick() const
    {
        if (selectedActor == entt::null || !registry->valid(selectedActor)) return;
        if (!registry->all_of<MoveableActor, sgTransform, Collideable>(selectedActor)) return;

        sys->actorMovementSystem->PathfindToLocation(selectedActor, sys->cursor->getFirstNaviCollision().point);
    }

    void ControllableActorSystem::SetSelectedActor(const entt::entity entity)
    {
        selectedActor = entity;
    }

    entt::entity ControllableActorSystem::GetSelectedActor() const
    {
        return selectedActor;
    }

    ControllableActorSystem::ControllableActorSystem(entt::registry* _registry, EngineSystems* _sys)
        : registry(_registry), sys(_sys)
    {
        navigationClickSub = sys->cursor->onNavigationClick.Subscribe(
            [this](entt::entity, CollisionLayer) { onNavigationClick(); });
    }
} // namespace sage

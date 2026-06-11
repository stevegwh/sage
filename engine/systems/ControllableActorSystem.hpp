//
// Created by Steve Wheeler on 11/06/2026.
//
#pragma once

#include "engine/Event.hpp"

#include "entt/entt.hpp"

namespace sage
{
    class EngineSystems;

    // Holds the player-controlled actor and routes cursor navigation clicks to
    // ActorMovementSystem. Selection is opt-in: nothing happens until the game
    // calls SetSelectedActor.
    class ControllableActorSystem
    {
        entt::registry* registry;
        EngineSystems* sys;
        entt::entity selectedActor = entt::null;
        Subscription navigationClickSub{};

        void onNavigationClick() const;

      public:
        void SetSelectedActor(entt::entity entity);
        [[nodiscard]] entt::entity GetSelectedActor() const;

        ControllableActorSystem(entt::registry* _registry, EngineSystems* _sys);
    };
} // namespace sage

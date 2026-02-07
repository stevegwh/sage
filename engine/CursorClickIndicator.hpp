//
// Created by Steve Wheeler on 06/12/2024.
//

#pragma once

#include "engine/Event.hpp"

#include "entt/entt.hpp"

namespace sage
{
    class EngineSystems;
    class CursorClickIndicator
    {
        entt::registry* registry;
        EngineSystems* sys;
        entt::entity self;
        float k = 0;

        Subscription destinationReachedSub{};

        void onCursorClick(entt::entity entity) const;
        void disableIndicator() const;
        void onSelectedActorChanged(entt::entity, entt::entity current);

      public:
        void Update();
        CursorClickIndicator(entt::registry* _registry, EngineSystems* _sys);
    };

} // namespace sage

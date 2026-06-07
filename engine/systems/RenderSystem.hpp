//
// Created by Steve Wheeler on 21/02/2024.
//

#pragma once

#include "entt/entt.hpp"

namespace sage
{
    class RenderSystem
    {
        entt::registry* registry;

      public:
        void Update();
        void Draw();
        explicit RenderSystem(entt::registry* _registry);
    };
} // namespace sage

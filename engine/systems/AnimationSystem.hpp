//
// Created by Steve Wheeler on 06/04/2024.
//

#pragma once

#include "entt/entt.hpp"

namespace sage
{
    class ScriptSystem;

    class AnimationSystem
    {
        entt::registry* registry;

      public:
        void Update() const;
        void Draw();
        void RegisterLuaBindings(ScriptSystem& scripts);
        explicit AnimationSystem(entt::registry* _registry);
    };
} // namespace sage

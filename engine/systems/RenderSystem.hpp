//
// Created by Steve Wheeler on 21/02/2024.
//

#pragma once

#include "entt/entt.hpp"

#include <memory>
#include <string>

namespace sage
{
    class Skybox;

    class RenderSystem
    {
        entt::registry* registry;
        std::unique_ptr<Skybox> skybox;

      public:
        void Update();
        void Draw();
        void SetSkybox(const std::string& imageKey);
        void ClearSkybox();
        explicit RenderSystem(entt::registry* _registry);
        ~RenderSystem();
    };
} // namespace sage

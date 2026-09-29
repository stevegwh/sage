//
// Created by Steve Wheeler on 21/02/2024.
//

#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

#include <memory>
#include <string>

namespace sage
{
    class Skybox;
    class LightManager;

    class RenderSystem
    {
        entt::registry* registry;
        LightManager* lightManager;
        std::unique_ptr<Skybox> skybox;

      public:
        void Update();
        void Draw();
        void DrawShadowCasters(Shader shader, int skinnedLocation) const;
        void SetSkybox(const std::string& imageKey);
        void ClearSkybox();
        explicit RenderSystem(entt::registry* _registry, LightManager* _lightManager);
        ~RenderSystem();
    };
} // namespace sage

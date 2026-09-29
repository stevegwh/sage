#pragma once

#include "Light.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

namespace sage
{
    class Camera;
    class RenderSystem;
    struct LightSettings;

    class LightManager
    {
        static constexpr int MAX_LIGHT_COUNT = 50; // Must match the shader light array.
        entt::registry* registry;
        Camera* camera;
        Shader defaultShader{};
        std::vector<Shader> shaders;
        int lightsCount = 0;
        float gamma = 1.9;
        std::array<float, 4> ambient{};
        Shader shadowShader{};
        unsigned int shadowFramebuffer = 0;
        unsigned int shadowCubemap = 0;
        int shadowLightIndex = -1;
        Shader sunShadowShader{};
        unsigned int sunShadowFramebuffer = 0;
        unsigned int sunShadowTexture = 0;
        int sunShadowLightIndex = -1;
        Matrix sunLightMatrix{};
        void updateShaderLights(Shader& _shader);
        void updateAmbientLight(Shader& _shader) const;
        void onLightAdded(entt::entity entity);
        void drawPointShadowMap(const RenderSystem& renderer, const Light& light);
        void drawSunShadowMap(const RenderSystem& renderer, const Light& sun);

      public:
        void RemoveLight(entt::entity light);
        entt::entity CreateLight(
            LightType type,
            Vector3 position,
            Vector3 target,
            Color color,
            float intensity); // Create a light and get shader locations
        void LinkShaderToLights(Shader& _shader);
        void ApplyLightSettings(const LightSettings& settings);
        void RefreshLights();
        void LinkRenderableToLight(entt::entity entity) const;
        void DrawDebugLights() const;
        void Update() const;
        void DrawShadowMap(const RenderSystem& renderer);
        void BindShadowMap() const;
        void UnbindShadowMap() const;
        explicit LightManager(entt::registry* _registry, Camera* _camera, const LightSettings& settings);
        ~LightManager();
    };
} // namespace sage

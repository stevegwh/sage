#pragma once

#include "Light.hpp"

#include "entt/entt.hpp"
#include "raylib.h"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

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
        struct ShaderState
        {
            Shader shader;
            std::vector<LightShaderLocations> lightLocations;
            int ambientLocation;
            int gammaLocation;
            int lightsCountLocation;
            int pointShadowIndexLocation;
            int sunShadowIndexLocation;
            int sunMatrixLocation;
            int bloomLocation;
            std::optional<std::array<float, 5>> ambientValues;
            std::optional<std::vector<Light>> lightValues;
            std::optional<std::pair<int, int>> shadowIndices;
            std::optional<std::array<float, 16>> sunMatrix;
            std::optional<std::array<float, 3>> cameraPosition;

            explicit ShaderState(Shader source);
        };
        std::vector<std::shared_ptr<ShaderState>> shaders;
        std::vector<Light> activeLights;
        int lightsCount = 0;
        float gamma = 1.9;
        std::array<float, 4> ambient{};
        Shader shadowShader{};
        int shadowPositionLocation = -1;
        int shadowSkinnedLocation = -1;
        unsigned int shadowFramebuffer = 0;
        unsigned int shadowCubemap = 0;
        int shadowLightIndex = -1;
        Shader sunShadowShader{};
        int sunSkinnedLocation = -1;
        unsigned int sunShadowFramebuffer = 0;
        unsigned int sunShadowTexture = 0;
        int sunShadowLightIndex = -1;
        bool shadowsEnabled = true;
        Matrix sunLightMatrix{};
        void updateShaderLights(ShaderState& state);
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
        void Update();
        void DrawShadowMap(const RenderSystem& renderer);
        void SetShadowsEnabled(bool enabled)
        {
            shadowsEnabled = enabled;
        }
        void BindShadowMap() const;
        void UnbindShadowMap() const;
        void SetBloomMask(bool enabled) const;
        explicit LightManager(
            entt::registry* _registry, Camera* _camera, const LightSettings& settings, bool shadowsEnabled);
        LightManager(const LightManager&) = delete;
        LightManager& operator=(const LightManager&) = delete;
        LightManager(LightManager&&) = delete;
        LightManager& operator=(LightManager&&) = delete;
        ~LightManager();
    };
} // namespace sage

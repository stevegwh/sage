#pragma once

#include "engine/ParticleSystem.hpp"
#include "entt/entt.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace sage
{
    class ParticleEmitterSystem
    {
        struct Instance
        {
            std::unique_ptr<Emitter> emitter;
            std::string texture;
            float elapsed = 0.0f;
            bool paused = false;

            void Reset(const EmitterConfig& config, const std::string& texturePath, bool play)
            {
                emitter = std::make_unique<Emitter>(config);
                texture = texturePath;
                elapsed = 0.0f;
                if (play) emitter->Start();
            }
        };

        entt::registry& registry;
        std::unordered_map<entt::entity, Instance> instances;

      public:
        explicit ParticleEmitterSystem(entt::registry& registry) : registry(registry) {}
        void Update(float dt);
        void Draw(Camera3D& camera) const;
        void Play(entt::entity entity);
        void Pause(entt::entity entity);
        void Burst(entt::entity entity);
        void Restart(entt::entity entity);
        [[nodiscard]] std::size_t Alive(entt::entity entity) const;
    };
} // namespace sage

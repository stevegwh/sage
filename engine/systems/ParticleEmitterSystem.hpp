#pragma once

#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/ParticleSystem.hpp"
#include "entt/entt.hpp"
#include <functional>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace sage
{
    EmitterConfig MakeParticleEmitterConfig(const ParticleEmitterComponent& settings, Vector3 origin);

    // Hierarchy order is shared by effect playback and editor previews.
    [[nodiscard]] std::vector<entt::entity> GatherParticleEmitters(
        const entt::registry& registry, entt::entity root);
    [[nodiscard]] entt::entity ParticleEffectRoot(const entt::registry& registry, entt::entity entity);

    class ParticleEmitterSystem
    {
        struct Instance
        {
            std::unique_ptr<Emitter> emitter;
            std::optional<ParticleEmitterComponent> settings;
            float elapsed = 0.0f;
            bool paused = false;

            void Reset(const EmitterConfig& config, const ParticleEmitterComponent& appliedSettings, bool play)
            {
                emitter = std::make_unique<Emitter>(config);
                settings = appliedSettings;
                elapsed = 0.0f;
                paused = false;
                if (play) emitter->Start();
            }
        };

        std::reference_wrapper<entt::registry> registry;
        std::unordered_map<entt::entity, Instance> instances;

      public:
        explicit ParticleEmitterSystem(entt::registry& registry) : registry(registry)
        {
        }
        // A supplied entity list restricts playback and clears instances outside that list.
        void Update(float dt, std::optional<std::span<const entt::entity>> activeEntities = std::nullopt);
        void Draw(Camera3D& camera) const;
        void Play(entt::entity entity, bool withChildren = true);
        void Pause(entt::entity entity, bool withChildren = true);
        void Burst(entt::entity entity, bool withChildren = true);
        void Restart(entt::entity entity, bool withChildren = true);
        [[nodiscard]] std::size_t Alive(entt::entity entity, bool withChildren = true) const;
    };
} // namespace sage

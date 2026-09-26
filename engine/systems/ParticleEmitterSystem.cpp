#include "ParticleEmitterSystem.hpp"

#include "engine/ResourceManager.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/sgTransform.hpp"

#include <algorithm>
#include <filesystem>

namespace sage
{
    namespace
    {
        EmitterConfig makeConfig(const ParticleEmitterComponent& settings, const Vector3 origin)
        {
            EmitterConfig config{};
            config.size = std::max(0.001f, settings.size);
            config.direction = settings.direction;
            config.velocity = {std::min(settings.speed.min, settings.speed.max),
                               std::max(settings.speed.min, settings.speed.max)};
            config.directionAngle = settings.spread;
            config.velocityAngle = settings.velocityAngle;
            config.offset = settings.offset;
            config.originAcceleration = settings.originAcceleration;
            config.burst = {std::max(0, std::min(settings.burst.min, settings.burst.max)),
                            std::max(0, std::max(settings.burst.min, settings.burst.max))};
            config.capacity = static_cast<std::size_t>(std::clamp(settings.capacity, 1, 10000));
            config.emissionRate = static_cast<std::size_t>(std::clamp(settings.emissionRate, 0, 10000));
            config.origin = origin;
            config.externalAcceleration = settings.gravity;
            config.startColor = settings.startColor;
            config.endColor = settings.endColor;
            config.age = {std::max(0.01f, std::min(settings.lifetime.min, settings.lifetime.max)),
                          std::max(0.01f, std::max(settings.lifetime.min, settings.lifetime.max))};
            config.blendMode = settings.blendMode;
            const auto path = std::filesystem::path{ParticleTextureDirectory} / settings.texture;
            if (path.lexically_normal().string().starts_with(std::string{ParticleTextureDirectory} + "/") &&
                std::filesystem::is_regular_file(path))
                config.texture = ResourceManager::GetInstance().TextureLoad(path.string());
            return config;
        }
    } // namespace

    void ParticleEmitterSystem::Update(const float dt)
    {
        const float deltaTime = std::max(0.0f, dt);
        std::erase_if(instances, [this](const auto& entry) {
            return !registry.valid(entry.first) ||
                   !registry.all_of<ParticleEmitterComponent, sgTransform>(entry.first);
        });

        for (const auto entity : registry.view<ParticleEmitterComponent, sgTransform>())
        {
            const auto& settings = registry.get<ParticleEmitterComponent>(entity);
            const auto origin = registry.get<sgTransform>(entity).GetWorldPos();
            const auto config = makeConfig(settings, origin);
            auto& instance = instances[entity];
            if (!instance.emitter || instance.texture != settings.texture)
            {
                instance.Reset(config, settings.texture, settings.playOnAwake);
            }
            else
            {
                // Reinit preserves live particles while applying edited settings.
                instance.emitter->Reinit(config);
            }
            if (instance.paused) continue;
            instance.elapsed += deltaTime;
            if (!settings.looping && instance.elapsed >= std::max(0.0f, settings.duration))
                instance.emitter->Stop();
            instance.emitter->Update(deltaTime);
        }
    }

    void ParticleEmitterSystem::Draw(Camera3D& camera) const
    {
        for (const auto& [entity, instance] : instances)
            if (instance.emitter && instance.emitter->config.texture.id != 0)
                instance.emitter->Draw(&camera);
    }

    void ParticleEmitterSystem::Play(const entt::entity entity)
    {
        if (auto it = instances.find(entity); it != instances.end())
        {
            it->second.paused = false;
            it->second.emitter->Start();
        }
    }
    void ParticleEmitterSystem::Pause(const entt::entity entity)
    {
        if (auto it = instances.find(entity); it != instances.end()) it->second.paused = true;
    }
    void ParticleEmitterSystem::Burst(const entt::entity entity)
    {
        if (auto it = instances.find(entity); it != instances.end()) it->second.emitter->Burst();
    }
    void ParticleEmitterSystem::Restart(const entt::entity entity)
    {
        instances.erase(entity);
        if (registry.valid(entity) && registry.all_of<ParticleEmitterComponent, sgTransform>(entity))
        {
            const auto& settings = registry.get<ParticleEmitterComponent>(entity);
            const auto origin = registry.get<sgTransform>(entity).GetWorldPos();
            instances[entity].Reset(makeConfig(settings, origin), settings.texture, true);
        }
    }
    std::size_t ParticleEmitterSystem::Alive(const entt::entity entity) const
    {
        const auto it = instances.find(entity);
        if (it == instances.end()) return 0;
        return std::count_if(it->second.emitter->particles.begin(), it->second.emitter->particles.end(),
            [](const auto& particle) { return particle->active; });
    }
} // namespace sage

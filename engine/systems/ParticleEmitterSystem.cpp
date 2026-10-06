#include "ParticleEmitterSystem.hpp"
#include "engine/components/EntityVisibility.hpp"

#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/ParticleSystemComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/ResourceManager.hpp"

#include <algorithm>
#include <filesystem>

namespace sage
{
    std::vector<entt::entity> GatherParticleEmitters(const entt::registry& registry, const entt::entity root)
    {
        std::vector<entt::entity> result;
        if (!registry.valid(root) || !registry.all_of<sgTransform>(root)) return result;
        if (registry.all_of<ParticleEmitterComponent>(root)) result.push_back(root);
        for (const auto child : registry.get<sgTransform>(root).GetChildren())
        {
            const auto descendants = GatherParticleEmitters(registry, child);
            result.insert(result.end(), descendants.begin(), descendants.end());
        }
        return result;
    }

    entt::entity ParticleEffectRoot(const entt::registry& registry, const entt::entity entity)
    {
        auto ancestor = entity;
        // The closest explicit system root owns this effect, even through transform-only folders.
        while (registry.valid(ancestor) && registry.all_of<sgTransform>(ancestor))
        {
            if (registry.all_of<ParticleSystemComponent>(ancestor)) return ancestor;
            ancestor = registry.get<sgTransform>(ancestor).GetParent();
        }
        // Existing standalone emitters also support child layers without a system marker.
        auto root = entity;
        while (registry.valid(root) && registry.all_of<sgTransform>(root))
        {
            const auto parent = registry.get<sgTransform>(root).GetParent();
            if (!registry.valid(parent) || !registry.all_of<sgTransform, ParticleEmitterComponent>(parent)) break;
            root = parent;
        }
        return root;
    }

    EmitterConfig MakeParticleEmitterConfig(const ParticleEmitterComponent& settings, const Vector3 origin)
    {
        EmitterConfig config{};
        config.sizeOverLifetime = settings.sizeOverLifetime;
        config.opacityOverLifetime = settings.opacityOverLifetime;
        config.speedOverLifetime = settings.speedOverLifetime;
        config.colorOverLifetime = settings.colorOverLifetime;
        config.size = std::max(0.001f, settings.size);
        config.direction = settings.direction;
        config.velocity = {
            .min = std::min(settings.speed.min, settings.speed.max),
            .max = std::max(settings.speed.min, settings.speed.max)};
        config.directionAngle = settings.spread;
        config.velocityAngle = settings.velocityAngle;
        config.offset = settings.offset;
        config.originAcceleration = settings.originAcceleration;
        config.burst = {
            .min = std::max(0, std::min(settings.burst.min, settings.burst.max)),
            .max = std::max({0, settings.burst.min, settings.burst.max})};
        config.capacity = static_cast<std::size_t>(std::clamp(settings.capacity, 1, 10000));
        config.emissionRate = static_cast<std::size_t>(std::clamp(settings.emissionRate, 0, 10000));
        config.origin = origin;
        config.externalAcceleration = settings.gravity;
        config.startColor = settings.startColor;
        config.endColor = settings.endColor;
        config.age = {
            .min = std::max(0.01f, std::min(settings.lifetime.min, settings.lifetime.max)),
            .max = std::max({0.01f, settings.lifetime.min, settings.lifetime.max})};
        config.blendMode = settings.blendMode;
        const auto path = std::filesystem::path{PARTICLE_TEXTURE_DIRECTORY} / settings.texture;
        if (path.lexically_normal().string().starts_with(std::string{PARTICLE_TEXTURE_DIRECTORY} + "/") &&
            std::filesystem::is_regular_file(path))
            config.texture = ResourceManager::GetInstance().TextureLoad(path.string());
        return config;
    }

    void ParticleEmitterSystem::Update(
        const float dt, const std::optional<std::span<const entt::entity>> activeEntities)
    {
        const float deltaTime = std::max(0.0f, dt);
        const auto isActive = [&activeEntities](const entt::entity entity) {
            return !activeEntities || std::ranges::find(*activeEntities, entity) != activeEntities->end();
        };
        std::erase_if(instances, [this, &isActive](const auto& entry) {
            return !registry.get().valid(entry.first) ||
                   !registry.get().all_of<ParticleEmitterComponent, sgTransform>(entry.first) ||
                   !isActive(entry.first);
        });

        for (const auto entity : registry.get().view<ParticleEmitterComponent, sgTransform>())
        {
            if (!isActive(entity)) continue;
            const auto& settings = registry.get().get<ParticleEmitterComponent>(entity);
            const auto origin = registry.get().get<sgTransform>(entity).GetWorldPos();
            auto& instance = instances[entity];
            if (!instance.emitter || !instance.settings || instance.settings->texture != settings.texture)
            {
                instance.Reset(MakeParticleEmitterConfig(settings, origin), settings, settings.playOnAwake);
            }
            else if (*instance.settings != settings)
            {
                // Reinit preserves live particles while applying edited settings.
                instance.emitter->Reinit(MakeParticleEmitterConfig(settings, origin));
                instance.settings = settings;
            }
            // Movement changes the spawn origin without rebuilding configuration or live particles.
            instance.emitter->config.origin = origin;
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
            if (IsEntityVisible(registry.get(), entity) && instance.emitter &&
                instance.emitter->config.texture.id != 0)
                instance.emitter->Draw(&camera);
    }

    void ParticleEmitterSystem::Play(const entt::entity entity, const bool withChildren)
    {
        const auto targets = withChildren ? GatherParticleEmitters(registry.get(), entity) : std::vector{entity};
        for (const auto target : targets)
        {
            auto it = instances.find(target);
            if (it == instances.end())
            {
                Restart(target, false);
                it = instances.find(target);
            }
            if (it != instances.end())
            {
                it->second.paused = false;
                it->second.emitter->Start();
            }
        }
    }
    void ParticleEmitterSystem::Pause(const entt::entity entity, const bool withChildren)
    {
        const auto targets = withChildren ? GatherParticleEmitters(registry.get(), entity) : std::vector{entity};
        for (const auto target : targets)
            if (auto it = instances.find(target); it != instances.end()) it->second.paused = true;
    }
    void ParticleEmitterSystem::Burst(const entt::entity entity, const bool withChildren)
    {
        const auto targets = withChildren ? GatherParticleEmitters(registry.get(), entity) : std::vector{entity};
        for (const auto target : targets)
        {
            if (!instances.contains(target))
            {
                Restart(target, false);
                if (auto it = instances.find(target); it != instances.end()) it->second.emitter->Stop();
            }
            if (auto it = instances.find(target); it != instances.end()) it->second.emitter->Burst();
        }
    }
    void ParticleEmitterSystem::Restart(const entt::entity entity, const bool withChildren)
    {
        const auto targets = withChildren ? GatherParticleEmitters(registry.get(), entity) : std::vector{entity};
        for (const auto target : targets)
        {
            instances.erase(target);
            if (registry.get().valid(target) &&
                registry.get().all_of<ParticleEmitterComponent, sgTransform>(target))
            {
                const auto& settings = registry.get().get<ParticleEmitterComponent>(target);
                const auto origin = registry.get().get<sgTransform>(target).GetWorldPos();
                instances[target].Reset(MakeParticleEmitterConfig(settings, origin), settings, true);
            }
        }
    }
    std::size_t ParticleEmitterSystem::Alive(const entt::entity entity, const bool withChildren) const
    {
        const auto targets = withChildren ? GatherParticleEmitters(registry.get(), entity) : std::vector{entity};
        std::size_t alive = 0;
        for (const auto target : targets)
            if (auto it = instances.find(target); it != instances.end())
                alive += static_cast<std::size_t>(std::ranges::count_if(
                    it->second.emitter->particles, [](const auto& particle) { return particle->active; }));
        return alive;
    }
} // namespace sage

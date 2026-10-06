#include "engine/components/EntityVisibility.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/ParticleSystemComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/Flatpack.hpp"
#include "engine/systems/ParticleEmitterSystem.hpp"
#include "engine/systems/TransformSystem.hpp"

#include "engine/content/ContentDocument.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void CheckSelectedPlayback()
    {
        entt::registry registry;
        sage::TransformSystem transforms(&registry);
        auto settings = sage::ParticleEmitterComponent{};
        settings.texture = "missing-test-texture.png";
        settings.playOnAwake = true;
        settings.emissionRate = 60;
        settings.lifetime = {.min = 3, .max = 3};
        const auto first = registry.create();
        registry.emplace<sage::sgTransform>(first);
        registry.emplace<sage::ParticleEmitterComponent>(first, settings);
        const auto second = registry.create();
        registry.emplace<sage::sgTransform>(second);
        registry.emplace<sage::ParticleEmitterComponent>(second, settings);
        sage::ParticleEmitterSystem system(registry);
        const std::vector selected{first};
        system.Update(1.0f / 60, std::span<const entt::entity>{selected});
        Require(system.Alive(first) == 1 && system.Alive(second) == 0, "unselected emitter played");
        system.Update(1.0f / 60, std::span<const entt::entity>{});
        Require(system.Alive(first) == 0, "deselection left particles alive");
        system.Update(1.0f / 60, std::span<const entt::entity>{selected});
        Require(system.Alive(first) == 1, "reselection did not restart preview");
        system.Update(1.0f / 60);
        Require(system.Alive(second) == 1, "runtime playback was restricted by editor selection");
    }
    void CheckConfigurationChanges()
    {
        entt::registry registry;
        sage::TransformSystem transforms(&registry);
        const auto entity = registry.create();
        registry.emplace<sage::sgTransform>(entity);
        auto& settings = registry.emplace<sage::ParticleEmitterComponent>(entity);
        settings.texture = "missing-test-texture.png";
        settings.emissionRate = 60;
        settings.capacity = 4;
        settings.lifetime = {.min = 10, .max = 10};
        sage::ParticleEmitterSystem system(registry);
        system.Update(1.0f / 60);
        system.Update(1.0f / 60);
        Require(system.Alive(entity) == 2, "emitter failed to fill before configuration edits");
        transforms.SetWorldPos(entity, {.x = 10, .y = 2, .z = 3});
        system.Update(0);
        Require(system.Alive(entity) == 2, "moving an emitter reset live particles");
        settings.sizeOverLifetime.enabled = true;
        settings.sizeOverLifetime.values[2] = 3;
        settings.colorOverLifetime.colors[2].r = 12;
        system.Update(0);
        Require(system.Alive(entity) == 2, "editing lifetime modules reset live particles");
        settings.capacity = 1;
        system.Update(0);
        Require(system.Alive(entity) == 1, "cached configuration ignored a capacity edit");
        settings.capacity = 4;
        system.Update(1.0f / 60);
        Require(system.Alive(entity) == 2, "cached configuration did not grow emitter capacity");
        settings.emissionRate = 0;
        system.Update(1.0f / 60);
        Require(system.Alive(entity) == 2, "cached configuration ignored the new emission rate");
        settings.looping = false;
        settings.duration = 0;
        settings.emissionRate = 60;
        system.Update(1.0f / 60);
        Require(system.Alive(entity) == 2, "cached configuration ignored non-looping duration");
        settings.texture = "different-missing-test-texture.png";
        system.Update(0);
        Require(system.Alive(entity) == 0, "changing the texture did not restart the emitter");
    }

    void CheckEffectPlayback()
    {
        entt::registry registry;
        sage::TransformSystem transforms(&registry);
        const auto root = registry.create();
        registry.emplace<sage::sgTransform>(root);
        auto settings = sage::ParticleEmitterComponent{};
        settings.texture = "missing-test-texture.png";
        settings.playOnAwake = false;
        settings.emissionRate = 60;
        settings.lifetime = {.min = 3, .max = 3};
        settings.burst = {.min = 4, .max = 4};
        registry.emplace<sage::ParticleSystemComponent>(root);
        const auto first = registry.create();
        registry.emplace<sage::sgTransform>(first).SetParent(root);
        registry.emplace<sage::ParticleEmitterComponent>(first, settings);
        const auto folder = registry.create();
        registry.emplace<sage::sgTransform>(folder).SetParent(root);
        const auto child = registry.create();
        registry.emplace<sage::sgTransform>(child).SetParent(folder);
        registry.emplace<sage::ParticleEmitterComponent>(child, settings);
        const auto unrelated = registry.create();
        registry.emplace<sage::sgTransform>(unrelated);
        registry.emplace<sage::ParticleEmitterComponent>(unrelated, settings);
        Require(
            sage::GatherParticleEmitters(registry, root) == std::vector{first, child},
            "effect hierarchy included unrelated emitters or lost a child");
        Require(
            sage::ParticleEffectRoot(registry, child) == root,
            "selecting a child did not resolve the effect root");
        Require(
            sage::ParticleEffectRoot(registry, unrelated) == unrelated,
            "standalone emitter inherited an unrelated effect root");
        Require(
            sage::GatherParticleEmitters(registry, root).size() == 2 &&
                !registry.all_of<sage::ParticleEmitterComponent>(root),
            "grouping root became an emitter");
        registry.ctx().insert_or_assign<sage::EntityViewScope>(sage::EntityViewScope{root});
        Require(
            sage::IsEntityVisible(registry, first) && sage::IsEntityVisible(registry, child) &&
                !sage::IsEntityVisible(registry, unrelated),
            "isolated view did not restrict visibility to the effect hierarchy");
        registry.emplace<sage::EntityVisibility>(unrelated).visible = false;
        registry.ctx().erase<sage::EntityViewScope>();
        Require(
            sage::IsEntityInView(registry, unrelated) && !sage::IsEntityVisible(registry, unrelated),
            "closing isolation changed authored visibility");
        registry.remove<sage::EntityVisibility>(unrelated);
        Require(sage::IsEntityVisible(registry, unrelated), "closing isolation did not restore the scene view");
        sage::ParticleEmitterSystem system(registry);
        // Controls must work before the first runtime update.
        system.Burst(root);
        Require(
            system.Alive(root) == 8 && system.Alive(child, false) == 4 && system.Alive(unrelated) == 0,
            "group burst did not initialize and emit every layer independently");
        system.Update(1.0f / 60);
        Require(system.Alive(root) == 8, "burst unexpectedly started continuous emission");
        system.Restart(root);
        system.Update(1.0f / 60);
        Require(system.Alive(root) == 2, "group restart did not start all layers");
        system.Pause(root);
        system.Update(0.5f);
        Require(system.Alive(root) == 2, "group pause left a child running");
        system.Play(root);
        system.Update(1.0f / 60);
        Require(system.Alive(root) == 4, "group play did not resume every layer");
        registry.emplace<sage::EntityVisibility>(root).visible = false;
        Require(
            !sage::IsEntityVisible(registry, first) && !sage::IsEntityVisible(registry, child) &&
                sage::IsEntityVisible(registry, unrelated),
            "root visibility did not propagate to its emitters");
        registry.get<sage::EntityVisibility>(root).visible = true;
        registry.emplace<sage::EntityVisibility>(first).visible = false;
        Require(
            !sage::IsEntityVisible(registry, first) && sage::IsEntityVisible(registry, child),
            "hiding one layer also hid a sibling");
        system.Pause(first, false);
        system.Update(1.0f / 60);
        Require(
            system.Alive(first, false) == 2 && system.Alive(child, false) == 3,
            "single-emitter pause also paused child emitters");
        registry.destroy(child);
        system.Update(0);
        Require(system.Alive(root) == 2, "removed child retained runtime particles");
    }

    void CheckSystemRootPersistence()
    {
        entt::registry source;
        sage::TransformSystem transforms(&source);
        const auto root = source.create();
        source.emplace<sage::sgTransform>(root).name = "Particle System";
        source.emplace<sage::ParticleSystemComponent>(root);
        source.emplace<sage::EntityVisibility>(root).visible = false;
        const auto emitter = source.create();
        auto& transform = source.emplace<sage::sgTransform>(emitter);
        transform.SetParent(root);
        transform.position.local = {.x = 2, .y = 1, .z = -3};
        source.emplace<sage::ParticleEmitterComponent>(emitter).emissionRate = 7;
        const auto path = std::filesystem::temp_directory_path() / "sage_particle_root_test.flatpack";
        Require(sage::SaveFlatpack(source, root, path.string().c_str()), "system root save failed");
        entt::registry loaded;
        sage::TransformSystem loadedTransforms(&loaded);
        const auto instance = sage::LoadFlatpack(loaded, path.string().c_str(), Vector3{});
        std::filesystem::remove(path);
        Require(
            instance && loaded.all_of<sage::ParticleSystemComponent>(instance.root) &&
                !loaded.all_of<sage::ParticleEmitterComponent>(instance.root),
            "flatpack lost the grouping root or added an emitter to it");
        const auto members = sage::GatherParticleEmitters(loaded, instance.root);
        Require(
            members.size() == 1 && sage::ParticleEffectRoot(loaded, members.front()) == instance.root &&
                loaded.get<sage::ParticleEmitterComponent>(members.front()).emissionRate == 7 &&
                loaded.get<sage::sgTransform>(members.front()).GetLocalPos().x == 2,
            "flatpack lost child emitter ownership or settings");
        Require(!sage::IsEntityVisible(loaded, members.front()), "flatpack lost inherited entity visibility");
        const auto document = sage::content::Capture(source, {root, emitter});
        entt::registry map;
        sage::TransformSystem mapTransforms(&map);
        const auto restored = sage::content::Instantiate(map, document);
        Require(
            restored.entities.size() == 2 && map.all_of<sage::ParticleSystemComponent>(restored.entities.front()),
            "map document lost the particle system root");
        const auto mapMembers = sage::GatherParticleEmitters(map, restored.entities.front());
        Require(!sage::IsEntityVisible(map, mapMembers.front()), "map lost inherited entity visibility");
        Require(
            mapMembers.size() == 1 &&
                sage::ParticleEffectRoot(map, mapMembers.front()) == restored.entities.front(),
            "map document lost child emitter ownership");
    }

    void CheckSimulation()
    {
        sage::ParticleCurve curve{.enabled = true, .values = {0, 1, 2, 1, 0}};
        Require(
            curve.Evaluate(-1) == 0 && curve.Evaluate(1) == 0 && curve.Evaluate(2) == 0 &&
                curve.Evaluate(0.375f) == 1.5f,
            "curve interpolation or endpoints incorrect");
        curve.enabled = false;
        Require(curve.Evaluate(0) == 1, "disabled curve must preserve base settings");

        sage::EmitterConfig config{};
        config.capacity = 8;
        config.burst = {.min = 4, .max = 4};
        config.size = 2;
        config.direction = {.x = 0, .y = 1, .z = 0};
        config.velocity = {.min = 1, .max = 3};
        config.age = {.min = 2, .max = 4};
        config.randomSeed = 42;
        sage::Emitter first(config), second(config);
        first.Burst();
        first.Reinit(config);
        Require(
            static_cast<bool>(first.particles.front()->particle_Deactivator),
            "editing settings cleared the default lifetime callback");
        // Other emitters must not perturb a seeded emitter's random sequence.
        auto otherConfig = config;
        otherConfig.randomSeed = 17;
        sage::Emitter other(otherConfig);
        other.Burst();
        second.Burst();
        for (int frame = 0; frame < 30; ++frame)
        {
            first.Update(1.0f / 60);
            second.Update(1.0f / 60);
        }
        for (std::size_t i = 0; i < config.capacity; ++i)
        {
            const auto& a = *first.particles.at(i);
            const auto& b = *second.particles.at(i);
            Require(
                a.active == b.active && a.ttl == b.ttl && a.position.x == b.position.x &&
                    a.position.y == b.position.y && a.position.z == b.position.z,
                "fixed seed not reproducible");
        }
        sage::Emitter restarted(config);
        restarted.Burst();
        Require(
            restarted.particles.front()->velocity.y == second.particles.front()->velocity.y,
            "restart did not reproduce initial velocity");

        sage::Particle particle({});
        particle.active = true;
        particle.ttl = 2;
        particle.velocity = {.x = 2, .y = 0, .z = 0};
        particle.Update(0.5f, sage::ParticleCurve{.enabled = true, .values = {0, 0.5f, 1, 1, 1}});
        Require(
            particle.position.x == 0.5f && particle.velocity.x == 2,
            "speed curve compounded velocity rather than scaling motion");
        first.config.sizeOverLifetime = {.enabled = true, .values = {0, 1, 2, 1, 0}};
        first.config.opacityOverLifetime = {.enabled = true, .values = {0, 0.5f, 1, 0.5f, 0}};
        first.config.colorOverLifetime.enabled = true;
        first.config.colorOverLifetime.colors.at(1) = {.r = 10, .g = 20, .b = 30, .a = 200};
        particle.size = 2;
        Require(
            first.ParticleSize(particle) == 2 && first.ParticleColor(particle).g == 20 &&
                first.ParticleColor(particle).a == 100,
            "lifetime appearance not applied");
        config.burst = {.min = 0, .max = 0};
        sage::Emitter empty(config);
        empty.Burst();
        Require(
            std::ranges::none_of(empty.particles, [](const auto& p) { return p->active; }),
            "zero burst emitted a particle");
    }
} // namespace

int main()
{
    try
    {
        CheckSimulation();
        CheckSelectedPlayback();
        CheckConfigurationChanges();
        CheckEffectPlayback();
        CheckSystemRootPersistence();
        entt::registry source;
        sage::TransformSystem sourceTransforms(&source);
        const auto root = source.create();
        source.emplace<sage::sgTransform>(root).name = "Embers";
        auto& original = source.emplace<sage::ParticleEmitterComponent>(root);
        original.texture = "Rotated/spark_05_rotated.png";
        original.playOnAwake = false;
        original.looping = false;
        original.duration = 3.5f;
        original.speed = {.min = 2.0f, .max = 7.0f};
        original.burst = {.min = 4, .max = 17};
        original.gravity = {.x = 0.0f, .y = -9.8f, .z = 1.0f};
        original.startColor = {.r = 255, .g = 44, .b = 12, .a = 220};
        original.blendMode = BLEND_ADDITIVE;
        original.sizeOverLifetime = {.enabled = true, .values = {0.1f, 0.5f, 1, 2, 0}};
        original.opacityOverLifetime = {.enabled = true, .values = {0, 0.5f, 1, 0.5f, 0}};
        original.speedOverLifetime = {.enabled = true, .values = {2, 1, 0.5f, 0.25f, 0}};
        original.colorOverLifetime.enabled = true;
        original.colorOverLifetime.colors.at(2) = {.r = 255, .g = 128, .b = 16, .a = 160};

        const auto child = source.create();
        auto& childTransform = source.emplace<sage::sgTransform>(child);
        childTransform.name = "Smoke";
        childTransform.SetParent(root);
        childTransform.position.local = {.x = 2, .y = 1, .z = -3};
        auto& childSettings = source.emplace<sage::ParticleEmitterComponent>(child);
        childSettings.texture = "smoke_01.png";
        childSettings.size = 3;
        childSettings.emissionRate = 7;

        const auto path = std::filesystem::temp_directory_path() / "sage_particle_persistence_test.flatpack";
        if (!sage::SaveFlatpack(source, root, path.string().c_str())) throw std::runtime_error("save failed");
        entt::registry loaded;
        sage::TransformSystem loadedTransforms(&loaded);
        const auto instance = sage::LoadFlatpack(loaded, path.string().c_str(), {.x = 0, .y = 0, .z = 0});
        std::filesystem::remove(path);
        if (!instance || !loaded.all_of<sage::ParticleEmitterComponent>(instance.root))
            throw std::runtime_error("particle component missing from loaded flatpack");
        const auto& result = loaded.get<sage::ParticleEmitterComponent>(instance.root);
        if (result.texture != original.texture || result.playOnAwake != original.playOnAwake ||
            result.looping != original.looping || result.duration != original.duration ||
            result.speed.min != original.speed.min || result.speed.max != original.speed.max ||
            result.burst.min != original.burst.min || result.burst.max != original.burst.max ||
            result.gravity.y != original.gravity.y || result.startColor.g != original.startColor.g ||
            result.blendMode != original.blendMode)
            throw std::runtime_error("particle settings changed during flatpack round trip");
        Require(
            result.sizeOverLifetime.enabled &&
                result.sizeOverLifetime.values == original.sizeOverLifetime.values &&
                result.opacityOverLifetime.values == original.opacityOverLifetime.values &&
                result.speedOverLifetime.values == original.speedOverLifetime.values &&
                result.colorOverLifetime.enabled && result.colorOverLifetime.colors.at(2).a == 160,
            "lifetime modules changed during flatpack round trip");
        const auto loadedMembers = sage::GatherParticleEmitters(loaded, instance.root);
        Require(loadedMembers.size() == 2, "flatpack lost a child emitter");
        const auto loadedChild = loadedMembers.at(1);
        Require(
            loaded.get<sage::ParticleEmitterComponent>(loadedChild).texture == childSettings.texture &&
                loaded.get<sage::ParticleEmitterComponent>(loadedChild).emissionRate == 7 &&
                loaded.get<sage::sgTransform>(loadedChild).GetLocalPos().x == 2 &&
                sage::ParticleEffectRoot(loaded, loadedChild) == instance.root,
            "flatpack lost independent child settings, offset or parent");
        const auto document = sage::content::Capture(source, {root, child});
        entt::registry map;
        sage::TransformSystem mapTransforms(&map);
        const auto restored = sage::content::Instantiate(map, document);
        Require(
            restored.entities.size() == 2 &&
                map.get<sage::ParticleEmitterComponent>(restored.entities.front()).sizeOverLifetime.values ==
                    original.sizeOverLifetime.values,
            "map document lost lifetime modules");
        const auto mapMembers = sage::GatherParticleEmitters(map, restored.entities.front());
        Require(
            mapMembers.size() == 2 && map.get<sage::ParticleEmitterComponent>(mapMembers.at(1)).size == 3 &&
                map.get<sage::sgTransform>(mapMembers.at(1)).GetLocalPos().z == -3,
            "map document lost layered effect settings or child placement");
        std::cout << "Particle simulation, effect playback and persistence passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "engine/Flatpack.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/systems/TransformSystem.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        entt::registry source;
        sage::TransformSystem sourceTransforms(&source);
        const auto root = source.create();
        source.emplace<sage::sgTransform>(root).name = "Embers";
        auto& original = source.emplace<sage::ParticleEmitterComponent>(root);
        original.texture = "Rotated/spark_05_rotated.png";
        original.playOnAwake = false;
        original.looping = false;
        original.duration = 3.5f;
        original.speed = {2.0f, 7.0f};
        original.burst = {4, 17};
        original.gravity = {0.0f, -9.8f, 1.0f};
        original.startColor = {255, 44, 12, 220};
        original.blendMode = BLEND_ADDITIVE;

        const auto path = std::filesystem::temp_directory_path() / "sage_particle_persistence_test.flatpack";
        if (!sage::SaveFlatpack(source, root, path.string().c_str()))
            throw std::runtime_error("save failed");
        entt::registry loaded;
        sage::TransformSystem loadedTransforms(&loaded);
        const auto instance = sage::LoadFlatpack(loaded, path.string().c_str(), {0, 0, 0});
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
        std::cout << "Particle flatpack round trip passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

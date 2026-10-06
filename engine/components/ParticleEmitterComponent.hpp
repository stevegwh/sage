#pragma once

#include "cereal/cereal.hpp"

#include "cereal/types/string.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/ParticleSystem.hpp"
#include "engine/raylib-cereal.hpp"

#include <string>
#include <string_view>
#include <tuple>

namespace sage
{
    inline constexpr const char* PARTICLE_TEXTURE_DIRECTORY = "resources/textures/particles";

    // Emitter settings. GPU textures and live particles are runtime state.
    struct ParticleEmitterComponent
    {
        std::string texture = "circle_01.png";
        bool playOnAwake = true;
        bool looping = true;
        float duration = 5.0f;
        float size = 1.0f;
        Vector3 direction{.x = 0.0f, .y = 1.0f, .z = 0.0f};
        FloatRange speed{.min = 1.0f, .max = 2.0f};
        FloatRange spread{.min = -15.0f, .max = 15.0f};
        FloatRange velocityAngle{.min = 0.0f, .max = 0.0f};
        FloatRange offset{.min = 0.0f, .max = 0.0f};
        FloatRange originAcceleration{.min = 0.0f, .max = 0.0f};
        Vector3 gravity{.x = 0.0f, .y = 0.0f, .z = 0.0f};
        FloatRange lifetime{.min = 1.0f, .max = 2.0f};
        Color startColor{.r = 255, .g = 255, .b = 255, .a = 255};
        Color endColor{.r = 255, .g = 255, .b = 255, .a = 0};
        int capacity = 256;
        int emissionRate = 20;
        IntRange burst{.min = 10, .max = 20};
        BlendMode blendMode = BLEND_ALPHA;
        ParticleCurve sizeOverLifetime;
        ParticleCurve opacityOverLifetime;
        ParticleCurve speedOverLifetime;
        ParticleGradient colorOverLifetime;

        bool operator==(const ParticleEmitterComponent& other) const
        {
            const auto values = [](const ParticleEmitterComponent& settings) {
                std::array<int, PARTICLE_LIFETIME_SAMPLE_COUNT> gradientColors{};
                std::ranges::transform(settings.colorOverLifetime.colors, gradientColors.begin(), ColorToInt);
                return std::tuple{
                    std::string_view{settings.texture},
                    settings.playOnAwake,
                    settings.looping,
                    settings.duration,
                    settings.size,
                    settings.direction.x,
                    settings.direction.y,
                    settings.direction.z,
                    settings.speed.min,
                    settings.speed.max,
                    settings.spread.min,
                    settings.spread.max,
                    settings.velocityAngle.min,
                    settings.velocityAngle.max,
                    settings.offset.min,
                    settings.offset.max,
                    settings.originAcceleration.min,
                    settings.originAcceleration.max,
                    settings.gravity.x,
                    settings.gravity.y,
                    settings.gravity.z,
                    settings.lifetime.min,
                    settings.lifetime.max,
                    ColorToInt(settings.startColor),
                    ColorToInt(settings.endColor),
                    settings.capacity,
                    settings.emissionRate,
                    settings.burst.min,
                    settings.burst.max,
                    settings.blendMode,
                    settings.sizeOverLifetime.enabled,
                    settings.sizeOverLifetime.values,
                    settings.opacityOverLifetime.enabled,
                    settings.opacityOverLifetime.values,
                    settings.speedOverLifetime.enabled,
                    settings.speedOverLifetime.values,
                    settings.colorOverLifetime.enabled,
                    gradientColors};
            };
            return values(*this) == values(other);
        }

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("texture", texture),
                cereal::make_nvp("playOnAwake", playOnAwake),
                cereal::make_nvp("looping", looping),
                cereal::make_nvp("duration", duration),
                cereal::make_nvp("size", size),
                cereal::make_nvp("direction", direction),
                cereal::make_nvp("speed.min", speed.min),
                cereal::make_nvp("speed.max", speed.max),
                cereal::make_nvp("spread.min", spread.min),
                cereal::make_nvp("spread.max", spread.max),
                cereal::make_nvp("velocityAngle.min", velocityAngle.min),
                cereal::make_nvp("velocityAngle.max", velocityAngle.max),
                cereal::make_nvp("offset.min", offset.min),
                cereal::make_nvp("offset.max", offset.max),
                cereal::make_nvp("originAcceleration.min", originAcceleration.min),
                cereal::make_nvp("originAcceleration.max", originAcceleration.max),
                cereal::make_nvp("gravity", gravity),
                cereal::make_nvp("lifetime.min", lifetime.min),
                cereal::make_nvp("lifetime.max", lifetime.max),
                cereal::make_nvp("startColor", startColor),
                cereal::make_nvp("endColor", endColor),
                cereal::make_nvp("capacity", capacity),
                cereal::make_nvp("emissionRate", emissionRate),
                cereal::make_nvp("burst.min", burst.min),
                cereal::make_nvp("burst.max", burst.max),
                cereal::make_nvp("blendMode", blendMode),
                cereal::make_nvp("sizeOverLifetime", sizeOverLifetime),
                cereal::make_nvp("opacityOverLifetime", opacityOverLifetime),
                cereal::make_nvp("speedOverLifetime", speedOverLifetime),
                cereal::make_nvp("colorOverLifetime", colorOverLifetime));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<sgTransform>();
            i.module("Main");
            i.field("play_on_awake", "Play On Awake", playOnAwake);
            i.field("looping", "Looping", looping);
            i.field("duration", "Duration", duration);
            i.field("size", "Start Size", size);
            i.field("lifetime_min", "Start Lifetime Min", lifetime.min);
            i.field("lifetime_max", "Start Lifetime Max", lifetime.max);
            i.field("speed_min", "Start Speed Min", speed.min);
            i.field("speed_max", "Start Speed Max", speed.max);
            i.field("start_color", "Start Color", startColor);
            i.field("capacity", "Max Particles", capacity);
            i.module("Emission");
            i.field("emission_rate", "Emission Rate", emissionRate);
            i.field("burst_min", "Burst Min", burst.min);
            i.field("burst_max", "Burst Max", burst.max);
            i.module("Shape and Motion");
            i.note("Space", "World");
            i.field("direction", "Direction", direction);
            i.field("spread_min", "Spread Min", spread.min);
            i.field("spread_max", "Spread Max", spread.max);
            i.field("velocity_angle_min", "Velocity Angle Min", velocityAngle.min);
            i.field("velocity_angle_max", "Velocity Angle Max", velocityAngle.max);
            i.field("offset_min", "Offset Min", offset.min);
            i.field("offset_max", "Offset Max", offset.max);
            i.field("origin_acceleration_min", "Origin Acceleration Min", originAcceleration.min);
            i.field("origin_acceleration_max", "Origin Acceleration Max", originAcceleration.max);
            i.field("gravity", "Gravity", gravity);
            i.module("Renderer");
            i.particleTextureDropdown("texture", "Texture", texture);
            i.field("end_color", "End Color", endColor);
            i.field("blend_mode", "Blend Mode", blendMode);
        }
    };
} // namespace sage

#pragma once

#include "cereal/cereal.hpp"

#include "engine/ParticleSystem.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/raylib-cereal.hpp"
#include "cereal/types/string.hpp"

#include <string>

namespace sage
{
    inline constexpr const char* ParticleTextureDirectory = "resources/textures/particles";

    // Emitter settings. GPU textures and live particles are runtime state.
    struct ParticleEmitterComponent
    {
        std::string texture = "circle_01.png";
        bool playOnAwake = true;
        bool looping = true;
        float duration = 5.0f;
        float size = 1.0f;
        Vector3 direction{0.0f, 1.0f, 0.0f};
        FloatRange speed{1.0f, 2.0f};
        FloatRange spread{-15.0f, 15.0f};
        FloatRange velocityAngle{0.0f, 0.0f};
        FloatRange offset{0.0f, 0.0f};
        FloatRange originAcceleration{0.0f, 0.0f};
        Vector3 gravity{0.0f, 0.0f, 0.0f};
        FloatRange lifetime{1.0f, 2.0f};
        Color startColor{255, 255, 255, 255};
        Color endColor{255, 255, 255, 0};
        int capacity = 256;
        int emissionRate = 20;
        IntRange burst{10, 20};
        BlendMode blendMode = BLEND_ALPHA;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("texture", texture), cereal::make_nvp("playOnAwake", playOnAwake), cereal::make_nvp("looping", looping), cereal::make_nvp("duration", duration), cereal::make_nvp("size", size), cereal::make_nvp("direction", direction), cereal::make_nvp("speed.min", speed.min), cereal::make_nvp("speed.max", speed.max), cereal::make_nvp("spread.min", spread.min), cereal::make_nvp("spread.max", spread.max), cereal::make_nvp("velocityAngle.min", velocityAngle.min), cereal::make_nvp("velocityAngle.max", velocityAngle.max), cereal::make_nvp("offset.min", offset.min), cereal::make_nvp("offset.max", offset.max), cereal::make_nvp("originAcceleration.min", originAcceleration.min), cereal::make_nvp("originAcceleration.max", originAcceleration.max), cereal::make_nvp("gravity", gravity), cereal::make_nvp("lifetime.min", lifetime.min), cereal::make_nvp("lifetime.max", lifetime.max), cereal::make_nvp("startColor", startColor), cereal::make_nvp("endColor", endColor), cereal::make_nvp("capacity", capacity), cereal::make_nvp("emissionRate", emissionRate), cereal::make_nvp("burst.min", burst.min), cereal::make_nvp("burst.max", burst.max), cereal::make_nvp("blendMode", blendMode));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<sgTransform>();
            i.note("Main", "Playback and initial particle settings");
            i.particleTextureDropdown("texture", "Texture", texture);
            i.field("play_on_awake", "Play On Awake", playOnAwake);
            i.field("looping", "Looping", looping);
            i.field("duration", "Duration", duration);
            i.field("size", "Size", size);
            i.divider("Emission");
            i.note("Emission", "Rate and burst counts");
            i.field("capacity", "Capacity", capacity);
            i.field("emission_rate", "Emission Rate", emissionRate);
            i.field("burst_min", "Burst Min", burst.min);
            i.field("burst_max", "Burst Max", burst.max);
            i.divider("Motion");
            i.note("Motion", "Velocity and forces in world space");
            i.field("direction", "Direction", direction);
            i.field("speed_min", "Speed Min", speed.min);
            i.field("speed_max", "Speed Max", speed.max);
            i.field("spread_min", "Spread Min", spread.min);
            i.field("spread_max", "Spread Max", spread.max);
            i.field("velocity_angle_min", "Velocity Angle Min", velocityAngle.min);
            i.field("velocity_angle_max", "Velocity Angle Max", velocityAngle.max);
            i.field("offset_min", "Offset Min", offset.min);
            i.field("offset_max", "Offset Max", offset.max);
            i.field("origin_acceleration_min", "Origin Acceleration Min", originAcceleration.min);
            i.field("origin_acceleration_max", "Origin Acceleration Max", originAcceleration.max);
            i.field("gravity", "Gravity", gravity);
            i.divider("Lifetime");
            i.field("lifetime_min", "Lifetime Min", lifetime.min);
            i.field("lifetime_max", "Lifetime Max", lifetime.max);
            i.divider("Rendering");
            i.field("start_color", "Start Color", startColor);
            i.field("end_color", "End Color", endColor);
            i.field("blend_mode", "Blend Mode", blendMode);
        }
    };
} // namespace sage

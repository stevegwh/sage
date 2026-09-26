//
// Created by Steve Wheeler on 26/09/2024.
//

#pragma once
#include "cereal/archives/json.hpp"

#include "cereal/cereal.hpp"

// #include "raylib-cereal.hpp"
#include "raylib.h"

namespace sage
{
    enum class LightType : int
    {
        Sun = 0,
        Point = 1
    };

    struct Light
    {
        LightType type;
        bool enabled;
        Vector3 position;
        Vector3 target;
        Color color;
        float brightness;
        // TODO: Allow user to set below values
        float constant = 0;
        float linear = 1.0;
        float quadratic = 0.5;

        void LinkShader(const Shader shader, const int lightsCount) const
        {
            // NOTE: Lighting shader naming must be the provided ones
            int enabledLoc = GetShaderLocation(shader, TextFormat("lights[%i].enabled", lightsCount));
            int typeLoc = GetShaderLocation(shader, TextFormat("lights[%i].type", lightsCount));
            int positionLoc = GetShaderLocation(shader, TextFormat("lights[%i].position", lightsCount));
            int targetLoc = GetShaderLocation(shader, TextFormat("lights[%i].target", lightsCount));
            int colorLoc = GetShaderLocation(shader, TextFormat("lights[%i].color", lightsCount));
            int brightnessLoc = GetShaderLocation(shader, TextFormat("lights[%i].brightness", lightsCount));
            int constantLoc = GetShaderLocation(shader, TextFormat("lights[%i].constant", lightsCount));
            int linearLoc = GetShaderLocation(shader, TextFormat("lights[%i].linear", lightsCount));
            int quadraticLoc = GetShaderLocation(shader, TextFormat("lights[%i].quadratic", lightsCount));

            // UpdateLightValues(shader, *this);
            float _position[3] = {position.x, position.y, position.z};
            float _target[3] = {target.x, target.y, target.z};
            float _color[4] = {
                static_cast<float>(color.r) / static_cast<float>(255),
                static_cast<float>(color.g) / static_cast<float>(255),
                static_cast<float>(color.b) / static_cast<float>(255),
                static_cast<float>(color.a) / static_cast<float>(255)};
            const int enabledValue = enabled ? 1 : 0;
            SetShaderValue(shader, enabledLoc, &enabledValue, SHADER_UNIFORM_INT);
            const int typeValue = static_cast<int>(type);
            SetShaderValue(shader, typeLoc, &typeValue, SHADER_UNIFORM_INT);
            SetShaderValue(shader, positionLoc, _position, SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, targetLoc, _target, SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, colorLoc, _color, SHADER_UNIFORM_VEC4);
            SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, constantLoc, &constant, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, linearLoc, &linear, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, quadraticLoc, &quadratic, SHADER_UNIFORM_FLOAT);
        }

        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("type", type), cereal::make_nvp("position", position), cereal::make_nvp("target", target), cereal::make_nvp("color", color), cereal::make_nvp("brightness", brightness));
            if constexpr(std::is_same_v<Archive,cereal::JSONInputArchive> || std::is_same_v<Archive,cereal::JSONOutputArchive>)
                archive(cereal::make_nvp("enabled", enabled), cereal::make_nvp("constant", constant), cereal::make_nvp("linear", linear), cereal::make_nvp("quadratic", quadratic));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("enabled", "Enabled", enabled);
            i.field("type", "Type", type);
            i.field("position", "Position", position);
            i.field("target", "Target", target);
            i.field("color", "Color", color);
            i.field("brightness", "Brightness", brightness);
            i.field("constant", "Constant", constant);
            i.field("linear", "Linear", linear);
            i.field("quadratic", "Quadratic", quadratic);
        }
    };
} // namespace sage

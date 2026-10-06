//
// Created by Steve Wheeler on 26/09/2024.
//

#pragma once
#include "cereal/archives/json.hpp"
#include <array>
#include <tuple>

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

    struct LightShaderLocations
    {
        int enabled;
        int type;
        int position;
        int target;
        int color;
        int brightness;
        int constant;
        int linear;
        int quadratic;

        LightShaderLocations(const Shader shader, const int index)
            : enabled(GetShaderLocation(shader, TextFormat("lights[%i].enabled", index))),
              type(GetShaderLocation(shader, TextFormat("lights[%i].type", index))),
              position(GetShaderLocation(shader, TextFormat("lights[%i].position", index))),
              target(GetShaderLocation(shader, TextFormat("lights[%i].target", index))),
              color(GetShaderLocation(shader, TextFormat("lights[%i].color", index))),
              brightness(GetShaderLocation(shader, TextFormat("lights[%i].brightness", index))),
              constant(GetShaderLocation(shader, TextFormat("lights[%i].constant", index))),
              linear(GetShaderLocation(shader, TextFormat("lights[%i].linear", index))),
              quadratic(GetShaderLocation(shader, TextFormat("lights[%i].quadratic", index)))
        {
        }
    };

    struct Light
    {
        LightType type = LightType::Sun;
        bool enabled = false;
        Vector3 position{};
        Vector3 target{};
        Color color{};
        float brightness = 0.0f;
        bool castsShadows = false;
        float constant = 1.0f;
        float linear = 0.025f;
        float quadratic = 0.004f;

        bool operator==(const Light& other) const
        {
            const auto values = [](const Light& light) {
                return std::tuple{
                    light.type,
                    light.enabled,
                    light.position.x,
                    light.position.y,
                    light.position.z,
                    light.target.x,
                    light.target.y,
                    light.target.z,
                    ColorToInt(light.color),
                    light.brightness,
                    light.castsShadows,
                    light.constant,
                    light.linear,
                    light.quadratic};
            };
            return values(*this) == values(other);
        }

        void LinkShader(const Shader shader, const LightShaderLocations& locations) const
        {
            // UpdateLightValues(shader, *this);
            std::array<float, 3> _position = {position.x, position.y, position.z};
            std::array<float, 3> _target = {target.x, target.y, target.z};
            std::array<float, 4> _color = {
                static_cast<float>(color.r) / static_cast<float>(255),
                static_cast<float>(color.g) / static_cast<float>(255),
                static_cast<float>(color.b) / static_cast<float>(255),
                static_cast<float>(color.a) / static_cast<float>(255)};
            const int enabledValue = enabled ? 1 : 0;
            SetShaderValue(shader, locations.enabled, &enabledValue, SHADER_UNIFORM_INT);
            const int typeValue = static_cast<int>(type);
            SetShaderValue(shader, locations.type, &typeValue, SHADER_UNIFORM_INT);
            SetShaderValue(shader, locations.position, _position.data(), SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, locations.target, _target.data(), SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, locations.color, _color.data(), SHADER_UNIFORM_VEC4);
            SetShaderValue(shader, locations.brightness, &brightness, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, locations.constant, &constant, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, locations.linear, &linear, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, locations.quadratic, &quadratic, SHADER_UNIFORM_FLOAT);
        }

        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("type", type),
                cereal::make_nvp("position", position),
                cereal::make_nvp("target", target),
                cereal::make_nvp("color", color),
                cereal::make_nvp("brightness", brightness));
            if constexpr (
                std::is_same_v<Archive, cereal::JSONInputArchive> ||
                std::is_same_v<Archive, cereal::JSONOutputArchive>)
                archive(
                    cereal::make_nvp("enabled", enabled),
                    cereal::make_nvp("castsShadows", castsShadows),
                    cereal::make_nvp("constant", constant),
                    cereal::make_nvp("linear", linear),
                    cereal::make_nvp("quadratic", quadratic));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("enabled", "Enabled", enabled);
            i.field("castsShadows", "Casts Shadows", castsShadows);
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

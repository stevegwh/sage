#include "engine/AudioManager.hpp"
#include "engine/Camera.hpp"
#include "engine/Colors.hpp"
#include "engine/components/DynamicRenderable.hpp"
#include "engine/components/EntityVisibility.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/KeyMapping.hpp"
#include "engine/LightManager.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"
#include "engine/systems/RenderSystem.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "rlgl.h"
#include "ShaderPaths.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace
{
    constexpr int WIDTH = 640;
    constexpr int HEIGHT = 480;
    constexpr int DARK_THRESHOLD = 15;
    constexpr int LIT_THRESHOLD = 40;
    constexpr int OVERFLOW_THRESHOLD = 10;
    constexpr float CAMERA_HEIGHT = 15.0f;
    constexpr float VIEW_SIZE = 10.0f;
    constexpr float FLOOR_WIDTH = 12.0f;
    constexpr float LIGHT_HEIGHT = 2.0f;
    constexpr float LIGHT_SPACING = 3.0f;
    constexpr float LIGHT_Z = -2.0f;
    constexpr float WALL_CLEAR_Z = 8.0f;
    constexpr Vector3 SHADOW_SAMPLE{.x = 0.0f, .y = 0.0f, .z = -LIGHT_Z};

    void Require(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void CheckDark(const Color pixel)
    {
        Require(
            pixel.r < DARK_THRESHOLD && pixel.g < DARK_THRESHOLD && pixel.b < DARK_THRESHOLD,
            "Direct light leaked through the wall");
    }

    void Run(const std::optional<std::filesystem::path>& output)
    {
        bool exit = false;
        sage::Settings settings(&exit);
        settings.SetLightSettings({.ambient = {}, .gamma = 1.0f});
        sage::KeyMapping keys;
        sage::AudioManager audio;
        entt::registry registry;
        sage::EngineSystems systems(&registry, &keys, &settings, &audio);
        auto& lights = *systems.lightSubSystem;
        lights.SetShadowsEnabled(true);
        auto& camera = *systems.camera->getRaylibCam();
        camera = {
            .position = {.x = 0.0f, .y = CAMERA_HEIGHT, .z = 0.0f},
            .target = {},
            .up = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
            .fovy = VIEW_SIZE,
            .projection = CAMERA_ORTHOGRAPHIC};

        auto& resources = sage::ResourceManager::GetInstance();
        auto shader =
            resources.ShaderLoad(sage::ShaderPath("custom/lighting.vs"), sage::ShaderPath("custom/lighting.fs"));
        // All three forward paths use the same lighting include and shadow uniforms.
        for (const std::string name : {"lighting", "ubershader", "terrain"})
        {
            auto linked = resources.ShaderLoad(
                sage::ShaderPath("custom/" + name + ".vs"), sage::ShaderPath("custom/" + name + ".fs"));
            Require(linked.id != rlGetShaderIdDefault(), "Lighting shader failed to compile/link");
            Require(GetShaderLocation(linked, "pointShadowMap2") >= 0, "Third shadow sampler missing");
            lights.LinkShaderToLights(linked);
        }

        const auto addMesh = [&](Mesh mesh, const Vector3 position) {
            const auto entity = registry.create();
            registry.emplace<sage::sgTransform>(entity);
            systems.transformSystem->SetWorldPos(entity, position);
            auto& renderable =
                registry.emplace<sage::DynamicRenderable>(entity, LoadModelFromMesh(mesh), MatrixIdentity());
            renderable.SetShader(shader);
            return entity;
        };
        addMesh(GenMeshPlane(FLOOR_WIDTH, VIEW_SIZE, 1, 1), {});
        const auto wall = addMesh(GenMeshCube(FLOOR_WIDTH, 4.0f, 0.4f), {.x = 0.0f, .y = LIGHT_HEIGHT, .z = 0.0f});
        std::array<entt::entity, 3> pointLights{};
        const std::array<Color, 3> colors{
            Color{.r = 255, .g = 0, .b = 0, .a = 255},
            Color{.r = 0, .g = 255, .b = 0, .a = 255},
            Color{.r = 0, .g = 0, .b = 255, .a = 255}};
        for (std::size_t index = 0; index < pointLights.size(); ++index)
        {
            pointLights.at(index) = lights.CreateLight(
                sage::LightType::Point,
                {.x = -LIGHT_SPACING + static_cast<float>(index) * LIGHT_SPACING, .y = LIGHT_HEIGHT, .z = LIGHT_Z},
                {},
                colors.at(index),
                1.0f);
            registry.get<sage::Light>(pointLights.at(index)).castsShadows = true;
        }
        const auto target = LoadRenderTexture(WIDTH, HEIGHT);
        Require(target.id != 0, "Could not allocate render target");
        const auto capture = [&](const char* name, const std::optional<Vector3>& shadowFocus = std::nullopt) {
            lights.RefreshLights();
            lights.Update();
            const auto viewTarget = camera.target;
            if (shadowFocus) camera.target = *shadowFocus;
            lights.DrawShadowMap(*systems.renderSystem);
            camera.target = viewTarget;
            BeginTextureMode(target);
            ClearBackground(sage::colors::BLACK_COLOR);
            BeginMode3D(camera);
            systems.renderSystem->Draw();
            EndMode3D();
            EndTextureMode();
            auto image = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&image);
            const auto screen = GetWorldToScreenEx(SHADOW_SAMPLE, camera, WIDTH, HEIGHT);
            const auto pixel = GetImageColor(image, static_cast<int>(screen.x), static_cast<int>(screen.y));
            bool saved = true;
            if (output) saved = ExportImage(image, (*output / (std::string(name) + ".png")).string().c_str());
            UnloadImage(image);
            Require(saved, "Could not save shadow verification image");
            std::cout << name << ": " << static_cast<int>(pixel.r) << ", " << static_cast<int>(pixel.g) << ", "
                      << static_cast<int>(pixel.b) << '\n';
            return pixel;
        };

        lights.SetShadowsEnabled(false);
        const auto unshadowed = capture("unshadowed");
        Require(
            unshadowed.r > LIT_THRESHOLD && unshadowed.g > LIT_THRESHOLD && unshadowed.b > LIT_THRESHOLD,
            "Test point did not receive all three lights");
        lights.SetShadowsEnabled(true);
        CheckDark(capture("three-shadows"));

        const auto sun = lights.CreateLight(
            sage::LightType::Sun, {.x = 0.0f, .y = 4.0f, .z = -4.0f}, {}, sage::colors::WHITE_COLOR, 1.0f);
        registry.get<sage::Light>(sun).castsShadows = true;
        CheckDark(capture("sun-and-three-points"));
        registry.get<sage::Light>(sun).enabled = false;

        registry.get<sage::Light>(pointLights.at(0)).castsShadows = false;
        const auto optedOut = capture("shadow-flag");
        Require(
            optedOut.r > LIT_THRESHOLD && optedOut.g < DARK_THRESHOLD && optedOut.b < DARK_THRESHOLD,
            "Per-light shadow flag was ignored");
        registry.get<sage::Light>(pointLights.at(0)).castsShadows = true;

        const auto darkLight = lights.CreateLight(
            sage::LightType::Point, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {}, sage::colors::WHITE_COLOR, 0.0f);
        registry.get<sage::Light>(darkLight).castsShadows = true;
        CheckDark(capture("zero-brightness-budget"));
        lights.RemoveLight(darkLight);

        {
            // Editor and Play worlds share shader programs, but own their light lists and maps.
            entt::registry otherRegistry;
            sage::LightManager otherLights(
                &otherRegistry, systems.camera.get(), settings.GetLightSettings(), false);
            otherLights.CreateLight(
                sage::LightType::Point,
                {.x = 0.0f, .y = LIGHT_HEIGHT, .z = -LIGHT_Z},
                {},
                sage::colors::WHITE_COLOR,
                1.0f);
            CheckDark(capture("restored-world"));
        }

        // Reusing a cubemap slot must update the light's position and contents each frame.
        auto& moving = registry.get<sage::Light>(pointLights.at(0));
        moving.position.z = SHADOW_SAMPLE.z;
        const auto moved = capture("moved-light");
        Require(
            moved.r > LIT_THRESHOLD && moved.g < DARK_THRESHOLD && moved.b < DARK_THRESHOLD,
            "Moved light used a stale shadow map or index");
        moving.position.z = LIGHT_Z;
        CheckDark(capture("restored-light"));

        systems.transformSystem->SetWorldPos(wall, {.x = 0.0f, .y = LIGHT_HEIGHT, .z = WALL_CLEAR_Z});
        const auto open = capture("moved-wall");
        Require(
            open.r > LIT_THRESHOLD && open.g > LIT_THRESHOLD && open.b > LIT_THRESHOLD,
            "Moved wall left stale shadows");
        systems.transformSystem->SetWorldPos(wall, {.x = 0.0f, .y = LIGHT_HEIGHT, .z = 0.0f});

        // A farther fourth caster exceeds the budget. Moving the focus towards it must select it.
        const auto fourth = lights.CreateLight(
            sage::LightType::Point,
            {.x = 0.0f, .y = LIGHT_HEIGHT, .z = -WALL_CLEAR_Z},
            {},
            sage::colors::WHITE_COLOR,
            1.0f);
        registry.get<sage::Light>(fourth).castsShadows = true;
        const auto overflow = capture("shadow-budget");
        Require(
            overflow.r > OVERFLOW_THRESHOLD && overflow.g > OVERFLOW_THRESHOLD && overflow.b > OVERFLOW_THRESHOLD,
            "Fourth light no longer illuminates the scene");
        const auto focused = capture("focused-budget", Vector3{.x = 0.0f, .y = 0.0f, .z = -WALL_CLEAR_Z});
        Require(
            focused.g < DARK_THRESHOLD && ((focused.r > LIT_THRESHOLD && focused.b < DARK_THRESHOLD) ||
                                           (focused.b > LIT_THRESHOLD && focused.r < DARK_THRESHOLD)),
            "Shadow budget did not prioritise lights nearest the camera focus");
        registry.emplace<sage::EntityVisibility>(pointLights.at(0), false);
        registry.get<sage::Light>(pointLights.at(1)).enabled = false;
        lights.RemoveLight(pointLights.at(2));
        CheckDark(capture("hidden-disabled-deleted"));

        registry.get<sage::Light>(fourth).enabled = false;
        registry.get<sage::Light>(sun).enabled = true;
        CheckDark(capture("sun-shadow"));
        lights.SetShadowsEnabled(false);
        const auto sunlit = capture("sun-unshadowed");
        Require(
            sunlit.r > LIT_THRESHOLD && sunlit.g > LIT_THRESHOLD && sunlit.b > LIT_THRESHOLD,
            "Sun lighting stopped working");

        UnloadRenderTexture(target);
        // Models own GPU resources; destroy them while the context and engine systems are alive.
        registry.clear();
    }
} // namespace

int main(const int argc, const char* const* argv)
{
    try
    {
        std::optional<std::filesystem::path> output;
        if (argc == 2)
        {
            output = std::span(argv, static_cast<std::size_t>(argc)).subspan(1).front();
            std::filesystem::create_directories(*output);
        }
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(WIDTH, HEIGHT, "Point-light shadow verification");
        Require(IsWindowReady(), "OpenGL context unavailable");
        Run(output);
        sage::ResourceManager::GetInstance().Reset();
        CloseWindow();
        std::cout << "Point-light shadow rendering passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        sage::ResourceManager::GetInstance().Reset();
        if (IsWindowReady()) CloseWindow();
        return EXIT_FAILURE;
    }
}

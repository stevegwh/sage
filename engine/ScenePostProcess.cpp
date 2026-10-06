#include "ScenePostProcess.hpp"
#include "Colors.hpp"
#include "RenderProfiler.hpp"
#include "ResourceManager.hpp"
#include "rlgl.h"
#include "SceneRenderTarget.hpp"
#include "Settings.hpp"
#include "ShaderPaths.hpp"
#include <algorithm>
#include <cmath>
namespace sage
{
    namespace
    {
        constexpr float MIN_RENDER_SCALE = 0.25f;
    }
    Vector2 SceneRenderSize(const int displayWidth, const int displayHeight, const GraphicsSettings& settings)
    {
        const int width = std::max(1, displayWidth);
        const int height = std::max(1, displayHeight);
        const float scale = std::min(
            std::clamp(settings.renderScale, MIN_RENDER_SCALE, 1.0f),
            static_cast<float>(std::max(1, settings.maxRenderHeight)) / static_cast<float>(height));
        return {
            .x = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * scale)))),
            .y = static_cast<float>(
                std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * scale))))};
    }
    bool ResizeSceneRenderTarget(
        RenderTexture& scene, const int displayWidth, const int displayHeight, const GraphicsSettings& settings)
    {
        const auto size = SceneRenderSize(displayWidth, displayHeight, settings);
        const int width = static_cast<int>(size.x);
        const int height = static_cast<int>(size.y);
        if (scene.id != 0 && scene.texture.width == width && scene.texture.height == height) return false;
        const auto replacement = LoadSceneRenderTarget(width, height);
        if (scene.id != 0) UnloadRenderTexture(scene);
        scene = replacement;
        return true;
    }
    namespace
    {
        void Resize(RenderTexture& target, const int width, const int height)
        {
            if (target.texture.width == width && target.texture.height == height) return;
            const auto replacement = LoadColorRenderTarget(width, height);
            if (target.id != 0) UnloadRenderTexture(target);
            target = replacement;
        }
    } // namespace
    ScenePostProcess::ScenePostProcess()
        : occlusionShader(
              ResourceManager::GetInstance().ShaderLoad(std::nullopt, ShaderPath("custom/ambient_occlusion.fs"))),
          blurShader(
              ResourceManager::GetInstance().ShaderLoad(std::nullopt, ShaderPath("custom/occlusion_blur.fs"))),
          compositeShader(
              ResourceManager::GetInstance().ShaderLoad(std::nullopt, ShaderPath("custom/color_grade.fs")))
    {
    }
    ScenePostProcess::~ScenePostProcess()
    {
        if (occlusion.id != 0) UnloadRenderTexture(occlusion);
        if (filteredOcclusion.id != 0) UnloadRenderTexture(filteredOcclusion);
        if (output.id != 0) UnloadRenderTexture(output);
    }
    void ScenePostProcess::Process(
        const RenderTexture scene,
        const Camera3D camera,
        const GraphicsSettings& settings,
        const Texture2D bloom,
        RenderProfiler& profiler)
    {
        Resize(output, scene.texture.width, scene.texture.height);
        if (settings.ambientOcclusion)
            profiler.Measure(RenderPass::Occlusion, [&] {
                const int width = std::max(1, (scene.texture.width + 1) / 2);
                const int height = std::max(1, (scene.texture.height + 1) / 2);
                const Rectangle bounds{
                    .x = 0, .y = 0, .width = static_cast<float>(width), .height = static_cast<float>(height)};
                Resize(occlusion, width, height);
                Resize(filteredOcclusion, width, height);
                BeginTextureMode(occlusion);
                ClearBackground(colors::WHITE_COLOR);
                BeginShaderMode(occlusionShader);
                SetSceneGraphicsUniforms(occlusionShader, settings);
                SetSceneOcclusionUniforms(occlusionShader, scene, camera);
                DrawRenderTexture(scene.texture, bounds);
                EndShaderMode();
                EndTextureMode();
                BeginTextureMode(filteredOcclusion);
                ClearBackground(colors::WHITE_COLOR);
                BeginShaderMode(blurShader);
                SetSceneGraphicsUniforms(blurShader, settings);
                SetSceneOcclusionUniforms(blurShader, scene, camera);
                DrawRenderTexture(occlusion.texture, bounds);
                EndShaderMode();
                EndTextureMode();
            });
        profiler.Measure(RenderPass::PostProcess, [&] {
            BeginTextureMode(output);
            ClearBackground(colors::BLANK_COLOR);
            rlDisableColorBlend();
            BeginShaderMode(compositeShader);
            SetSceneGraphicsUniforms(compositeShader, settings);
            SetSceneOcclusionUniforms(compositeShader, scene, camera);
            if (settings.ambientOcclusion)
                SetShaderValueTexture(
                    compositeShader,
                    GetShaderLocation(compositeShader, "occlusionTexture"),
                    filteredOcclusion.texture);
            if (settings.bloom)
                SetShaderValueTexture(compositeShader, GetShaderLocation(compositeShader, "bloomTexture"), bloom);
            DrawRenderTexture(
                scene.texture,
                {.x = 0,
                 .y = 0,
                 .width = static_cast<float>(scene.texture.width),
                 .height = static_cast<float>(scene.texture.height)});
            EndShaderMode();
            rlEnableColorBlend();
            EndTextureMode();
        });
    }
} // namespace sage

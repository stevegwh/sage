#pragma once
#include "raylib.h"
namespace sage
{
    struct GraphicsSettings;
    class RenderProfiler;
    Vector2 SceneRenderSize(int displayWidth, int displayHeight, const GraphicsSettings& settings);
    bool ResizeSceneRenderTarget(
        RenderTexture& scene, int displayWidth, int displayHeight, const GraphicsSettings& settings);
    class ScenePostProcess
    {
        RenderTexture occlusion{};
        RenderTexture filteredOcclusion{};
        RenderTexture output{};
        Shader occlusionShader{};
        Shader blurShader{};
        Shader compositeShader{};

      public:
        ScenePostProcess();
        ~ScenePostProcess();
        ScenePostProcess(const ScenePostProcess&) = delete;
        ScenePostProcess(ScenePostProcess&&) = delete;
        ScenePostProcess& operator=(ScenePostProcess&&) = delete;
        ScenePostProcess& operator=(const ScenePostProcess&) = delete;
        void Process(
            RenderTexture scene,
            Camera3D camera,
            const GraphicsSettings& settings,
            Texture2D bloom,
            RenderProfiler& profiler);
        [[nodiscard]] Texture2D Texture() const
        {
            return output.texture;
        }
    };
} // namespace sage

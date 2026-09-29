#pragma once

#include "raylib.h"

namespace sage
{
    struct GraphicsSettings;
    // A scene framebuffer whose depth attachment can be sampled by post effects.
    RenderTexture LoadSceneRenderTarget(int width, int height);
    void SetSceneOcclusionUniforms(Shader shader, RenderTexture scene, Camera3D camera);
    void SetSceneGraphicsUniforms(Shader shader, const GraphicsSettings& settings);
} // namespace sage

#pragma once

#include "raylib.h"

namespace sage
{
    // A scene framebuffer whose depth attachment can be sampled by post effects.
    RenderTexture LoadSceneRenderTarget(int width, int height);
    void SetSceneOcclusionUniforms(Shader shader, RenderTexture scene, Camera3D camera);
} // namespace sage

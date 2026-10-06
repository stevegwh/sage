#pragma once

#include "raylib.h"

namespace sage
{
    struct GraphicsSettings;
    // A scene framebuffer whose depth attachment can be sampled by post effects.
    RenderTexture LoadSceneRenderTarget(int width, int height);
    RenderTexture LoadColorRenderTarget(int width, int height);
    // Framebuffer textures are vertically flipped when drawn into a viewport or another target.
    void DrawRenderTexture(Texture2D texture, Rectangle destination);
    void SetSceneOcclusionUniforms(Shader shader, RenderTexture scene, Camera3D camera);
    void SetSceneGraphicsUniforms(Shader shader, const GraphicsSettings& settings);
} // namespace sage

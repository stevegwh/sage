#include "SceneRenderTarget.hpp"

#include "rlgl.h"
#include "raymath.h"

#include <algorithm>
#include <stdexcept>

namespace sage
{
    RenderTexture LoadSceneRenderTarget(const int width, const int height)
    {
        const int targetWidth = std::max(1, width);
        const int targetHeight = std::max(1, height);
        RenderTexture target{};
        target.id = rlLoadFramebuffer();
        if (target.id == 0) throw std::runtime_error("Could not create scene framebuffer");

        target.texture = {rlLoadTexture(nullptr, targetWidth, targetHeight,
                                        PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1),
                          targetWidth, targetHeight, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        target.depth = {rlLoadTextureDepth(targetWidth, targetHeight, false),
                        targetWidth, targetHeight, 1, 19};
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        const bool complete = target.texture.id != 0 && target.depth.id != 0 && rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
        if (!complete)
        {
            UnloadRenderTexture(target);
            throw std::runtime_error("Could not create scene framebuffer with sampleable depth");
        }
        SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
        return target;
    }

    void SetSceneOcclusionUniforms(const Shader shader, const RenderTexture scene, const Camera3D camera)
    {
        const float aspect = static_cast<float>(scene.texture.width) / static_cast<float>(scene.texture.height);
        const Matrix projection = camera.projection == CAMERA_ORTHOGRAPHIC
            ? MatrixOrtho(-camera.fovy * aspect * 0.5, camera.fovy * aspect * 0.5,
                          -camera.fovy * 0.5, camera.fovy * 0.5,
                          rlGetCullDistanceNear(), rlGetCullDistanceFar())
            : MatrixPerspective(camera.fovy * DEG2RAD, aspect,
                                rlGetCullDistanceNear(), rlGetCullDistanceFar());
        SetShaderValueTexture(shader, GetShaderLocation(shader, "sceneDepth"), scene.depth);
        SetShaderValueMatrix(shader, GetShaderLocation(shader, "sceneProjection"), projection);
        SetShaderValueMatrix(shader, GetShaderLocation(shader, "inverseSceneProjection"), MatrixInvert(projection));
    }
} // namespace sage

#include "SceneRenderTarget.hpp"
#include "engine/MathConstants.hpp"
#include "Settings.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <stdexcept>

namespace sage
{
    namespace
    {
        constexpr int DEPTH_TEXTURE_FORMAT = 19;
    }
    RenderTexture LoadColorRenderTarget(const int width, const int height)
    {
        const int targetWidth = std::max(1, width);
        const int targetHeight = std::max(1, height);
        RenderTexture target{};
        target.id = rlLoadFramebuffer();
        if (target.id == 0) throw std::runtime_error("Could not create scene framebuffer");

        target.texture = {
            .id = rlLoadTexture(nullptr, targetWidth, targetHeight, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1),
            .width = targetWidth,
            .height = targetHeight,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        rlFramebufferAttach(
            target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        const bool complete = target.texture.id != 0 && rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
        if (!complete)
        {
            UnloadRenderTexture(target);
            throw std::runtime_error("Could not create color framebuffer");
        }
        SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
        return target;
    }

    RenderTexture LoadSceneRenderTarget(const int width, const int height)
    {
        auto target = LoadColorRenderTarget(width, height);
        const int targetWidth = target.texture.width;
        const int targetHeight = target.texture.height;
        target.depth = {
            .id = rlLoadTextureDepth(targetWidth, targetHeight, false),
            .width = targetWidth,
            .height = targetHeight,
            .mipmaps = 1,
            .format = DEPTH_TEXTURE_FORMAT};
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
                                      ? MatrixOrtho(
                                            -camera.fovy * aspect * 0.5,
                                            camera.fovy * aspect * 0.5,
                                            -camera.fovy * 0.5,
                                            camera.fovy * 0.5,
                                            rlGetCullDistanceNear(),
                                            rlGetCullDistanceFar())
                                      : MatrixPerspective(
                                            camera.fovy * sage::math::DEGREES_TO_RADIANS,
                                            aspect,
                                            rlGetCullDistanceNear(),
                                            rlGetCullDistanceFar());
        const float targetDistance = Vector3Distance(camera.position, camera.target);
        SetShaderValue(
            shader, GetShaderLocation(shader, "cameraTargetDistance"), &targetDistance, SHADER_UNIFORM_FLOAT);
        SetShaderValueTexture(shader, GetShaderLocation(shader, "sceneDepth"), scene.depth);
        SetShaderValueMatrix(shader, GetShaderLocation(shader, "sceneProjection"), projection);
        SetShaderValueMatrix(
            shader, GetShaderLocation(shader, "inverseSceneProjection"), MatrixInvert(projection));
    }

    void SetSceneGraphicsUniforms(const Shader shader, const GraphicsSettings& settings)
    {
        const auto setToggle = [shader](const char* name, const bool enabled) {
            const int value = enabled ? 1 : 0;
            SetShaderValue(shader, GetShaderLocation(shader, name), &value, SHADER_UNIFORM_INT);
        };
        const auto setFloat = [shader](const char* name, const float value) {
            SetShaderValue(shader, GetShaderLocation(shader, name), &value, SHADER_UNIFORM_FLOAT);
        };
        setToggle("enableBloom", settings.bloom);
        setFloat("bloomStrength", settings.bloomStrength);
        setToggle("enableAmbientOcclusion", settings.ambientOcclusion);
        setFloat("occlusionRadius", settings.occlusionRadius);
        setFloat("occlusionStrength", settings.occlusionStrength);
        setToggle("enableFxaa", settings.fxaa);
        setToggle("enableColorGrading", settings.colorGrading);
        setFloat("saturation", settings.saturation);
        setFloat("contrast", settings.contrast);
        setToggle("enableDepthOfField", settings.depthOfField);
        setToggle("focusCameraTarget", settings.focusCameraTarget);
        setFloat("focusDistance", settings.focusDistance);
        setFloat("focusRange", settings.focusRange);
        setFloat("maxBlurRadius", settings.maxBlurRadius);
    }
} // namespace sage

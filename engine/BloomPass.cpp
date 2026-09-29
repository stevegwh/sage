#include "BloomPass.hpp"

#include "ResourceManager.hpp"
#include "ShaderPaths.hpp"

#include <algorithm>

namespace sage
{
    BloomPass::BloomPass(const int width, const int height)
    {
        blurShader = ResourceManager::GetInstance().ShaderLoad(nullptr, ShaderPath("custom/bloom_blur.fs").c_str());
        texelStepLocation = GetShaderLocation(blurShader, "texelStep");
        Resize(width, height);
    }

    BloomPass::~BloomPass()
    {
        if (mask.id != 0) UnloadRenderTexture(mask);
        if (blurTarget.id != 0) UnloadRenderTexture(blurTarget);
    }

    void BloomPass::Resize(const int width, const int height)
    {
        if (mask.id != 0) UnloadRenderTexture(mask);
        if (blurTarget.id != 0) UnloadRenderTexture(blurTarget);

        const int halfWidth = std::max(1, (width + 1) / 2);
        const int halfHeight = std::max(1, (height + 1) / 2);
        mask = LoadRenderTexture(halfWidth, halfHeight);
        blurTarget = LoadRenderTexture(halfWidth, halfHeight);
        SetTextureFilter(mask.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(blurTarget.texture, TEXTURE_FILTER_BILINEAR);
    }

    void BloomPass::Blur() const
    {
        const float horizontalStep[2] = {1.6f / static_cast<float>(mask.texture.width), 0.0f};
        BeginTextureMode(blurTarget);
        ClearBackground(BLACK);
        BeginShaderMode(blurShader);
        SetShaderValue(blurShader, texelStepLocation, horizontalStep, SHADER_UNIFORM_VEC2);
        DrawTextureRec(mask.texture, {0, 0, float(mask.texture.width), -float(mask.texture.height)}, {0, 0}, WHITE);
        EndShaderMode();
        EndTextureMode();

        const float verticalStep[2] = {0.0f, 1.6f / static_cast<float>(mask.texture.height)};
        BeginTextureMode(mask);
        ClearBackground(BLACK);
        BeginShaderMode(blurShader);
        SetShaderValue(blurShader, texelStepLocation, verticalStep, SHADER_UNIFORM_VEC2);
        DrawTextureRec(
            blurTarget.texture,
            {0, 0, float(blurTarget.texture.width), -float(blurTarget.texture.height)},
            {0, 0}, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
} // namespace sage

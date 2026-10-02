#include "BloomPass.hpp"
#include "engine/Colors.hpp"
#include <array>

#include "ResourceManager.hpp"
#include "ShaderPaths.hpp"

#include <algorithm>

namespace sage
{
    BloomPass::BloomPass(const int width, const int height)
        : blurShader(ResourceManager::GetInstance().ShaderLoad(std::nullopt, ShaderPath("custom/bloom_blur.fs"))),
          texelStepLocation(GetShaderLocation(blurShader, "texelStep"))
    {
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
        const std::array<float, 2> horizontalStep = {1.6f / static_cast<float>(mask.texture.width), 0.0f};
        BeginTextureMode(blurTarget);
        ClearBackground(sage::colors::BLACK_COLOR);
        BeginShaderMode(blurShader);
        SetShaderValue(blurShader, texelStepLocation, horizontalStep.data(), SHADER_UNIFORM_VEC2);
        DrawTextureRec(
            mask.texture,
            {.x = 0,
             .y = 0,
             .width = static_cast<float>(mask.texture.width),
             .height = -static_cast<float>(mask.texture.height)},
            {.x = 0, .y = 0},
            sage::colors::WHITE_COLOR);
        EndShaderMode();
        EndTextureMode();

        const std::array<float, 2> verticalStep = {0.0f, 1.6f / static_cast<float>(mask.texture.height)};
        BeginTextureMode(mask);
        ClearBackground(sage::colors::BLACK_COLOR);
        BeginShaderMode(blurShader);
        SetShaderValue(blurShader, texelStepLocation, verticalStep.data(), SHADER_UNIFORM_VEC2);
        DrawTextureRec(
            blurTarget.texture,
            {.x = 0,
             .y = 0,
             .width = static_cast<float>(blurTarget.texture.width),
             .height = -static_cast<float>(blurTarget.texture.height)},
            {.x = 0, .y = 0},
            sage::colors::WHITE_COLOR);
        EndShaderMode();
        EndTextureMode();
    }
} // namespace sage

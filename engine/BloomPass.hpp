#pragma once

#include "raylib.h"

namespace sage
{
    class BloomPass
    {
        RenderTexture mask{};
        RenderTexture blurTarget{};
        Shader blurShader{};
        int texelStepLocation = -1;

      public:
        BloomPass(int width, int height);
        ~BloomPass();
        BloomPass(const BloomPass&) = delete;
        BloomPass& operator=(const BloomPass&) = delete;

        void Resize(int width, int height);
        void Blur() const;
        [[nodiscard]] RenderTexture MaskTarget() const { return mask; }
        [[nodiscard]] Texture2D Texture() const { return mask.texture; }
    };
} // namespace sage

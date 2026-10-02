//
// Created by steve on 21/11/2024.
//

#pragma once

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace sage
{
    struct UberShaderComponent
    {

        enum class Flags : std::uint32_t
        {
            Skinned = 1 << 0,
            Lit = 1 << 1,
            EmissiveTexture = 1 << 2,
            EmissiveCol = 1 << 3,
            Grayscale = 1 << 4
        };

        // uint32_t flags{};
        Shader shader{};
        int litLoc{};
        int skinnedLoc{};
        int hasEmissiveTexLoc{}; // The boolean, not the texture
        int hasEmissiveColLoc{};
        int grayscaleLoc{};
        int colEmissiveLoc{}; // Loc of the color itself (not the bool)

        std::vector<uint32_t> materialMap;

        void SetShaderBools(unsigned int materialIdx) const
        {
            auto setBool = [&](int loc, Flags flag) {
                int value = HasFlag(materialIdx, flag) ? 1 : 0;
                SetShaderValue(shader, loc, &value, SHADER_UNIFORM_INT);
            };

            setBool(skinnedLoc, Flags::Skinned);
            setBool(litLoc, Flags::Lit);
            setBool(hasEmissiveTexLoc, Flags::EmissiveTexture);
            setBool(hasEmissiveColLoc, Flags::EmissiveCol);
            setBool(grayscaleLoc, Flags::Grayscale);
        }

        void SetShaderBools() const
        {
            for (unsigned int i = 0; i < materialMap.size(); ++i)
            {
                SetShaderBools(i);
            }
        }

        [[nodiscard]] bool HasFlag(unsigned int idx, Flags flag) const
        {
            return materialMap.at(idx) & static_cast<std::uint32_t>(flag);
        }

        void SetFlag(unsigned int idx, Flags flag)
        {
            materialMap.at(idx) |= static_cast<std::uint32_t>(flag);
        }

        void ClearFlag(unsigned int idx, Flags flag)
        {
            materialMap.at(idx) &= ~static_cast<std::uint32_t>(flag);
        }

        void SetFlagAll(Flags flag)
        {
            for (unsigned int& i : materialMap)
            {
                i |= static_cast<std::uint32_t>(flag);
            }
        }

        void ClearFlagAll(Flags flag)
        {
            for (unsigned int& i : materialMap)
            {
                i &= ~static_cast<std::uint32_t>(flag);
            }
        }

        explicit UberShaderComponent(unsigned int materialCount)
        {
            materialMap.resize(materialCount);
        }
    };

} // namespace sage

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

        enum Flags
        {
            Skinned = 1 << 0,
            Lit = 1 << 1,
            EmissiveTexture = 1 << 2,
            EmissiveCol = 1 << 3
        };

        // uint32_t flags{};
        Shader shader{};
        int litLoc{};
        int skinnedLoc{};
        int hasEmissiveTexLoc{}; // The boolean, not the texture
        int hasEmissiveColLoc{};
        int colEmissiveLoc{}; // Loc of the color itself (not the bool)

        std::vector<uint32_t> materialMap;

        void SetShaderBools(unsigned int materialIdx) const
        {
            auto setBool = [&](int loc, Flags flag) {
                int value = HasFlag(materialIdx, flag) ? 1 : 0;
                SetShaderValue(shader, loc, &value, RL_SHADER_UNIFORM_INT);
            };

            setBool(skinnedLoc, Skinned);
            setBool(litLoc, Lit);
            setBool(hasEmissiveTexLoc, EmissiveTexture);
            setBool(hasEmissiveColLoc, EmissiveCol);
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
            return materialMap.at(idx) & flag;
        }

        void SetFlag(unsigned int idx, Flags flag)
        {
            materialMap.at(idx) |= flag;
        }

        void ClearFlag(unsigned int idx, Flags flag)
        {
            materialMap.at(idx) &= ~flag;
        }

        void SetFlagAll(Flags flag)
        {
            for (unsigned int& i : materialMap)
            {
                i |= flag;
            }
        }

        void ClearFlagAll(Flags flag)
        {
            for (unsigned int& i : materialMap)
            {
                i &= ~flag;
            }
        }

        explicit UberShaderComponent(unsigned int materialCount)
        {
            materialMap.resize(materialCount);
        }
    };

} // namespace sage

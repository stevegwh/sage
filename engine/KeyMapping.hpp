//
// Created by steve on 05/05/2024.
//

#pragma once

#include "cereal/cereal.hpp"
#include "raylib.h"
#include "Serializer.hpp"

namespace sage
{
    struct KeyMapping
    {
        KeyboardKey keyA = KEY_A;
        KeyboardKey keyB = KEY_B;
        KeyboardKey keyC = KEY_C;
        KeyboardKey keyD = KEY_D;
        KeyboardKey keyE = KEY_E;
        KeyboardKey keyF = KEY_F;
        KeyboardKey keyG = KEY_G;
        KeyboardKey keyH = KEY_H;
        KeyboardKey keyI = KEY_I;
        KeyboardKey keyJ = KEY_J;
        KeyboardKey keyK = KEY_K;
        KeyboardKey keyL = KEY_L;
        KeyboardKey keyM = KEY_M;
        KeyboardKey keyN = KEY_N;
        KeyboardKey keyO = KEY_O;
        KeyboardKey keyP = KEY_P;
        KeyboardKey keyQ = KEY_Q;
        KeyboardKey keyR = KEY_R;
        KeyboardKey keyS = KEY_S;
        KeyboardKey keyT = KEY_T;
        KeyboardKey keyU = KEY_U;
        KeyboardKey keyV = KEY_V;
        KeyboardKey keyW = KEY_W;
        KeyboardKey keyX = KEY_X;
        KeyboardKey keyY = KEY_Y;
        KeyboardKey keyZ = KEY_Z;
        KeyboardKey keyEscape = KEY_ESCAPE;
        KeyboardKey keySpace = KEY_SPACE;
        KeyboardKey keyDelete = KEY_DELETE;
        KeyboardKey keyOne = KEY_ONE;
        KeyboardKey keyTwo = KEY_TWO;
        KeyboardKey keyThree = KEY_THREE;
        KeyboardKey keyFour = KEY_FOUR;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("keyA", keyA),
                cereal::make_nvp("keyB", keyB),
                cereal::make_nvp("keyC", keyC),
                cereal::make_nvp("keyD", keyD),
                cereal::make_nvp("keyE", keyE),
                cereal::make_nvp("keyF", keyF),
                cereal::make_nvp("keyG", keyG),
                cereal::make_nvp("keyH", keyH),
                cereal::make_nvp("keyI", keyI),
                cereal::make_nvp("keyJ", keyJ),
                cereal::make_nvp("keyK", keyK),
                cereal::make_nvp("keyL", keyL),
                cereal::make_nvp("keyM", keyM),
                cereal::make_nvp("keyN", keyN),
                cereal::make_nvp("keyO", keyO),
                cereal::make_nvp("keyP", keyP),
                cereal::make_nvp("keyQ", keyQ),
                cereal::make_nvp("keyR", keyR),
                cereal::make_nvp("keyS", keyS),
                cereal::make_nvp("keyT", keyT),
                cereal::make_nvp("keyU", keyU),
                cereal::make_nvp("keyV", keyV),
                cereal::make_nvp("keyW", keyW),
                cereal::make_nvp("keyX", keyX),
                cereal::make_nvp("keyY", keyY),
                cereal::make_nvp("keyZ", keyZ),
                cereal::make_nvp("keyEscape", keyEscape),
                cereal::make_nvp("keySpace", keySpace),
                cereal::make_nvp("keyDelete", keyDelete),
                cereal::make_nvp("keyOne", keyOne),
                cereal::make_nvp("keyTwo", keyTwo),
                cereal::make_nvp("keyThree", keyThree),
                cereal::make_nvp("keyFour", keyFour));
        }

        KeyMapping()
        {
            serializer::DeserializeJsonFile<KeyMapping>("resources/keybinding.json", *this);
        }
    };
} // namespace sage

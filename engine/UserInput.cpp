//
// Created by Steve Wheeler on 18/02/2024
//

#include "UserInput.hpp"

#include <array>
#include <cassert>

namespace sage
{
    bool IsMetaKeyDown()
    {
        return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
               IsKeyDown(KEY_RIGHT_SUPER);
    }

    void UserInput::toggleFullScreen() const
    {
        settings->toggleFullScreenRequested = true;
    }

    void UserInput::ListenForInput() const
    {
        if (IsKeyPressed(KEY_ENTER) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)))
        {
            toggleFullScreen();
        }

        struct KeyEvents
        {
            int key;
            const Event<>* pressed;
            const Event<>* up;
        };

        const std::array bindings = {
            KeyEvents{.key = keyMapping->keyA, .pressed = &keyAPressed, .up = &keyAUp},
            KeyEvents{.key = keyMapping->keyB, .pressed = &keyBPressed, .up = &keyBUp},
            KeyEvents{.key = keyMapping->keyC, .pressed = &keyCPressed, .up = &keyCUp},
            KeyEvents{.key = keyMapping->keyD, .pressed = &keyDPressed, .up = &keyDUp},
            KeyEvents{.key = keyMapping->keyE, .pressed = &keyEPressed, .up = &keyEUp},
            KeyEvents{.key = keyMapping->keyF, .pressed = &keyFPressed, .up = &keyFUp},
            KeyEvents{.key = keyMapping->keyG, .pressed = &keyGPressed, .up = &keyGUp},
            KeyEvents{.key = keyMapping->keyH, .pressed = &keyHPressed, .up = &keyHUp},
            KeyEvents{.key = keyMapping->keyI, .pressed = &keyIPressed, .up = &keyIUp},
            KeyEvents{.key = keyMapping->keyJ, .pressed = &keyJPressed, .up = &keyJUp},
            KeyEvents{.key = keyMapping->keyK, .pressed = &keyKPressed, .up = &keyKUp},
            KeyEvents{.key = keyMapping->keyL, .pressed = &keyLPressed, .up = &keyLUp},
            KeyEvents{.key = keyMapping->keyM, .pressed = &keyMPressed, .up = &keyMUp},
            KeyEvents{.key = keyMapping->keyN, .pressed = &keyNPressed, .up = &keyNUp},
            KeyEvents{.key = keyMapping->keyO, .pressed = &keyOPressed, .up = &keyOUp},
            KeyEvents{.key = keyMapping->keyP, .pressed = &keyPPressed, .up = &keyPUp},
            KeyEvents{.key = keyMapping->keyQ, .pressed = &keyQPressed, .up = &keyQUp},
            KeyEvents{.key = keyMapping->keyR, .pressed = &keyRPressed, .up = &keyRUp},
            KeyEvents{.key = keyMapping->keyS, .pressed = &keySPressed, .up = &keySUp},
            KeyEvents{.key = keyMapping->keyT, .pressed = &keyTPressed, .up = &keyTUp},
            KeyEvents{.key = keyMapping->keyU, .pressed = &keyUPressed, .up = &keyUUp},
            KeyEvents{.key = keyMapping->keyV, .pressed = &keyVPressed, .up = &keyVUp},
            KeyEvents{.key = keyMapping->keyW, .pressed = &keyWPressed, .up = &keyWUp},
            KeyEvents{.key = keyMapping->keyX, .pressed = &keyXPressed, .up = &keyXUp},
            KeyEvents{.key = keyMapping->keyY, .pressed = &keyYPressed, .up = &keyYUp},
            KeyEvents{.key = keyMapping->keyZ, .pressed = &keyZPressed, .up = &keyZUp},
            KeyEvents{.key = keyMapping->keyEscape, .pressed = &keyEscapePressed, .up = &keyEscapeUp},
            KeyEvents{.key = keyMapping->keySpace, .pressed = &keySpacePressed, .up = &keySpaceUp},
            KeyEvents{.key = keyMapping->keyDelete, .pressed = &keyDeletePressed, .up = &keyDeleteUp},
            KeyEvents{.key = keyMapping->keyOne, .pressed = &keyOnePressed, .up = &keyOneUp},
            KeyEvents{.key = keyMapping->keyTwo, .pressed = &keyTwoPressed, .up = &keyTwoUp},
            KeyEvents{.key = keyMapping->keyThree, .pressed = &keyThreePressed, .up = &keyThreeUp},
            KeyEvents{.key = keyMapping->keyFour, .pressed = &keyFourPressed, .up = &keyFourUp},
        };

        for (const auto& [key, pressed, up] : bindings)
        {
            if (IsKeyPressed(key)) pressed->Publish();
            if (IsKeyUp(key)) up->Publish();
        }
    }

    UserInput::UserInput(KeyMapping* _keyMapping, Settings* _settings)
        : keyMapping(_keyMapping), settings(_settings)
    {
        assert(settings != nullptr);
        assert(keyMapping != nullptr);
    }
} // namespace sage

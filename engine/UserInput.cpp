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
            KeyEvents{keyMapping->keyA, &keyAPressed, &keyAUp},
            KeyEvents{keyMapping->keyB, &keyBPressed, &keyBUp},
            KeyEvents{keyMapping->keyC, &keyCPressed, &keyCUp},
            KeyEvents{keyMapping->keyD, &keyDPressed, &keyDUp},
            KeyEvents{keyMapping->keyE, &keyEPressed, &keyEUp},
            KeyEvents{keyMapping->keyF, &keyFPressed, &keyFUp},
            KeyEvents{keyMapping->keyG, &keyGPressed, &keyGUp},
            KeyEvents{keyMapping->keyH, &keyHPressed, &keyHUp},
            KeyEvents{keyMapping->keyI, &keyIPressed, &keyIUp},
            KeyEvents{keyMapping->keyJ, &keyJPressed, &keyJUp},
            KeyEvents{keyMapping->keyK, &keyKPressed, &keyKUp},
            KeyEvents{keyMapping->keyL, &keyLPressed, &keyLUp},
            KeyEvents{keyMapping->keyM, &keyMPressed, &keyMUp},
            KeyEvents{keyMapping->keyN, &keyNPressed, &keyNUp},
            KeyEvents{keyMapping->keyO, &keyOPressed, &keyOUp},
            KeyEvents{keyMapping->keyP, &keyPPressed, &keyPUp},
            KeyEvents{keyMapping->keyQ, &keyQPressed, &keyQUp},
            KeyEvents{keyMapping->keyR, &keyRPressed, &keyRUp},
            KeyEvents{keyMapping->keyS, &keySPressed, &keySUp},
            KeyEvents{keyMapping->keyT, &keyTPressed, &keyTUp},
            KeyEvents{keyMapping->keyU, &keyUPressed, &keyUUp},
            KeyEvents{keyMapping->keyV, &keyVPressed, &keyVUp},
            KeyEvents{keyMapping->keyW, &keyWPressed, &keyWUp},
            KeyEvents{keyMapping->keyX, &keyXPressed, &keyXUp},
            KeyEvents{keyMapping->keyY, &keyYPressed, &keyYUp},
            KeyEvents{keyMapping->keyZ, &keyZPressed, &keyZUp},
            KeyEvents{keyMapping->keyEscape, &keyEscapePressed, &keyEscapeUp},
            KeyEvents{keyMapping->keySpace, &keySpacePressed, &keySpaceUp},
            KeyEvents{keyMapping->keyDelete, &keyDeletePressed, &keyDeleteUp},
            KeyEvents{keyMapping->keyOne, &keyOnePressed, &keyOneUp},
            KeyEvents{keyMapping->keyTwo, &keyTwoPressed, &keyTwoUp},
            KeyEvents{keyMapping->keyThree, &keyThreePressed, &keyThreeUp},
            KeyEvents{keyMapping->keyFour, &keyFourPressed, &keyFourUp},
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

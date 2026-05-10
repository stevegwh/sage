//
// Created by Steve Wheeler on 06/04/2024.
//

#pragma once

#include "../AnimationId.hpp"
#include "../Event.hpp"
#include "../ResourceManager.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace sage
{

    struct AnimationParams
    {
        AnimationId animationId{};
        int animSpeed = 1;
        bool oneShot = false;
        float animationDelay = 0;
    };

    // TODO: Use a timer for animation delay
    struct Animation
    {
        struct AnimData
        {
            unsigned int index = 0;
            unsigned int currentFrame = 0;
            unsigned int lastFrame = 0;
            int speed = 1;
        };

        std::unordered_map<AnimationId, int> animationMap;
        ModelAnimation* animations;
        int animsCount;

        bool oneShotMode = false;
        AnimData current{};

        Event<entt::entity> onAnimationEnd{};
        Event<entt::entity> onAnimationStart{};
        Event<entt::entity> onAnimationUpdated{};

        void ChangeAnimationByParams(AnimationParams params);
        void ChangeAnimationById(AnimationId animationId, int _animSpeed);
        void ChangeAnimationById(AnimationId animationId);
        void ChangeAnimation(int index);
        void ChangeAnimation(int index, int _animSpeed);

        void PlayOneShot(AnimationId animationId, int _animSpeed);
        void PlayOneShot(int index, int _animSpeed);
        void RestoreAfterOneShot();

        Animation(const Animation&) = delete;
        Animation& operator=(const Animation&) = delete;
        explicit Animation(const std::string& id);

      private:
        AnimData prev{};
    };
} // namespace sage

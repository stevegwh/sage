//
// Created by Steve Wheeler on 06/04/2024.
//

#include "Animation.hpp"

namespace sage
{

    void Animation::ChangeAnimationByParams(AnimationParams params)
    {
        if (params.oneShot)
        {
            PlayOneShot(params.animationId, params.animSpeed);
            return;
        }
        ChangeAnimationById(params.animationId, params.animSpeed);
    }

    void Animation::ChangeAnimationById(AnimationId animationId, int _animSpeed)
    {
        assert(animationMap.contains(animationId));
        ChangeAnimation(animationMap.at(animationId), _animSpeed);
    }

    void Animation::ChangeAnimationById(AnimationId animationId)
    {
        ChangeAnimationById(animationId, 1);
    }

    void Animation::ChangeAnimation(int index)
    {
        ChangeAnimation(index, 1);
    }

    void Animation::ChangeAnimation(int index, int _animSpeed)
    {
        auto& a = oneShotMode ? prev : current;

        if (a.index == index) return;
        a.speed = _animSpeed;
        a.index = index;
        a.currentFrame = 0;
    }

    void Animation::PlayOneShot(AnimationId animationId, int _animSpeed)
    {
        PlayOneShot(animationMap.at(animationId), _animSpeed);
    }

    void Animation::PlayOneShot(int index, int _animSpeed)
    {
        if (!oneShotMode)
        {
            oneShotMode = true;
            prev = current;
        }

        current.index = index;
        current.speed = _animSpeed;
        current.currentFrame = 0;
        current.lastFrame = 0;
    }

    void Animation::RestoreAfterOneShot()
    {
        oneShotMode = false;
        current = prev;
        prev = {};
    }

    Animation::Animation(const std::string& id)
    {
        animsCount = 0;
        animations = ResourceManager::GetInstance().GetModelAnimation(id, &animsCount);
    }
} // namespace sage

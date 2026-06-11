//
// Created by Steve Wheeler on 06/04/2024.
//

#include "Animation.hpp"

namespace sage
{

    int Animation::GetClipIndex(const std::string_view clipName) const
    {
        for (std::size_t i = 0; i < clipNames.size(); ++i)
        {
            if (clipName == clipNames[i]) return static_cast<int>(i);
        }
        return -1;
    }

    const char* Animation::GetClipName(const unsigned int index) const
    {
        if (index >= clipNames.size()) return "";
        return clipNames[index].c_str();
    }

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
        // In one-shot mode this only changes the clip restored later, not the visible pose.
        if (!oneShotMode) StartBlend();
        a.speed = _animSpeed;
        a.index = index;
        a.currentFrame = 0;
    }

    bool Animation::ChangeAnimationByName(const std::string_view clipName)
    {
        return ChangeAnimationByName(clipName, 1);
    }

    bool Animation::ChangeAnimationByName(const std::string_view clipName, const int _animSpeed)
    {
        const int index = GetClipIndex(clipName);
        if (index < 0) return false;
        ChangeAnimation(index, _animSpeed);
        return true;
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

        StartBlend();
        current.index = index;
        current.speed = _animSpeed;
        current.currentFrame = 0;
        current.lastFrame = 0;
    }

    bool Animation::PlayOneShotByName(const std::string_view clipName, const int _animSpeed)
    {
        const int index = GetClipIndex(clipName);
        if (index < 0) return false;
        PlayOneShot(index, _animSpeed);
        return true;
    }

    void Animation::RestoreAfterOneShot()
    {
        oneShotMode = false;
        StartBlend();
        current = prev;
        prev = {};
    }

    void Animation::StartBlend()
    {
        if (blendDuration <= 0.0f || animsCount == 0)
        {
            blending = false;
            return;
        }
        // Interrupting a running blend snapshots the clip we were fading to; a
        // mid-blend mix can't be captured in AnimData, so a brief pop is possible.
        blendFrom = current;
        blending = true;
        blendTimer = blendDuration;
    }

    void Animation::LoadAnimations()
    {
        animations = nullptr;
        animsCount = 0;
        clipNames.clear();
        if (!modelKey.empty())
        {
            animations = ResourceManager::GetInstance().GetModelAnimation(modelKey, &animsCount);
        }
        clipNames.reserve(animsCount);
        for (int i = 0; i < animsCount; ++i)
        {
            clipNames.emplace_back(animations[i].name);
        }
        // A saved clip index can outlive a model re-export; fall back to clip 0 rather than read past the array.
        if (current.index >= static_cast<unsigned int>(animsCount)) current = {};
        // blendFrom may also point at a clip that no longer exists.
        blending = false;
        blendTimer = 0.0f;
        blendFrom = {};
    }

    Animation::Animation(const std::string& id) : modelKey(id)
    {
        LoadAnimations();
    }
} // namespace sage

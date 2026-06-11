//
// Created by Steve Wheeler on 06/04/2024.
//

#include "AnimationSystem.hpp"
#include "components/Animation.hpp"
#include "components/MoveableActor.hpp"
#include "components/Renderable.hpp"
#include "Event.hpp"

#include "raymath.h"

namespace sage
{

    void AnimationSystem::Update() const
    {
        // Movement drives the base clip; ChangeAnimationByName is a no-op when the
        // clip is already active and leaves the animation untouched on unknown names.
        for (const auto& view = registry->view<Animation, MoveableActor>(); auto& entity : view)
        {
            auto& animation = registry->get<Animation>(entity);
            const auto& moveable = registry->get<MoveableActor>(entity);
            animation.ChangeAnimationByName(moveable.IsMoving() ? moveable.moveClip : moveable.idleClip);
        }

        for (const auto& view = registry->view<Animation, Renderable>(); auto& entity : view)
        {
            auto& animation = registry->get<Animation>(entity);
            if (animation.animations == nullptr || animation.animsCount == 0) continue;
            auto& renderable = registry->get<Renderable>(entity);
            auto& animData = animation.current;
            const ModelAnimation& anim = animation.animations[animData.index];

            if (animData.currentFrame == 0 || animData.currentFrame < animData.lastFrame)
            {
                animation.onAnimationStart.Publish(entity);
            }

            bool finalFrame = animData.currentFrame + animData.speed >= anim.frameCount;
            animData.lastFrame = animData.currentFrame;
            animData.currentFrame = (animData.currentFrame + animData.speed) % anim.frameCount;

            if (animation.blending)
            {
                animation.blendTimer -= GetFrameTime();
                if (animation.blendTimer <= 0.0f)
                {
                    animation.blending = false;
                }
                else
                {
                    // The outgoing clip keeps playing while it fades out.
                    auto& from = animation.blendFrom;
                    const ModelAnimation& fromAnim = animation.animations[from.index];
                    from.lastFrame = from.currentFrame;
                    from.currentFrame = (from.currentFrame + from.speed) % fromAnim.frameCount;
                }
            }

            if (animation.blending)
            {
                // Clamp in case blendDuration was changed (or zeroed) mid-blend.
                const float t = Clamp(1.0f - animation.blendTimer / animation.blendDuration, 0.0f, 1.0f);
                renderable.GetModel()->UpdateAnimationBlended(
                    animation.animations[animation.blendFrom.index],
                    animation.blendFrom.currentFrame,
                    anim,
                    animData.currentFrame,
                    t);
            }
            else
            {
                renderable.GetModel()->UpdateAnimation(anim, animData.currentFrame);
            }

            if (finalFrame) // Must be at end, as end of death animations can result in entities being destroyed
            {
                animation.onAnimationEnd.Publish(entity);
                if (animation.oneShotMode)
                {
                    animation.RestoreAfterOneShot();
                }
            }
            animation.onAnimationUpdated.Publish(entity);
        }
    }

    void AnimationSystem::Draw()
    {
    }

    AnimationSystem::AnimationSystem(entt::registry* _registry) : registry(_registry)
    {
    }
} // namespace sage

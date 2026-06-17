//
// Created by Steve Wheeler on 06/04/2024.
//

#include "AnimationSystem.hpp"
#include "components/Animation.hpp"
#include "components/MoveableActor.hpp"
#include "components/Renderable.hpp"
#include "Event.hpp"

#include "raymath.h"

#include <cstdint>
#include <iostream>
#include <unordered_set>

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
            animation.ChangeAnimationByName(moveable.IsWalking() ? moveable.moveClip : moveable.idleClip);
        }

        for (const auto& view = registry->view<Animation, Renderable>(); auto& entity : view)
        {
            auto& animation = registry->get<Animation>(entity);
            if (animation.animations == nullptr || animation.animsCount == 0) continue;
            auto& renderable = registry->get<Renderable>(entity);
            auto& animData = animation.current;
            const ModelAnimation& anim = animation.animations[animData.index];

            // Guard against a model/animation skeleton mismatch: if a model is
            // re-exported with a different bone count but its cached/packed
            // ModelAnimation is stale, raylib's UpdateModelAnimationBones asserts
            // boneCount equality and aborts the whole game (one bad entity kills
            // every other animation). Skip and warn once per entity instead.
            {
                const Model& rl = renderable.GetModel()->GetRlModel();
                const int modelBones = (rl.meshCount > 0) ? rl.meshes[0].boneCount : rl.boneCount;
                if (modelBones != anim.boneCount)
                {
                    static std::unordered_set<std::uint32_t> warned;
                    if (warned.insert(static_cast<std::uint32_t>(entity)).second)
                    {
                        std::cerr << "AnimationSystem: skipping entity " << static_cast<std::uint32_t>(entity)
                                  << " - model bone count (" << modelBones
                                  << ") does not match animation '" << animation.GetClipName(animData.index)
                                  << "' (" << anim.boneCount
                                  << "). The animation data is stale for this model; re-pack/re-import it.\n";
                    }
                    continue;
                }
            }

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

//
// Created by Steve Wheeler on 06/04/2024.
//

#pragma once

#include "../AnimationId.hpp"
#include "../Event.hpp"
#include "../ResourceManager.hpp"

#include "cereal/types/string.hpp"
#include "cereal/types/unordered_map.hpp"
#include "entt/entt.hpp"
#include "raylib.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sage
{
    struct Renderable;

    struct AnimationParams
    {
        AnimationId animationId{};
        int animSpeed = 1;
        bool oneShot = false;
    };

    struct Animation
    {
        struct AnimData
        {
            unsigned int index = 0;
            unsigned int currentFrame = 0;
            unsigned int lastFrame = 0;
            int speed = 1;
        };

        // Transparent hash so clipIndexByName can be queried by std::string_view without allocating.
        struct TransparentStringHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }
        };

        std::string modelKey;
        // Legacy AnimationId→clip-index map driving ChangeAnimationById/PlayOneShot(AnimationId). The
        // current API is the GLB-name path (ChangeAnimationByName, used by scripts and AnimationSystem);
        // this is retained only because it is serialized. Prefer the *ByName methods for new code.
        std::unordered_map<AnimationId, int> animationMap;
        // Derived from the GLB on load (Blender NLA track names), index-aligned with `animations`.
        std::vector<std::string> clipNames;
        // Derived alongside clipNames: name → index, for O(1) GetClipIndex (called per moving actor
        // per frame by AnimationSystem). Not serialized; rebuilt by LoadAnimations.
        std::unordered_map<std::string, int, TransparentStringHash, std::equal_to<>> clipIndexByName;
        ModelAnimation* animations = nullptr;
        int animsCount = 0;

        bool oneShotMode = false;
        AnimData current{};

        // Clip switches cross-fade over this many seconds; 0 snaps instantly.
        float blendDuration = 0.2f;
        // Runtime blend state, never serialized. blendFrom keeps advancing while it fades out.
        bool blending = false;
        float blendTimer = 0.0f;
        AnimData blendFrom{};

        Event<entt::entity> onAnimationEnd{};
        Event<entt::entity> onAnimationStart{};
        Event<entt::entity> onAnimationUpdated{};

        // Clip names come from the GLB (Blender NLA track names). Returns -1 when no clip matches.
        [[nodiscard]] int GetClipIndex(std::string_view clipName) const;
        [[nodiscard]] const char* GetClipName(unsigned int index) const;

        void ChangeAnimationByParams(AnimationParams params);
        void ChangeAnimationById(AnimationId animationId, int _animSpeed);
        void ChangeAnimationById(AnimationId animationId);
        void ChangeAnimation(int index);
        void ChangeAnimation(int index, int _animSpeed);
        bool ChangeAnimationByName(std::string_view clipName);
        bool ChangeAnimationByName(std::string_view clipName, int _animSpeed);

        void PlayOneShot(AnimationId animationId, int _animSpeed);
        void PlayOneShot(int index, int _animSpeed);
        bool PlayOneShotByName(std::string_view clipName, int _animSpeed);
        bool PlayOneShotByName(std::string_view clipName)
        {
            return PlayOneShotByName(clipName, 1);
        }
        void RestoreAfterOneShot();

        template <class Archive>
        void save(Archive& archive) const
        {
            archive(modelKey, animationMap, current.index, current.speed);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            archive(modelKey, animationMap, current.index, current.speed);
            LoadAnimations();
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Renderable>();
            for (std::size_t n = 0; n < clipNames.size(); ++n)
            {
                i.field("Clip " + std::to_string(n), clipNames[n], false);
            }
        }

        template <class Api>
        static void define_script_api(Api& api)
        {
            api.event("OnAnimationStarted", &Animation::onAnimationStart);
            api.event("OnAnimationEnded", &Animation::onAnimationEnd);
            api.event("OnAnimationUpdated", &Animation::onAnimationUpdated);
            api.method(
                "Play",
                static_cast<bool (Animation::*)(std::string_view)>(&Animation::ChangeAnimationByName),
                {"clip"});
            api.method(
                "Play",
                static_cast<bool (Animation::*)(std::string_view, int)>(&Animation::ChangeAnimationByName),
                {"clip", "speed"});
            api.method(
                "PlayOneShot",
                static_cast<bool (Animation::*)(std::string_view)>(&Animation::PlayOneShotByName),
                {"clip"});
            api.method(
                "PlayOneShot",
                static_cast<bool (Animation::*)(std::string_view, int)>(&Animation::PlayOneShotByName),
                {"clip", "speed"});
            api.readonly("ClipCount", &Animation::animsCount);
            api.property(
                "BlendDuration",
                [](const Animation& animation) { return animation.blendDuration; },
                [](Animation& animation, const float seconds) {
                    animation.blendDuration = std::max(0.0f, seconds);
                });
        }

        Animation() = default;
        Animation(const Animation&) = delete;
        Animation& operator=(const Animation&) = delete;
        explicit Animation(const std::string& id);

      private:
        void LoadAnimations();
        void StartBlend();
        AnimData prev{};
    };
} // namespace sage

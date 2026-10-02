//
// Created by steve on 04/01/2025.
//

#pragma once

#include "raylib.h"

#include <string>
#include <unordered_set>

namespace sage
{

    class AudioManager
    {
        std::unordered_set<std::string> musicPlaying;
        std::unordered_set<std::string> sfxPlaying;

      public:
        Music PlayMusic(const std::string& name);
        Sound PlaySFX(const std::string& name);
        void Update() const;
        AudioManager(const AudioManager&) = delete;
        AudioManager& operator=(const AudioManager&) = delete;
        AudioManager(AudioManager&&) = delete;
        AudioManager& operator=(AudioManager&&) = delete;
        ~AudioManager();
        AudioManager();
    };

} // namespace sage

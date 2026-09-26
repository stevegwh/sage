//
// Created by Steve Wheeler on 06/05/2024.
//

#pragma once

#include <string>

#include "cereal/cereal.hpp"
#include "raylib.h"

namespace sage
{
    struct EditorSettings
    {
        std::string resourcePath = "resources/";
        std::string lastOpenedMap;
        std::string lastVisitedDirectory;

        EditorSettings()
        {
            lastVisitedDirectory = GetWorkingDirectory();
        }

        template <class Archive>
        void save(Archive& archive) const
        {
            archive(cereal::make_nvp("resourcePath", resourcePath), cereal::make_nvp("lastOpenedMap", lastOpenedMap), cereal::make_nvp("lastVisitedDirectory", lastVisitedDirectory));
        }

        template <class Archive>
        void load(Archive& archive)
        {
            archive(cereal::make_nvp("resourcePath", resourcePath), cereal::make_nvp("lastOpenedMap", lastOpenedMap), cereal::make_nvp("lastVisitedDirectory", lastVisitedDirectory));
        }
    };
} // namespace sage

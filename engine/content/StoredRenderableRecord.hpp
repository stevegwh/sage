#pragma once

#include "cereal/archives/json.hpp"
#include "engine/Colors.hpp"
#include "raylib.h"

#include <cstdint>
#include <string>
#include <type_traits>

namespace sage::content_binary
{
    struct StoredRenderableRecord
    {
        std::uint8_t kind = 0;
        std::string key;
        Matrix initialTransform{};
        bool active = true;
        Color hint = sage::colors::WHITE_COLOR;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("kind", kind),
                cereal::make_nvp("key", key),
                cereal::make_nvp("initialTransform", initialTransform));
            if constexpr (
                std::is_same_v<Archive, cereal::JSONInputArchive> ||
                std::is_same_v<Archive, cereal::JSONOutputArchive>)
                archive(cereal::make_nvp("active", active), cereal::make_nvp("hint", hint));
        }
    };

} // namespace sage::content_binary

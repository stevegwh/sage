#pragma once

#include "engine/content/ContentDocument.hpp"
#include "engine/EditorLayoutTags.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace sage::editor
{
    inline constexpr std::string_view SPAWN_POINT_TAG = editor_layout::SPAWN_POINT_SCENE_TAG;

    struct EditorMapEntity
    {
    };

    struct EditorMapBase
    {
    };

    using PersistentEntityId = sage::PersistentEntityId;

    struct AssetReference
    {
        std::string assetKey;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(assetKey);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("Asset Key", assetKey, false);
        }
    };
} // namespace sage::editor

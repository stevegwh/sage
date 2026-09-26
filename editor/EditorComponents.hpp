#pragma once

#include "engine/EditorLayoutTags.hpp"
#include "engine/content/ContentDocument.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace sage::editor
{
    inline constexpr std::string_view SpawnPointTag = editor_layout::SpawnPointSceneTag;

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

//
// Asset source-file renaming: validates a requested file name, renames the model
// file (and its sibling defaults file) on disk, updates the ResourceManager and
// catalog, and repoints every AssetReference / Renderable that used the old key.
// Lives apart from EditorScene because it is self-contained filesystem + registry
// work; the scene only handles the editor-side follow-up on success.
//

#pragma once

#include "EditorGui.hpp"

#include "entt/entt.hpp"

#include <cstddef>
#include <string>

namespace sage::editor
{
    class EditorAssetCatalog;

    // On failure nothing is changed and the result carries an explanatory message.
    // On success result.renamed is true and updatedEntry holds the new catalog entry.
    [[nodiscard]] EditorGui::AssetRenameResult RenameAssetFile(
        entt::registry& registry,
        EditorAssetCatalog& catalog,
        std::size_t index,
        const std::string& requestedFileName);
} // namespace sage::editor

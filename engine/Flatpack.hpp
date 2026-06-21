#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sage
{
    struct FlatpackCatalogEntry
    {
        std::string displayName;
        std::filesystem::path path;
    };

    struct FlatpackInstance
    {
        entt::entity root = entt::null;
        std::vector<entt::entity> entities;

        [[nodiscard]] explicit operator bool() const
        {
            return root != entt::null;
        }
    };

    [[nodiscard]] bool IsFlatpackFile(const char* path);

    // Serializes the transform subtree rooted at `root`. The root is rebased to
    // the origin and every entity's optional Archetype is persisted by id.
    bool SaveFlatpack(entt::registry& source, entt::entity root, const char* path);

    // Creates a fresh instance in `destination`. This runtime API deliberately
    // adds no editor-only marker components; callers receive the full entity set
    // so they can add their own context-specific state.
    [[nodiscard]] FlatpackInstance LoadFlatpack(
        entt::registry& destination, const char* path, Vector3 anchorWorldPos);

    [[nodiscard]] std::vector<FlatpackCatalogEntry> ListFlatpacks(
        const std::filesystem::path& directory);
} // namespace sage

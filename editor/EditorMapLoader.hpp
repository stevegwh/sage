#pragma once

#include "entt/entt.hpp"

#include <functional>
#include <vector>

namespace sage::editor
{
    // Loads/saves the editor-only layout map format. This is intentionally
    // separate from the game/respacker map .bin format.
    [[nodiscard]] bool IsEditorLayoutMap(const char* path);
    bool LoadMap(
        entt::registry& destination, const char* path, const std::function<void()>& updateLoadingScreen = {});
    bool SaveMap(entt::registry& source, const char* path);
    bool SaveMap(entt::registry& source, const char* path, const std::vector<entt::entity>& hierarchyOrder);
} // namespace sage::editor

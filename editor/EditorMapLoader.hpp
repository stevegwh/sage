#pragma once

#include "entt/entt.hpp"

#include <vector>

namespace sage::editor
{
    class InspectorRegistry;
    // Loads/saves the editor-only layout map format. This is intentionally
    // separate from the game/respacker map .bin format.
    [[nodiscard]] bool IsEditorLayoutMap(const char* path);
    bool LoadMap(entt::registry* destination, const char* path, const InspectorRegistry* components = nullptr);
    void SaveMap(entt::registry& source, const char* path, const InspectorRegistry* components = nullptr);
    void SaveMap(
        entt::registry& source,
        const char* path,
        const std::vector<entt::entity>& hierarchyOrder,
        const InspectorRegistry* components = nullptr);
} // namespace sage::editor

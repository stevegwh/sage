//
// Camera-focus framing math: given a set of entities, compute a point and
// radius the editor camera can frame. Pulled out of EditorScene so the scene
// keeps only the camera/selection orchestration.
//

#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

#include <optional>
#include <vector>

namespace sage::editor
{
    struct FocusTarget
    {
        Vector3 position{};
        float radius = 1.0f;
    };

    // Frames the given entities by combining each one's collision box, renderable
    // bounds, or transform position (in that order of preference). Returns nullopt
    // when none of the entities can contribute a bound.
    [[nodiscard]] std::optional<FocusTarget> ComputeFocusTarget(
        entt::registry& registry, const std::vector<entt::entity>& entities);
} // namespace sage::editor

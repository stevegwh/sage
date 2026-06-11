//
// Renders the body of the Inspector window: each component's fields, with
// per-type widgets, mixed-value handling, parse/format, and copy menus. Pulled
// out of EditorGui so the window shell (layout, docking, styling) stays small and
// this field-rendering engine depends only on the InspectedComponent data model.
//

#pragma once

#include "EditorInspector.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sage::editor
{
    struct InspectorComponentsResult
    {
        bool changed = false;   // a field's value was written this frame
        bool began = false;     // an edit gesture started this frame (widget activated)
        bool committed = false; // an edit gesture ended this frame (widget deactivated)
        // Display name of a removable component whose "Remove Component" menu item
        // was clicked this frame.
        std::optional<std::string> removeComponent;
    };

    // Draws every component (collapsing header + field table). Caller supplies the
    // surrounding Inspector window; this only emits the contents.
    [[nodiscard]] InspectorComponentsResult DrawInspectorComponents(
        const std::vector<InspectedComponent>& components);
} // namespace sage::editor

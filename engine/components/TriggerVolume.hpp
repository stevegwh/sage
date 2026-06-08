#pragma once

#include "engine/components/NavigationGridSquare.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/raylib-cereal.hpp"
#include "raylib.h"

#include "cereal/types/string.hpp"

#include <string>

namespace sage
{
    // An editor-authored box region that fires a gameplay event when entered.
    // The box is centred on the owning entity's transform; halfExtents define its
    // size. `event` names what to fire (e.g. "EnterCombat", "AreaTransition") and
    // `targetTag` optionally references another tagged entity/destination.
    struct TriggerVolume
    {
        Vector3 halfExtents{1.0f, 1.0f, 1.0f};
        std::string event;
        std::string targetTag;
        bool oneShot = true;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(halfExtents, event, targetTag, oneShot);
        }

        template <class Inspector>
        void define_editor_fields(Inspector& i)
        {
            i.field("Half Extents", halfExtents);
            i.field("Event", event);
            i.field("Target Tag", targetTag);
            i.field("One Shot", oneShot);
        }
    };

    // Returns the first trigger volume whose transform occupies the given grid square,
    // or entt::null if none do. Templated on the grid system so this component header
    // stays free of system dependencies; GridSystem need only provide
    // `bool WorldToGridSpace(Vector3, GridSquare&) const`.
    template <class GridSystem>
    [[nodiscard]] entt::entity FindTriggerVolumeAtGridSquare(
        entt::registry& registry, const GridSystem& grid, const GridSquare& target)
    {
        for (auto view = registry.view<TriggerVolume, sgTransform>(); const auto entity : view)
        {
            GridSquare square{};
            if (grid.WorldToGridSpace(view.get<sgTransform>(entity).GetWorldPos(), square) && square == target)
            {
                return entity;
            }
        }
        return entt::null;
    }
} // namespace sage

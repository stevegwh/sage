#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

#include <optional>

namespace sage
{
    struct Terrain;
    class LightManager;

    // Inclusive vertex range of a height field, used to limit GPU re-uploads.
    struct TerrainRegion
    {
        int minRow = 0;
        int minCol = 0;
        int maxRow = 0;
        int maxCol = 0;
    };

    // Builds a chunked grid model from the height field. Chunks keep each mesh
    // under raylib's 16-bit index limit and let brush edits re-upload only the
    // chunks they touch.
    [[nodiscard]] Model GenerateTerrainModel(const Terrain& terrain);

    // Recomputes vertices and normals of every chunk overlapping the given
    // vertex range and re-uploads their GPU buffers.
    void UpdateTerrainModelRegion(Model& model, const Terrain& terrain, const TerrainRegion& region);

    [[nodiscard]] BoundingBox GetTerrainLocalBounds(const Terrain& terrain);

    // Sculpt brush behaviours, Unreal-landscape style. Ramp is not a paint
    // brush (see ApplyTerrainRamp) but shares the enum for the mode selector.
    enum class TerrainBrushMode
    {
        RaiseLower, // add amount (signed) to the field
        Smooth,     // average each vertex toward its neighbours
        Flatten,    // pull the field toward a reference height
        Noise,      // jitter the field for natural roughness
        Erosion,    // wear down ridges, lightly fill pits (thermal-style)
        Ramp        // linear grade between two points (handled separately)
    };

    // Applies one paint-brush dab inside the brush circle with a smoothstep
    // falloff. localCenter and radius are in terrain-local units. `amount` is
    // the per-call scalar (typically strength * frame time): for RaiseLower it
    // is the signed world-unit delta at the centre; for the others it is a
    // positive rate. `reference` is the Flatten target height (ignored by other
    // modes). Returns the touched vertex range.
    TerrainRegion ApplyTerrainBrush(
        Terrain& terrain,
        Vector2 localCenter,
        float radius,
        float amount,
        TerrainBrushMode mode,
        float reference = 0.0f);

    // Linearly grades a strip of the given half-width between two local points,
    // taking the end heights from the current field and blending toward that
    // ramp with a smoothstep falloff across the strip. Single-shot (not a drag).
    // Returns the touched vertex range.
    TerrainRegion ApplyTerrainRamp(
        Terrain& terrain, Vector2 localStart, Vector2 localEnd, float halfWidth);

    // Intersects a world-space ray with the height field of a terrain whose
    // entity sits at worldOrigin. Returns the world-space hit point.
    [[nodiscard]] std::optional<Vector3> GetTerrainRayHit(
        const Terrain& terrain, Vector3 worldOrigin, const Ray& ray);

    // (Re)builds the entity's DynamicRenderable model and Collideable bounds
    // from its Terrain component and links the lighting shader. Shared by the
    // editor (create/load/undo) and the game's level loader.
    void AttachTerrainRenderable(entt::registry& registry, entt::entity entity, LightManager& lightManager);

    // Refits the Collideable boxes to the current height range; cheap enough to
    // run per brush stroke frame.
    void UpdateTerrainCollideableBounds(entt::registry& registry, entt::entity entity);
} // namespace sage

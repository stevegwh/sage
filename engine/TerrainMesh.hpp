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

    // Raises (positive delta) or lowers the field inside the brush circle with
    // a smoothstep falloff. localCenter and radius are in terrain-local units,
    // delta in world units at the brush centre. Returns the touched range.
    TerrainRegion ApplyTerrainBrush(Terrain& terrain, Vector2 localCenter, float radius, float delta);

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

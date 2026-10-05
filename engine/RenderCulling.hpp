#pragma once

#include "raylib.h"

#include <array>
#include <optional>
#include <vector>

namespace sage
{
    // Planes come from the active render pass, including each light's shadow camera.
    class RenderFrustum
    {
        std::array<Vector4, 6> planes{};
        bool valid = true;

      public:
        explicit RenderFrustum(Matrix viewProjection);
        [[nodiscard]] bool Intersects(const BoundingBox& localBounds, Matrix localToWorld) const;
    };

    // Geometry bounds are independent of entity transforms and shared by views of the same model.
    // Missing CPU vertices and skinned meshes have unknown bounds and remain visible.
    class ModelRenderBounds
    {
        std::optional<std::vector<std::optional<BoundingBox>>> meshes;

      public:
        void Invalidate();
        [[nodiscard]] std::optional<BoundingBox> Get(const Model& model, int meshIndex);
    };
} // namespace sage

#include "RenderCulling.hpp"

#include "raymath.h"

#include <cmath>
#include <span>

namespace sage
{
    namespace
    {
        constexpr float FRUSTUM_EDGE_TOLERANCE = 0.001f;

        bool Finite(const Vector3 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    } // namespace

    RenderFrustum::RenderFrustum(const Matrix m)
        : planes{
              Vector4{.x = m.m3 + m.m0, .y = m.m7 + m.m4, .z = m.m11 + m.m8, .w = m.m15 + m.m12},
              Vector4{.x = m.m3 - m.m0, .y = m.m7 - m.m4, .z = m.m11 - m.m8, .w = m.m15 - m.m12},
              Vector4{.x = m.m3 + m.m1, .y = m.m7 + m.m5, .z = m.m11 + m.m9, .w = m.m15 + m.m13},
              Vector4{.x = m.m3 - m.m1, .y = m.m7 - m.m5, .z = m.m11 - m.m9, .w = m.m15 - m.m13},
              Vector4{.x = m.m3 + m.m2, .y = m.m7 + m.m6, .z = m.m11 + m.m10, .w = m.m15 + m.m14},
              Vector4{.x = m.m3 - m.m2, .y = m.m7 - m.m6, .z = m.m11 - m.m10, .w = m.m15 - m.m14}}
    {
        for (auto& plane : planes)
        {
            const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
            if (!std::isfinite(length) || length == 0.0f || !std::isfinite(plane.w))
            {
                valid = false;
                break;
            }
            plane.x /= length;
            plane.y /= length;
            plane.z /= length;
            plane.w /= length;
        }
    }

    bool RenderFrustum::Intersects(const BoundingBox& bounds, const Matrix transform) const
    {
        if (!valid || !Finite(bounds.min) || !Finite(bounds.max)) return true;
        for (const auto plane : planes)
        {
            // Transform each plane into model space. This tests the oriented box without
            // enlarging it into a world AABB or transforming all eight corners per pass.
            const Vector3 normal = {
                .x = plane.x * transform.m0 + plane.y * transform.m1 + plane.z * transform.m2,
                .y = plane.x * transform.m4 + plane.y * transform.m5 + plane.z * transform.m6,
                .z = plane.x * transform.m8 + plane.y * transform.m9 + plane.z * transform.m10};
            const float distance =
                plane.x * transform.m12 + plane.y * transform.m13 + plane.z * transform.m14 + plane.w;
            const Vector3 support = {
                .x = normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
                .y = normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
                .z = normal.z >= 0.0f ? bounds.max.z : bounds.min.z};
            if (Vector3DotProduct(normal, support) + distance < -FRUSTUM_EDGE_TOLERANCE) return false;
        }
        return true;
    }

    void ModelRenderBounds::Invalidate()
    {
        meshes.reset();
    }

    std::optional<BoundingBox> ModelRenderBounds::Get(const Model& model, const int meshIndex)
    {
        if (meshIndex < 0 || meshIndex >= model.meshCount || model.meshes == nullptr) return std::nullopt;
        if (!meshes || meshes->size() != static_cast<std::size_t>(model.meshCount))
        {
            meshes.emplace(static_cast<std::size_t>(model.meshCount));
            const std::span<const Mesh> modelMeshes(model.meshes, static_cast<std::size_t>(model.meshCount));
            std::size_t index = 0;
            for (const auto& mesh : modelMeshes)
            {
                auto& cachedBounds = meshes->at(index++);
                if (mesh.vertices == nullptr || mesh.vertexCount <= 0 || mesh.boneCount > 0) continue;
                const auto bounds = GetMeshBoundingBox(mesh);
                if (Finite(bounds.min) && Finite(bounds.max)) cachedBounds = bounds;
            }
        }
        return meshes->at(static_cast<std::size_t>(meshIndex));
    }
} // namespace sage

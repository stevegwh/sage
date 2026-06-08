//
// BoxColliderGizmo implementation. See BoxColliderGizmo.hpp.
//

#include "BoxColliderGizmo.hpp"

#include "EditGizmo.hpp"

#include "raymath.h"

#include <algorithm>
#include <array>
#include <limits>

namespace sage::editor
{
    namespace
    {
        constexpr float HANDLE_SCREEN_HIT_RADIUS = 12.0f;
        // Length of the reference segment used to project a face normal into
        // screen space when converting pixel drags back into world units.
        constexpr float NORMAL_REFERENCE_LENGTH = 1.0f;

        Vector3 FaceNormal(const BoxColliderGizmo::Face face)
        {
            switch (face)
            {
            case BoxColliderGizmo::Face::MinX:
                return {-1.0f, 0.0f, 0.0f};
            case BoxColliderGizmo::Face::MaxX:
                return {1.0f, 0.0f, 0.0f};
            case BoxColliderGizmo::Face::MinY:
                return {0.0f, -1.0f, 0.0f};
            case BoxColliderGizmo::Face::MaxY:
                return {0.0f, 1.0f, 0.0f};
            case BoxColliderGizmo::Face::MinZ:
                return {0.0f, 0.0f, -1.0f};
            case BoxColliderGizmo::Face::MaxZ:
                return {0.0f, 0.0f, 1.0f};
            case BoxColliderGizmo::Face::None:
                return Vector3Zero();
            }
            return Vector3Zero();
        }

        // Centre of the given face of the box.
        Vector3 FaceCenter(const BoundingBox& box, const BoxColliderGizmo::Face face)
        {
            const Vector3 center = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
            switch (face)
            {
            case BoxColliderGizmo::Face::MinX:
                return {box.min.x, center.y, center.z};
            case BoxColliderGizmo::Face::MaxX:
                return {box.max.x, center.y, center.z};
            case BoxColliderGizmo::Face::MinY:
                return {center.x, box.min.y, center.z};
            case BoxColliderGizmo::Face::MaxY:
                return {center.x, box.max.y, center.z};
            case BoxColliderGizmo::Face::MinZ:
                return {center.x, center.y, box.min.z};
            case BoxColliderGizmo::Face::MaxZ:
                return {center.x, center.y, box.max.z};
            case BoxColliderGizmo::Face::None:
                return center;
            }
            return center;
        }

        constexpr std::array<BoxColliderGizmo::Face, 6> ALL_FACES = {
            BoxColliderGizmo::Face::MinX,
            BoxColliderGizmo::Face::MaxX,
            BoxColliderGizmo::Face::MinY,
            BoxColliderGizmo::Face::MaxY,
            BoxColliderGizmo::Face::MinZ,
            BoxColliderGizmo::Face::MaxZ};

        Vector2 WorldToScreen(const Camera3D& camera, const Vector2 viewport, const Vector3 worldPosition)
        {
            return GetWorldToScreenEx(
                worldPosition, camera, static_cast<int>(viewport.x), static_cast<int>(viewport.y));
        }
    } // namespace

    BoxColliderGizmo::Face BoxColliderGizmo::HitTest(
        const Camera3D& camera,
        const Vector2 viewport,
        const BoundingBox& worldBox,
        const Vector2 mousePosition) const
    {
        Face closestFace = Face::None;
        float closestDistance = std::numeric_limits<float>::max();
        for (const auto face : ALL_FACES)
        {
            const float distance = Vector2Distance(
                mousePosition, WorldToScreen(camera, viewport, FaceCenter(worldBox, face)));
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closestFace = face;
            }
        }
        return closestDistance <= HANDLE_SCREEN_HIT_RADIUS ? closestFace : Face::None;
    }

    void BoxColliderGizmo::BeginDrag(const Face face, const Vector2 mousePosition)
    {
        drag = {.active = true, .face = face, .lastMousePosition = mousePosition};
    }

    void BoxColliderGizmo::EndDrag()
    {
        drag = {};
    }

    BoxColliderGizmo::DragSample BoxColliderGizmo::SampleDrag(
        const Camera3D& camera,
        const Vector2 viewport,
        const BoundingBox& worldBox,
        const Vector2 mousePosition)
    {
        if (!drag.active) return {};

        const Vector2 mouseDelta = Vector2Subtract(mousePosition, drag.lastMousePosition);
        drag.lastMousePosition = mousePosition;
        if (Vector2Length(mouseDelta) <= 0.0001f) return {};

        // Project the face's outward normal into screen space, then read off how
        // far the cursor moved along it — the same screen-axis projection
        // EditGizmo uses for translate/scale, scaled back into world units.
        const Vector3 faceCenter = FaceCenter(worldBox, drag.face);
        const Vector3 normal = FaceNormal(drag.face);
        const Vector2 start = WorldToScreen(camera, viewport, faceCenter);
        const Vector2 end = WorldToScreen(
            camera, viewport, Vector3Add(faceCenter, Vector3Scale(normal, NORMAL_REFERENCE_LENGTH)));
        const Vector2 screenNormal = Vector2Subtract(end, start);
        const float screenLength = Vector2Length(screenNormal);
        if (screenLength <= 0.0001f) return {};

        const float pixels = Vector2DotProduct(mouseDelta, Vector2Scale(screenNormal, 1.0f / screenLength));
        return {.face = drag.face, .worldDelta = pixels * NORMAL_REFERENCE_LENGTH / screenLength};
    }

    void BoxColliderGizmo::Draw(
        const Camera3D& camera, const BoundingBox& worldBox, const float viewportScale) const
    {
        DrawBoundingBox(worldBox, drag.active ? GOLD : SKYBLUE);

        for (const auto face : ALL_FACES)
        {
            const Vector3 center = FaceCenter(worldBox, face);
            const float radius = EditGizmo::SizeForCamera(camera.position, center, viewportScale) * 0.06f;
            const Color color = drag.active && drag.face == face ? GOLD : SKYBLUE;
            DrawSphere(center, radius, color);
        }
    }
} // namespace sage::editor

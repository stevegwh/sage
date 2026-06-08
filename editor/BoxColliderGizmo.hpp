//
// Unity-style box-collider editing handle. While the editor is in the
// "Box Collider" edit mode it draws a draggable dot at the centre of each of
// the six faces of the selected Collideable's world bounding box. Dragging a
// dot pushes that single face in/out along its axis, growing or shrinking the
// box. Owns only its drag state and screen-space hit testing — the caller
// supplies the world box and applies the per-face delta this reports back.
//
// Deliberately a sibling of EditGizmo rather than a fourth mode inside it: a
// box has six signed faces instead of three axes through a pivot, so the
// geometry doesn't fit EditGizmo's origin+axis model.
//

#pragma once

#include "raylib.h"

namespace sage::editor
{
    class BoxColliderGizmo
    {
      public:
        enum class Face
        {
            None,
            MinX,
            MaxX,
            MinY,
            MaxY,
            MinZ,
            MaxZ
        };

        // The result of polling a drag for one frame. worldDelta is the distance
        // the dragged face should travel along its outward normal (positive =
        // outward, i.e. the box grows). Zero when not dragging or the cursor
        // hasn't moved.
        struct DragSample
        {
            Face face = Face::None;
            float worldDelta = 0.0f;
        };

        // Returns the face handle under the cursor, or Face::None.
        [[nodiscard]] Face HitTest(
            const Camera3D& camera, Vector2 viewport, const BoundingBox& worldBox, Vector2 mousePosition) const;

        // Drag lifecycle, driven from the caller's mouse-down / mouse-up events.
        void BeginDrag(Face face, Vector2 mousePosition);
        void EndDrag();
        [[nodiscard]] bool IsDragging() const
        {
            return drag.active;
        }

        // Returns DragSample{face = Face::None} when not dragging, when the mouse
        // hasn't moved, or when the projected screen normal is degenerate.
        DragSample SampleDrag(
            const Camera3D& camera, Vector2 viewport, const BoundingBox& worldBox, Vector2 mousePosition);

        // viewportScale keeps the face-handle dots a constant fraction of the
        // window rather than the render viewport — see EditGizmo::SizeForCamera.
        void Draw(const Camera3D& camera, const BoundingBox& worldBox, float viewportScale = 1.0f) const;

      private:
        struct DragState
        {
            bool active = false;
            Face face = Face::None;
            Vector2 lastMousePosition{};
        };

        DragState drag;
    };
} // namespace sage::editor

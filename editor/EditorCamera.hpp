#pragma once

#include "EditorFocus.hpp"

namespace sage::editor
{
    enum class CameraMode
    {
        Game,
        Free,
        Focused
    };

    // Editor-only controls. The game camera continues to own in-game movement.
    class EditorCamera
    {
        float focusRadius = 1.0f;

      public:
        CameraMode mode = CameraMode::Game;
        void Focus(Camera3D& camera, const FocusTarget& target, float aspect);
        void Follow(Camera3D& camera, const FocusTarget& target);
        void Zoom(Camera3D& camera, float wheel);
        void Yaw(Camera3D& camera, float radians) const;
        void Pitch(Camera3D& camera, float radians) const;
        void Look(Camera3D& camera, Vector2 delta) const;
        void Pan(Camera3D& camera, Vector2 delta) const;
        void Move(Camera3D& camera, Vector3 localMovement, float deltaTime, bool fast) const;
    };
} // namespace sage::editor

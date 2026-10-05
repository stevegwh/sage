#include "EditorCamera.hpp"
#include "engine/MathConstants.hpp"

#include "raymath.h"
#include "rcamera.h"

#include <algorithm>
#include <cmath>

namespace sage::editor
{
    namespace
    {
        constexpr float LOOK_SENSITIVITY = 0.003f;
        constexpr float FOCUSED_KEY_ROTATION_SPEED = 1.5f;
        constexpr float MOVE_SPEED = 30.0f;
        constexpr float FAST_MULTIPLIER = 4.0f;
        constexpr float FOCUS_CLEARANCE = 0.5f;

        Vector3 viewOffset(const Camera3D& camera)
        {
            const auto offset = Vector3Subtract(camera.position, camera.target);
            return Vector3Length(offset) > 0.0001f ? Vector3Normalize(offset)
                                                   : Vector3Normalize(Vector3{.x = 0, .y = 1, .z = 1});
        }

        void translate(Camera3D& camera, Vector3 movement)
        {
            camera.position = Vector3Add(camera.position, movement);
            camera.target = Vector3Add(camera.target, movement);
        }
    } // namespace

    void EditorCamera::Focus(Camera3D& camera, const FocusTarget& target, const float aspect)
    {
        mode = CameraMode::Focused;
        focusRadius = target.radius;
        const float verticalHalfFov = camera.fovy * sage::math::DEGREES_TO_RADIANS * 0.5f;
        const float horizontalHalfFov = std::atan(std::tan(verticalHalfFov) * std::max(0.01f, aspect));
        const float distance = std::max(
            focusRadius + FOCUS_CLEARANCE,
            focusRadius * 1.15f / std::sin(std::min(verticalHalfFov, horizontalHalfFov)));
        camera.position = Vector3Add(target.position, Vector3Scale(viewOffset(camera), distance));
        camera.target = target.position;
    }

    void EditorCamera::Follow(Camera3D& camera, const FocusTarget& target)
    {
        translate(camera, Vector3Subtract(target.position, camera.target));
        focusRadius = target.radius;
        // A growing selection must not engulf the camera.
        Zoom(camera, 0.0f);
    }

    void EditorCamera::Zoom(Camera3D& camera, const float wheel)
    {
        if (mode == CameraMode::Focused)
        {
            const float distance = Vector3Distance(camera.position, camera.target);
            const float nextDistance = std::max(
                focusRadius + FOCUS_CLEARANCE, distance * std::exp(-std::clamp(wheel, -20.0f, 20.0f) * 0.15f));
            camera.position = Vector3Add(camera.target, Vector3Scale(viewOffset(camera), nextDistance));
        }
        else if (mode == CameraMode::Free)
        {
            translate(camera, Vector3Scale(GetCameraForward(&camera), wheel * 3.0f));
        }
    }

    void EditorCamera::Yaw(Camera3D& camera, const float radians) const
    {
        CameraYaw(&camera, radians, mode == CameraMode::Focused);
    }

    void EditorCamera::Pitch(Camera3D& camera, const float radians) const
    {
        CameraPitch(&camera, radians, true, mode == CameraMode::Focused, false);
    }

    void EditorCamera::Look(Camera3D& camera, const Vector2 delta) const
    {
        Yaw(camera, -delta.x * LOOK_SENSITIVITY);
        Pitch(camera, -delta.y * LOOK_SENSITIVITY);
    }

    void EditorCamera::RotateFocused(Camera3D& camera, const Vector2 input, const float deltaTime) const
    {
        if (mode != CameraMode::Focused) return;
        Yaw(camera, input.x * FOCUSED_KEY_ROTATION_SPEED * deltaTime);
        Pitch(camera, input.y * FOCUSED_KEY_ROTATION_SPEED * deltaTime);
    }

    void EditorCamera::Pan(Camera3D& camera, const Vector2 delta) const
    {
        if (mode != CameraMode::Free) return;
        const float speed = std::max(1.0f, Vector3Distance(camera.position, camera.target)) * 0.0015f;
        translate(
            camera,
            Vector3Add(
                Vector3Scale(GetCameraRight(&camera), -delta.x * speed),
                Vector3Scale(GetCameraUp(&camera), delta.y * speed)));
    }

    void EditorCamera::Move(Camera3D& camera, Vector3 localMovement, const float deltaTime, const bool fast) const
    {
        if (mode != CameraMode::Free) return;
        if (Vector3Length(localMovement) > 1.0f) localMovement = Vector3Normalize(localMovement);
        const float step = MOVE_SPEED * deltaTime * (fast ? FAST_MULTIPLIER : 1.0f);
        Vector3 movement = Vector3Scale(GetCameraRight(&camera), localMovement.x);
        movement.y += localMovement.y;
        movement = Vector3Add(movement, Vector3Scale(GetCameraForward(&camera), localMovement.z));
        translate(camera, Vector3Scale(movement, step));
    }
} // namespace sage::editor

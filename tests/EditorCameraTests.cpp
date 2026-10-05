#include "EditorCamera.hpp"
#include "engine/MathConstants.hpp"
#include "raymath.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool near(Vector3 a, Vector3 b)
    {
        return Vector3Distance(a, b) < 0.001f;
    }
} // namespace

int main()
{
    try
    {
        sage::editor::EditorCamera controls;
        Camera3D camera{
            .position = {.x = 10, .y = 20, .z = 30},
            .target = {.x = 0, .y = 8, .z = 0},
            .up = {.x = 0, .y = 1, .z = 0},
            .fovy = 45,
            .projection = CAMERA_PERSPECTIVE};
        sage::editor::FocusTarget object{.position = {.x = 100, .y = 57, .z = -40}, .radius = 12};
        controls.Focus(camera, object, 0.5f);
        require(near(camera.target, object.position), "Focus must use the object's center, including its height");
        const float distance = Vector3Distance(camera.position, camera.target);
        const float halfFov = std::atan(std::tan(45 * sage::math::DEGREES_TO_RADIANS / 2) * 0.5f);
        require(std::asin(object.radius / distance) < halfFov, "Focus must fit a narrow viewport");

        controls.Look(camera, {.x = 150, .y = 80});
        require(near(camera.target, object.position), "Orbit must preserve the object's center");
        require(
            std::abs(Vector3Distance(camera.position, camera.target) - distance) < 0.001f,
            "Orbit must preserve distance");
        const auto beforeKeyboardOrbit = camera.position;
        controls.Yaw(camera, 0.4f);
        require(near(camera.target, object.position), "Q/E orbit must keep the object centered");
        require(
            std::abs(Vector3Distance(camera.position, camera.target) - distance) < 0.001f,
            "Q/E orbit must keep its distance");
        controls.Yaw(camera, -0.4f);
        require(near(camera.position, beforeKeyboardOrbit), "Opposite Q/E rotations must reverse each other");
        controls.Pitch(camera, -0.3f);
        require(camera.position.y > beforeKeyboardOrbit.y, "W must raise the focused camera");
        require(near(camera.target, object.position), "W/S orbit must keep the object centered");
        require(
            std::abs(Vector3Distance(camera.position, camera.target) - distance) < 0.001f,
            "W/S orbit must keep its distance");
        controls.Pitch(camera, 0.3f);
        require(near(camera.position, beforeKeyboardOrbit), "S must reverse W's camera movement");
        controls.RotateFocused(camera, {.x = 1, .y = -1}, 0.1f);
        require(near(camera.target, object.position), "Shared keyboard orbit must preserve the focused center");
        require(camera.position.y > beforeKeyboardOrbit.y, "Shared W input must raise the focused camera");
        require(
            std::abs(Vector3Distance(camera.position, camera.target) - distance) < 0.001f,
            "Shared keyboard orbit must preserve distance");
        controls.RotateFocused(camera, {.x = 0, .y = 1}, 0.1f);
        controls.RotateFocused(camera, {.x = -1, .y = 0}, 0.1f);
        require(
            near(camera.position, beforeKeyboardOrbit), "Opposite shared keyboard inputs must restore the view");
        for (int i = 0; i < 100; ++i)
            controls.Zoom(camera, 5);
        require(
            Vector3Distance(camera.position, camera.target) >= object.radius + 0.49f,
            "Repeated zoom must stay outside the object");
        require(near(camera.target, object.position), "Zoom must remain centered");
        controls.Zoom(camera, -2);
        require(
            Vector3Distance(camera.position, camera.target) > object.radius + 0.6f,
            "Zoom must reverse immediately without game camera momentum");

        object.position = {.x = 200, .y = -30, .z = 70};
        object.radius = 80;
        controls.Follow(camera, object);
        require(near(camera.target, object.position), "Focused camera must follow a moved selection");
        require(
            Vector3Distance(camera.position, camera.target) >= 80.49f,
            "Growing selection must not engulf the camera");

        controls.mode = sage::editor::CameraMode::Free;
        const auto position = camera.position;
        controls.Look(camera, {.x = 100, .y = -50});
        require(near(camera.position, position), "Free look must rotate about the camera itself");
        const auto offset = Vector3Subtract(camera.target, camera.position);
        controls.Move(camera, {.x = 0, .y = 0, .z = 1}, 1, false);
        require(camera.position.y != position.y, "Free flight must follow pitch without a ground constraint");
        require(
            near(Vector3Subtract(camera.target, camera.position), offset), "Flight must preserve view direction");
        const auto beforeZoom = camera.position;
        controls.Zoom(camera, 2);
        require(Vector3Distance(beforeZoom, camera.position) > 5.9f, "Free wheel must move the camera");
        require(
            near(Vector3Subtract(camera.target, camera.position), offset),
            "Free wheel must preserve view direction");

        // Degenerate starting poses should still produce a valid focus direction.
        camera.position = camera.target;
        controls.Focus(camera, {.position = {.x = 0, .y = 0, .z = 0}, .radius = 1}, 1);
        require(
            std::isfinite(camera.position.y) && Vector3Distance(camera.position, camera.target) > 1,
            "Focus must handle a zero view offset");
        std::cout << "Editor camera checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

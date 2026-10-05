#include "engine/RenderCulling.hpp"
#include "raymath.h"

#include <array>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

namespace
{
    constexpr unsigned int TEST_SEED = 6419;
    constexpr int RANDOM_BOX_COUNT = 3000;
    constexpr BoundingBox UNIT_BOX = {
        .min = {.x = -0.5f, .y = -0.5f, .z = -0.5f}, .max = {.x = 0.5f, .y = 0.5f, .z = 0.5f}};

    void Check(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool ReferenceIntersects(const BoundingBox& box, const Matrix transform, const Matrix viewProjection)
    {
        std::array<bool, 6> allOutside = {true, true, true, true, true, true};
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vector3 world = Vector3Transform(
                {.x = (corner & 1) != 0 ? box.max.x : box.min.x,
                 .y = (corner & 2) != 0 ? box.max.y : box.min.y,
                 .z = (corner & 4) != 0 ? box.max.z : box.min.z},
                transform);
            const Vector3 clip = Vector3Transform(world, viewProjection);
            const float w = world.x * viewProjection.m3 + world.y * viewProjection.m7 +
                            world.z * viewProjection.m11 + viewProjection.m15;
            const std::array<float, 6> distances = {
                w + clip.x, w - clip.x, w + clip.y, w - clip.y, w + clip.z, w - clip.z};
            for (std::size_t plane = 0; plane < allOutside.size(); ++plane)
                allOutside.at(plane) = allOutside.at(plane) && distances.at(plane) < 0.0f;
        }
        for (const bool outside : allOutside)
            if (outside) return false;
        return true;
    }

    void TestFrustums()
    {
        const Matrix perspective = MatrixPerspective(std::numbers::pi / 2.0, 1.0, 1.0, 50.0);
        const Matrix orthographic = MatrixOrtho(-10.0, 10.0, -10.0, 10.0, 1.0, 50.0);
        for (const Matrix projection : {perspective, orthographic})
        {
            const sage::RenderFrustum frustum(projection);
            Check(frustum.Intersects(UNIT_BOX, MatrixTranslate(0.0f, 0.0f, -5.0f)), "Visible box culled");
            for (const Vector3 position : std::array<Vector3, 6>{
                     Vector3{.x = -100.0f, .y = 0.0f, .z = -5.0f},
                     Vector3{.x = 100.0f, .y = 0.0f, .z = -5.0f},
                     Vector3{.x = 0.0f, .y = -100.0f, .z = -5.0f},
                     Vector3{.x = 0.0f, .y = 100.0f, .z = -5.0f},
                     Vector3{.x = 0.0f, .y = 0.0f, .z = 5.0f},
                     Vector3{.x = 0.0f, .y = 0.0f, .z = -100.0f}})
                Check(
                    !frustum.Intersects(UNIT_BOX, MatrixTranslate(position.x, position.y, position.z)),
                    "Outside box submitted");
            Check(
                frustum.Intersects(UNIT_BOX, MatrixTranslate(0.0f, 0.0f, -0.75f)),
                "Near-plane intersection culled");
            Check(
                frustum.Intersects(UNIT_BOX, MatrixScale(200.0f, 200.0f, 200.0f)),
                "Box enclosing the frustum culled");
        }
        Check(
            sage::RenderFrustum(Matrix{}).Intersects(UNIT_BOX, MatrixTranslate(100.0f, 0.0f, 0.0f)),
            "Invalid frustum should retain meshes");

        const Matrix view = MatrixLookAt(
            {.x = 8.0f, .y = 12.0f, .z = 20.0f},
            {.x = -2.0f, .y = 1.0f, .z = -5.0f},
            {.x = 0.0f, .y = 1.0f, .z = 0.0f});
        std::mt19937 random(TEST_SEED);
        std::uniform_real_distribution<float> position(-80.0f, 80.0f);
        std::uniform_real_distribution<float> scale(-8.0f, 8.0f);
        std::uniform_real_distribution<float> angle(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
        for (const Matrix projection : {perspective, orthographic})
        {
            const Matrix viewProjection = MatrixMultiply(view, projection);
            const sage::RenderFrustum frustum(viewProjection);
            for (int index = 0; index < RANDOM_BOX_COUNT; ++index)
            {
                const Matrix transform = MatrixMultiply(
                    MatrixMultiply(
                        MatrixScale(scale(random), scale(random), scale(random)),
                        MatrixRotateXYZ({.x = angle(random), .y = angle(random), .z = angle(random)})),
                    MatrixTranslate(position(random), position(random), position(random)));
                Check(
                    frustum.Intersects(UNIT_BOX, transform) ==
                        ReferenceIntersects(UNIT_BOX, transform, viewProjection),
                    "Transformed box disagrees with eight-corner clip test");
            }
        }

        const std::array<Vector3, 6> directions = {
            Vector3{.x = 1.0f},
            Vector3{.x = -1.0f},
            Vector3{.y = 1.0f},
            Vector3{.y = -1.0f},
            Vector3{.z = 1.0f},
            Vector3{.z = -1.0f}};
        for (const auto direction : directions)
        {
            const Vector3 up = direction.y == 0.0f ? Vector3{.y = 1.0f} : Vector3{.z = 1.0f};
            const sage::RenderFrustum face(MatrixMultiply(MatrixLookAt({}, direction, up), perspective));
            Check(
                face.Intersects(UNIT_BOX, MatrixTranslate(direction.x * 5, direction.y * 5, direction.z * 5)),
                "Cubemap face culled its caster");
            Check(
                !face.Intersects(UNIT_BOX, MatrixTranslate(direction.x * -5, direction.y * -5, direction.z * -5)),
                "Cubemap face retained its opposite caster");
        }
        const sage::RenderFrustum camera(perspective);
        const sage::RenderFrustum light(MatrixMultiply(MatrixLookAt({.x = 20.0f}, {}, {.y = 1.0f}), orthographic));
        const Matrix caster = MatrixTranslate(10.0f, 0.0f, 0.0f);
        Check(
            !camera.Intersects(UNIT_BOX, caster) && light.Intersects(UNIT_BOX, caster),
            "Off-camera shadow caster must remain visible to its light");
    }

    void TestCachedBounds()
    {
        std::array<float, 6> vertices = {-1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f};
        std::array<Mesh, 2> meshes = {Mesh{.vertexCount = 2, .vertices = vertices.data()}, Mesh{}};
        Model model{};
        model.meshCount = 1;
        model.meshes = meshes.data();
        sage::ModelRenderBounds cache;
        Check(cache.Get(model, 0)->max.y == 2.0f, "Initial mesh bounds incorrect");
        vertices.at(4) = 25.0f;
        Check(cache.Get(model, 0)->max.y == 2.0f, "Bounds rescanned on every draw");
        cache.Invalidate();
        Check(cache.Get(model, 0)->max.y == 25.0f, "Edited terrain bounds stayed stale");
        model.meshCount = 2;
        Check(!cache.Get(model, 1), "Unknown mesh must not supply culling bounds");
        meshes.at(0).boneCount = 1;
        cache.Invalidate();
        Check(!cache.Get(model, 0), "Skinned mesh bind-pose bounds must not cull animation");
        Check(!cache.Get(model, -1) && !cache.Get(model, 2), "Invalid mesh index supplied bounds");
    }
} // namespace

int main()
{
    try
    {
        TestFrustums();
        TestCachedBounds();
        std::cout << "Render culling tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

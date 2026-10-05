#pragma once

#include "engine/Colors.hpp"
#include "engine/RenderCulling.hpp"
#include "entt/entt.hpp"
#include "raylib.h"
#include "raymath.h"

#include <functional>
#include <optional>
#include <string>

namespace sage
{
    class DynamicRenderable
    {
        Model model{};
        mutable ModelRenderBounds renderBounds;
        std::string name = "DynamicRenderable";

      public:
        Color hint = sage::colors::WHITE_COLOR;
        bool active = true;
        Matrix initialTransform{};
        std::function<void(entt::entity)> reqShaderUpdate;

        DynamicRenderable() = default;
        DynamicRenderable(Model _model, Matrix _localTransform);
        ~DynamicRenderable();

        DynamicRenderable(const DynamicRenderable&) = delete;
        DynamicRenderable& operator=(const DynamicRenderable&) = delete;
        DynamicRenderable(DynamicRenderable&& other) noexcept;
        DynamicRenderable& operator=(DynamicRenderable&& other) noexcept;

        [[nodiscard]] bool HasModel() const;
        [[nodiscard]] std::optional<BoundingBox> GetRenderMeshBounds(int meshIndex) const;
        // Call after changing vertices through GetModel() or a retained mesh reference.
        void InvalidateRenderBounds();
        [[nodiscard]] std::optional<std::reference_wrapper<Model>> GetModel();
        [[nodiscard]] std::optional<std::reference_wrapper<const Model>> GetModel() const;
        [[nodiscard]] std::optional<std::reference_wrapper<Mesh>> GetMesh(int num = 0);
        [[nodiscard]] std::optional<std::reference_wrapper<const Mesh>> GetMesh(int num = 0) const;
        [[nodiscard]] const std::string& GetName() const;

        void SetName(const std::string& _name);
        void SetModel(Model _model, Matrix _localTransform = MatrixIdentity());
        void Unload();
        void SetTransform(Matrix trans);
        void SetShader(Shader shader, int materialIdx);
        void SetShader(Shader shader);
        void Draw(
            Vector3 position,
            Vector3 rotationAxis,
            float rotationAngle,
            Vector3 scale,
            Color tint,
            const std::optional<RenderFrustum>& frustum = std::nullopt) const;
    };
} // namespace sage

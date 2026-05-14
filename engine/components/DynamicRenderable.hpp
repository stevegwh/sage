#pragma once

#include "entt/entt.hpp"
#include "raylib.h"
#include "raymath.h"

#include <functional>
#include <string>

namespace sage
{
    class DynamicRenderable
    {
        Model model{};
        std::string name = "DynamicRenderable";

      public:
        Color hint = WHITE;
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
        [[nodiscard]] Model* GetModel();
        [[nodiscard]] const Model* GetModel() const;
        [[nodiscard]] Mesh* GetMesh(int num = 0);
        [[nodiscard]] const Mesh* GetMesh(int num = 0) const;
        [[nodiscard]] const std::string& GetName() const;

        void SetName(const std::string& _name);
        void SetModel(Model _model, Matrix _localTransform = MatrixIdentity());
        void Unload();
        void SetTransform(Matrix trans);
        void SetShader(Shader shader, int materialIdx);
        void SetShader(Shader shader);
        void Draw(Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint) const;
    };
} // namespace sage

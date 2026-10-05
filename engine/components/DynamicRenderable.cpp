#include "DynamicRenderable.hpp"
#include "engine/MathConstants.hpp"

#include <utility>

namespace sage
{
    DynamicRenderable::DynamicRenderable(Model _model, Matrix _localTransform)
        : model(_model), initialTransform(_localTransform)
    {
        model.transform = _localTransform;
    }

    DynamicRenderable::~DynamicRenderable()
    {
        Unload();
    }

    DynamicRenderable::DynamicRenderable(DynamicRenderable&& other) noexcept
        : model(other.model),
          renderBounds(std::move(other.renderBounds)),
          name(std::move(other.name)),
          hint(other.hint),
          active(other.active),
          initialTransform(other.initialTransform),
          reqShaderUpdate(std::move(other.reqShaderUpdate))
    {
        other.model = {};
    }

    DynamicRenderable& DynamicRenderable::operator=(DynamicRenderable&& other) noexcept
    {
        if (this == &other) return *this;

        Unload();

        model = other.model;
        renderBounds = std::move(other.renderBounds);
        name = std::move(other.name);
        hint = other.hint;
        active = other.active;
        initialTransform = other.initialTransform;
        reqShaderUpdate = std::move(other.reqShaderUpdate);

        other.model = {};
        return *this;
    }

    std::optional<BoundingBox> DynamicRenderable::GetRenderMeshBounds(const int meshIndex) const
    {
        return renderBounds.Get(model, meshIndex);
    }

    void DynamicRenderable::InvalidateRenderBounds()
    {
        renderBounds.Invalidate();
    }

    bool DynamicRenderable::HasModel() const
    {
        return model.meshCount > 0 && model.meshes != nullptr;
    }

    std::optional<std::reference_wrapper<Model>> DynamicRenderable::GetModel()
    {
        if (!HasModel()) return std::nullopt;
        return std::ref(model);
    }

    std::optional<std::reference_wrapper<const Model>> DynamicRenderable::GetModel() const
    {
        if (!HasModel()) return std::nullopt;
        return std::cref(model);
    }

    std::optional<std::reference_wrapper<Mesh>> DynamicRenderable::GetMesh(int num)
    {
        if (!HasModel() || num < 0 || num >= model.meshCount) return std::nullopt;
        InvalidateRenderBounds();
        return std::ref(model.meshes[num]);
    }

    std::optional<std::reference_wrapper<const Mesh>> DynamicRenderable::GetMesh(int num) const
    {
        if (!HasModel() || num < 0 || num >= model.meshCount) return std::nullopt;
        return std::cref(model.meshes[num]);
    }

    const std::string& DynamicRenderable::GetName() const
    {
        return name;
    }

    void DynamicRenderable::SetName(const std::string& _name)
    {
        name = _name;
    }

    void DynamicRenderable::SetModel(Model _model, Matrix _localTransform)
    {
        Unload();
        model = _model;
        initialTransform = _localTransform;
        model.transform = _localTransform;
    }

    void DynamicRenderable::Unload()
    {
        InvalidateRenderBounds();
        if (model.meshes == nullptr && model.materials == nullptr && model.meshMaterial == nullptr &&
            model.bones == nullptr && model.bindPose == nullptr)
        {
            model = {};
            return;
        }

        UnloadModel(model);
        model = {};
    }

    void DynamicRenderable::SetTransform(Matrix trans)
    {
        if (!HasModel()) return;
        model.transform = trans;
    }

    void DynamicRenderable::SetShader(Shader shader, int materialIdx)
    {
        if (!HasModel() || materialIdx < 0 || materialIdx >= model.materialCount) return;
        model.materials[materialIdx].shader = shader;
    }

    void DynamicRenderable::SetShader(Shader shader)
    {
        if (!HasModel()) return;
        for (int i = 0; i < model.materialCount; ++i)
        {
            SetShader(shader, i);
        }
    }

    void DynamicRenderable::Draw(
        Vector3 position,
        Vector3 rotationAxis,
        float rotationAngle,
        Vector3 scale,
        Color tint,
        const std::optional<RenderFrustum>& frustum) const
    {
        if (!HasModel()) return;
        if (!frustum)
        {
            DrawModelEx(model, position, rotationAxis, rotationAngle, scale, tint);
            return;
        }
        const Matrix srt = MatrixMultiply(
            MatrixMultiply(
                MatrixScale(scale.x, scale.y, scale.z),
                MatrixRotate(rotationAxis, rotationAngle * math::DEGREES_TO_RADIANS)),
            MatrixTranslate(position.x, position.y, position.z));
        const Matrix transform = MatrixMultiply(model.transform, srt);
        for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex)
        {
            const auto bounds = GetRenderMeshBounds(meshIndex);
            if (bounds && !frustum->Intersects(*bounds, transform)) continue;
            auto& material = model.materials[model.meshMaterial[meshIndex]];
            const Color color = material.maps[MATERIAL_MAP_DIFFUSE].color;
            material.maps[MATERIAL_MAP_DIFFUSE].color = {
                .r = static_cast<unsigned char>(static_cast<int>(color.r) * tint.r / 255),
                .g = static_cast<unsigned char>(static_cast<int>(color.g) * tint.g / 255),
                .b = static_cast<unsigned char>(static_cast<int>(color.b) * tint.b / 255),
                .a = static_cast<unsigned char>(static_cast<int>(color.a) * tint.a / 255)};
            DrawMesh(model.meshes[meshIndex], material, transform);
            material.maps[MATERIAL_MAP_DIFFUSE].color = color;
        }
    }
} // namespace sage

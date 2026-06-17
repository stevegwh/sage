#include "DynamicRenderable.hpp"

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
        name = std::move(other.name);
        hint = other.hint;
        active = other.active;
        initialTransform = other.initialTransform;
        reqShaderUpdate = std::move(other.reqShaderUpdate);

        other.model = {};
        return *this;
    }

    bool DynamicRenderable::HasModel() const
    {
        return model.meshCount > 0 && model.meshes != nullptr;
    }

    Model* DynamicRenderable::GetModel()
    {
        return const_cast<Model*>(std::as_const(*this).GetModel());
    }

    const Model* DynamicRenderable::GetModel() const
    {
        return HasModel() ? &model : nullptr;
    }

    Mesh* DynamicRenderable::GetMesh(int num)
    {
        return const_cast<Mesh*>(std::as_const(*this).GetMesh(num));
    }

    const Mesh* DynamicRenderable::GetMesh(int num) const
    {
        if (!HasModel() || num < 0 || num >= model.meshCount) return nullptr;
        return &model.meshes[num];
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
        Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint) const
    {
        if (!HasModel()) return;
        DrawModelEx(model, position, rotationAxis, rotationAngle, scale, tint);
    }
} // namespace sage

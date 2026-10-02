//
// Created by Steve Wheeler on 03/05/2024.
//

#include "Renderable.hpp"
#include "engine/slib.hpp"

#include <utility>

namespace sage
{
    namespace
    {
        constexpr char MATERIAL_LIST_MARKER = '\x1f';
        constexpr char MATERIAL_KEY_SEPARATOR = '\x1e';
    } // namespace

    std::optional<std::reference_wrapper<const ModelView>> Renderable::GetModel() const
    {
        return std::visit(
            []<class T>(const T& value) -> std::optional<std::reference_wrapper<const ModelView>> {
                if constexpr (std::is_same_v<T, std::monostate>)
                    return std::nullopt;
                else
                    return std::cref(static_cast<const ModelView&>(value));
            },
            model);
    }

    std::optional<std::reference_wrapper<ModelView>> Renderable::GetModel()
    {
        return std::visit(
            []<class T>(T& value) -> std::optional<std::reference_wrapper<ModelView>> {
                if constexpr (std::is_same_v<T, std::monostate>)
                    return std::nullopt;
                else
                    return std::ref(static_cast<ModelView&>(value));
            },
            model);
    }

    std::optional<std::reference_wrapper<ModelMutable>> Renderable::GetMutable()
    {
        if (auto* value = std::get_if<ModelMutable>(&model)) return std::ref(*value);
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<const ModelMutable>> Renderable::GetMutable() const
    {
        if (const auto* value = std::get_if<ModelMutable>(&model)) return std::cref(*value);
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<ModelMutable>> Renderable::EnsureMutable()
    {
        if (auto mutableModel = GetMutable()) return mutableModel;
        const auto currentModel = GetModel();
        if (!currentModel) return std::nullopt;
        auto mutableModel = ResourceManager::GetInstance().CreateModelMutable(currentModel->get().GetKey());
        mutableModel.SetTransform(currentModel->get().GetTransform());
        model = std::move(mutableModel);
        return GetMutable();
    }

    void Renderable::SetModel(ModelView _model)
    {
        model = std::move(_model);
        ResetMaterialKeys();
    }

    void Renderable::SetModel(ModelMutable _model)
    {
        model = std::move(_model);
        ResetMaterialKeys();
    }

    const std::vector<std::string>& Renderable::GetMaterialKeys() const
    {
        return materialKeys;
    }

    bool Renderable::SetMaterialKey(const unsigned int materialIndex, const std::string& materialKey)
    {
        auto currentModel = GetModel();
        if (!currentModel || std::cmp_greater_equal(materialIndex, currentModel->get().GetMaterialCount()) ||
            materialKey.empty())
        {
            return false;
        }
        if (materialKeys.size() != static_cast<std::size_t>(currentModel->get().GetMaterialCount()))
            ResetMaterialKeys();
        if (materialKeys.at(materialIndex) == materialKey) return false;

        auto mutableModel = EnsureMutable();
        if (!mutableModel) return false;
        mutableModel->get().SetMaterial(materialIndex, ResourceManager::GetInstance().GetMaterial(materialKey));
        materialKeys.at(materialIndex) = materialKey;
        return true;
    }

    void Renderable::ResetMaterialKeys()
    {
        const auto currentModel = GetModel();
        if (!currentModel)
        {
            materialKeys.clear();
            return;
        }

        materialKeys = ResourceManager::GetInstance().GetModelMaterialKeys(currentModel->get().GetKey());
        materialKeys.resize(static_cast<std::size_t>(currentModel->get().GetMaterialCount()));
    }

    std::string Renderable::SerializedModelKey(const std::string& modelKey) const
    {
        if (modelKey.empty() || materialKeys.empty() ||
            materialKeys == ResourceManager::GetInstance().GetModelMaterialKeys(modelKey))
        {
            return modelKey;
        }

        std::string serialized = modelKey;
        serialized.push_back(MATERIAL_LIST_MARKER);
        for (std::size_t i = 0; i < materialKeys.size(); ++i)
        {
            if (i > 0) serialized.push_back(MATERIAL_KEY_SEPARATOR);
            serialized += materialKeys.at(i);
        }
        return serialized;
    }

    std::vector<std::string> Renderable::ParseMaterialKeys(std::string& modelKey)
    {
        const auto marker = modelKey.find(MATERIAL_LIST_MARKER);
        if (marker == std::string::npos) return {};

        std::vector<std::string> keys;
        std::size_t start = marker + 1;
        while (start <= modelKey.size())
        {
            const auto end = modelKey.find(MATERIAL_KEY_SEPARATOR, start);
            keys.push_back(modelKey.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        modelKey.resize(marker);
        return keys;
    }

    void Renderable::Enable()
    {
        active = true;
    }

    void Renderable::Disable()
    {
        active = false;
    }

    Renderable::Renderable(ModelView _model, Matrix _localTransform)
        : model(std::move(_model)), initialTransform(_localTransform)
    {
        std::get<ModelView>(model).SetTransform(_localTransform);
        ResetMaterialKeys();
    }

    Renderable::Renderable(ModelMutable _model, Matrix _localTransform)
        : model(std::move(_model)), initialTransform(_localTransform)
    {
        std::get<ModelMutable>(model).SetTransform(_localTransform);
        ResetMaterialKeys();
    }
} // namespace sage

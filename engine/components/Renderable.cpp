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
        constexpr char MaterialListMarker = '\x1f';
        constexpr char MaterialKeySeparator = '\x1e';
    } // namespace

    const ModelView* Renderable::GetModel() const
    {
        return std::visit(
            []<typename T0>(const T0& m) -> const ModelView* {
                using T = std::decay_t<T0>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    return nullptr;
                }
                else
                {
                    return &m;
                }
            },
            model);
    }

    ModelView* Renderable::GetModel()
    {
        return const_cast<ModelView*>(std::as_const(*this).GetModel());
    }

    ModelMutable* Renderable::GetMutable()
    {
        return std::get_if<ModelMutable>(&model);
    }

    const ModelMutable* Renderable::GetMutable() const
    {
        return std::get_if<ModelMutable>(&model);
    }

    ModelMutable* Renderable::EnsureMutable()
    {
        if (auto* mutableModel = GetMutable()) return mutableModel;
        const auto* currentModel = GetModel();
        if (currentModel == nullptr) return nullptr;

        auto mutableModel = ResourceManager::GetInstance().CreateModelMutable(currentModel->GetKey());
        mutableModel.SetTransform(currentModel->GetTransform());
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
        auto* currentModel = GetModel();
        if (currentModel == nullptr ||
            materialIndex >= static_cast<unsigned int>(currentModel->GetMaterialCount()) || materialKey.empty())
        {
            return false;
        }
        if (materialKeys.size() != static_cast<std::size_t>(currentModel->GetMaterialCount())) ResetMaterialKeys();
        if (materialKeys[materialIndex] == materialKey) return false;

        auto* mutableModel = EnsureMutable();
        if (mutableModel == nullptr) return false;
        mutableModel->SetMaterial(materialIndex, ResourceManager::GetInstance().GetMaterial(materialKey));
        materialKeys[materialIndex] = materialKey;
        return true;
    }

    void Renderable::ResetMaterialKeys()
    {
        const auto* currentModel = GetModel();
        if (currentModel == nullptr)
        {
            materialKeys.clear();
            return;
        }

        materialKeys = ResourceManager::GetInstance().GetModelMaterialKeys(currentModel->GetKey());
        materialKeys.resize(static_cast<std::size_t>(currentModel->GetMaterialCount()));
    }

    std::string Renderable::SerializedModelKey(const std::string& modelKey) const
    {
        if (modelKey.empty() || materialKeys.empty() ||
            materialKeys == ResourceManager::GetInstance().GetModelMaterialKeys(modelKey))
        {
            return modelKey;
        }

        std::string serialized = modelKey;
        serialized.push_back(MaterialListMarker);
        for (std::size_t i = 0; i < materialKeys.size(); ++i)
        {
            if (i > 0) serialized.push_back(MaterialKeySeparator);
            serialized += materialKeys[i];
        }
        return serialized;
    }

    std::vector<std::string> Renderable::ParseMaterialKeys(std::string& modelKey)
    {
        const auto marker = modelKey.find(MaterialListMarker);
        if (marker == std::string::npos) return {};

        std::vector<std::string> keys;
        std::size_t start = marker + 1;
        while (start <= modelKey.size())
        {
            const auto end = modelKey.find(MaterialKeySeparator, start);
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

    Renderable::Renderable(ModelView _model, Matrix _localTransform) : initialTransform(_localTransform)
    {
        _model.SetTransform(_localTransform);
        model = std::move(_model);
        ResetMaterialKeys();
    }

    Renderable::Renderable(ModelMutable _model, Matrix _localTransform) : initialTransform(_localTransform)
    {
        _model.SetTransform(_localTransform);
        model = std::move(_model);
        ResetMaterialKeys();
    }
} // namespace sage

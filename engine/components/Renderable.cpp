//
// Created by Steve Wheeler on 03/05/2024.
//

#include "Renderable.hpp"
#include "engine/slib.hpp"

#include <utility>

namespace sage
{

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

    void Renderable::SetModel(ModelView _model)
    {
        model = std::move(_model);
    }

    void Renderable::SetModel(ModelMutable _model)
    {
        model = std::move(_model);
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
    }

    Renderable::Renderable(ModelMutable _model, Matrix _localTransform) : initialTransform(_localTransform)
    {
        _model.SetTransform(_localTransform);
        model = std::move(_model);
    }
} // namespace sage

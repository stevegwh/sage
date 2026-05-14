//
// Created by Steve Wheeler on 03/05/2024.
//

#include "Renderable.hpp"
#include "engine/slib.hpp"

#include <algorithm>
#include <regex>

namespace sage
{

    std::string trim(const std::string& str)
    {
        const auto start = str.find_first_not_of(" \t\n\r");
        const auto end = str.find_last_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
    }

    const std::string& Renderable::GetName() const
    {
        return name;
    }

    void Renderable::SetName(const std::string& _name)
    {
        name = _name;
        setVanityName();
    }

    void Renderable::setVanityName()
    {
        std::string vanity = name;

        if (name[0] == '_') // Remove tag
        {
            if (const auto endPos = name.substr(1).find_first_of('_'); endPos != std::string::npos)
            {
                vanity = name.substr(endPos + 1);
            }
        }

        std::ranges::replace(vanity, '_', ' ');
        vanity = std::regex_replace(vanity, std::regex(R"(\s*\(\d+\)\s*)"), "");

        vanity = trim(vanity);

        vanityName = TitleCase(vanity);
    }

    std::string Renderable::GetVanityName() const
    {
        return vanityName;
    }

    ModelView* Renderable::GetModel()
    {
        return std::visit(
            [](auto& m) -> ModelView* {
                using T = std::decay_t<decltype(m)>;
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

    const ModelView* Renderable::GetModel() const
    {
        return std::visit(
            [](const auto& m) -> const ModelView* {
                using T = std::decay_t<decltype(m)>;
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

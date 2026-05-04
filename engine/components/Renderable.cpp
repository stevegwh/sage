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

    ModelSafe* Renderable::GetModel() const
    {
        // assert(model != nullptr);
        return model.get();
    }

    void Renderable::SetModel(Model _model)
    {
        model = std::make_unique<ModelSafeUnique>(_model);
    }

    void Renderable::SetModel(ModelSafe _model)
    {
        model = std::make_unique<ModelSafe>(std::move(_model));
    }

    void Renderable::SetModel(ModelSafeUnique _model)
    {
        model = std::make_unique<ModelSafeUnique>(std::move(_model));
    }

    void Renderable::Enable()
    {
        active = true;
    }

    void Renderable::Disable()
    {
        active = false;
    }

    // Copy constructor — only meaningful when the source holds a shared ModelSafe.
    // Copying a Renderable that holds a ModelSafeUnique would either double-release
    // the deep-copy entry or duplicate procedural ownership, neither of which is sound.
    Renderable::Renderable(const Renderable& other)
        : name(other.name),
          vanityName(other.vanityName),
          hint(other.hint),
          active(other.active),
          initialTransform(other.initialTransform),
          reqShaderUpdate(other.reqShaderUpdate),
          serializable(other.serializable)
    {
        if (other.model)
        {
            assert(
                dynamic_cast<ModelSafeUnique*>(other.model.get()) == nullptr &&
                "Renderable copy is only valid for shared (non-unique) models");
            model = std::make_unique<ModelSafe>();
            model->rlmodel = other.model->rlmodel;
            model->modelKey = other.model->modelKey;
        }
    }

    Renderable& Renderable::operator=(const Renderable& other)
    {
        if (this != &other)
        {
            name = other.name;
            vanityName = other.vanityName;
            hint = other.hint;
            active = other.active;
            initialTransform = other.initialTransform;
            reqShaderUpdate = other.reqShaderUpdate;
            serializable = other.serializable;

            if (other.model)
            {
                assert(
                    dynamic_cast<ModelSafeUnique*>(other.model.get()) == nullptr &&
                    "Renderable copy is only valid for shared (non-unique) models");
                model = std::make_unique<ModelSafe>();
                model->rlmodel = other.model->rlmodel;
                model->modelKey = other.model->modelKey;
            }
            else
            {
                model.reset();
            }
        }
        return *this;
    }

    // Move constructor
    Renderable::Renderable(Renderable&& other) noexcept
        : model(std::move(other.model)),
          name(std::move(other.name)),
          vanityName(std::move(other.vanityName)),
          hint(other.hint),
          active(other.active),
          initialTransform(other.initialTransform),
          reqShaderUpdate(std::move(other.reqShaderUpdate)),
          serializable(other.serializable)
    {
    }

    // Move assignment operator
    Renderable& Renderable::operator=(Renderable&& other) noexcept
    {
        if (this != &other)
        {
            model = std::move(other.model);
            name = std::move(other.name);
            vanityName = std::move(other.vanityName);
            hint = other.hint;
            active = other.active;
            initialTransform = other.initialTransform;
            reqShaderUpdate = std::move(other.reqShaderUpdate);
            serializable = other.serializable;
        }
        return *this;
    }

    Renderable::Renderable(std::unique_ptr<ModelSafe> _model, Matrix _localTransform)
        : model(std::move(_model)), initialTransform(_localTransform)
    {
        model->SetTransform(_localTransform);
    }

    Renderable::Renderable(Model _model, Matrix _localTransform)
        : model(std::make_unique<ModelSafeUnique>(_model)), initialTransform(_localTransform)
    {
        model->SetTransform(_localTransform);
    }

    Renderable::Renderable(ModelSafe _model, Matrix _localTransform)
        : model(std::make_unique<ModelSafe>(std::move(_model))), initialTransform(_localTransform)
    {
        model->SetTransform(_localTransform);
    }

    Renderable::Renderable(ModelSafeUnique _model, Matrix _localTransform)
        : model(std::make_unique<ModelSafeUnique>(std::move(_model))), initialTransform(_localTransform)
    {
        model->SetTransform(_localTransform);
    }
} // namespace sage
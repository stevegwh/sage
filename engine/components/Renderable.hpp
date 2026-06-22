//
// Created by Steve Wheeler on 18/02/2024.
//

#pragma once

#include "engine/raylib-cereal.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/slib.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace sage
{

    // Emplace this "tag" to draw this renderable "last" (or, at least, with the other deferred renderables)
    struct RenderableDeferred
    {
    };

    class Renderable
    {
        std::variant<std::monostate, ModelView, ModelMutable> model;
        std::vector<std::string> materialKeys;

        void ResetMaterialKeys();
        [[nodiscard]] std::string SerializedModelKey(const std::string& modelKey) const;
        static std::vector<std::string> ParseMaterialKeys(std::string& modelKey);

      public:
        Color hint = WHITE;
        bool active = true;
        Matrix initialTransform{};
        std::function<void(entt::entity)> reqShaderUpdate;
        bool serializable = true;

        // Returns the underlying view (ModelView pointer; also valid when holding a
        // ModelMutable, since it derives from ModelView). Returns nullptr if the
        // Renderable has no model (default-constructed / monostate).
        [[nodiscard]] ModelView* GetModel();
        [[nodiscard]] const ModelView* GetModel() const;

        // Returns a pointer to the mutable view if this Renderable holds one,
        // otherwise nullptr. Use when you need to call mutating methods
        // (SetTexture, SetMaterial) that don't exist on ModelView.
        [[nodiscard]] ModelMutable* GetMutable();
        [[nodiscard]] const ModelMutable* GetMutable() const;
        // Promotes a shared model view to an entity-local copy before changing
        // materials, textures, or shaders.
        [[nodiscard]] ModelMutable* EnsureMutable();

        void SetModel(ModelView _model);
        void SetModel(ModelMutable _model);
        [[nodiscard]] const std::vector<std::string>& GetMaterialKeys() const;
        bool SetMaterialKey(unsigned int materialIndex, const std::string& materialKey);

        void Enable();
        void Disable();

        Renderable() = default;
        ~Renderable() = default;
        Renderable(const Renderable&) = default;
        Renderable& operator=(const Renderable&) = default;
        Renderable(Renderable&&) noexcept = default;
        Renderable& operator=(Renderable&&) noexcept = default;

        Renderable(ModelView _model, Matrix _localTransform);
        Renderable(ModelMutable _model, Matrix _localTransform);

        template <class Archive>
        void save(Archive& archive) const
        {
            std::uint8_t kind = 0;
            std::string key;
            if (const auto* mut = std::get_if<ModelMutable>(&model))
            {
                kind = 2;
                key = mut->GetKey();
            }
            else if (const auto* view = std::get_if<ModelView>(&model))
            {
                kind = 1;
                key = view->GetKey();
            }
            key = SerializedModelKey(key);
            archive(kind, key, initialTransform);
        }

        template <class Archive>
        void load(Archive& archive)
        {
            std::uint8_t kind = 0;
            std::string key;
            archive(kind, key, initialTransform);
            const auto loadedMaterialKeys = ParseMaterialKeys(key);

            if (kind == 1)
            {
                ModelView view = ResourceManager::GetInstance().GetModelView(key);
                assert(view.GetRlModel().meshes != nullptr);
                view.SetTransform(initialTransform);
                model = std::move(view);
            }
            else if (kind == 2)
            {
                ModelMutable mut = ResourceManager::GetInstance().CreateModelMutable(key);
                assert(mut.GetRlModel().meshes != nullptr);
                mut.SetTransform(initialTransform);
                model = std::move(mut);
            }
            else
            {
                model = std::monostate{};
            }

            ResetMaterialKeys();
            for (unsigned int i = 0; i < loadedMaterialKeys.size() && i < materialKeys.size(); ++i)
            {
                if (loadedMaterialKeys[i] != materialKeys[i]) SetMaterialKey(i, loadedMaterialKeys[i]);
            }
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("Active", active);
            i.field("Serializable", serializable);
            i.field("Hint", hint);
        }
    };
} // namespace sage

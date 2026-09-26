#pragma once

#include "cereal/cereal.hpp"

#include "engine/components/Renderable.hpp"

#include "cereal/types/string.hpp"
#include "raylib.h"

#include <string>

namespace sage
{
    // Custom shader assignment for a Renderable. GPU handles and uniform
    // locations are derived at runtime from the persisted file/uniform names.
    struct CustomShaderComponent
    {
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
        std::string timeUniform = "seconds";
        std::string secondTextureUniform = "texture1";
        std::string texture0Key;
        std::string texture1Key;

        void Update(Renderable& renderable);
        void RebindOnNextUpdate()
        {
            appliedMaterials = nullptr;
        }

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("vertexShaderPath", vertexShaderPath), cereal::make_nvp("fragmentShaderPath", fragmentShaderPath), cereal::make_nvp("timeUniform", timeUniform), cereal::make_nvp("secondTextureUniform", secondTextureUniform), cereal::make_nvp("texture0Key", texture0Key), cereal::make_nvp("texture1Key", texture1Key));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Renderable>();
            i.vertexShaderFile("vertex_shader", "Vertex Shader", vertexShaderPath);
            i.fragmentShaderFile("fragment_shader", "Fragment Shader", fragmentShaderPath);
            i.field("time_uniform", "Time Uniform", timeUniform);
            i.field("second_texture_uniform", "Second Texture Uniform", secondTextureUniform);
            i.textureDropdown("texture_0", "Texture 0", texture0Key);
            i.textureDropdown("texture_1", "Texture 1", texture1Key);
        }

      private:
        std::string appliedVertexShaderPath;
        std::string appliedFragmentShaderPath;
        std::string appliedTimeUniform;
        std::string appliedSecondTextureUniform;
        std::string appliedTexture0Key;
        std::string appliedTexture1Key;
        Shader shader{};
        int timeLocation = -1;
        const Material* appliedMaterials = nullptr;
        Texture originalTexture0{};
        Texture originalTexture1{};
    };
} // namespace sage

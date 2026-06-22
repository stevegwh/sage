#pragma once

#include "engine/components/Renderable.hpp"

#include "cereal/types/string.hpp"
#include "raylib.h"

#include <string>

namespace sage
{
    // Authored custom shader assignment for a Renderable. GPU handles and uniform
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
            archive(
                vertexShaderPath, fragmentShaderPath, timeUniform, secondTextureUniform, texture0Key, texture1Key);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<Renderable>();
            i.vertexShaderFile("Vertex Shader", vertexShaderPath);
            i.fragmentShaderFile("Fragment Shader", fragmentShaderPath);
            i.field("Time Uniform", timeUniform);
            i.field("Second Texture Uniform", secondTextureUniform);
            i.textureDropdown("Texture 0", texture0Key);
            i.textureDropdown("Texture 1", texture1Key);
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

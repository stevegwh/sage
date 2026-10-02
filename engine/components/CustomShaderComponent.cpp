#include "CustomShaderComponent.hpp"
#include "engine/SimulationClock.hpp"

#include "engine/ResourceManager.hpp"

namespace sage
{
    void CustomShaderComponent::Update(Renderable& renderable)
    {
        auto model = renderable.EnsureMutable();
        if (!model) return;

        const bool assignmentChanged =
            vertexShaderPath != appliedVertexShaderPath || fragmentShaderPath != appliedFragmentShaderPath ||
            timeUniform != appliedTimeUniform || secondTextureUniform != appliedSecondTextureUniform ||
            texture0Key != appliedTexture0Key || texture1Key != appliedTexture1Key;
        const auto* materials = model->get().GetRlModel().materials;
        const bool materialsChanged =
            appliedMaterials ? appliedMaterials->data() != materials : materials != nullptr;
        if (assignmentChanged || shader.id == 0 || materialsChanged)
        {
            if (materialsChanged && model->get().GetMaterialCount() > 0)
            {
                originalTexture0 = materials[0].maps[MATERIAL_MAP_DIFFUSE].texture;
                originalTexture1 = materials[0].maps[MATERIAL_MAP_EMISSION].texture;
            }
            shader = ResourceManager::GetInstance().ShaderLoad(
                vertexShaderPath.empty() ? std::nullopt : std::make_optional(vertexShaderPath),
                fragmentShaderPath.empty() ? std::nullopt : std::make_optional(fragmentShaderPath));
            timeLocation = timeUniform.empty() ? -1 : GetShaderLocation(shader, timeUniform.c_str());
            if (!secondTextureUniform.empty())
            {
                shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, secondTextureUniform.c_str());
            }
            if (model->get().GetMaterialCount() > 0)
            {
                const auto texture0 = texture0Key.empty()
                                          ? originalTexture0
                                          : ResourceManager::GetInstance().TextureLoad(texture0Key);
                const auto texture1 = texture1Key.empty()
                                          ? originalTexture1
                                          : ResourceManager::GetInstance().TextureLoad(texture1Key);
                model->get().SetTexture(texture0, 0, MATERIAL_MAP_DIFFUSE);
                model->get().SetTexture(texture1, 0, MATERIAL_MAP_EMISSION);
            }
            model->get().SetShader(shader);

            appliedVertexShaderPath = vertexShaderPath;
            appliedFragmentShaderPath = fragmentShaderPath;
            appliedTimeUniform = timeUniform;
            appliedSecondTextureUniform = secondTextureUniform;
            appliedTexture0Key = texture0Key;
            appliedTexture1Key = texture1Key;
            appliedMaterials =
                std::span<const Material>{materials, static_cast<std::size_t>(model->get().GetMaterialCount())};
        }

        if (timeLocation >= 0)
        {
            const auto seconds = static_cast<float>(sage::Time());
            SetShaderValue(shader, timeLocation, &seconds, SHADER_UNIFORM_FLOAT);
        }
    }
} // namespace sage

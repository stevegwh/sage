#include "CustomShaderComponent.hpp"

#include "engine/ResourceManager.hpp"

namespace sage
{
    void CustomShaderComponent::Update(Renderable& renderable)
    {
        auto* model = renderable.EnsureMutable();
        if (model == nullptr) return;

        const bool assignmentChanged =
            vertexShaderPath != appliedVertexShaderPath || fragmentShaderPath != appliedFragmentShaderPath ||
            timeUniform != appliedTimeUniform || secondTextureUniform != appliedSecondTextureUniform ||
            texture0Key != appliedTexture0Key || texture1Key != appliedTexture1Key;
        const auto* materials = model->GetRlModel().materials;
        const bool materialsChanged = appliedMaterials != materials;
        if (assignmentChanged || shader.id == 0 || materialsChanged)
        {
            if (materialsChanged && model->GetMaterialCount() > 0)
            {
                originalTexture0 = materials[0].maps[MATERIAL_MAP_DIFFUSE].texture;
                originalTexture1 = materials[0].maps[MATERIAL_MAP_EMISSION].texture;
            }
            shader = ResourceManager::GetInstance().ShaderLoad(
                vertexShaderPath.empty() ? nullptr : vertexShaderPath.c_str(),
                fragmentShaderPath.empty() ? nullptr : fragmentShaderPath.c_str());
            timeLocation = timeUniform.empty() ? -1 : GetShaderLocation(shader, timeUniform.c_str());
            if (!secondTextureUniform.empty())
            {
                shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, secondTextureUniform.c_str());
            }
            if (model->GetMaterialCount() > 0)
            {
                const auto texture0 = texture0Key.empty()
                                          ? originalTexture0
                                          : ResourceManager::GetInstance().TextureLoad(texture0Key);
                const auto texture1 = texture1Key.empty()
                                          ? originalTexture1
                                          : ResourceManager::GetInstance().TextureLoad(texture1Key);
                model->SetTexture(texture0, 0, MATERIAL_MAP_DIFFUSE);
                model->SetTexture(texture1, 0, MATERIAL_MAP_EMISSION);
            }
            model->SetShader(shader);

            appliedVertexShaderPath = vertexShaderPath;
            appliedFragmentShaderPath = fragmentShaderPath;
            appliedTimeUniform = timeUniform;
            appliedSecondTextureUniform = secondTextureUniform;
            appliedTexture0Key = texture0Key;
            appliedTexture1Key = texture1Key;
            appliedMaterials = materials;
        }

        if (timeLocation >= 0)
        {
            const float seconds = static_cast<float>(GetTime());
            SetShaderValue(shader, timeLocation, &seconds, SHADER_UNIFORM_FLOAT);
        }
    }
} // namespace sage

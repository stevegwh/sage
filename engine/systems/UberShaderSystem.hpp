//
// Created by steve on 21/11/2024.
//

#pragma once

#include "entt/entt.hpp"
#include "raylib.h"

namespace sage
{

    class EngineSystems;

    class UberShaderSystem
    {
        entt::registry* registry;
        EngineSystems* sys;
        Shader shader{};
        int litLoc;
        int skinnedLoc;
        int hasEmissiveTexLoc;
        int hasEmissiveColLoc;
        int colEmissionLoc;

        void onComponentAdded(entt::entity entity);
        void onComponentRemoved(entt::entity entity);

      public:
        // Rebinds the uber shader after a Renderable swaps or reloads its model.
        // Existing authored flags (Lit/Skinned) are preserved while material-derived
        // state is refreshed from the replacement model.
        void RebindRenderable(entt::entity entity);
        UberShaderSystem(entt::registry* _registry, EngineSystems* _sys);
    };

} // namespace sage

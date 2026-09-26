//
// Created by Steve Wheeler on 11/06/2026.
//

#pragma once

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"

#include <string>

namespace sage
{
    // Reference to a managed Sage.Script type. Runtime instances live in
    // CSharpScriptSystem and are recreated for every Play session.
    struct ScriptComponent
    {
        // Fully-qualified type name, e.g. "HeroHerder.Scripts.Wizard".
        // This remains the first serialized string so existing maps can be migrated
        // without changing their binary record shape.
        std::string className;
        bool enabled = true;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("className", className), cereal::make_nvp("enabled", enabled));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.scriptFile("class", "Class", className);
            i.field("enabled", "Enabled", enabled);
        }
    };
} // namespace sage

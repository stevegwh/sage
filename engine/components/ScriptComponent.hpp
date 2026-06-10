//
// Created by Steve Wheeler on 11/06/2026.
//

#pragma once

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"

#include <string>

namespace sage
{
    // Attaches a Lua script to an entity. ScriptSystem loads the file into its own
    // environment and drives Unity-style lifecycle callbacks defined as globals in
    // the script: Awake(), OnEnable(), Start(), Update(dt), OnDisable().
    //
    // Runtime state (the Lua environment, cached function refs) lives inside
    // ScriptSystem, keyed by entity — this component is just the authored data.
    struct ScriptComponent
    {
        // Path relative to the working directory, e.g. "resources/scripts/door.lua".
        std::string scriptPath;
        bool enabled = true;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(scriptPath, enabled);
        }

        template <class Inspector>
        void define_editor_fields(Inspector& i)
        {
            i.field("Script Path", scriptPath);
            i.field("Enabled", enabled);
        }
    };
} // namespace sage

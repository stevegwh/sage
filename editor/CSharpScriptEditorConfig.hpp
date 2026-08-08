#pragma once

#include <filesystem>
#include <string>

namespace sage::editor
{
    // Project-owned information the generic editor needs to map a managed type
    // such as HeroHerder.Scripts.Ariana to its C# source file.
    struct CSharpScriptEditorConfig
    {
        std::filesystem::path sourceDirectory;
        std::string rootNamespace;

        [[nodiscard]] bool IsConfigured() const
        {
            return !sourceDirectory.empty() && !rootNamespace.empty();
        }
    };
} // namespace sage::editor

#include "engine/scripting/EngineScriptApi.hpp"
#include "engine/scripting/ScriptApi.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(const int argc, const char* const* argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: sage_script_api_codegen <output.cs>\n";
        return EXIT_FAILURE;
    }
    sage::ScriptApiRegistry api;
    sage::RegisterEngineScriptApi(api);
    if (api.WriteCSharp(std::filesystem::path{argv[1]})) return EXIT_SUCCESS;
    std::cerr << "Could not write the generated Sage C# component API.\n";
    return EXIT_FAILURE;
}

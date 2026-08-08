# SAGE (Steve's Awesome Game Engine)

SAGE is a C++20 game engine and editor layer built on top of raylib and EnTT. It provides the reusable runtime,
tooling, rendering, scripting, serialization, and editor systems used by Hero Herder.

## Core Features

- Integrated editor for building maps, placing entities, editing components, and testing changes in play mode.
- Terrain tools for sculpting heightfields, authoring navigation surfaces, and aligning gameplay objects to the world.
- Navigation and movement systems designed for point-and-click controls, including multi-tile footprints, obstacle
  avoidance, and smooth actor turning.
- Collision and trigger system with raycasts, box/mesh queries, collision layers, and an editable collision matrix.
- Rendering stack for dynamic models, skinned animation, emissive materials, skyboxes, custom shaders, and particles.
- Typed C# scripting for entity behaviours, gameplay events, and project-specific game APIs.
- Custom UI framework for in-game windows, tables, tooltips, drag-and-drop, and themed HUD overlays.
- Editor undo/redo history, including hierarchy edits, component changes, transforms, terrain sculpting,
  and multi-selection edits.
- Packed asset and serialization pipeline for loading game resources, saving editor maps, and reusing groups of entities
  as flatpacks.

## Dependencies

C++20, CMake, .NET 10, raylib, EnTT, cereal, ImGui, and magic_enum.

## C# component APIs

Script-facing component data stays beside the C++ component in a language-neutral
`define_script_api` declaration:

```cpp
template <class Api>
static void define_script_api(Api& api)
{
    api.property("Active", &Collideable::active);
    api.readonly("ClipCount", &Animation::animsCount);
    api.method(
        "Play",
        static_cast<bool (Animation::*)(std::string_view)>(&Animation::ChangeAnimationByName),
        {"clip"});
}
```

Register the component once with `ScriptApiRegistry::RegisterComponent`. The native
runtime consumes the declaration for safe entity/component dispatch, and the CMake
build generates the matching typed C# wrapper under `managed/**/Generated/`.

Properties and methods currently support booleans, signed and unsigned integers,
floats, strings, `Vector3`, entity handles, and registered enums. Computed properties
can provide getter/setter lambdas when direct member access would bypass a system
invariant, as the transform bindings do.

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
- Lua scripting layer for entity behaviours, gameplay events, and project-specific game APIs.
- Custom UI framework for in-game windows, tables, tooltips, drag-and-drop, and themed HUD overlays.
- Undo/redo history for editor workflows, including hierarchy edits, component changes, transforms, terrain sculpting,
  and multi-selection edits.
- Packed asset and serialization pipeline for loading game resources, saving editor maps, and reusing groups of entities
  as flatpacks.

## Dependencies

C++20, CMake, raylib, EnTT, Lua, sol2, cereal, ImGui, and magic_enum.

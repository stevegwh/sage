# SAGE (Steve's Awesome Game Engine)

SAGE is a C++20 game engine and editor layer built on top of raylib and EnTT. It provides the reusable runtime,
tooling, rendering, scripting, serialization, and editor systems used by Hero Herder.

## Core Features

- ECS runtime with hierarchical transforms and engine-owned systems for rendering, physics, navigation, scripting, UI,
  and audio.
- 3D rendering with dynamic models, skinned animation, emissive materials, skyboxes, custom shaders, particles, and an
  uber shader.
- Packed asset pipeline and resource manager for models, materials, textures, shaders, audio, fonts, and animations.
- Cereal-based serialization for engine data, raylib types, packed resources, editor maps, and prefab-style flatpacks.
- Collision and trigger system with raycasts, box/mesh queries, layers, and an editable collision matrix.
- Navigation and actor movement with terrain sampling, occupancy, A*, BFS, footprint-aware movement, path events, and
  smooth turning.
- Lua scripting with per-entity lifecycles, isolated environments, hot reload, event subscriptions, and game API
  extension points.
- Custom game UI framework with windows, tables, tooltips, drag-and-drop, themes, scrollbars, and overlay rendering.
- Full editor with scene hierarchy, inspector, asset browser, placement tools, transform gizmos, terrain sculpting,
  collision/light editing, play-in-editor, and flatpack editing.
- Incremental editor undo/redo for touched entity subtrees, with multi-selection, mixed-value editing, copy/paste,
  reparenting, snapping, and dirty-state prompts.

## Dependencies

C++20, CMake, raylib, EnTT, Lua, sol2, cereal, ImGui, and magic_enum.

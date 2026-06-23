# SAGE (Steve's Awesome Game Engine)

SAGE is a C++20 game engine and editor layer built on top of raylib and EnTT. It provides the reusable runtime,
tooling, rendering, scripting, serialization, and editor systems used by Hero Herder.

## Core Features

- EnTT-based ECS runtime with engine-owned systems for transforms, rendering, collision, navigation, animation,
  scripting, UI, audio, and cleanup.
- Hierarchical transforms with parent-child relationships and dirty propagation.
- 3D rendering pipeline with renderable components, dynamic renderables, skyboxes, custom shaders, skinned models,
  emissive materials, and an uber shader system.
- Resource manager for models, materials, textures, images, shaders, fonts, sounds, music, and model animations.
- Serialized asset/resource packs for fast runtime loading.
- Cereal-backed binary, JSON, and XML serialization, including raylib math/graphics types.
- Editor-authored map format with hierarchy, transforms, renderables, collision, lighting, terrain, scripts,
  archetypes, and project-specific component payloads.
- Flatpack prefab system for saving, editing, cataloging, and instantiating reusable entity hierarchies.
- Collision system with box/mesh queries, raycasts, collision layers, editable collision matrix, and trigger events.
- Navigation grid with terrain height sampling, occupancy tracking, A* pathfinding, BFS pathfinding, and footprint-aware
  movement.
- Actor movement system with path following, destination events, cancellation, recalculation, and turn-speed controlled
  rotation.
- Lua scripting through sol2, with per-entity scripts, lifecycle callbacks, isolated script environments, hot reload,
  and explicit event subscriptions.
- Animation system with model animation playback, Lua animation events, and skinned renderable support.
- 3D billboard particle system with configurable emitters, bursts, blend modes, acceleration, lifetime, color
  transitions, and draw ordering.
- Spatial audio components and runtime audio resource management.
- Custom game UI framework with windows, docked windows, tables, cells, tooltips, drag-and-drop, themes, scrollbars, and
  overlay drawing.
- Full editor application with scene viewport, hierarchy, inspector, asset drawer, placement tools, transform gizmos,
  terrain sculpting, collision matrix editing, light settings, and play-in-editor mode.
- Undo/redo editor history based on touched entity subtrees rather than full-scene snapshots.
- Multi-selection editor workflows, mixed-value inspector editing, copy/paste, reparenting, snapping, and dirty-state
  save prompts.
- Project extension points for custom collision layers, scene tags, cursor types, Lua APIs, archetypes, and persistent
  game-owned components.

## Dependencies

C++20, CMake, raylib, EnTT, Lua, sol2, cereal, ImGui, and magic_enum.

---@meta
--- Hero Herder engine API exposed to Lua scripts.
---
--- This is a *definition file*: it is read by the Lua Language Server
--- (sumneko.lua) for autocomplete and type-checking only. It is never loaded
--- or executed at runtime. The real bindings live in
--- component `define_lua_bindings()`, system `RegisterLuaBindings()`, and
--- ScriptSystem core bindings — keep this file in sync with those definitions.

---An entity id. Returned by the various Find/Get functions and by
---`sage.GetEntity()`. Pass it into `sage.GetTransform(e)` etc.
---@alias entity integer

---Engine functions available to every entity script.
---@class SageApi
sage = {}

--==============================================================================
-- Usertypes
--==============================================================================

---A 3D vector. Construct with `sage.Vec3()` or `sage.Vec3(x, y, z)`.
---@class Vec3
---@field x number
---@field y number
---@field z number
---@operator add(Vec3): Vec3
---@operator sub(Vec3): Vec3
---@operator mul(number): Vec3

---Construct a 3D vector. No args yields (0, 0, 0).
---@overload fun(): Vec3
---@param x number
---@param y number
---@param z number
---@return Vec3
function sage.Vec3(x, y, z) end

---An entity's transform. Cannot be constructed from Lua; obtain one via
---`sage.GetTransform(e)`. Setters route through the transform system so dirty
---propagation to children runs automatically.
---@class Transform
---@field name string Read-only display name.
local Transform = {}

---@return Vec3 # World-space position.
function Transform:GetPosition() end

---@param v Vec3
function Transform:SetPosition(v) end

---@return Vec3 # Position relative to the parent (or world if root).
function Transform:GetLocalPosition() end

---@param v Vec3
function Transform:SetLocalPosition(v) end

---@return Vec3 # World rotation as Euler angles (degrees).
function Transform:GetRotation() end

---@param v Vec3 # Euler angles in degrees.
function Transform:SetRotation(v) end

---@return Vec3 # Scale factors per axis.
function Transform:GetScale() end

---@param v Vec3
function Transform:SetScale(v) end

---@return Vec3 # Unit forward direction.
function Transform:Forward() end

---@return entity? # Parent entity id, or nil if this transform is a root.
function Transform:GetParent() end

---An entity's collider. Cannot be constructed from Lua; obtain one via
---`sage.GetCollideable(e)`.
---@class Collideable
---@field active boolean Whether the collider participates in collision.
---@field debugDraw boolean Draw the collider's bounds for debugging.

---An entity's "kind"/noun identity. Cannot be constructed from Lua; obtain one
---via `sage.GetArchetype(e)`.
---@class Archetype
---@field id integer Read-only archetype id (hashed name).
local Archetype = {}

---@param name string
---@return boolean # True if this archetype matches the given name.
function Archetype:Is(name) end

---An entity's animation player. Cannot be constructed from Lua; obtain one via
---`sage.GetAnimation(e)`. Clip names are GLB animation names (Blender NLA tracks).
---@class Animation
local Animation = {}

---Play (and loop) a clip by name. Returns false if no clip matches.
---@param clip string
---@param speed? integer # Playback speed multiplier (default 1).
---@return boolean
function Animation:Play(clip, speed) end

---Play a clip once (no loop) by name. Returns false if no clip matches.
---@param clip string
---@param speed? integer # Playback speed multiplier (default 1).
---@return boolean
function Animation:PlayOneShot(clip, speed) end

---@return integer # Number of available clips.
function Animation:ClipCount() end

---@return string[] # All clip names.
function Animation:GetClipNames() end

---@param seconds number # Cross-fade duration; clamped to >= 0.
function Animation:SetBlendDuration(seconds) end

---@return number
function Animation:GetBlendDuration() end

---An entity's mood. Cannot be constructed from Lua; obtain one via
---`sage.GetMood(e)`.
---@enum EmoteType
EmoteType = {
    Neutral = 0,
    Happy = 1,
    Sad = 2,
    Angry = 3,
    Bored = 4,
}

---@class Mood
---@field valence integer # Pleasant (+100) to unpleasant (-100).
---@field arousal integer # Activated (+100) to subdued (-100).

--==============================================================================
-- Namespaced engine API
--==============================================================================

---Write a message to the engine log (LOG_INFO, prefixed "Lua: ").
---@param msg string
function sage.Log(msg) end

--==============================================================================
-- Per-entity API (the `sage` table, injected into every script's environment)
--==============================================================================

---This script's own entity id.
---@return entity
function sage.GetEntity() end

---Find the first entity with the given archetype name.
---@param name string
---@return entity? # nil if none found.
function sage.FindFirstWithArchetype(name) end

---Find the first entity carrying the given scene tag.
---@param tag string
---@return entity? # nil if none found.
function sage.FindFirstWithTag(tag) end

---Whether an entity has the given scene tag.
---@param e entity
---@param tag string
---@return boolean
function sage.HasTag(e, tag) end

---All scene tags on an entity.
---@param e entity
---@return string[]
function sage.GetTags(e) end

---Spawn an editor-authored flatpack at a world-space grid location.
---`name` is the flatpack file stem under `resources/flatpacks`.
---@param name string
---@param position Vec3
---@param rotation? Vec3 # Optional Euler rotation in degrees.
---@return entity? # The spawned flatpack root, or nil when no matching flatpack exists.
function sage.SpawnFlatpack(name, position, rotation) end

---Get the topmost active navigation-surface entity at a world-space position.
---@param position Vec3
---@return entity? # nil if the position is outside the grid or has no navigation surface.
function sage.GetNavigationSurfaceAt(position) end

---Move the owning entity directly toward a destination (no pathfinding).
---Requires a Transform.
---@param destination Vec3
---@return boolean # false if the entity has no Transform.
function sage.MoveToLocation(destination) end

---Whether the owning entity currently has a movement route.
---Returns false when it has no MoveableActor or its route is empty.
---@return boolean
function sage.HasRoute() end

---Pathfind the owning entity to a destination. By default, an occupied or
---unreachable destination falls back to the closest reachable grid cell within
---the actor's pathfinding range.
---The destination's x/z coordinates select the grid cell; its y value is ignored.
---Requires Transform + MoveableActor + Collideable.
---@param destination Vec3
---@param astar? boolean # Use A* (otherwise the default search).
---@param findNextBestIfInvalid? boolean # Defaults to true; false requires the exact destination.
---@return boolean # True when a route was created.
function sage.TryPathfindToLocation(destination, astar, findNextBestIfInvalid) end

---Get an entity's Archetype.
---@param e entity
---@return Archetype? # nil if the entity has none.
function sage.GetArchetype(e) end

---Get an entity's Transform.
---@param e entity
---@return Transform? # nil if the entity has no Transform.
function sage.GetTransform(e) end

---Get an entity's Collideable.
---@param e entity
---@return Collideable? # nil if the entity has no Collideable.
function sage.GetCollideable(e) end

---Get an entity's Animation.
---@param e entity
---@return Animation? # nil if the entity has no Animation.
function sage.GetAnimation(e) end

---Get an entity's Mood.
---@param e entity
---@return Mood? # nil if the entity has no Mood.
function sage.GetMood(e) end

--==============================================================================
-- Events
--
-- Subscribe with sage.On<Event>(sourceEntity, callback). The callback receives
-- only the event's own arguments (the source is the entity you passed in). Each
-- returns a subscription id, or nil if the source lacks the required component.
-- Pass sage.GetEntity() to listen to your own entity. Subscriptions stay active
-- while the script is disabled (dispatch pauses) and are removed automatically
-- when the script is destroyed/reloaded or the source's component is destroyed.
--==============================================================================

---An entity started overlapping `e`'s trigger.
---@param e entity
---@param callback fun(other: entity)
---@return integer? subscription
function sage.OnTriggerEnter(e, callback) end

---An entity remains overlapping `e`'s trigger (every frame).
---@param e entity
---@param callback fun(other: entity)
---@return integer? subscription
function sage.OnTriggerStay(e, callback) end

---An entity stopped overlapping `e`'s trigger.
---@param e entity
---@param callback fun(other: entity)
---@return integer? subscription
function sage.OnTriggerExit(e, callback) end

---`e` received a movement path.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnMovementStarted(e, callback) end

---`e` reached the end of its movement path.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnDestinationReached(e, callback) end

---Pathfinding could not reach the requested destination for `e`.
---@param e entity
---@param callback fun(destination: Vec3)
---@return integer? subscription
function sage.OnDestinationUnreachable(e, callback) end

---`e`'s movement was cancelled.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnMovementCancelled(e, callback) end

---`e` was rerouted while already moving.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnPathChanged(e, callback) end

---`e`'s animation started.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnAnimationStarted(e, callback) end

---`e`'s animation ended.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnAnimationEnded(e, callback) end

---`e`'s animation updated.
---@param e entity
---@param callback fun()
---@return integer? subscription
function sage.OnAnimationUpdated(e, callback) end

---Remove a subscription returned by a sage.On* function.
---@param subscription integer
---@return boolean # True when an active subscription was removed.
function sage.Unsubscribe(subscription) end

--==============================================================================
-- Lifecycle callbacks
--
-- Define any of these as globals in your script; the engine calls them
-- automatically. All are optional. These are the only magic globals — events are
-- subscribed to explicitly via the sage.On* functions above.
--==============================================================================

---Called once when the script instance is created (even while disabled).
function Awake() end

---Called when the script transitions from disabled to enabled.
function OnEnable() end

---Called once before the first Update while enabled.
function Start() end

---Called every frame while enabled.
---@param dt number # Seconds since the previous frame.
function Update(dt) end

---Called when the script transitions from enabled to disabled (or is destroyed).
function OnDisable() end

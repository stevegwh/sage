#include "ContentDocument.hpp"
#include "ContentInspector.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/DynamicRenderable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Terrain.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/EditorLayoutMapFormat.hpp"
#include "engine/systems/TransformSystem.hpp"
#include "FlatpackRecords.hpp"
#include <chrono>
#include <set>

namespace sage::content
{
    namespace
    {
        constexpr std::uint32_t NULL_ID = entt::entt_traits<entt::entity>::to_entity(entt::entity{entt::null});
        struct StoredTransform
        {
            std::string name;
            Vector3 position{}, rotation{}, scale{1, 1, 1};
            Vector3 localPosition{}, localRotation{}, localScale{1, 1, 1};
            std::uint32_t parent = NULL_ID;
            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(name, position, rotation, scale, localPosition, localRotation, localScale, parent);
            }
        };
        using MapEntity =
            editor_layout::BasicEntityRecord<StoredTransform, content_binary::StoredRenderableRecord>;
        using MapTerrain = editor_layout::BasicTerrainRecord<StoredTransform>;
        const detail::ComponentOperations* FindComponentOperations(const std::string& key)
        {
            EnsureEngineComponentsRegistered();
            for (const auto& operations : detail::RegisteredComponentOperations())
                if (operations.key == key) return &operations;
            return nullptr;
        }
        std::string ComponentKey(entt::id_type type)
        {
            if (type == entt::type_hash<sgTransform>::value()) return "sage.Transform";
            if (type == entt::type_hash<Renderable>::value()) return "sage.Renderable";
            for (const auto& operations : detail::RegisteredComponentOperations())
                if (operations.typeId == type) return operations.key;
            throw std::runtime_error("Component constraint refers to an unregistered type");
        }
        std::string Hex(const std::string& bytes)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string result;
            for (unsigned char byte : bytes)
            {
                result += digits[byte >> 4];
                result += digits[byte & 15];
            }
            return result;
        }
        std::string Unhex(const std::string& bytes)
        {
            if (bytes.size() % 2) throw std::runtime_error("Invalid opaque payload");
            std::string result;
            for (std::size_t i = 0; i < bytes.size(); i += 2)
            {
                auto digit = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    throw std::runtime_error("Invalid opaque payload");
                };
                result += static_cast<char>(digit(bytes[i]) * 16 + digit(bytes[i + 1]));
            }
            return result;
        }
        json::Document Empty(const std::string& kind)
        {
            json::Document doc(rapidjson::kObjectType);
            auto& a = doc.GetAllocator();
            json::Put(doc, "format", "sage-content", a);
            json::Put(doc, "version", std::uint64_t(DOCUMENT_VERSION), a);
            json::Put(doc, "kind", kind, a);
            json::Put(doc, "entities", json::Value(rapidjson::kArrayType), a);
            return doc;
        }
        json::Value Node(
            std::uint32_t id,
            const std::string& name,
            Vector3 position,
            Vector3 rotation,
            Vector3 scale,
            std::uint32_t parent,
            json::Allocator& a)
        {
            json::Value node(rapidjson::kObjectType), transform(rapidjson::kObjectType);
            json::Put(node, "id", std::uint64_t(id), a);
            json::Put(node, "name", name, a);
            if (parent == NULL_ID)
                json::Put(node, "parent", json::Value(), a);
            else
                json::Put(node, "parent", std::uint64_t(parent), a);
            json::Put(transform, "position", json::Encode(position), a);
            json::Put(transform, "rotation", json::Encode(rotation), a);
            json::Put(transform, "scale", json::Encode(scale), a);
            json::Put(node, "transform", transform, a);
            json::Put(node, "components", json::Value(rapidjson::kObjectType), a);
            return node;
        }
        void Component(json::Value& node, const std::string& key, const json::Value& data, json::Allocator& a)
        {
            json::Value wrapper(rapidjson::kObjectType);
            json::Put(wrapper, "version", std::uint64_t(1), a);
            json::Put(wrapper, "data", data, a);
            json::Put(node["components"], key.c_str(), wrapper, a);
        }
        template <class T>
        void Add(json::Value& node, const std::string& key, const T& value, json::Allocator& a)
        {
            Component(node, key, json::Encode(value), a);
        }
        void Opaque(json::Value& node, const std::string& key, const std::string& bytes, json::Allocator& a)
        {
            if (const auto* operations = FindComponentOperations(key))
                Component(node, key, json::Parse(operations->toJson(bytes)), a);
            else
            {
                json::Value wrapper(rapidjson::kObjectType);
                json::Put(wrapper, "version", std::uint64_t(1), a);
                json::Put(wrapper, "encoding", "binary-hex", a);
                json::Put(wrapper, "data", Hex(bytes), a);
                json::Put(node["components"], key.c_str(), wrapper, a);
            }
        }
        json::Value& Entity(json::Value& document, std::uint32_t id)
        {
            for (auto& node : document["entities"].GetArray())
                if (json::Id(node, "id") == id) return node;
            throw std::runtime_error("Unknown entity ID: " + std::to_string(id));
        }
        template <class Record>
        void AddBase(json::Value& node, const Record& r, json::Allocator& a)
        {
            if (r.hasRenderable) Add(node, "sage.Renderable", r.renderable, a);
            if (r.hasCollideable) Add(node, "sage.Collideable", r.collideable, a);
            if (r.hasNavigationSurface) Add(node, "sage.NavigationSurface", r.navigationSurface, a);
            if (r.hasNavigationObstacle) Add(node, "sage.NavigationObstacle", r.navigationObstacle, a);
            if (r.hasTriggerVolume) Add(node, "sage.TriggerVolume", r.triggerVolume, a);
            if (r.hasCursorTarget) Add(node, "sage.CursorTarget", r.cursorTarget, a);
        }
        void RemapReferences(
            json::Value& doc, const std::unordered_map<std::uint32_t, entt::entity>& ids, json::Allocator& a)
        {
            for (auto& node : doc["entities"].GetArray())
                for (auto& m : node["components"].GetObject())
                {
                    if (m.value.HasMember("encoding") || json::Id(m.value, "version") != 1) continue;
                    if (const auto* operations = FindComponentOperations(m.name.GetString()))
                        json::Put(
                            m.value,
                            "data",
                            json::Parse(operations->remapJson(json::Stringify(m.value["data"]), ids)),
                            a);
                }
        }
        void NormalizeLegacyIds(json::Document& doc)
        {
            auto& a = doc.GetAllocator();
            std::unordered_map<std::uint32_t, entt::entity> ids;
            for (auto& node : doc["entities"].GetArray())
            {
                const auto old = json::Id(node, "id");
                ids.emplace(old, static_cast<entt::entity>(old + 1));
                node["id"].SetUint(old + 1);
                if (node["parent"].IsUint()) node["parent"].SetUint(node["parent"].GetUint() + 1);
            }
            if (doc.HasMember("root")) doc["root"].SetUint(doc["root"].GetUint() + 1);
            RemapReferences(doc, ids, a);
        }
        json::Document ReadMap(const std::filesystem::path& path)
        {
            auto doc = Empty("map");
            auto& a = doc.GetAllocator();
            std::vector<Light> lights;
            std::vector<MapEntity> entities;
            std::vector<editor_layout::EntityScriptRecord> scripts;
            std::vector<editor_layout::EntityAnimationRecord> animations;
            std::vector<editor_layout::EntityMoveableActorRecord> actors;
            std::vector<MapTerrain> terrains;
            std::vector<editor_layout::EntityArchetypeRecord> archetypes;
            std::vector<editor_layout::EntityGameComponentRecord> custom;
            serializer::ReadCompressedBinary(
                path.string().c_str(), editor_layout::MAP_MAGIC, [&](auto& input, std::istream& stream) {
                    input(lights, entities, scripts, animations, actors, terrains);
                    if (stream.peek() != std::char_traits<char>::eof()) input(archetypes);
                    if (stream.peek() != std::char_traits<char>::eof()) input(custom);
                });
            std::uint32_t next = 0;
            for (const auto& r : entities)
            {
                next = std::max(next, r.entity.id + 1);
                auto n = Node(
                    r.entity.id,
                    r.transform.name,
                    r.transform.position,
                    r.transform.rotation,
                    r.transform.scale,
                    r.transform.parent,
                    a);
                AddBase(n, r, a);
                if (r.hasCollideable)
                    json::Put(n["components"]["sage.Collideable"]["data"], "isStatic", !r.hasTriggerVolume, a);
                if (r.hasMetaData) Add(n, "sage.MetaData", r.metaData, a);
                doc["entities"].PushBack(n, a);
            }
            for (auto light : lights)
            {
                // Legacy map loaders always enabled saved lights; the binary payload has no enabled field.
                light.enabled = true;
                auto n = Node(next++, "Light", light.position, {}, {1, 1, 1}, NULL_ID, a);
                Add(n, "sage.Light", light, a);
                doc["entities"].PushBack(n, a);
            }
            for (const auto& r : terrains)
            {
                auto n = Node(
                    next++,
                    r.transform.name,
                    r.transform.position,
                    r.transform.rotation,
                    r.transform.scale,
                    NULL_ID,
                    a);
                Terrain terrain;
                terrain.resolution = r.resolution;
                terrain.cellSize = r.cellSize;
                terrain.heights = r.heights;
                Add(n, "sage.Terrain", terrain, a);
                Add(n, "sage.Collideable", r.collideable, a);
                doc["entities"].PushBack(n, a);
            }
            for (const auto& r : scripts)
                Add(Entity(doc, r.targetId), "sage.Script", r.script, a);
            for (const auto& r : animations)
            {
                json::Value data(rapidjson::kObjectType);
                json::Put(data, "modelKey", r.modelKey, a);
                Component(Entity(doc, r.targetId), "sage.Animation", data, a);
            }
            for (const auto& r : actors)
            {
                json::Value data(rapidjson::kObjectType);
                json::Put(data, "movementSpeed", json::Encode(r.movementSpeed), a);
                json::Put(data, "turnSpeed", json::Encode(r.turnSpeed), a);
                json::Put(data, "pathfindingBounds", json::Encode(r.pathfindingBounds), a);
                json::Put(data, "moveClip", r.moveClip, a);
                json::Put(data, "idleClip", r.idleClip, a);
                Component(Entity(doc, r.targetId), "sage.MoveableActor", data, a);
            }
            for (const auto& r : archetypes)
                Add(Entity(doc, r.targetId), "sage.Archetype", r.archetype, a);
            for (const auto& r : custom)
                Opaque(Entity(doc, r.targetId), r.key, r.data, a);
            NormalizeLegacyIds(doc);
            return doc;
        }
        json::Document ReadFlatpack(const std::filesystem::path& path, const std::string& magic)
        {
            if (magic != "LQFP" && magic != "LQF2" && magic != "LQF3" && magic != "LQF4" && magic != "LQF5" &&
                magic != "LQF6")
                throw std::runtime_error("Unsupported flatpack version: " + magic);
            using namespace content_binary;
            auto doc = Empty("flatpack");
            auto& a = doc.GetAllocator();
            MigrationFlatpackData data;
            LegacyMigrationFlatpackData legacy;
            auto read = [&](const auto& bytes) {
                serializer::ReadCompressedBinary(path.string().c_str(), bytes, [&](auto& input, std::istream&) {
                    if (magic == "LQF6")
                        input(data.records, data.names, data.scripts, data.animations);
                    else
                        input(legacy.records, data.names, data.scripts, data.animations);
                    if (magic == "LQFP")
                    {
                        std::vector<LegacyFlatpackMoveableActorRecord> actors;
                        input(actors);
                        for (const auto& r : actors)
                            data.moveables.push_back(
                                {r.localId, r.movementSpeed, 240.f, r.pathfindingBounds, r.moveClip, r.idleClip});
                    }
                    else
                        input(data.moveables);
                    if (magic != "LQFP" && magic != "LQF2") input(data.archetypes);
                    if (magic == "LQF4" || magic == "LQF5" || magic == "LQF6") input(data.customShaders);
                    if (magic == "LQF5" || magic == "LQF6") input(data.customComponents);
                });
            };
            const char bytes[4] = {magic[0], magic[1], magic[2], magic[3]};
            read(bytes);
            for (const auto& r : legacy.records)
            {
                MigrationFlatpackEntityRecord n;
                n.parentLocalId = r.parentLocalId;
                n.worldPos = r.worldPos;
                n.worldRot = r.worldRot;
                n.worldScale = r.worldScale;
                n.hasRenderable = r.hasRenderable;
                n.renderable = r.renderable;
                n.hasCollideable = r.hasCollideable;
                n.collideable = r.collideable;
                n.hasNavigationSurface = r.hasNavigationSurface;
                n.navigationSurface = r.navigationSurface;
                n.hasNavigationObstacle = r.hasNavigationObstacle;
                n.navigationObstacle = r.navigationObstacle;
                n.hasTriggerVolume = r.hasTriggerVolume;
                n.triggerVolume = r.triggerVolume;
                n.hasCursorTarget = r.hasCursorTarget;
                n.cursorTarget = r.cursorTarget.Current();
                n.hasLight = r.hasLight;
                n.light = r.light;
                data.records.push_back(n);
            }
            for (std::uint32_t id = 0; id < data.records.size(); ++id)
            {
                const auto& r = data.records[id];
                auto n = Node(
                    id,
                    id < data.names.size() ? data.names[id] : "",
                    r.worldPos,
                    r.worldRot,
                    r.worldScale,
                    r.parentLocalId < 0 ? NULL_ID : std::uint32_t(r.parentLocalId),
                    a);
                AddBase(n, r, a);
                if (r.hasLight) Add(n, "sage.Light", r.light, a);
                doc["entities"].PushBack(n, a);
            }
            json::Put(doc, "root", std::uint64_t(0), a);
            for (const auto& r : data.scripts)
                Add(Entity(doc, r.localId), "sage.Script", r.script, a);
            for (const auto& r : data.animations)
            {
                json::Value v(rapidjson::kObjectType);
                json::Put(v, "modelKey", r.modelKey, a);
                Component(Entity(doc, r.localId), "sage.Animation", v, a);
            }
            for (const auto& r : data.moveables)
            {
                json::Value v(rapidjson::kObjectType);
                json::Put(v, "movementSpeed", json::Encode(r.movementSpeed), a);
                json::Put(v, "turnSpeed", json::Encode(r.turnSpeed), a);
                json::Put(v, "pathfindingBounds", json::Encode(r.pathfindingBounds), a);
                json::Put(v, "moveClip", r.moveClip, a);
                json::Put(v, "idleClip", r.idleClip, a);
                Component(Entity(doc, r.localId), "sage.MoveableActor", v, a);
            }
            for (const auto& r : data.archetypes)
                Add(Entity(doc, r.localId), "sage.Archetype", r.archetype, a);
            for (const auto& r : data.customShaders)
                Add(Entity(doc, r.localId), "sage.CustomShader", r.shader, a);
            for (const auto& r : data.customComponents)
                Opaque(Entity(doc, r.localId), r.key, r.data, a);
            NormalizeLegacyIds(doc);
            return doc;
        }
        std::uint32_t EnsureId(entt::registry& registry, entt::entity entity, std::uint32_t& next)
        {
            auto* existing = registry.try_get<PersistentEntityId>(entity);
            if (existing) return static_cast<std::uint32_t>(existing->id);
            if (next >= NULL_ID) throw std::runtime_error("Document entity ID space exhausted");
            registry.emplace<PersistentEntityId>(entity, next);
            return next++;
        }
    } // namespace
    entt::entity FindEntityById(const entt::registry& registry, std::uint64_t id)
    {
        for (const auto entity : registry.view<PersistentEntityId>())
            if (registry.get<PersistentEntityId>(entity).id == id) return entity;
        return entt::null;
    }

    void EnsureEngineComponentsRegistered()
    {
        static const bool initialized = [] {
            RegisterFlatpackComponent<Collideable>("sage.Collideable");
            RegisterFlatpackComponent<NavigationSurface>("sage.NavigationSurface");
            RegisterFlatpackComponent<NavigationObstacle>("sage.NavigationObstacle");
            RegisterFlatpackComponent<TriggerVolume>("sage.TriggerVolume");
            RegisterFlatpackComponent<CursorTarget>("sage.CursorTarget");
            RegisterFlatpackComponent<Light>("sage.Light");
            RegisterFlatpackComponent<MetaData>("sage.MetaData");
            RegisterFlatpackComponent<ScriptComponent>("sage.Script");
            RegisterFlatpackComponent<Archetype>("sage.Archetype");
            RegisterFlatpackComponent<CustomShaderComponent>("sage.CustomShader");
            RegisterFlatpackComponent<ParticleEmitterComponent>("sage.ParticleEmitter");
            RegisterFlatpackComponent<Terrain>("sage.Terrain");
            RegisterFlatpackComponent<MoveableActor>("sage.MoveableActor");
            return true;
        }();
        (void)initialized;
    }
    bool IsDocument(const std::filesystem::path& path, const std::string& kind)
    {
        try
        {
            const auto doc = json::Parse(json::Read(path));
            return doc.IsObject() && doc.HasMember("format") && json::String(doc, "format") == "sage-content" &&
                   (kind.empty() || json::String(doc, "kind") == kind);
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    json::Document ReadDocument(const std::filesystem::path& path)
    {
        const auto bytes = json::Read(path);
        json::Document doc;
        const auto first = bytes.find_first_not_of(" \n\r\t");
        if (first != std::string::npos && bytes[first] == '{')
            doc = json::Parse(bytes);
        else if (bytes.starts_with("LQE6"))
            doc = ReadMap(path);
        else if (bytes.starts_with("LQF"))
            doc = ReadFlatpack(path, bytes.substr(0, 4));
        else
            throw std::runtime_error("Unsupported content format: " + path.string());
        const auto errors = Validate(doc);
        if (!errors.empty()) throw std::runtime_error(errors.front());
        return doc;
    }
    std::vector<std::string> Validate(const json::Value& doc)
    {
        std::vector<std::string> errors;
        try
        {
            if (json::String(doc, "format") != "sage-content" || json::Id(doc, "version") != DOCUMENT_VERSION)
                throw std::runtime_error("Unsupported document version");
            const auto kind = json::String(doc, "kind");
            if (kind != "map" && kind != "flatpack") throw std::runtime_error("Expected map or flatpack document");
            const auto& nodes = json::Require(doc, "entities");
            if (!nodes.IsArray()) throw std::runtime_error("entities must be an array");
            std::set<std::uint32_t> ids;
            for (const auto& n : nodes.GetArray())
            {
                auto id = json::Id(n, "id");
                if (id == 0 || id >= NULL_ID || !ids.insert(id).second)
                    errors.push_back("Duplicate or out-of-range entity ID: " + std::to_string(id));
            }
            std::unordered_map<std::uint32_t, entt::entity> references;
            for (auto id : ids)
                references.emplace(id, static_cast<entt::entity>(id));
            for (const auto& n : nodes.GetArray())
            {
                const auto id = json::Id(n, "id");
                json::String(n, "name");
                if (n.HasMember("order") && !n["order"].IsUint64())
                    throw std::runtime_error("order must be an unsigned integer");
                const auto& parent = json::Require(n, "parent");
                if (!parent.IsNull() &&
                    (!parent.IsUint() || !ids.contains(parent.GetUint()) || parent.GetUint() == id))
                    errors.push_back("Invalid parent for entity " + std::to_string(id));
                const auto& transform = json::Require(n, "transform");
                Vector3 value{};
                for (const char* field : {"position", "rotation", "scale"})
                    json::Decode(json::Require(transform, field), value);
                const auto& components = json::Require(n, "components");
                if (!components.IsObject()) throw std::runtime_error("components must be an object");
                for (const auto& m : components.GetObject())
                {
                    const auto version = json::Id(m.value, "version");
                    json::Require(m.value, "data");
                    if (m.value.HasMember("encoding"))
                    {
                        if (json::String(m.value, "encoding") != "binary-hex")
                            throw std::runtime_error("Unknown payload encoding");
                        Unhex(json::String(m.value, "data"));
                        continue;
                    }
                    if (version != 1) continue;
                    const auto* operations = FindComponentOperations(m.name.GetString());
                    if (operations)
                    {
                        for (auto type : operations->requirements)
                        {
                            const auto key = ComponentKey(type);
                            if (key != "sage.Transform" && !components.HasMember(key.c_str()))
                                errors.push_back(std::string(m.name.GetString()) + " requires " + key);
                        }
                        for (auto type : operations->incompatible)
                        {
                            const auto key = ComponentKey(type);
                            if (components.HasMember(key.c_str()))
                                errors.push_back(std::string(m.name.GetString()) + " is incompatible with " + key);
                        }
                        if (!operations->validateJson(json::Stringify(m.value["data"]), references))
                            errors.push_back("Invalid entity reference in " + std::string(m.name.GetString()));
                    }
                    if (std::string(m.name.GetString()) == "sage.Animation")
                    {
                        json::String(m.value["data"], "modelKey");
                        if (!components.HasMember("sage.Renderable"))
                            errors.push_back("sage.Animation requires sage.Renderable");
                    }
                    if (std::string(m.name.GetString()) == "sage.Renderable")
                    {
                        content_binary::StoredRenderableRecord r;
                        json::Decode(m.value["data"], r);
                        if (r.kind > 2) errors.push_back("Invalid renderable kind");
                    }
                }
                const auto supported = [&](const char* key) {
                    return components.HasMember(key) && json::Id(components[key], "version") == 1 &&
                           !components[key].HasMember("encoding");
                };
                if (supported("sage.NavigationSurface") && supported("sage.NavigationObstacle"))
                    errors.push_back("Entity cannot have both navigation surface and obstacle");
                for (const char* aspect :
                     {"sage.NavigationSurface",
                      "sage.NavigationObstacle",
                      "sage.TriggerVolume",
                      "sage.CursorTarget"})
                    if (supported(aspect) && !components.HasMember("sage.Collideable"))
                        errors.push_back(std::string(aspect) + " requires sage.Collideable");
                if (supported("sage.Terrain"))
                {
                    Terrain t;
                    json::Decode(components["sage.Terrain"]["data"], t);
                    if (!t.IsValid()) errors.push_back("Invalid terrain on entity " + std::to_string(id));
                }
                std::set<std::uint32_t> visited{id};
                auto* current = &n;
                while (current->HasMember("parent") && (*current)["parent"].IsUint())
                {
                    const auto p = (*current)["parent"].GetUint();
                    if (!visited.insert(p).second)
                    {
                        errors.push_back("Hierarchy cycle");
                        break;
                    }
                    current = nullptr;
                    for (const auto& candidate : nodes.GetArray())
                        if (json::Id(candidate, "id") == p)
                        {
                            current = &candidate;
                            break;
                        }
                    if (!current) break;
                }
            }
            if (kind == "flatpack" && !ids.contains(json::Id(doc, "root")))
                errors.push_back("Flatpack root does not exist");
        }
        catch (const std::exception& error)
        {
            errors.push_back(error.what());
        }
        return errors;
    }
    void WriteDocument(const std::filesystem::path& path, const json::Value& document)
    {
        const auto errors = Validate(document);
        if (!errors.empty()) throw std::runtime_error(errors.front());
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".writing-" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try
        {
            {
                std::ofstream output(temporary, std::ios::binary);
                output << json::Stringify(document);
                output.close();
                if (!output) throw std::runtime_error("Could not write " + path.string());
            }
            const auto verified = ReadDocument(temporary);
            if (json::Stringify(verified) != json::Stringify(document))
                throw std::runtime_error("Document verification failed");
            std::filesystem::rename(temporary, path);
        }
        catch (...)
        {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
    std::vector<std::string> Dependencies(const json::Value& document)
    {
        std::set<std::string> result;
        const auto visit = [&](auto& self, const json::Value& value) -> void {
            if (value.IsArray())
                for (const auto& v : value.GetArray())
                    self(self, v);
            if (!value.IsObject()) return;
            for (const auto& m : value.GetObject())
            {
                const std::string key = m.name.GetString();
                if (m.value.IsString() && (key == "key" || key == "modelKey" || key == "texture" ||
                                           key == "texture0Key" || key == "texture1Key" || key == "className" ||
                                           key == "vertexShaderPath" || key == "fragmentShaderPath"))
                {
                    const std::string dependency = m.value.GetString();
                    if (!dependency.empty()) result.insert(dependency);
                }
                self(self, m.value);
            }
        };
        visit(visit, document);
        return {result.begin(), result.end()};
    }
    json::Document Capture(
        entt::registry& registry,
        const std::vector<entt::entity>& entities,
        const std::string& kind,
        entt::entity root,
        bool keepExternalReferences)
    {
        EnsureEngineComponentsRegistered();
        auto doc = Empty(kind);
        auto& a = doc.GetAllocator();
        std::uint32_t next = 1;
        for (auto e : registry.view<PersistentEntityId>())
            next = std::max(next, static_cast<std::uint32_t>(registry.get<PersistentEntityId>(e).id + 1));
        std::unordered_map<std::uint32_t, entt::entity> refs;
        std::set<entt::entity> included(entities.begin(), entities.end());
        for (auto e : entities)
            if (registry.valid(e))
                refs.emplace(
                    entt::entt_traits<entt::entity>::to_entity(e),
                    static_cast<entt::entity>(EnsureId(registry, e, next)));
        if (keepExternalReferences)
            for (auto entity : registry.view<sgTransform>())
            {
                refs.emplace(
                    entt::entt_traits<entt::entity>::to_entity(entity),
                    static_cast<entt::entity>(EnsureId(registry, entity, next)));
                included.insert(entity);
            }
        const Vector3 origin = root != entt::null ? registry.get<sgTransform>(root).GetWorldPos() : Vector3{};
        std::uint64_t order = 0;
        for (auto e : entities)
        {
            if (!registry.valid(e)) continue;
            const auto* t = registry.try_get<sgTransform>(e);
            const auto* light = registry.try_get<Light>(e);
            if (!t && !light) continue;
            const auto position = t ? t->GetWorldPos() : light->position;
            const auto parent =
                t && included.contains(t->GetParent())
                    ? static_cast<std::uint32_t>(registry.get<PersistentEntityId>(t->GetParent()).id)
                    : NULL_ID;
            auto n = Node(
                static_cast<std::uint32_t>(registry.get<PersistentEntityId>(e).id),
                t ? t->name : "Light",
                {position.x - origin.x, position.y - origin.y, position.z - origin.z},
                t ? t->GetWorldRot() : Vector3{},
                t ? t->GetScale() : Vector3{1, 1, 1},
                parent,
                a);
            json::Put(n, "order", order++, a);
            for (const auto& operations : detail::RegisteredComponentOperations())
                if (operations.has(registry, e))
                    Component(n, operations.key, json::Parse(operations.captureJson(registry, e, refs)), a);
            if (light && t)
            {
                auto sourceLight = *light;
                sourceLight.position = {position.x - origin.x, position.y - origin.y, position.z - origin.z};
                Add(n, "sage.Light", sourceLight, a);
            }
            if (const auto* r = registry.try_get<Renderable>(e); r && r->serializable)
            {
                std::ostringstream stream(std::ios::binary);
                {
                    cereal::BinaryOutputArchive output(stream);
                    output(*r);
                }
                content_binary::StoredRenderableRecord stored;
                std::istringstream input(stream.str(), std::ios::binary);
                cereal::BinaryInputArchive archive(input);
                archive(stored);
                stored.active = r->active;
                stored.hint = r->hint;
                Add(n, "sage.Renderable", stored, a);
            }
            if (const auto* animation = registry.try_get<Animation>(e))
            {
                json::Value v(rapidjson::kObjectType);
                json::Put(v, "modelKey", animation->modelKey, a);
                Component(n, "sage.Animation", v, a);
            }
            if (const auto* unknown = registry.try_get<UnknownContentComponents>(e))
                for (const auto& [key, value] : unknown->values)
                    if (!n["components"].HasMember(key.c_str()))
                        json::Put(n["components"], key.c_str(), json::Parse(value), a);
            doc["entities"].PushBack(n, a);
        }
        if (kind == "flatpack")
            json::Put(doc, "root", std::uint64_t(registry.get<PersistentEntityId>(root).id), a);
        // Stable IDs keep diffs canonical; order preserves saved sibling ordering.
        std::sort(doc["entities"].Begin(), doc["entities"].End(), [](const auto& l, const auto& r) {
            return json::Id(l, "id") < json::Id(r, "id");
        });
        return doc;
    }
    static void RestoreComponents(entt::registry& registry, entt::entity e, const json::Value& n)
    {
        const auto& desired = n["components"];
        auto supported = [&](const char* key) {
            return desired.HasMember(key) && json::Id(desired[key], "version") == 1;
        };
        for (const auto& operations : detail::RegisteredComponentOperations())
            if (operations.has(registry, e) && !supported(operations.key.c_str())) operations.remove(registry, e);
        if (!supported("sage.Renderable"))
        {
            registry.remove<Renderable>(e);
            registry.remove<UberShaderComponent>(e);
        }
        if (!supported("sage.Animation")) registry.remove<Animation>(e);
        if (!supported("sage.MoveableActor")) registry.remove<MoveableActor>(e);
        if (!supported("sage.Terrain")) registry.remove<DynamicRenderable>(e);
        registry.remove<UnknownContentComponents>(e);
        for (const auto& m : n["components"].GetObject())
        {
            const std::string key = m.name.GetString();
            const auto& wrapper = m.value;
            if (json::Id(wrapper, "version") != 1)
            {
                registry.get_or_emplace<UnknownContentComponents>(e).values[key] = json::Stringify(wrapper);
                continue;
            }
            const auto& data = wrapper["data"];
            if (key == "sage.Renderable")
            {
                content_binary::StoredRenderableRecord stored;
                json::Decode(data, stored);
                std::ostringstream output(std::ios::binary);
                {
                    cereal::BinaryOutputArchive archive(output);
                    archive(stored);
                }
                std::istringstream input(output.str(), std::ios::binary);
                Renderable r;
                cereal::BinaryInputArchive archive(input);
                archive(r);
                r.active = stored.active;
                r.hint = stored.hint;
                registry.emplace_or_replace<Renderable>(e, std::move(r));
            }
            else if (key == "sage.Animation")
            {
                const auto model = json::String(data, "modelKey");
                if (!ResourceManager::GetInstance().HasModelAnimation(model))
                    throw std::runtime_error("Missing animation: " + model);
                registry.remove<Animation>(e);
                registry.emplace<Animation>(e, model);
            }
            else if (const auto* operations = FindComponentOperations(key))
            {
                if (wrapper.HasMember("encoding"))
                    operations->deserialize(registry, e, Unhex(json::String(wrapper, "data")));
                else
                    operations->restoreJson(registry, e, json::Stringify(data));
            }
            else
                registry.get_or_emplace<UnknownContentComponents>(e).values[key] = json::Stringify(wrapper);
        }
        if (auto* light = registry.try_get<Light>(e)) light->position = registry.get<sgTransform>(e).GetWorldPos();
    }
    void RestoreEntity(
        entt::registry& registry,
        entt::entity entity,
        const json::Value& node,
        const std::unordered_map<std::uint32_t, entt::entity>& references)
    {
        EnsureEngineComponentsRegistered();
        auto& t = registry.get_or_emplace<sgTransform>(entity);
        t.name = json::String(node, "name");
        Vector3 position, rotation, scale;
        json::Decode(node["transform"]["position"], position);
        json::Decode(node["transform"]["rotation"], rotation);
        json::Decode(node["transform"]["scale"], scale);
        t.position.world = position;
        t.rotation.world = rotation;
        t.scale.world = scale;
        RestoreComponents(registry, entity, node);
        ResolveRegisteredComponentReferences(registry, entity, references);
        if (auto* collider = registry.try_get<Collideable>(entity))
            collider->worldBoundingBox = TransformBoundingBoxByCorners(collider->localBoundingBox, t.GetMatrix());
    }

    ContentLoadResult Instantiate(entt::registry& registry, const json::Value& doc, Vector3 anchor, bool freshIds)
    {
        const auto errors = Validate(doc);
        if (!errors.empty()) throw std::runtime_error(errors.front());
        ContentLoadResult result;
        std::unordered_map<std::uint32_t, entt::entity> ids;
        std::uint32_t next = 1;
        for (auto e : registry.view<PersistentEntityId>())
            next = std::max(next, static_cast<std::uint32_t>(registry.get<PersistentEntityId>(e).id + 1));
        try
        {
            for (const auto& n : doc["entities"].GetArray())
            {
                const auto e = registry.create();
                result.entities.push_back(e);
                const auto id = json::Id(n, "id");
                ids.emplace(id, e);
                registry.emplace<PersistentEntityId>(e, freshIds ? next++ : id);
                auto& t = registry.emplace<sgTransform>(e);
                t.name = json::String(n, "name");
                Vector3 p, r, s;
                json::Decode(n["transform"]["position"], p);
                json::Decode(n["transform"]["rotation"], r);
                json::Decode(n["transform"]["scale"], s);
                t.position.world = Vector3{p.x + anchor.x, p.y + anchor.y, p.z + anchor.z};
                t.rotation.world = r;
                t.scale.world = s;
            }
            std::vector<const json::Value*> ordered;
            for (const auto& n : doc["entities"].GetArray())
                ordered.push_back(&n);
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
                const auto order = [](const auto& node) -> std::uint64_t {
                    return node.HasMember("order") ? node["order"].GetUint64() : 0;
                };
                return order(*left) < order(*right);
            });
            for (const auto* node : ordered)
            {
                const auto& n = *node;
                auto e = ids.at(json::Id(n, "id"));
                if (!n["parent"].IsNull()) registry.get<sgTransform>(e).SetParent(ids.at(n["parent"].GetUint()));
                RestoreComponents(registry, e, n);
            }
            for (auto e : result.entities)
            {
                ResolveRegisteredComponentReferences(registry, e, ids);
                if (auto* collider = registry.try_get<Collideable>(e))
                    collider->worldBoundingBox = TransformBoundingBoxByCorners(
                        collider->localBoundingBox, registry.get<sgTransform>(e).GetMatrix());
            }
            if (doc.HasMember("root"))
                result.root = ids.at(json::Id(doc, "root"));
            else if (!result.entities.empty())
                result.root = result.entities.front();
        }
        catch (...)
        {
            for (auto e : result.entities)
                if (registry.valid(e)) registry.destroy(e);
            throw;
        }
        return result;
    }
    json::Document DescribeComponents(const json::Value* node)
    {
        EnsureEngineComponentsRegistered();
        json::Document result(rapidjson::kObjectType);
        auto& a = result.GetAllocator();
        for (const auto& operations : detail::RegisteredComponentOperations())
        {
            json::Value description(rapidjson::kObjectType), required(rapidjson::kArrayType),
                incompatible(rapidjson::kArrayType);
            json::Put(description, "version", std::uint64_t(1), a);
            std::string data;
            if (node && (*node)["components"].HasMember(operations.key.c_str()))
            {
                const auto& wrapper = (*node)["components"][operations.key.c_str()];
                if (json::Id(wrapper, "version") == 1 && !wrapper.HasMember("encoding"))
                    data = json::Stringify(wrapper["data"]);
            }
            json::Put(description, "fields", json::Parse(operations.schema(data)), a);
            for (auto type : operations.requirements)
                required.PushBack(json::Value(ComponentKey(type).c_str(), a), a);
            for (auto type : operations.incompatible)
                incompatible.PushBack(json::Value(ComponentKey(type).c_str(), a), a);
            json::Put(description, "requires", required, a);
            json::Put(description, "incompatible", incompatible, a);
            json::Put(result, operations.key.c_str(), description, a);
        }
        return result;
    }
    static void ApplyUnchecked(json::Value& doc, const json::Value& operation, json::Allocator& a)
    {
        const auto op = json::String(operation, "op");
        if (op == "create" || op == "instantiate")
        {
            std::uint32_t next = 1;
            for (const auto& n : doc["entities"].GetArray())
                next = std::max(next, json::Id(n, "id") + 1);
            if (op == "create")
            {
                Vector3 position{};
                if (operation.HasMember("position")) json::Decode(operation["position"], position);
                auto node = Node(
                    next,
                    operation.HasMember("name") ? json::String(operation, "name") : "Entity",
                    position,
                    {},
                    {1, 1, 1},
                    operation.HasMember("parent") && !operation["parent"].IsNull() ? json::Id(operation, "parent")
                                                                                   : NULL_ID,
                    a);
                doc["entities"].PushBack(node, a);
            }
            else
            {
                auto prefab = ReadDocument(json::String(operation, "path"));
                if (json::String(prefab, "kind") != "flatpack") throw std::runtime_error("Expected flatpack");
                std::unordered_map<std::uint32_t, entt::entity> ids;
                for (const auto& n : prefab["entities"].GetArray())
                    ids.emplace(json::Id(n, "id"), static_cast<entt::entity>(next++));
                Vector3 anchor{};
                if (operation.HasMember("position")) json::Decode(operation["position"], anchor);
                RemapReferences(prefab, ids, prefab.GetAllocator());
                for (auto& n : prefab["entities"].GetArray())
                {
                    n["id"].SetUint(entt::to_integral(ids.at(json::Id(n, "id"))));
                    if (n["parent"].IsUint())
                        n["parent"].SetUint(entt::to_integral(ids.at(n["parent"].GetUint())));
                    else if (operation.HasMember("parent"))
                        json::Put(n, "parent", operation["parent"], prefab.GetAllocator());
                    Vector3 position;
                    json::Decode(n["transform"]["position"], position);
                    position.x += anchor.x;
                    position.y += anchor.y;
                    position.z += anchor.z;
                    json::Put(n["transform"], "position", json::Encode(position), prefab.GetAllocator());
                    json::Value copy;
                    copy.CopyFrom(n, a);
                    doc["entities"].PushBack(copy, a);
                }
            }
            const auto errors = Validate(doc);
            if (!errors.empty()) throw std::runtime_error(errors.front());
            return;
        }
        auto& node = Entity(doc, json::Id(operation, "entity"));
        if (op == "model")
        {
            if (!node["components"].HasMember("sage.Renderable"))
                throw std::runtime_error("Entity has no renderable");
            json::Put(node["components"]["sage.Renderable"]["data"], "key", json::String(operation, "value"), a);
        }
        else if (op == "rename")
            json::Put(node, "name", json::String(operation, "value"), a);
        else if (op == "reparent")
            json::Put(node, "parent", json::Require(operation, "parent"), a);
        else if (op == "transform")
        {
            const auto field = json::String(operation, "field");
            if (field != "position" && field != "rotation" && field != "scale")
                throw std::runtime_error("Unknown transform field");
            Vector3 v;
            json::Decode(json::Require(operation, "value"), v);
            json::Put(node["transform"], field.c_str(), json::Encode(v), a);
            if (field == "position" && node["components"].HasMember("sage.Light"))
                json::Put(node["components"]["sage.Light"]["data"], "position", json::Encode(v), a);
        }
        else if (op == "set")
        {
            const auto component = json::String(operation, "component");
            if (!node["components"].HasMember(component.c_str()))
                throw std::runtime_error("Component does not exist");
            auto& wrapper = node["components"][component.c_str()];
            const auto* operations = FindComponentOperations(component);
            if (!operations || json::Id(wrapper, "version") != 1 || wrapper.HasMember("encoding"))
                throw std::runtime_error("Component cannot be edited by this executable");
            const auto value = operations->editJson(
                json::Stringify(wrapper["data"]),
                json::String(operation, "field"),
                json::Stringify(json::Require(operation, "value")));
            json::Put(wrapper, "data", json::Parse(value), a);
            if (component == "sage.Light" && json::String(operation, "field") == "position")
                json::Put(node["transform"], "position", wrapper["data"]["position"], a);
        }
        else if (op == "add")
        {
            const auto key = json::String(operation, "component");
            if (node["components"].HasMember(key.c_str())) throw std::runtime_error("Component already exists");
            if (!FindComponentOperations(key)) throw std::runtime_error("Unknown component");
            Component(node, key, json::Require(operation, "value"), a);
        }
        else if (op == "remove")
        {
            const auto key = json::String(operation, "component");
            if (!node["components"].HasMember(key.c_str())) throw std::runtime_error("Component does not exist");
            node["components"].RemoveMember(key.c_str());
        }
        else if (op == "delete")
        {
            const auto id = json::Id(node, "id");
            std::set<std::uint32_t> deleted{id};
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto& n : doc["entities"].GetArray())
                    if (n["parent"].IsUint() && deleted.contains(n["parent"].GetUint()))
                        changed = deleted.insert(json::Id(n, "id")).second || changed;
            }
            auto& nodes = doc["entities"];
            for (auto i = nodes.Begin(); i != nodes.End();)
                if (deleted.contains(json::Id(*i, "id")))
                    i = nodes.Erase(i);
                else
                    ++i;
        }
        else
            throw std::runtime_error("Unknown content operation: " + op);
        const auto errors = Validate(doc);
        if (!errors.empty()) throw std::runtime_error(errors.front());
    }
    void Apply(json::Value& document, const json::Value& operation, json::Allocator& allocator)
    {
        json::Document candidate;
        candidate.CopyFrom(document, candidate.GetAllocator());
        ApplyUnchecked(candidate, operation, candidate.GetAllocator());
        document.CopyFrom(candidate, allocator);
    }

} // namespace sage::content

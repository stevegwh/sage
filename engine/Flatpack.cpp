#include "Flatpack.hpp"

#include "Archetypes.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/CustomShaderComponent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/Light.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Serializer.hpp"

#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "raymath.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace sage
{
    namespace
    {
        constexpr char kFlatpackMagic[4] = {'L', 'Q', 'F', '5'};
        constexpr char kVersionFourFlatpackMagic[4] = {'L', 'Q', 'F', '4'};
        constexpr char kVersionThreeFlatpackMagic[4] = {'L', 'Q', 'F', '3'};
        constexpr char kVersionTwoFlatpackMagic[4] = {'L', 'Q', 'F', '2'};
        constexpr char kLegacyFlatpackMagic[4] = {'L', 'Q', 'F', 'P'};

        std::vector<detail::FlatpackComponentCodec>& FlatpackComponentCodecs()
        {
            static std::vector<detail::FlatpackComponentCodec> codecs;
            return codecs;
        }

        // A serialized entity from the source registry. parentLocalId points into
        // this same vector (-1 for the root). Transform values are captured as
        // plain Vector3 (rather than sgTransform itself) so saving doesn't have
        // to touch the registry-bound proxies on the source transforms.
        // Component blobs are flagged so we can leave optional components empty
        // when the source entity doesn't carry them.
        struct FlatpackEntityRecord
        {
            std::int32_t parentLocalId = -1;
            Vector3 worldPos{};
            Vector3 worldRot{};
            Vector3 worldScale{1.0f, 1.0f, 1.0f};
            bool hasCollideable = false;
            Collideable collideable{};
            bool hasNavigationSurface = false;
            NavigationSurface navigationSurface{};
            bool hasNavigationObstacle = false;
            NavigationObstacle navigationObstacle{};
            bool hasTriggerVolume = false;
            TriggerVolume triggerVolume{};
            bool hasCursorTarget = false;
            CursorTarget cursorTarget{};
            bool hasRenderable = false;
            Renderable renderable{};
            bool hasLight = false;
            Light light{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(parentLocalId, worldPos, worldRot, worldScale);
                archive(hasCollideable);
                if (hasCollideable) archive(collideable);
                archive(hasNavigationSurface);
                if (hasNavigationSurface) archive(navigationSurface);
                archive(hasNavigationObstacle);
                if (hasNavigationObstacle) archive(navigationObstacle);
                archive(hasTriggerVolume);
                if (hasTriggerVolume) archive(triggerVolume);
                archive(hasCursorTarget);
                if (hasCursorTarget) archive(cursorTarget);
                archive(hasRenderable);
                if (hasRenderable) archive(renderable);
                archive(hasLight);
                if (hasLight) archive(light);
            }
        };

        // Component sections reference entities by localId, which indexes the
        // records vector.
        struct FlatpackScriptRecord
        {
            std::uint32_t localId = 0;
            ScriptComponent script{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, script);
            }
        };

        // Only the model key is saved; clips are derived from packed animation
        // data on load.
        struct FlatpackAnimationRecord
        {
            std::uint32_t localId = 0;
            std::string modelKey;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, modelKey);
            }
        };

        // Only the authored fields are saved; the rest of the component is
        // runtime state.
        struct FlatpackMoveableActorRecord
        {
            std::uint32_t localId = 0;
            float movementSpeed = 0.0f;
            float turnSpeed = 240.0f;
            std::int32_t pathfindingBounds = 0;
            std::string moveClip;
            std::string idleClip;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, movementSpeed, turnSpeed, pathfindingBounds, moveClip, idleClip);
            }
        };

        struct FlatpackArchetypeRecord
        {
            std::uint32_t localId = 0;
            Archetype archetype{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, archetype);
            }
        };

        struct FlatpackCustomShaderRecord
        {
            std::uint32_t localId = 0;
            CustomShaderComponent shader{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, shader);
            }
        };

        struct FlatpackCustomComponentRecord
        {
            std::uint32_t localId = 0;
            std::string key;
            std::string data;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, key, data);
            }
        };

        struct LegacyFlatpackMoveableActorRecord
        {
            std::uint32_t localId = 0;
            float movementSpeed = 0.0f;
            std::int32_t pathfindingBounds = 0;
            std::string moveClip;
            std::string idleClip;

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(localId, movementSpeed, pathfindingBounds, moveClip, idleClip);
            }
        };

    } // namespace

    void detail::RegisterFlatpackComponentCodec(FlatpackComponentCodec codec)
    {
        auto& codecs = FlatpackComponentCodecs();
        const auto existing = std::ranges::find(codecs, codec.key, &FlatpackComponentCodec::key);
        if (existing != codecs.end())
        {
            *existing = std::move(codec);
            return;
        }
        codecs.push_back(std::move(codec));
    }

    bool RestoreRegisteredComponent(entt::registry& registry, entt::entity entity,
        const std::string& key, const std::string& data)
    {
        const auto& codecs = FlatpackComponentCodecs();
        const auto codec = std::ranges::find(codecs, key, &detail::FlatpackComponentCodec::key);
        if (codec == codecs.end()) return false;
        codec->deserialize(registry, entity, data);
        return true;
    }

    void ResolveRegisteredComponentReferences(entt::registry& registry, entt::entity entity,
        const std::unordered_map<std::uint32_t, entt::entity>& ids)
    {
        for (const auto& codec : FlatpackComponentCodecs()) codec.resolveReferences(registry, entity, ids);
    }

    bool IsFlatpackFile(const char* path)
    {
        std::ifstream storage(path, std::ios::binary);
        if (!storage.is_open()) return false;

        char fileMagic[4]{};
        storage.read(fileMagic, sizeof(fileMagic));
        return storage.gcount() == sizeof(fileMagic) &&
               (std::memcmp(fileMagic, kFlatpackMagic, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, kVersionFourFlatpackMagic, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, kVersionThreeFlatpackMagic, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, kVersionTwoFlatpackMagic, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, kLegacyFlatpackMagic, sizeof(fileMagic)) == 0);
    }

    bool SaveFlatpack(entt::registry& source, entt::entity root, const char* path)
    {
        if (!source.valid(root) || !source.any_of<sgTransform>(root))
        {
            std::cerr << "ERROR: SaveFlatpack: root is not a valid sgTransform entity." << std::endl;
            return false;
        }

        // Depth-first walk of the source subtree. Each entry maps a source entity
        // to its local id (index in the records vector).
        std::vector<entt::entity> subtreeOrder;
        std::unordered_map<entt::entity, std::int32_t> localIds;
        auto visit = [&](auto& self, entt::entity entity) -> void {
            if (!source.valid(entity) || !source.any_of<sgTransform>(entity)) return;
            localIds.emplace(entity, static_cast<std::int32_t>(subtreeOrder.size()));
            subtreeOrder.push_back(entity);
            for (const auto child : source.get<sgTransform>(entity).GetChildren())
            {
                self(self, child);
            }
        };
        visit(visit, root);
        std::unordered_map<std::uint32_t, entt::entity> referenceIds;
        for (const auto& [entity, localId] : localIds)
            referenceIds.emplace(entt::entt_traits<entt::entity>::to_entity(entity), static_cast<entt::entity>(localId));

        const Vector3 rootWorldOrigin = source.get<sgTransform>(root).GetWorldPos();

        std::vector<FlatpackEntityRecord> records;
        std::vector<std::string> names;
        std::vector<FlatpackScriptRecord> scripts;
        std::vector<FlatpackAnimationRecord> animations;
        std::vector<FlatpackMoveableActorRecord> moveables;
        std::vector<FlatpackArchetypeRecord> archetypes;
        std::vector<FlatpackCustomShaderRecord> customShaders;
        std::vector<FlatpackCustomComponentRecord> customComponents;
        records.reserve(subtreeOrder.size());
        names.reserve(subtreeOrder.size());
        for (const auto entity : subtreeOrder)
        {
            const auto localId = static_cast<std::uint32_t>(records.size());
            auto& record = records.emplace_back();
            const auto& transform = source.get<sgTransform>(entity);
            names.push_back(transform.name);

            if (const auto* archetype = source.try_get<Archetype>(entity))
            {
                archetypes.push_back(FlatpackArchetypeRecord{localId, *archetype});
            }
            if (const auto* shader = source.try_get<CustomShaderComponent>(entity))
            {
                customShaders.push_back(FlatpackCustomShaderRecord{localId, *shader});
            }
            for (const auto& codec : FlatpackComponentCodecs())
            {
                if (!codec.has(source, entity)) continue;
                customComponents.push_back(
                    FlatpackCustomComponentRecord{localId, codec.key, codec.serialize(source, entity, referenceIds)});
            }

            if (const auto* script = source.try_get<ScriptComponent>(entity);
                script != nullptr && !script->className.empty())
            {
                scripts.push_back(FlatpackScriptRecord{localId, *script});
            }
            if (const auto* animation = source.try_get<Animation>(entity);
                animation != nullptr && !animation->modelKey.empty())
            {
                animations.push_back(FlatpackAnimationRecord{localId, animation->modelKey});
            }
            if (const auto* moveable = source.try_get<MoveableActor>(entity))
            {
                moveables.push_back(
                    FlatpackMoveableActorRecord{
                        localId,
                        moveable->movementSpeed,
                        moveable->turnSpeed,
                        moveable->pathfindingBounds,
                        moveable->moveClip,
                        moveable->idleClip});
            }
            const auto parentIter = localIds.find(transform.GetParent());
            record.parentLocalId = (parentIter != localIds.end()) ? parentIter->second : -1;

            // Rebase world position relative to the root so the saved root sits
            // at the origin and descendants keep their world-space offsets.
            record.worldPos = Vector3Subtract(transform.GetWorldPos(), rootWorldOrigin);
            record.worldRot = transform.GetWorldRot();
            record.worldScale = transform.GetScale();

            if (source.any_of<Collideable>(entity))
            {
                record.hasCollideable = true;
                record.collideable = source.get<Collideable>(entity);
            }
            if (source.any_of<NavigationSurface>(entity))
            {
                record.hasNavigationSurface = true;
                record.navigationSurface = source.get<NavigationSurface>(entity);
            }
            if (source.any_of<NavigationObstacle>(entity))
            {
                record.hasNavigationObstacle = true;
                record.navigationObstacle = source.get<NavigationObstacle>(entity);
            }
            if (source.any_of<TriggerVolume>(entity))
            {
                record.hasTriggerVolume = true;
                record.triggerVolume = source.get<TriggerVolume>(entity);
            }
            if (source.any_of<CursorTarget>(entity))
            {
                record.hasCursorTarget = true;
                record.cursorTarget = source.get<CursorTarget>(entity);
            }
            if (source.any_of<Renderable>(entity))
            {
                record.hasRenderable = true;
                record.renderable = source.get<Renderable>(entity);
            }
            if (source.any_of<Light>(entity))
            {
                record.hasLight = true;
                record.light = source.get<Light>(entity);
                // Light::position is a world-space cache; rebase the same way
                // so the saved root sits at the origin.
                record.light.position = Vector3Subtract(record.light.position, rootWorldOrigin);
            }
        }

        const std::filesystem::path outputPath{path};
        if (const auto parent = outputPath.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        sage::serializer::WriteCompressedBinary(path, kFlatpackMagic, [&](cereal::BinaryOutputArchive& output) {
            output(records);
            output(names);
            output(scripts);
            output(animations);
            output(moveables);
            output(archetypes);
            output(customShaders);
            output(customComponents);
        });

        return true;
    }

    FlatpackInstance LoadFlatpack(entt::registry& destination, const char* path, const Vector3 anchorWorldPos)
    {
        if (!IsFlatpackFile(path))
        {
            std::cerr << "ERROR: Not a flatpack file: " << path << std::endl;
            return {};
        }

        std::vector<FlatpackEntityRecord> records;
        std::vector<std::string> names;
        std::vector<FlatpackScriptRecord> scripts;
        std::vector<FlatpackAnimationRecord> animations;
        std::vector<FlatpackMoveableActorRecord> moveables;
        std::vector<FlatpackArchetypeRecord> archetypes;
        std::vector<FlatpackCustomShaderRecord> customShaders;
        std::vector<FlatpackCustomComponentRecord> customComponents;
        std::ifstream header(path, std::ios::binary);
        char fileMagic[4]{};
        header.read(fileMagic, sizeof(fileMagic));
        const bool legacyFormat = std::memcmp(fileMagic, kLegacyFlatpackMagic, sizeof(fileMagic)) == 0;
        const bool versionTwoFormat = std::memcmp(fileMagic, kVersionTwoFlatpackMagic, sizeof(fileMagic)) == 0;
        const bool versionThreeFormat = std::memcmp(fileMagic, kVersionThreeFlatpackMagic, sizeof(fileMagic)) == 0;
        const bool versionFourFormat = std::memcmp(fileMagic, kVersionFourFlatpackMagic, sizeof(fileMagic)) == 0;
        const auto readArchive = [&](const auto& magic) {
            sage::serializer::ReadCompressedBinary(
                path, magic, [&](cereal::BinaryInputArchive& input, std::istream&) {
                    input(records, names, scripts, animations);
                    if (legacyFormat)
                    {
                        std::vector<LegacyFlatpackMoveableActorRecord> legacyMoveables;
                        input(legacyMoveables);
                        moveables.reserve(legacyMoveables.size());
                        for (auto& record : legacyMoveables)
                        {
                            moveables.push_back(
                                FlatpackMoveableActorRecord{
                                    record.localId,
                                    record.movementSpeed,
                                    240.0f,
                                    record.pathfindingBounds,
                                    std::move(record.moveClip),
                                    std::move(record.idleClip)});
                        }
                    }
                    else
                    {
                        input(moveables);
                    }
                    if (!legacyFormat && !versionTwoFormat)
                    {
                        input(archetypes);
                    }
                    if (!legacyFormat && !versionTwoFormat && !versionThreeFormat)
                    {
                        input(customShaders);
                    }
                    if (!legacyFormat && !versionTwoFormat && !versionThreeFormat && !versionFourFormat)
                    {
                        input(customComponents);
                    }
                });
        };
        if (legacyFormat)
            readArchive(kLegacyFlatpackMagic);
        else if (versionTwoFormat)
            readArchive(kVersionTwoFlatpackMagic);
        else if (versionThreeFormat)
            readArchive(kVersionThreeFlatpackMagic);
        else if (versionFourFormat)
            readArchive(kVersionFourFlatpackMagic);
        else
            readArchive(kFlatpackMagic);
        if (records.empty()) return {};

        // Create entities up-front so parent local ids resolve to real entt::entity values.
        std::vector<entt::entity> created;
        created.reserve(records.size());
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            created.push_back(destination.create());
        }

        // Apply transforms top-down so each call to SetParent sees the parent
        // already positioned in world space. We saved records in DFS order with
        // the root at index 0, so iterating in order is top-down.
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            const auto& record = records[i];
            const auto entity = created[i];

            destination.emplace<sgTransform>(entity);
            // emplace fires on_construct which binds the transform to the system,
            // so the proxy assignments below route through TransformSystem.
            auto& transform = destination.get<sgTransform>(entity);
            if (i < names.size()) transform.name = names[i];
            transform.position.world = Vector3Add(record.worldPos, anchorWorldPos);
            transform.rotation.world = record.worldRot;
            transform.scale.world = record.worldScale;

            if (record.parentLocalId >= 0 && static_cast<std::size_t>(record.parentLocalId) < created.size())
            {
                transform.SetParent(created[static_cast<std::size_t>(record.parentLocalId)]);
            }
        }

        // Restore optional components after the hierarchy is in place. Renderable
        // emplaces re-load the model from its asset key via the ResourceManager
        // (see Renderable::load), so each instance gets its own model handle.
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            const auto& record = records[i];
            const auto entity = created[i];

            if (record.hasCollideable)
            {
                destination.emplace<Collideable>(entity, record.collideable);
            }
            if (record.hasNavigationSurface)
            {
                destination.emplace<NavigationSurface>(entity, record.navigationSurface);
            }
            if (record.hasNavigationObstacle)
            {
                destination.emplace<NavigationObstacle>(entity, record.navigationObstacle);
            }
            if (record.hasTriggerVolume)
            {
                destination.emplace<TriggerVolume>(entity, record.triggerVolume);
            }
            if (record.hasCursorTarget)
            {
                destination.emplace<CursorTarget>(entity, record.cursorTarget);
            }
            if (record.hasRenderable)
            {
                destination.emplace<Renderable>(entity, record.renderable);
            }
            if (record.hasLight)
            {
                auto light = record.light;
                // Rebase the cached world position from the flatpack-root frame
                // (which was 0) into the destination world frame.
                light.position = Vector3Add(light.position, anchorWorldPos);
                destination.emplace<Light>(entity, light);
            }
        }

        for (const auto& record : scripts)
        {
            if (record.localId >= created.size()) continue;
            destination.emplace_or_replace<ScriptComponent>(created[record.localId], record.script);
        }

        for (const auto& record : animations)
        {
            if (record.localId >= created.size()) continue;
            if (!ResourceManager::GetInstance().HasModelAnimation(record.modelKey))
            {
                std::cerr << "Flatpack: no packed animation data for '" << record.modelKey
                          << "', skipping Animation component.\n";
                continue;
            }
            // Animation is neither copyable nor movable (live Subscriptions hold
            // its address), so replace by remove + emplace.
            destination.remove<Animation>(created[record.localId]);
            destination.emplace<Animation>(created[record.localId], record.modelKey);
        }

        for (const auto& record : moveables)
        {
            if (record.localId >= created.size()) continue;
            auto& moveable = destination.get_or_emplace<MoveableActor>(created[record.localId]);
            moveable.movementSpeed = record.movementSpeed;
            moveable.turnSpeed = record.turnSpeed;
            moveable.pathfindingBounds = record.pathfindingBounds;
            moveable.moveClip = record.moveClip;
            moveable.idleClip = record.idleClip;
        }

        for (const auto& record : archetypes)
        {
            if (record.localId >= created.size()) continue;
            SetArchetype(destination, created[record.localId], record.archetype);
        }

        for (auto& record : customShaders)
        {
            if (record.localId >= created.size()) continue;
            destination.emplace_or_replace<CustomShaderComponent>(
                created[record.localId], std::move(record.shader));
        }

        for (const auto& record : customComponents)
        {
            if (record.localId >= created.size()) continue;
            const auto codec =
                std::ranges::find(FlatpackComponentCodecs(), record.key, &detail::FlatpackComponentCodec::key);
            if (codec == FlatpackComponentCodecs().end())
            {
                std::cerr << "Flatpack: no component codec registered for '" << record.key << "'.\n";
                continue;
            }
            try
            {
                codec->deserialize(destination, created[record.localId], record.data);
            }
            catch (const std::exception& error)
            {
                std::cerr << "Flatpack: could not deserialize component '" << record.key << "': " << error.what()
                          << "\n";
            }
        }

        std::unordered_map<std::uint32_t, entt::entity> referenceIds;
        for (std::uint32_t i = 0; i < created.size(); ++i) referenceIds.emplace(i, created[i]);
        for (const auto entity : created)
            ResolveRegisteredComponentReferences(destination, entity, referenceIds);

        return FlatpackInstance{created.front(), std::move(created)};
    }

    std::vector<FlatpackCatalogEntry> ListFlatpacks(const std::filesystem::path& directory)
    {
        std::vector<FlatpackCatalogEntry> entries;
        if (!std::filesystem::is_directory(directory)) return entries;

        for (const auto& dirEntry : std::filesystem::directory_iterator{directory})
        {
            if (!dirEntry.is_regular_file()) continue;
            const auto& path = dirEntry.path();
            if (path.extension() != ".flatpack" && path.extension() != ".bin") continue;
            if (!IsFlatpackFile(path.string().c_str())) continue;
            entries.push_back({.displayName = path.stem().string(), .path = path});
        }

        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.displayName < rhs.displayName;
        });
        return entries;
    }

    FlatpackInstance InstantiateFlatpack(
        entt::registry& destination,
        const char* path,
        const Vector3 position,
        const std::optional<Vector3> eulerRotation,
        const bool applyLitShader)
    {
        auto instance = LoadFlatpack(destination, path, position);
        if (!instance) return instance;

        // Orient the whole subtree first: SetWorldRot propagates to children
        // synchronously, so the collider refit below sees final world transforms.
        if (eulerRotation.has_value())
        {
            destination.get<sgTransform>(instance.root).rotation.world = *eulerRotation;
        }

        for (const auto entity : instance.entities)
        {
            // LoadFlatpack restores Renderables without a shader; attach the lit
            // uber-shader so the instance lights like the rest of the scene.
            if (applyLitShader)
            {
                auto* renderable = destination.try_get<Renderable>(entity);
                if (renderable && renderable->GetModel() &&
                    !destination.any_of<UberShaderComponent, CustomShaderComponent>(entity))
                {
                    auto& uber = destination.emplace<UberShaderComponent>(
                        entity, renderable->GetModel()->GetMaterialCount());
                    uber.SetFlagAll(UberShaderComponent::Flags::Lit);
                    if (destination.any_of<Animation>(entity))
                    {
                        uber.SetFlagAll(UberShaderComponent::Flags::Skinned);
                    }
                }
            }

            // Saved collider boxes are in the flatpack-root frame; refit each to its
            // world transform so picking and collision line up with the placed instance.
            auto* collideable = destination.try_get<Collideable>(entity);
            if (collideable && destination.any_of<sgTransform>(entity))
            {
                // Mesh colliders derive their broad-phase box from the render model, so
                // re-derive the local box rather than trusting the saved one (matches the
                // editor's RefreshCollisionBoundsRecursive). Box colliders keep their
                // possibly hand-authored bounds.
                if (collideable->shape == ColliderShape::RenderMesh)
                {
                    if (const auto* renderable = destination.try_get<Renderable>(entity);
                        renderable && renderable->GetModel())
                    {
                        collideable->localBoundingBox = renderable->GetModel()->CalcLocalBoundingBox();
                    }
                }
                const auto& transform = destination.get<sgTransform>(entity);
                collideable->worldBoundingBox =
                    TransformBoundingBoxByCorners(collideable->localBoundingBox, transform.GetMatrix());
            }
        }

        return instance;
    }

    FlatpackInstance InstantiateFlatpackByName(
        entt::registry& destination,
        const std::string& name,
        const Vector3 position,
        const std::optional<Vector3> eulerRotation,
        const std::filesystem::path directory)
    {
        for (const auto& entry : ListFlatpacks(directory))
        {
            if (entry.displayName == name)
            {
                return InstantiateFlatpack(destination, entry.path.string().c_str(), position, eulerRotation);
            }
        }
        std::cerr << "InstantiateFlatpackByName: no flatpack named '" << name << "' in " << directory << '\n';
        return {};
    }
} // namespace sage

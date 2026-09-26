#include "Flatpack.hpp"
#include "content/FlatpackRecords.hpp"
#include "content/ContentDocument.hpp"

#include "Archetypes.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/CustomShaderComponent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
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
        constexpr char FLATPACK_MAGIC[4] = {'L', 'Q', 'F', '6'};
        constexpr char VERSION_FIVE_FLATPACK_MAGIC[4] = {'L', 'Q', 'F', '5'};
        constexpr char VERSION_FOUR_FLATPACK_MAGIC[4] = {'L', 'Q', 'F', '4'};
        constexpr char VERSION_THREE_FLATPACK_MAGIC[4] = {'L', 'Q', 'F', '3'};
        constexpr char VERSION_TWO_FLATPACK_MAGIC[4] = {'L', 'Q', 'F', '2'};
        constexpr char LEGACY_FLATPACK_MAGIC[4] = {'L', 'Q', 'F', 'P'};

        std::vector<detail::ComponentOperations>& ComponentOperationsRegistry()
        {
            static std::vector<detail::ComponentOperations> registrations;
            return registrations;
        }

        // A serialized entity from the source registry. parentLocalId points into
        // this same vector (-1 for the root). Transform values are captured as
        // plain Vector3 (rather than sgTransform itself) so saving doesn't have
        // to touch the registry-bound proxies on the source transforms.
        // Component blobs are flagged so we can leave optional components empty
        // when the source entity doesn't carry them.
        using namespace content_binary;

        template <class Data>
        Data ReadMigrationFlatpack(const std::filesystem::path& path, const char (&magic)[4])
        {
            Data data;
            sage::serializer::ReadCompressedBinary(
                path.string().c_str(), magic,
                [&data](cereal::BinaryInputArchive& input, std::istream&) { data.archive(input); });
            return data;
        }

        template <class Left, class Right>
        bool SameComponentPayloads(const Left& left, const Right& right)
        {
            if (left.customComponents.size() != right.customComponents.size()) return false;
            for (std::size_t index = 0; index < left.customComponents.size(); ++index)
            {
                const auto& lhs = left.customComponents[index];
                const auto& rhs = right.customComponents[index];
                if (lhs.localId != rhs.localId || lhs.key != rhs.key || lhs.data != rhs.data) return false;
            }
            return true;
        }

    } // namespace

    const std::vector<detail::ComponentOperations>& detail::RegisteredComponentOperations()
    {
        return ComponentOperationsRegistry();
    }

    void detail::RegisterComponentOperations(ComponentOperations operations)
    {
        auto& registrations = ComponentOperationsRegistry();
        const auto existing = std::ranges::find(registrations, operations.key, &ComponentOperations::key);
        if (existing != registrations.end())
        {
            *existing = std::move(operations);
            return;
        }
        registrations.push_back(std::move(operations));
    }

    namespace
    {
        void EnsureEngineComponentOperations()
        {
            static const bool registered = [] {
                RegisterFlatpackComponent<ParticleEmitterComponent>("sage.ParticleEmitter");
                return true;
            }();
            (void)registered;
        }
    }

    bool RestoreRegisteredComponent(entt::registry& registry, entt::entity entity,
        const std::string& key, const std::string& data)
    {
        EnsureEngineComponentOperations();
        const auto& registrations = ComponentOperationsRegistry();
        const auto operations = std::ranges::find(registrations, key, &detail::ComponentOperations::key);
        if (operations == registrations.end()) return false;
        operations->deserialize(registry, entity, data);
        return true;
    }

    void ResolveRegisteredComponentReferences(entt::registry& registry, entt::entity entity,
        const std::unordered_map<std::uint32_t, entt::entity>& ids)
    {
        EnsureEngineComponentOperations();
        for (const auto& operations : ComponentOperationsRegistry()) operations.resolveReferences(registry, entity, ids);
    }

    bool IsFlatpackFile(const char* path)
    {
        if (content::IsDocument(path, "flatpack")) return true;
        std::ifstream storage(path, std::ios::binary);
        if (!storage.is_open()) return false;

        char fileMagic[4]{};
        storage.read(fileMagic, sizeof(fileMagic));
        return storage.gcount() == sizeof(fileMagic) &&
               (std::memcmp(fileMagic, FLATPACK_MAGIC, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, VERSION_FIVE_FLATPACK_MAGIC, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, VERSION_FOUR_FLATPACK_MAGIC, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, VERSION_THREE_FLATPACK_MAGIC, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, VERSION_TWO_FLATPACK_MAGIC, sizeof(fileMagic)) == 0 ||
                std::memcmp(fileMagic, LEGACY_FLATPACK_MAGIC, sizeof(fileMagic)) == 0);
    }

    std::optional<FlatpackComponentMigrationResult> MigrateFlatpackComponents(
        const std::filesystem::path& path, const bool writeChanges)
    {
        if (content::IsDocument(path, "flatpack"))
        {
            const auto document = content::ReadDocument(path);
            FlatpackComponentMigrationResult result;
            result.hasComponentSection = true;
            for (const auto& node : document["entities"].GetArray())
                result.recognizedComponents += node["components"].MemberCount();
            return result;
        }
        EnsureEngineComponentOperations();
        std::ifstream header(path, std::ios::binary);
        char fileMagic[4]{};
        header.read(fileMagic, sizeof(fileMagic));
        if (header.gcount() != sizeof(fileMagic)) return std::nullopt;

        const bool currentFormat = std::memcmp(fileMagic, FLATPACK_MAGIC, sizeof(fileMagic)) == 0;
        const bool versionFiveFormat =
            std::memcmp(fileMagic, VERSION_FIVE_FLATPACK_MAGIC, sizeof(fileMagic)) == 0;
        if (!currentFormat && !versionFiveFormat)
        {
            if (!IsFlatpackFile(path.string().c_str())) return std::nullopt;
            return FlatpackComponentMigrationResult{};
        }

        const auto migrate = [&](auto data, const char (&magic)[4])
            -> std::optional<FlatpackComponentMigrationResult> {
            FlatpackComponentMigrationResult result;
            result.hasComponentSection = true;
            for (auto& record : data.customComponents)
            {
                const auto operations = std::ranges::find(
                    ComponentOperationsRegistry(), record.key, &detail::ComponentOperations::key);
                if (operations == ComponentOperationsRegistry().end()) continue;

                ++result.recognizedComponents;
                auto migrated = operations->migrate(record.data);
                if (migrated == record.data) continue;
                record.data = std::move(migrated);
                ++result.changedComponents;
            }

            if (!writeChanges || result.changedComponents == 0) return result;

            auto temporaryPath = path;
            temporaryPath += ".migrating";
            std::error_code error;
            std::filesystem::remove(temporaryPath, error);
            if (!sage::serializer::WriteCompressedBinary(
                    temporaryPath.string().c_str(), magic,
                    [&data](cereal::BinaryOutputArchive& output) { data.archive(output); }))
                return std::nullopt;

            const auto verification = ReadMigrationFlatpack<decltype(data)>(temporaryPath, magic);
            if (!SameComponentPayloads(data, verification))
            {
                std::filesystem::remove(temporaryPath, error);
                return std::nullopt;
            }

            std::filesystem::rename(temporaryPath, path, error);
            if (error)
            {
                std::filesystem::remove(temporaryPath, error);
                return std::nullopt;
            }
            result.wroteChanges = true;
            return result;
        };

        if (versionFiveFormat)
            return migrate(
                ReadMigrationFlatpack<LegacyMigrationFlatpackData>(path, VERSION_FIVE_FLATPACK_MAGIC),
                VERSION_FIVE_FLATPACK_MAGIC);
        return migrate(ReadMigrationFlatpack<MigrationFlatpackData>(path, FLATPACK_MAGIC), FLATPACK_MAGIC);
    }

    bool SaveFlatpack(entt::registry& source, entt::entity root, const char* path)
    {
        try
        {
            std::vector<entt::entity> entities;
            const auto visit = [&](auto& self, entt::entity entity) -> void {
                entities.push_back(entity);
                for (auto child : source.get<sgTransform>(entity).GetChildren())
                    self(self, child);
            };
            if (!source.valid(root) || !source.any_of<sgTransform>(root)) return false;
            visit(visit, root);
            content::WriteDocument(path, content::Capture(source, entities, "flatpack", root));
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "SaveFlatpack: " << error.what() << '\n';
            return false;
        }
    }

    FlatpackInstance LoadFlatpack(entt::registry& destination, const char* path, Vector3 anchorWorldPos)
    {
        try
        {
            const auto document = content::ReadDocument(path);
            if (json::String(document, "kind") != "flatpack") throw std::runtime_error("Expected a flatpack");
            auto result = content::Instantiate(destination, document, anchorWorldPos, true);
            return {result.root, std::move(result.entities)};
        }
        catch (const std::exception& error)
        {
            std::cerr << "LoadFlatpack: " << error.what() << '\n';
            return {};
        }
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
                // possibly manually configured bounds.
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

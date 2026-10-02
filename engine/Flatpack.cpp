#include "Flatpack.hpp"
#include "content/ContentDocument.hpp"

#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CustomShaderComponent.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/UberShaderComponent.hpp"

#include "raymath.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace sage
{
    namespace
    {
        std::vector<detail::ComponentOperations>& ComponentOperationsRegistry()
        {
            static std::vector<detail::ComponentOperations> registrations;
            return registrations;
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
            static_cast<void>(registered);
        }
    } // namespace

    bool RestoreRegisteredComponent(
        entt::registry& registry, entt::entity entity, const std::string& key, const std::string& data)
    {
        EnsureEngineComponentOperations();
        const auto& registrations = ComponentOperationsRegistry();
        const auto operations = std::ranges::find(registrations, key, &detail::ComponentOperations::key);
        if (operations == registrations.end()) return false;
        operations->deserialize(registry, entity, data);
        return true;
    }

    void ResolveRegisteredComponentReferences(
        entt::registry& registry, entt::entity entity, const std::unordered_map<std::uint32_t, entt::entity>& ids)
    {
        EnsureEngineComponentOperations();
        for (const auto& operations : ComponentOperationsRegistry())
            operations.resolveReferences(registry, entity, ids);
    }

    bool IsFlatpackFile(const char* path)
    {
        return content::IsDocument(path, "flatpack");
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
            return {.root = result.root, .entities = std::move(result.entities)};
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

        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator{directory})
        {
            if (!dirEntry.is_regular_file()) continue;
            const auto& path = dirEntry.path();
            if (path.extension() != ".flatpack") continue;
            if (!IsFlatpackFile(path.string().c_str())) continue;
            entries.push_back({.displayName = path.stem().string(), .path = path});
        }

        std::ranges::sort(
            entries, [](const auto& lhs, const auto& rhs) { return lhs.displayName < rhs.displayName; });
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
                        entity, renderable->GetModel()->get().GetMaterialCount());
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
                        collideable->localBoundingBox = renderable->GetModel()->get().CalcLocalBoundingBox();
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

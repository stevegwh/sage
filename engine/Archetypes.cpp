#include "Archetypes.hpp"

#include "entt/entity/registry.hpp"

#include <unordered_map>

namespace sage
{
    struct ArchetypeIndex
    {
        struct Location
        {
            std::uint32_t archetypeId{};
            std::size_t index{};
        };

        bool connected = false;
        bool built = false;
        std::unordered_map<std::uint32_t, std::vector<entt::entity>> entitiesByArchetype;
        std::unordered_map<entt::entity, Location> entityLocations;
        std::vector<entt::entity> empty;

        void Clear()
        {
            entitiesByArchetype.clear();
            entityLocations.clear();
            built = true;
        }

        void Remove(const entt::entity entity)
        {
            const auto locationIt = entityLocations.find(entity);
            if (locationIt == entityLocations.end()) return;

            const Location location = locationIt->second;
            auto bucketIt = entitiesByArchetype.find(location.archetypeId);
            if (bucketIt != entitiesByArchetype.end() && location.index < bucketIt->second.size())
            {
                auto& bucket = bucketIt->second;
                const entt::entity moved = bucket.back();
                bucket[location.index] = moved;
                bucket.pop_back();

                if (moved != entity)
                {
                    entityLocations[moved] = Location{location.archetypeId, location.index};
                }

                if (bucket.empty())
                {
                    entitiesByArchetype.erase(bucketIt);
                }
            }

            entityLocations.erase(locationIt);
        }

        void Sync(const entt::entity entity, const Archetype archetype)
        {
            if (!archetype.IsValid())
            {
                Remove(entity);
                return;
            }

            const auto locationIt = entityLocations.find(entity);
            if (locationIt != entityLocations.end())
            {
                if (locationIt->second.archetypeId == archetype.id) return;
                Remove(entity);
            }

            auto& bucket = entitiesByArchetype[archetype.id];
            entityLocations[entity] = Location{archetype.id, bucket.size()};
            bucket.push_back(entity);
        }

        void Rebuild(entt::registry& registry)
        {
            Clear();
            for (auto view = registry.view<Archetype>(); const entt::entity entity : view)
            {
                Sync(entity, view.get<Archetype>(entity));
            }
        }

        [[nodiscard]] const std::vector<entt::entity>& Find(const Archetype archetype) const
        {
            if (!archetype.IsValid()) return empty;
            const auto bucketIt = entitiesByArchetype.find(archetype.id);
            return bucketIt != entitiesByArchetype.end() ? bucketIt->second : empty;
        }
    };

    namespace
    {
        ArchetypeIndex& GetOrCreateArchetypeIndex(entt::registry& registry)
        {
            if (auto* index = registry.ctx().find<ArchetypeIndex>())
            {
                return *index;
            }
            return registry.ctx().emplace<ArchetypeIndex>();
        }

        void SyncArchetypeIndex(entt::registry& registry, const entt::entity entity)
        {
            auto& index = GetOrCreateArchetypeIndex(registry);
            index.Sync(entity, registry.get<Archetype>(entity));
        }

        void RemoveArchetypeFromIndex(entt::registry& registry, const entt::entity entity)
        {
            if (auto* index = registry.ctx().find<ArchetypeIndex>())
            {
                index->Remove(entity);
            }
        }

        void ConnectArchetypeIndex(entt::registry& registry, ArchetypeIndex& index)
        {
            if (!index.connected)
            {
                registry.on_construct<Archetype>().connect<&SyncArchetypeIndex>();
                registry.on_update<Archetype>().connect<&SyncArchetypeIndex>();
                registry.on_destroy<Archetype>().connect<&RemoveArchetypeFromIndex>();
                index.connected = true;
            }
        }

        void EnsureArchetypeIndex(entt::registry& registry)
        {
            auto& index = GetOrCreateArchetypeIndex(registry);
            ConnectArchetypeIndex(registry, index);
            if (!index.built)
            {
                index.Rebuild(registry);
            }
        }
    } // namespace

    void EnableArchetypeIndex(entt::registry& registry)
    {
        EnsureArchetypeIndex(registry);
    }

    void RebuildArchetypeIndex(entt::registry& registry)
    {
        auto& index = GetOrCreateArchetypeIndex(registry);
        ConnectArchetypeIndex(registry, index);
        index.Rebuild(registry);
    }

    void SetArchetype(entt::registry& registry, const entt::entity entity, const Archetype archetype)
    {
        if (!registry.valid(entity)) return;

        if (registry.all_of<Archetype>(entity))
        {
            registry.patch<Archetype>(entity, [archetype](Archetype& current) { current = archetype; });
        }
        else
        {
            registry.emplace<Archetype>(entity, archetype);
        }
    }

    const std::vector<entt::entity>& FindAllWithArchetype(entt::registry& registry, const Archetype archetype)
    {
        EnsureArchetypeIndex(registry);
        return registry.ctx().get<ArchetypeIndex>().Find(archetype);
    }

    std::optional<entt::entity> FindFirstWithArchetype(entt::registry& registry, const Archetype archetype)
    {
        EnsureArchetypeIndex(registry);
        const auto& result = registry.ctx().get<ArchetypeIndex>().Find(archetype);
        if (result.empty()) return std::nullopt;
        return result.front();
    }
} // namespace sage

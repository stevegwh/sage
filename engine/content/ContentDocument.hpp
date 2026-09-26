#pragma once
#include "entt/entt.hpp"
#include "Json.hpp"
#include "raylib.h"
#include <filesystem>
#include <map>
#include <vector>

namespace sage
{
    // Document identity is independent of recycled EnTT handles. IDs fit the legacy reference map.
    struct PersistentEntityId
    {
        std::uint64_t id = 0;
        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("id", id));
        }
        template <class Inspector>
        void define_editor_options(Inspector& inspector)
        {
            inspector.field("Id", id, false);
        }
    };
    // Retain components unavailable to this executable, including future versions.
    struct UnknownContentComponents
    {
        std::map<std::string, std::string> values;
    };
    struct ContentLoadResult
    {
        entt::entity root = entt::null;
        std::vector<entt::entity> entities;
    };

    namespace content
    {
        inline constexpr std::uint32_t DOCUMENT_VERSION = 1;
        entt::entity FindEntityById(const entt::registry& registry, std::uint64_t id);
        bool IsDocument(const std::filesystem::path& path, const std::string& kind = {});
        json::Document ReadDocument(const std::filesystem::path& path);
        void WriteDocument(const std::filesystem::path& path, const json::Value& document);
        std::vector<std::string> Validate(const json::Value& document);
        std::vector<std::string> Dependencies(const json::Value& document);
        json::Document Capture(
            entt::registry& registry,
            const std::vector<entt::entity>& entities,
            const std::string& kind = "map",
            entt::entity root = entt::null,
            bool keepExternalReferences = false);
        ContentLoadResult Instantiate(
            entt::registry& registry, const json::Value& document, Vector3 anchor = {}, bool freshIds = false);
        // Restore one incremental snapshot; callers own hierarchy ordering and editor tags.
        void RestoreEntity(
            entt::registry& registry,
            entt::entity entity,
            const json::Value& node,
            const std::unordered_map<std::uint32_t, entt::entity>& references);
        // The same operation is used by CLI edits and the running editor.
        void Apply(json::Value& document, const json::Value& operation, json::Allocator& allocator);
        json::Document DescribeComponents(const json::Value* node = nullptr);
        void EnsureEngineComponentsRegistered();
    } // namespace content
} // namespace sage

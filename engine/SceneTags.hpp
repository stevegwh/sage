#pragma once

#include "cereal/types/string.hpp"
#include "entt/core/hashed_string.hpp"
#include "entt/entt.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace sage
{
    using SceneTag = std::string_view;

    struct MetaData
    {
        // Comma-separated scene tags. Example: "SpawnPoint, Goal".
        std::string tags;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(tags);
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            // Dropdown sourced from the project's scene tags
            // (sage::CustomSceneTags) rather than free-text entry.
            i.tagSet("Tags", tags);
        }
    };

    [[nodiscard]] inline bool IsSceneTagSpace(const char ch)
    {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    }

    [[nodiscard]] inline std::string_view TrimSceneTag(std::string_view tag)
    {
        while (!tag.empty() && IsSceneTagSpace(tag.front())) tag.remove_prefix(1);
        while (!tag.empty() && IsSceneTagSpace(tag.back())) tag.remove_suffix(1);
        return tag;
    }

    [[nodiscard]] inline std::string_view SceneTagText(const std::string& tags)
    {
        return {tags.data(), tags.size()};
    }

    [[nodiscard]] inline entt::id_type SceneTagStorageId(const std::string_view tag)
    {
        const auto trimmed = TrimSceneTag(tag);
        return entt::hashed_string::value(trimmed.data(), trimmed.size());
    }

    template <class Func>
    inline void ForEachSceneTag(const std::string_view tags, Func&& func)
    {
        std::string_view remaining = tags;
        while (!remaining.empty())
        {
            const auto separator = remaining.find(',');
            const auto tag = TrimSceneTag(remaining.substr(0, separator));
            if (!tag.empty()) func(tag);

            if (separator == std::string_view::npos) break;
            remaining.remove_prefix(separator + 1);
        }
    }

    [[nodiscard]] inline bool HasTag(const MetaData& meta, const std::string_view tag)
    {
        bool found = false;
        const auto needle = TrimSceneTag(tag);
        ForEachSceneTag(SceneTagText(meta.tags), [&](const std::string_view current) {
            if (current == needle) found = true;
        });
        return found;
    }

    [[nodiscard]] inline bool HasTag(
        entt::registry& registry, const entt::entity entity, const std::string_view tag)
    {
        const auto* meta = registry.try_get<MetaData>(entity);
        return meta != nullptr && HasTag(*meta, tag);
    }

    inline void AddTag(entt::registry& registry, const entt::entity entity, const std::string_view tag)
    {
        const auto trimmed = TrimSceneTag(tag);
        if (trimmed.empty()) return;

        auto& meta = registry.get_or_emplace<MetaData>(entity);
        if (!HasTag(meta, trimmed))
        {
            if (!meta.tags.empty()) meta.tags += ", ";
            meta.tags.append(trimmed.data(), trimmed.size());
        }

        auto& index = registry.storage<void>(SceneTagStorageId(trimmed));
        if (!index.contains(entity)) index.emplace(entity);
    }

    inline void RemoveTag(entt::registry& registry, const entt::entity entity, const std::string_view tag)
    {
        const auto trimmed = TrimSceneTag(tag);
        if (trimmed.empty()) return;

        if (auto* meta = registry.try_get<MetaData>(entity))
        {
            std::string nextTags;
            ForEachSceneTag(SceneTagText(meta->tags), [&](const std::string_view current) {
                if (current == trimmed) return;
                if (!nextTags.empty()) nextTags += ", ";
                nextTags.append(current.data(), current.size());
            });
            meta->tags = std::move(nextTags);
        }

        if (auto* index = registry.storage(SceneTagStorageId(trimmed)); index != nullptr)
        {
            index->remove(entity);
        }
    }

    inline void RebuildSceneTagIndex(entt::registry& registry)
    {
        for (auto view = registry.view<MetaData>(); const auto entity : view)
        {
            ForEachSceneTag(SceneTagText(view.get<MetaData>(entity).tags), [&](const std::string_view tag) {
                auto& index = registry.storage<void>(SceneTagStorageId(tag));
                if (!index.contains(entity)) index.emplace(entity);
            });
        }
    }

    [[nodiscard]] inline entt::entity FindFirstWithTag(entt::registry& registry, const std::string_view tag)
    {
        const auto trimmed = TrimSceneTag(tag);
        if (trimmed.empty()) return entt::null;

        if (auto* index = registry.storage(SceneTagStorageId(trimmed)); index != nullptr)
        {
            for (const auto entity : *index)
            {
                if (registry.valid(entity) && HasTag(registry, entity, trimmed)) return entity;
            }
        }

        for (auto view = registry.view<MetaData>(); const auto entity : view)
        {
            if (HasTag(view.get<MetaData>(entity), trimmed)) return entity;
        }
        return entt::null;
    }
} // namespace sage

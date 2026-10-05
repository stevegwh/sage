#pragma once

#include "cereal/cereal.hpp"
#include "engine/components/sgTransform.hpp"

namespace sage
{
    struct EntityVisibility
    {
        bool visible = true;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("visible", visible));
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("visible", "Visible", visible);
        }
    };

    // Transient viewport isolation; never serialized or applied to authored visibility.
    struct EntityViewScope
    {
        entt::entity root;
    };

    [[nodiscard]] inline bool IsEntityInView(const entt::registry& registry, entt::entity entity)
    {
        if (!registry.ctx().contains<EntityViewScope>()) return true;
        const auto root = registry.ctx().get<EntityViewScope>().root;
        while (registry.valid(entity))
        {
            if (entity == root) return true;
            if (!registry.all_of<sgTransform>(entity)) break;
            entity = registry.get<sgTransform>(entity).GetParent();
        }
        return false;
    }

    [[nodiscard]] inline bool IsEntityVisible(const entt::registry& registry, entt::entity entity)
    {
        if (!IsEntityInView(registry, entity)) return false;
        while (registry.valid(entity))
        {
            if (registry.all_of<EntityVisibility>(entity) && !registry.get<EntityVisibility>(entity).visible)
                return false;
            if (!registry.all_of<sgTransform>(entity)) break;
            entity = registry.get<sgTransform>(entity).GetParent();
        }
        return true;
    }
} // namespace sage

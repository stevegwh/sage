#pragma once

#include "../components/Renderable.hpp"
#include "../components/sgTransform.hpp"
#include "engine/slib.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

namespace sage
{

    class TransformSystem
    {
        entt::registry* registry;

        static Vector3 divideScale(const Vector3& worldScale, const Vector3& parentWorldScale);
        void addChild(entt::entity parent, entt::entity child, entt::entity insertBefore) const;
        void removeChild(entt::entity parent, entt::entity child) const;
        void syncWorldFromLocal(entt::entity entity) const;
        void syncLocalFromWorld(entt::entity entity) const;
        void propagateChildren(entt::entity entity);
        void onComponentAdded(entt::entity entity);
        void onComponentRemoved(entt::entity entity);

      public:
        void SetWorldPos(entt::entity entity, const Vector3& position);
        void SetWorldRot(entt::entity entity, const Vector3& rotation);
        void SetWorldScale(entt::entity entity, const Vector3& scale);
        void SetWorldScale(entt::entity entity, float scale);
        void SetLocalPos(entt::entity entity, const Vector3& position);
        void SetLocalRot(entt::entity entity, const Vector3& rotation);
        void SetLocalRot(entt::entity entity, const Quaternion& rotation);
        void SetLocalScale(entt::entity entity, const Vector3& scale);
        void SetLocalScale(entt::entity entity, float scale);
        void SetParent(entt::entity entity, entt::entity newParent);
        void SetParent(entt::entity entity, entt::entity newParent, entt::entity insertBefore);

        [[nodiscard]] entt::entity FindTransformByMeshName(const std::string& name) const
        {
            return FindTransformByMeshName<>(name);
        }

        // Very inefficient way to find the entity id of a renderable
        template <typename... Components>
        [[nodiscard]] entt::entity FindTransformByMeshName(const std::string& name) const
        {
            auto meshKey = StripPath(name);
            auto view = registry->view<sgTransform, Components...>();

            for (const auto& entity : view)
            {
                const auto& renderable = registry->get<Renderable>(entity);
                auto model = renderable.GetModel();
                if (!model) continue;
                const auto& key = StripPath(model->GetKey());
                if (key == meshKey) return entity;
            }
            return entt::null;
        }

        [[nodiscard]] entt::entity FindTransformByName(const std::string& name) const
        {
            return FindTransformByName<>(name);
        }

        template <typename... Components>
        [[nodiscard]] entt::entity FindTransform(const std::string& name) const
        {
            auto entity = FindTransformByName<Components...>(name);
            if (entity == entt::null)
            {
                entity = FindTransformByMeshName<Components...>(name);
            }
            return entity;
        }

        [[nodiscard]] entt::entity FindTransform(const std::string& name) const
        {
            auto entity = FindTransformByName(name);
            if (entity == entt::null)
            {
                entity = FindTransformByMeshName(name);
            }
            return entity;
        }

        // Very inefficient way to find the entity id of a renderable
        template <typename... Components>
        [[nodiscard]] entt::entity FindTransformByName(const std::string& name) const
        {
            auto nameStripped = StripPath(name);
            auto view = registry->view<sgTransform, Components...>();

            for (const auto& entity : view)
            {
                const auto& transform = registry->get<sgTransform>(entity);
                if (transform.name == nameStripped) return entity;
            }
            return entt::null;
        }

        explicit TransformSystem(entt::registry* _registry);
    };
} // namespace sage

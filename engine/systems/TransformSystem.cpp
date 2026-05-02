#include "TransformSystem.hpp"

#include "components/sgTransform.hpp"
#include "raylib.h"
#include "slib.hpp"

namespace sage
{

    void TransformSystem::updateChildrenPos(entt::entity entity)
    {
        const auto& transform = registry->get<sgTransform>(entity);
        for (auto childEntity : transform.m_children)
        {
            const auto& child = registry->get<sgTransform>(childEntity);
            SetPosition(childEntity, Vector3Add(transform.m_positionWorld, child.GetLocalPos()));
        }
    }

    void TransformSystem::updateChildrenRot(entt::entity entity)
    {
        const auto& transform = registry->get<sgTransform>(entity);
        for (auto childEntity : transform.m_children)
        {
            const auto& child = registry->get<sgTransform>(childEntity);
            SetRotation(childEntity, Vector3Add(transform.m_rotationWorld, child.GetLocalRot()));
            // TODO: Scale
        }
    }

    void TransformSystem::SetLocalPos(entt::entity entity, const Vector3& position)
    {
        auto& transform = registry->get<sgTransform>(entity);
        transform.m_positionLocal = position;
        if (transform.m_parent == entt::null) return;
        const auto& parent = registry->get<sgTransform>(transform.m_parent);
        SetPosition(entity, Vector3Add(parent.m_positionWorld, transform.m_positionLocal));
    }

    void TransformSystem::SetLocalRot(entt::entity entity, const Quaternion& rotation)
    {
        Vector3 rot = QuaternionToEuler(rotation);
        rot = Vector3MultiplyByValue(rot, RAD2DEG); // raylib gives this back in rad
        SetLocalRot(entity, rot);
    }

    void TransformSystem::SetLocalRot(entt::entity entity, const Vector3& rotation)
    {
        auto& transform = registry->get<sgTransform>(entity);
        transform.m_rotationLocal = rotation;
        if (transform.m_parent == entt::null)
        {
            SetRotation(entity, transform.m_rotationLocal);
        }
        else
        {
            const auto& parent = registry->get<sgTransform>(transform.m_parent);
            SetRotation(entity, Vector3Add(parent.m_rotationWorld, transform.m_rotationLocal));
        }
    }

    void TransformSystem::SetPosition(entt::entity entity, const Vector3& position)
    {
        auto& transform = registry->get<sgTransform>(entity);

        if (transform.m_parent != entt::null)
        {
            transform.m_positionWorld = position;
        }
        else
        {
            transform.m_positionLocal = position;
            transform.m_positionWorld = position;
        }
        updateChildrenPos(entity);

        transform.onPositionUpdate.Publish(entity);
    }

    void TransformSystem::SetRotation(entt::entity entity, const Vector3& rotation)
    {
        auto& transform = registry->get<sgTransform>(entity);

        if (transform.m_parent != entt::null)
        {
            transform.m_rotationWorld = rotation;
        }
        else
        {
            transform.m_rotationLocal = rotation;
            transform.m_rotationWorld = rotation;
        }
        updateChildrenRot(entity);
    }

    void TransformSystem::SetScale(entt::entity entity, const Vector3& scale)
    {
        auto& transform = registry->get<sgTransform>(entity);

        transform.m_scale = scale;
    }

    void TransformSystem::SetScale(entt::entity entity, float scale)
    {
        auto& transform = registry->get<sgTransform>(entity);

        transform.m_scale = {scale, scale, scale};
    }

    void TransformSystem::SetParent(entt::entity entity, entt::entity newParent)
    {
        auto& transform = registry->get<sgTransform>(entity);

        if (transform.m_parent != entt::null)
        {
            auto& parentChildren = registry->get<sgTransform>(transform.m_parent).m_children;
            // Remove this from old parent's children list
            auto it = std::find(parentChildren.begin(), parentChildren.end(), entity);
            if (it != parentChildren.end()) parentChildren.erase(it);
        }

        transform.m_parent = newParent;

        if (transform.m_parent != entt::null)
        {
            auto& parent = registry->get<sgTransform>(transform.m_parent);
            parent.m_children.push_back(entity);
            // Recalculate local position based on current world position and new parent
            transform.m_positionLocal = Vector3Subtract(transform.m_positionWorld, parent.GetWorldPos());
            transform.m_rotationLocal = Vector3Subtract(transform.m_rotationWorld, parent.GetWorldRot());
        }
        else
        {
            // If no parent, local = world
            transform.m_positionLocal = transform.m_positionWorld;
            transform.m_rotationLocal = transform.m_rotationWorld;
        }
    }

    void TransformSystem::AddChild(entt::entity entity, entt::entity newChild)
    {
        SetParent(newChild, entity);
    }

    void TransformSystem::SetViaMatrix(entt::entity entity, Matrix mat)
    {
        Matrix newMat{};
        Vector3 trans{};
        Quaternion rotQ{};
        Vector3 scale{};
        MatrixDecompose(newMat, &trans, &rotQ, &scale);
        Vector3 rot = QuaternionToEuler(rotQ);
        SetScale(entity, scale);
        SetRotation(entity, rot);
        SetPosition(entity, trans);
    }

    void TransformSystem::onComponentRemoved(entt::entity entity)
    {
    }

    void TransformSystem::onComponentAdded(entt::entity entity)
    {
    }

    TransformSystem::TransformSystem(entt::registry* _registry) : registry(_registry)
    {
        registry->on_construct<sgTransform>().connect<&TransformSystem::onComponentAdded>(this);
        registry->on_destroy<sgTransform>().connect<&TransformSystem::onComponentRemoved>(this);
    }
} // namespace sage
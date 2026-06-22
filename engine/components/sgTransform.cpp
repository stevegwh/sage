//
// Created by Steve Wheeler on 03/05/2024.
//

#include "sgTransform.hpp"
#include "raymath.h"
#include "slib.hpp"
#include "systems/ScriptSystem.hpp"
#include "systems/TransformSystem.hpp"
#include "sol/sol.hpp"

#include <cassert>
#include <utility>

namespace sage
{
    void sgTransform::define_lua_bindings(ScriptSystem& scripts)
    {
        scripts.GetLuaState().new_usertype<sgTransform>(
            "Transform",
            sol::no_constructor,
            "name",
            &sgTransform::name,
            "GetPosition",
            [](const sgTransform& transform) -> Vector3 { return transform.GetWorldPos(); },
            "SetPosition",
            [](sgTransform& transform, const Vector3& value) { transform.position.world = value; },
            "GetLocalPosition",
            [](const sgTransform& transform) -> Vector3 { return transform.GetLocalPos(); },
            "SetLocalPosition",
            [](sgTransform& transform, const Vector3& value) { transform.position.local = value; },
            "GetRotation",
            [](const sgTransform& transform) -> Vector3 { return transform.GetWorldRot(); },
            "SetRotation",
            [](sgTransform& transform, const Vector3& value) { transform.rotation.world = value; },
            "GetScale",
            [](const sgTransform& transform) -> Vector3 { return transform.GetScale(); },
            "SetScale",
            [](sgTransform& transform, const Vector3& value) { transform.scale.world = value; },
            "Forward",
            [](const sgTransform& transform) { return transform.forward(); },
            "GetParent",
            [](const sgTransform& transform, sol::this_state state) -> sol::object {
                const auto parent = transform.GetParent();
                if (parent == entt::null) return sol::lua_nil;
                return sol::make_object(state, static_cast<std::uint32_t>(parent));
            });
        scripts.RegisterApiExtension("sage", [](sol::table& api, entt::entity, entt::registry& registry) {
            api.set_function("GetTransform", [&registry](const std::uint32_t id) {
                return registry.try_get<sgTransform>(static_cast<entt::entity>(id));
            });
        });
    }

    void sgTransform::SetParent(const entt::entity newParent)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetParent(m_entity, newParent);
    }

    void sgTransform::ResolveSerializedParent(
        const std::unordered_map<std::uint32_t, entt::entity>& idMap)
    {
        if (m_savedParentId == serializedNullId()) return;
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);

        const auto it = idMap.find(m_savedParentId);
        if (it != idMap.end())
        {
            m_transformSystem->SetParent(m_entity, it->second);
        }
        m_savedParentId = serializedNullId();
    }

    void sgTransform::writeLocalPos(const Vector3& v)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetLocalPos(m_entity, v);
    }

    void sgTransform::writeWorldPos(const Vector3& v)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetWorldPos(m_entity, v);
    }

    void sgTransform::writeLocalRot(const Vector3& v)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetLocalRot(m_entity, v);
    }

    void sgTransform::writeWorldRot(const Vector3& v)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetWorldRot(m_entity, v);
    }

    void sgTransform::writeLocalScale(const Vector3& v)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetLocalScale(m_entity, v);
    }

    void sgTransform::writeWorldScale(const Vector3& v)
    {
        assert(m_transformSystem != nullptr);
        assert(m_entity != entt::null);
        m_transformSystem->SetWorldScale(m_entity, v);
    }

    void sgTransform::Bind(TransformSystem* transformSystem, const entt::entity entity)
    {
        assert(transformSystem != nullptr);
        assert(entity != entt::null);
        m_transformSystem = transformSystem;
        m_entity = entity;
        rebindProxies();
    }

    void sgTransform::rebindProxies()
    {
        auto bindVec = [this](auto& field) {
            field.owner_ = this;
            field.x.parent = &field;
            field.y.parent = &field;
            field.z.parent = &field;
        };
        bindVec(position.local);
        bindVec(position.world);
        bindVec(rotation.local);
        bindVec(rotation.world);
        bindVec(scale.local);
        bindVec(scale.world);
    }

    Matrix sgTransform::GetMatrixNoRot() const
    {
        Matrix trans = MatrixTranslate(GetWorldPos().x, GetWorldPos().y, GetWorldPos().z);
        Matrix _scale = MatrixScale(GetScale().x, GetScale().y, GetScale().z);
        return MatrixMultiply(_scale, trans);
    }

    Matrix sgTransform::GetMatrix() const
    {
        Matrix trans = MatrixTranslate(GetWorldPos().x, GetWorldPos().y, GetWorldPos().z);
        Matrix _scale = MatrixScale(GetScale().x, GetScale().y, GetScale().z);
        Matrix rot = EulerToMatrix(GetWorldRot());
        return MatrixMultiply(MatrixMultiply(_scale, rot), trans);
    }

    Vector3 sgTransform::forward() const
    {
        Matrix matrix = GetMatrix();
        Vector3 forward = {matrix.m8, matrix.m9, matrix.m10};
        return Vector3Normalize(forward);
    }

    const Vector3& sgTransform::GetLocalPos() const
    {
        return position.local.value;
    }

    const Vector3& sgTransform::GetWorldPos() const
    {
        return position.world.value;
    }

    const Vector3& sgTransform::GetWorldRot() const
    {
        return rotation.world.value;
    }

    const Vector3& sgTransform::GetLocalRot() const
    {
        return rotation.local.value;
    }

    const Vector3& sgTransform::GetScale() const
    {
        return scale.world.value;
    }

    const Vector3& sgTransform::GetLocalScale() const
    {
        return scale.local.value;
    }

    entt::entity sgTransform::GetParent() const
    {
        return m_parent;
    }

    const std::vector<entt::entity>& sgTransform::GetChildren() const
    {
        return m_children;
    }

    void DestroyHierarchy(entt::registry& registry, const entt::entity root)
    {
        if (!registry.valid(root)) return;

        if (const auto* transform = registry.try_get<sgTransform>(root))
        {
            // Copy: destroying a child fires TransformSystem's on_destroy hook, which
            // mutates this live child list. Recurse children-first so nothing is left
            // orphaned and the root's child list is already drained when it's destroyed.
            const std::vector<entt::entity> children = transform->GetChildren();
            for (const auto child : children)
            {
                DestroyHierarchy(registry, child);
            }
        }

        if (registry.valid(root)) registry.destroy(root);
    }

    sgTransform::sgTransform()
    {
        scale.local.value = {1, 1, 1};
        scale.world.value = {1, 1, 1};
        rebindProxies();
    }

    void sgTransform::assignStateFrom(const sgTransform& rhs)
    {
        m_entity = rhs.m_entity;
        m_transformSystem = rhs.m_transformSystem;
        m_parent = rhs.m_parent;
        m_children = rhs.m_children;
        m_savedParentId = rhs.m_savedParentId;
        name = rhs.name;
        direction = rhs.direction;
        movementDirectionDebugLine = rhs.movementDirectionDebugLine;
        position.world.value = rhs.position.world.value;
        position.local.value = rhs.position.local.value;
        rotation.world.value = rhs.rotation.world.value;
        rotation.local.value = rhs.rotation.local.value;
        scale.world.value = rhs.scale.world.value;
        scale.local.value = rhs.scale.local.value;
    }

    void sgTransform::stealStateFrom(sgTransform&& rhs)
    {
        m_entity = rhs.m_entity;
        m_transformSystem = rhs.m_transformSystem;
        m_parent = rhs.m_parent;
        m_children = std::move(rhs.m_children);
        m_savedParentId = rhs.m_savedParentId;
        name = std::move(rhs.name);
        direction = rhs.direction;
        movementDirectionDebugLine = rhs.movementDirectionDebugLine;
        position.world.value = rhs.position.world.value;
        position.local.value = rhs.position.local.value;
        rotation.world.value = rhs.rotation.world.value;
        rotation.local.value = rhs.rotation.local.value;
        scale.world.value = rhs.scale.world.value;
        scale.local.value = rhs.scale.local.value;
        rhs.m_transformSystem = nullptr;
        rhs.m_entity = entt::null;
        rhs.m_savedParentId = serializedNullId();
    }

    // The on_construct signal rebinds registry copies to their new entity/system.
    sgTransform::sgTransform(const sgTransform& rhs)
    {
        assignStateFrom(rhs);
        rebindProxies();
    }

    sgTransform& sgTransform::operator=(const sgTransform& rhs)
    {
        if (this == &rhs) return *this;
        assignStateFrom(rhs);
        rebindProxies();
        return *this;
    }

    sgTransform::sgTransform(sgTransform&& rhs) noexcept
    {
        stealStateFrom(std::move(rhs));
        rebindProxies();
    }

    sgTransform& sgTransform::operator=(sgTransform&& rhs) noexcept
    {
        if (this == &rhs) return *this;
        stealStateFrom(std::move(rhs));
        rebindProxies();
        return *this;
    }
} // namespace sage

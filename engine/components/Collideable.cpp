//
// Created by Steve Wheeler on 02/05/2024.
//

#include "Collideable.hpp"

#include "components/sgTransform.hpp"

#include "raymath.h"

namespace sage
{
    void Collideable::OnTransformUpdate(entt::entity self)
    {
        assert(registry);
        auto& trans = registry->get<sgTransform>(self);
        Matrix mat = trans.GetMatrixNoRot(); // AABB, so no rotation
        SetWorldBoundingBox(mat);
    }

    void Collideable::SetWorldBoundingBox(Matrix mat)
    {
        auto bb = localBoundingBox;
        bb.min = Vector3Transform(bb.min, mat);
        bb.max = Vector3Transform(bb.max, mat);
        worldBoundingBox = bb;
    }

    void Collideable::Enable()
    {
        active = true;
    }

    void Collideable::Disable()
    {
        active = false;
    }

    // Applies the world matrix. Does not move, so does not subscribe to Transform updates.
    Collideable::Collideable(const BoundingBox& _localBoundingBox, const Matrix& worldMatrix)
        : localBoundingBox(_localBoundingBox)
    {
        SetWorldBoundingBox(worldMatrix);
    }

    // Applies the world matrix of this entity's transform and subscribes to its updates.
    Collideable::Collideable(entt::registry* _registry, entt::entity _self, BoundingBox _localBoundingBox)
        : registry(_registry), localBoundingBox(_localBoundingBox), worldBoundingBox(_localBoundingBox)
    {
        assert(registry->any_of<sgTransform>(_self));
        auto& transform = registry->get<sgTransform>(_self);
        transform.onPositionUpdate.Subscribe([this](entt::entity entity) { OnTransformUpdate(entity); });
        SetWorldBoundingBox(transform.GetMatrix());
    }

    // Copy constructor
    Collideable::Collideable(const Collideable& other)
        : registry(other.registry),
          active(other.active),
          localBoundingBox(other.localBoundingBox),
          worldBoundingBox(other.worldBoundingBox),
          collisionLayer(other.collisionLayer),
          debugDraw(other.debugDraw)
    {
        // Note: We don't copy event subscriptions - the new copy starts without subscriptions
        // If you need to re-subscribe, that should be done explicitly after copying
    }

    // Copy assignment operator
    Collideable& Collideable::operator=(const Collideable& other)
    {
        if (this != &other)
        {
            registry = other.registry;
            active = other.active;
            localBoundingBox = other.localBoundingBox;
            worldBoundingBox = other.worldBoundingBox;
            collisionLayer = other.collisionLayer;
            debugDraw = other.debugDraw;
            // Note: We don't copy event subscriptions
        }
        return *this;
    }

    // Move constructor
    Collideable::Collideable(Collideable&& other) noexcept
        : registry(other.registry),
          active(other.active),
          localBoundingBox(other.localBoundingBox),
          worldBoundingBox(other.worldBoundingBox),
          collisionLayer(other.collisionLayer),
          debugDraw(other.debugDraw)
    {
        // Note: Event subscriptions are not transferred in move
        other.registry = nullptr;
    }

    // Move assignment operator
    Collideable& Collideable::operator=(Collideable&& other) noexcept
    {
        if (this != &other)
        {
            registry = other.registry;
            active = other.active;
            localBoundingBox = other.localBoundingBox;
            worldBoundingBox = other.worldBoundingBox;
            collisionLayer = other.collisionLayer;
            debugDraw = other.debugDraw;

            other.registry = nullptr;
        }
        return *this;
    }
} // namespace sage
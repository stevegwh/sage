//
// Created by Steve Wheeler on 06/12/2024.
//

#include "CursorClickIndicator.hpp"

#include "components/MoveableActor.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "Cursor.hpp"
#include "EngineSystems.hpp"
#include "systems/NavigationGridSystem.hpp"

namespace sage
{

    void CursorClickIndicator::onCursorClick(entt::entity entity) const
    {
        const auto selectedActor = sys->cursor->GetSelectedActor();
        if (entity == entt::null || !registry->any_of<Collideable>(entity) ||
            !sys->navigationGridSystem->IsValidMove(sys->cursor->getFirstNaviCollision().point, selectedActor))
        {
            disableIndicator();
            return;
        }

        const auto& col = registry->get<Collideable>(entity);
        if (col.collisionLayer != CollisionLayer::GEOMETRY_SIMPLE &&
            col.collisionLayer != CollisionLayer::GEOMETRY_COMPLEX)
        {
            disableIndicator();
            return;
        }
        const auto& moveable = registry->get<MoveableActor>(selectedActor);
        if (!moveable.IsMoving())
        {
            disableIndicator();
            return;
        }
        auto& renderable = registry->get<Renderable>(self);
        renderable.active = true;
        const auto dest = moveable.GetDestination();
        auto& transform = registry->get<sgTransform>(self);
        transform.SetPosition(dest);
    }

    void CursorClickIndicator::onSelectedActorChanged(entt::entity, entt::entity current)
    {
        if (destinationReachedSub.IsActive())
        {
            destinationReachedSub.UnSubscribe();
        }
        auto& renderable = registry->get<Renderable>(self);
        renderable.active = false;
        auto& moveable = registry->get<MoveableActor>(current);
        destinationReachedSub =
            moveable.onDestinationReached.Subscribe([this](entt::entity) { disableIndicator(); });
    }

    void CursorClickIndicator::disableIndicator() const
    {
        auto& renderable = registry->get<Renderable>(self);
        renderable.active = false;
    }

    void CursorClickIndicator::Update()
    {
        auto& renderable = registry->get<Renderable>(self);
        if (!renderable.active) return;
        k += 5 * GetFrameTime();
        float normalizedScale = (sin(k) + 1.0f) * 0.5f;
        float minScale = 0.25f;
        float maxScale = 1.0f;
        float scale = minScale + normalizedScale * (maxScale - minScale);

        auto& transform = registry->get<sgTransform>(self);
        transform.SetScale(scale);
    }

    CursorClickIndicator::CursorClickIndicator(entt::registry* _registry, EngineSystems* _sys)
        : registry(_registry), sys(_sys), self(registry->create())
    {
        _sys->cursor->onLeftClick.Subscribe([this](const entt::entity entity) { onCursorClick(entity); });
        _sys->cursor->onSelectedActorChange.Subscribe(
            [this](entt::entity prev, entt::entity current) { onSelectedActorChanged(prev, current); });

        // Init indicator graphics here
        _registry->emplace<sgTransform>(self, self);
        auto model = LoadModelFromMesh(GenMeshSphere(1, 32, 32));
        ModelSafe sphere(model);
        auto& renderable =
            _registry->emplace<Renderable>(self, std::move(sphere), MatrixIdentity()); // requires model etc.
        renderable.hint = GREEN;
        renderable.active = false;
    }

} // namespace sage
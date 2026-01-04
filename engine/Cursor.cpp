//
// Created by Steve Wheeler on 04/05/2024.
//

#include "engine/Cursor.hpp"

#include "engine/Camera.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/NavigationGridSquare.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Settings.hpp"
#include "engine/systems/CollisionSystem.hpp"
#include "engine/systems/NavigationGridSystem.hpp"

#include <algorithm>

#ifndef FLT_MAX
#define FLT_MAX                                                                                                   \
    340282346638528859811704183484516925440.0f // Maximum value of a float, from bit
                                               // pattern 01111111011111111111111111111111
#endif

#include <ranges>

namespace sage
{
    void Cursor::checkMouseHover()
    {
        if (!registry->any_of<Collideable>(m_mouseHitInfo.collidedEntityId)) return;
        const auto& layer = registry->get<Collideable>(m_mouseHitInfo.collidedEntityId).collisionLayer;

        if (!m_mouseHitInfo.rlCollision.hit ||
            std::ranges::find(cursorHoverLayers, layer) == cursorHoverLayers.end())
        {
            if (m_hoverInfo.has_value())
            {
                onStopHover.Publish();
            }
            m_hoverInfo.reset();
            return;
        }
        if (!m_hoverInfo.has_value() || m_mouseHitInfo.collidedEntityId != m_hoverInfo->target)
        {
            HoverInfo newInfo;
            newInfo.target = m_mouseHitInfo.collidedEntityId;
            newInfo.beginHoverTime = GetTime();
            m_hoverInfo.emplace(newInfo);
        }
    }

    void Cursor::onMouseHover() const
    {
        if (!enabled) return;

        onHover.Publish(m_mouseHitInfo.collidedEntityId);
    }

    void Cursor::onMouseLeftClick() const
    {
        if (!enabled) return;

        const auto& layer = registry->get<Collideable>(m_mouseHitInfo.collidedEntityId).collisionLayer;
        if (layer == CollisionLayer::GEOMETRY_SIMPLE || layer == CollisionLayer::GEOMETRY_COMPLEX ||
            layer == CollisionLayer::STAIRS)
        {
            onFloorClick.Publish(m_mouseHitInfo.collidedEntityId);
        }
        onLeftClick.Publish(m_mouseHitInfo.collidedEntityId);
    }

    void Cursor::onMouseRightClick() const
    {
        if (!enabled) return;
        onRightClick.Publish(m_mouseHitInfo.collidedEntityId);
    }

    void Cursor::onMouseLeftDown()
    {
        if (!enabled) return;
        leftClickTimer += GetFrameTime();

        if (leftClickTimer < 0.25) return;
        leftClickTimer = 0;

        const auto& layer = registry->get<Collideable>(m_mouseHitInfo.collidedEntityId).collisionLayer;
        if (layer == CollisionLayer::GEOMETRY_SIMPLE || layer == CollisionLayer::GEOMETRY_COMPLEX ||
            layer == CollisionLayer::STAIRS)
        {
            onFloorClick.Publish(m_mouseHitInfo.collidedEntityId);
        }
    }

    void Cursor::onMouseRightDown() const
    {
        if (!enabled) return;
    }

    void Cursor::DisableContextSwitching() // Lock mouse context? Like changing depending
                                           // on getFirstCollision.
    {
        contextLocked = true;
    }

    void Cursor::EnableContextSwitching()
    {
        contextLocked = false;
    }

    void Cursor::Enable()
    {
        enabled = true;
    }

    void Cursor::Disable()
    {
        enabled = false;
    }

    void Cursor::Hide()
    {
        hideCursor = true;
    }

    void Cursor::Show()
    {
        hideCursor = false;
    }

    bool Cursor::OutOfRange() const
    {
        auto mouseHit = m_naviHitInfo.rlCollision.point;
        const auto& moveable = registry->get<MoveableActor>(selectedActor);
        GridSquare minRange{};
        GridSquare maxRange{};
        sys->navigationGridSystem->GetPathfindRange(selectedActor, moveable.pathfindingBounds, minRange, maxRange);

        return !sys->navigationGridSystem->CheckWithinBounds(mouseHit, minRange, maxRange);
    }

    void Cursor::changeCursors(CollisionLayer collisionLayer)
    {
        if (contextLocked) return;
        if (OutOfRange() ||
            ((collisionLayer == CollisionLayer::GEOMETRY_SIMPLE ||
              collisionLayer == CollisionLayer::GEOMETRY_COMPLEX || collisionLayer == CollisionLayer::STAIRS) &&
             !sys->navigationGridSystem->IsValidMove(m_naviHitInfo.rlCollision.point, selectedActor)))
        {
            currentTex = ResourceManager::GetInstance().TextureLoad("cursor_denied");
            currentColor = invalidColor;
            return;
        }
        if (cursorTextureMap.contains(collisionLayer))
        {
            currentTex = ResourceManager::GetInstance().TextureLoad(cursorTextureMap.at(collisionLayer));
        }
        else
        {
            currentTex = ResourceManager::GetInstance().TextureLoad("cursor_regular");
        }
    }

    void Cursor::getMouseRayCollision()
    {
        // Reset hit information
        resetHitInfo(m_mouseHitInfo);
        resetHitInfo(m_naviHitInfo);
        hitObjectName = "None";
        currentTex = ResourceManager::GetInstance().TextureLoad("cursor_regular");
        currentColor = defaultColor;

        auto viewport = sys->settings->GetViewPort();
        // Get ray and test against objects
        ray = GetScreenToWorldRayEx(GetMousePosition(), *sys->camera->getRaylibCam(), viewport.x, viewport.y);
        auto collisions = sys->collisionSystem->GetCollisionsWithRay(ray);

        // Replace floor BB hit with mesh hit then re-sort vector
        // Discards hits with a BB that do not have a collision with mesh
        for (auto it = collisions.begin(); it != collisions.end();)
        {
            if (it->collisionLayer == CollisionLayer::GEOMETRY_COMPLEX ||
                it->collisionLayer == CollisionLayer::STAIRS)
            {
                if (!findMeshCollision(*it))
                {
                    it = collisions.erase(it);
                    continue;
                }
            }

            ++it;
        }

        if (collisions.empty()) // Could put this sooner, but would need to repeat after above
        {
            return;
        }

        CollisionSystem::SortCollisionsByDistance(collisions);

        m_mouseHitInfo = collisions[0];

        if (m_mouseHitInfo.collisionLayer == CollisionLayer::GEOMETRY_SIMPLE ||
            m_mouseHitInfo.collisionLayer == CollisionLayer::GEOMETRY_COMPLEX ||
            m_mouseHitInfo.collisionLayer == CollisionLayer::STAIRS)
        {
            m_naviHitInfo = m_mouseHitInfo;
        }
        else
        {
            // Find first navigation collision (if any)
            const auto navIt = std::find_if(collisions.begin(), collisions.end(), [](const CollisionInfo& coll) {
                return coll.collisionLayer == CollisionLayer::GEOMETRY_SIMPLE ||
                       coll.collisionLayer == CollisionLayer::GEOMETRY_COMPLEX ||
                       coll.collisionLayer == CollisionLayer::STAIRS;
            });

            if (navIt != collisions.end())
            {
                m_naviHitInfo = *navIt;
            }
        }

        onCollisionHit.Publish(m_mouseHitInfo.collidedEntityId);

        const auto layer = registry->get<Collideable>(m_mouseHitInfo.collidedEntityId).collisionLayer;
        changeCursors(layer);
    }

    void Cursor::resetHitInfo(CollisionInfo& hitInfo)
    {
        hitInfo.rlCollision = {};
        hitInfo.rlCollision.distance = FLT_MAX;
        hitInfo.rlCollision.hit = false;
    }

    // Find the model's mesh collision (instead of using its bounding box)
    bool Cursor::findMeshCollision(CollisionInfo& hitInfo) const
    {
        if (registry->any_of<Renderable>(hitInfo.collidedEntityId))
        {
            const auto& renderable = registry->get<Renderable>(hitInfo.collidedEntityId);
            const auto& transform = registry->get<sgTransform>(hitInfo.collidedEntityId);

            for (int i = 0; i < renderable.GetModel()->GetMeshCount(); ++i)
            {
                if (const auto meshCollision =
                        renderable.GetModel()->GetRayMeshCollision(ray, i, transform.GetMatrix());
                    meshCollision.hit)
                {
                    hitInfo.rlCollision = meshCollision;
                    return true;
                }
            }
        }
        return false;
    }

    const CollisionInfo& Cursor::getMouseHitInfo() const
    {
        return m_mouseHitInfo;
    }

    const RayCollision& Cursor::getFirstNaviCollision() const
    {
        return m_naviHitInfo.rlCollision;
    }

    const RayCollision& Cursor::getFirstCollision() const
    {
        return m_mouseHitInfo.rlCollision;
    }

    entt::entity Cursor::GetSelectedActor() const
    {
        return selectedActor;
    }

    void Cursor::SetSelectedActor(entt::entity actor)
    {
        const auto& old = selectedActor;
        selectedActor = actor;
        onSelectedActorChange.Publish(old, actor);
    }

    void Cursor::Update()
    {

        getMouseRayCollision();
        checkMouseHover();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            onMouseLeftClick();
            leftClickTimer = 0;
        }
        else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            onMouseRightClick();
        }
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            onMouseLeftDown();
        }
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            onMouseRightDown();
        }
        else if (
            !OutOfRange() && m_hoverInfo.has_value() &&
            GetTime() >= m_hoverInfo.value().beginHoverTime + m_hoverInfo.value().hoverTimeThreshold)
        {
            onMouseHover();
        }
    }

    void Cursor::DrawDebug() const
    {
        if (!m_mouseHitInfo.rlCollision.hit) return;
        if (contextLocked) return;
        DrawCube(m_mouseHitInfo.rlCollision.point, 0.5f, 0.5f, 0.5f, currentColor);
        Vector3 normalEnd;
        normalEnd.x = m_mouseHitInfo.rlCollision.point.x + m_mouseHitInfo.rlCollision.normal.x;
        normalEnd.y = m_mouseHitInfo.rlCollision.point.y + m_mouseHitInfo.rlCollision.normal.y;
        normalEnd.z = m_mouseHitInfo.rlCollision.point.z + m_mouseHitInfo.rlCollision.normal.z;
        DrawLine3D(m_mouseHitInfo.rlCollision.point, normalEnd, RED);
    }

    void Cursor::Draw3D()
    {
    }

    void Cursor::Draw2D() const
    {
        if (hideCursor) return;
        Vector2 pos = GetMousePosition();
        // TODO: Awful hack below
        if (currentTex.id != ResourceManager::GetInstance().TextureLoad("cursor_regular").id)
        {
            pos = Vector2Subtract(
                pos, {static_cast<float>(currentTex.width / 2), static_cast<float>(currentTex.height / 2)});
        }
        DrawTextureEx(currentTex, pos, 0.0, 1.0f, WHITE);
    }

    Cursor::Cursor(entt::registry* _registry, EngineSystems* _sys) : registry(_registry), sys(_sys)
    {
        currentTex = ResourceManager::GetInstance().TextureLoad("cursor_regular");
        EnableContextSwitching();
    }
} // namespace sage

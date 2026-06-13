//
// Created by Steve Wheeler on 04/05/2024.
//

#include "Cursor.hpp"

#include "components/CollisionIntent.hpp"
#include "EngineSystems.hpp"
#include "MousePicker.hpp"
#include "ResourceManager.hpp"
#include "Settings.hpp"
#include "systems/CollisionSystem.hpp"
#include "systems/NavigationGridSystem.hpp"

#include <utility>

namespace sage
{
    void Cursor::checkMouseHover()
    {
        const auto& mouseHitInfo = getMouseHitInfo();
        if (!mouseHitInfo.rlCollision.hit || !mouseHitInfo.collisionLayer.IsValid())
        {
            if (m_hoverInfo.has_value())
            {
                onStopHover.Publish();
            }
            m_hoverInfo.reset();
            return;
        }

        const auto* cursorTarget = registry->try_get<CursorTarget>(mouseHitInfo.collidedEntityId);
        if (cursorTarget == nullptr || !cursorTarget->hoverable)
        {
            if (m_hoverInfo.has_value())
            {
                onStopHover.Publish();
            }
            m_hoverInfo.reset();
            return;
        }
        if (!m_hoverInfo.has_value() || mouseHitInfo.collidedEntityId != m_hoverInfo->target)
        {
            HoverInfo newInfo;
            newInfo.target = mouseHitInfo.collidedEntityId;
            newInfo.beginHoverTime = GetTime();
            m_hoverInfo.emplace(newInfo);
        }
    }

    void Cursor::onMouseHover() const
    {
        if (!enabled) return;

        const auto& hitInfo = getMouseHitInfo();
        onHover.Publish(hitInfo.collidedEntityId, hitInfo.collisionLayer);
    }

    void Cursor::onMouseLeftClick() const
    {
        if (!enabled) return;

        const auto& hitInfo = getMouseHitInfo();
        if (shouldPublishNavigationClick(hitInfo))
        {
            const auto& navHitInfo = getNavigationHitInfo();
            onNavigationClick.Publish(navHitInfo.collidedEntityId, navHitInfo.collisionLayer);
        }
        onLeftClick.Publish(hitInfo.collidedEntityId, hitInfo.collisionLayer);
    }

    void Cursor::onMouseRightClick() const
    {
        if (!enabled) return;
        const auto& hitInfo = getMouseHitInfo();
        onRightClick.Publish(hitInfo.collidedEntityId, hitInfo.collisionLayer);
    }

    void Cursor::onMouseLeftDown()
    {
        if (!enabled) return;
        leftClickTimer += GetFrameTime();

        if (leftClickTimer < 0.25) return;
        leftClickTimer = 0;

        const auto& hitInfo = getMouseHitInfo();
        if (shouldPublishNavigationClick(hitInfo))
        {
            const auto& navHitInfo = getNavigationHitInfo();
            onNavigationClick.Publish(navHitInfo.collidedEntityId, navHitInfo.collisionLayer);
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

    void Cursor::SetNavigationRangeProvider(std::function<bool(Vector3)> provider)
    {
        navigationRangeProvider = std::move(provider);
    }

    void Cursor::SetNavigationValidityProvider(std::function<bool(Vector3)> provider)
    {
        navigationValidityProvider = std::move(provider);
    }

    bool Cursor::OutOfRange() const
    {
        if (!navigationRangeProvider) return false;
        if (!getNavigationHitInfo().rlCollision.hit) return false;
        return !navigationRangeProvider(getFirstNaviCollision().point);
    }

    void Cursor::changeCursors(const CollisionInfo& hitInfo)
    {
        if (contextLocked) return;

        const auto* navigationSurface = registry->try_get<NavigationSurface>(hitInfo.collidedEntityId);
        const auto* cursorTarget = registry->try_get<CursorTarget>(hitInfo.collidedEntityId);
        const bool navigationHit = navigationSurface != nullptr && navigationSurface->active;
        const bool invalidNavigation =
            navigationHit && navigationValidityProvider && !navigationValidityProvider(getFirstNaviCollision().point);
        const bool deniedTarget = cursorTarget != nullptr && cursorTarget->deniesNavigation;
        if (OutOfRange() || invalidNavigation || deniedTarget)
        {
            currentTex = ResourceManager::GetInstance().TextureLoad(std::string{cursors::Denied});
            currentColor = invalidColor;
            return;
        }
        if (cursorTarget != nullptr)
        {
            currentTex = ResourceManager::GetInstance().TextureLoad(cursorTarget->cursor);
        }
        else if (navigationHit)
        {
            currentTex = ResourceManager::GetInstance().TextureLoad(std::string{cursors::Move});
        }
        else
        {
            currentTex = ResourceManager::GetInstance().TextureLoad(std::string{cursors::Regular});
        }
    }

    const CollisionInfo& Cursor::getMouseHitInfo() const
    {
        return sys->picker->GetMouseHitInfo();
    }

    const CollisionInfo& Cursor::getNavigationHitInfo() const
    {
        return sys->picker->GetNavigationHitInfo();
    }

    const RayCollision& Cursor::getFirstNaviCollision() const
    {
        return sys->picker->GetFirstNavigationCollision();
    }

    const RayCollision& Cursor::getFirstCollision() const
    {
        return sys->picker->GetFirstCollision();
    }

    void Cursor::Update()
    {
        sys->picker->Update();
        currentTex = ResourceManager::GetInstance().TextureLoad(std::string{cursors::Regular});
        currentColor = defaultColor;

        const auto& hitInfo = getMouseHitInfo();
        if (hitInfo.rlCollision.hit)
        {
            onCollisionHit.Publish(hitInfo.collidedEntityId, hitInfo.collisionLayer);
            changeCursors(hitInfo);
        }

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

    bool Cursor::shouldPublishNavigationClick(const CollisionInfo& mouseHitInfo) const
    {
        const auto& navHitInfo = getNavigationHitInfo();
        const auto* surface = registry->try_get<NavigationSurface>(navHitInfo.collidedEntityId);
        if (!navHitInfo.rlCollision.hit || surface == nullptr || !surface->active) return false;
        if (mouseHitInfo.collidedEntityId == navHitInfo.collidedEntityId) return true;

        const auto* target = registry->try_get<CursorTarget>(mouseHitInfo.collidedEntityId);
        if (target == nullptr) return true;
        return target->allowNavigationClickThrough && !target->deniesNavigation;
    }

    void Cursor::DrawDebug() const
    {
        if (contextLocked) return;
        const auto& mouseHitInfo = getMouseHitInfo();
        if (!mouseHitInfo.rlCollision.hit) return;
        DrawCube(mouseHitInfo.rlCollision.point, 0.5f, 0.5f, 0.5f, currentColor);
        Vector3 normalEnd;
        normalEnd.x = mouseHitInfo.rlCollision.point.x + mouseHitInfo.rlCollision.normal.x;
        normalEnd.y = mouseHitInfo.rlCollision.point.y + mouseHitInfo.rlCollision.normal.y;
        normalEnd.z = mouseHitInfo.rlCollision.point.z + mouseHitInfo.rlCollision.normal.z;
        DrawLine3D(mouseHitInfo.rlCollision.point, normalEnd, RED);
    }

    void Cursor::Draw3D()
    {
    }

    void Cursor::Draw2D() const
    {
        if (hideCursor) return;
        if (currentTex.id == 0) return;
        Vector2 pos = sys->settings->ScreenToViewportPosition(GetMousePosition());
        // TODO: Awful hack below
        if (currentTex.id !=
            ResourceManager::GetInstance().TextureLoad(std::string{cursors::Regular}).id)
        {
            pos = Vector2Subtract(
                pos, {static_cast<float>(currentTex.width / 2), static_cast<float>(currentTex.height / 2)});
        }
        DrawTextureEx(currentTex, pos, 0.0, 1.0f, WHITE);
    }

    Cursor::Cursor(entt::registry* _registry, EngineSystems* _sys) : registry(_registry), sys(_sys)
    {
        EnableContextSwitching();
    }
} // namespace sage

//
// Created by Steve Wheeler on 04/05/2024.
//

#pragma once

#include "Event.hpp"
#include "systems/CollisionSystem.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace sage
{
    class EngineSystems;

    struct HoverInfo
    {
        entt::entity target = entt::null;
        double beginHoverTime = 0.0;
        const float hoverTimeThreshold = 0.75f;
    };

    class Cursor
    {
        float leftClickTimer = 0;
        entt::registry* registry;
        EngineSystems* sys;

        std::optional<HoverInfo> m_hoverInfo{};

        Texture2D currentTex{};
        std::unordered_map<CollisionLayer, std::string> cursorTextureMap{};
        CollisionMask cursorHoverMask{};
        std::function<bool(Vector3)> navigationRangeProvider{};
        std::function<bool(Vector3)> navigationValidityProvider{};

        Color defaultColor = WHITE;
        Color hoverColor = LIME;
        Color invalidColor = RED;
        Color currentColor = WHITE;

        bool contextLocked = false;
        bool hideCursor = false;
        bool enabled = true;

        void checkMouseHover();
        void onMouseHover() const;
        void onMouseLeftClick() const;
        void onMouseRightClick() const;
        void onMouseLeftDown();
        void onMouseRightDown() const;
        void changeCursors(CollisionLayer collisionLayer);

      public:
        std::string hitObjectName{};
        [[nodiscard]] const CollisionInfo& getMouseHitInfo() const;
        [[nodiscard]] const RayCollision& getFirstNaviCollision() const;
        [[nodiscard]] const RayCollision& getFirstCollision() const;

        Event<entt::entity> onCollisionHit{};
        Event<entt::entity> onNavigationClick{};
        Event<entt::entity> onLeftClick{};
        Event<entt::entity> onRightClick{};
        Event<entt::entity> onHover{};
        Event<> onStopHover{};

        void Update();
        void DrawDebug() const;
        void Draw3D();
        void Draw2D() const;
        void DisableContextSwitching();
        void EnableContextSwitching();
        void Enable();
        void Disable();
        void Hide();
        void Show();
        void SetCursorTexture(CollisionLayer layer, std::string textureKey);
        void SetCursorHoverMask(CollisionMask mask);
        void SetNavigationRangeProvider(std::function<bool(Vector3)> provider);
        void SetNavigationValidityProvider(std::function<bool(Vector3)> provider);
        [[nodiscard]] bool OutOfRange() const;

        Cursor(entt::registry* registry, EngineSystems* _sys);
    };
} // namespace sage

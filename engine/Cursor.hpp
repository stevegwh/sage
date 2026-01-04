//
// Created by Steve Wheeler on 04/05/2024.
//

#pragma once

#include "engine/Event.hpp"
#include "systems/CollisionSystem.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <optional>

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
        // Need to know if object is clickable, hoverable, etc.
        float leftClickTimer = 0;
        entt::registry* registry;
        EngineSystems* sys;

        entt::entity selectedActor = entt::null;

        CollisionInfo m_mouseHitInfo{};
        CollisionInfo m_naviHitInfo{};
        std::optional<HoverInfo> m_hoverInfo{};

        Texture2D currentTex{};

        Ray ray{};
        Color defaultColor = WHITE;
        Color hoverColor = LIME;
        Color invalidColor = RED;
        Color currentColor = WHITE;

        bool contextLocked = false;
        bool hideCursor = false;
        bool enabled = true;

        void getMouseRayCollision();
        void checkMouseHover();
        void onMouseHover() const;
        void onMouseLeftClick() const;
        void onMouseRightClick() const;
        void onMouseLeftDown();
        void onMouseRightDown() const;
        void changeCursors(CollisionLayer collisionLayer);
        static void resetHitInfo(CollisionInfo& hitInfo);
        [[nodiscard]] bool findMeshCollision(sage::CollisionInfo& hitInfo) const;

      public:
        std::string hitObjectName{};
        [[nodiscard]] const CollisionInfo& getMouseHitInfo() const;
        [[nodiscard]] const RayCollision& getFirstNaviCollision() const;
        [[nodiscard]] const RayCollision& getFirstCollision() const;
        [[nodiscard]] entt::entity GetSelectedActor() const;
        void SetSelectedActor(entt::entity actor);

        Event<entt::entity, entt::entity> onSelectedActorChange{}; // prev, current
        Event<entt::entity> onCollisionHit{};
        Event<entt::entity> onFloorClick{};
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
        [[nodiscard]] bool OutOfRange() const;

        Cursor(entt::registry* registry, EngineSystems* _sys);
    };
} // namespace sage

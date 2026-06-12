//
// GameUIEngine: top-level UI manager. Owns the window list, the tooltip window,
// drag/hover tracking, and orchestrates per-frame Update / Draw2D.
//

#pragma once

#include "../UserInput.hpp"
#include "UIBase.hpp"
#include "UIWindow.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sage
{
    class EngineSystems;
    class UserInput;
    class Cursor;
    struct Settings;

    class GameUIEngine
    {

      protected:
        std::optional<ErrorMessage> errorMessage;
        std::vector<std::unique_ptr<Window>> windows;
        std::unique_ptr<TooltipWindow> tooltipWindow;
        std::optional<CellElement*> draggedObject;
        std::optional<CellElement*> hoveredDraggableCellElement;
        InputSnapshot currentInput;
        mutable std::vector<std::function<void()>> overlayDrawQueue;

        void pruneWindows();
        void processWindows();

        [[nodiscard]] bool mouseInNonObscuredWindowRegion(Window* window, Vector2 mousePos) const;

      public:
        entt::registry* registry;
        UserInput* userInput;
        Cursor* cursor;
        Settings* settings;
        GameUIEngine(entt::registry* _registry, const EngineSystems* _sys);
        virtual ~GameUIEngine() = default;

        void BringClickedWindowToFront(Window* clicked);
        void CreateErrorMessage(const std::string& msg);

        template <typename T = TooltipWindow, typename... Args>
        T* CreateTooltipWindow(Args&&... args)
        {
            auto window = std::make_unique<T>(std::forward<Args>(args)...);
            auto* created = window.get();
            tooltipWindow = std::move(window);
            created->windowUpdateSub = userInput->onWindowUpdate.Subscribe(
                [created](Vector2 prev, Vector2 current) { created->OnWindowUpdate(prev, current); });
            created->InitLayout();
            return created;
        }

        template <typename T = Window, typename... Args>
        T* CreateWindow(Args&&... args)
        {
            auto window = std::make_unique<T>(std::forward<Args>(args)...);
            auto* created = window.get();
            windows.push_back(std::move(window));
            created->windowUpdateSub = userInput->onWindowUpdate.Subscribe(
                [created](Vector2 prev, Vector2 current) { created->OnWindowUpdate(prev, current); });
            created->InitLayout();
            return created;
        }

        template <typename T = WindowDocked, typename... Args>
        T* CreateWindowDocked(Args&&... args)
        {
            auto window = std::make_unique<T>(std::forward<Args>(args)...);
            auto* created = window.get();
            windows.push_back(std::move(window));
            created->windowUpdateSub = userInput->onWindowUpdate.Subscribe(
                [created](Vector2 prev, Vector2 current) { created->OnWindowUpdate(prev, current); });
            created->InitLayout();
            return created;
        }

        [[nodiscard]] static Rectangle GetOverlap(Rectangle rec1, Rectangle rec2);
        [[nodiscard]] bool ObjectBeingDragged() const;
        [[nodiscard]] Window* GetWindowCollision(const Window* toCheck) const;
        [[nodiscard]] CellElement* GetCellUnderCursor() const;
        [[nodiscard]] bool IsMouseOverWindow() const;
        [[nodiscard]] Vector2 ViewportMousePosition() const;
        void DrawDebug2D() const;
        void Draw2D() const;
        void QueueOverlayDraw(std::function<void()> draw) const;
        void Update();

        // The current frame's input snapshot. Captured at the top of Update().
        [[nodiscard]] const InputSnapshot& Input() const { return currentInput; }

        // Drag/hover-lock accessors used by the state machine. Replaces the friend
        // declarations the state classes used to need to mutate the protected fields.
        void SetDraggedObject(CellElement* el) { draggedObject = el; }
        void ClearDraggedObject() { draggedObject.reset(); }
        void ClaimHoveredDraggableLock(CellElement* el) { hoveredDraggableCellElement = el; }
        void ReleaseHoveredDraggableLock() { hoveredDraggableCellElement.reset(); }
        [[nodiscard]] std::optional<CellElement*> GetHoveredDraggableLock() const
        {
            return hoveredDraggableCellElement;
        }
    };
} // namespace sage

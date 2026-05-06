//
// GameUIEngine: top-level UI manager. Owns the window list, the tooltip window,
// drag/hover tracking, and orchestrates per-frame Update / Draw2D.
//

#pragma once

#include "UIBase.hpp"
#include "UIWindow.hpp"

#include "entt/entt.hpp"
#include "raylib.h"

#include <memory>
#include <optional>
#include <string>
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

        void pruneWindows();
        void processWindows();

        [[nodiscard]] bool mouseInNonObscuredWindowRegion(Window* window, Vector2 mousePos) const;
        GameUIEngine(entt::registry* _registry, const EngineSystems* _sys);

      public:
        entt::registry* registry;
        UserInput* userInput;
        Cursor* cursor;
        Settings* settings;
        void BringClickedWindowToFront(Window* clicked);
        void CreateErrorMessage(const std::string& msg);
        TooltipWindow* CreateTooltipWindow(std::unique_ptr<TooltipWindow> _tooltipWindow);
        Window* CreateWindow(std::unique_ptr<Window> _window);
        WindowDocked* CreateWindowDocked(std::unique_ptr<WindowDocked> _windowDocked);

        [[nodiscard]] static Rectangle GetOverlap(Rectangle rec1, Rectangle rec2);
        [[nodiscard]] bool ObjectBeingDragged() const;
        [[nodiscard]] Window* GetWindowCollision(const Window* toCheck) const;
        [[nodiscard]] CellElement* GetCellUnderCursor() const;
        void DrawDebug2D() const;
        void Draw2D() const;
        void Update();

        friend class UIState;
        friend class DragDelayState;
        friend class DragState;
        friend class HoverState;
    };
} // namespace sage

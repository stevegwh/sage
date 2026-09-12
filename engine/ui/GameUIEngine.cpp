#include "GameUIEngine.hpp"

#include "../Cursor.hpp"
#include "../Settings.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace sage
{
    GameUIEngine::GameUIEngine(Settings* settings, Cursor* cursor) : settings(settings), cursor(cursor)
    {
    }

    UITheme& GameUIEngine::Theme()
    {
        return theme;
    }

    Window& GameUIEngine::AddWindow(const Rectangle designBounds)
    {
        auto window = std::make_unique<Window>(designBounds, theme.window);
        Window& result = *window;
        windows.push_back(std::move(window));
        return result;
    }

    GameUIEngine::Hit GameUIEngine::hitTest(const Vector2 point) const
    {
        for (auto window = windows.rbegin(); window != windows.rend(); ++window)
        {
            if ((*window)->hidden || !CheckCollisionPointRec(point, (*window)->bounds)) continue;
            return {window->get(), (*window)->HitTest(point)};
        }
        return {};
    }

    void GameUIEngine::bringToFront(Window* window)
    {
        const auto found = std::ranges::find_if(
            windows, [window](const std::unique_ptr<Window>& candidate) { return candidate.get() == window; });
        if (found == windows.end() || std::next(found) == windows.end()) return;
        auto ownedWindow = std::move(*found);
        windows.erase(found);
        windows.push_back(std::move(ownedWindow));
    }

    void GameUIEngine::Update()
    {
        for (const auto& window : windows)
        {
            if (!window->hidden) window->Layout(*settings);
        }

        if (hovered.window && hovered.window->hidden) hovered = {};
        if (pressed.window && pressed.window->hidden)
        {
            pressed = {};
            draggingWindow = false;
        }

        const Vector2 mouse = settings->ScreenToViewportPosition(GetMousePosition());
        Hit hit = hitTest(mouse);
        hovered = hit;

        cursor->Enable();
        cursor->EnableContextSwitching();
        if (hovered.window)
        {
            cursor->Disable();
            cursor->DisableContextSwitching();
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            pressed = hit;
            pressPosition = mouse;
            draggingWindow = false;
            if (pressed.window)
            {
                const Rectangle designBounds = pressed.window->designBounds;
                pressedWindowPosition = {designBounds.x, designBounds.y};
                bringToFront(pressed.window);
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && pressed.cell && pressed.cell->dragsWindow)
        {
            const Vector2 delta = Vector2Subtract(mouse, pressPosition);
            if (std::abs(delta.x) > 1 || std::abs(delta.y) > 1) draggingWindow = true;

            const Vector2 viewport = settings->GetViewPort();
            pressed.window->MoveTo({
                pressedWindowPosition.x + delta.x * Settings::TARGET_SCREEN_WIDTH / std::max(1.0f, viewport.x),
                pressedWindowPosition.y + delta.y * Settings::TARGET_SCREEN_HEIGHT / std::max(1.0f, viewport.y)});
            pressed.window->Layout(*settings);

            hit = hitTest(mouse);
            hovered = hit;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            std::function<void()> click;
            if (!draggingWindow && pressed.window == hit.window && pressed.cell == hit.cell && pressed.cell)
                click = pressed.cell->click;

            pressed = {};
            draggingWindow = false;

            if (click) click();
        }
    }

    void GameUIEngine::Draw2D() const
    {
        for (const auto& window : windows)
        {
            if (!window->hidden)
                window->Draw(hovered.cell, pressed.cell, settings->GetCurrentScaleFactor());
        }
    }

    void GameUIEngine::DrawDebug2D() const
    {
        for (const auto& window : windows)
        {
            if (!window->hidden) window->DrawDebug();
        }
    }

} // namespace sage

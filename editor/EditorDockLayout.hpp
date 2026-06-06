#pragma once

#include "engine/Settings.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace sage::editor
{
    inline constexpr float EDITOR_SCENE_ASPECT_WIDTH = 16.0f;
    inline constexpr float EDITOR_SCENE_ASPECT_HEIGHT = 9.0f;
    inline constexpr float EDITOR_SCENE_VIEW_PADDING = 18.0f;
    inline constexpr float EDITOR_LEFT_DOCK_DEFAULT_WIDTH = 340.0f;
    inline constexpr float EDITOR_RIGHT_DOCK_DEFAULT_WIDTH = 440.0f;
    inline constexpr float EDITOR_ASSET_DRAWER_DEFAULT_HEIGHT = 344.0f;
    inline constexpr float EDITOR_LEFT_DOCK_MIN_WIDTH = 220.0f;
    inline constexpr float EDITOR_LEFT_DOCK_MAX_WIDTH = 620.0f;
    inline constexpr float EDITOR_RIGHT_DOCK_MIN_WIDTH = 300.0f;
    inline constexpr float EDITOR_RIGHT_DOCK_MAX_WIDTH = 720.0f;
    inline constexpr float EDITOR_ASSET_DRAWER_MIN_HEIGHT = 180.0f;
    inline constexpr float EDITOR_ASSET_DRAWER_MAX_HEIGHT = 620.0f;
    inline constexpr float EDITOR_SCENE_MIN_WIDTH = 640.0f;
    inline constexpr float EDITOR_SCENE_MIN_HEIGHT = 360.0f;

    struct EditorDockLayout
    {
        float leftDockWidth = EDITOR_LEFT_DOCK_DEFAULT_WIDTH;
        float rightDockWidth = EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        float assetDrawerHeight = EDITOR_ASSET_DRAWER_DEFAULT_HEIGHT;
    };

    inline float MaxLeftDockWidth(const EditorDockLayout& layout)
    {
        return std::max(
            EDITOR_LEFT_DOCK_MIN_WIDTH,
            Settings::TARGET_SCREEN_WIDTH - layout.rightDockWidth - EDITOR_SCENE_MIN_WIDTH -
                EDITOR_SCENE_VIEW_PADDING * 2.0f);
    }

    inline float MaxRightDockWidth(const EditorDockLayout& layout)
    {
        return std::max(
            EDITOR_RIGHT_DOCK_MIN_WIDTH,
            Settings::TARGET_SCREEN_WIDTH - layout.leftDockWidth - EDITOR_SCENE_MIN_WIDTH -
                EDITOR_SCENE_VIEW_PADDING * 2.0f);
    }

    inline float MaxAssetDrawerHeight()
    {
        return std::max(
            EDITOR_ASSET_DRAWER_MIN_HEIGHT,
            Settings::TARGET_SCREEN_HEIGHT - EDITOR_SCENE_MIN_HEIGHT - EDITOR_SCENE_VIEW_PADDING * 3.0f);
    }

    inline bool SetLeftDockWidth(EditorDockLayout& layout, const float width)
    {
        const float clamped = std::clamp(
            width,
            EDITOR_LEFT_DOCK_MIN_WIDTH,
            std::min(EDITOR_LEFT_DOCK_MAX_WIDTH, MaxLeftDockWidth(layout)));
        if (std::fabs(layout.leftDockWidth - clamped) < 0.01f) return false;
        layout.leftDockWidth = clamped;
        return true;
    }

    inline bool SetRightDockWidth(EditorDockLayout& layout, const float width)
    {
        const float clamped = std::clamp(
            width, EDITOR_RIGHT_DOCK_MIN_WIDTH, std::min(EDITOR_RIGHT_DOCK_MAX_WIDTH, MaxRightDockWidth(layout)));
        if (std::fabs(layout.rightDockWidth - clamped) < 0.01f) return false;
        layout.rightDockWidth = clamped;
        return true;
    }

    inline bool SetAssetDrawerHeight(EditorDockLayout& layout, const float height)
    {
        const float clamped = std::clamp(
            height,
            EDITOR_ASSET_DRAWER_MIN_HEIGHT,
            std::min(EDITOR_ASSET_DRAWER_MAX_HEIGHT, MaxAssetDrawerHeight()));
        if (std::fabs(layout.assetDrawerHeight - clamped) < 0.01f) return false;
        layout.assetDrawerHeight = clamped;
        return true;
    }

    inline void ClampEditorDockLayout(EditorDockLayout& layout)
    {
        SetRightDockWidth(layout, layout.rightDockWidth);
        SetLeftDockWidth(layout, layout.leftDockWidth);
        SetRightDockWidth(layout, layout.rightDockWidth);
        SetAssetDrawerHeight(layout, layout.assetDrawerHeight);
    }

    inline Rectangle CalculateEditor16By9Rect(const float x, const float y, const float width, const float height)
    {
        const float maxScale =
            std::min(width / EDITOR_SCENE_ASPECT_WIDTH, height / EDITOR_SCENE_ASPECT_HEIGHT);
        const float scale = std::max(1.0f, std::floor(maxScale));
        const float viewportWidth = EDITOR_SCENE_ASPECT_WIDTH * scale;
        const float viewportHeight = EDITOR_SCENE_ASPECT_HEIGHT * scale;
        return {
            x + (width - viewportWidth) * 0.5f,
            y + (height - viewportHeight) * 0.5f,
            viewportWidth,
            viewportHeight};
    }

    inline Rectangle CalculateEditorSceneViewport(
        const Settings& settings, const EditorDockLayout& layout, const bool fullscreen)
    {
        const auto appViewport = settings.GetViewPort();
        if (fullscreen)
        {
            return CalculateEditor16By9Rect(0.0f, 0.0f, appViewport.x, appViewport.y);
        }

        const float left = settings.ScaleValueWidth(layout.leftDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float right = settings.ScaleValueWidth(layout.rightDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float bottom =
            settings.ScaleValueHeight(layout.assetDrawerHeight + EDITOR_SCENE_VIEW_PADDING * 2.0f);
        const float top = settings.ScaleValueHeight(EDITOR_SCENE_VIEW_PADDING);
        const float availableWidth = std::max(1.0f, appViewport.x - left - right);
        const float availableHeight = std::max(1.0f, appViewport.y - top - bottom);

        return CalculateEditor16By9Rect(left, top, availableWidth, availableHeight);
    }
} // namespace sage::editor

//
// Created by Steve Wheeler on 06/05/2024.
//

#pragma once

#include "Serializer.hpp"

#include "cereal/cereal.hpp"
#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace sage
{
    struct LightSettings
    {
        Vector4 ambient{0.6f, 0.2f, 0.8f, 1.0f};
        float gamma = 1.9f;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("ambient_red", ambient.x),
                cereal::make_nvp("ambient_green", ambient.y),
                cereal::make_nvp("ambient_blue", ambient.z),
                cereal::make_nvp("ambient_alpha", ambient.w),
                cereal::make_nvp("gamma", gamma));
        }
    };

    struct GraphicsSettings
    {
        bool shadows = true;
        bool bloom = true;
        float bloomStrength = 0.65f;
        bool ambientOcclusion = true;
        float occlusionRadius = 1.2f;
        float occlusionStrength = 0.7f;
        bool fxaa = true;
        bool colorGrading = true;
        float saturation = 1.04f;
        float contrast = 1.04f;
        bool depthOfField = false;
        float focusDistance = 12.0f;
        float focusRange = 5.0f;
        float maxBlurRadius = 6.0f;

        template <class Archive>
        void save(Archive& archive) const
        {
            archive(cereal::make_nvp("shadows", shadows),
                    cereal::make_nvp("bloom", bloom),
                    cereal::make_nvp("bloom_strength", bloomStrength),
                    cereal::make_nvp("ambient_occlusion", ambientOcclusion),
                    cereal::make_nvp("occlusion_radius", occlusionRadius),
                    cereal::make_nvp("occlusion_strength", occlusionStrength),
                    cereal::make_nvp("fxaa", fxaa),
                    cereal::make_nvp("color_grading", colorGrading),
                    cereal::make_nvp("saturation", saturation),
                    cereal::make_nvp("contrast", contrast),
                    cereal::make_nvp("depth_of_field", depthOfField),
                    cereal::make_nvp("focus_distance", focusDistance),
                    cereal::make_nvp("focus_range", focusRange),
                    cereal::make_nvp("max_blur_radius", maxBlurRadius));
        }

        template <class Archive>
        void load(Archive& archive)
        {
            archive(cereal::make_nvp("shadows", shadows),
                    cereal::make_nvp("bloom", bloom),
                    cereal::make_nvp("bloom_strength", bloomStrength),
                    cereal::make_nvp("ambient_occlusion", ambientOcclusion),
                    cereal::make_nvp("occlusion_radius", occlusionRadius),
                    cereal::make_nvp("occlusion_strength", occlusionStrength),
                    cereal::make_nvp("fxaa", fxaa),
                    cereal::make_nvp("color_grading", colorGrading),
                    cereal::make_nvp("saturation", saturation),
                    cereal::make_nvp("contrast", contrast));
            // Existing project settings have no depth-of-field fields.
            try
            {
                archive(cereal::make_nvp("depth_of_field", depthOfField),
                        cereal::make_nvp("focus_distance", focusDistance),
                        cereal::make_nvp("focus_range", focusRange),
                        cereal::make_nvp("max_blur_radius", maxBlurRadius));
            }
            catch (const cereal::Exception&)
            {
                depthOfField = false;
                focusDistance = 12.0f;
                focusRange = 5.0f;
                maxBlurRadius = 6.0f;
            }
        }
    };

    struct ProjectSettings
    {
        LightSettings lightSettings{};
        GraphicsSettings graphicsSettings{};

        template <class Archive>
        void save(Archive& archive) const
        {
            archive(cereal::make_nvp("light_settings", lightSettings),
                    cereal::make_nvp("graphics_settings", graphicsSettings));
        }

        template <class Archive>
        void load(Archive& archive)
        {
            archive(cereal::make_nvp("light_settings", lightSettings));
            // Older projects contain only light_settings.
            try
            {
                archive(cereal::make_nvp("graphics_settings", graphicsSettings));
            }
            catch (const cereal::Exception&)
            {
                graphicsSettings = {};
            }
        }
    };

    struct Settings
    {
      private:
        bool* exitProgram;

        // Current settings
        int screenWidth = 1280;
        int screenHeight = 720;
        int viewportWidth = 1920;
        int viewportHeight = 1080;
        int renderViewportWidth = 1920;
        int renderViewportHeight = 1080;
        int renderViewportOffsetX = 0;
        int renderViewportOffsetY = 0;
        bool preserveAspectRatio = true;

        // Play-in-editor: the game's viewport is a sub-rectangle of the window
        // (the editor's docked scene view), so its offset isn't centred. When
        // set, this overrides the centred GetViewportOffset() calculation.
        bool useViewportOffsetOverride = false;
        Vector2 viewportOffsetOverride{};

        // Serialized local-user and project settings.
        int screenWidthUser{};
        int screenHeightUser{};
        ProjectSettings projectSettings{};

        // Hardcoded defaults
        static constexpr int SCREEN_WIDTH = 1920;
        static constexpr int SCREEN_HEIGHT = 1080;

      public:
        static constexpr const char* USER_SETTINGS_PATH = "resources/settings.json";
        static constexpr const char* PROJECT_SETTINGS_PATH = "resources/project-settings.json";
        static constexpr float TARGET_SCREEN_WIDTH = 1920.0f;
        static constexpr float TARGET_SCREEN_HEIGHT = 1080.0f;

        bool toggleFullScreenRequested = false;

        void ExitProgram()
        {
            *exitProgram = true;
        }

        void SetScreenSize(int w, int h)
        {
            screenWidth = w;
            screenHeight = h;
            UpdateViewport();
        }

        void SetPreserveAspectRatio(const bool preserve)
        {
            preserveAspectRatio = preserve;
            UpdateViewport();
        }

        [[nodiscard]] Vector2 GetScreenSize() const
        {
            return {static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
        }

        [[nodiscard]] Vector2 GetViewPort() const
        {
            return {static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)};
        }

        [[nodiscard]] Vector2 GetRenderViewPort() const
        {
            return {static_cast<float>(renderViewportWidth), static_cast<float>(renderViewportHeight)};
        }

        void UpdateViewport()
        {
            if (!preserveAspectRatio)
            {
                viewportWidth = screenWidth;
                viewportHeight = screenHeight;
                ResetRenderViewportToAppViewport();
                return;
            }

            float aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
            static constexpr float targetAspectRatio = 16.0f / 9.0f;

            // Calculate viewport dimensions while maintaining aspect ratio
            if (aspectRatio > targetAspectRatio)
            {
                // Screen is wider than target ratio - fit to height
                viewportHeight = screenHeight;
                viewportWidth = static_cast<int>(screenHeight * targetAspectRatio);
            }
            else
            {
                // Screen is taller than target ratio - fit to width
                viewportWidth = screenWidth;
                viewportHeight = static_cast<int>(screenWidth / targetAspectRatio);
            }
            ResetRenderViewportToAppViewport();
        }

        [[nodiscard]] Vector2 GetViewportOffset() const
        {
            if (useViewportOffsetOverride) return viewportOffsetOverride;
            return {
                std::floor((static_cast<float>(screenWidth) - static_cast<float>(viewportWidth)) * 0.5f),
                std::floor((static_cast<float>(screenHeight) - static_cast<float>(viewportHeight)) * 0.5f)};
        }

        // Pins the viewport to an explicit screen rectangle (used by play-in-editor
        // so the game's UI scales to, centres in, and hit-tests against the
        // editor's docked scene view rather than the whole window). The render
        // viewport is collapsed onto this viewport (no further offset).
        void SetPlayViewport(const Rectangle screenRect)
        {
            viewportWidth = std::max(1, static_cast<int>(screenRect.width));
            viewportHeight = std::max(1, static_cast<int>(screenRect.height));
            useViewportOffsetOverride = true;
            viewportOffsetOverride = {screenRect.x, screenRect.y};
            ResetRenderViewportToAppViewport();
        }

        [[nodiscard]] Rectangle GetViewportScreenRect() const
        {
            const auto viewportOffset = GetViewportOffset();
            return {
                viewportOffset.x,
                viewportOffset.y,
                static_cast<float>(viewportWidth),
                static_cast<float>(viewportHeight)};
        }

        [[nodiscard]] bool IsPointInViewport(const Vector2 point) const
        {
            return CheckCollisionPointRec(point, GetViewportScreenRect());
        }

        [[nodiscard]] Vector2 ScreenToViewportPosition(const Vector2 point) const
        {
            const auto viewportOffset = GetViewportOffset();
            return {point.x - viewportOffset.x, point.y - viewportOffset.y};
        }

        [[nodiscard]] Vector2 ViewportToScreenPosition(const Vector2 point) const
        {
            const auto viewportOffset = GetViewportOffset();
            return {point.x + viewportOffset.x, point.y + viewportOffset.y};
        }

        [[nodiscard]] Vector2 GetRenderViewportOffset() const
        {
            return {static_cast<float>(renderViewportOffsetX), static_cast<float>(renderViewportOffsetY)};
        }

        [[nodiscard]] Rectangle GetRenderViewportRect() const
        {
            return {
                static_cast<float>(renderViewportOffsetX),
                static_cast<float>(renderViewportOffsetY),
                static_cast<float>(renderViewportWidth),
                static_cast<float>(renderViewportHeight)};
        }

        [[nodiscard]] Rectangle GetRenderViewportScreenRect() const
        {
            const auto viewportOffset = GetViewportOffset();
            return {
                viewportOffset.x + static_cast<float>(renderViewportOffsetX),
                viewportOffset.y + static_cast<float>(renderViewportOffsetY),
                static_cast<float>(renderViewportWidth),
                static_cast<float>(renderViewportHeight)};
        }

        [[nodiscard]] bool IsPointInRenderViewport(const Vector2 point) const
        {
            return CheckCollisionPointRec(point, GetRenderViewportScreenRect());
        }

        [[nodiscard]] Vector2 ScreenToRenderViewportPosition(const Vector2 point) const
        {
            const auto viewportOffset = GetViewportOffset();
            return {
                point.x - viewportOffset.x - static_cast<float>(renderViewportOffsetX),
                point.y - viewportOffset.y - static_cast<float>(renderViewportOffsetY)};
        }

        void SetRenderViewport(const int width, const int height, const Vector2 offset)
        {
            renderViewportWidth = std::max(1, width);
            renderViewportHeight = std::max(1, height);
            renderViewportOffsetX = static_cast<int>(offset.x);
            renderViewportOffsetY = static_cast<int>(offset.y);
        }

        void ResetRenderViewportToAppViewport()
        {
            renderViewportWidth = viewportWidth;
            renderViewportHeight = viewportHeight;
            renderViewportOffsetX = 0;
            renderViewportOffsetY = 0;
        }

        static float GetScreenScaleFactor(float width, float height)
        {

            float scaleX = width / TARGET_SCREEN_WIDTH;
            float scaleY = height / TARGET_SCREEN_HEIGHT;

            // Use the smaller scale factor to maintain aspect ratio
            return std::min(scaleX, scaleY);
        }

        [[nodiscard]] float GetCurrentScaleFactor() const
        {
            return GetScreenScaleFactor(viewportWidth, viewportHeight);
        }

        [[nodiscard]] float ScaleValueMaintainRatio(const float toScale) const
        {
            return toScale * GetCurrentScaleFactor();
        }

        [[nodiscard]] float ScaleValueHeight(const float toScale) const
        {
            float scaleY = viewportHeight / TARGET_SCREEN_HEIGHT;
            return toScale * scaleY;
        }

        [[nodiscard]] float ScaleValueWidth(const float toScale) const
        {
            float scaleX = viewportWidth / TARGET_SCREEN_WIDTH;
            return toScale * scaleX;
        }

        [[nodiscard]] Vector2 ScalePos(Vector2 toScale) const
        {
            return {ScaleValueWidth(toScale.x), ScaleValueHeight(toScale.y)};
        }

        void ResetToUserDefined()
        {
            screenWidth = screenWidthUser;
            screenHeight = screenHeightUser;
            UpdateViewport();
        }

        void ResetToDefaults()
        {
            screenWidthUser = SCREEN_WIDTH;
            screenHeightUser = SCREEN_HEIGHT;
            ResetToUserDefined();
        }

        [[nodiscard]] const LightSettings& GetLightSettings() const
        {
            return projectSettings.lightSettings;
        }

        void SetLightSettings(const LightSettings& value)
        {
            projectSettings.lightSettings = value;
        }

        [[nodiscard]] const GraphicsSettings& GetGraphicsSettings() const
        {
            return projectSettings.graphicsSettings;
        }

        void SetGraphicsSettings(const GraphicsSettings& value)
        {
            projectSettings.graphicsSettings = value;
        }

        [[nodiscard]] bool SaveProjectSettings() const
        {
            return serializer::SaveClassJson(PROJECT_SETTINGS_PATH, projectSettings);
        }

        explicit Settings(bool* _exitProgram) : exitProgram(_exitProgram)
        {
            serializer::DeserializeJsonFile<Settings>(USER_SETTINGS_PATH, *this);
            serializer::DeserializeJsonFile<ProjectSettings>(PROJECT_SETTINGS_PATH, projectSettings);
        }

        template <class Archive>
        void serialize(Archive& archive)
        {
            ResetToDefaults();
            archive(
                cereal::make_nvp("screen_width", screenWidthUser),
                cereal::make_nvp("screen_height", screenHeightUser));
            ResetToUserDefined();
        }
    };
} // namespace sage

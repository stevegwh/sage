#pragma once

#include "EditorCamera.hpp"
#include "engine/ParticleSystem.hpp"
#include "entt/entt.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace sage
{
    struct ParticleEmitterComponent;
}

namespace sage::editor
{
    class EditorHistory;

    class EditorParticleEditor
    {
        static constexpr float DEFAULT_CAMERA_YAW = 0.7f;
        static constexpr float DEFAULT_CAMERA_PITCH = 0.35f;
        static constexpr float DEFAULT_CAMERA_DISTANCE = 8.0f;
        RenderTexture2D preview{};
        struct PreviewEmitter
        {
            entt::entity entity;
            std::unique_ptr<Emitter> emitter;
            bool visible = true;
        };
        std::vector<PreviewEmitter> emitters;
        std::optional<entt::entity> root;
        std::optional<entt::entity> editing;
        bool paused = false;
        bool fixedSeed = true;
        int seed = 1;
        float playbackSpeed = 1.0f;
        float elapsed = 0.0f;
        float accumulator = 0.0f;
        EditorCamera cameraControls;
        Camera3D camera{};
        bool middleCameraDrag = false;
        bool rightCameraDrag = false;
        int background = 0;
        bool ground = true;

        [[nodiscard]] EmitterConfig configFor(const entt::registry& registry, entt::entity entity) const;
        void restart(const entt::registry& registry, bool play);
        void step(const entt::registry& registry);
        void drawViewport(const entt::registry& registry);
        void resetCamera();

      public:
        EditorParticleEditor();
        EditorParticleEditor(const EditorParticleEditor&) = delete;
        EditorParticleEditor& operator=(const EditorParticleEditor&) = delete;
        EditorParticleEditor(EditorParticleEditor&&) = delete;
        EditorParticleEditor& operator=(EditorParticleEditor&&) = delete;
        ~EditorParticleEditor();
        [[nodiscard]] bool DrawPreview(entt::registry& registry, entt::entity effectRoot, Rectangle bounds);
        void DrawInspectorModules(
            entt::registry& registry, std::optional<entt::entity> entity, EditorHistory& history);
    };
} // namespace sage::editor

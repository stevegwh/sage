#include "EditorParticleEditor.hpp"
#include "EditorHistory.hpp"
#include "engine/components/EntityVisibility.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/systems/ParticleEmitterSystem.hpp"

#include "imgui.h"
#include "raymath.h"
#include "rlgl.h"
#include "rlImGui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace sage::editor
{
    namespace
    {
        constexpr float STEP_SECONDS = 1.0f / 60.0f;
        constexpr float GRAPH_HEIGHT = 90.0f;
        constexpr float MIN_GRAPH_WIDTH = 80.0f;
        constexpr float CURVE_ROUNDING = 4.0f;
        constexpr float POINT_RADIUS = 4.0f;
        constexpr float CURVE_LINE_WIDTH = 2.0f;
        constexpr float VALUE_DRAG_SPEED = 0.01f;
        constexpr float GRADIENT_HEIGHT = 24.0f;
        constexpr float COLOR_CHANNEL_MAX = 255.0f;
        constexpr float CONTROL_WIDTH = 100.0f;
        constexpr float SPEED_CONTROL_WIDTH = 130.0f;
        constexpr float MIN_PLAYBACK_SPEED = 0.1f;
        constexpr float MAX_PLAYBACK_SPEED = 4.0f;
        constexpr float MAX_FRAME_SECONDS = 0.1f;
        constexpr float MAX_CURVE_MULTIPLIER = 4.0f;
        constexpr int GROUND_GRID_CELLS = 10;
        constexpr float GROUND_SIZE = 10.0f;
        constexpr float GROUND_OFFSET = -0.01f;
        constexpr float CAMERA_FOV = 45.0f;
        const ImVec4 GRAPH_BACKGROUND{25 / 255.0f, 28 / 255.0f, 34 / 255.0f, 1};
        const ImVec4 GRAPH_GRID{60 / 255.0f, 64 / 255.0f, 72 / 255.0f, 1};
        const ImVec4 GRAPH_LINE{110 / 255.0f, 195 / 255.0f, 240 / 255.0f, 1};
        const ImVec4 GRAPH_POINT{235 / 255.0f, 195 / 255.0f, 90 / 255.0f, 1};
        constexpr Color DARK_BACKGROUND{.r = 25, .g = 28, .b = 35, .a = 255};
        constexpr Color LIGHT_BACKGROUND{.r = 220, .g = 222, .b = 228, .a = 255};
        constexpr Color DARK_GROUND{.r = 45, .g = 48, .b = 55, .a = 255};
        constexpr Color LIGHT_GROUND{.r = 185, .g = 188, .b = 195, .a = 255};
        constexpr Color X_AXIS_COLOR{.r = 230, .g = 80, .b = 80, .a = 255};
        constexpr Color Y_AXIS_COLOR{.r = 80, .g = 230, .b = 80, .a = 255};
        constexpr Vector3 PREVIEW_ORIGIN{.x = 0, .y = 0, .z = 0};

        bool DrawCurve(const char* label, ParticleCurve& curve, float maximum)
        {
            ImGui::PushID(label);
            bool changed = ImGui::Checkbox(label, &curve.enabled);
            if (curve.enabled)
            {
                const auto origin = ImGui::GetCursorScreenPos();
                const ImVec2 size{std::max(MIN_GRAPH_WIDTH, ImGui::GetContentRegionAvail().x), GRAPH_HEIGHT};
                constexpr float CURVE_PADDING = 8.0f;
                const auto intervals = static_cast<float>(curve.values.size() - 1);
                const float width = size.x - 2 * CURVE_PADDING;
                const float height = size.y - 2 * CURVE_PADDING;
                ImGui::InvisibleButton("curve", size);
                const auto mouse = ImGui::GetMousePos();
                if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const auto index = static_cast<std::size_t>(std::clamp(
                        std::round((mouse.x - origin.x - CURVE_PADDING) / width * intervals), 0.0f, intervals));
                    curve.values.at(index) = std::clamp(
                        (origin.y + CURVE_PADDING + height - mouse.y) / height * maximum, 0.0f, maximum);
                    changed = true;
                }
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(
                    origin,
                    {origin.x + size.x, origin.y + size.y},
                    ImGui::ColorConvertFloat4ToU32(GRAPH_BACKGROUND),
                    CURVE_ROUNDING);
                const auto point = [&](std::size_t index) {
                    return ImVec2{
                        origin.x + CURVE_PADDING + width * static_cast<float>(index) / intervals,
                        origin.y + CURVE_PADDING +
                            height * (1 - std::clamp(curve.values.at(index) / maximum, 0.0f, 1.0f))};
                };
                for (std::size_t index = 0; index < curve.values.size(); ++index)
                {
                    const float x = point(index).x;
                    draw->AddLine(
                        {x, origin.y + CURVE_PADDING},
                        {x, origin.y + size.y - CURVE_PADDING},
                        ImGui::ColorConvertFloat4ToU32(GRAPH_GRID));
                    if (index > 0)
                        draw->AddLine(
                            point(index - 1),
                            point(index),
                            ImGui::ColorConvertFloat4ToU32(GRAPH_LINE),
                            CURVE_LINE_WIDTH);
                    draw->AddCircleFilled(point(index), POINT_RADIUS, ImGui::ColorConvertFloat4ToU32(GRAPH_POINT));
                }
                // ImGui exposes disabled text through its native variadic API.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
                ImGui::TextDisabled(
                    "Birth                         Normalized lifetime                         Death");
                for (std::size_t index = 0; index < curve.values.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    if (index > 0) ImGui::SameLine();
                    ImGui::SetNextItemWidth(
                        (size.x - intervals * ImGui::GetStyle().ItemSpacing.x) /
                        static_cast<float>(curve.values.size()));
                    changed |= ImGui::DragFloat(
                        "##value",
                        &curve.values.at(index),
                        VALUE_DRAG_SPEED,
                        0,
                        maximum,
                        "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
            return changed;
        }

        bool DrawGradient(ParticleEmitterComponent& settings)
        {
            auto& gradient = settings.colorOverLifetime;
            bool changed = ImGui::Checkbox("Colour over lifetime", &gradient.enabled);
            if (!gradient.enabled) return changed;
            const auto origin = ImGui::GetCursorScreenPos();
            const float width = std::max(MIN_GRAPH_WIDTH, ImGui::GetContentRegionAvail().x);
            ImGui::Dummy({width, GRADIENT_HEIGHT});
            auto* draw = ImGui::GetWindowDrawList();
            for (int pixel = 0; pixel < static_cast<int>(width); ++pixel)
            {
                const auto color =
                    gradient.Evaluate(static_cast<float>(pixel) / width, settings.startColor, settings.endColor);
                // Show colour independently of alpha; RGBA stop widgets and the preview expose opacity.
                draw->AddLine(
                    {origin.x + static_cast<float>(pixel), origin.y},
                    {origin.x + static_cast<float>(pixel), origin.y + GRADIENT_HEIGHT},
                    ImGui::ColorConvertFloat4ToU32(
                        {static_cast<float>(color.r) / COLOR_CHANNEL_MAX,
                         static_cast<float>(color.g) / COLOR_CHANNEL_MAX,
                         static_cast<float>(color.b) / COLOR_CHANNEL_MAX,
                         1}));
            }
            for (std::size_t index = 0; index < gradient.colors.size(); ++index)
            {
                auto& color = gradient.colors.at(index);
                auto value = ColorNormalize(color);
                std::array<float, 4> rgba{value.x, value.y, value.z, value.w};
                const auto label = std::to_string(index * 100 / (gradient.colors.size() - 1)) + "%";
                if (ImGui::ColorEdit4(label.c_str(), rgba.data()))
                {
                    color =
                        ColorFromNormalized({.x = rgba.at(0), .y = rgba.at(1), .z = rgba.at(2), .w = rgba.at(3)});
                    changed = true;
                }
            }
            return changed;
        }
    } // namespace

    EditorParticleEditor::EditorParticleEditor()
    {
        resetCamera();
    }

    void EditorParticleEditor::resetCamera()
    {
        camera = {
            .position =
                {.x = DEFAULT_CAMERA_DISTANCE * std::cos(DEFAULT_CAMERA_PITCH) * std::sin(DEFAULT_CAMERA_YAW),
                 .y = 1 + DEFAULT_CAMERA_DISTANCE * std::sin(DEFAULT_CAMERA_PITCH),
                 .z = DEFAULT_CAMERA_DISTANCE * std::cos(DEFAULT_CAMERA_PITCH) * std::cos(DEFAULT_CAMERA_YAW)},
            .target = {.x = 0, .y = 1, .z = 0},
            .up = {.x = 0, .y = 1, .z = 0},
            .fovy = CAMERA_FOV,
            .projection = CAMERA_PERSPECTIVE};
        cameraControls.mode = CameraMode::Focused;
        middleCameraDrag = rightCameraDrag = false;
    }

    EditorParticleEditor::~EditorParticleEditor()
    {
        if (preview.id != 0) UnloadRenderTexture(preview);
    }

    EmitterConfig EditorParticleEditor::configFor(const entt::registry& registry, const entt::entity entity) const
    {
        const auto origin = Vector3Subtract(
            registry.get<sgTransform>(entity).GetWorldPos(), registry.get<sgTransform>(*root).GetWorldPos());
        auto config = MakeParticleEmitterConfig(registry.get<ParticleEmitterComponent>(entity), origin);
        if (fixedSeed)
        {
            const auto index =
                std::ranges::find_if(emitters, [entity](const auto& entry) { return entry.entity == entity; }) -
                emitters.begin();
            config.randomSeed = static_cast<std::uint32_t>(seed) + static_cast<std::uint32_t>(index);
        }
        return config;
    }

    void EditorParticleEditor::restart(const entt::registry& registry, const bool play)
    {
        for (auto& entry : emitters)
        {
            entry.emitter = std::make_unique<Emitter>(configFor(registry, entry.entity));
            if (play) entry.emitter->Start();
        }
        paused = !play;
        elapsed = 0;
        accumulator = 0;
    }

    void EditorParticleEditor::step(const entt::registry& registry)
    {
        for (auto& entry : emitters)
        {
            const auto& settings = registry.get<ParticleEmitterComponent>(entry.entity);
            if (!settings.looping && elapsed >= std::max(0.0f, settings.duration)) entry.emitter->Stop();
            entry.emitter->Update(STEP_SECONDS);
        }
        elapsed += STEP_SECONDS;
    }

    bool EditorParticleEditor::DrawPreview(
        entt::registry& registry, const entt::entity effectRoot, const Rectangle bounds)
    {
        if (!registry.valid(effectRoot) || !registry.all_of<sgTransform>(effectRoot)) return false;
        const auto members = GatherParticleEmitters(registry, effectRoot);
        if (members.empty())
        {
            root.reset();
            emitters.clear();
            return false;
        }
        const bool effectChanged =
            root != effectRoot || members.size() != emitters.size() ||
            !std::equal(
                members.begin(), members.end(), emitters.begin(), [](const auto member, const auto& entry) {
                    return member == entry.entity;
                });
        if (effectChanged)
        {
            root = effectRoot;
            emitters.clear();
            for (const auto member : members)
                emitters.push_back({.entity = member});
            restart(registry, true);
        }
        for (auto& entry : emitters)
        {
            entry.emitter->Reinit(configFor(registry, entry.entity));
            entry.visible = IsEntityVisible(registry, entry.entity);
        }
        // Reserve the scene tab bar above the preview.
        const float tabHeight = ImGui::GetFrameHeightWithSpacing() + 2 * ImGui::GetStyle().WindowPadding.y;
        ImGui::SetNextWindowPos({bounds.x, bounds.y + tabHeight}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({bounds.width, std::max(1.0f, bounds.height - tabHeight)}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (!ImGui::Begin("##particleEditorTab", nullptr, flags))
        {
            ImGui::End();
            return true;
        }
        bool keepOpen = !ImGui::Button("Scene View");
        ImGui::SameLine();
        ImGui::TextUnformatted(registry.get<sgTransform>(*root).name.c_str());
        if (ImGui::Button(paused ? "Play" : "Pause"))
        {
            paused = !paused;
            if (!paused)
                for (auto& entry : emitters)
                    entry.emitter->Start();
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart")) restart(registry, true);
        ImGui::SameLine();
        if (ImGui::Button("Stop")) restart(registry, false);
        ImGui::SameLine();
        if (ImGui::Button("Burst"))
            for (auto& entry : emitters)
                entry.emitter->Burst();
        ImGui::SameLine();
        if (ImGui::Button("Restart burst"))
        {
            restart(registry, false);
            for (auto& entry : emitters)
                entry.emitter->Burst();
        }
        ImGui::SameLine();
        if (ImGui::Button("Step 1/60 s"))
        {
            paused = true;
            step(registry);
        }
        ImGui::SetNextItemWidth(SPEED_CONTROL_WIDTH);
        ImGui::SliderFloat("Playback speed", &playbackSpeed, MIN_PLAYBACK_SPEED, MAX_PLAYBACK_SPEED, "%.2fx");
        bool reset = ImGui::Checkbox("Fixed random seed", &fixedSeed);
        if (fixedSeed)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(CONTROL_WIDTH);
            reset |= ImGui::InputInt("Seed", &seed);
        }
        if (reset) restart(registry, !paused);
        if (!paused)
        {
            accumulator += std::min(GetFrameTime(), MAX_FRAME_SECONDS) * playbackSpeed;
            while (accumulator >= STEP_SECONDS)
            {
                step(registry);
                accumulator -= STEP_SECONDS;
            }
        }
        std::size_t alive = 0;
        std::size_t capacity = 0;
        for (const auto& entry : emitters)
        {
            alive += static_cast<std::size_t>(std::ranges::count_if(
                entry.emitter->particles, [](const auto& particle) { return particle->active; }));
            capacity += entry.emitter->config.capacity;
        }
        ImGui::TextUnformatted(
            std::format(
                "Time: {:.2f} s   Alive: {} / {}   Emitters: {}", elapsed, alive, capacity, emitters.size())
                .c_str());
        ImGui::SetNextItemWidth(CONTROL_WIDTH);
        ImGui::Combo("Background", &background, "Dark\0Light\0");
        ImGui::SameLine();
        ImGui::Checkbox("Ground plane", &ground);
        ImGui::SameLine();
        if (ImGui::Button("Reset camera")) resetCamera();
        drawViewport(registry);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        ImGui::TextDisabled("Middle/right-drag or Q/E to orbit; W/S to raise/lower; wheel to zoom.");
        ImGui::End();
        return keepOpen;
    }

    void EditorParticleEditor::DrawInspectorModules(
        entt::registry& registry, const std::optional<entt::entity> entity, EditorHistory& history)
    {
        if (editing && (editing != entity || !ImGui::IsAnyItemActive()))
        {
            if (history.HasActiveTransaction()) history.Commit();
            editing.reset();
        }
        if (!entity || !registry.valid(*entity) || !registry.all_of<ParticleEmitterComponent>(*entity)) return;
        auto& settings = registry.get<ParticleEmitterComponent>(*entity);
        auto edited = settings;
        bool changed = false;
        if (ImGui::CollapsingHeader("Size over Lifetime"))
            changed |= DrawCurve("Enabled##size", edited.sizeOverLifetime, MAX_CURVE_MULTIPLIER);
        if (ImGui::CollapsingHeader("Opacity over Lifetime"))
            changed |= DrawCurve("Enabled##opacity", edited.opacityOverLifetime, 1);
        if (ImGui::CollapsingHeader("Speed over Lifetime"))
            changed |= DrawCurve("Enabled##speed", edited.speedOverLifetime, MAX_CURVE_MULTIPLIER);
        if (ImGui::CollapsingHeader("Colour over Lifetime")) changed |= DrawGradient(edited);
        if (changed)
        {
            if (!editing && !history.HasActiveTransaction())
            {
                history.Begin(EditAction::EditField, {*entity});
                editing = entity;
            }
            if (editing)
            {
                settings = edited;
                if (!ImGui::IsAnyItemActive())
                {
                    history.Commit();
                    editing.reset();
                }
            }
        }
    }

    void EditorParticleEditor::drawViewport(const entt::registry& registry)
    {
        const auto available = ImGui::GetContentRegionAvail();
        const int width = std::max(1, static_cast<int>(available.x));
        const int height = std::max(1, static_cast<int>(available.y - ImGui::GetTextLineHeightWithSpacing()));
        if (preview.texture.width != width || preview.texture.height != height)
        {
            if (preview.id != 0) UnloadRenderTexture(preview);
            preview = LoadRenderTexture(width, height);
            SetTextureFilter(preview.texture, TEXTURE_FILTER_BILINEAR);
        }
        rlDrawRenderBatchActive();
        BeginTextureMode(preview);
        ClearBackground(background == 0 ? DARK_BACKGROUND : LIGHT_BACKGROUND);
        BeginMode3D(camera);
        if (ground)
        {
            DrawPlane(
                {.x = 0, .y = GROUND_OFFSET, .z = 0},
                {.x = GROUND_SIZE, .y = GROUND_SIZE},
                background == 0 ? DARK_GROUND : LIGHT_GROUND);
            DrawGrid(GROUND_GRID_CELLS, 1);
        }
        DrawLine3D(PREVIEW_ORIGIN, {.x = 1, .y = 0, .z = 0}, X_AXIS_COLOR);
        DrawLine3D(PREVIEW_ORIGIN, {.x = 0, .y = 1, .z = 0}, Y_AXIS_COLOR);
        for (const auto& entry : emitters)
            if (IsEntityVisible(registry, entry.entity) && entry.emitter->config.texture.id != 0)
                entry.emitter->Draw(&camera);
        EndMode3D();
        EndTextureMode();
        rlImGuiImageRenderTexture(&preview);
        const bool hovered = ImGui::IsItemHovered();
        const auto& io = ImGui::GetIO();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) middleCameraDrag = true;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rightCameraDrag = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) middleCameraDrag = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) rightCameraDrag = false;
        if (io.WantTextInput || ImGui::IsAnyItemActive() || !ImGui::IsWindowFocused())
        {
            middleCameraDrag = rightCameraDrag = false;
            return;
        }
        if (middleCameraDrag || rightCameraDrag)
            cameraControls.Look(camera, {.x = io.MouseDelta.x, .y = io.MouseDelta.y});
        if (!io.KeySuper && !io.KeyAlt && (hovered || middleCameraDrag || rightCameraDrag))
        {
            cameraControls.RotateFocused(
                camera,
                {.x = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_E) - ImGui::IsKeyDown(ImGuiKey_Q)),
                 .y = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_S) - ImGui::IsKeyDown(ImGuiKey_W))},
                GetFrameTime());
            if (hovered) cameraControls.Zoom(camera, io.MouseWheel);
        }
    }
} // namespace sage::editor

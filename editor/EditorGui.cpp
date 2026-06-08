#include "EditorGui.hpp"

#include "engine/components/UberShaderComponent.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"

#include "raymath.h"
#include "rlImGui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sage::editor
{
    namespace
    {
        constexpr int THUMBNAIL_SIZE = 128;
        constexpr float ASSET_TILE_WIDTH = 152.0f;
        constexpr float ASSET_TILE_HEIGHT = 188.0f;
        constexpr float FLATPACK_TILE_HEIGHT = 116.0f;
        constexpr float ASSET_DEFAULTS_PANEL_WIDTH = 260.0f;
        constexpr float DOCK_RESIZE_HANDLE_THICKNESS = 8.0f;
        constexpr int PREVIEW_LIGHT_DIRECTIONAL = 0;
        constexpr int PREVIEW_LIGHT_POINT = 1;
        constexpr float PREVIEW_GAMMA = 1.9f;
        constexpr Color EDITOR_WINDOW_BACKGROUND = {35, 38, 43, 245};
        constexpr Color EDITOR_TEXT = {230, 234, 240, 255};
        constexpr Color PREVIEW_LIGHT_COLOR = {255, 244, 214, 255};
        constexpr const char* HIERARCHY_DRAG_PAYLOAD = "SAGE_HIER_ENTITY";
        constexpr const char* ASSET_RENAME_POPUP = "Rename Asset File";

        ImVec4 ToImGuiColor(const Color color)
        {
            return {
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                static_cast<float>(color.a) / 255.0f};
        }

        // Draws a search field with a placeholder hint plus a trailing "x" button that
        // clears the bound filter. idPrefix must be unique per call site.
        void DrawSearchFilter(ImGuiTextFilter& filter, const char* idPrefix, const char* hint, const float width)
        {
            ImGui::PushID(idPrefix);
            const float buttonWidth = ImGui::GetFrameHeight();
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(std::max(1.0f, width - buttonWidth - spacing));
            if (ImGui::InputTextWithHint("##filter_input", hint, filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
            {
                filter.Build();
            }
            ImGui::SameLine(0.0f, spacing);
            ImGui::BeginDisabled(!filter.IsActive());
            if (ImGui::Button("x##filter_clear", ImVec2{buttonWidth, buttonWidth}))
            {
                filter.Clear();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        std::uint32_t EntityPayloadId(const entt::entity entity)
        {
            return static_cast<std::uint32_t>(entt::to_integral(entity));
        }

        entt::entity EntityFromPayloadId(const std::uint32_t id)
        {
            return static_cast<entt::entity>(id);
        }

        Shader LoadThumbnailShader()
        {
            auto shader = ResourceManager::GetInstance().ShaderLoad(
                "resources/shaders/custom/ubershader.vs", "resources/shaders/custom/ubershader.fs");
            shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, "emissionMap");
            return shader;
        }

        UberShaderComponent CreateThumbnailUberComponent(const ModelView& model, const Shader shader)
        {
            UberShaderComponent uber(static_cast<unsigned int>(model.GetMaterialCount()));
            uber.shader = shader;
            uber.litLoc = GetShaderLocation(shader, "lit");
            uber.skinnedLoc = GetShaderLocation(shader, "skinned");
            uber.hasEmissiveTexLoc = GetShaderLocation(shader, "hasEmissionTex");
            uber.hasEmissiveColLoc = GetShaderLocation(shader, "hasEmissionCol");
            uber.colEmissiveLoc = GetShaderLocation(shader, "colEmission");
            uber.SetFlagAll(UberShaderComponent::Flags::Lit);

            const auto& rlmodel = model.GetRlModel();
            for (int i = 0; i < rlmodel.materialCount; ++i)
            {
                const auto emissionColor = rlmodel.materials[i].maps[MATERIAL_MAP_EMISSION].color;
                const auto emissionTexture = rlmodel.materials[i].maps[MATERIAL_MAP_EMISSION].texture.id;
                if (emissionColor.r != 0 || emissionColor.g != 0 || emissionColor.b != 0)
                {
                    uber.SetFlag(static_cast<unsigned int>(i), UberShaderComponent::Flags::EmissiveCol);
                }
                if (emissionTexture > 1)
                {
                    uber.SetFlag(static_cast<unsigned int>(i), UberShaderComponent::Flags::EmissiveTexture);
                }
            }

            return uber;
        }

        void SetThumbnailLight(
            const Shader shader,
            const int index,
            const int type,
            const bool enabled,
            const Vector3 position,
            const Vector3 target,
            const Color color,
            const float brightness,
            const float constant,
            const float linear,
            const float quadratic)
        {
            const int enabledInt = enabled ? 1 : 0;
            const float positionValue[3] = {position.x, position.y, position.z};
            const float targetValue[3] = {target.x, target.y, target.z};
            const float colorValue[4] = {
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                static_cast<float>(color.a) / 255.0f};

            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].enabled", index)),
                &enabledInt,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].type", index)),
                &type,
                SHADER_UNIFORM_INT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].position", index)),
                positionValue,
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].target", index)),
                targetValue,
                SHADER_UNIFORM_VEC3);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].color", index)),
                colorValue,
                SHADER_UNIFORM_VEC4);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].brightness", index)),
                &brightness,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].constant", index)),
                &constant,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].linear", index)),
                &linear,
                SHADER_UNIFORM_FLOAT);
            SetShaderValue(
                shader,
                GetShaderLocation(shader, TextFormat("lights[%i].quadratic", index)),
                &quadratic,
                SHADER_UNIFORM_FLOAT);
        }

        void ConfigureThumbnailLighting(const Shader shader, const Camera3D& camera, const Vector3& center)
        {
            const float ambient[4] = {0.6f, 0.2f, 0.8f, 1.0f};
            SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient, SHADER_UNIFORM_VEC4);

            const int lightsCount = 2;
            SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
            SetShaderValue(shader, GetShaderLocation(shader, "gamma"), &PREVIEW_GAMMA, SHADER_UNIFORM_FLOAT);

            const float viewPos[3] = {camera.position.x, camera.position.y, camera.position.z};
            SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), viewPos, SHADER_UNIFORM_VEC3);

            SetThumbnailLight(
                shader,
                0,
                PREVIEW_LIGHT_POINT,
                true,
                camera.position,
                center,
                PREVIEW_LIGHT_COLOR,
                1.17f,
                1.0f,
                0.0f,
                0.0f);
            SetThumbnailLight(
                shader,
                1,
                PREVIEW_LIGHT_DIRECTIONAL,
                true,
                Vector3Add(center, {-3.0f, 4.0f, -4.0f}),
                center,
                Color{172, 202, 255, 255},
                0.23f,
                1.0f,
                0.0f,
                0.0f);
        }

        template <class T>
        bool CommitField(const LeafField<T>& field, const T& value)
        {
            if (field.setter)
            {
                field.setter(value);
                return true;
            }
            else if (field.data)
            {
                *field.data = value;
                return true;
            }
            return false;
        }

        std::string TrimCopy(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            {
                value.remove_prefix(1);
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            {
                value.remove_suffix(1);
            }
            return std::string{value};
        }

        std::string NormalizedNumberList(std::string_view value)
        {
            std::string result{value};
            for (auto& c : result)
            {
                if (c == ',' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}')
                {
                    c = ' ';
                }
            }
            return result;
        }

        bool ParseBool(std::string_view value, bool& out)
        {
            auto text = TrimCopy(value);
            std::ranges::transform(text, text.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (text == "true" || text == "1" || text == "yes" || text == "on")
            {
                out = true;
                return true;
            }
            if (text == "false" || text == "0" || text == "no" || text == "off")
            {
                out = false;
                return true;
            }
            return false;
        }

        bool ParseScalar(std::string_view value, int& out)
        {
            try
            {
                std::size_t parsed = 0;
                const auto result = std::stoi(TrimCopy(value), &parsed);
                if (parsed == 0) return false;
                out = result;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ParseScalar(std::string_view value, unsigned int& out)
        {
            int parsed = 0;
            if (!ParseScalar(value, parsed)) return false;
            out = static_cast<unsigned int>(std::max(0, parsed));
            return true;
        }

        bool ParseScalar(std::string_view value, std::uint64_t& out)
        {
            try
            {
                std::size_t parsed = 0;
                const auto result = std::stoull(TrimCopy(value), &parsed);
                if (parsed == 0) return false;
                out = static_cast<std::uint64_t>(result);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ParseScalar(std::string_view value, float& out)
        {
            try
            {
                std::size_t parsed = 0;
                const auto result = std::stof(TrimCopy(value), &parsed);
                if (parsed == 0) return false;
                out = result;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ParseVector2(std::string_view value, Vector2& out)
        {
            std::istringstream stream{NormalizedNumberList(value)};
            return static_cast<bool>(stream >> out.x >> out.y);
        }

        bool ParseVector3(std::string_view value, Vector3& out)
        {
            std::istringstream stream{NormalizedNumberList(value)};
            return static_cast<bool>(stream >> out.x >> out.y >> out.z);
        }

        bool ParseColor(std::string_view value, Color& out)
        {
            const auto text = TrimCopy(value);
            if (!text.empty() && text.front() == '#')
            {
                try
                {
                    const auto hex = text.substr(1);
                    if (hex.size() != 6 && hex.size() != 8) return false;
                    const auto packed = std::stoul(hex, nullptr, 16);
                    out.r = static_cast<unsigned char>((packed >> (hex.size() == 8 ? 24 : 16)) & 0xff);
                    out.g = static_cast<unsigned char>((packed >> (hex.size() == 8 ? 16 : 8)) & 0xff);
                    out.b = static_cast<unsigned char>((packed >> (hex.size() == 8 ? 8 : 0)) & 0xff);
                    out.a = hex.size() == 8 ? static_cast<unsigned char>(packed & 0xff) : 255;
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            }

            std::istringstream stream{NormalizedNumberList(text)};
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 255;
            if (!(stream >> r >> g >> b)) return false;
            stream >> a;
            out = {
                static_cast<unsigned char>(std::clamp(r, 0, 255)),
                static_cast<unsigned char>(std::clamp(g, 0, 255)),
                static_cast<unsigned char>(std::clamp(b, 0, 255)),
                static_cast<unsigned char>(std::clamp(a, 0, 255))};
            return true;
        }

        std::string FormatLeafValue(const LeafField<bool>& field)
        {
            return field.data && *field.data ? "true" : "false";
        }

        template <class T>
        std::string FormatLeafValue(const LeafField<T>& field)
        {
            if (!field.data) return {};
            if constexpr (std::is_same_v<T, float>)
                return std::format("{:.3f}", *field.data);
            else if constexpr (std::is_same_v<T, std::string>)
                return *field.data;
            else
                return std::to_string(*field.data);
        }

        std::string FormatLeafValue(const LeafField<Vector2>& field)
        {
            if (!field.data) return {};
            return std::format("{:.3f}, {:.3f}", field.data->x, field.data->y);
        }

        std::string FormatLeafValue(const LeafField<Vector3>& field)
        {
            if (!field.data) return {};
            return std::format("{:.3f}, {:.3f}, {:.3f}", field.data->x, field.data->y, field.data->z);
        }

        std::string FormatLeafValue(const LeafField<::Color>& field)
        {
            if (!field.data) return {};
            return std::format(
                "#{:02X}{:02X}{:02X}{:02X}", field.data->r, field.data->g, field.data->b, field.data->a);
        }

        std::string FormatEnumValue(const EnumField& field)
        {
            if (!field.getIndex) return {};
            const auto index = field.getIndex();
            if (index >= field.options.size()) return {};
            return field.options[index];
        }

        std::string FormatFieldValue(const FieldValue& value)
        {
            return std::visit(
                []<typename T0>(const T0& field) {
                    using T = std::decay_t<T0>;
                    if constexpr (std::is_same_v<T, EnumField>)
                        return FormatEnumValue(field);
                    else
                        return FormatLeafValue(field);
                },
                value);
        }

        std::string FormatComponentValues(const InspectedComponent& component)
        {
            std::string result = component.displayName;
            for (const auto& field : component.fields)
            {
                result += "\n";
                result += field.label;
                result += ": ";
                result += field.mixed ? "-" : FormatFieldValue(field.value);
            }
            return result;
        }

        bool DrawFieldClipboardMenu(
            const std::string& value,
            const bool editable,
            const std::function<bool(std::string_view)>& paste)
        {
            if (!ImGui::BeginPopupContextItem("field_context")) return false;

            bool changed = false;
            if (ImGui::MenuItem("Copy Value"))
            {
                ImGui::SetClipboardText(value.c_str());
            }
            if (editable && ImGui::MenuItem("Paste Value"))
            {
                if (const char* clipboard = ImGui::GetClipboardText(); clipboard != nullptr && paste)
                {
                    changed = paste(clipboard);
                }
            }
            ImGui::EndPopup();
            return changed;
        }

        void DrawTextFit(
            const Font font,
            const std::string& text,
            const Vector2 position,
            const float maxWidth,
            int fontSize,
            const Color color)
        {
            while (fontSize > 12 && MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x > maxWidth)
            {
                --fontSize;
            }

            DrawTextEx(font, text.c_str(), {position.x + 1.0f, position.y + 1.0f}, fontSize, 1.0f, BLACK);
            DrawTextEx(font, text.c_str(), position, fontSize, 1.0f, color);
        }

        bool DrawDockResizeHandle(
            const char* id,
            const ImVec2 pos,
            const ImVec2 size,
            const ImGuiMouseCursor cursor,
            const std::function<bool(ImVec2)>& resize)
        {
            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton(id, size);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (hovered || active)
            {
                ImGui::SetMouseCursor(cursor);
            }

            bool changed = false;
            if (active)
            {
                changed = resize(ImGui::GetIO().MouseDelta);
            }

            const ImVec4 color =
                active ? ImVec4{0.36f, 0.58f, 0.92f, 0.95f}
                       : hovered ? ImVec4{0.36f, 0.58f, 0.92f, 0.70f}
                                 : ImVec4{0.28f, 0.32f, 0.38f, 0.55f};
            ImGui::GetWindowDrawList()->AddRectFilled(
                pos, ImVec2{pos.x + size.x, pos.y + size.y}, ImGui::GetColorU32(color));
            return changed;
        }

        // Edit-boundary probes for undo/redo. The inspector mutates the live
        // component every frame a widget is held, so these flag the frame an edit
        // begins (widget activated) and the frame it ends with a value change.
        // DrawInspectorWindow resets them before drawing and reads them after.
        // Single-threaded immediate-mode UI makes file-scope state safe here.
        bool g_fieldEditBegan = false;
        bool g_fieldEditCommitted = false;

        template <class DrawFn>
        void DrawMaybeDisabled(const bool editable, DrawFn&& draw)
        {
            int styleColorCount = 0;
            if (editable)
            {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.18f, 0.38f, 0.54f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.24f, 0.48f, 0.68f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.30f, 0.58f, 0.80f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.58f, 0.78f, 0.94f, 0.90f});
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4{0.82f, 0.94f, 1.00f, 1.00f});
                styleColorCount = 5;
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.12f, 0.13f, 0.15f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.14f, 0.15f, 0.17f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.14f, 0.15f, 0.17f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.24f, 0.26f, 0.30f, 0.85f});
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4{0.48f, 0.52f, 0.58f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4{0.50f, 0.54f, 0.60f, 1.00f});
                styleColorCount = 6;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            if (!editable) ImGui::BeginDisabled();
            draw();
            // The widget is the last-submitted item here (the clipboard menu, if any,
            // is submitted by the caller after this returns), so these queries refer
            // to it.
            g_fieldEditBegan |= ImGui::IsItemActivated();
            g_fieldEditCommitted |= ImGui::IsItemDeactivatedAfterEdit();
            if (!editable) ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(styleColorCount);
        }

        template <class T, class ParseFn>
        bool DrawMixedTextField(const LeafField<T>& field, const bool editable, ParseFn&& parse)
        {
            std::string value = "-";
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                constexpr ImGuiInputTextFlags flags =
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                const bool enterPressed = ImGui::InputText("##value", &value, flags);
                const bool commit = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
                if (commit && editable && value != "-")
                {
                    T parsed{};
                    if (parse(value, parsed))
                    {
                        changed = CommitField(field, parsed);
                    }
                }
            });
            changed |= DrawFieldClipboardMenu("-", editable, [field, parse](const std::string_view text) {
                T parsed{};
                if (!parse(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawMixedStringField(const LeafField<std::string>& field, const bool editable)
        {
            std::string value = "-";
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                constexpr ImGuiInputTextFlags flags =
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                const bool enterPressed = ImGui::InputText("##value", &value, flags);
                const bool commit = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
                if (commit && editable && value != "-")
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu("-", editable, [field](const std::string_view text) {
                return CommitField(field, std::string{text});
            });
            return changed;
        }

        // Multi-selection editing for vector fields: one text input per axis. An axis
        // whose values agree across the selection shows the shared number; an axis that
        // differs shows "-". Editing one axis writes only that axis to every selected
        // entity (via componentSetter), leaving each entity's other axes untouched.
        bool DrawMixedVectorComponents(
            const float* values,
            const int count,
            const unsigned int mixedMask,
            const bool editable,
            const std::function<void(std::size_t, float)>& componentSetter)
        {
            bool changed = false;
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float fullWidth = ImGui::GetContentRegionAvail().x;
            const float itemWidth = std::max(1.0f, (fullWidth - spacing * static_cast<float>(count - 1)) / count);
            for (int i = 0; i < count; ++i)
            {
                ImGui::PushID(i);
                if (i > 0) ImGui::SameLine(0.0f, spacing);
                ImGui::SetNextItemWidth(itemWidth);
                std::string text = (mixedMask & (1u << i)) ? "-" : std::format("{:.3f}", values[i]);
                DrawMaybeDisabled(editable, [&]() {
                    constexpr ImGuiInputTextFlags flags =
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                    const bool enterPressed = ImGui::InputText("##value", &text, flags);
                    const bool commit = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
                    float parsed = 0.0f;
                    if (commit && editable && text != "-" && componentSetter && ParseScalar(text, parsed))
                    {
                        componentSetter(static_cast<std::size_t>(i), parsed);
                        changed = true;
                    }
                });
                ImGui::PopID();
            }
            return changed;
        }

        bool DrawInspectorFieldWidget(const LeafField<bool>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedTextField<bool>(field, editable, [](const std::string_view text, bool& out) {
                    return ParseBool(text, out);
                });
            }

            bool value = field.data && *field.data;
            bool changed = false;
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::Checkbox("##value", &value) && editable)
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                bool parsed = false;
                if (!ParseBool(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(const LeafField<int>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedTextField<int>(field, editable, [](const std::string_view text, int& out) {
                    return ParseScalar(text, out);
                });
            }

            int value = field.data ? *field.data : 0;
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragInt("##value", &value, 1.0f) && editable)
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                int parsed = 0;
                if (!ParseScalar(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(
            const LeafField<unsigned int>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedTextField<unsigned int>(
                    field, editable, [](const std::string_view text, unsigned int& out) {
                        return ParseScalar(text, out);
                    });
            }

            unsigned int value = field.data ? *field.data : 0;
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragScalar("##value", ImGuiDataType_U32, &value, 1.0f) && editable)
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                unsigned int parsed = 0;
                if (!ParseScalar(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(
            const LeafField<std::uint64_t>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedTextField<std::uint64_t>(
                    field, editable, [](const std::string_view text, std::uint64_t& out) {
                        return ParseScalar(text, out);
                    });
            }

            std::uint64_t value = field.data ? *field.data : 0;
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::InputScalar("##value", ImGuiDataType_U64, &value) && editable)
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                std::uint64_t parsed = 0;
                if (!ParseScalar(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(const LeafField<float>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedTextField<float>(field, editable, [](const std::string_view text, float& out) {
                    return ParseScalar(text, out);
                });
            }

            float value = field.data ? *field.data : 0.0f;
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragFloat("##value", &value, 0.05f, 0.0f, 0.0f, "%.3f") && editable)
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                float parsed = 0.0f;
                if (!ParseScalar(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(
            const LeafField<std::string>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedStringField(field, editable);
            }

            std::string value = field.data ? *field.data : std::string{};
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            const auto flags = editable ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::InputText("##value", &value, flags) && editable)
                {
                    changed = CommitField(field, value);
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                return CommitField(field, std::string{text});
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(const LeafField<Vector2>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                const float values[2] = {
                    field.data ? field.data->x : 0.0f,
                    field.data ? field.data->y : 0.0f};
                return DrawMixedVectorComponents(
                    values, 2, field.mixedComponents, editable, field.componentSetter);
            }

            float value[2] = {
                field.data ? field.data->x : 0.0f,
                field.data ? field.data->y : 0.0f};
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragFloat2("##value", value, 0.05f, 0.0f, 0.0f, "%.3f") && editable)
                {
                    changed = CommitField(field, Vector2{value[0], value[1]});
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                Vector2 parsed{};
                if (!ParseVector2(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(const LeafField<Vector3>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                const float values[3] = {
                    field.data ? field.data->x : 0.0f,
                    field.data ? field.data->y : 0.0f,
                    field.data ? field.data->z : 0.0f};
                return DrawMixedVectorComponents(
                    values, 3, field.mixedComponents, editable, field.componentSetter);
            }

            float value[3] = {
                field.data ? field.data->x : 0.0f,
                field.data ? field.data->y : 0.0f,
                field.data ? field.data->z : 0.0f};
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragFloat3("##value", value, 0.05f, 0.0f, 0.0f, "%.3f") && editable)
                {
                    changed = CommitField(field, Vector3{value[0], value[1], value[2]});
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                Vector3 parsed{};
                if (!ParseVector3(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(const LeafField<::Color>& field, const bool editable, const bool mixed)
        {
            if (mixed)
            {
                return DrawMixedTextField<::Color>(field, editable, [](const std::string_view text, Color& out) {
                    return ParseColor(text, out);
                });
            }

            float value[4] = {
                field.data ? static_cast<float>(field.data->r) / 255.0f : 1.0f,
                field.data ? static_cast<float>(field.data->g) / 255.0f : 1.0f,
                field.data ? static_cast<float>(field.data->b) / 255.0f : 1.0f,
                field.data ? static_cast<float>(field.data->a) / 255.0f : 1.0f};
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            constexpr ImGuiColorEditFlags flags =
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB;
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::ColorEdit4("##value", value, flags) && editable)
                {
                    const auto toByte = [](const float v) {
                        return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                    };
                    changed = CommitField(
                        field,
                        Color{toByte(value[0]), toByte(value[1]), toByte(value[2]), toByte(value[3])});
                }
            });
            changed |= DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                Color parsed{};
                if (!ParseColor(text, parsed)) return false;
                return CommitField(field, parsed);
            });
            return changed;
        }

        bool DrawInspectorFieldWidget(const EnumField& field, const bool editable, const bool mixed)
        {
            const auto currentIndex = field.getIndex ? field.getIndex() : 0;
            const char* currentLabel =
                mixed ? "-" : currentIndex < field.options.size() ? field.options[currentIndex].c_str() : "";
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::BeginCombo("##value", currentLabel))
                {
                    for (std::size_t i = 0; i < field.options.size(); ++i)
                    {
                        const bool selected = i == currentIndex;
                        if (ImGui::Selectable(field.options[i].c_str(), selected) && editable && field.setIndex)
                        {
                            field.setIndex(i);
                            changed = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            });
            changed |= DrawFieldClipboardMenu(mixed ? "-" : FormatEnumValue(field), editable, [field](const std::string_view text) {
                const auto pasted = TrimCopy(text);
                const auto it = std::ranges::find(field.options, pasted);
                if (it == field.options.end() || !field.setIndex) return false;
                field.setIndex(static_cast<std::size_t>(std::distance(field.options.begin(), it)));
                return true;
            });
            return changed;
        }

        bool DrawInspectorFieldRow(const InspectorField& field)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(field.label.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(field.label.c_str());
            bool changed = false;
            std::visit(
                [&](const auto& value) {
                    changed = DrawInspectorFieldWidget(value, field.editable, field.mixed);
                },
                field.value);
            ImGui::PopID();
            return changed;
        }

        bool DrawInspectorComponent(const InspectedComponent& component)
        {
            ImGui::PushID(component.displayName.c_str());
            bool changed = false;
            const bool open = ImGui::CollapsingHeader(
                component.displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::BeginPopupContextItem("component_context"))
            {
                if (ImGui::MenuItem("Copy Component Values"))
                {
                    const auto text = FormatComponentValues(component);
                    ImGui::SetClipboardText(text.c_str());
                }
                ImGui::EndPopup();
            }

            if (open)
            {
                constexpr ImGuiTableFlags tableFlags =
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_PadOuterX;
                if (ImGui::BeginTable("fields", 2, tableFlags))
                {
                    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                    for (const auto& field : component.fields)
                    {
                        changed |= DrawInspectorFieldRow(field);
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
            return changed;
        }

    } // namespace

    void EditorGui::StartImGui()
    {
        rlImGuiBegin();
    }

    void EditorGui::EndImGui()
    {
        rlImGuiEnd();
    }

    void EditorGui::DrawHierarchyWindow()
    {
        if (!settings) return;

        if (focusedHierarchyEntity.has_value() &&
            !std::ranges::any_of(hierarchyEntries, [this](const SceneObjectEntry& entry) {
                return entry.entity == *focusedHierarchyEntity;
            }))
        {
            focusedHierarchyEntity.reset();
        }

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float mainMenuHeight = ImGui::GetFrameHeight();
        const float leftDockWidth = dockLayout ? dockLayout->leftDockWidth : EDITOR_LEFT_DOCK_DEFAULT_WIDTH;
        const ImVec2 windowPos{viewportOffset.x, viewportOffset.y + mainMenuHeight};
        const ImVec2 windowSize{
            settings->ScaleValueWidth(leftDockWidth), std::max(1.0f, viewport.y - mainMenuHeight)};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImGuiColor(EDITOR_WINDOW_BACKGROUND));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EDITOR_TEXT));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.26f, 0.38f, 0.90f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.23f, 0.34f, 0.50f, 0.95f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});

        std::optional<SceneSelectionRequest> sceneSelectionRequest;
        std::optional<HierarchyMoveRequest> hierarchyMoveRequest;

        auto acceptHierarchyDrop = [&](const entt::entity newParent, const entt::entity insertBefore) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HIERARCHY_DRAG_PAYLOAD);
                payload != nullptr && payload->Delivery && payload->DataSize == sizeof(std::uint32_t))
            {
                const auto dragged = EntityFromPayloadId(*static_cast<const std::uint32_t*>(payload->Data));
                if (dragged != newParent && dragged != insertBefore)
                {
                    hierarchyMoveRequest = HierarchyMoveRequest{
                        .dragged = dragged,
                        .newParent = newParent,
                        .insertBefore = insertBefore};
                }
            }
        };

        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("Hierarchy", nullptr, windowFlags))
        {
            const float resizeHandleWidth = dockLayout ? DOCK_RESIZE_HANDLE_THICKNESS : 0.0f;
            const float resizeHandleX = windowPos.x + windowSize.x - resizeHandleWidth;

            const float filterWidth = std::max(1.0f, resizeHandleX - ImGui::GetCursorScreenPos().x);
            DrawSearchFilter(hierarchyFilter, "hierarchy_filter", "Search...", filterWidth);
            ImGui::Spacing();

            const ImVec2 contentRegion = ImGui::GetContentRegionAvail();
            const ImVec2 treePos = ImGui::GetCursorScreenPos();
            const ImVec2 treeSize{
                dockLayout ? std::max(1.0f, resizeHandleX - treePos.x)
                           : std::max(1.0f, contentRegion.x),
                std::max(1.0f, contentRegion.y)};

            if (ImGui::BeginChild("hierarchy_scroll", treeSize, false))
            {
                auto drawBlankDropTarget = [&]() {
                    const ImVec2 available = ImGui::GetContentRegionAvail();
                    const ImVec2 dropSize{std::max(1.0f, available.x), std::max(28.0f, available.y)};
                    const ImVec2 dropMin = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##hierarchy_blank_drop", dropSize);
                    bool targetActive = false;
                    if (ImGui::BeginDragDropTarget())
                    {
                        targetActive = true;
                        acceptHierarchyDrop(entt::null, entt::null);
                        ImGui::EndDragDropTarget();
                    }
                    if (targetActive)
                    {
                        const auto color = ImGui::GetColorU32(ImVec4{0.36f, 0.58f, 0.92f, 0.85f});
                        ImGui::GetWindowDrawList()->AddRect(
                            dropMin,
                            ImVec2{dropMin.x + dropSize.x, dropMin.y + dropSize.y},
                            color,
                            4.0f,
                            0,
                            2.0f);
                    }
                };

                if (hierarchyEntries.empty())
                {
                    ImGui::TextDisabled("No scene objects");
                    drawBlankDropTarget();
                }
                else
                {
                    auto subtreeContainsEntity = [this](const std::size_t rootIndex, const entt::entity entity) {
                        if (rootIndex >= hierarchyEntries.size()) return false;
                        const int rootDepth = hierarchyEntries[rootIndex].depth;
                        for (std::size_t i = rootIndex; i < hierarchyEntries.size(); ++i)
                        {
                            if (i != rootIndex && hierarchyEntries[i].depth <= rootDepth) break;
                            if (hierarchyEntries[i].entity == entity) return true;
                        }
                        return false;
                    };

                    // True if the entry at rootIndex or any of its descendants matches the
                    // active search filter. Used to keep ancestors of a match visible.
                    auto subtreePassesFilter = [this](const std::size_t rootIndex) {
                        if (rootIndex >= hierarchyEntries.size()) return false;
                        const int rootDepth = hierarchyEntries[rootIndex].depth;
                        for (std::size_t i = rootIndex; i < hierarchyEntries.size(); ++i)
                        {
                            if (i != rootIndex && hierarchyEntries[i].depth <= rootDepth) break;
                            if (hierarchyFilter.PassFilter(hierarchyEntries[i].displayName.c_str())) return true;
                        }
                        return false;
                    };

                    auto drawInsertBeforeTarget = [&](const std::size_t entryIndex) {
                        const auto& entry = hierarchyEntries[entryIndex];
                        constexpr float targetHeight = 7.0f;
                        const ImVec2 targetMin = ImGui::GetCursorScreenPos();
                        const float targetWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);

                        ImGui::PushID(static_cast<int>(entryIndex));
                        ImGui::InvisibleButton("##insert_before", ImVec2{targetWidth, targetHeight});
                        bool targetActive = false;
                        if (ImGui::BeginDragDropTarget())
                        {
                            targetActive = true;
                            acceptHierarchyDrop(entry.parent, entry.entity);
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::PopID();

                        if (targetActive)
                        {
                            const float indent = static_cast<float>(entry.depth) * ImGui::GetTreeNodeToLabelSpacing();
                            const float y = targetMin.y + targetHeight * 0.5f;
                            const auto color = ImGui::GetColorU32(ImVec4{0.36f, 0.58f, 0.92f, 1.00f});
                            ImGui::GetWindowDrawList()->AddLine(
                                ImVec2{targetMin.x + indent, y},
                                ImVec2{targetMin.x + targetWidth, y},
                                color,
                                2.0f);
                        }
                    };

                    auto skipSubtree = [this](std::size_t& index, const int depth) {
                        ++index;
                        while (index < hierarchyEntries.size() && hierarchyEntries[index].depth > depth)
                        {
                            ++index;
                        }
                    };

                    std::function<void(std::size_t&)> drawEntry = [&](std::size_t& index) {
                        if (index >= hierarchyEntries.size()) return;

                        const std::size_t entryIndex = index;
                        const auto& entry = hierarchyEntries[entryIndex];

                        if (hierarchyFilter.IsActive() && !subtreePassesFilter(entryIndex))
                        {
                            skipSubtree(index, entry.depth);
                            return;
                        }

                        const bool hasChildren = entryIndex + 1 < hierarchyEntries.size() &&
                                                 hierarchyEntries[entryIndex + 1].depth > entry.depth;
                        const bool selected =
                            std::ranges::find(selectedSceneEntities, entry.entity) != selectedSceneEntities.end();

                        drawInsertBeforeTarget(entryIndex);

                        ImGuiTreeNodeFlags nodeFlags =
                            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                            ImGuiTreeNodeFlags_OpenOnDoubleClick;
                        if (selected) nodeFlags |= ImGuiTreeNodeFlags_Selected;
                        if (!hasChildren) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                        if (hasChildren)
                        {
                            if (hierarchyFilter.IsActive() ||
                                (focusedHierarchyEntity.has_value() &&
                                 subtreeContainsEntity(entryIndex, *focusedHierarchyEntity)))
                            {
                                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                            }
                            else
                            {
                                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                            }
                        }

                        const auto imguiEntityId =
                            static_cast<std::uintptr_t>(EntityPayloadId(entry.entity)) + 1u;
                        const bool open = ImGui::TreeNodeEx(
                            reinterpret_cast<void*>(imguiEntityId), nodeFlags, "%s", entry.displayName.c_str());

                        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                        {
                            sceneSelectionRequest = makeSceneSelectionRequest(entry.entity);
                        }

                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            pendingHierarchyContextEntity = entry.entity;
                        }

                        if (focusedHierarchyEntity.has_value() && *focusedHierarchyEntity == entry.entity)
                        {
                            ImGui::SetScrollHereY(0.5f);
                            focusedHierarchyEntity.reset();
                        }

                        if (ImGui::BeginDragDropSource())
                        {
                            const std::uint32_t payload = EntityPayloadId(entry.entity);
                            ImGui::SetDragDropPayload(HIERARCHY_DRAG_PAYLOAD, &payload, sizeof(payload));
                            ImGui::TextUnformatted(entry.displayName.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::BeginDragDropTarget())
                        {
                            acceptHierarchyDrop(entry.entity, entt::null);
                            ImGui::EndDragDropTarget();
                        }

                        if (!hasChildren)
                        {
                            ++index;
                        }
                        else if (open)
                        {
                            ++index;
                            while (index < hierarchyEntries.size() && hierarchyEntries[index].depth > entry.depth)
                            {
                                drawEntry(index);
                            }
                            ImGui::TreePop();
                        }
                        else
                        {
                            skipSubtree(index, entry.depth);
                        }

                    };

                    std::size_t index = 0;
                    while (index < hierarchyEntries.size())
                    {
                        drawEntry(index);
                    }
                    drawBlankDropTarget();
                }
            }
            ImGui::EndChild();

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##hierarchy_resize",
                    ImVec2{resizeHandleX, handleTop},
                    ImVec2{resizeHandleWidth, std::max(1.0f, windowSize.y - ImGui::GetFrameHeight())},
                    ImGuiMouseCursor_ResizeEW,
                    [this, viewport](const ImVec2 delta) {
                        const float logicalDelta =
                            delta.x * Settings::TARGET_SCREEN_WIDTH / std::max(1.0f, viewport.x);
                        return SetLeftDockWidth(*dockLayout, dockLayout->leftDockWidth + logicalDelta);
                    });
            }
        }
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);

        if (sceneSelectionRequest.has_value() && onSceneObjectSelectedCb)
        {
            onSceneObjectSelectedCb(*sceneSelectionRequest);
        }
        if (hierarchyMoveRequest.has_value() && onHierarchyMoveCb)
        {
            onHierarchyMoveCb(*hierarchyMoveRequest);
        }
    }

    EditorGui::InspectorEditResult EditorGui::DrawInspectorWindow()
    {
        if (!settings) return {};
        bool changed = false;
        g_fieldEditBegan = false;
        g_fieldEditCommitted = false;

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float mainMenuHeight = ImGui::GetFrameHeight();
        const float rightDockWidth = dockLayout ? dockLayout->rightDockWidth : EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        const float width = settings->ScaleValueWidth(rightDockWidth);
        const ImVec2 windowPos{
            viewportOffset.x + viewport.x - width,
            viewportOffset.y + mainMenuHeight};
        const ImVec2 windowSize{width, std::max(1.0f, viewport.y - mainMenuHeight)};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImGuiColor(EDITOR_WINDOW_BACKGROUND));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EDITOR_TEXT));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.26f, 0.38f, 0.90f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.23f, 0.34f, 0.50f, 0.95f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 7.0f});

        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Inspector", nullptr, windowFlags))
        {
            ImGui::Text("Selected: %s", inspectorSelectedEntity.c_str());
            ImGui::Separator();

            if (inspectedComponents.empty())
            {
                ImGui::TextDisabled("No component data");
            }
            else
            {
                for (const auto& component : inspectedComponents)
                {
                    changed |= DrawInspectorComponent(component);
                }
            }

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##inspector_resize",
                    ImVec2{windowPos.x, handleTop},
                    ImVec2{DOCK_RESIZE_HANDLE_THICKNESS, std::max(1.0f, windowSize.y - ImGui::GetFrameHeight())},
                    ImGuiMouseCursor_ResizeEW,
                    [this, viewport](const ImVec2 delta) {
                        const float logicalDelta =
                            delta.x * Settings::TARGET_SCREEN_WIDTH / std::max(1.0f, viewport.x);
                        return SetRightDockWidth(*dockLayout, dockLayout->rightDockWidth - logicalDelta);
                    });
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
        return {.changed = changed, .began = g_fieldEditBegan, .committed = g_fieldEditCommitted};
    }

    void EditorGui::DrawAssetDrawerWindow()
    {
        if (!settings) return;

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float leftDockWidth = dockLayout ? dockLayout->leftDockWidth : EDITOR_LEFT_DOCK_DEFAULT_WIDTH;
        const float rightDockWidth = dockLayout ? dockLayout->rightDockWidth : EDITOR_RIGHT_DOCK_DEFAULT_WIDTH;
        const float assetDrawerHeight =
            dockLayout ? dockLayout->assetDrawerHeight : EDITOR_ASSET_DRAWER_DEFAULT_HEIGHT;
        const float left = settings->ScaleValueWidth(leftDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float right = settings->ScaleValueWidth(rightDockWidth + EDITOR_SCENE_VIEW_PADDING);
        const float height = settings->ScaleValueHeight(assetDrawerHeight);
        const float bottomMargin = settings->ScaleValueHeight(EDITOR_SCENE_VIEW_PADDING);
        const ImVec2 windowPos{
            viewportOffset.x + left,
            viewportOffset.y + viewport.y - height - bottomMargin};
        const ImVec2 windowSize{std::max(1.0f, viewport.x - left - right), height};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImGuiColor(EDITOR_WINDOW_BACKGROUND));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EDITOR_TEXT));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.26f, 0.38f, 0.90f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.23f, 0.34f, 0.50f, 0.95f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 7.0f});

        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Asset Drawer", nullptr, windowFlags))
        {
            const bool showDefaults = selectedAssetIndex.has_value();
            if (showDefaults && ImGui::BeginTable("asset_drawer_split", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Browser", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Defaults", ImGuiTableColumnFlags_WidthFixed, ASSET_DEFAULTS_PANEL_WIDTH);

                ImGui::TableNextColumn();
                if (ImGui::BeginTabBar("asset_tabs"))
                {
                    if (ImGui::BeginTabItem("Assets"))
                    {
                        currentTab = BrowserTab::Assets;
                        drawAssetGrid();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Flatpacks"))
                    {
                        currentTab = BrowserTab::Flatpacks;
                        drawFlatpackGrid();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::TableNextColumn();
                drawAssetDefaultsControls();
                ImGui::EndTable();
            }
            else if (ImGui::BeginTabBar("asset_tabs"))
            {
                if (ImGui::BeginTabItem("Assets"))
                {
                    currentTab = BrowserTab::Assets;
                    drawAssetGrid();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Flatpacks"))
                {
                    currentTab = BrowserTab::Flatpacks;
                    drawFlatpackGrid();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            drawAssetRenamePopup();

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##asset_drawer_resize",
                    ImVec2{windowPos.x, handleTop},
                    ImVec2{windowSize.x, DOCK_RESIZE_HANDLE_THICKNESS},
                    ImGuiMouseCursor_ResizeNS,
                    [this, viewport](const ImVec2 delta) {
                        const float logicalDelta =
                            delta.y * Settings::TARGET_SCREEN_HEIGHT / std::max(1.0f, viewport.y);
                        return SetAssetDrawerHeight(*dockLayout, dockLayout->assetDrawerHeight - logicalDelta);
                    });
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
    }

    void EditorGui::DrawDeleteConfirmationModal()
    {
        if (deleteConfirmationVisible)
        {
            ImGui::OpenPopup("Confirm Delete");
        }

        bool open = deleteConfirmationVisible;
        ImGui::SetNextWindowSize(ImVec2{420.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Confirm Delete", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", deleteConfirmationPrompt.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2{120.0f, 0.0f}))
            {
                pendingDeleteConfirmationAction = DeleteConfirmationAction::Confirm;
                deleteConfirmationVisible = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                pendingDeleteConfirmationAction = DeleteConfirmationAction::Cancel;
                deleteConfirmationVisible = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!open)
        {
            deleteConfirmationVisible = false;
        }
    }

    void EditorGui::SetOverlayStatus(const std::string& mode, const std::string& cursor) const
    {
        modeStatus = mode;
        cursorStatus = cursor;
    }

    void EditorGui::SetSaveStatus(const std::string& status, const bool hasUnsavedChanges) const
    {
        saveStatus = status;
        sceneHasUnsavedChanges = hasUnsavedChanges;
    }

    void EditorGui::SetAssetDefaultsStatus(
        const std::string& assetName,
        const std::string& modelDefaultHeight,
        const std::string& modelDefaultRotation,
        const std::string& modelDefaultScale)
    {
        assetDefaultsAssetName = assetName;
        assetDefaultsHeight = modelDefaultHeight;
        assetDefaultsRotation = modelDefaultRotation;
        assetDefaultsScale = modelDefaultScale;
    }

    void EditorGui::SetSceneName(const std::string& sceneName) const
    {
        sceneNameStatus = sceneName;
    }

    void EditorGui::SetSelectedAsset(const std::optional<std::size_t> index)
    {
        selectedAssetIndex = index;
    }

    EditorGui::SceneSelectionRequest EditorGui::makeSceneSelectionRequest(const entt::entity clicked) const
    {
        const ImGuiIO& io = ImGui::GetIO();

        // Shift selects every entity in the displayed tree between the anchor and
        // the clicked entity, mirroring file-explorer range selection.
        if (io.KeyShift && hierarchySelectionAnchor != entt::null)
        {
            const auto indexOf = [this](const entt::entity entity) -> std::optional<std::size_t> {
                for (std::size_t i = 0; i < hierarchyEntries.size(); ++i)
                {
                    if (hierarchyEntries[i].entity == entity) return i;
                }
                return std::nullopt;
            };

            const auto anchorIndex = indexOf(hierarchySelectionAnchor);
            const auto clickedIndex = indexOf(clicked);
            if (anchorIndex.has_value() && clickedIndex.has_value())
            {
                const auto [lo, hi] = std::minmax(*anchorIndex, *clickedIndex);
                std::vector<entt::entity> rangeEntities;
                rangeEntities.reserve(hi - lo + 1);
                for (std::size_t i = lo; i <= hi; ++i)
                {
                    rangeEntities.push_back(hierarchyEntries[i].entity);
                }
                return SceneSelectionRequest{
                    .entity = clicked, .mode = SceneSelectionMode::Range, .rangeEntities = std::move(rangeEntities)};
            }
        }

        // Alt/meta toggles a single entity into or out of the selection.
        if (io.KeyAlt)
        {
            return SceneSelectionRequest{.entity = clicked, .mode = SceneSelectionMode::Toggle};
        }

        return SceneSelectionRequest{.entity = clicked, .mode = SceneSelectionMode::Replace};
    }

    void EditorGui::SetHierarchy(
        const std::vector<SceneObjectEntry>& entries,
        std::vector<entt::entity> selectedEntities,
        const entt::entity selectionAnchor)
    {
        hierarchyEntries = entries;
        selectedSceneEntities = std::move(selectedEntities);
        hierarchySelectionAnchor = selectionAnchor;
    }

    std::optional<entt::entity> EditorGui::ConsumeHierarchyContextEntity()
    {
        const auto entity = pendingHierarchyContextEntity;
        pendingHierarchyContextEntity.reset();
        return entity;
    }

    void EditorGui::FocusHierarchyOnEntity(const entt::entity entity)
    {
        focusedHierarchyEntity = entity;
    }

    void EditorGui::SetInspector(
        const std::string& selectedEntity, const std::vector<InspectedComponent>& inspectedComponents)
    {
        inspectorSelectedEntity = selectedEntity;
        this->inspectedComponents = inspectedComponents;
    }

    void EditorGui::DrawSceneViewInfo() const
    {
        if (!settings) return;

        const auto renderViewport = settings->GetRenderViewportScreenRect();
        const float x = renderViewport.x + settings->ScaleValueWidth(16.0f);
        const float y = renderViewport.y + settings->ScaleValueHeight(14.0f);
        const float maxWidth = std::max(1.0f, renderViewport.width - settings->ScaleValueWidth(32.0f));
        const int titleSize = std::max(22, static_cast<int>(settings->ScaleValueMaintainRatio(22.0f)));
        const int metaSize = std::max(16, static_cast<int>(settings->ScaleValueMaintainRatio(16.0f)));
        const Font titleFont =
            ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-Bold.ttf");
        const Font metaFont =
            ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-SemiBold.ttf");

        const std::string title = sceneHasUnsavedChanges ? sceneNameStatus + " *" : sceneNameStatus;
        DrawTextFit(titleFont, title, {x, y}, maxWidth, titleSize, EDITOR_TEXT);
        DrawTextFit(
            metaFont,
            "Mode: " + modeStatus + "  |  Cursor: " + cursorStatus,
            {x, y + settings->ScaleValueHeight(28.0f)},
            maxWidth,
            metaSize,
            Color{202, 211, 224, 255});
        if (!saveStatus.empty())
        {
            DrawTextFit(
                metaFont,
                saveStatus,
                {x, y + settings->ScaleValueHeight(56.0f)},
                maxWidth,
                metaSize,
                sceneHasUnsavedChanges ? Color{252, 211, 77, 255} : Color{134, 239, 172, 255});
        }
    }

    void EditorGui::ShowDeleteConfirmation(const std::string& selectedEntity)
    {
        deleteConfirmationPrompt = "Delete " + selectedEntity + "?";
        deleteConfirmationVisible = true;
    }

    void EditorGui::HideDeleteConfirmation()
    {
        deleteConfirmationVisible = false;
    }

    bool EditorGui::IsDeleteConfirmationVisible() const
    {
        return deleteConfirmationVisible;
    }

    bool EditorGui::WantsMouseCapture() const
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    }

    bool EditorGui::WantsKeyboardCapture() const
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
    }

    bool EditorGui::ConsumeDockLayoutChanged()
    {
        const bool changed = dockLayoutChanged;
        dockLayoutChanged = false;
        return changed;
    }

    EditorGui::DeleteConfirmationAction EditorGui::ConsumeDeleteConfirmationAction()
    {
        const auto action = pendingDeleteConfirmationAction;
        pendingDeleteConfirmationAction = DeleteConfirmationAction::None;
        return action;
    }

    void EditorGui::SetFlatpacks(std::vector<FlatpackEntry> entries)
    {
        flatpackEntries = std::move(entries);
    }

    RenderTexture2D EditorGui::createAssetThumbnail(const AssetEntry& asset) const
    {
        auto thumbnail = LoadRenderTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        auto model = ResourceManager::GetInstance().GetModelView(asset.modelKey);
        const auto shader = LoadThumbnailShader();
        auto uber = CreateThumbnailUberComponent(model, shader);
        model.SetShader(shader);

        const auto bounds = model.CalcLocalBoundingBox();
        const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
        const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
        const float radius = std::max({std::fabs(size.x), std::fabs(size.y), std::fabs(size.z), 1.0f});

        Camera3D camera{};
        camera.position = Vector3Add(center, {radius * 1.35f, radius * 0.85f, radius * 1.65f});
        camera.target = center;
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 32.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        BeginTextureMode(thumbnail);
        ClearBackground(Color{244, 247, 251, 255});
        BeginMode3D(camera);
        ConfigureThumbnailLighting(shader, camera, center);
        model.DrawUber(&uber, Vector3Zero(), {0.0f, 1.0f, 0.0f}, 0.0f, Vector3One(), WHITE);
        EndMode3D();
        EndTextureMode();

        return thumbnail;
    }

    void EditorGui::openAssetRenamePopup(const std::size_t index)
    {
        if (index >= assetEntries.size()) return;

        const auto& asset = assetEntries[index];
        const auto renamePath = !asset.sourcePath.empty() ? asset.sourcePath : asset.defaultsPath;
        renamingAssetIndex = index;
        assetRenameInput = renamePath.filename().string();
        assetRenameStatus.clear();
        assetRenamePopupOpenRequested = true;
    }

    void EditorGui::drawAssetRenamePopup()
    {
        if (!renamingAssetIndex.has_value()) return;
        if (*renamingAssetIndex >= assetEntries.size())
        {
            renamingAssetIndex.reset();
            return;
        }

        if (assetRenamePopupOpenRequested)
        {
            ImGui::OpenPopup(ASSET_RENAME_POPUP);
            assetRenamePopupOpenRequested = false;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2{430.0f, 0.0f}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(ASSET_RENAME_POPUP, &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto index = *renamingAssetIndex;
            const auto& asset = assetEntries[index];
            const auto renamePath = !asset.sourcePath.empty() ? asset.sourcePath : asset.defaultsPath;

            ImGui::TextWrapped("%s", asset.displayName.c_str());
            ImGui::TextDisabled("%s", renamePath.parent_path().string().c_str());
            ImGui::Spacing();

            ImGui::SetNextItemWidth(390.0f);
            const bool enterPressed =
                ImGui::InputText("File name", &assetRenameInput, ImGuiInputTextFlags_EnterReturnsTrue);

            if (!assetRenameStatus.empty())
            {
                ImGui::TextWrapped("%s", assetRenameStatus.c_str());
            }

            ImGui::Spacing();
            const bool renamePressed = ImGui::Button("Rename", ImVec2{120.0f, 0.0f});
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}))
            {
                renamingAssetIndex.reset();
                assetRenameInput.clear();
                assetRenameStatus.clear();
                assetRenamePopupOpenRequested = false;
                ImGui::CloseCurrentPopup();
            }

            if ((enterPressed || renamePressed) && onAssetRenameCb)
            {
                auto result = onAssetRenameCb(index, assetRenameInput);
                assetRenameStatus = std::move(result.message);
                if (result.renamed)
                {
                    if (result.updatedEntry.has_value())
                    {
                        assetEntries[index] = std::move(*result.updatedEntry);
                    }
                    renamingAssetIndex.reset();
                    assetRenameInput.clear();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if (!open)
        {
            renamingAssetIndex.reset();
            assetRenameInput.clear();
            assetRenameStatus.clear();
            assetRenamePopupOpenRequested = false;
        }
    }

    void EditorGui::drawAssetDefaultsControls()
    {
        if (ImGui::BeginChild("asset_defaults", ImVec2{0.0f, 0.0f}, true))
        {
            ImGui::TextUnformatted("Asset Defaults");
            ImGui::Separator();
            ImGui::TextWrapped("Asset: %s", assetDefaultsAssetName.c_str());

            auto adjustmentRow = [](const char* label,
                                    const std::string& value,
                                    const std::function<void()>& down,
                                    const std::function<void()>& up) {
                ImGui::PushID(label);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine(70.0f);
                if (ImGui::SmallButton("-") && down) down();
                ImGui::SameLine();
                auto displayValue = value;
                ImGui::SetNextItemWidth(72.0f);
                ImGui::InputText("##value", &displayValue, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::SmallButton("+") && up) up();
                ImGui::PopID();
            };

            adjustmentRow("Z", assetDefaultsHeight, modelDefaultCallbacks.heightDown, modelDefaultCallbacks.heightUp);
            adjustmentRow(
                "Rot Y",
                assetDefaultsRotation,
                modelDefaultCallbacks.rotationDown,
                modelDefaultCallbacks.rotationUp);
            adjustmentRow("Scale", assetDefaultsScale, modelDefaultCallbacks.scaleDown, modelDefaultCallbacks.scaleUp);

            ImGui::Spacing();
            if (ImGui::Button("Apply", ImVec2{96.0f, 0.0f}) && modelDefaultCallbacks.apply)
            {
                modelDefaultCallbacks.apply();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2{96.0f, 0.0f}) && modelDefaultCallbacks.reset)
            {
                modelDefaultCallbacks.reset();
            }
        }
        ImGui::EndChild();
    }

    void EditorGui::drawAssetGrid()
    {
        if (assetEntries.empty())
        {
            ImGui::TextDisabled("No assets loaded");
            return;
        }

        DrawSearchFilter(assetFilter, "asset_filter", "Search...", ImGui::GetContentRegionAvail().x);
        ImGui::Spacing();

        // Compact the visible entries so filtered-out tiles don't leave gaps in the grid.
        std::vector<std::size_t> visibleAssets;
        visibleAssets.reserve(assetEntries.size());
        for (std::size_t i = 0; i < assetEntries.size(); ++i)
        {
            const auto& asset = assetEntries[i];
            if (assetFilter.PassFilter(asset.displayName.c_str()) ||
                assetFilter.PassFilter(asset.modelKey.c_str()))
            {
                visibleAssets.push_back(i);
            }
        }

        if (visibleAssets.empty())
        {
            ImGui::TextDisabled("No assets match the filter");
            return;
        }

        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float columnPitch = ASSET_TILE_WIDTH + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>(availableWidth / columnPitch));

        if (!ImGui::BeginChild("asset_grid_scroll", ImVec2{0.0f, 0.0f}, false))
        {
            ImGui::EndChild();
            return;
        }

        if (ImGui::BeginTable("asset_grid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
        {
            for (int column = 0; column < columns; ++column)
            {
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, ASSET_TILE_WIDTH);
            }

            for (std::size_t slot = 0; slot < visibleAssets.size(); ++slot)
            {
                const std::size_t i = visibleAssets[slot];
                if (slot % static_cast<std::size_t>(columns) == 0)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, ASSET_TILE_HEIGHT);
                }
                ImGui::TableSetColumnIndex(static_cast<int>(slot % static_cast<std::size_t>(columns)));
                const auto& asset = assetEntries[i];
                const bool selected = selectedAssetIndex.has_value() && *selectedAssetIndex == i;

                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginGroup();
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    selected ? ImVec4{0.20f, 0.39f, 0.72f, 1.00f} : ImVec4{0.14f, 0.16f, 0.19f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.23f, 0.34f, 0.50f, 1.00f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});

                Texture2D* texture = i < assetThumbnails.size() ? &assetThumbnails[i].texture : nullptr;
                const bool clicked =
                    texture ? ImGui::ImageButton(
                                  "thumbnail",
                                  reinterpret_cast<ImTextureID>(texture),
                                  ImVec2{THUMBNAIL_SIZE, THUMBNAIL_SIZE},
                                  ImVec2{0.0f, 1.0f},
                                  ImVec2{1.0f, 0.0f},
                                  ImVec4{0.10f, 0.11f, 0.13f, 1.00f})
                            : ImGui::Button("No Preview", ImVec2{THUMBNAIL_SIZE, THUMBNAIL_SIZE});
                ImGui::PopStyleColor(3);

                if (clicked && onAssetSelectedCb)
                {
                    onAssetSelectedCb(i);
                }
                if (ImGui::IsItemHovered())
                {
                    const auto sourcePath = asset.sourcePath.string();
                    const auto tooltipPath = sourcePath.empty() ? asset.defaultsPath.string() : sourcePath;
                    ImGui::SetTooltip(
                        "%s\n%s\n%s",
                        asset.displayName.c_str(),
                        asset.modelKey.c_str(),
                        tooltipPath.c_str());
                }
                if (ImGui::BeginPopupContextItem("asset_context"))
                {
                    if (ImGui::MenuItem("Rename File")) openAssetRenamePopup(i);
                    if (ImGui::MenuItem("Copy Asset Name")) ImGui::SetClipboardText(asset.displayName.c_str());
                    if (ImGui::MenuItem("Copy Model Key")) ImGui::SetClipboardText(asset.modelKey.c_str());
                    const auto sourcePath = asset.sourcePath.string();
                    if (!sourcePath.empty() && ImGui::MenuItem("Copy Source Path"))
                    {
                        ImGui::SetClipboardText(sourcePath.c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ASSET_TILE_WIDTH);
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.72f, 0.83f, 1.00f, 1.00f});
                    ImGui::TextWrapped("%s", asset.displayName.c_str());
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::TextWrapped("%s", asset.displayName.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    void EditorGui::drawFlatpackGrid()
    {
        if (flatpackEntries.empty())
        {
            ImGui::TextDisabled("No flatpacks found");
            return;
        }

        DrawSearchFilter(flatpackFilter, "flatpack_filter", "Search...", ImGui::GetContentRegionAvail().x);
        ImGui::Spacing();

        // Compact the visible entries so filtered-out tiles don't leave gaps in the grid.
        std::vector<std::size_t> visibleFlatpacks;
        visibleFlatpacks.reserve(flatpackEntries.size());
        for (std::size_t i = 0; i < flatpackEntries.size(); ++i)
        {
            if (flatpackFilter.PassFilter(flatpackEntries[i].displayName.c_str()))
            {
                visibleFlatpacks.push_back(i);
            }
        }

        if (visibleFlatpacks.empty())
        {
            ImGui::TextDisabled("No flatpacks match the filter");
            return;
        }

        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float columnPitch = ASSET_TILE_WIDTH + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>(availableWidth / columnPitch));

        if (!ImGui::BeginChild("flatpack_grid_scroll", ImVec2{0.0f, 0.0f}, false))
        {
            ImGui::EndChild();
            return;
        }

        if (ImGui::BeginTable("flatpack_grid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
        {
            for (int column = 0; column < columns; ++column)
            {
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, ASSET_TILE_WIDTH);
            }

            for (std::size_t slot = 0; slot < visibleFlatpacks.size(); ++slot)
            {
                const std::size_t i = visibleFlatpacks[slot];
                if (slot % static_cast<std::size_t>(columns) == 0)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, FLATPACK_TILE_HEIGHT);
                }
                ImGui::TableSetColumnIndex(static_cast<int>(slot % static_cast<std::size_t>(columns)));
                const auto& flatpack = flatpackEntries[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginGroup();
                const bool clicked = ImGui::Button("Flatpack", ImVec2{ASSET_TILE_WIDTH, 56.0f});
                if (clicked && onFlatpackSelectedCb)
                {
                    onFlatpackSelectedCb(flatpack.path);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", flatpack.path.string().c_str());
                }
                if (ImGui::BeginPopupContextItem("flatpack_context"))
                {
                    const auto path = flatpack.path.string();
                    if (ImGui::MenuItem("Copy Flatpack Name")) ImGui::SetClipboardText(flatpack.displayName.c_str());
                    if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(path.c_str());
                    ImGui::EndPopup();
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ASSET_TILE_WIDTH);
                ImGui::TextWrapped("%s", flatpack.displayName.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    EditorGui::EditorGui(
        Settings* _settings,
        EditorDockLayout* _dockLayout,
        const std::vector<AssetEntry>& assets,
        const std::function<void(std::size_t)>& onAssetSelected,
        const std::function<AssetRenameResult(std::size_t, const std::string&)>& onAssetRename,
        const std::function<void(std::filesystem::path)>& onFlatpackSelected,
        const std::function<void(const SceneSelectionRequest&)>& onSceneObjectSelected,
        const std::function<void(const HierarchyMoveRequest&)>& onHierarchyMove,
        ModelDefaultCallbacks callbacks)
        : settings(_settings),
          dockLayout(_dockLayout),
          onAssetSelectedCb(onAssetSelected),
          onAssetRenameCb(onAssetRename),
          onFlatpackSelectedCb(onFlatpackSelected),
          onSceneObjectSelectedCb(onSceneObjectSelected),
          onHierarchyMoveCb(onHierarchyMove),
          modelDefaultCallbacks(std::move(callbacks))
    {
        assetEntries = assets;
        assetThumbnails.reserve(assetEntries.size());
        for (const auto& asset : assetEntries)
        {
            assetThumbnails.push_back(createAssetThumbnail(asset));
        }
    }

    EditorGui::~EditorGui()
    {
        for (auto& thumbnail : assetThumbnails)
        {
            if (thumbnail.id != 0)
            {
                UnloadRenderTexture(thumbnail);
            }
        }
    }
} // namespace sage::editor

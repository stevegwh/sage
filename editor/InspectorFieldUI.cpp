#include "InspectorFieldUI.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace sage::editor
{
    namespace
    {
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
            // Close the transaction on *any* deactivation, not only after an edit.
            // Activating a field (IsItemActivated) opens a transaction; if we only
            // closed it via IsItemDeactivatedAfterEdit, focusing a field and leaving
            // it unchanged (click away / Escape / no-op drag) would leak an open
            // transaction, leaving history->HasActiveTransaction() stuck true and the
            // scene permanently flagged "unsaved". Commit() discards no-op edits, so
            // committing an unchanged field is safe and produces no history entry.
            g_fieldEditCommitted |= ImGui::IsItemDeactivated();
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

    InspectorComponentsResult DrawInspectorComponents(const std::vector<InspectedComponent>& components)
    {
        g_fieldEditBegan = false;
        g_fieldEditCommitted = false;
        bool changed = false;
        for (const auto& component : components)
        {
            changed |= DrawInspectorComponent(component);
        }
        return {.changed = changed, .began = g_fieldEditBegan, .committed = g_fieldEditCommitted};
    }
} // namespace sage::editor

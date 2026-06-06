#include "EditorGui.hpp"

#include "engine/GameUiEngine.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"
#include "engine/ui/Scrollbar.hpp"
#include "engine/ui/UIElements.hpp"
#include "engine/ui/UILayout.hpp"
#include "engine/ui/UIWindow.hpp"
#include "engine/slib.hpp"
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
#include <memory>
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
        constexpr int ASSET_GRID_COLUMNS = 5;
        constexpr int ASSET_VISIBLE_ROWS = 2;
        constexpr float LEFT_DOCK_WIDTH = 340.0f;
        constexpr float RIGHT_DOCK_WIDTH = 440.0f;
        constexpr float ASSET_BROWSER_MARGIN = 18.0f;
        constexpr float ASSET_BROWSER_HEIGHT = 344.0f;
        constexpr float ASSET_BROWSER_WIDTH =
            1920.0f - LEFT_DOCK_WIDTH - RIGHT_DOCK_WIDTH - ASSET_BROWSER_MARGIN * 2.0f;
        constexpr Color EDITOR_WINDOW_BACKGROUND = {35, 38, 43, 245};
        constexpr Color EDITOR_TEXT = {230, 234, 240, 255};
        constexpr const char* HIERARCHY_DRAG_PAYLOAD = "SAGE_HIER_ENTITY";
        constexpr float BOTTOM_DOCK_TITLE_ROW_HEIGHT = 10.0f;
        constexpr float FLOATING_PANEL_TITLE_ROW_HEIGHT = 15.0f;
        constexpr Padding CONTENT_ROW_PADDING = {2, 0, 0, 0};
        constexpr Padding CONTENT_CELL_PADDING = {2, 6, 8, 8};

        ImVec4 ToImGuiColor(const Color color)
        {
            return {
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                static_cast<float>(color.a) / 255.0f};
        }

        std::uint32_t EntityPayloadId(const entt::entity entity)
        {
            return static_cast<std::uint32_t>(entt::to_integral(entity));
        }

        entt::entity EntityFromPayloadId(const std::uint32_t id)
        {
            return static_cast<entt::entity>(id);
        }

        template <class T>
        void CommitField(const LeafField<T>& field, const T& value)
        {
            if (field.setter)
                field.setter(value);
            else if (field.data)
                *field.data = value;
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
                [](const auto& field) {
                    using T = std::decay_t<decltype(field)>;
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
                result += FormatFieldValue(field.value);
            }
            return result;
        }

        void DrawFieldClipboardMenu(
            const std::string& value,
            const bool editable,
            const std::function<bool(std::string_view)>& paste)
        {
            if (!ImGui::BeginPopupContextItem("field_context")) return;

            if (ImGui::MenuItem("Copy Value"))
            {
                ImGui::SetClipboardText(value.c_str());
            }
            if (editable && ImGui::MenuItem("Paste Value"))
            {
                if (const char* clipboard = ImGui::GetClipboardText(); clipboard != nullptr && paste)
                {
                    (void)paste(clipboard);
                }
            }
            ImGui::EndPopup();
        }

        TextBox::FontInfo EditorTextFontInfo()
        {
            auto info = TextBox::FontInfo{};
            info.color = EDITOR_TEXT;
            return info;
        }

        TextBox::FontInfo EditorTitleFontInfo()
        {
            auto info = EditorTextFontInfo();
            info.font = ResourceManager::GetInstance().FontLoad("resources/fonts/NotoSans/NotoSans-ExtraBold.ttf");
            info.baseFontSize = 22;
            info.minFontSize = 18;
            return info;
        }

        class AssetThumbnailButton final : public ImageBox
        {
            std::optional<std::size_t> assetIndex;
            std::string label;
            RenderTexture2D* thumbnail{};
            std::optional<std::size_t>* selectedAssetIndex{};
            std::function<void(std::size_t)> onAssetSelected;

          public:
            void OnClick() override
            {
                if (assetIndex.has_value() && onAssetSelected) onAssetSelected(*assetIndex);
            }

            void SetAsset(
                const std::optional<std::size_t> index,
                std::string displayName = "",
                RenderTexture2D* assetThumbnail = nullptr)
            {
                assetIndex = index;
                label = std::move(displayName);
                thumbnail = assetThumbnail;
            }

            void UpdateDimensions() override
            {
                rec = {
                    parent->GetRec().x + parent->padding.left,
                    parent->GetRec().y + parent->padding.up,
                    parent->GetRec().width - parent->padding.left - parent->padding.right,
                    parent->GetRec().height - parent->padding.up - parent->padding.down};
            }

            void Draw2D() override
            {
                if (!assetIndex.has_value())
                {
                    DrawRectangleRec(rec, Color{39, 42, 47, 180});
                    DrawRectangleLinesEx(rec, 1.0f, Color{75, 82, 94, 180});
                    return;
                }

                const bool selected =
                    selectedAssetIndex && selectedAssetIndex->has_value() && **selectedAssetIndex == *assetIndex;
                const Color background = selected ? Color{221, 235, 255, 255} : Color{245, 247, 250, 255};
                const Color border = selected ? Color{37, 99, 235, 255} : Color{171, 181, 196, 255};

                DrawRectangleRec(rec, background);
                DrawRectangleLinesEx(rec, selected ? 3.0f : 1.0f, border);

                const float labelHeight = 28.0f;
                const float imageSize = std::max(0.0f, std::min(rec.width, rec.height - labelHeight - 6.0f));
                const Rectangle imageDest = {
                    rec.x + (rec.width - imageSize) * 0.5f, rec.y + 6.0f, imageSize, imageSize};

                if (thumbnail && thumbnail->texture.id != 0)
                {
                    DrawTexturePro(
                        thumbnail->texture,
                        {0.0f,
                         0.0f,
                         static_cast<float>(thumbnail->texture.width),
                         -static_cast<float>(thumbnail->texture.height)},
                        imageDest,
                        Vector2Zero(),
                        0.0f,
                        WHITE);
                }

                int fontSize = 16;
                const Font font =
                    ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-SemiBold.ttf");
                const int maxTextWidth = static_cast<int>(std::max(0.0f, rec.width - 12.0f));
                while (fontSize > 12 && MeasureTextEx(font, label.c_str(), fontSize, 1.0f).x > maxTextWidth)
                {
                    --fontSize;
                }
                const Vector2 textSize = MeasureTextEx(font, label.c_str(), fontSize, 1.0f);
                DrawTextEx(
                    font,
                    label.c_str(),
                    {rec.x + (rec.width - textSize.x) * 0.5f,
                     rec.y + rec.height - labelHeight + (labelHeight - textSize.y) * 0.5f},
                    fontSize,
                    1.0f,
                    BLACK);
            }

            AssetThumbnailButton(
                GameUIEngine* ui,
                TableCell* parent,
                std::optional<std::size_t>* selectedIndex,
                std::function<void(std::size_t)> callback)
                : ImageBox(
                      ui,
                      parent,
                      ResourceManager::GetInstance().TextureLoad("resources/transpixel.png"),
                      ImageBox::OverflowBehaviour::ALLOW_OVERFLOW),
                  selectedAssetIndex(selectedIndex),
                  onAssetSelected(std::move(callback))
            {
            }
        };

        class TextButton final : public TextBox
        {
            std::function<void()> onPressed;
            std::function<bool()> isVisible;

          public:
            void OnClick() override
            {
                if (isVisible && !isVisible()) return;
                if (onPressed) onPressed();
            }

            void UpdateDimensions() override
            {
                rec = {
                    parent->GetRec().x + parent->padding.left,
                    parent->GetRec().y + parent->padding.up,
                    parent->GetRec().width - parent->padding.left - parent->padding.right,
                    parent->GetRec().height - parent->padding.up - parent->padding.down};
            }

            void Draw2D() override
            {
                if (isVisible && !isVisible()) return;
                DrawRectangleRec(rec, Color{233, 238, 246, 255});
                DrawRectangleLinesEx(rec, 1.0f, Color{151, 164, 184, 255});
                const int fontSize = static_cast<int>(fontInfo.fontSize);
                const Vector2 textSize =
                    MeasureTextEx(fontInfo.font, GetContent().c_str(), fontSize, fontInfo.fontSpacing);
                DrawTextEx(
                    fontInfo.font,
                    GetContent().c_str(),
                    {rec.x + (rec.width - textSize.x) * 0.5f, rec.y + (rec.height - textSize.y) * 0.5f},
                    fontSize,
                    fontInfo.fontSpacing,
                    BLACK);
            }

            TextButton(
                GameUIEngine* ui,
                TableCell* parent,
                std::function<void()> callback,
                std::function<bool()> visiblePredicate = {})
                : TextBox(ui, parent, TextBox::FontInfo{}, VertAlignment::MIDDLE, HoriAlignment::CENTER),
                  onPressed(std::move(callback)),
                  isVisible(std::move(visiblePredicate))
            {
            }
        };

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

        template <class DrawFn>
        void DrawMaybeDisabled(const bool editable, DrawFn&& draw)
        {
            if (!editable) ImGui::BeginDisabled();
            draw();
            if (!editable) ImGui::EndDisabled();
        }

        void DrawInspectorFieldWidget(const LeafField<bool>& field, const bool editable)
        {
            bool value = field.data && *field.data;
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::Checkbox("##value", &value) && editable)
                {
                    CommitField(field, value);
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                bool parsed = false;
                if (!ParseBool(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<int>& field, const bool editable)
        {
            int value = field.data ? *field.data : 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragInt("##value", &value, 1.0f) && editable)
                {
                    CommitField(field, value);
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                int parsed = 0;
                if (!ParseScalar(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<unsigned int>& field, const bool editable)
        {
            unsigned int value = field.data ? *field.data : 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragScalar("##value", ImGuiDataType_U32, &value, 1.0f) && editable)
                {
                    CommitField(field, value);
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                unsigned int parsed = 0;
                if (!ParseScalar(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<std::uint64_t>& field, const bool editable)
        {
            std::uint64_t value = field.data ? *field.data : 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::InputScalar("##value", ImGuiDataType_U64, &value) && editable)
                {
                    CommitField(field, value);
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                std::uint64_t parsed = 0;
                if (!ParseScalar(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<float>& field, const bool editable)
        {
            float value = field.data ? *field.data : 0.0f;
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragFloat("##value", &value, 0.05f, 0.0f, 0.0f, "%.3f") && editable)
                {
                    CommitField(field, value);
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                float parsed = 0.0f;
                if (!ParseScalar(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<std::string>& field, const bool editable)
        {
            std::string value = field.data ? *field.data : std::string{};
            ImGui::SetNextItemWidth(-FLT_MIN);
            const auto flags = editable ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
            if (ImGui::InputText("##value", &value, flags) && editable)
            {
                CommitField(field, value);
            }
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                CommitField(field, std::string{text});
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<Vector2>& field, const bool editable)
        {
            float value[2] = {
                field.data ? field.data->x : 0.0f,
                field.data ? field.data->y : 0.0f};
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragFloat2("##value", value, 0.05f, 0.0f, 0.0f, "%.3f") && editable)
                {
                    CommitField(field, Vector2{value[0], value[1]});
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                Vector2 parsed{};
                if (!ParseVector2(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<Vector3>& field, const bool editable)
        {
            float value[3] = {
                field.data ? field.data->x : 0.0f,
                field.data ? field.data->y : 0.0f,
                field.data ? field.data->z : 0.0f};
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawMaybeDisabled(editable, [&]() {
                if (ImGui::DragFloat3("##value", value, 0.05f, 0.0f, 0.0f, "%.3f") && editable)
                {
                    CommitField(field, Vector3{value[0], value[1], value[2]});
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                Vector3 parsed{};
                if (!ParseVector3(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const LeafField<::Color>& field, const bool editable)
        {
            float value[4] = {
                field.data ? static_cast<float>(field.data->r) / 255.0f : 1.0f,
                field.data ? static_cast<float>(field.data->g) / 255.0f : 1.0f,
                field.data ? static_cast<float>(field.data->b) / 255.0f : 1.0f,
                field.data ? static_cast<float>(field.data->a) / 255.0f : 1.0f};
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
                    CommitField(field, Color{toByte(value[0]), toByte(value[1]), toByte(value[2]), toByte(value[3])});
                }
            });
            DrawFieldClipboardMenu(FormatLeafValue(field), editable, [field](const std::string_view text) {
                Color parsed{};
                if (!ParseColor(text, parsed)) return false;
                CommitField(field, parsed);
                return true;
            });
        }

        void DrawInspectorFieldWidget(const EnumField& field, const bool editable)
        {
            const auto currentIndex = field.getIndex ? field.getIndex() : 0;
            const char* currentLabel =
                currentIndex < field.options.size() ? field.options[currentIndex].c_str() : "";
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
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            });
            DrawFieldClipboardMenu(FormatEnumValue(field), editable, [field](const std::string_view text) {
                const auto pasted = TrimCopy(text);
                const auto it = std::ranges::find(field.options, pasted);
                if (it == field.options.end() || !field.setIndex) return false;
                field.setIndex(static_cast<std::size_t>(std::distance(field.options.begin(), it)));
                return true;
            });
        }

        void DrawInspectorFieldRow(const InspectorField& field)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(field.label.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(field.label.c_str());
            std::visit(
                [&](const auto& value) {
                    DrawInspectorFieldWidget(value, field.editable);
                },
                field.value);
            ImGui::PopID();
        }

        void DrawInspectorComponent(const InspectedComponent& component)
        {
            ImGui::PushID(component.displayName.c_str());
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
                        DrawInspectorFieldRow(field);
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
        }

    } // namespace

    void EditorGui::StartImGui()
    {
        rlImGuiBegin();
        imGuiEnabled = true;
    }

    void EditorGui::EndImGui()
    {
        rlImGuiEnd();
        imGuiEnabled = false;
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
        const ImVec2 windowPos{viewportOffset.x, viewportOffset.y + mainMenuHeight};
        const ImVec2 windowSize{
            settings->ScaleValueWidth(LEFT_DOCK_WIDTH), std::max(1.0f, viewport.y - mainMenuHeight)};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImGuiColor(EDITOR_WINDOW_BACKGROUND));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EDITOR_TEXT));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.26f, 0.38f, 0.90f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.23f, 0.34f, 0.50f, 0.95f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});

        std::optional<entt::entity> sceneSelectionRequest;
        std::optional<std::pair<entt::entity, entt::entity>> reparentRequest;

        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Hierarchy", nullptr, windowFlags))
        {
            if (hierarchyEntries.empty())
            {
                ImGui::TextDisabled("No scene objects");
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
                    const bool hasChildren = entryIndex + 1 < hierarchyEntries.size() &&
                                             hierarchyEntries[entryIndex + 1].depth > entry.depth;
                    const bool selected = selectedSceneEntity.has_value() && *selectedSceneEntity == entry.entity;

                    ImGuiTreeNodeFlags nodeFlags =
                        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                        ImGuiTreeNodeFlags_OpenOnDoubleClick;
                    if (selected) nodeFlags |= ImGuiTreeNodeFlags_Selected;
                    if (!hasChildren) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                    if (hasChildren)
                    {
                        if (focusedHierarchyEntity.has_value() &&
                            subtreeContainsEntity(entryIndex, *focusedHierarchyEntity))
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
                        sceneSelectionRequest = entry.entity;
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
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HIERARCHY_DRAG_PAYLOAD);
                            payload != nullptr && payload->Delivery &&
                            payload->DataSize == sizeof(std::uint32_t))
                        {
                            const auto dragged =
                                EntityFromPayloadId(*static_cast<const std::uint32_t*>(payload->Data));
                            if (dragged != entry.entity)
                            {
                                reparentRequest = std::pair{dragged, entry.entity};
                            }
                        }
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
            }
        }
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);

        if (sceneSelectionRequest.has_value() && onSceneObjectSelectedCb)
        {
            onSceneObjectSelectedCb(*sceneSelectionRequest);
        }
        if (reparentRequest.has_value() && onHierarchyReparentCb)
        {
            onHierarchyReparentCb(reparentRequest->first, reparentRequest->second);
        }
    }

    void EditorGui::DrawInspectorWindow()
    {
        if (!settings) return;

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float mainMenuHeight = ImGui::GetFrameHeight();
        const float width = settings->ScaleValueWidth(RIGHT_DOCK_WIDTH);
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
                    DrawInspectorComponent(component);
                }
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
    }

    void EditorGui::SetOverlayStatus(const std::string& mode, const std::string& cursor) const
    {
        modeStatus = mode;
        cursorStatus = cursor;
    }

    void EditorGui::SetAssetDefaultsStatus(
        const std::string& assetName,
        const std::string& modelDefaultHeight,
        const std::string& modelDefaultRotation,
        const std::string& modelDefaultScale) const
    {
        if (defaultsAssetText) defaultsAssetText->SetContent("Asset: " + assetName);
        if (defaultsPositionText) defaultsPositionText->SetContent(modelDefaultHeight);
        if (defaultsRotationText) defaultsRotationText->SetContent(modelDefaultRotation);
        if (defaultsScaleText) defaultsScaleText->SetContent(modelDefaultScale);
    }

    void EditorGui::SetSceneName(const std::string& sceneName) const
    {
        sceneNameStatus = sceneName;
    }

    void EditorGui::SetSelectedAsset(const std::optional<std::size_t> index)
    {
        selectedAssetIndex = index;
        if (!assetDefaultsWindow) return;
        if (selectedAssetIndex.has_value())
        {
            if (assetDefaultsWindow->IsHidden()) assetDefaultsWindow->Show();
        }
        else
        {
            if (!assetDefaultsWindow->IsHidden()) assetDefaultsWindow->Hide();
        }
    }

    void EditorGui::SetHierarchy(
        const std::vector<SceneObjectEntry>& entries, const std::optional<entt::entity> selectedEntity)
    {
        hierarchyEntries = entries;
        selectedSceneEntity = selectedEntity;
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
        if (!ui || !ui->settings) return;

        const auto renderViewport = ui->settings->GetRenderViewportScreenRect();
        const float x = renderViewport.x + ui->settings->ScaleValueWidth(16.0f);
        const float y = renderViewport.y + ui->settings->ScaleValueHeight(14.0f);
        const float maxWidth = std::max(1.0f, renderViewport.width - ui->settings->ScaleValueWidth(32.0f));
        const int titleSize = std::max(22, static_cast<int>(ui->settings->ScaleValueMaintainRatio(22.0f)));
        const int metaSize = std::max(16, static_cast<int>(ui->settings->ScaleValueMaintainRatio(16.0f)));
        const Font titleFont =
            ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-Bold.ttf");
        const Font metaFont =
            ResourceManager::GetInstance().FontLoad("resources/fonts/FiraCode/FiraCode-SemiBold.ttf");

        DrawTextFit(titleFont, sceneNameStatus, {x, y}, maxWidth, titleSize, EDITOR_TEXT);
        DrawTextFit(
            metaFont,
            "Mode: " + modeStatus + "  |  Cursor: " + cursorStatus,
            {x, y + ui->settings->ScaleValueHeight(28.0f)},
            maxWidth,
            metaSize,
            Color{202, 211, 224, 255});
    }

    void EditorGui::ShowDeleteConfirmation(const std::string& selectedEntity) const
    {
        if (deleteConfirmationText)
        {
            deleteConfirmationText->SetContent("Delete " + selectedEntity + "?");
        }
        if (deleteConfirmationWindow && deleteConfirmationWindow->IsHidden())
        {
            deleteConfirmationWindow->Show();
        }
    }

    void EditorGui::HideDeleteConfirmation() const
    {
        if (deleteConfirmationWindow && !deleteConfirmationWindow->IsHidden())
        {
            deleteConfirmationWindow->Hide();
        }
    }

    bool EditorGui::IsDeleteConfirmationVisible() const
    {
        return deleteConfirmationWindow && !deleteConfirmationWindow->IsHidden();
    }

    bool EditorGui::WantsMouseCapture() const
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    }

    bool EditorGui::WantsKeyboardCapture() const
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
    }

    EditorGui::DeleteConfirmationAction EditorGui::ConsumeDeleteConfirmationAction()
    {
        const auto action = pendingDeleteConfirmationAction;
        pendingDeleteConfirmationAction = DeleteConfirmationAction::None;
        return action;
    }

    void EditorGui::refreshAssetButtonContent()
    {
        const std::size_t scrollRow =
            assetWindow && assetWindow->GetScrollbar() ? assetWindow->GetScrollbar()->ScrollOffset() : 0;
        const std::size_t firstItemIndex = scrollRow * ASSET_GRID_COLUMNS;

        const std::size_t itemCount =
            (currentTab == BrowserTab::Assets) ? assetEntries.size() : flatpackEntries.size();

        for (std::size_t slot = 0; slot < assetButtons.size(); ++slot)
        {
            auto* button = dynamic_cast<AssetThumbnailButton*>(assetButtons[slot]);
            if (!button) continue;

            const std::size_t itemIndex = firstItemIndex + slot;
            if (itemIndex >= itemCount)
            {
                button->SetAsset(std::nullopt);
                continue;
            }

            if (currentTab == BrowserTab::Assets)
            {
                button->SetAsset(
                    itemIndex, assetEntries[itemIndex].displayName, &assetThumbnails.at(itemIndex));
            }
            else
            {
                // Flatpacks have no rendered thumbnail yet — just show the label
                // on a blank tile so the click target stays consistent.
                button->SetAsset(itemIndex, flatpackEntries[itemIndex].displayName, nullptr);
            }
        }
    }

    void EditorGui::SetFlatpacks(std::vector<FlatpackEntry> entries)
    {
        flatpackEntries = std::move(entries);
        if (auto* sb = assetWindow ? assetWindow->GetScrollbar() : nullptr) sb->ClampOffset();
        refreshAssetButtonContent();
    }

    RenderTexture2D EditorGui::createAssetThumbnail(const AssetEntry& asset) const
    {
        auto thumbnail = LoadRenderTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        auto model = ResourceManager::GetInstance().GetModelView(asset.modelKey);
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
        model.Draw(Vector3Zero(), {0.0f, 1.0f, 0.0f}, 0.0f, Vector3One(), WHITE);
        EndMode3D();
        EndTextureMode();

        return thumbnail;
    }

    void EditorGui::createAssetWindow(
        const std::vector<AssetEntry>& assets, const std::function<void(std::size_t)>& /*unused*/)
    {
        auto window = std::make_unique<WindowDocked>(
            settings,
            editorWindowBackgroundTexture,
            TextureStretchMode::STRETCH,
            LEFT_DOCK_WIDTH + ASSET_BROWSER_MARGIN,
            -ASSET_BROWSER_MARGIN,
            ASSET_BROWSER_WIDTH,
            ASSET_BROWSER_HEIGHT,
            VertAlignment::BOTTOM,
            HoriAlignment::LEFT,
            Padding{20, 16, 14, 14});

        assetWindow = ui->CreateWindowDocked(std::move(window));
        assetWindow->SetOverflowContingency(OverflowContingency::SCROLLBAR);
        auto* mainTable = assetWindow->CreateTable({0, 0, 4, 0});

        {
            auto* tabRow = mainTable->CreateTableRow(BOTTOM_DOCK_TITLE_ROW_HEIGHT);
            auto makeTab = [&](const char* label, BrowserTab tab) {
                auto* cell = tabRow->CreateTableCell(Padding{2, 2, 4, 4});
                auto button = std::make_unique<TextButton>(ui, cell, [this, tab]() {
                    if (currentTab == tab) return;
                    currentTab = tab;
                    if (auto* sb = assetWindow ? assetWindow->GetScrollbar() : nullptr)
                    {
                        sb->ClampOffset();
                    }
                    refreshAssetButtonContent();
                });
                browserTabButtons.push_back(cell->CreateTextbox(std::move(button), label));
            };
            makeTab("Assets", BrowserTab::Assets);
            makeTab("Flatpacks", BrowserTab::Flatpacks);
        }

        {
            auto* contentRow = mainTable->CreateTableRow(CONTENT_ROW_PADDING);
            auto* contentCell = contentRow->CreateTableCell(CONTENT_CELL_PADDING);
            auto* table = contentCell->CreateTable();

            // Dispatch click to either the asset or flatpack callback depending
            // on which tab is active when the click arrives.
            auto onBrowserItemSelected = [this](std::size_t index) {
                if (currentTab == BrowserTab::Assets)
                {
                    if (onAssetSelectedCb) onAssetSelectedCb(index);
                }
                else
                {
                    if (index >= flatpackEntries.size() || !onFlatpackSelectedCb) return;
                    onFlatpackSelectedCb(flatpackEntries[index].path);
                }
            };

            assetButtons.clear();
            assetButtons.reserve(ASSET_VISIBLE_ROWS * ASSET_GRID_COLUMNS);
            for (int rowIndex = 0; rowIndex < ASSET_VISIBLE_ROWS; ++rowIndex)
            {
                auto* row = table->CreateTableRow(Padding{0, 0, 0, 0});
                for (int colIndex = 0; colIndex < ASSET_GRID_COLUMNS; ++colIndex)
                {
                    auto* cell = row->CreateTableCell(Padding{5, 5, 5, 5});
                    auto thumbnail = std::make_unique<AssetThumbnailButton>(
                        ui, cell, &selectedAssetIndex, onBrowserItemSelected);
                    assetButtons.push_back(cell->CreateImagebox(std::move(thumbnail)));
                }
            }
        }

        if (auto* sb = assetWindow->GetScrollbar())
        {
            sb->SetProviders(
                [this]() {
                    const std::size_t count = (currentTab == BrowserTab::Assets) ? assetEntries.size()
                                                                                 : flatpackEntries.size();
                    return (count + ASSET_GRID_COLUMNS - 1) / ASSET_GRID_COLUMNS;
                },
                []() { return static_cast<std::size_t>(ASSET_VISIBLE_ROWS); });
            assetScrollSub = sb->onScrollChanged.Subscribe([this]() { refreshAssetButtonContent(); });
        }

        assetWindow->FinalizeLayout();
        refreshAssetButtonContent();
    }

    void EditorGui::createAssetDefaultsWindow()
    {
        auto window = std::make_unique<WindowDocked>(
            settings,
            editorWindowBackgroundTexture,
            TextureStretchMode::STRETCH,
            LEFT_DOCK_WIDTH + ASSET_BROWSER_MARGIN,
            -(ASSET_BROWSER_MARGIN + ASSET_BROWSER_HEIGHT + 12.0f),
            392.0f,
            232.0f,
            VertAlignment::BOTTOM,
            HoriAlignment::LEFT,
            Padding{20, 16, 14, 14});

        assetDefaultsWindow = ui->CreateWindowDocked(std::move(window));
        auto* mainTable = assetDefaultsWindow->CreateTable({0, 0, 4, 0});

        {
            auto* titleRow = mainTable->CreateTableRow(FLOATING_PANEL_TITLE_ROW_HEIGHT);
            auto* titleCell = titleRow->CreateTableCell();
            auto title = std::make_unique<TitleBar>(ui, titleCell, EditorTitleFontInfo());
            titleCell->CreateTitleBar(std::move(title), "Asset Defaults");
        }

        {
            auto* contentRow = mainTable->CreateTableRow(CONTENT_ROW_PADDING);
            auto* contentCell = contentRow->CreateTableCell(CONTENT_CELL_PADDING);
            auto* table = contentCell->CreateTable();

            auto addLine = [this, table](const char* text) {
                auto* row = table->CreateTableRow();
                auto* cell = row->CreateTableCell();
                auto label = std::make_unique<TextBox>(ui, cell, EditorTextFontInfo());
                return cell->CreateTextbox(std::move(label), text);
            };

            defaultsAssetText = addLine("Asset: None");

            auto addControlRow = [this, table](
                                     const char* labelText,
                                     const char* initialValue,
                                     const std::function<void()>& onDown,
                                     const std::function<void()>& onUp) {
                auto* row = table->CreateTableRow();

                auto* labelCell = row->CreateTableCell(36.0f);
                auto label = std::make_unique<TextBox>(ui, labelCell, EditorTextFontInfo());
                labelCell->CreateTextbox(std::move(label), labelText);

                auto* downCell = row->CreateTableCell(15.0f, Padding{1, 1, 2, 2});
                auto downButton = std::make_unique<TextButton>(ui, downCell, onDown);
                downCell->CreateTextbox(std::move(downButton), "-");

                auto* valueCell = row->CreateTableCell(34.0f);
                auto value = std::make_unique<TextBox>(
                    ui, valueCell, EditorTextFontInfo(), VertAlignment::TOP, HoriAlignment::CENTER);
                auto* valueText = valueCell->CreateTextbox(std::move(value), initialValue);

                auto* upCell = row->CreateTableCell(15.0f, Padding{1, 1, 2, 2});
                auto upButton = std::make_unique<TextButton>(ui, upCell, onUp);
                upCell->CreateTextbox(std::move(upButton), "+");

                return valueText;
            };

            defaultsPositionText =
                addControlRow("Z", "0.00", modelDefaultCallbacks.heightDown, modelDefaultCallbacks.heightUp);
            defaultsRotationText =
                addControlRow("Rot Y", "0", modelDefaultCallbacks.rotationDown, modelDefaultCallbacks.rotationUp);
            defaultsScaleText =
                addControlRow("Scale", "1.00", modelDefaultCallbacks.scaleDown, modelDefaultCallbacks.scaleUp);

            auto* buttonRow = table->CreateTableRow();

            auto* applyCell = buttonRow->CreateTableCell(50.0f, Padding{3, 3, 4, 4});
            auto applyButton = std::make_unique<TextButton>(ui, applyCell, modelDefaultCallbacks.apply);
            applyCell->CreateTextbox(std::move(applyButton), "Apply");

            auto* resetCell = buttonRow->CreateTableCell(50.0f, Padding{3, 3, 4, 4});
            auto resetButton = std::make_unique<TextButton>(ui, resetCell, modelDefaultCallbacks.reset);
            resetCell->CreateTextbox(std::move(resetButton), "Reset");
        }

        assetDefaultsWindow->FinalizeLayout();
        assetDefaultsWindow->Hide();
    }

    void EditorGui::createDeleteConfirmationWindow()
    {
        auto window = std::make_unique<WindowDocked>(
            settings,
            editorWindowBackgroundTexture,
            TextureStretchMode::STRETCH,
            0.0f,
            0.0f,
            420.0f,
            164.0f,
            VertAlignment::MIDDLE,
            HoriAlignment::CENTER,
            Padding{20, 16, 14, 14});

        deleteConfirmationWindow = ui->CreateWindowDocked(std::move(window));
        auto* mainTable = deleteConfirmationWindow->CreateTable({0, 0, 4, 0});

        {
            auto* titleRow = mainTable->CreateTableRow(FLOATING_PANEL_TITLE_ROW_HEIGHT);
            auto* titleCell = titleRow->CreateTableCell();
            auto title = std::make_unique<TitleBar>(ui, titleCell, EditorTitleFontInfo());
            titleCell->CreateTitleBar(std::move(title), "Confirm Delete");
        }

        {
            auto* textRow = mainTable->CreateTableRow(44.0f);
            auto* textCell = textRow->CreateTableCell({8, 8, 8, 8});
            auto text = std::make_unique<TextBox>(
                ui, textCell, EditorTextFontInfo(), VertAlignment::MIDDLE, HoriAlignment::CENTER);
            deleteConfirmationText = textCell->CreateTextbox(std::move(text), "Delete selected entity?");
        }

        {
            auto* buttonRow = mainTable->CreateTableRow(34.0f);

            auto* confirmCell = buttonRow->CreateTableCell(50.0f, Padding{3, 3, 4, 4});
            auto confirmButton = std::make_unique<TextButton>(ui, confirmCell, [this]() {
                pendingDeleteConfirmationAction = DeleteConfirmationAction::Confirm;
            });
            confirmCell->CreateTextbox(std::move(confirmButton), "Delete");

            auto* cancelCell = buttonRow->CreateTableCell(50.0f, Padding{3, 3, 4, 4});
            auto cancelButton = std::make_unique<TextButton>(
                ui, cancelCell, [this]() { pendingDeleteConfirmationAction = DeleteConfirmationAction::Cancel; });
            cancelCell->CreateTextbox(std::move(cancelButton), "Cancel");
        }

        deleteConfirmationWindow->FinalizeLayout();
        deleteConfirmationWindow->Hide();
    }

    EditorGui::EditorGui(
        GameUIEngine* _ui,
        Settings* _settings,
        const std::vector<AssetEntry>& assets,
        const std::function<void(std::size_t)>& onAssetSelected,
        const std::function<void(std::filesystem::path)>& onFlatpackSelected,
        const std::function<void(entt::entity)>& onSceneObjectSelected,
        const std::function<void(entt::entity, entt::entity)>& onHierarchyReparent,
        ModelDefaultCallbacks callbacks)
        : ui(_ui),
          settings(_settings),
          onAssetSelectedCb(onAssetSelected),
          onFlatpackSelectedCb(onFlatpackSelected),
          onSceneObjectSelectedCb(onSceneObjectSelected),
          onHierarchyReparentCb(onHierarchyReparent),
          modelDefaultCallbacks(std::move(callbacks))
    {
        Image panelImage = GenImageColor(1, 1, EDITOR_WINDOW_BACKGROUND);
        editorWindowBackgroundTexture = LoadTextureFromImage(panelImage);
        UnloadImage(panelImage);

        assetEntries = assets;
        assetThumbnails.reserve(assetEntries.size());
        for (const auto& asset : assetEntries)
        {
            assetThumbnails.push_back(createAssetThumbnail(asset));
        }

        createAssetWindow(assets, onAssetSelected);
        createAssetDefaultsWindow();
        createDeleteConfirmationWindow();
    }

    EditorGui::~EditorGui()
    {
        if (editorWindowBackgroundTexture.id != 0)
        {
            UnloadTexture(editorWindowBackgroundTexture);
        }

        for (auto& thumbnail : assetThumbnails)
        {
            if (thumbnail.id != 0)
            {
                UnloadRenderTexture(thumbnail);
            }
        }
    }
} // namespace sage::editor

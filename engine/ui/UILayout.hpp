//
// Layout container hierarchy: TableElement (abstract base), TableCell, TableRow,
// Table, and the *Grid variants. Builds the tree that Window owns.
//

#pragma once

#include "UIBase.hpp"
#include "UIElements.hpp"

#include "raylib.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sage
{
    class TableGrid;
    class TableCell;
    class TableRow;
    class Table;
    class Window;
    struct Settings;

    class TableElement : public UIElement
    {
      protected:
        struct UnscaledDimensions
        {
            Rectangle rec{};
            Padding padding{};
            Vector2 scalePosOffset{}; // The offset between the old and new screen size
        };

        TextureStretchMode textureStretchMode = TextureStretchMode::NONE;
        virtual void Reset();

      public:
        Padding padding;
        UnscaledDimensions unscaledDimensions{};
        TableElement* parent{};
        std::vector<std::unique_ptr<TableElement>> children;
        std::optional<std::unique_ptr<CellElement>> element;
        std::optional<Texture> tex{};
        std::optional<NPatchInfo> nPatchInfo{};

        virtual CellElement* GetCellUnderCursor(Vector2 mousePos);
        [[nodiscard]] virtual bool CapturesCursor(Vector2 point) const;
        void OnHoverStop() override;
        virtual void Update();
        virtual void ScaleContents(Settings* _settings);
        virtual void SetPos(float x, float y);
        void SetDimensions(float w, float h);
        void SetTexture(const Texture& _tex, TextureStretchMode _stretchMode);
        void UpdateTextureDimensions();
        virtual void FinalizeLayout();
        virtual void InitLayout() = 0;
        virtual void DrawDebug2D();
        virtual void Draw2D();
        [[nodiscard]] Window* GetWindow();

        TableElement(TableElement* _parent, float x, float y, float width, float height, Padding _padding);
        TableElement(TableElement* _parent, Padding _padding);

        TableElement(const TableElement&) = delete;
        TableElement(TableElement&&) noexcept = default;
        TableElement& operator=(const TableElement&) = delete;
        TableElement& operator=(TableElement&&) noexcept = default;
        ~TableElement() override = default;
    };

    class TableCell : public TableElement
    {
      protected:
        float requestedWidth{};
        bool autoSize = true;

        // Attaches el as this cell's single element and lays it out.
        template <typename T>
        T* attachElement(std::unique_ptr<T> el)
        {
            T* raw = el.get();
            element = std::move(el);
            InitLayout();
            return raw;
        }

      public:
        // The engine that owns this cell's window. Valid for windows created
        // through GameUIEngine::CreateWindow* (asserts otherwise).
        [[nodiscard]] GameUIEngine* GetEngine();

        // Constructs a CellElement subclass in place as this cell's element:
        // T(engine, this, args...). Use this for element types without a
        // dedicated Create* method below (e.g. game-side subclasses).
        template <typename T, typename... Args>
        T* CreateElement(Args&&... args)
        {
            return attachElement(std::make_unique<T>(GetEngine(), this, std::forward<Args>(args)...));
        }

        TextBox* CreateTextbox(
            const std::string& _content,
            const TextBox::FontInfo& _fontInfo = {},
            VertAlignment _vertAlignment = VertAlignment::TOP,
            HoriAlignment _horiAlignment = HoriAlignment::LEFT);
        Button* CreateButton(
            const std::string& _label,
            std::function<void()> _onPress = {},
            const TextBox::FontInfo& _fontInfo = {},
            VertAlignment _vertAlignment = VertAlignment::MIDDLE,
            HoriAlignment _horiAlignment = HoriAlignment::CENTER);
        Checkbox* CreateCheckbox(
            bool _checked = false,
            VertAlignment _vertAlignment = VertAlignment::MIDDLE,
            HoriAlignment _horiAlignment = HoriAlignment::CENTER);
        DropdownList* CreateDropdownList(
            std::vector<std::string> _options = {},
            std::size_t _selectedIndex = 0,
            const TextBox::FontInfo& _fontInfo = {},
            VertAlignment _vertAlignment = VertAlignment::MIDDLE,
            HoriAlignment _horiAlignment = HoriAlignment::LEFT);
        TitleBar* CreateTitleBar(const std::string& _title, const TextBox::FontInfo& _fontInfo = {});
        ImageBox* CreateImagebox(
            const Texture& _tex,
            ImageBox::OverflowBehaviour _behaviour = ImageBox::OverflowBehaviour::SHRINK_TO_FIT,
            VertAlignment _vertAlignment = VertAlignment::TOP,
            HoriAlignment _horiAlignment = HoriAlignment::LEFT);
        CloseButton* CreateCloseButton(const Texture& _tex, bool _closeDeletesWindow = false);
        GameWindowButton* CreateGameWindowButton(const Texture& _tex, Window* _toOpen);
        TableGrid* CreateTableGrid(int rows, int cols, float cellSpacing = 0, Padding _padding = {0, 0, 0, 0});
        Table* CreateTable(Padding _padding = {0, 0, 0, 0});
        Table* CreateTable(Percent _requestedHeight, Padding _padding = {0, 0, 0, 0});
        void InitLayout() override;
        ~TableCell() override = default;
        explicit TableCell(TableRow* _parent, Padding _padding = {0, 0, 0, 0});
        friend class TableRow;
    };

    class TableRow : public TableElement
    {
        float requestedHeight{};
        bool autoSize = true;

      public:
        TableCell* CreateTableCell(Padding _padding = {0, 0, 0, 0});
        TableCell* CreateTableCell(Percent _requestedWidth, Padding _padding = {0, 0, 0, 0});
        void InitLayout() override;
        ~TableRow() override = default;
        explicit TableRow(Table* _parent, Padding _padding = {0, 0, 0, 0});
        friend class Table;
    };

    class TableRowGrid final : public TableRow
    {
        float cellSpacing = 0;

      public:
        void InitLayout() override;
        explicit TableRowGrid(Table* _parent, Padding _padding = {0, 0, 0, 0});
        friend class Table;
    };

    class Table : public TableElement
    {
        float requestedHeight{};
        bool autoSize = true;

      public:
        TableRowGrid* CreateTableRowGrid(int cols, float cellSpacing, Padding _padding);
        TableRow* CreateTableRow(Padding _padding = {0, 0, 0, 0});
        TableRow* CreateTableRow(Percent _requestedHeight, Padding _padding = {0, 0, 0, 0});
        void InitLayout() override;
        ~Table() override = default;
        explicit Table(Window* _parent, Padding _padding = {0, 0, 0, 0});
        explicit Table(TableCell* _parent, Padding _padding = {0, 0, 0, 0});
        friend class Window;
        friend class TableCell;
    };

    class TableGrid final : public Table
    {
        float cellSpacing = 0;

      public:
        void InitLayout() override;
        explicit TableGrid(Window* _parent, Padding _padding = {0, 0, 0, 0});
        explicit TableGrid(TableCell* _parent, Padding _padding = {0, 0, 0, 0});
        friend class Window;
        friend class TableCell;
    };

    // Per-child input to distributeAlong: percentage-based or auto.
    struct SizeRequest
    {
        bool autoSize;
        float requestedPercent;
    };

    // Two-pass percentage size distribution along one axis. Auto-sized children
    // share the leftover percent equally; explicit-percent children take their
    // requested share. Returned sizes are ceil-rounded.
    std::vector<float> distributeAlong(float availableSize, const std::vector<SizeRequest>& requests);
} // namespace sage

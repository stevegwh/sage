//
// UIBuilder implementation. See UIBuilder.hpp.
//

#include "UIBuilder.hpp"

#include "../Settings.hpp"
#include "GameUIEngine.hpp"
#include "UIElements.hpp"
#include "UILayout.hpp"
#include "UIWindow.hpp"

#include <cassert>
#include <utility>

namespace sage
{
    FramedWindow CreateFramedWindow(GameUIEngine* engine, const std::string& title, WindowOptions opts)
    {
        const auto& theme = engine->Theme();
        auto* settings = engine->settings;
        const float width = settings->TARGET_SCREEN_WIDTH * (opts.width.value / 100.0f);
        const float height = settings->TARGET_SCREEN_HEIGHT * (opts.height.value / 100.0f);
        const Rectangle rect{
            (settings->TARGET_SCREEN_WIDTH - width) / 2.0f,
            (settings->TARGET_SCREEN_HEIGHT - height) / 2.0f,
            width,
            height};

        Window* window =
            opts.framed
                ? engine->CreateWindow(
                      settings, theme.frameTexture, theme.frameStretchMode, rect, theme.windowPadding)
                : engine->CreateWindow(settings, rect, theme.windowPadding);

        auto* table = window->CreateTable();
        TableCell* body = nullptr;
        if (opts.titleBar)
        {
            auto* titleRow = table->CreateTableRow(Percent{theme.titleBarHeightPercent});
            if (opts.closeButton)
            {
                titleRow->CreateTableCell(Percent{80})->CreateTitleBar(title, theme.font);
                titleRow->CreateTableCell(Percent{20})->CreateCloseButton(theme.closeButtonTexture);
            }
            else
            {
                titleRow->CreateTableCell()->CreateTitleBar(title, theme.font);
            }
            body = table->CreateTableRow(theme.bodyPadding)->CreateTableCell();
        }
        else
        {
            body = table->CreateTableRow()->CreateTableCell();
        }
        return {window, body};
    }

    UIBuilder::UIBuilder(GameUIEngine* _engine) : engine(_engine)
    {
    }

    UIBuilder::Frame& UIBuilder::top()
    {
        assert(!stack.empty()); // call Window() before adding content
        return stack.back();
    }

    TableCell* UIBuilder::nextCell()
    {
        Frame& f = top();
        switch (f.kind)
        {
        case Kind::Body:
            return f.bodyCell;
        case Kind::Column:
        {
            auto* row = f.table->CreateTableRow(engine->Theme().itemSpacing);
            return row->CreateTableCell(engine->Theme().itemCellPadding);
        }
        case Kind::Row:
            return f.row->CreateTableCell(engine->Theme().itemCellPadding);
        case Kind::Grid:
        {
            const int idx = f.count++;
            auto* gridRow = static_cast<TableRow*>(f.table->children[idx / f.cols].get());
            return static_cast<TableCell*>(gridRow->children[idx % f.cols].get());
        }
        }
        return nullptr;
    }

    void UIBuilder::ensureContainer()
    {
        // Vertical stacking is the default when a leaf is added straight into
        // the window body.
        if (top().kind == Kind::Body) Column();
    }

    UIBuilder& UIBuilder::Window(const std::string& title, WindowOptions opts)
    {
        auto framed = CreateFramedWindow(engine, title, opts);
        window = framed.window;
        stack.clear();
        stack.push_back(Frame{Kind::Body, framed.body});
        return *this;
    }

    UIBuilder& UIBuilder::Column()
    {
        auto* table = nextCell()->CreateTable();
        stack.push_back(Frame{Kind::Column, nullptr, table});
        return *this;
    }

    UIBuilder& UIBuilder::Row()
    {
        auto* table = nextCell()->CreateTable();
        auto* row = table->CreateTableRow();
        stack.push_back(Frame{Kind::Row, nullptr, table, row});
        return *this;
    }

    UIBuilder& UIBuilder::Grid(int rows, int cols, float cellSpacing)
    {
        Table* grid = nextCell()->CreateTableGrid(rows, cols, cellSpacing);
        stack.push_back(Frame{Kind::Grid, nullptr, grid, nullptr, cols, 0});
        return *this;
    }

    UIBuilder& UIBuilder::Label(const std::string& text)
    {
        ensureContainer();
        nextCell()->CreateTextbox(text, engine->Theme().font);
        return *this;
    }

    UIBuilder& UIBuilder::Button(const std::string& label, std::function<void()> onPress)
    {
        ensureContainer();
        nextCell()->CreateButton(label, std::move(onPress), engine->Theme().font);
        return *this;
    }

    UIBuilder& UIBuilder::Checkbox(bool checked, std::function<void(bool)> onChange)
    {
        ensureContainer();
        auto* box = nextCell()->CreateCheckbox(checked);
        if (onChange) box->onValueChanged.Subscribe(std::move(onChange));
        return *this;
    }

    UIBuilder& UIBuilder::Dropdown(
        std::vector<std::string> options,
        std::size_t selected,
        std::function<void(std::size_t, const std::string&)> onChange)
    {
        ensureContainer();
        auto* dropdown = nextCell()->CreateDropdownList(std::move(options), selected, engine->Theme().font);
        if (onChange) dropdown->onSelectionChanged.Subscribe(std::move(onChange));
        return *this;
    }

    UIBuilder& UIBuilder::Image(const Texture& tex)
    {
        ensureContainer();
        nextCell()->CreateImagebox(tex);
        return *this;
    }

    UIBuilder& UIBuilder::End()
    {
        if (top().kind != Kind::Body) stack.pop_back();
        return *this;
    }

    Window* UIBuilder::Finalize()
    {
        while (stack.size() > 1)
            stack.pop_back();
        if (window) window->FinalizeLayout();
        return window;
    }

    UIBuilder GameUIEngine::Build()
    {
        return UIBuilder{this};
    }
} // namespace sage

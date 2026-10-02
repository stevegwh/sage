#include "CanvasEditor.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    sage::json::Document command(sage::editor::CanvasEditor& editor, const char* action)
    {
        auto request = editor.Inspect();
        sage::json::Put(request, "action", action, request.GetAllocator());
        return request;
    }
} // namespace

int main()
{
    const auto originalDirectory = std::filesystem::current_path();
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("sage-canvas-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try
    {
        std::filesystem::create_directories(temporary / "resources");
        std::filesystem::current_path(temporary);
        sage::CanvasDocument document;
        document.nodes.front().script = "Missing.Script";
        document.nodes.front().references.push_back({"Unfinished", 999});
        document.Save("resources/first.canvas");
        document.nodes.front().name = "Second";
        document.Save("resources/second.canvas");

        sage::editor::CanvasEditor editor({});
        editor.Open("resources/first.canvas");
        require(editor.IsActive(), "Canvas did not open");
        document.nodes.front().name = "Draft";
        auto edit = command(editor, "replace");
        sage::json::Put(edit, "document", sage::json::Encode(document), edit.GetAllocator());
        editor.Command(edit);
        require(editor.IsDirty(), "Editing did not mark the canvas dirty");
        editor.ShowScene();
        require(
            !editor.IsActive() && editor.HasDocument() && editor.IsDirty(),
            "Returning to the scene discarded the canvas draft");
        editor.Open(temporary / "resources/first.canvas");
        require(editor.IsActive() && editor.IsDirty(), "Opening the same asset lost the draft");
        editor.Command(command(editor, "undo"));
        require(!editor.IsDirty(), "Scene switching lost undo history");
        editor.Command(command(editor, "redo"));
        require(editor.IsDirty(), "Scene switching lost redo history");
        editor.Command(command(editor, "save"));
        require(!editor.IsDirty(), "Incomplete script bindings blocked Save");
        const auto saved = sage::CanvasDocument::Load("resources/first.canvas");
        require(
            saved.nodes.front().name == "Draft" && saved.nodes.front().references.front().node == 999,
            "Saving a draft discarded its content or broken bindings");
        editor.Open("resources/second.canvas");
        require(editor.Path() == "resources/second.canvas", "Clean canvas could not switch directly");
        editor.Command(command(editor, "undo"));
        require(!editor.IsDirty(), "Undo history leaked between canvas assets");
        const auto window = document.Add(1, sage::UINodeKind::Window);
        document.Find(window)->get().windowHorizontal = sage::WindowHorizontalAlignment::CENTER;
        document.Find(window)->get().windowVertical = sage::WindowVerticalAlignment::MIDDLE;
        auto alignmentEdit = command(editor, "replace");
        sage::json::Put(alignmentEdit, "document", sage::json::Encode(document), alignmentEdit.GetAllocator());
        editor.Command(alignmentEdit);
        editor.Command(command(editor, "undo"));
        require(!editor.IsDirty(), "Undo did not restore the document before window alignment");
        editor.Command(command(editor, "redo"));
        editor.Command(command(editor, "save"));
        const auto aligned = sage::CanvasDocument::Load("resources/second.canvas");
        require(
            aligned.Find(window)->get().windowHorizontal == sage::WindowHorizontalAlignment::CENTER &&
                aligned.Find(window)->get().windowVertical == sage::WindowVerticalAlignment::MIDDLE,
            "Editor replacement, undo, redo or saving lost window alignment");
        editor.New();
        const auto newPath = editor.Path();
        editor.ShowScene();
        editor.Open("resources/first.canvas");
        require(
            editor.IsActive() && editor.IsDirty() && editor.Path() == newPath,
            "Switching assets discarded an unsaved canvas without confirmation");
        std::cout << "Canvas draft saving, scene switching, history and unsaved protection passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::filesystem::current_path(originalDirectory);
    std::filesystem::remove_all(temporary);
    return result;
}

#include "EditorApplication.hpp"

int main()
{
    sage::EditorApplication editor;
    return editor.Update() ? 0 : 1;
}

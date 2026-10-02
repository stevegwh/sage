#include "CanvasEditor.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <fstream>
#include <iterator>
#include <regex>

namespace sage::editor
{
    std::vector<std::string> CanvasEditor::exposedFields(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path)) return {};
        const auto source = json::Read(path);
        const std::regex expression(R"(\[Expose\]\s*public\s+(?:Sage\.)?Entity\s+(\w+))");
        std::vector<std::string> fields;
        for (auto match = std::sregex_iterator(source.begin(), source.end(), expression);
             match != std::sregex_iterator();
             ++match)
            fields.push_back(match->str(1));
        return fields;
    }
    void CanvasEditor::scanAssets()
    {
        textures.clear();
        fonts.clear();
        scriptSources.clear();
        for (const auto& file : std::filesystem::recursive_directory_iterator("resources"))
            if (file.is_regular_file())
            {
                const auto extension = file.path().extension().string();
                if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
                    textures.push_back(file.path());
                if (extension == ".ttf" || extension == ".otf") fonts.push_back(file.path());
            }
        std::ranges::sort(textures);
        std::ranges::sort(fonts);
        if (std::filesystem::exists(scripts.sourceDirectory))
            for (const auto& file : std::filesystem::recursive_directory_iterator(scripts.sourceDirectory))
                if (file.is_regular_file() && file.path().extension() == ".cs")
                {
                    const auto source = json::Read(file.path());
                    std::smatch ns, type;
                    if (std::regex_search(source, ns, std::regex(R"(namespace\s+([\w.]+))")) &&
                        std::regex_search(source, type, std::regex(R"(class\s+(\w+)\s*:\s*(?:Sage\.)?Script\b)")))
                        scriptSources[ns.str(1) + "." + type.str(1)] = file.path();
                }
    }
    void CanvasEditor::drawBehaviour(CanvasNode& node)
    {
        ImGui::SeparatorText("Behaviour");
        if (ImGui::BeginCombo("Script", node.script.empty() ? "None" : node.script.c_str()))
        {
            if (ImGui::Selectable("None", node.script.empty()))
            {
                node.script.clear();
                node.references.clear();
            }
            for (const auto& [name, source] : scriptSources)
                if (ImGui::Selectable(name.c_str(), node.script == name) && node.script != name)
                {
                    node.script = name;
                    node.references.clear();
                }
            ImGui::EndCombo();
        }
        if (scripts.IsConfigured() && ImGui::Button("New script...")) ImGui::OpenPopup("Create UI script");
        if (ImGui::BeginPopup("Create UI script"))
        {
            ImGui::InputText("Class name", &newScriptName);
            if (ImGui::Button("Create and attach"))
            {
                try
                {
                    if (!std::regex_match(newScriptName, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                        throw std::runtime_error("Enter a valid C# class name");
                    const auto source = scripts.sourceDirectory / (newScriptName + ".cs");
                    if (std::filesystem::exists(source)) throw std::runtime_error("That script already exists");
                    std::ofstream output(source);
                    output << "using Sage;\n\nnamespace " << scripts.rootNamespace << ";\n\npublic sealed class "
                           << newScriptName
                           << " : Script\n{\n    // Add [Expose] public Entity fields to connect canvas nodes in "
                              "the inspector.\n"
                              "    protected override void Start() { }\n    protected override void Update(float "
                              "deltaTime) { }\n}\n";
                    output.close();
                    if (!output) throw std::runtime_error("Could not save script");
                    node.script = scripts.rootNamespace + "." + newScriptName;
                    node.references.clear();
                    scanAssets();
                    OpenURL(("file://" + std::filesystem::absolute(source).string()).c_str());
                    ImGui::CloseCurrentPopup();
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                }
            }
            ImGui::EndPopup();
        }
        if (!node.script.empty())
        {
            if (scriptSources.contains(node.script))
            {
                if (ImGui::Button("Open script"))
                    OpenURL(
                        ("file://" + std::filesystem::absolute(scriptSources.at(node.script)).string()).c_str());
                ImGui::SameLine();
                if (ImGui::Button("Refresh scripts")) scanAssets();
                const auto fields = exposedFields(scriptSources.at(node.script));
                for (const auto& field : fields)
                {
                    auto ref =
                        std::ranges::find_if(node.references, [&](const auto& r) { return r.field == field; });
                    unsigned int target = ref == node.references.end() ? 0 : ref->node;
                    const auto referenced = document.Find(target);
                    if (ImGui::BeginCombo(
                            field.c_str(), referenced ? referenced->get().name.c_str() : "Assign node"))
                    {
                        for (const auto& candidate : document.nodes)
                        {
                            ImGui::PushID(static_cast<int>(candidate.id));
                            if (ImGui::Selectable(candidate.name.c_str(), candidate.id == target))
                            {
                                if (ref == node.references.end())
                                    node.references.push_back({.field = field, .node = candidate.id});
                                else
                                    ref->node = candidate.id;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    if (!referenced) ImGui::TextColored({1, .65f, .25f, 1}, "Required before running");
                }
                for (const auto& reference : node.references)
                    if (std::ranges::find(fields, reference.field) == fields.end())
                        ImGui::TextColored(
                            {1, .65f, .25f, 1}, "Obsolete field: %s (clear below)", reference.field.c_str());
            }
            else
                ImGui::TextWrapped("Script source not found: %s", node.script.c_str());
            for (const auto& reference : node.references)
                if (!document.Find(reference.node))
                    ImGui::TextColored({1, 0.4f, 0.3f, 1}, "Broken reference: %s", reference.field.c_str());
            if (!node.references.empty() && ImGui::TreeNode("Clear saved references"))
            {
                for (std::size_t i = 0; i < node.references.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::SmallButton(("Clear " + node.references.at(i).field).c_str()))
                    {
                        node.references.erase(std::next(node.references.begin(), static_cast<std::ptrdiff_t>(i)));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                    if (!document.Find(node.references.at(i).node))
                    {
                        ImGui::TextColored(
                            {1, 0.4f, 0.3f, 1}, "Broken reference: %s", node.references.at(i).field.c_str());
                    }
                }
                ImGui::TreePop();
            }
        }
    }
    void CanvasEditor::drawScriptIssues()
    {
        std::vector<std::pair<unsigned int, std::string>> issues;
        for (const auto& node : document.nodes)
        {
            if (node.script.empty()) continue;
            if (!scriptSources.contains(node.script) ||
                !std::filesystem::is_regular_file(scriptSources.at(node.script)))
            {
                issues.emplace_back(node.id, node.name + ": script source missing");
                continue;
            }
            const auto fields = exposedFields(scriptSources.at(node.script));
            for (const auto& field : fields)
                if (std::ranges::none_of(node.references, [&](const auto& ref) {
                        return ref.field == field && document.Find(ref.node);
                    }))
                    issues.emplace_back(node.id, node.name + ": assign " + field);
            for (const auto& ref : node.references)
                if (std::ranges::find(fields, ref.field) == fields.end())
                    issues.emplace_back(node.id, node.name + ": obsolete field " + ref.field);
        }
        if (!issues.empty())
        {
            const auto label = std::to_string(issues.size()) + " script binding issue(s) - draft can be saved";
            if (ImGui::TreeNode(label.c_str()))
            {
                for (std::size_t i = 0; i < issues.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(issues.at(i).second.c_str())) selected = issues.at(i).first;
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
} // namespace sage::editor

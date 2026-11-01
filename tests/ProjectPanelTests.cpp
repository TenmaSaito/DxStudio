#include "ProjectPanel.h"
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <iterator>

int main() {
    namespace fs = std::filesystem;

    // The fixture lives in the system temp folder: deep checkouts would exceed
    // MAX_PATH, and substituted drives (subst) change canonical paths.
    const auto fixture = fs::temp_directory_path() / (L"project-import-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    if (!fs::create_directory(fixture, ec)) {
        std::cerr << "Cannot create isolated fixture\n";
        return 1;
    }

    const auto project = fixture / L"project";
    const auto destination = project / L"Assets";
    const auto input1 = fixture / L"input1";
    const auto input2 = fixture / L"input2";
    const auto outside = fixture / L"outside";
    if (!fs::create_directories(destination, ec) || !fs::create_directory(input1, ec) || !fs::create_directory(input2, ec) ||
        !fs::create_directory(outside, ec)) {
        std::cerr << "Cannot prepare fixture\n";
        return 2;
    }

    const auto first = input1 / L"sprite.dat", second = input2 / L"sprite.dat";
    {
        std::ofstream out(first, std::ios::binary);
        out << "original";
    }

    {
        std::ofstream out(second, std::ios::binary);
        out << "replacement";
    }

    std::wstring error;
    if (!dxs::ImportProjectFile(project, destination, first, error)) {
        std::wcerr << L"Valid import failed: " << error << L'\n';
        return 3;
    }

    if (dxs::ImportProjectFile(project, destination, second, error) || error.empty()) {
        std::cerr << "Duplicate import was not rejected\n";
        return 4;
    }

    std::string data;
    {
        std::ifstream in(destination / L"sprite.dat", std::ios::binary);
        in >> data;
    }

    if (data != "original") {
        std::cerr << "Existing file was modified\n";
        return 5;
    }

    if (dxs::ImportProjectFile(project, outside, first, error) || error.empty() || fs::exists(outside / L"sprite.dat")) {
        std::cerr << "Outside destination was not rejected\n";
        return 6;
    }

    if (dxs::ImportProjectFile(project, project / L".." / L"outside", first, error) || fs::exists(outside / L"sprite.dat")) {
        std::cerr << "Traversal destination was not rejected\n";
        return 7;
    }

    // Developer Mode or the appropriate privilege may be required on Windows.
    const auto link = project / L"outside-link";
    fs::create_directory_symlink(outside, link, ec);
    if (!ec) {
        if (dxs::ImportProjectFile(project, link, first, error) || fs::exists(outside / L"sprite.dat")) {
            std::cerr << "Reparse destination was not rejected\n";
            return 8;
        }

        fs::remove(link, ec);
        if (ec) return 9;
    } else {
        std::cout << "Symlink case skipped (Windows did not permit creation).\n";
        ec.clear();
    }

    for (const auto& name : {L"../Escape", L"1Bad", L"Bad Name", L"Bad.cpp", L"CON", L"lpt1", L"", L"A\"Injected"}) {
        if (dxs::CreateProjectComponent(project, name, error) || error.empty()) {
            std::cerr << "Invalid component name was accepted\n";
            return 10;
        }
    }

    const auto scripts = project / L"Scripts";
    const auto generatedHeader = scripts / L"Mover_2.h", generatedSource = scripts / L"Mover_2.cpp";
    if (!dxs::CreateProjectComponent(project, L"Mover_2", error)) {
        std::wcerr << L"Component creation failed: " << error << L'\n';
        return 11;
    }

    if (!dxs::GenerateVisualStudioProject(project, fs::current_path(), error) ||
        !fs::is_regular_file(project / "DxStudioProject.sln") || !fs::is_regular_file(project / "DxStudioProject.vcxproj")) {
        std::wcerr << L"Visual Studio project generation failed: " << error << L'\n';
        return 17;
    }

    {
        std::ifstream generated(project / "DxStudioProject.vcxproj");
        std::string text((std::istreambuf_iterator<char>(generated)), {});
        if (text.find("stdcpp20") == std::string::npos || text.find("Engine.h") == std::string::npos ||
            text.find("Mover_2.cpp") == std::string::npos) {
            return 18;
        }
    }

    auto read = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto headerText = read(generatedHeader), sourceText = read(generatedSource);
    if (headerText.find("class Mover_2 final : public dxs::Component") == std::string::npos ||
        sourceText.find("void Mover_2::Start") == std::string::npos ||
        sourceText.find("void Mover_2::Update") == std::string::npos ||
        sourceText.find("DXS_REGISTER_COMPONENT(Mover_2)") == std::string::npos) {
        std::cerr << "Generated component is missing its class lifecycle or automatic registration\n";
        return 12;
    }

    if (dxs::CreateProjectComponent(project, L"Mover_2", error) || error.empty() || read(generatedHeader) != headerText ||
        read(generatedSource) != sourceText) {
        std::cerr << "Component collision changed an existing file\n";
        return 13;
    }

    const auto collision = scripts / L"Taken.cpp";
    {
        std::ofstream out(collision);
        out << "existing source";
    }

    if (dxs::CreateProjectComponent(project, L"Taken", error) || error.empty() || fs::exists(scripts / L"Taken.h") ||
        read(collision) != "existing source") {
        std::cerr << "Source collision did not roll back the newly created header\n";
        return 14;
    }

    const auto blocked = fixture / L"blocked";
    fs::create_directory(blocked, ec);
    {
        std::ofstream out(blocked / L"Scripts");
        out << "not a directory";
    }

    if (dxs::CreateProjectComponent(blocked, L"Mover", error) || error.empty()) {
        std::cerr << "A Scripts file was treated as a directory\n";
        return 15;
    }

    const auto redirected = fixture / L"redirected";
    fs::create_directory(redirected, ec);
    fs::create_directory_symlink(outside, redirected / L"Scripts", ec);
    if (!ec) {
        if (dxs::CreateProjectComponent(redirected, L"Escaped", error) || fs::exists(outside / L"Escaped.cpp")) {
            std::cerr << "Component creation followed a reparse directory\n";
            return 16;
        }

        fs::remove(redirected / L"Scripts", ec);
    } else {
        ec.clear();
    }

    for (const auto& p :
         {destination / L"sprite.dat", first, second, generatedHeader, generatedSource, collision, blocked / L"Scripts"}) {
        fs::remove(p, ec);
    }

    for (const auto& p : {destination, scripts, project, input1, input2, outside, blocked, redirected, fixture}) {
        fs::remove(p, ec);
    }

    std::cout << "Project import and component creation: success, no-overwrite, rollback, name validation and path confinement "
                 "tests passed.\n";
    return 0;
}

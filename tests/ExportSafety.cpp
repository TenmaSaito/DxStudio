//==================================================================================
//
// ゲームの書き出しの安全性のテスト [ExportSafety.cpp]
//
// 書き出し先の制限（アプリやプロジェクトの中には書き出さない）などを確認する。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"
#include <fstream>
#include <iostream>

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    namespace fs = std::filesystem;
    const fs::path fixture = fs::current_path() / L"build-direct" / (L"export-safety-" + std::to_wstring(GetCurrentProcessId()));
    if (!fs::create_directory(fixture)) return 1;
    fs::create_directory(fixture / L"project");
    fs::create_directory(fixture / L"output");
    fs::copy_file(L"Game.exe", fixture / L"Game.exe");
    {
        std::ofstream(fixture / L"project/Default.hlsl") << "this is not valid HLSL\n";
    }

    {
        std::ofstream(fixture / L"output/Game.exe") << "existing game";
    }

    {
        std::ofstream(fixture / L"output/scene.dxs") << "existing scene";
    }

    dxs::Scene scene;
    dxs::Entity entity;
    entity.sName = "test";
    scene.vEntities.push_back(entity);
    std::string error;
    bool passed = !dxs::ExportGame(fixture, fixture / L"output", scene, error);
    std::string gameText, sceneText;
    {
        std::ifstream in(fixture / L"output/Game.exe");
        std::getline(in, gameText);
    }

    {
        std::ifstream in(fixture / L"output/scene.dxs");
        std::getline(in, sceneText);
    }

    passed = passed && !error.empty() && gameText == "existing game" && sceneText == "existing scene";
    passed = passed && !dxs::ExportGame(fixture, fixture / L"project", scene, error);
    for (const auto& file : {fixture / L"Game.exe", fixture / L"project/Default.hlsl", fixture / L"output/Game.exe",
                             fixture / L"output/scene.dxs"}) {
        fs::remove(file);
    }

    fs::remove(fixture / L"project");
    fs::remove(fixture / L"output");
    fs::remove(fixture);
    if (!passed) {
        std::cerr << "Export failure changed existing files or allowed the project directory\n";
        return 2;
    }

    std::cout << "Invalid shader leaves existing export intact; project directory is protected.\n";
}

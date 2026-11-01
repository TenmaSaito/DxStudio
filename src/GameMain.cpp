//==================================================================================
//
// ゲーム（書き出したプレイヤー）のエントリーポイント [GameMain.cpp]
//
// Game.exe の横にある scene.dxs を読み込み、ウィンドウを作ってゲームループを回す。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {
//**********************************************************************************
// *** ゲームのウィンドウの状態 ***
//**********************************************************************************
struct GameWindow {
    dxs::Renderer renderer; // 描画
    bool ready = false;     // 描画の準備ができたか（大きさの変更で使う）
};

//==================================================================================
// --- ウィンドウプロシージャ ---
//==================================================================================
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* game = reinterpret_cast<GameWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_SIZE:
        if (game && game->ready && wparam != SIZE_MINIMIZED && LOWORD(lparam) && HIWORD(lparam)) {
            game->renderer.Resize(LOWORD(lparam), HIWORD(lparam));
        }

        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }

        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

//==================================================================================
// --- 実行ファイルのフォルダーの取得処理 ---
//==================================================================================
std::filesystem::path ExeDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

//==================================================================================
// --- UTF-8 → UTF-16 の変換処理 ---
//==================================================================================
std::wstring WideText(const char* pText) {
    std::wstring sResult;
    for (; pText && *pText; ++pText) {
        sResult.push_back(static_cast<wchar_t>(*pText));
    }

    return sResult;
}

//==================================================================================
// --- 使用するグラフィックス API の決定処理 ---
//==================================================================================
// 優先順位: コマンドライン（-dx12 / -dx11 / -dx9）> Game.exe の横の
// ProjectSettings.dxsettings > Direct3D 12
dxs::GraphicsApi ChooseGraphicsApi(const std::filesystem::path& base, const wchar_t* pCommandLine) {
    dxs::ProjectSettings settings;
    std::string ignored;
    dxs::LoadProjectSettings(base / dxs::PROJECT_SETTINGS_FILE_NAME, settings, ignored);
    int nArgumentCount = 0;
    LPWSTR* ppArguments = pCommandLine && *pCommandLine ? CommandLineToArgvW(pCommandLine, &nArgumentCount) : nullptr;
    for (int nIndex = 0; ppArguments && nIndex < nArgumentCount; ++nIndex) {
        std::string sArgument;
        for (const wchar_t* pChar = ppArguments[nIndex]; *pChar; ++pChar) {
            sArgument.push_back(static_cast<char>(*pChar & 0x7F));
        }

        dxs::GraphicsApi api{};
        if (dxs::ParseGraphicsApi(sArgument, api)) {
            settings.graphicsApi = api;
        }
    }

    if (ppArguments) {
        LocalFree(ppArguments);
    }

    return settings.graphicsApi;
}
} // namespace

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto base = ExeDirectory();
    if (base.empty()) {
        MessageBoxA(nullptr, "Could not locate Game.exe.", "DxStudio Game", MB_OK | MB_ICONERROR);
        return 1;
    }

    // シーンの読み込み
    dxs::Scene scene;
    std::string error;
    if (!dxs::LoadScene(base / "scene.dxs", scene, error)) {
        MessageBoxA(nullptr, error.c_str(), "DxStudio Game", MB_OK | MB_ICONERROR);
        return 1;
    }

    // コンポーネントの登録と、シーンで使っている物がすべて登録済みかの確認
    dxs::RegisterProjectComponents();
    for (const auto& entity : scene.vEntities) {
        for (const auto& component : entity.vComponents) {
            if (!dxs::IsComponentRegistered(component)) {
                error = "Component is not registered: " + component + " (entity: " + entity.sName + ")";
                MessageBoxA(nullptr, error.c_str(), "DxStudio Game", MB_OK | MB_ICONERROR);
                return 1;
            }
        }
    }

    // ウィンドウクラスの登録
    const wchar_t* className = L"DxStudioGameWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // 矢印のカーソル（IDC_ARROW）
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Could not register the game window.", L"DxStudio Game", MB_OK | MB_ICONERROR);
        return 1;
    }

    // ウィンドウの作成（描画領域が 1280 x 720 になる大きさ）
    GameWindow game;
    RECT rect{0, 0, 1280, 720};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, className, L"DxStudio Game", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &game);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the game window.", L"DxStudio Game", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 描画の初期化（選んだ API が使えなければ 12 → 11 → 9 の順に試す）
    RECT client{};
    GetClientRect(window, &client);
    const dxs::GraphicsApi preferredApi = ChooseGraphicsApi(base, commandLine);
    std::string sFallbackReport;
    if (!game.renderer.InitializeWithFallback(window, client.right, client.bottom, (base / "Default.cso").wstring(), preferredApi,
                                              error, &sFallbackReport)) {
        MessageBoxA(window, (error + "\n\n" + sFallbackReport).c_str(), "Renderer initialization failed", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }

    // タイトルに使っている API を表示し、ウィンドウを表示する
    const std::wstring sTitle =
        std::wstring(L"DxStudio Game  [") + WideText(dxs::GraphicsApiDisplayName(game.renderer.Api())) + L"]";
    SetWindowTextW(window, sTitle.c_str());
    game.renderer.SetCamera(dxs::ActiveGameCamera(scene));
    game.ready = true;
    ShowWindow(window, show);
    UpdateWindow(window);

    // ゲームループ: メッセージ処理 → コンポーネントの更新 → 描画
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    bool running = true;
    while (running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // 最小化中は更新しない（戻ったときに時間が飛ばないよう、時刻だけ進める）
        if (IsIconic(window)) {
            previous = Clock::now();
            Sleep(30);
            continue;
        }

        // 経過時間（止まっていた場合でも 0.1 秒まで）
        const auto now = Clock::now();
        const float dt = std::min(std::chrono::duration<float>(now - previous).count(), 0.1f);
        previous = now;
        dxs::UpdateUserComponents(scene, dt);
        game.renderer.SetCamera(dxs::ActiveGameCamera(scene));
        auto draws = dxs::ResolveWorldDraws(scene);
        if (!game.renderer.Render(draws, error)) {
            MessageBoxA(window, error.c_str(), "Rendering failed", MB_OK | MB_ICONERROR);
            DestroyWindow(window);
            running = false;
        }
    }

    game.ready = false;
    game.renderer.Shutdown();
    return 0;
}

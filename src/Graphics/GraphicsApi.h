//==================================================================================
//
// グラフィックス API の種類のヘッダーファイル [GraphicsApi.h]
//
// 実行時に切り替えられる描画バックエンド。エディター・書き出したゲーム・
// テストはすべて同じ Renderer を使い、変わるのはデバイスの実装だけ。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <array>
#include <string_view>

namespace dxs {

//**********************************************************************************
// *** グラフィックス API の種類 ***
//**********************************************************************************
enum class GraphicsApi : unsigned char { DIRECT3D12, DIRECT3D11, DIRECT3D9 };

// すべての API（優先度の高い順。初期化に失敗したら次を試す）
inline constexpr std::array<GraphicsApi, 3> ALL_GRAPHICS_APIS = {
    GraphicsApi::DIRECT3D12, GraphicsApi::DIRECT3D11, GraphicsApi::DIRECT3D9};

//==================================================================================
// --- 短い表示名の取得処理（ツールバーの表示用。例: "DX12"） ---
//==================================================================================
constexpr const char* GraphicsApiShortName(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return "DX11";
    case GraphicsApi::DIRECT3D9: return "DX9";
    default: return "DX12";
    }
}

//==================================================================================
// --- 正式な表示名の取得処理（例: "Direct3D 12"） ---
//==================================================================================
constexpr const char* GraphicsApiDisplayName(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return "Direct3D 11";
    case GraphicsApi::DIRECT3D9: return "Direct3D 9";
    default: return "Direct3D 12";
    }
}

//==================================================================================
// --- 保存用の文字列の取得処理（ProjectSettings.dxsettings に書く。例: "D3D12"） ---
//==================================================================================
constexpr const char* GraphicsApiToken(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return "D3D11";
    case GraphicsApi::DIRECT3D9: return "D3D9";
    default: return "D3D12";
    }
}

//==================================================================================
// --- 文字列からの読み取り処理 ---
//==================================================================================
// 保存用の文字列とよく使う書き方を受け付ける: D3D11, DX11, dx11, 11, -dx11, --dx11
// （大文字小文字は区別しない）。読めたら true。
inline bool ParseGraphicsApi(std::string_view text, GraphicsApi& api) {
    while (!text.empty() && text.front() == '-') text.remove_prefix(1); // コマンドライン用の先頭の '-' を除く
    // 大文字小文字を区別しない比較
    auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t nIndex = 0; nIndex < a.size(); ++nIndex) {
            const char cA = (a[nIndex] >= 'A' && a[nIndex] <= 'Z') ? static_cast<char>(a[nIndex] - 'A' + 'a') : a[nIndex];
            const char cB = (b[nIndex] >= 'A' && b[nIndex] <= 'Z') ? static_cast<char>(b[nIndex] - 'A' + 'a') : b[nIndex];
            if (cA != cB) return false;
        }
        return true;
    };
    // 「接頭辞 + 番号」のすべての組み合わせと比べる
    for (const auto candidate : ALL_GRAPHICS_APIS) {
        const std::string_view number = candidate == GraphicsApi::DIRECT3D12 ? "12" : candidate == GraphicsApi::DIRECT3D11 ? "11" : "9";
        for (const std::string_view prefix : {"d3d", "dx", "direct3d", ""}) {
            const std::size_t nLength = prefix.size();
            if (text.size() == nLength + number.size() && equalsIgnoreCase(text.substr(0, nLength), prefix) &&
                text.substr(nLength) == number) {
                api = candidate;
                return true;
            }
        }
    }
    return false;
}

} // namespace dxs

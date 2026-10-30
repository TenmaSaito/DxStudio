#pragma once

// Graphics backends selectable at runtime. The editor, exported games and
// tests share one Renderer front-end; only the device implementation changes.

#include <array>
#include <string_view>

namespace dxs {

enum class GraphicsApi : unsigned char { DIRECT3D12, DIRECT3D11, DIRECT3D9 };

inline constexpr std::array<GraphicsApi, 3> ALL_GRAPHICS_APIS = {
    GraphicsApi::DIRECT3D12, GraphicsApi::DIRECT3D11, GraphicsApi::DIRECT3D9};

// Short display name used in the toolbar badge, e.g. "DX12".
constexpr const char* GraphicsApiShortName(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return "DX11";
    case GraphicsApi::DIRECT3D9: return "DX9";
    default: return "DX12";
    }
}

// Long display name, e.g. "Direct3D 12".
constexpr const char* GraphicsApiDisplayName(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return "Direct3D 11";
    case GraphicsApi::DIRECT3D9: return "Direct3D 9";
    default: return "Direct3D 12";
    }
}

// Stable token stored in ProjectSettings.dxsettings, e.g. "D3D12".
constexpr const char* GraphicsApiToken(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return "D3D11";
    case GraphicsApi::DIRECT3D9: return "D3D9";
    default: return "D3D12";
    }
}

// Accepts tokens and common spellings: D3D11, DX11, dx11, 11, -dx11, --dx11.
inline bool ParseGraphicsApi(std::string_view text, GraphicsApi& api) {
    while (!text.empty() && text.front() == '-') text.remove_prefix(1);
    auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t nIndex = 0; nIndex < a.size(); ++nIndex) {
            const char cA = (a[nIndex] >= 'A' && a[nIndex] <= 'Z') ? static_cast<char>(a[nIndex] - 'A' + 'a') : a[nIndex];
            const char cB = (b[nIndex] >= 'A' && b[nIndex] <= 'Z') ? static_cast<char>(b[nIndex] - 'A' + 'a') : b[nIndex];
            if (cA != cB) return false;
        }
        return true;
    };
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

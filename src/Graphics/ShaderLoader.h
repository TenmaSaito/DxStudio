#pragma once

// Shared HLSL loading for every backend.

#include <d3dcommon.h>
#include <wrl/client.h>

#include <string>

namespace dxs::graphics {

using Microsoft::WRL::ComPtr;

struct ShaderPair {
    ComPtr<ID3DBlob> pVertexShader;
    ComPtr<ID3DBlob> pPixelShader;
};

// Shader model targets of one backend.
struct ShaderProfile {
    const char* pVertexTarget = "vs_5_0";
    const char* pPixelTarget = "ps_5_0";
    // Precompiled pair suffixes: Default.cso -> Default<suffix>.vs.cso
    const wchar_t* pCompiledSuffix = L"";
    bool bLegacy = false; // SM3: enables backwards-compatibility compilation
};

inline constexpr ShaderProfile SHADER_PROFILE_SM5{"vs_5_0", "ps_5_0", L"", false};
inline constexpr ShaderProfile SHADER_PROFILE_SM3{"vs_3_0", "ps_3_0", L"9", true};

// Loads `path`: a ".cso" path reads <stem><suffix>.vs.cso / .ps.cso, any other
// extension is compiled from HLSL source with VSMain / PSMain.
bool LoadShaderPair(const std::wstring& sPath, const ShaderProfile& profile, ShaderPair& shaders, std::string& error);

// Compiles in-memory HLSL (the built-in overlay shader and D3D9 fallbacks).
bool CompileShaderSource(const char* pSource, const ShaderProfile& profile, ShaderPair& shaders, std::string& error);

// Source of the built-in flat-colour shader used for editor overlays.
extern const char* const BUILTIN_FLAT_SHADER_SOURCE;

} // namespace dxs::graphics

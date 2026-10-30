#include "ShaderLoader.h"
#include "GraphicsDevice.h"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace dxs::graphics {

const char* const BUILTIN_FLAT_SHADER_SOURCE = R"(
cbuffer ObjectConstants : register(b0) {
    row_major float4x4 mvp;
    float4 color;
};
struct VertexInput { float3 position : POSITION; };
struct PixelInput { float4 position : SV_POSITION; };
PixelInput VSMain(VertexInput input) {
    PixelInput output;
    output.position = mul(float4(input.position, 1.0f), mvp);
    return output;
}
float4 PSMain(PixelInput input) : SV_TARGET { return color; }
)";

std::string FormatHResult(const char* pAction, long nResult) {
    char aBuffer[160];
    std::snprintf(aBuffer, sizeof(aBuffer), "%s (HRESULT 0x%08lX)", pAction, static_cast<unsigned long>(nResult));
    return aBuffer;
}

namespace {
UINT CompileFlags(const ShaderProfile& profile) {
    UINT uFlags = profile.bLegacy ? D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY : D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    uFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    return uFlags;
}

std::string DiagnosticText(ID3DBlob* pDiagnostics, const char* pFallback, HRESULT hr) {
    if (pDiagnostics && pDiagnostics->GetBufferSize()) {
        return std::string(static_cast<const char*>(pDiagnostics->GetBufferPointer()), pDiagnostics->GetBufferSize());
    }
    return FormatHResult(pFallback, hr);
}
} // namespace

bool LoadShaderPair(const std::wstring& sPath, const ShaderProfile& profile, ShaderPair& shaders, std::string& error) {
    const std::filesystem::path shaderFile(sPath);
    if (shaderFile.extension() == L".cso") {
        auto vertexFile = shaderFile;
        auto pixelFile = shaderFile;
        const std::wstring sSuffix = profile.pCompiledSuffix ? profile.pCompiledSuffix : L"";
        vertexFile.replace_extension(sSuffix + L".vs.cso");
        pixelFile.replace_extension(sSuffix + L".ps.cso");
        HRESULT hr = D3DReadFileToBlob(vertexFile.c_str(), &shaders.pVertexShader);
        if (FAILED(hr)) {
            error = FormatHResult("Read compiled vertex shader failed", hr);
            return false;
        }
        hr = D3DReadFileToBlob(pixelFile.c_str(), &shaders.pPixelShader);
        if (FAILED(hr)) {
            error = FormatHResult("Read compiled pixel shader failed", hr);
            return false;
        }
        return true;
    }
    auto compile = [&](const char* pEntry, const char* pTarget, ComPtr<ID3DBlob>& pOutput) {
        ComPtr<ID3DBlob> pDiagnostics;
        const HRESULT hr = D3DCompileFromFile(sPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, pEntry, pTarget,
                                              CompileFlags(profile), 0, &pOutput, &pDiagnostics);
        if (FAILED(hr)) error = DiagnosticText(pDiagnostics.Get(), "Compile shader failed", hr);
        return SUCCEEDED(hr);
    };
    return compile("VSMain", profile.pVertexTarget, shaders.pVertexShader) &&
           compile("PSMain", profile.pPixelTarget, shaders.pPixelShader);
}

bool CompileShaderSource(const char* pSource, const ShaderProfile& profile, ShaderPair& shaders, std::string& error) {
    auto compile = [&](const char* pEntry, const char* pTarget, ComPtr<ID3DBlob>& pOutput) {
        ComPtr<ID3DBlob> pDiagnostics;
        const HRESULT hr = D3DCompile(pSource, std::strlen(pSource), "DxStudioBuiltin", nullptr, nullptr, pEntry, pTarget,
                                      CompileFlags(profile), 0, &pOutput, &pDiagnostics);
        if (FAILED(hr)) error = DiagnosticText(pDiagnostics.Get(), "Compile built-in shader failed", hr);
        return SUCCEEDED(hr);
    };
    return compile("VSMain", profile.pVertexTarget, shaders.pVertexShader) &&
           compile("PSMain", profile.pPixelTarget, shaders.pPixelShader);
}

std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::DIRECT3D11: return CreateDeviceD3D11();
    case GraphicsApi::DIRECT3D9: return CreateDeviceD3D9();
    default: return CreateDeviceD3D12();
    }
}

} // namespace dxs::graphics

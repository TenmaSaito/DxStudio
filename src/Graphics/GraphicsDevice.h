#pragma once

// ---------------------------------------------------------------------------
// IGraphicsDevice: the minimal render-hardware interface used by Renderer.
//
// Renderer (picking, gizmos, grid, outlines, draw ordering) is API agnostic and
// talks only to this interface. Each backend translates the small command set
// below into Direct3D 12, 11 or 9 calls:
//
//   UploadVertices*  ->  BeginFrame  ->  { SetPipeline, Draw }*  ->  EndFrame
//
// Shader contract (identical for every backend, see project/Default.hlsl):
//   VSMain / PSMain, float3 POSITION input, row_major float4x4 mvp + float4 color.
//   D3D12/D3D11 bind them as constant buffer b0 (vs_5_0/ps_5_0).
//   D3D9 compiles the same HLSL as vs_3_0/ps_3_0: VS reads mvp from c0-c3,
//   PS reads color from c0 (fxc assigns registers in that order).
// ---------------------------------------------------------------------------

#include "GraphicsApi.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DirectXMath.h>

#include <cstddef>
#include <memory>
#include <string>

namespace dxs::graphics {

struct Vertex {
    float x, y, z;
};

// Fixed render state combinations used by the renderer.
enum class PipelineKind : unsigned char {
    SCENE,   // triangles, project shader, depth test + write
    GRID,    // lines, built-in shader, depth test, no depth write
    OVERLAY, // lines, built-in shader, no depth test (outlines, gizmos, marquee)
};

// Layout matches the HLSL constant buffer (80 bytes).
struct ObjectConstants {
    DirectX::XMFLOAT4X4 mvp;
    DirectX::XMFLOAT4 color;
};
static_assert(sizeof(ObjectConstants) == 80, "ObjectConstants must match the HLSL constant buffer");

struct DeviceCreateInfo {
    HWND hWnd = nullptr;
    unsigned uWidth = 0;
    unsigned uHeight = 0;
    std::wstring sShaderPath;        // .hlsl source, or Default.cso for precompiled pairs
    const Vertex* pStaticVertices = nullptr;
    std::size_t nStaticVertexCount = 0;
    std::size_t nMaxVertexCount = 0; // static + per-frame model vertices
};

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;

    [[nodiscard]] virtual GraphicsApi Api() const = 0;
    virtual bool Initialize(const DeviceCreateInfo& info, std::string& error) = 0;
    virtual bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) = 0;
    // Writes vertices at nFirstVertex. Only valid between frames.
    virtual bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string& error) = 0;
    virtual bool BeginFrame(const float (&aClearColor)[4], std::string& error) = 0;
    virtual void SetPipeline(PipelineKind kind) = 0;
    virtual void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) = 0;
    virtual bool EndFrame(std::string& error) = 0;
    virtual void Shutdown() = 0;
};

// Returns nullptr only when the backend is compiled out.
std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(GraphicsApi api);

// Backend factories (one translation unit each).
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D12();
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D11();
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D9();

// "Create Direct3D 11 device failed (HRESULT 0x887A0004)"
std::string FormatHResult(const char* pAction, long nResult);

} // namespace dxs::graphics

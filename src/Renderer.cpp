#include "Renderer.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include "OverlayVS.h"
#include "OverlayPS.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <limits>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace dxs {
namespace {
constexpr unsigned BufferCount = 2;
constexpr size_t MaxVertexCount = 1u << 20;
constexpr DXGI_FORMAT ColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

std::string Failure(const char* action, HRESULT hr) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s (HRESULT 0x%08lX)", action, static_cast<unsigned long>(hr));
    return buffer;
}

struct Vertex { float x, y, z; };
// Cube: six independently specified faces, followed by a quad in the XY plane.
constexpr Vertex Vertices[] = {
    {-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},
    { 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},
    { 0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},
    { 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},
    { 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f},{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f},{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f,0},{-0.5f, 0.5f,0},{0.5f, 0.5f,0},
    {-0.5f,-0.5f,0},{ 0.5f, 0.5f,0},{0.5f,-0.5f,0},
};

constexpr UINT GridStart = 42;
constexpr UINT GridCount = 84; // 21 lines in each direction, two endpoints each
constexpr UINT AxisXStart = GridStart + GridCount;
constexpr UINT AxisZStart = AxisXStart + 2;
constexpr UINT CubeLinesStart = AxisZStart + 2;
constexpr UINT QuadLinesStart = CubeLinesStart + 24;
constexpr UINT GizmoArrowStart = QuadLinesStart + 8;
constexpr UINT GizmoScaleStart = GizmoArrowStart + 6;
constexpr UINT GizmoRingStart = GizmoScaleStart + 10;
constexpr UINT GizmoRingSegments = 64;
constexpr UINT GizmoPlaneStart = GizmoRingStart + GizmoRingSegments * 2;
constexpr UINT GizmoCenterStart = GizmoPlaneStart + 8;
constexpr UINT MarqueeStart = GizmoCenterStart + 8;

std::vector<Vertex> BuildVertices() {
    std::vector<Vertex> v(std::begin(Vertices), std::end(Vertices));
    for (int i = -10; i <= 10; ++i) {
        const float n = static_cast<float>(i);
        v.push_back({n, 0, -10}); v.push_back({n, 0, 10});
        v.push_back({-10, 0, n}); v.push_back({10, 0, n});
    }
    v.push_back({-10, 0, 0}); v.push_back({10, 0, 0});
    v.push_back({0, 0, -10}); v.push_back({0, 0, 10});
    constexpr Vertex corners[] = {
        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}
    };
    constexpr int edges[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto& edge : edges) { v.push_back(corners[edge[0]]); v.push_back(corners[edge[1]]); }
    v.push_back({-0.5f,-0.5f,0}); v.push_back({0.5f,-0.5f,0});
    v.push_back({0.5f,-0.5f,0}); v.push_back({0.5f,0.5f,0});
    v.push_back({0.5f,0.5f,0}); v.push_back({-0.5f,0.5f,0});
    v.push_back({-0.5f,0.5f,0}); v.push_back({-0.5f,-0.5f,0});
    // A shaft along +X, followed by a small arrow and a scale-box tip.
    v.push_back({0,0,0}); v.push_back({1,0,0});
    v.push_back({1,0,0}); v.push_back({0.80f,0.09f,0});
    v.push_back({1,0,0}); v.push_back({0.80f,-0.09f,0});
    v.push_back({0,0,0}); v.push_back({1,0,0});
    v.push_back({0.91f,-0.06f,0}); v.push_back({1.03f,-0.06f,0});
    v.push_back({1.03f,-0.06f,0}); v.push_back({1.03f,0.06f,0});
    v.push_back({1.03f,0.06f,0}); v.push_back({0.91f,0.06f,0});
    v.push_back({0.91f,0.06f,0}); v.push_back({0.91f,-0.06f,0});
    for (UINT i = 0; i < GizmoRingSegments; ++i) {
        const float a = XM_2PI * i / GizmoRingSegments;
        const float b = XM_2PI * (i + 1) / GizmoRingSegments;
        v.push_back({std::cos(a), std::sin(a), 0});
        v.push_back({std::cos(b), std::sin(b), 0});
    }
    auto square = [&](float lo, float hi) {
        v.push_back({lo,lo,0}); v.push_back({hi,lo,0});
        v.push_back({hi,lo,0}); v.push_back({hi,hi,0});
        v.push_back({hi,hi,0}); v.push_back({lo,hi,0});
        v.push_back({lo,hi,0}); v.push_back({lo,lo,0});
    };
    square(0.23f, 0.45f); // plane movement handle
    square(-0.08f, 0.08f); // uniform-scale handle
    square(0, 1); // screen-space rectangle
    return v;
}

XMMATRIX World(const DrawItem& item) {
    return XMMatrixScaling(item.scale.x, item.scale.y, item.scale.z) *
        XMMatrixRotationRollPitchYaw(item.rotation.x, item.rotation.y, item.rotation.z) *
        XMMatrixTranslation(item.position.x, item.position.y, item.position.z);
}

XMMATRIX ViewProjection(const Camera& camera, float aspect) {
    const XMVECTOR target = XMLoadFloat3(&camera.target);
    const float cp = std::cos(camera.pitch);
    const XMVECTOR offset = XMVectorSet(std::sin(camera.yaw) * cp * camera.distance,
        std::sin(camera.pitch) * camera.distance, -std::cos(camera.yaw) * cp * camera.distance, 0);
    const float height = camera.distance * (10.0f / 6.32455532f);
    const auto projection = camera.orthographic
        ? XMMatrixOrthographicLH(height * aspect, height, camera.nearClip, camera.farClip)
        : XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.fieldOfView), aspect, camera.nearClip, camera.farClip);
    return XMMatrixLookAtLH(XMVectorAdd(target, offset), target, XMVectorSet(0, 1, 0, 0)) *
        projection;
}

XMMATRIX AxisRotation(int axis) {
    return axis == 0 ? XMMatrixIdentity() : axis == 1 ? XMMatrixRotationZ(XM_PIDIV2) : XMMatrixRotationY(-XM_PIDIV2);
}
XMMATRIX RingRotation(int axis) {
    return axis == 0 ? XMMatrixRotationY(XM_PIDIV2) : axis == 1 ? XMMatrixRotationX(-XM_PIDIV2) : XMMatrixIdentity();
}
XMMATRIX GizmoAxisRotation(const DrawItem& item, int axis, GizmoMode mode) {
    // Scaling changes the object's local axes, while position is in world space.
    const auto basis = AxisRotation(axis);
    return mode == GizmoMode::Scale ? basis * XMMatrixRotationRollPitchYaw(item.rotation.x, item.rotation.y, item.rotation.z) : basis;
}
bool GizmoLength(const Camera& camera, const DrawItem& item, unsigned width, unsigned height, float& length) {
    if (!width || !height) return false;
    const auto vp = ViewProjection(camera, static_cast<float>(width) / height);
    const auto clip = XMVector4Transform(XMVectorSet(item.position.x, item.position.y, item.position.z, 1), vp);
    const float w = XMVectorGetW(clip), z = XMVectorGetZ(clip);
    if (!std::isfinite(w) || w <= 0 || z < 0 || z > w) return false;
    const float worldHeight = camera.orthographic ? camera.distance * (10.0f / 6.32455532f) : 2.0f * std::tan(XM_PIDIV4 / 2) * w;
    length = worldHeight * 90.0f / height;
    return std::isfinite(length) && length > 0;
}
bool ScreenPoint(XMVECTOR point, FXMMATRIX mvp, unsigned width, unsigned height, XMFLOAT2& result) {
    auto c = XMVector4Transform(point, mvp);
    const float w = XMVectorGetW(c), z = XMVectorGetZ(c);
    if (!std::isfinite(w) || w <= 0 || z < 0 || z > w) return false;
    result = {(XMVectorGetX(c) / w + 1.0f) * 0.5f * width,
              (1.0f - XMVectorGetY(c) / w) * 0.5f * height};
    return std::isfinite(result.x) && std::isfinite(result.y);
}
float SegmentDistance(float x, float y, XMFLOAT2 a, XMFLOAT2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    if (len2 < 1e-5f) return std::numeric_limits<float>::infinity();
    const float t = std::clamp(((x-a.x)*dx + (y-a.y)*dy) / len2, 0.0f, 1.0f);
    const float px = x - (a.x + t*dx), py = y - (a.y + t*dy);
    return std::sqrt(px*px + py*py);
}
bool PlaneHit(int x, int y, unsigned width, unsigned height, FXMMATRIX vp,
              XMVECTOR origin, XMVECTOR normal, XMVECTOR& hit) {
    const float sx = 2.0f * x / width - 1.0f, sy = 1.0f - 2.0f * y / height;
    XMVECTOR det;
    const auto inv = XMMatrixInverse(&det, vp);
    if (std::abs(XMVectorGetX(det)) < 1e-12f) return false;
    const auto nearPoint = XMVector3TransformCoord(XMVectorSet(sx, sy, 0, 1), inv);
    const auto farPoint = XMVector3TransformCoord(XMVectorSet(sx, sy, 1, 1), inv);
    const auto ray = XMVectorSubtract(farPoint, nearPoint);
    const float denom = XMVectorGetX(XMVector3Dot(ray, normal));
    if (std::abs(denom) < 1e-7f) return false;
    const float t = XMVectorGetX(XMVector3Dot(XMVectorSubtract(origin, nearPoint), normal)) / denom;
    if (!std::isfinite(t)) return false;
    hit = XMVectorSubtract(XMVectorAdd(nearPoint, XMVectorScale(ray, t)), origin);
    return std::isfinite(XMVectorGetX(hit)) && std::isfinite(XMVectorGetY(hit)) && std::isfinite(XMVectorGetZ(hit));
}

bool PointInQuad(float x, float y, const XMFLOAT2 (&q)[4]) {
    bool positive = false, negative = false;
    for (int i = 0; i < 4; ++i) {
        const auto& a = q[i]; const auto& b = q[(i + 1) % 4];
        const float cross = (b.x-a.x)*(y-a.y) - (b.y-a.y)*(x-a.x);
        positive |= cross > 0.001f; negative |= cross < -0.001f;
    }
    return !(positive && negative);
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}
}

struct Renderer::Impl {
    HWND hwnd{};
    unsigned width{}, height{}, rtvStride{};
    UINT frameIndex{};
    UINT64 fenceValue{};
    HANDLE fenceEvent{};
    Camera camera{};
    std::vector<int> selection;
    bool marquee = false;
    int rectX0{}, rectY0{}, rectX1{}, rectY1{};
    int gizmoHot = -1;
    std::string resizeError;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap;
    ComPtr<ID3D12Resource> targets[BufferCount], depth, vertexBuffer;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline, gridPipeline, outlinePipeline;
    ComPtr<ID3D12Fence> fence;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    UINT baseVertexCount{};

    bool Wait(std::string& error) {
        if (!queue || !fence || !fenceEvent) return true;
        const UINT64 value = ++fenceValue;
        HRESULT hr = queue->Signal(fence.Get(), value);
        if (FAILED(hr)) { error = Failure("Command queue signal failed", hr); return false; }
        if (fence->GetCompletedValue() < value) {
            hr = fence->SetEventOnCompletion(value, fenceEvent);
            if (FAILED(hr)) { error = Failure("Fence event failed", hr); return false; }
            if (WaitForSingleObject(fenceEvent, INFINITE) != WAIT_OBJECT_0) {
                error = "Waiting for the GPU failed"; return false;
            }
        }
        return true;
    }

    bool CreateTargets(std::string& error) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (unsigned i = 0; i < BufferCount; ++i) {
            HRESULT hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&targets[i]));
            if (FAILED(hr)) { error = Failure("Get swap-chain buffer failed", hr); return false; }
            device->CreateRenderTargetView(targets[i].Get(), nullptr, handle);
            handle.ptr += rtvStride;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth));
        if (FAILED(hr)) { error = Failure("Create depth buffer failed", hr); return false; }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth.Get(), &dsv, dsvHeap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }
};

Renderer::Renderer() = default;
Renderer::~Renderer() { Shutdown(); }

bool Renderer::Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, std::string& error) {
    Shutdown();
    error.clear();
    if (!hwnd || !width || !height) { error = "Invalid viewport dimensions or window"; return false; }
    p_ = std::make_unique<Impl>();
    auto& p = *p_;
    p.hwnd = hwnd; p.width = width; p.height = height;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&p.factory));
    if (FAILED(hr)) { error = Failure("Create DXGI factory failed", hr); return false; }
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&p.device));
    if (FAILED(hr)) { error = Failure("Create Direct3D 12 device failed", hr); return false; }
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = p.device->CreateCommandQueue(&q, IID_PPV_ARGS(&p.queue));
    if (FAILED(hr)) { error = Failure("Create command queue failed", hr); return false; }
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = width; sc.Height = height; sc.Format = ColorFormat;
    sc.SampleDesc.Count = 1; sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = BufferCount; sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> baseSwapchain;
    hr = p.factory->CreateSwapChainForHwnd(p.queue.Get(), hwnd, &sc, nullptr, nullptr, &baseSwapchain);
    if (FAILED(hr)) { error = Failure("Create swap chain failed", hr); return false; }
    hr = baseSwapchain.As(&p.swapchain);
    if (FAILED(hr)) { error = Failure("Query swap chain failed", hr); return false; }
    p.factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    p.frameIndex = p.swapchain->GetCurrentBackBufferIndex();
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = BufferCount;
    hr = p.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&p.rtvHeap));
    if (FAILED(hr)) { error = Failure("Create RTV heap failed", hr); return false; }
    p.rtvStride = p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap.NumDescriptors = 1;
    hr = p.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&p.dsvHeap));
    if (FAILED(hr)) { error = Failure("Create DSV heap failed", hr); return false; }
    if (!p.CreateTargets(error)) return false;
    hr = p.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&p.allocator));
    if (FAILED(hr)) { error = Failure("Create command allocator failed", hr); return false; }

    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0; parameter.Constants.Num32BitValues = 20;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1; rootDesc.pParameters = &parameter;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature, diagnostics;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &diagnostics);
    if (FAILED(hr)) { error = diagnostics ? std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize()) : Failure("Serialize root signature failed", hr); return false; }
    hr = p.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p.root));
    if (FAILED(hr)) { error = Failure("Create root signature failed", hr); return false; }
    ComPtr<ID3DBlob> vs, ps;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& output) {
        diagnostics.Reset();
        HRESULT result = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, target, compileFlags, 0, &output, &diagnostics);
        if (FAILED(result)) error = diagnostics ? std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize()) : Failure("Compile shader failed", result);
        return SUCCEEDED(result);
    };
    const std::filesystem::path shaderFile(shaderPath);
    if (shaderFile.extension() == L".cso") {
        auto vertexFile = shaderFile, pixelFile = shaderFile;
        vertexFile.replace_extension(L".vs.cso");
        pixelFile.replace_extension(L".ps.cso");
        hr = D3DReadFileToBlob(vertexFile.c_str(), &vs);
        if (FAILED(hr)) { error = Failure("Read compiled vertex shader failed", hr); return false; }
        hr = D3DReadFileToBlob(pixelFile.c_str(), &ps);
        if (FAILED(hr)) { error = Failure("Read compiled pixel shader failed", hr); return false; }
    } else if (!compile("VSMain", "vs_5_0", vs) || !compile("PSMain", "ps_5_0", ps)) return false;
    D3D12_INPUT_ELEMENT_DESC input[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature = p.root.Get();
    state.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    state.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    state.InputLayout = {input, 1};
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    state.RasterizerState.DepthClipEnable = TRUE;
    state.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    state.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    state.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    state.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    state.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    state.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    state.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    state.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    state.DepthStencilState.DepthEnable = TRUE;
    state.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    state.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    state.DepthStencilState.StencilEnable = FALSE;
    state.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    state.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    state.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    state.DepthStencilState.BackFace = state.DepthStencilState.FrontFace;
    state.SampleMask = UINT_MAX;
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets = 1; state.RTVFormats[0] = ColorFormat;
    state.DSVFormat = DXGI_FORMAT_D32_FLOAT; state.SampleDesc.Count = 1;
    hr = p.device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&p.pipeline));
    if (FAILED(hr)) { error = Failure("Create graphics pipeline failed", hr); return false; }
    // Editor guides use baked shaders, so a project shader cannot hide or
    // distort the grid and selection outline. Game exports need no HLSL file.
    state.VS = {g_overlayVS, sizeof(g_overlayVS)};
    state.PS = {g_overlayPS, sizeof(g_overlayPS)};
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    state.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    hr = p.device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&p.gridPipeline));
    if (FAILED(hr)) { error = Failure("Create grid pipeline failed", hr); return false; }
    state.DepthStencilState.DepthEnable = FALSE;
    hr = p.device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&p.outlinePipeline));
    if (FAILED(hr)) { error = Failure("Create outline pipeline failed", hr); return false; }
    hr = p.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p.allocator.Get(), p.pipeline.Get(), IID_PPV_ARGS(&p.commands));
    if (FAILED(hr)) { error = Failure("Create command list failed", hr); return false; }
    hr = p.commands->Close();
    if (FAILED(hr)) { error = Failure("Close command list failed", hr); return false; }

    D3D12_HEAP_PROPERTIES upload{}; upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload.CreationNodeMask = 1; upload.VisibleNodeMask = 1;
    const auto vertices = BuildVertices();
    const auto verticesBytes = MaxVertexCount * sizeof(Vertex);
    p.baseVertexCount = static_cast<UINT>(vertices.size());
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = verticesBytes;
    buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = p.device->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&p.vertexBuffer));
    if (FAILED(hr)) { error = Failure("Create vertex buffer failed", hr); return false; }
    void* data{}; D3D12_RANGE readRange{0, 0};
    hr = p.vertexBuffer->Map(0, &readRange, &data);
    if (FAILED(hr)) { error = Failure("Map vertex buffer failed", hr); return false; }
    std::memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
    p.vertexBuffer->Unmap(0, nullptr);
    p.vertexView.BufferLocation = p.vertexBuffer->GetGPUVirtualAddress();
    p.vertexView.SizeInBytes = static_cast<UINT>(verticesBytes); p.vertexView.StrideInBytes = sizeof(Vertex);
    hr = p.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p.fence));
    if (FAILED(hr)) { error = Failure("Create fence failed", hr); return false; }
    p.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!p.fenceEvent) { error = "Create fence event failed"; return false; }
    return true;
}

void Renderer::Resize(unsigned width, unsigned height) {
    if (!p_ || !p_->swapchain || !width || !height || (p_->width == width && p_->height == height)) return;
    auto& p = *p_;
    p.resizeError.clear();
    if (!p.Wait(p.resizeError)) return;
    for (auto& target : p.targets) target.Reset();
    p.depth.Reset();
    HRESULT hr = p.swapchain->ResizeBuffers(BufferCount, width, height, ColorFormat, 0);
    if (FAILED(hr)) { p.resizeError = Failure("Resize swap chain failed", hr); return; }
    p.width = width; p.height = height;
    p.frameIndex = p.swapchain->GetCurrentBackBufferIndex();
    p.CreateTargets(p.resizeError);
}

void Renderer::SetCamera(const Camera& camera) {
    if (!p_ || !std::isfinite(camera.distance) || !std::isfinite(camera.yaw) ||
        !std::isfinite(camera.pitch) || !std::isfinite(camera.target.x) ||
        !std::isfinite(camera.target.y) || !std::isfinite(camera.target.z)) return;
    p_->camera = camera;
    p_->camera.distance = std::clamp(camera.distance, 0.2f, 80.0f);
    p_->camera.pitch = std::clamp(camera.pitch, -1.54f, 1.54f);
}

int Renderer::Pick(int x, int y, const std::vector<DrawItem>& items) const {
    if (!p_ || !p_->width || !p_->height || x < 0 || y < 0 ||
        x >= static_cast<int>(p_->width) || y >= static_cast<int>(p_->height)) return -1;
    const float sx = (2.0f * (x + 0.5f) / p_->width) - 1.0f;
    const float sy = 1.0f - (2.0f * (y + 0.5f) / p_->height);
    const float aspect = static_cast<float>(p_->width) / p_->height;
    float bestDepth = std::numeric_limits<float>::infinity();
    int best = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (!item.visible) continue;
        const XMMATRIX mvp = World(item) * ViewProjection(p_->camera, aspect);
        XMVECTOR det;
        const XMMATRIX inverse = XMMatrixInverse(&det, mvp);
        if (!std::isfinite(XMVectorGetX(det)) || std::abs(XMVectorGetX(det)) < 1e-12f) continue;
        XMFLOAT3 origin, end;
        XMStoreFloat3(&origin, XMVector3TransformCoord(XMVectorSet(sx, sy, 0, 1), inverse));
        XMStoreFloat3(&end, XMVector3TransformCoord(XMVectorSet(sx, sy, 1, 1), inverse));
        const float o[] = {origin.x, origin.y, origin.z};
        const float d[] = {end.x-origin.x, end.y-origin.y, end.z-origin.z};
        float tmin = 0.0f, tmax = 1.0f;
        bool hit = true;
        for (int axis = 0; axis < 3; ++axis) {
            const float lo = item.is2D && axis == 2 ? 0.0f : -0.5f;
            const float hi = item.is2D && axis == 2 ? 0.0f : 0.5f;
            if (std::abs(d[axis]) < 1e-8f) {
                if (o[axis] < lo || o[axis] > hi) { hit = false; break; }
            } else {
                float a = (lo-o[axis])/d[axis], b = (hi-o[axis])/d[axis];
                if (a > b) std::swap(a, b);
                tmin = std::max(tmin, a); tmax = std::min(tmax, b);
                if (tmin > tmax) { hit = false; break; }
            }
        }
        if (!hit) continue;
        XMVECTOR local = XMVectorSet(o[0]+d[0]*tmin, o[1]+d[1]*tmin, o[2]+d[2]*tmin, 1);
        XMVECTOR clip = XMVector4Transform(local, mvp);
        const float w = XMVectorGetW(clip);
        if (w <= 0) continue;
        const float depth = XMVectorGetZ(clip) / w;
        if (depth >= 0 && depth <= 1 && depth < bestDepth) { bestDepth = depth; best = static_cast<int>(i); }
    }
    return best;
}

std::vector<int> Renderer::PickRect(int x0, int y0, int x1, int y1, const std::vector<DrawItem>& items) const {
    std::vector<int> result;
    if (!p_ || !p_->width || !p_->height) return result;
    const float left = static_cast<float>(std::clamp(std::min(x0, x1), 0, static_cast<int>(p_->width)));
    const float right = static_cast<float>(std::clamp(std::max(x0, x1), 0, static_cast<int>(p_->width)));
    const float top = static_cast<float>(std::clamp(std::min(y0, y1), 0, static_cast<int>(p_->height)));
    const float bottom = static_cast<float>(std::clamp(std::max(y0, y1), 0, static_cast<int>(p_->height)));
    const float aspect = static_cast<float>(p_->width) / p_->height;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (!item.visible) continue;
        const auto mvp = World(item) * ViewProjection(p_->camera, aspect);
        float minX = std::numeric_limits<float>::infinity(), minY = minX;
        float maxX = -minX, maxY = -minX;
        bool projected = false;
        for (int z = 0; z < (item.is2D ? 1 : 2); ++z)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) {
                    XMFLOAT2 point;
                    if (!ScreenPoint(XMVectorSet(x ? 0.5f : -0.5f, y ? 0.5f : -0.5f,
                                                  item.is2D ? 0.0f : (z ? 0.5f : -0.5f), 1),
                                     mvp, p_->width, p_->height, point)) continue;
                    projected = true;
                    minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
                    minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
                }
        if (projected && maxX >= left && minX <= right && maxY >= top && minY <= bottom)
            result.push_back(static_cast<int>(i));
    }
    return result;
}

void Renderer::SetSelection(const std::vector<int>& indices) { if (p_) p_->selection = indices; }
void Renderer::SetMarquee(bool visible, int x0, int y0, int x1, int y1) {
    if (!p_) return;
    p_->marquee = visible; p_->rectX0 = x0; p_->rectY0 = y0; p_->rectX1 = x1; p_->rectY1 = y1;
}
void Renderer::SetGizmoHot(int handle) { if (p_) p_->gizmoHot = handle; }

int Renderer::HitGizmo(int x, int y, const DrawItem& item, GizmoMode mode) const {
    if (!p_ || x < 0 || y < 0 || x >= static_cast<int>(p_->width) || y >= static_cast<int>(p_->height)) return -1;
    float length;
    if (!GizmoLength(p_->camera, item, p_->width, p_->height, length)) return -1;
    const auto vp = ViewProjection(p_->camera, static_cast<float>(p_->width) / p_->height);
    const auto base = XMMatrixScaling(length, length, length) * XMMatrixTranslation(item.position.x, item.position.y, item.position.z) * vp;
    // Center box uniformly scales all dimensions. Plane handles take priority
    // over shafts, while the remaining shafts remain easy to pick.
    if (mode == GizmoMode::Scale) {
        XMFLOAT2 c;
        if (ScreenPoint(XMVectorSet(0,0,0,1), base, p_->width, p_->height, c) &&
            std::hypot(c.x-x, c.y-y) <= 9.0f) return 6;
    }
    if (mode == GizmoMode::Translate) {
        for (int normal = 0; normal < 3; ++normal) {
            const auto mvp = RingRotation(normal) * base;
            XMFLOAT2 q[4]{};
            const XMVECTOR pts[] = {XMVectorSet(.23f,.23f,0,1), XMVectorSet(.45f,.23f,0,1),
                                    XMVectorSet(.45f,.45f,0,1), XMVectorSet(.23f,.45f,0,1)};
            bool valid = true;
            for (int n = 0; n < 4; ++n) valid &= ScreenPoint(pts[n],mvp,p_->width,p_->height,q[n]);
            const float area = std::abs((q[1].x-q[0].x)*(q[3].y-q[0].y) - (q[1].y-q[0].y)*(q[3].x-q[0].x));
            if (valid && area > 20.0f && PointInQuad(static_cast<float>(x),static_cast<float>(y),q)) return 3 + normal;
        }
    }
    float best = 9.0f;
    int selected = -1;
    for (int axis = 0; axis < 3; ++axis) {
        const auto mvp = (mode == GizmoMode::Rotate ? RingRotation(axis) : GizmoAxisRotation(item, axis, mode)) * base;
        if (mode == GizmoMode::Rotate) {
            XMFLOAT2 c, u, v;
            if (!ScreenPoint(XMVectorSet(0,0,0,1),mvp,p_->width,p_->height,c) ||
                !ScreenPoint(XMVectorSet(1,0,0,1),mvp,p_->width,p_->height,u) ||
                !ScreenPoint(XMVectorSet(0,1,0,1),mvp,p_->width,p_->height,v)) continue;
            if (std::abs((u.x-c.x)*(v.y-c.y) - (u.y-c.y)*(v.x-c.x)) < 64.0f) continue;
            for (UINT i = 0; i < GizmoRingSegments; ++i) {
                const float a = XM_2PI * i / GizmoRingSegments, b = XM_2PI * (i+1) / GizmoRingSegments;
                XMFLOAT2 pa, pb;
                if (!ScreenPoint(XMVectorSet(std::cos(a),std::sin(a),0,1),mvp,p_->width,p_->height,pa) ||
                    !ScreenPoint(XMVectorSet(std::cos(b),std::sin(b),0,1),mvp,p_->width,p_->height,pb)) continue;
                const float dist = SegmentDistance(static_cast<float>(x),static_cast<float>(y),pa,pb);
                if (dist < best) { best = dist; selected = axis; }
            }
        } else {
            XMFLOAT2 a, b;
            if (!ScreenPoint(XMVectorSet(0.20f,0,0,1),mvp,p_->width,p_->height,a) ||
                !ScreenPoint(XMVectorSet(1,0,0,1),mvp,p_->width,p_->height,b)) continue;
            if (std::hypot(b.x-a.x, b.y-a.y) < 8.0f) continue;
            const float dist = SegmentDistance(static_cast<float>(x),static_cast<float>(y),a,b);
            if (dist < best) { best = dist; selected = axis; }
        }
    }
    return selected;
}

bool Renderer::DragGizmo(const DrawItem& start, GizmoMode mode, int axis,
                         int startX, int startY, int x, int y, DrawItem& out) const {
    if (!p_ || axis < 0 || axis > 6) return false;
    if (axis >= 3 && axis <= 5 && mode != GizmoMode::Translate) return false;
    if (axis == 6 && mode != GizmoMode::Scale) return false;
    float length;
    if (!GizmoLength(p_->camera, start, p_->width, p_->height, length)) return false;
    const auto vp = ViewProjection(p_->camera, static_cast<float>(p_->width) / p_->height);
    out = start;
    if (axis >= 3 && axis <= 5) {
        const int normalAxis = axis - 3;
        const auto center = XMVectorSet(start.position.x,start.position.y,start.position.z,1);
        const auto normal = normalAxis == 0 ? XMVectorSet(1,0,0,0) : normalAxis == 1 ? XMVectorSet(0,1,0,0) : XMVectorSet(0,0,1,0);
        XMVECTOR a, b;
        if (!PlaneHit(startX,startY,p_->width,p_->height,vp,center,normal,a) ||
            !PlaneHit(x,y,p_->width,p_->height,vp,center,normal,b)) return false;
        XMFLOAT3 delta{};
        XMStoreFloat3(&delta, XMVectorSubtract(b,a));
        out.position.x += delta.x; out.position.y += delta.y; out.position.z += delta.z;
        return true;
    }
    if (axis == 6) {
        const float factor = std::max(0.01f, 1.0f + (static_cast<float>(x-startX) - static_cast<float>(y-startY))/90.0f);
        if (!std::isfinite(factor)) return false;
        out.scale.x *= factor; out.scale.y *= factor;
        out.scale.z *= factor;
        return true;
    }
    float delta = 0;
    if (mode == GizmoMode::Rotate) {
        const auto center = XMVectorSet(start.position.x,start.position.y,start.position.z,1);
        const auto normal = axis == 0 ? XMVectorSet(1,0,0,0) : axis == 1 ? XMVectorSet(0,1,0,0) : XMVectorSet(0,0,1,0);
        XMVECTOR a, b;
        if (!PlaneHit(startX,startY,p_->width,p_->height,vp,center,normal,a) ||
            !PlaneHit(x,y,p_->width,p_->height,vp,center,normal,b)) return false;
        if (XMVectorGetX(XMVector3LengthSq(a)) < 1e-10f || XMVectorGetX(XMVector3LengthSq(b)) < 1e-10f) return false;
        a = XMVector3Normalize(a); b = XMVector3Normalize(b);
        delta = std::atan2(XMVectorGetX(XMVector3Dot(normal,XMVector3Cross(a,b))), XMVectorGetX(XMVector3Dot(a,b)));
    } else {
        const auto mvp = GizmoAxisRotation(start,axis,mode) * XMMatrixScaling(length,length,length) * XMMatrixTranslation(start.position.x,start.position.y,start.position.z) * vp;
        XMFLOAT2 a,b;
        if (!ScreenPoint(XMVectorSet(0,0,0,1),mvp,p_->width,p_->height,a) || !ScreenPoint(XMVectorSet(1,0,0,1),mvp,p_->width,p_->height,b)) return false;
        const float vx=b.x-a.x, vy=b.y-a.y, denominator=vx*vx+vy*vy;
        if (denominator < 64.0f) return false;
        const float ratio = ((x-startX)*vx+(y-startY)*vy)/denominator;
        delta = mode == GizmoMode::Translate ? ratio * length : ratio;
    }
    if (!std::isfinite(delta)) return false;
    float* target = mode == GizmoMode::Translate ? (axis == 0 ? &out.position.x : axis == 1 ? &out.position.y : &out.position.z)
                  : mode == GizmoMode::Rotate ? (axis == 0 ? &out.rotation.x : axis == 1 ? &out.rotation.y : &out.rotation.z)
                  : (axis == 0 ? &out.scale.x : axis == 1 ? &out.scale.y : &out.scale.z);
    *target += delta;
    if (mode == GizmoMode::Scale && std::abs(*target) < 0.01f) *target = *target < 0 ? -0.01f : 0.01f;
    return true;
}

bool Renderer::Render(const std::vector<DrawItem>& items, std::string& error, int selected, bool showGrid, GizmoMode mode, bool showGizmo) {
    error.clear();
    if (!p_ || !p_->pipeline || !p_->fenceEvent) { error = "Renderer is not initialized"; return false; }
    auto& p = *p_;
    if (!p.resizeError.empty()) { error = p.resizeError; return false; }
    std::vector<UINT> modelStarts(items.size(), 0), modelCounts(items.size(), 0);
    size_t cursor = p.baseVertexCount;
    for (size_t i=0;i<items.size();++i) if (items[i].visible && items[i].vertices) {
        if (cursor + items[i].vertices->size() > MaxVertexCount) { error="Model vertex limit exceeded (1,048,576 vertices per frame)."; return false; }
        modelStarts[i]=static_cast<UINT>(cursor); modelCounts[i]=static_cast<UINT>(items[i].vertices->size()); cursor+=items[i].vertices->size();
    }
    if (cursor > p.baseVertexCount) {
        void* mapped{}; D3D12_RANGE noRead{0,0};
        HRESULT mapResult=p.vertexBuffer->Map(0,&noRead,&mapped);
        if(FAILED(mapResult)){error=Failure("Map model vertex buffer failed",mapResult);return false;}
        for(size_t i=0;i<items.size();++i) if(modelCounts[i]) std::memcpy(static_cast<Vertex*>(mapped)+modelStarts[i],items[i].vertices->data(),modelCounts[i]*sizeof(Vertex));
        p.vertexBuffer->Unmap(0,nullptr);
    }
    HRESULT hr = p.allocator->Reset();
    if (FAILED(hr)) { error = Failure("Reset command allocator failed", hr); return false; }
    hr = p.commands->Reset(p.allocator.Get(), p.pipeline.Get());
    if (FAILED(hr)) { error = Failure("Reset command list failed", hr); return false; }
    auto barrier = Transition(p.targets[p.frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    p.commands->ResourceBarrier(1, &barrier);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += p.frameIndex * p.rtvStride;
    const auto dsv = p.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    // Default procedural sky environment. It stays independent from project
    // shaders, so a scene always has a readable sky backdrop.
    const float clear[] = {0.16f, 0.31f, 0.50f, 1.0f};
    p.commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    p.commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
    p.commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(p.width), static_cast<float>(p.height), 0, 1};
    D3D12_RECT rect{0, 0, static_cast<LONG>(p.width), static_cast<LONG>(p.height)};
    p.commands->RSSetViewports(1, &viewport); p.commands->RSSetScissorRects(1, &rect);
    p.commands->SetGraphicsRootSignature(p.root.Get());
    p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.commands->IASetVertexBuffers(0, 1, &p.vertexView);
    const float aspect = static_cast<float>(p.width) / static_cast<float>(p.height);
    const XMMATRIX vp = ViewProjection(p.camera, aspect);
    for (size_t itemIndex=0; itemIndex<items.size(); ++itemIndex) {
        const auto& item=items[itemIndex];
        if (!item.visible) continue;
        XMMATRIX mvp = World(item) * vp;
        struct Constants { XMFLOAT4X4 mvp; XMFLOAT4 color; } constants{};
        XMStoreFloat4x4(&constants.mvp, mvp); constants.color = item.color;
        p.commands->SetGraphicsRoot32BitConstants(0, 20, &constants, 0);
        if (modelCounts[itemIndex]) {
            p.commands->DrawInstanced(modelCounts[itemIndex],1,modelStarts[itemIndex],0);
        } else if (item.is2D) {
            p.commands->DrawInstanced(6, 1, 36, 0);
        } else {
            // Distinguish the cube's faces even with the minimal default shader.
            constexpr float shades[] = {0.75f, 0.9f, 0.62f, 0.82f, 1.0f, 0.55f};
            for (UINT face = 0; face < 6; ++face) {
                XMFLOAT4 tint{item.color.x * shades[face], item.color.y * shades[face],
                    item.color.z * shades[face], item.color.w};
                p.commands->SetGraphicsRoot32BitConstants(0, 4, &tint, 16);
                p.commands->DrawInstanced(6, 1, face * 6, 0);
            }
        }
    }
    struct Constants { XMFLOAT4X4 mvp; XMFLOAT4 color; } lineConstants{};
    if (showGrid) {
        const bool grid2D = selected >= 0 && static_cast<size_t>(selected) < items.size() && items[selected].is2D;
        XMMATRIX gridMvp = grid2D ? XMMatrixRotationX(XM_PIDIV2) * vp : vp;
        XMStoreFloat4x4(&lineConstants.mvp, gridMvp);
        lineConstants.color = {0.27f, 0.30f, 0.34f, 1.0f};
        p.commands->SetPipelineState(p.gridPipeline.Get());
        p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        p.commands->SetGraphicsRoot32BitConstants(0, 20, &lineConstants, 0);
        p.commands->DrawInstanced(GridCount, 1, GridStart, 0);
        lineConstants.color = {0.65f, 0.20f, 0.20f, 1};
        p.commands->SetGraphicsRoot32BitConstants(0, 4, &lineConstants.color, 16);
        p.commands->DrawInstanced(2, 1, AxisXStart, 0);
        lineConstants.color = {0.20f, 0.52f, 0.27f, 1};
        p.commands->SetGraphicsRoot32BitConstants(0, 4, &lineConstants.color, 16);
        p.commands->DrawInstanced(2, 1, AxisZStart, 0);
    }
    auto outline = [&](int index) {
        if (index < 0 || static_cast<size_t>(index) >= items.size()) return;
        const auto& item = items[index];
        if (!item.visible) return;
        XMStoreFloat4x4(&lineConstants.mvp, XMMatrixScaling(1.025f, 1.025f, 1.025f) * World(item) * vp);
        lineConstants.color = index == selected ? XMFLOAT4{1.0f,0.63f,0.08f,1.0f} : XMFLOAT4{1.0f,0.76f,0.25f,1.0f};
        p.commands->SetPipelineState(p.outlinePipeline.Get());
        p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        p.commands->SetGraphicsRoot32BitConstants(0, 20, &lineConstants, 0);
        p.commands->DrawInstanced(item.is2D ? 8 : 24, 1, item.is2D ? QuadLinesStart : CubeLinesStart, 0);
    };
    if (showGrid || showGizmo) for (int index : p.selection) if (index != selected) outline(index);
    outline(selected);
    if (showGizmo && selected >= 0 && static_cast<size_t>(selected) < items.size()) {
        const auto& item = items[selected];
        float length;
        if (GizmoLength(p.camera, item, p.width, p.height, length)) {
            const auto base = XMMatrixScaling(length,length,length) * XMMatrixTranslation(item.position.x,item.position.y,item.position.z) * vp;
            constexpr XMFLOAT4 colors[] = {{1,0.24f,0.22f,1},{0.28f,0.9f,0.28f,1},{0.28f,0.52f,1,1}};
            p.commands->SetPipelineState(p.outlinePipeline.Get());
            p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            for (int axis=0; axis<3; ++axis) {
                const auto gizmoMvp = (mode == GizmoMode::Rotate ? RingRotation(axis) : GizmoAxisRotation(item,axis,mode)) * base;
                lineConstants.color = axis == p.gizmoHot ? XMFLOAT4{1,0.88f,0.15f,1} : colors[axis];
                // DX12 line lists are one pixel wide. Offset the same geometry
                // in clip space for a readable three-pixel cross-section.
                constexpr float offsets[][2] = {{0,0},{-1,0},{1,0},{0,-1},{0,1}};
                for (const auto& offset : offsets) {
                    const auto shifted = gizmoMvp * XMMatrixTranslation(2.0f * offset[0] / p.width, 2.0f * offset[1] / p.height, 0);
                    XMStoreFloat4x4(&lineConstants.mvp, shifted);
                    p.commands->SetGraphicsRoot32BitConstants(0,20,&lineConstants,0);
                    if (mode == GizmoMode::Rotate) p.commands->DrawInstanced(GizmoRingSegments*2,1,GizmoRingStart,0);
                    else if (mode == GizmoMode::Scale) p.commands->DrawInstanced(10,1,GizmoScaleStart,0);
                    else p.commands->DrawInstanced(6,1,GizmoArrowStart,0);
                }
            }
            if (mode == GizmoMode::Translate) {
                for (int normal = 0; normal < 3; ++normal) {
                    const auto mvp = RingRotation(normal) * base;
                    lineConstants.color = p.gizmoHot == 3 + normal ? XMFLOAT4{1,0.88f,0.15f,1} : colors[normal];
                    XMStoreFloat4x4(&lineConstants.mvp, mvp);
                    p.commands->SetGraphicsRoot32BitConstants(0,20,&lineConstants,0);
                    p.commands->DrawInstanced(8,1,GizmoPlaneStart,0);
                }
            }
            if (mode == GizmoMode::Scale) {
                XMFLOAT2 center;
                if (ScreenPoint(XMVectorSet(0,0,0,1),base,p.width,p.height,center)) {
                    const auto mvp = XMMatrixScaling(180.0f/p.width,180.0f/p.height,1) *
                        XMMatrixTranslation(2*center.x/p.width-1,1-2*center.y/p.height,0);
                    lineConstants.color = p.gizmoHot == 6 ? XMFLOAT4{1,0.88f,0.15f,1} : XMFLOAT4{0.92f,0.92f,0.92f,1};
                    XMStoreFloat4x4(&lineConstants.mvp, mvp);
                    p.commands->SetGraphicsRoot32BitConstants(0,20,&lineConstants,0);
                    p.commands->DrawInstanced(8,1,GizmoCenterStart,0);
                }
            }
        }
    }
    if (p.marquee && (showGrid || showGizmo) && p.width && p.height) {
        const float left = 2.0f * p.rectX0 / p.width - 1.0f;
        const float top = 1.0f - 2.0f * p.rectY0 / p.height;
        const float dx = 2.0f * (p.rectX1-p.rectX0) / p.width;
        const float dy = -2.0f * (p.rectY1-p.rectY0) / p.height;
        XMStoreFloat4x4(&lineConstants.mvp, XMMatrixScaling(dx,dy,1) * XMMatrixTranslation(left,top,0));
        lineConstants.color = {0.45f,0.75f,1.0f,1.0f};
        p.commands->SetPipelineState(p.outlinePipeline.Get());
        p.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        p.commands->SetGraphicsRoot32BitConstants(0,20,&lineConstants,0);
        p.commands->DrawInstanced(8,1,MarqueeStart,0);
    }
    barrier = Transition(p.targets[p.frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    p.commands->ResourceBarrier(1, &barrier);
    hr = p.commands->Close();
    if (FAILED(hr)) { error = Failure("Close command list failed", hr); return false; }
    ID3D12CommandList* lists[] = {p.commands.Get()};
    p.queue->ExecuteCommandLists(1, lists);
    hr = p.swapchain->Present(1, 0);
    if (FAILED(hr)) { error = Failure("Present failed", hr); return false; }
    if (!p.Wait(error)) return false;
    p.frameIndex = p.swapchain->GetCurrentBackBufferIndex();
    return true;
}

void Renderer::Shutdown() {
    if (!p_) return;
    std::string ignored;
    p_->Wait(ignored);
    if (p_->fenceEvent) CloseHandle(p_->fenceEvent);
    p_.reset();
}
}

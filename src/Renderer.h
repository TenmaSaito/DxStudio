//==================================================================================
//
// レンダラー（描画の窓口）のヘッダーファイル [Renderer.h]
//
// API に依存しない描画クラス。GPU のバックエンド（Direct3D 12 / 11 / 9）は
// Initialize で決まり、それ以外の機能（ピック・ギズモの計算は CPU で行う）は
// どのバックエンドでも同じように動く。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DirectXMath.h>
#include "Graphics/GraphicsApi.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 描画するオブジェクト 1 つ分の情報 ***
//**********************************************************************************
struct DrawItem {
    DirectX::XMFLOAT3 position{0, 0, 0}; // 位置
    DirectX::XMFLOAT3 rotation{0, 0, 0}; // 回転（ラジアン、X / Y / Z）
    DirectX::XMFLOAT3 scale{1, 1, 1};    // 拡大率
    DirectX::XMFLOAT4 color{1, 1, 1, 1}; // 色（A < 1 で半透明）
    // 組み込みの形の選択: true = ワールド空間の XY 四角形、false = 立方体
    bool bIs2D = false;
    // false にすると、トランスフォームや選択はそのままで描画だけをしない
    bool bVisible = true;
    // 任意: 正規化済みの三角形リスト（ModelRenderer / MeshRenderer が使う）。
    // 空なら組み込みの立方体 / 四角形を描く。
    std::shared_ptr<const std::vector<DirectX::XMFLOAT3>> pVertices;
    // 任意: pVertices は「同じ明るさの三角形」が連続するように並んでいる
    // （1 区間ごとの頂点数、暗い順）。単色のシェーダーでも面ごとに陰影が付き、
    // 立体に見える。空ならメッシュを 1 色で描く。
    std::shared_ptr<const std::vector<std::uint32_t>> pShadeRuns;
};

//**********************************************************************************
// *** カメラ ***
//**********************************************************************************
// 注視点はワールド座標、角度はラジアン。平行投影でも同じ向きを使い、
// 距離（fDistance）でズームを決める。
struct Camera {
    DirectX::XMFLOAT3 target{0, 0, 0}; // 注視点
    float fYaw = 0.0f;                 // 水平方向の角度
    float fPitch = 0.32175055f;        // 上下方向の角度
    float fDistance = 6.32455532f;     // 注視点からの距離
    bool bOrthographic = false;        // 平行投影か
    float fFieldOfView = 45.0f;        // 視野角（度、透視投影のみ）
    float fNearClip = 0.1f;            // 近クリップ面
    float fFarClip = 1000.0f;          // 遠クリップ面
};

// エディター専用の変形ハンドル。番号: 0=X, 1=Y, 2=Z,
// 3/4/5 = YZ / XZ / XY 平面での移動、6 = 均等スケール
enum class GizmoMode { Translate, Rotate, Scale };

// シーンビューの描画モード（Unity: Shaded / Wireframe / Shaded Wireframe）
enum class DrawMode : unsigned char { SHADED, WIREFRAME, SHADED_WIREFRAME };

//**********************************************************************************
// *** レンダラークラス ***
//**********************************************************************************
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // API を指定しない Initialize が使うバックエンド
    static void SetDefaultApi(GraphicsApi api);
    static GraphicsApi DefaultApi();

    // 初期化（API 指定なし / あり）
    bool Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, std::string& error);
    bool Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, GraphicsApi api,
                    std::string& error);
    // preferred を最初に試し、だめなら残りの API を順に試す（12 → 11 → 9）。
    // pReport には、使えなかった各 API の理由が入る。
    bool InitializeWithFallback(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath,
                                GraphicsApi preferred, std::string& error, std::string* pReport = nullptr);
    [[nodiscard]] bool IsInitialized() const; // 初期化済みか
    [[nodiscard]] GraphicsApi Api() const;    // 使っている API

    void Resize(unsigned width, unsigned height); // 描画領域の大きさ変更
    void SetCamera(const Camera& camera);         // カメラの設定
    // エディターのビュー（showGrid == true）の隅に X/Y/Z の向きを表示するか
    void SetShowOrientationGizmo(bool bShow);
    // エディターの描画（showGrid == true）にだけ適用される。ゲームの描画は常にシェーディング。
    void SetDrawMode(DrawMode mode);
    [[nodiscard]] DrawMode GetDrawMode() const;
    // ビューポートのクライアント座標にあるオブジェクトの番号（無ければ -1）
    int Pick(int x, int y, const std::vector<DrawItem>& items) const;
    // 画面上の範囲が四角形と重なるオブジェクトの番号一覧（範囲選択）
    std::vector<int> PickRect(int x0, int y0, int x1, int y1, const std::vector<DrawItem>& items) const;
    void SetSelection(const std::vector<int>& indices);           // 選択中（アウトラインを描く物）
    void SetMarquee(bool visible, int x0, int y0, int x1, int y1); // 範囲選択の四角形
    // シーンビューの補助線（コライダーの輪郭など）: ワールド座標の点 2 つで 1 本。
    // グリッド表示時に最前面に描く。
    void SetDebugLines(std::vector<DirectX::XMFLOAT3> vSegments, const DirectX::XMFLOAT4& color);
    void SetGizmoHot(int handle);                                  // マウスが乗っているハンドル（強調表示）
    // 画面座標にあるギズモのハンドル番号（無ければ -1）
    int HitGizmo(int x, int y, const DrawItem& item, GizmoMode mode) const;
    // ハンドルのドラッグ量から変形後の状態を計算する
    bool DragGizmo(const DrawItem& start, GizmoMode mode, int axis,
                   int startX, int startY, int x, int y, DrawItem& out) const;
    // 1 フレーム描画する。showGrid / showGizmo はエディター用の補助表示。
    bool Render(const std::vector<DrawItem>& items, std::string& error,
                int selected = -1, bool showGrid = false,
                GizmoMode mode = GizmoMode::Translate, bool showGizmo = false);
    void Shutdown(); // 解放
private:
    struct Impl;              // 実装の詳細（Renderer.cpp）
    std::unique_ptr<Impl> p_; // 実装へのポインタ（Pimpl）
};
}

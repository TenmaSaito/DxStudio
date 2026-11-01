//==================================================================================
//
// エディターのウィンドウの内部状態のヘッダーファイル [EditorWindow.h]
//
// Editor*.cpp で共有する、エディターのウィンドウの内部状態。
// 外から使う入口は Editor.h（RunEditor）。
//
// ファイルの分担:
//   Editor.cpp          : 起動、メッセージループ、ウィンドウプロシージャ
//   EditorLayout.cpp    : パネルの配置、ドッキング、スプリッター
//   EditorChrome.cpp    : メニューバー、ツールバー、タブ、ステータスバー
//   EditorPanels.cpp    : 各パネル（Hierarchy / Scene / Project）の作成と描画
//   EditorInspector.cpp : インスペクター（トランスフォーム、コンポーネントの欄）
//   EditorActions.cpp   : コマンド（保存、読み込み、Play、ビルド、書き出し）
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"
#include "Renderer.h"
#include "ProjectPanel.h"
#include "EditorInteraction.h"
#include "EditorCommands.h"
#include "EditorLocale.h"
#include "EditorTheme.h"
#include "EditorUtility.h"
#include "ConsolePanel.h"
#include "ModelConvert.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cwctype>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <iomanip>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <stdexcept>
#include <string>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

namespace dxs::editor {

//**********************************************************************************
// *** ウィンドウプロシージャ ***
//**********************************************************************************
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);   // メインウィンドウ
LRESULT CALLBACK ViewportProc(HWND, UINT, WPARAM, LPARAM); // シーンビュー（DirectX の描画先）
LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);    // 各パネルの枠
LRESULT CALLBACK HierarchySubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                       DWORD_PTR); // Hierarchy のリスト（ドラッグで親子付け）
LRESULT CALLBACK SplitterSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR); // パネル間の境界線

//**********************************************************************************
// *** パネル（Hierarchy / Scene / Inspector / Project） ***
//**********************************************************************************
struct Editor;

struct EditorPanel {
    Editor* editor{};                                         // 持ち主のエディター
    int index{};                                              // パネルの番号（PANEL_NAMES の添字）
    HWND window{}, buttons[3]{};                              // 枠のウィンドウと、見出しのボタン（フロート / 最小化 / 閉じる）
    bool floating = false, hidden = false, collapsed = false; // 浮いている / 非表示 / 最小化
    // タブをドックの外へドラッグするとパネルが浮き、浮いたパネルをエディターの
    // 上へ戻すと再びドッキングする（Unity / UE のタブのドッキング）。
    POINT tabDragStart{};     // タブのドラッグ開始位置
    bool bTabPressed = false; // タブを押しているか
    bool bMoving = false;     // 浮いたパネルを移動中か
};

// メインウィンドウのドックの位置。aSlotPanel は位置ごとに表示するパネルを表し、
// 浮いたパネルを位置の上に落とすと、そこにあるパネルと入れ替わる。
constexpr int SLOT_LEFT = 0, SLOT_CENTER = 1, SLOT_RIGHT = 2, SLOT_BOTTOM = 3, SLOT_COUNT = 4;
inline const wchar_t* DOCK_SLOT_NAMES[SLOT_COUNT] = {L"left", L"centre", L"right", L"bottom"};
// シーンビューの描画モードとゲームビューの縦横比の名前
inline const wchar_t* const DRAW_MODE_NAMES[] = {L"Shaded", L"Wireframe", L"Shaded Wireframe"};
inline const wchar_t* const ASPECT_NAMES[] = {L"Free Aspect", L"16:9", L"16:10", L"4:3", L"1:1"};
constexpr float ASPECT_RATIOS[] = {0.0f, /* 0 = ウィンドウに合わせる */ 16.0f / 9.0f, 16.0f / 10.0f, 4.0f / 3.0f, 1.0f};
constexpr int ASPECT_COUNT = 5;
LRESULT CALLBACK LabelDragSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                       DWORD_PTR); // X / Y / Z などのラベルを横にドラッグして値を変える
// パネルの名前とアイコン
inline const wchar_t* PANEL_NAMES[] = {L"Hierarchy", L"Scene", L"Inspector", L"Project"};
constexpr theme::Icon PANEL_ICONS[] = {theme::Icon::HIERARCHY, theme::Icon::SCENE, theme::Icon::INSPECTOR, theme::Icon::PROJECT};
// 下のドックのタブはパネル 3 を共有する（Unity: Project | Console）
inline const wchar_t* BOTTOM_TAB_NAMES[] = {L"Project", L"Console"};
constexpr theme::Icon BOTTOM_TAB_ICONS[] = {theme::Icon::PROJECT, theme::Icon::CONSOLE};
constexpr int BOTTOM_TAB_COUNT = 2;
// メニューバーの項目
constexpr int MENU_COUNT = 8;
inline const wchar_t* MENU_NAMES[MENU_COUNT] = {L"File",      L"Edit",  L"Assets", L"GameObject",
                                                L"Component", L"Build", L"Window", L"Help"};
// 上部の枠: 自前のメニューバー、その下にメインのツールバー
constexpr int TOP_CHROME_HEIGHT = theme::MENU_BAR_HEIGHT + theme::TOOLBAR_HEIGHT;
constexpr int HIERARCHY_SCENE_ROW = 22; // Hierarchy のリストの上にある「シーン」の見出し行の高さ

//**********************************************************************************
// *** インスペクターのプロパティの 1 行（Unity: ラベルの列 + 値の列） ***
//**********************************************************************************
struct InspectorFieldRow {
    std::string sField;    // 変数のパス（"Speed"、"Points[0]" など）
    HWND hLabel = nullptr; // ラベル
    HWND hValue = nullptr; // EDIT、bool なら自前で描くチェックボックス、警告の行なら null
    bool bBoolean = false; // bool か
    bool bChecked = false; // bool の現在の値
    bool bNumeric = false; // float / int: マウスのドラッグとホイールで値を変えられる
    bool bInteger = false; // int か
    // 追加した種類（EditorFieldType を参照）
    EditorFieldType type = EditorFieldType::String;
    std::vector<HWND> vParts;            // Vector2 / Vector3: X Y (Z) の入力欄。hValue == vParts[0]
    std::vector<HWND> vPartLabels;       // "X" "Y" "Z"
    HWND hSlider = nullptr;              // EditRange: 値の欄の左にあるスライダー
    float fMin = 0.0f, fMax = 1.0f;      // スライダーの範囲
    bool bEnum = false;                  // hValue がドロップダウンか
    std::vector<std::string> vEnumNames; // 列挙型の名前
    bool bHeader = false;                // 配列の折りたたみ（hLabel がボタン、hValue なし）
    bool bArraySize = false;             // 配列のサイズの行か
    int nIndent = 0;                     // 字下げ（配列のサイズ / 要素の行）
    std::string sArrayPath;              // 所属する配列（折りたたみ中は非表示）
};

//**********************************************************************************
// *** 独自のメッセージ / タイマー ***
//**********************************************************************************
// 配列のサイズが変わったときに送る（コントロールの通知が終わってから行を作り直すため）
constexpr UINT WM_DXS_REBUILD_INSPECTOR = WM_APP + 41;
// バックグラウンドの作業スレッドが、作業の終了時に送る
constexpr UINT WM_DXS_TASK_DONE = WM_APP + 42;
constexpr UINT_PTR TASK_STATUS_TIMER = 2; // 「Building... 42%  12 s」とゲージを更新するタイマー

//**********************************************************************************
// *** バックグラウンドの作業 ***
//**********************************************************************************
// 時間のかかる作業（Android のビルド、書き出し）は作業スレッドで行い、エディターは
// 描画と操作を続ける。結果は UI スレッドに戻ってから処理する。
struct BackgroundTask {
    std::wstring sLabel;      // ステータスバーに出す名前
    ULONGLONG nStartTick = 0; // 開始時刻（経過秒の表示用）
    HANDLE hJob = nullptr;    // ビルドのプロセスツリーのジョブオブジェクト（中止用）
    bool bCanceled = false;   // 中止されたか
    // UI スレッドから定期的に呼ぶ: 0..1、または進度が分からないとき < 0
    // （その場合ゲージは動くバーを表示する）
    std::function<float()> progress;
    float fProgress = -1.0f;      // 最後に取得した進度
    std::thread worker;           // 作業スレッド
    std::function<void()> onDone; // 終了後に UI スレッドで実行する処理
};

//**********************************************************************************
// *** 補助関数 ***
//**********************************************************************************
std::vector<std::string> SplitVectorText(const std::string& sValue, int nCount);   // "1,2,3" を成分ごとに分ける
std::string DisplayValue(const InspectorFieldRow& row, const std::string& sValue); // 入力欄に表示する文字列
std::string SummarizeBuildLog(const std::filesystem::path& log);                   // ビルドのログから失敗の原因の行を抜き出す
// Gradle のビルドの進度（書き込み中のログの「> Task」の行数 / 予想される行数）
float BuildLogProgress(const std::filesystem::path& log, int nExpectedTasks);
int CountBuildLogTasks(const std::filesystem::path& log); // 成功したビルドのログのタスク数（それ以外は 0）
// Unreal 風の数値欄: 欄の中をドラッグ（またはホイール）して値を変える。
// ドラッグせずにクリックすると、値を選択した状態で文字の入力になる。
// uIdSubclass: Transform / Color の欄の番号（2..14）、または FIELD_SCRUB_COMPONENT + コントロール ID
constexpr UINT_PTR FIELD_SCRUB_COMPONENT = 0x10000;
LRESULT CALLBACK NumericFieldSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK WheelToPanelSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR); // ホイールをパネルのスクロールに回す

//**********************************************************************************
// *** インスペクターのコンポーネント 1 つ分（見出し + プロパティの行） ***
//**********************************************************************************
struct InspectorComponentView {
    size_t nComponentIndex = 0;           // エンティティ内のコンポーネントの番号
    std::string sName;                    // コンポーネントの名前
    HWND hHeader = nullptr;               // 見出し（折りたたみ / メニュー）
    HWND hScriptLabel = nullptr;          // 「Script」のラベル
    HWND hScriptValue = nullptr;          // スクリプト名
    std::vector<InspectorFieldRow> vRows; // プロパティの行
    bool bTruncated = false;              // コントロール ID より行が多く、途中までしか表示していない
};

//**********************************************************************************
// *** エディター本体 ***
//**********************************************************************************

struct Editor : public IPlayModeCallbacks, public IGraphicsCallbacks {
    // ---- Unity 風のインスペクターのコンポーネント欄 ------------------------
    std::vector<InspectorComponentView> vComponentViews;       // 表示中のコンポーネント
    std::string sComponentViewSignature;                       // 表示の構成（変わったら作り直す）
    std::unordered_map<std::string, bool> collapsedComponents; // 折りたたんだコンポーネント
    HWND colorLabel{};
    int nInspectorHeaderBottom = 0;
    int nAddComponentSeparator = 0;
    void RebuildComponentViews();     // コンポーネント欄の作り直し
    void UpdateComponentViewValues(); // 値の表示だけ更新
    InspectorFieldRow* FindFieldRow(int controlId, InspectorComponentView** ppView);
    bool IsComponentFieldEdit(HWND control) const;
    void CommitComponentField(int controlId, bool bToggle);
    std::unordered_map<std::string, bool> collapsedArrays; // 折りたたんだ配列（"<コンポーネント>/<配列のパス>"）
    bool bInspectorRebuildPosted = false;
    void RequestInspectorRebuild();
    void RebuildInspectorNow();
    int SliderSteps(const InspectorFieldRow& row) const;
    void SetSliderFromValue(const InspectorFieldRow& row, const std::string& sValue);
    void OnInspectorSlider(HWND hSlider);
    void DrawEnumItem(const DRAWITEMSTRUCT& item);
    bool SelectComponentForWindow(HWND control);
    bool IsOverTitlebarMenu(HWND header) const;
    void ShowTitlebarMenu(HWND header);
    void ShowAddComponentMenu();
    void ShowMeshPicker();

    // ---- X / FBX の変換（ModelConvert.h） --------------------------------------
    // 選択中の物体のメッシュ（.x → マテリアル作成 / .fbx → X 変換）
    void ConvertSelectedMesh();

    // X ファイルから .dxmat を作る。bAssign = true なら選択中の物体に最初のマテリアルを設定する。
    void CreateMaterialFromX(const std::filesystem::path& xFile, bool bAssign);

    // FBX を X に変換する。bAssign = true なら選択中の物体のメッシュを X に差し替える。
    void ConvertFbxAsset(const std::filesystem::path& fbxFile, bool bAssign);

    // メッシュ欄のアセットの拡張子（小文字。".x" / ".fbx" / ...）
    std::wstring SelectedMeshExtension() const;

    // 組み込みの基本図形のメッシュを使う MeshRenderer の物体を作る
    void AddPrimitive(PrimitiveShape shape);
    void AppendPrimitiveMenus(HMENU menu);
    void EnsureProjectPrimitives();

    // Unity と同じく、入力欄からフォーカスが外れたときに反映する（Apply ボタンなし）
    bool bQuietApply = false;
    void ReportApplyError(const std::string& message);
    bool IsInspectorEdit(HWND control) const;

    // Transform / Rendering の欄に、入力済みで未反映の文字があるか
    bool InspectorHasPendingEdits() const;

    // ---- 編集中のリアルタイムな表示 ------------------------------------------
    // WM_TIMER はメッセージキューが空のときしか届かないため、マウスの移動が
    // 続くと描画されなくなる。ドラッグ中は直接描画する（最大で約 15ms に 1 回）。
    ULONGLONG nLastRenderTick = 0;
    void RequestRender();

    // Transform / Color の値 1 つ（欄 2..14）をエンティティに直接書く
    bool SetFieldValue(int field, float fValue);

    // 入力中の欄をその場で反映する（正しい数値のときだけ）
    void ApplyFieldLive(int field);

    // Transform / Color の表示だけを更新する（軽い。ドラッグ中に使う）
    void RefreshTransformFields();

    // 数値欄のマウスでの調整（欄の中のドラッグ / ホイール）
    int nFieldScrubStartX = 0;
    float fFieldScrubStartValue = 0.0f;
    bool bFieldScrubbing = false;
    bool bFieldPressed = false;

    // 1 ピクセル（ドラッグ）またはホイール 1 段あたりの変化量（Shift / Alt を掛ける前）
    float FieldScrubStep(UINT_PTR fieldId, bool bWheel) const;

    // Transform の欄またはコンポーネントのプロパティに新しい数値を書く
    void SetScrubbedValue(UINT_PTR fieldId, HWND hEdit, float fValue);
    void ApplyOnFocusLoss(HWND control);

    // ---- Unity / UE 風の画面（メニューバー、ツールバー、タブ、Console、ステータス）
    HWND menuButtons[MENU_COUNT]{};
    HMENU menuPopups[MENU_COUNT]{};
    HWND pauseButton{}, stepButton{}, graphicsButton{}, layoutButton{};
    HWND consolePanel{}, bottomTabs[BOTTOM_TAB_COUNT]{}, statusRight{};
    int nBottomTab = 0;

    // 中央のドック: シーンビュー（エディターのカメラ、ギズモ）またはゲームビュー（有効なカメラ）
    HWND viewTabs[2]{};
    bool bGameView = false;
    int nHierarchyHot = -1;

    // シーンビューの描画モードとゲームビューの縦横比（Unity のツールバーのドロップダウン）
    HWND drawModeButton{}, aspectButton{};
    DrawMode drawMode = DrawMode::SHADED;
    int nGameAspect = 0;

    // ドッキング: 位置 → パネルの番号、半透明の落とし先のプレビュー
    int aSlotPanel[SLOT_COUNT] = {0, 1, 2, 3};
    HWND dockPreview{};

    // ラベルのドラッグ（X / Y / Z / R / G / B / A のラベルを横にドラッグして値を変える）
    int nScrubField = -1;
    int nScrubStartX = 0;
    float fScrubStartValue = 0.0f; // ドラッグ開始時の値
    bool bPaused = false, bStepRequested = false;
    bool bInspectorEnabledChecked = false;
    ProjectSettings projectSettings; // プロジェクトの設定
    // Hierarchy のツリーの状態（vHierarchyRows と同じ並び）
    std::vector<int> vHierarchyDepths;
    std::vector<char> vHierarchyHasChildren;
    std::vector<char> vCollapsedEntities;      // エンティティごとの折りたたみ
    ULONGLONG nFpsTick = 0;                    // フレームレートの計測開始時刻
    int nFpsFrames = 0;                        // 計測中のフレーム数
    CallbackRegistration callbackRegistration; // Play モード / グラフィックスの通知の登録

    // Play モード / グラフィックス API の変更の通知（Console に出す）

    void OnEnterPlayMode(Scene& playScene) override {
        Debug::Log("Entered Play Mode (" + std::to_string(playScene.vEntities.size()) + " objects).");
    }

    void OnExitPlayMode(Scene&) override { Debug::Log("Exited Play Mode. Runtime changes were discarded."); }

    void OnPauseChanged(bool bPaused) override { Debug::Log(bPaused ? "Play Mode paused." : "Play Mode resumed."); }

    void OnGraphicsApiChanged(GraphicsApi api) override {
        Debug::Log(std::string("Scene view renderer: ") + GraphicsApiDisplayName(api));
    }

    // ---- ウィンドウとコントロール ----------------------------------------
    HINSTANCE instance{};
    HWND window{}, viewport{}, hierarchy{}, hierarchySearch{}, status{}, projectPanel{};
    HWND splitters[3]{};
    HWND controls[11]{}, gizmoButtons[3]{}, groupLabels[3]{};
    HWND labels[FIELD_COUNT]{}, edits[FIELD_COUNT]{};
    HWND componentList{}, componentChoice{}, componentAdd{}, componentRemove{};
    HWND componentFields{}, componentValue{}, componentValueApply{};
    std::vector<std::pair<std::string, std::string>> vInspectedComponentFields;
    EditorPanel panels[4]{};
    HWND projectionButtons[2]{}, rowLabels[3]{}, inspectorHint{};
    HWND materialLabel{}, materialEdit{}, textureLabel{}, textureEdit{};
    HWND meshLabel{}, meshEdit{}, meshPick{}; // Mesh Renderer / Model Renderer のアセットの欄
    HWND meshConvert{};                       // Mesh 欄の下の変換ボタン（.x / .fbx のときだけ表示）
    HWND inspectorEnabled{}, inspectorIcon{}, inspectorTagLabel{}, inspectorTagValue{};
    HWND inspectorLayerLabel{}, inspectorLayerValue{};
    bool aSectionOpen[3]{true, true, true};       // インスペクターの各欄が開いているか
    bool bShellReady = false, bArranging = false; // 画面の作成が終わったか / 配置中か
    HFONT font{}, titleFont{};
    HBRUSH background{}, inputBackground{};

    // ---- シーンと状態 --------------------------------------------------------
    std::filesystem::path base, projectRoot, projectFile; // 実行ファイル / プロジェクトのフォルダー / .dxproject
    Scene scene;                                          // 編集中（Play 中は実行中）のシーン
    Scene editSceneSnapshot;                              // Play 開始時のシーン（終了時に戻す）
    Renderer renderer;                                    // シーンビューの描画
    Camera camera;                                        // エディターのカメラ
    Camera editCameraSnapshot;                            // Play 開始時のカメラ
    bool bRendererReady = false;                          // 描画の初期化に成功したか
    bool bSceneProtected = false;                         // 読み込みに失敗したシーンを上書き保存しないようにする
    bool bPlaying = false;                                // Play 中か
    ULONGLONG nPlayTick = 0;                              // 前のフレームの時刻（Play 中）
    int nSelected = -1;                                   // 選択中（アクティブ）のエンティティ
    std::vector<int> vSelectedIndices;                    // 選択中のエンティティすべて
    // リストの行は絞り込まれることがあるため、表示上の行番号をエンティティの
    // 番号と取り違えないよう、明示的な対応表を持つ。
    std::vector<int> vHierarchyRows;

    // ---- シーンビューのマウス操作 --------------------------------------------
    GizmoMode gizmoMode = GizmoMode::Translate; // ギズモの種類
    // ドラッグの種類: なし / 回転（右ボタン）/ 平行移動（中ボタン）/ ギズモ / 範囲選択
    enum class Drag { None, Orbit, Pan, Gizmo, Select } drag = Drag::None;
    POINT lastMouse{};
    POINT gizmoStartMouse{};
    int nGizmoAxis = -1;                                    // ドラッグ中のギズモのハンドル
    DrawItem gizmoStartItem{};                              // ドラッグ開始時のアクティブな物体
    std::vector<std::pair<int, DrawItem>> vGizmoStartItems; // ドラッグ開始時の選択中の物体すべて
    POINT selectionStart{};
    bool bSelectingRect = false, bSelectionAdd = false, bSelectionToggle = false;
    std::vector<int> vSelectionBeforeDrag; // 範囲選択の開始前の選択
    ULONGLONG nCameraTick = 0;             // カメラの移動の前回の時刻
    POINT rightStart{};
    bool bRightUsed = false,
         bOrbitPivot = false; // 右ドラッグで動かしたか（右クリックメニューを出さない）/ Alt で注視点中心に回すか
    int nHierarchyDrag = -1;  // Hierarchy でドラッグ中のエンティティ
    POINT hierarchyDragStart{};
    bool bHierarchyDragging = false;
    int nDockLeft = 210, nDockRight = 480, nDockProject = 210; // 左 / 右 / 下のドックの大きさ
    int nInspectorScroll = 0;                                  // インスペクターのスクロール量
    enum class LayoutDrag { None, Left, Right, Project } layoutDrag = LayoutDrag::None;

    //==============================================================================
    // --- カメラのフライスルーの更新処理（右ドラッグ中の WASD / QE） ---
    //==============================================================================
    void TickCamera() {
        const ULONGLONG now = GetTickCount64();
        const float seconds = nCameraTick ? static_cast<float>(now - nCameraTick) / 1000.0f : 0.0f;
        nCameraTick = now;
        if (drag != Drag::Orbit || bOrbitPivot || GetCapture() != viewport ||
            GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != window) {
            return;
        }

        // キーが押されていれば 1
        auto down = [](int key) {
            return (GetAsyncKeyState(key) & 0x8000) ? 1.0f : 0.0f;
        };
        if (FlyCamera(camera, down('W') - down('S'), down('D') - down('A'), down('E') - down('Q'), seconds,
                      down(VK_SHIFT) != 0)) {
            bRightUsed = true;
            renderer.SetCamera(camera);
        }
    }

    void SyncHierarchy(); // Hierarchy の表示をシーンに合わせる

    //==============================================================================
    // --- 選択の設定処理（範囲外・重複を除き、アクティブを決める） ---
    //==============================================================================
    void SetSelection(std::vector<int> indices, int active = -1) {
        indices.erase(std::remove_if(indices.begin(), indices.end(),
                                     [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.vEntities.size(); }),
                      indices.end());
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        vSelectedIndices = std::move(indices);
        nSelected = std::find(vSelectedIndices.begin(), vSelectedIndices.end(), active) != vSelectedIndices.end()
                        ? active
                        : (vSelectedIndices.empty() ? -1 : vSelectedIndices.back());
        SyncHierarchy();
        Inspect();
    }

    std::vector<int> MergeSelection(const std::vector<int>& hits) const; // Shift / Ctrl に応じた選択の合成
    void RestoreDrag();                                                  // ドラッグの取り消し（Esc）
    void UpdateGroup(const DrawItem& updated);                           // ギズモの変化を選択中の物体すべてに適用
    void FrameSelection();                                               // 選択中の物体が見えるようにカメラを動かす（F キー）

    // 子コントロールを作成する。ボタン / ラベルの文字列は現在の言語に翻訳し、
    // 起動時に作るもの（ツールバー・見出しなど）は言語切り替え用に英語を覚えておく。
    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, LocalizedControlText(cls, text), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (!bShellReady && h && text && *text && (_wcsicmp(cls, L"BUTTON") == 0 || _wcsicmp(cls, L"STATIC") == 0)) {
            vLocalizedControls.emplace_back(h, text);
        }

        return h;
    }

    // 言語切り替え用: 起動時に作ったコントロール / ツールチップの英語の文字列
    std::vector<std::pair<HWND, std::wstring>> vLocalizedControls;
    std::vector<std::pair<HWND, std::wstring>> vLocalizedTips;

    // 表示言語を切り替えて、メニュー・パネル・インスペクターを作り直す
    void ApplyLanguage(EditorLanguage language);

    // ---- テーマ（見た目） ------------------------------------------------------
    HWND tooltip{};
    void AddTooltip(HWND control, const wchar_t* text); // ツールチップの追加
    // 自前で描くすべてのコントロールに Unity / UE 風の見た目を設定する
    void ApplyTheme();

    // 入力欄には枠が無く、親のパネルが Unity 風の枠を周りに描く。
    // 枠とコントロールの間の余白を返す。
    RECT BoxInset(HWND control) const;
    void PaintInputBoxes(HDC dc, HWND panel);           // 入力欄の枠の描画
    theme::Icon EntityIcon(const Entity& entity) const; // エンティティの種類のアイコン
    // Hierarchy の行: 字下げ、折りたたみ、種類のアイコン、名前（非表示なら薄く）
    void DrawHierarchyRow(const DRAWITEMSTRUCT& item);
    void DrawComponentRow(const DRAWITEMSTRUCT& item);

    // 欄の行: 左に「名前」、右に入力欄風の枠の中に値（Unity のプロパティの行）
    void DrawFieldRow(const DRAWITEMSTRUCT& item);

    // インスペクターの見出し: 物体のアイコンと Tag / Layer のドロップダウン
    void DrawInspectorStatic(const DRAWITEMSTRUCT& item);
    void CreatePanels(); // パネルの作成
    // 自前で描くメニューバーから開くポップアップメニュー（ダーク、Unity の並び）
    void CreateMainMenu();

    // メニュー index をメニューバーのボタンの下に開く（Alt + 文字でも使う）
    void OpenMainMenu(int index);
    void UpdateMenuChecks();                       // メニューのチェックの更新
    void ResizeDock(LayoutDrag drag, POINT point); // スプリッターのドラッグでドックの大きさを変える
    void FloatPanel(int index);                    // パネルを浮かせる
    bool ShellCommand(int id);                     // 画面（メニュー / パネル）のコマンド
    void ShowObjectMenu(POINT point);              // 物体の右クリックメニュー
    void ShowComponentMenu(POINT point);           // コンポーネントの右クリックメニュー

    // ステータスバーの文字の設定（翻訳して表示）
    void SetStatus(const wchar_t* text) {
        SetWindowTextW(status, Tr(text));
        RECT r{};
        GetClientRect(window, &r);
        RECT bar{0, r.bottom - theme::STATUS_BAR_HEIGHT, r.right, r.bottom};
        InvalidateRect(window, &bar, TRUE);
    }

    // ---- Play モード（Unity: Play / Pause / Step） ------------------------------
    void RefreshTransport(); // Play / Pause / Step のボタンの表示の更新
    void TogglePlay();       // Play の開始 / 終了
    void TogglePause();      // 一時停止の切り替え
    // Unity と同じ動作: Step は一時停止し（必要なら Play を開始して）、1 フレームだけ進める
    void StepFrame();

    // ---- グラフィックス API（DirectX 12 / 11 / 9） ------------------------------
    void SwitchGraphicsApi(GraphicsApi api); // API の切り替え（プロジェクトの設定に保存）
    void UpdateGraphicsBadge();              // ツールバーの API 表示の更新
    void ShowToolbarDropdown(int id);        // ツールバーのドロップダウン
    // ---- 下のドックのタブ（Project | Console） ----------------------------------
    void SelectBottomTab(int tab); // 下のタブの選択
    void SelectViewTab(bool game); // Scene / Game のタブの選択
    void ShowViewDropdown(int id); // ビューのドロップダウン（描画モード / 縦横比）
    void SetDrawMode(int index);   // 描画モードの設定
    void SetGameAspect(int index); // ゲームビューの縦横比の設定

    EditorPanel& SlotPanel(int slot) { return panels[aSlotPanel[slot]]; }

    int PanelSlot(int panelIndex) const; // パネルが置かれている位置（無ければ -1）
    // ドックの位置のスクリーン座標の四角形（判定とプレビューに使う）
    RECT DockSlotRect(int slot) const;

    // スクリーン座標の下にある位置（ドックの外なら -1）
    int DockSlotAt(POINT screen) const;
    void ShowDockPreview(int slot);                  // 落とし先のプレビューの表示
    void DockPanelInSlot(int panelIndex, int slot);  // パネルを位置にドッキングする
    void AttachToSelection(const std::string& name); // 選択中の物体にコンポーネントを追加する
    // <プロジェクト>/ProjectSettings.dxsettings を読む（ファイルが無ければ既定値）
    void LoadSettings();
    bool SwitchProject(const std::filesystem::path& descriptor);          // プロジェクトの切り替え
    bool NewProject();                                                    // 新しいプロジェクトの作成
    bool RunBuildScript(const wchar_t* scriptName, const wchar_t* label); // ビルドのスクリプトをバックグラウンドで実行
    std::unique_ptr<BackgroundTask> pTask;                                // 実行中のバックグラウンドの作業

    [[nodiscard]] bool IsTaskRunning() const { return static_cast<bool>(pTask); }

    // 作業スレッドで work を始める。終わったら UI スレッドで onDone を実行する。
    bool StartBackgroundTask(std::wstring sLabel, std::function<void()> work, std::function<void()> onDone,
                             HANDLE hJob = nullptr);
    void FinishBackgroundTask(); // 作業の終了処理（WM_DXS_TASK_DONE で呼ぶ）
    // 実行中のビルドを止める（プロセスツリーごと）。書き出しは途中では止められない。
    void CancelBackgroundTask(bool bWaitForWorker);
    void UpdateTaskStatus();                            // ステータスバーの進度の表示の更新
    HWND progressGauge{};                               // ステータスバーのゲージ（バックグラウンドの作業中だけ表示）
    void DrawProgressGauge(const DRAWITEMSTRUCT& item); // ゲージの描画
    void Populate();                                    // Hierarchy の作り直し
    void Inspect();                                     // 選択中の物体をインスペクターに表示
    void RefreshComponentFields();                      // コンポーネントの欄の更新
    bool Apply();                                       // インスペクターの入力を物体に反映
    // メインウィンドウ: メニューバー、ツールバー、ドッキングしたパネル、ステータスバー
    //   +---------------------------------------------------------------+
    //   | File Edit Assets GameObject Component Build Window Help        |  menu bar
    //   | [Save][Load]        [>][||][>|]        [Shader][Export]..[DX12]|  toolbar
    //   | Hierarchy |          Scene              |  Inspector          |
    //   |-----------+-----------------------------|                     |
    //   | Project | Console                       |                     |
    //   | status                                             DX12 60fps|  status bar
    //   +---------------------------------------------------------------+
    void Layout();               // 全体の配置
    void LayoutPanel(int index); // パネル内の配置
    // プロジェクトのグラフィックス API でシーンビューの描画を（作り直して）用意する。
    // その API が使えなければ次の物を使う（12 → 11 → 9）。
    bool InitRenderer();
    void Draw(); // シーンビュー / ゲームビューの描画
    // ステータスバーの右側: バックエンド、フレームレート、Play の状態
    void UpdateFrameStats();
    void Command(int id, int notification); // WM_COMMAND の処理
};

} // namespace dxs::editor

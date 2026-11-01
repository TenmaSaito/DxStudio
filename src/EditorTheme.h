//==================================================================================
//
// エディターの見た目（テーマ）のヘッダーファイル [EditorTheme.h]
//
// Unity 2022 の「Dark」スキンを基に、Unreal Engine 5 風の強調色（青い選択中の
// ツール、色付きの軸）を加えた見た目。
//
//   Unity の部分                    色         DxStudio の定数
//   ウィンドウ / パネルの本体       #383838    COLOR_PANEL
//   ドックのタブの帯                #282828    COLOR_TAB_STRIP
//   選択中のタブ                    #3C3C3C    COLOR_TAB_ACTIVE
//   メインのツールバー / ステータス #191919    COLOR_TOOLBAR
//   入力欄                          #2A2A2A    COLOR_INPUT
//   ボタン                          #585858    COLOR_BUTTON
//   選択（フォーカスあり）          #2C5D87    COLOR_SELECTION
//   コンポーネントの見出し          #3E3E3E    COLOR_HEADER
//   ラベルの文字                    #C4C4C4    COLOR_TEXT
//
// 自前で描くボタンは、SetButtonSpec() で登録した ButtonSpec で見た目が決まる。
// マウスが乗ったときの追跡も自動で設定される。
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

#include <string>

namespace dxs::theme {

//**********************************************************************************
// *** 色 ***
//**********************************************************************************
inline constexpr COLORREF COLOR_TOOLBAR = RGB(25, 25, 25);
inline constexpr COLORREF COLOR_TOOLBAR_PLAYING = RGB(31, 43, 58); // Play 中のツールバーの色
inline constexpr COLORREF COLOR_TAB_STRIP = RGB(40, 40, 40);
inline constexpr COLORREF COLOR_TAB_ACTIVE = RGB(60, 60, 60);
inline constexpr COLORREF COLOR_TAB_ACCENT = RGB(58, 121, 187);
inline constexpr COLORREF COLOR_PANEL = RGB(56, 56, 56);
inline constexpr COLORREF COLOR_PANEL_ALT = RGB(60, 60, 60); // 一行おきの背景色
inline constexpr COLORREF COLOR_HEADER = RGB(62, 62, 62);
inline constexpr COLORREF COLOR_HEADER_HOVER = RGB(70, 70, 70);
inline constexpr COLORREF COLOR_SEPARATOR = RGB(26, 26, 26);
inline constexpr COLORREF COLOR_INPUT = RGB(42, 42, 42);
inline constexpr COLORREF COLOR_INPUT_BORDER = RGB(33, 33, 33);
inline constexpr COLORREF COLOR_FOCUS = RGB(58, 121, 187);
inline constexpr COLORREF COLOR_BUTTON = RGB(88, 88, 88);
inline constexpr COLORREF COLOR_BUTTON_HOVER = RGB(103, 103, 103);
inline constexpr COLORREF COLOR_BUTTON_PRESSED = RGB(70, 70, 70);
inline constexpr COLORREF COLOR_BUTTON_BORDER = RGB(36, 36, 36);
inline constexpr COLORREF COLOR_TOOL_HOVER = RGB(53, 53, 53);
inline constexpr COLORREF COLOR_ACCENT = RGB(0, 112, 224); // UE5 風の選択中のツールの青
inline constexpr COLORREF COLOR_ACCENT_HOVER = RGB(24, 132, 240);
inline constexpr COLORREF COLOR_PLAY = RGB(83, 186, 98); // 停止中の Play ボタンの三角
inline constexpr COLORREF COLOR_SELECTION = RGB(44, 93, 135);
inline constexpr COLORREF COLOR_SELECTION_INACTIVE = RGB(77, 77, 77);
inline constexpr COLORREF COLOR_ROW_HOVER = RGB(69, 69, 69);
inline constexpr COLORREF COLOR_TEXT = RGB(210, 210, 210);
inline constexpr COLORREF COLOR_TEXT_BRIGHT = RGB(238, 238, 238);
inline constexpr COLORREF COLOR_TEXT_DIM = RGB(138, 138, 138);
inline constexpr COLORREF COLOR_TEXT_DISABLED = RGB(108, 108, 108);
inline constexpr COLORREF COLOR_AXIS_X = RGB(219, 62, 62);
inline constexpr COLORREF COLOR_AXIS_Y = RGB(110, 186, 55);
inline constexpr COLORREF COLOR_AXIS_Z = RGB(56, 123, 232);
inline constexpr COLORREF COLOR_LOG_INFO = RGB(190, 190, 190);
inline constexpr COLORREF COLOR_LOG_WARNING = RGB(255, 196, 0);
inline constexpr COLORREF COLOR_LOG_ERROR = RGB(255, 84, 84);
inline constexpr COLORREF COLOR_ICON_CUBE = RGB(120, 170, 230);
inline constexpr COLORREF COLOR_ICON_FOLDER = RGB(214, 182, 106);
inline constexpr COLORREF COLOR_ICON_SCRIPT = RGB(126, 200, 140);

//**********************************************************************************
// *** 大きさ（DPI で拡大する前のピクセル。エディターの配置はピクセル単位） ***
//**********************************************************************************
inline constexpr int PANEL_HEADER_HEIGHT = 28;
inline constexpr int MENU_BAR_HEIGHT = 24;
inline constexpr int TOOLBAR_HEIGHT = 36;
inline constexpr int STATUS_BAR_HEIGHT = 26;
inline constexpr int ROW_HEIGHT = 20;

//**********************************************************************************
// *** アイコン ***
//**********************************************************************************
enum class Icon : unsigned char {
    NONE,
    // ツール / 再生操作
    HAND,
    MOVE,
    ROTATE,
    SCALE,
    PLAY,
    STOP,
    PAUSE,
    STEP,
    // ツールバーの操作
    SAVE,
    LOAD,
    EXPORT,
    SHADER,
    ANDROID,
    GRAPHICS,
    LAYOUT,
    PLUS,
    SEARCH,
    // パネル / ウィンドウの枠
    HIERARCHY,
    SCENE,
    INSPECTOR,
    PROJECT,
    CONSOLE,
    KEBAB,
    CLOSE,
    MINIMIZE,
    FLOAT,
    DOCK,
    CHEVRON_RIGHT,
    CHEVRON_DOWN,
    DROPDOWN,
    CHECK,
    // 物体 / コンポーネント
    CUBE,
    QUAD,
    GAME_OBJECT,
    CAMERA,
    MODEL,
    IMAGE,
    TRANSFORM,
    RENDERER,
    SCRIPT,
    FOLDER,
    FILE,
    // ログの種類
    INFO,
    WARNING,
    ERROR_ICON,
    TRASH,
    PERSPECTIVE,
    ORTHOGRAPHIC,
};

// icon を box の中央に描く。ベクターのアイコンは box の大きさに合わせて拡大する。
void DrawIcon(HDC hDc, Icon icon, const RECT& box, COLORREF color);

//**********************************************************************************
// *** 自前で描くボタン ***
//**********************************************************************************
enum class ButtonRole : unsigned char {
    PUSH,      // Unity の灰色の押しボタン
    PRIMARY,   // 強調色の押しボタン（Apply、Add Component）
    TOOL,      // 平らなツールバーのボタン（選択中は強調色で塗る）
    TRANSPORT, // Play / Pause / Step のつながったボタン
    MENU_BAR,  // メニューバーの項目（"File"、"Edit"、...）
    TAB,       // ドックのタブ
    HEADER,    // インスペクターのコンポーネントの見出し（折りたたみ）
    ICON,      // アイコンだけの小さな平らなボタン（パネルの枠）
    DROPDOWN,  // 右端に ∨ が付いたツールバーのドロップダウン
    CHECKBOX,  // 四角いチェックボックス
    FOLDOUT,   // インスペクターの配列の折りたたみ（∨ + ラベル、背景なし）
};

struct ButtonSpec {
    ButtonRole role = ButtonRole::PUSH;
    Icon icon = Icon::NONE;

    // アイコンの色（任意）。CLR_INVALID なら種類の文字色を使う。
    COLORREF iconColor = CLR_INVALID;

    // ボタンの背景色。CLR_INVALID なら種類から決める。
    COLORREF background = CLR_INVALID;

    // HEADER: 折りたたみの ∨ を開いた状態で描く
    bool bExpanded = true;
};

// 自前で描くボタンの見た目を登録し、マウスが乗ったときの追跡を設定する
void SetButtonSpec(HWND hButton, const ButtonSpec& spec);
[[nodiscard]] ButtonSpec GetButtonSpec(HWND hButton);
void SetButtonIcon(HWND hButton, Icon icon);
void SetButtonExpanded(HWND hButton, bool bExpanded);
[[nodiscard]] bool IsHot(HWND hWindow);

// 自前で描くボタンを描く。bActive は押された / チェックされた状態
void DrawButton(const DRAWITEMSTRUCT& item, bool bActive);

//**********************************************************************************
// *** 描画の基本関数 ***
//**********************************************************************************
void Fill(HDC hDc, const RECT& rect, COLORREF color);
void Frame(HDC hDc, const RECT& rect, COLORREF color);
void Line(HDC hDc, int nX0, int nY0, int nX1, int nY1, COLORREF color, int nWidth = 1);
void RoundFill(HDC hDc, const RECT& rect, int nRadius, COLORREF fill, COLORREF border);
void DrawLabel(HDC hDc, const std::wstring& sText, RECT rect, COLORREF color, HFONT hFont, UINT uFormat);
HBRUSH Brush(COLORREF color); // キャッシュする（テーマが所有・解放する）
COLORREF Blend(COLORREF a, COLORREF b, float fAmount);

//**********************************************************************************
// *** フォント ***
//**********************************************************************************
struct Fonts {
    HFONT hRegular = nullptr;
    HFONT hBold = nullptr;
    HFONT hSmall = nullptr;
    HFONT hTitle = nullptr;
};

void InitializeFonts(UINT uDpi);
const Fonts& GetFonts();
void ReleaseResources();

//**********************************************************************************
// *** Windows のダークモード ***
//**********************************************************************************
// プロセス全体: 対応していれば右クリックメニューとスクロールバーをダークにする
void EnableDarkMode();
// タイトルバーをダークにする（Windows 10 2004 以降、Windows 11 のタイトルバーの色）
void ApplyDarkTitleBar(HWND hWindow);
// スクロールバー / ツリー / リストをダークにする（"DarkMode_Explorer"）
void ApplyDarkControl(HWND hControl);
// コンボボックスと入力欄をダークにする（"DarkMode_CFD"）
void ApplyDarkInput(HWND hControl);

} // namespace dxs::theme

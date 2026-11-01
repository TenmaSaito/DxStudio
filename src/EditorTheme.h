#pragma once

// ---------------------------------------------------------------------------
// Editor look & feel, modelled on the Unity 2022 "Dark" editor skin with
// Unreal Engine 5 style accents (blue active tool, coloured axis bars).
//
//   Unity reference                          DxStudio token
//   window / panel body       #383838       COLOR_PANEL
//   dock tab strip            #282828       COLOR_TAB_STRIP
//   active tab                #3C3C3C       COLOR_TAB_ACTIVE
//   main toolbar / status     #191919       COLOR_TOOLBAR
//   input field               #2A2A2A       COLOR_INPUT
//   button                    #585858       COLOR_BUTTON
//   selection (focused)       #2C5D87       COLOR_SELECTION
//   component header          #3E3E3E       COLOR_HEADER
//   label text                #C4C4C4       COLOR_TEXT
//
// Owner-drawn buttons get their appearance from a ButtonSpec registered with
// SetButtonSpec(); hover tracking is installed automatically.
// ---------------------------------------------------------------------------

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>

namespace dxs::theme {

// ---- Palette ------------------------------------------------------------------
inline constexpr COLORREF COLOR_TOOLBAR = RGB(25, 25, 25);
inline constexpr COLORREF COLOR_TOOLBAR_PLAYING = RGB(31, 43, 58); // tint while in Play Mode
inline constexpr COLORREF COLOR_TAB_STRIP = RGB(40, 40, 40);
inline constexpr COLORREF COLOR_TAB_ACTIVE = RGB(60, 60, 60);
inline constexpr COLORREF COLOR_TAB_ACCENT = RGB(58, 121, 187);
inline constexpr COLORREF COLOR_PANEL = RGB(56, 56, 56);
inline constexpr COLORREF COLOR_PANEL_ALT = RGB(60, 60, 60); // alternating rows
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
inline constexpr COLORREF COLOR_ACCENT = RGB(0, 112, 224); // UE5 active tool blue
inline constexpr COLORREF COLOR_ACCENT_HOVER = RGB(24, 132, 240);
inline constexpr COLORREF COLOR_PLAY = RGB(83, 186, 98); // play triangle while idle
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

// ---- Metrics (unscaled pixels; the editor layout is pixel based) ----------------
inline constexpr int PANEL_HEADER_HEIGHT = 28;
inline constexpr int MENU_BAR_HEIGHT = 24;
inline constexpr int TOOLBAR_HEIGHT = 36;
inline constexpr int STATUS_BAR_HEIGHT = 26;
inline constexpr int ROW_HEIGHT = 20;

// ---- Icons ------------------------------------------------------------------------
enum class Icon : unsigned char {
    NONE,
    // tools / transport
    HAND,
    MOVE,
    ROTATE,
    SCALE,
    PLAY,
    STOP,
    PAUSE,
    STEP,
    // toolbar actions
    SAVE,
    LOAD,
    EXPORT,
    SHADER,
    ANDROID,
    GRAPHICS,
    LAYOUT,
    PLUS,
    SEARCH,
    // panel & window chrome
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
    // objects & components
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
    // log levels
    INFO,
    WARNING,
    ERROR_ICON,
    TRASH,
    PERSPECTIVE,
    ORTHOGRAPHIC,
};

// Draws `icon` centred in `box`. Vector icons scale with the box size.
void DrawIcon(HDC hDc, Icon icon, const RECT& box, COLORREF color);

// ---- Owner-drawn buttons -----------------------------------------------------------
enum class ButtonRole : unsigned char {
    PUSH,      // Unity grey push button
    PRIMARY,   // accent push button (Apply, Add Component)
    TOOL,      // flat toolbar button, accent fill when active
    TRANSPORT, // Play / Pause / Step segmented group
    MENU_BAR,  // menu bar item ("File", "Edit", ...)
    TAB,       // dock tab
    HEADER,    // Inspector component header (foldout)
    ICON,      // small flat icon-only button (panel chrome)
    DROPDOWN,  // toolbar dropdown with a trailing chevron
    CHECKBOX,  // square check box
    FOLDOUT,   // Inspector array foldout: chevron + label, no background
};

struct ButtonSpec {
    ButtonRole role = ButtonRole::PUSH;
    Icon icon = Icon::NONE;

    // Optional icon tint; CLR_INVALID uses the role's text colour.
    COLORREF iconColor = CLR_INVALID;

    // Fill behind the button; CLR_INVALID derives it from the role.
    COLORREF background = CLR_INVALID;

    // HEADER: draw the foldout chevron expanded.
    bool bExpanded = true;
};

// Registers the look of an owner-drawn button and installs hover tracking.
void SetButtonSpec(HWND hButton, const ButtonSpec& spec);
[[nodiscard]] ButtonSpec GetButtonSpec(HWND hButton);
void SetButtonIcon(HWND hButton, Icon icon);
void SetButtonExpanded(HWND hButton, bool bExpanded);
[[nodiscard]] bool IsHot(HWND hWindow);

// Draws an owner-drawn button. `bActive` selects the toggled/checked state.
void DrawButton(const DRAWITEMSTRUCT& item, bool bActive);

// ---- Primitives ----------------------------------------------------------------------
void Fill(HDC hDc, const RECT& rect, COLORREF color);
void Frame(HDC hDc, const RECT& rect, COLORREF color);
void Line(HDC hDc, int nX0, int nY0, int nX1, int nY1, COLORREF color, int nWidth = 1);
void RoundFill(HDC hDc, const RECT& rect, int nRadius, COLORREF fill, COLORREF border);
void DrawLabel(HDC hDc, const std::wstring& sText, RECT rect, COLORREF color, HFONT hFont, UINT uFormat);
HBRUSH Brush(COLORREF color); // cached, owned by the theme
COLORREF Blend(COLORREF a, COLORREF b, float fAmount);

// ---- Fonts ---------------------------------------------------------------------------
struct Fonts {
    HFONT hRegular = nullptr;
    HFONT hBold = nullptr;
    HFONT hSmall = nullptr;
    HFONT hTitle = nullptr;
};

void InitializeFonts(UINT uDpi);
const Fonts& GetFonts();
void ReleaseResources();

// ---- Windows dark mode ---------------------------------------------------------------
// Process-wide: dark context menus and dark scrollbars where supported.
void EnableDarkMode();
// Dark caption bar (Windows 10 2004+, Windows 11 caption colour).
void ApplyDarkTitleBar(HWND hWindow);
// Dark scrollbars / tree / list views ("DarkMode_Explorer").
void ApplyDarkControl(HWND hControl);
// Dark combo box and edit control ("DarkMode_CFD").
void ApplyDarkInput(HWND hControl);

} // namespace dxs::theme

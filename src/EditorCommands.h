#pragma once

// Command and control identifiers of the editor window (WM_COMMAND ids).
// Ranges: 100-199 commands, 200-214 Inspector edits, 400-499 panel/layout,
// 500-599 main menu/toolbar additions.

namespace dxs::editor {

constexpr int ID_HIERARCHY = 100, ID_ADD_CUBE = 101, ID_ADD_SPRITE = 102, ID_DELETE = 103;
constexpr int ID_APPLY = 104, ID_SAVE = 105, ID_LOAD = 106, ID_EXPORT = 107, ID_RELOAD_SHADER = 108;
constexpr int ID_MOVE_MODE = 109, ID_ROTATE_MODE = 110, ID_SCALE_MODE = 111;
constexpr int ID_COMPONENT_LIST = 112, ID_COMPONENT_CHOICE = 113, ID_ADD_COMPONENT = 114, ID_REMOVE_COMPONENT = 115;
constexpr int ID_PERSPECTIVE = 120, ID_ORTHOGRAPHIC = 121, ID_FRAME_SELECTION = 122, ID_CHOOSE_SCRIPT_EDITOR = 123, ID_PLATFORM_INFO = 124, ID_FRONT_VIEW = 125;
constexpr int ID_AUTOMATIC_SCRIPT_EDITOR = 126;
constexpr int ID_OPEN_PROJECT = 127, ID_SAVE_PROJECT_AS = 128, ID_BUILD_DEBUG = 129, ID_BUILD_ANDROID = 130;
constexpr int ID_NEW_PROJECT = 131, ID_ADD_EMPTY = 132, ID_ADD_MODEL = 133;
constexpr int ID_COMPONENT_FIELDS = 134, ID_COMPONENT_VALUE = 135, ID_APPLY_COMPONENT_VALUE = 136;
constexpr int ID_MOVE_COMPONENT_UP = 137, ID_MOVE_COMPONENT_DOWN = 138, ID_SPLITTER = 460;
constexpr int ID_DUPLICATE = 139;
constexpr int ID_CREATE_MENU = 140;
constexpr int ID_HIERARCHY_SEARCH = 141;
constexpr int ID_INSPECTOR_ENABLED = 142;
constexpr int ID_PANEL_SHOW = 400, ID_PANEL_FLOAT = 410, ID_PANEL_MINIMIZE = 420, ID_PANEL_HIDE = 430;
constexpr int ID_SECTION_TOGGLE = 440, ID_RESET_LAYOUT = 450, ID_EXIT = 451;
constexpr int ID_SAVE_LAYOUT = 452, ID_LOAD_LAYOUT = 453;
constexpr int ID_FIRST_EDIT = 200;

// Added by the Unity/UE style editor shell.
constexpr int ID_PAUSE = 160, ID_STEP = 161, ID_GRAPHICS_MENU = 162, ID_LAYOUT_MENU = 163;
constexpr int ID_GRAPHICS_D3D12 = 170, ID_GRAPHICS_D3D11 = 171, ID_GRAPHICS_D3D9 = 172;
constexpr int ID_DRAW_MODE_MENU = 164, ID_ASPECT_MENU = 165;
constexpr int ID_DRAW_MODE_FIRST = 180; // Shaded, Wireframe, Shaded Wireframe
constexpr int ID_ASPECT_FIRST = 185;    // Free, 16:9, 16:10, 4:3, 1:1
constexpr int ID_CLEAR_CONSOLE = 175, ID_SHOW_CONSOLE = 176, ID_ABOUT = 177, ID_OPEN_README = 178;
constexpr int ID_COMPONENT_HEADER_FIRST = 700;  // Inspector component titlebars
constexpr int ID_COMPONENT_FIELD_FIRST = 800;   // Inspector property fields
constexpr int ID_COMPONENT_FIELD_LAST = 999;
constexpr int COMPONENT_VIEW_LIMIT = 50;
constexpr int ID_ADD_COMPONENT_MENU_FIRST = 600; // Component menu: one id per registered class
constexpr int ID_ADD_COMPONENT_MENU_LAST = 699;
constexpr int ID_MENU_BAR_FIRST = 500;           // custom-drawn menu bar buttons
constexpr int ID_PANEL_TAB_FIRST = 520;          // bottom dock tabs (Project, Console)
constexpr int ID_VIEW_TAB_FIRST = 530;           // centre dock tabs (Scene, Game)

constexpr int FIELD_COUNT = 15;
inline const wchar_t* const FIELD_LABELS[FIELD_COUNT] = {
    L"Name", L"", L"Position X", L"Position Y", L"Position Z", L"Rotation X", L"Rotation Y", L"Rotation Z",
    L"Scale X", L"Scale Y", L"Scale Z", L"Color R", L"Color G", L"Color B", L"Color A"
};

} // namespace dxs::editor

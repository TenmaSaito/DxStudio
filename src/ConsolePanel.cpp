#include "ConsolePanel.h"
#include "Core/Log.h"
#include "EditorLocale.h"
#include "EditorTheme.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace dxs {
namespace {
constexpr wchar_t CONSOLE_WINDOW_CLASS[] = L"DxStudioConsolePanel";
constexpr UINT WM_APP_LOG_CHANGED = WM_APP + 41;
constexpr int ID_CONSOLE_LIST = 7201, ID_CONSOLE_CLEAR = 7202, ID_CONSOLE_COLLAPSE = 7203;
constexpr int ID_CONSOLE_INFO = 7204, ID_CONSOLE_WARNING = 7205, ID_CONSOLE_ERROR = 7206;
constexpr int TOOLBAR_HEIGHT = 28;
constexpr int ROW_HEIGHT = 22;

std::wstring Wide(const std::string& sValue) {
    if (sValue.empty()) return {};
    const int nLength = MultiByteToWideChar(CP_UTF8, 0, sValue.data(), static_cast<int>(sValue.size()), nullptr, 0);
    std::wstring sResult(static_cast<size_t>(nLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, sValue.data(), static_cast<int>(sValue.size()), sResult.data(), nLength);
    return sResult;
}

struct ConsoleRow {
    LogEntry entry;
    int nCount = 1; // > 1 when collapsed
};

class ConsolePanel {
public:
    HWND m_hWindow = nullptr;
    HWND m_hList = nullptr;
    HWND m_hClear = nullptr, m_hCollapse = nullptr;
    HWND m_hFilters[3]{};
    HINSTANCE m_hInstance = nullptr;
    bool m_bCollapse = false;
    bool m_bShow[3] = {true, true, true};
    std::uint64_t m_nClearedBefore = 0; // entries up to this sequence were cleared
    std::vector<ConsoleRow> m_vRows;
    int m_aCounts[3]{};
    ScopedDelegateHandle m_logSubscription;

    HWND Make(const wchar_t* pClass, const wchar_t* pText, DWORD dwStyle, int nId) {
        HWND hControl =
            CreateWindowExW(0, pClass, editor::LocalizedControlText(pClass, pText), WS_CHILD | WS_VISIBLE | dwStyle, 0, 0, 1, 1,
                            m_hWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(nId)), m_hInstance, nullptr);
        SendMessageW(hControl, WM_SETFONT, reinterpret_cast<WPARAM>(theme::GetFonts().hRegular), TRUE);
        return hControl;
    }

    void Create() {
        m_hClear = Make(L"BUTTON", L"Clear", BS_OWNERDRAW, ID_CONSOLE_CLEAR);
        m_hCollapse = Make(L"BUTTON", L"Collapse", BS_OWNERDRAW, ID_CONSOLE_COLLAPSE);
        const theme::Icon aIcons[] = {theme::Icon::INFO, theme::Icon::WARNING, theme::Icon::ERROR_ICON};
        const COLORREF aColors[] = {theme::COLOR_LOG_INFO, theme::COLOR_LOG_WARNING, theme::COLOR_LOG_ERROR};
        for (int nLevel = 0; nLevel < 3; ++nLevel) {
            m_hFilters[nLevel] = Make(L"BUTTON", L"0", BS_OWNERDRAW, ID_CONSOLE_INFO + nLevel);
            theme::ButtonSpec spec{theme::ButtonRole::TOOL, aIcons[nLevel], aColors[nLevel]};
            spec.background = theme::COLOR_PANEL;
            theme::SetButtonSpec(m_hFilters[nLevel], spec);
        }

        theme::ButtonSpec toolSpec{theme::ButtonRole::TOOL, theme::Icon::TRASH};
        toolSpec.background = theme::COLOR_PANEL;
        theme::SetButtonSpec(m_hClear, toolSpec);
        toolSpec.icon = theme::Icon::NONE;
        theme::SetButtonSpec(m_hCollapse, toolSpec);
        m_hList =
            Make(L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT | WS_VSCROLL,
                 ID_CONSOLE_LIST);
        SendMessageW(m_hList, LB_SETITEMHEIGHT, 0, ROW_HEIGHT);
        theme::ApplyDarkControl(m_hList);

        // The log can be written from any thread: the handler only posts a
        // message and the UI thread re-reads Debug::History().
        HWND hWindow = m_hWindow;
        m_logSubscription = ScopedDelegateHandle(Debug::OnLog(), Debug::OnLog().AddLambda([hWindow](const LogEntry&) {
            PostMessageW(hWindow, WM_APP_LOG_CHANGED, 0, 0);
        }));
        Rebuild();
    }

    void Rebuild() {
        m_vRows.clear();
        std::fill(std::begin(m_aCounts), std::end(m_aCounts), 0);
        for (const auto& entry : Debug::History()) {
            if (entry.nSequence <= m_nClearedBefore) continue;
            const int nLevel = static_cast<int>(entry.level);
            ++m_aCounts[nLevel];
            if (!m_bShow[nLevel]) continue;
            if (m_bCollapse) {
                const auto found = std::find_if(m_vRows.begin(), m_vRows.end(), [&](const ConsoleRow& row) {
                    return row.entry.level == entry.level && row.entry.sMessage == entry.sMessage;
                });
                if (found != m_vRows.end()) {
                    ++found->nCount;
                    continue;
                }
            }

            m_vRows.push_back({entry, 1});
        }

        const bool bAtBottom = IsScrolledToBottom();
        SendMessageW(m_hList, WM_SETREDRAW, FALSE, 0);
        SendMessageW(m_hList, LB_RESETCONTENT, 0, 0);
        for (size_t nIndex = 0; nIndex < m_vRows.size(); ++nIndex) {
            SendMessageW(m_hList, LB_ADDSTRING, 0, static_cast<LPARAM>(nIndex));
        }

        if (bAtBottom && !m_vRows.empty()) {
            SendMessageW(m_hList, LB_SETTOPINDEX, m_vRows.size() - 1, 0);
        }

        SendMessageW(m_hList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(m_hList, nullptr, TRUE);
        for (int nLevel = 0; nLevel < 3; ++nLevel) {
            SetWindowTextW(m_hFilters[nLevel], std::to_wstring(std::min(m_aCounts[nLevel], 999)).c_str());
            InvalidateRect(m_hFilters[nLevel], nullptr, FALSE);
        }
    }

    bool IsScrolledToBottom() const {
        const int nCount = static_cast<int>(SendMessageW(m_hList, LB_GETCOUNT, 0, 0));
        if (nCount <= 0) return true;
        RECT client{};
        GetClientRect(m_hList, &client);
        const int nVisible = std::max(1, static_cast<int>(client.bottom) / ROW_HEIGHT);
        return static_cast<int>(SendMessageW(m_hList, LB_GETTOPINDEX, 0, 0)) + nVisible >= nCount - 1;
    }

    void Clear() {
        const auto vHistory = Debug::History();
        m_nClearedBefore = vHistory.empty() ? m_nClearedBefore : vHistory.back().nSequence;
        Rebuild();
    }

    void Layout() {
        RECT client{};
        GetClientRect(m_hWindow, &client);
        const int nWidth = client.right, nHeight = client.bottom;
        MoveWindow(m_hClear, 4, 3, 66, 22, TRUE);
        MoveWindow(m_hCollapse, 74, 3, 92, 22, TRUE);
        int nX = nWidth - 4;
        for (int nLevel = 2; nLevel >= 0; --nLevel) {
            nX -= 52;
            MoveWindow(m_hFilters[nLevel], nX, 3, 50, 22, TRUE);
        }

        MoveWindow(m_hList, 0, TOOLBAR_HEIGHT, nWidth, std::max(1, nHeight - TOOLBAR_HEIGHT), TRUE);
    }

    void DrawRow(const DRAWITEMSTRUCT& item) {
        HDC hDc = item.hDC;
        RECT rect = item.rcItem;
        if (item.itemID == static_cast<UINT>(-1) || item.itemID >= m_vRows.size()) {
            theme::Fill(hDc, rect, theme::COLOR_PANEL);
            return;
        }

        const auto& row = m_vRows[item.itemID];
        const bool bSelected = (item.itemState & ODS_SELECTED) != 0;
        const COLORREF background =
            bSelected ? theme::COLOR_SELECTION : (item.itemID % 2 ? theme::COLOR_PANEL_ALT : theme::COLOR_PANEL);
        theme::Fill(hDc, rect, background);
        const int nMid = (rect.top + rect.bottom) / 2;
        const theme::Icon icon = row.entry.level == LogLevel::LOG_ERROR     ? theme::Icon::ERROR_ICON
                                 : row.entry.level == LogLevel::LOG_WARNING ? theme::Icon::WARNING
                                                                            : theme::Icon::INFO;
        const COLORREF iconColor = row.entry.level == LogLevel::LOG_ERROR     ? theme::COLOR_LOG_ERROR
                                   : row.entry.level == LogLevel::LOG_WARNING ? theme::COLOR_LOG_WARNING
                                                                              : theme::COLOR_LOG_INFO;
        RECT iconBox{rect.left + 6, nMid - 7, rect.left + 20, nMid + 7};
        theme::DrawIcon(hDc, icon, iconBox, iconColor);
        wchar_t aTime[32];
        const int nSeconds = static_cast<int>(row.entry.dTimeSeconds);
        swprintf_s(aTime, L"[%02d:%02d:%02d]", nSeconds / 3600, (nSeconds / 60) % 60, nSeconds % 60);
        RECT timeRect{rect.left + 26, rect.top, rect.left + 104, rect.bottom};
        theme::DrawLabel(hDc, aTime, timeRect, theme::COLOR_TEXT_DIM, theme::GetFonts().hSmall,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT textRect{rect.left + 110, rect.top, rect.right - (row.nCount > 1 ? 44 : 6), rect.bottom};
        std::wstring sMessage = Wide(row.entry.sMessage);
        std::replace(sMessage.begin(), sMessage.end(), L'\n', L' ');
        theme::DrawLabel(hDc, sMessage, textRect, theme::COLOR_TEXT, theme::GetFonts().hRegular,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (row.nCount > 1) {
            RECT badge{rect.right - 40, nMid - 8, rect.right - 6, nMid + 8};
            theme::RoundFill(hDc, badge, 8, theme::COLOR_BUTTON, theme::COLOR_BUTTON);
            theme::DrawLabel(hDc, std::to_wstring(row.nCount), badge, theme::COLOR_TEXT_BRIGHT, theme::GetFonts().hSmall,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    void CopySelection() {
        const int nIndex = static_cast<int>(SendMessageW(m_hList, LB_GETCURSEL, 0, 0));
        if (nIndex < 0 || static_cast<size_t>(nIndex) >= m_vRows.size() || !OpenClipboard(m_hWindow)) return;
        const std::wstring sText = Wide(m_vRows[nIndex].entry.sMessage);
        EmptyClipboard();
        if (HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, (sText.size() + 1) * sizeof(wchar_t))) {
            std::copy(sText.c_str(), sText.c_str() + sText.size() + 1, static_cast<wchar_t*>(GlobalLock(hMemory)));
            GlobalUnlock(hMemory);
            if (!SetClipboardData(CF_UNICODETEXT, hMemory)) {
                GlobalFree(hMemory);
            }
        }

        CloseClipboard();
    }
};

LRESULT CALLBACK ConsoleWindowProc(HWND hWindow, UINT uMessage, WPARAM wParam, LPARAM lParam) {
    auto* pPanel = reinterpret_cast<ConsolePanel*>(GetWindowLongPtrW(hWindow, GWLP_USERDATA));
    if (uMessage == WM_NCCREATE) {
        pPanel = static_cast<ConsolePanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        pPanel->m_hWindow = hWindow;
        SetWindowLongPtrW(hWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pPanel));
    }

    if (!pPanel) return DefWindowProcW(hWindow, uMessage, wParam, lParam);
    switch (uMessage) {
    case WM_CREATE:
        pPanel->Create();
        pPanel->Layout();
        return 0;
    case WM_SIZE: pPanel->Layout(); return 0;
    case WM_APP_LOG_CHANGED: {
        // Coalesce bursts of log messages into one rebuild.
        MSG pending{};
        while (PeekMessageW(&pending, hWindow, WM_APP_LOG_CHANGED, WM_APP_LOG_CHANGED, PM_REMOVE)) {
        }

        pPanel->Rebuild();
        return 0;
    }
    case WM_COMMAND: {
        const int nId = LOWORD(wParam);
        if (HIWORD(wParam) != BN_CLICKED) return 0;
        if (nId == ID_CONSOLE_CLEAR) {
            pPanel->Clear();
        } else if (nId == ID_CONSOLE_COLLAPSE) {
            pPanel->m_bCollapse = !pPanel->m_bCollapse;
            pPanel->Rebuild();
            InvalidateRect(pPanel->m_hCollapse, nullptr, FALSE);
        } else if (nId >= ID_CONSOLE_INFO && nId <= ID_CONSOLE_ERROR) {
            pPanel->m_bShow[nId - ID_CONSOLE_INFO] = !pPanel->m_bShow[nId - ID_CONSOLE_INFO];
            pPanel->Rebuild();
        }

        return 0;
    }
    case WM_KEYDOWN: break;
    case WM_VKEYTOITEM:
        if (LOWORD(wParam) == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            pPanel->CopySelection();
            return -2;
        }

        return -1;
    case WM_DRAWITEM: {
        const auto* pItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (pItem->CtlID == ID_CONSOLE_LIST) {
            pPanel->DrawRow(*pItem);
        } else {
            const int nId = static_cast<int>(pItem->CtlID);
            const bool bActive = nId == ID_CONSOLE_COLLAPSE ? pPanel->m_bCollapse
                                 : nId >= ID_CONSOLE_INFO   ? pPanel->m_bShow[nId - ID_CONSOLE_INFO]
                                                            : false;
            theme::DrawButton(*pItem, bActive && nId != ID_CONSOLE_CLEAR);
        }

        return TRUE;
    }
    case WM_CTLCOLORLISTBOX:
        SetBkColor(reinterpret_cast<HDC>(wParam), theme::COLOR_PANEL);
        return reinterpret_cast<LRESULT>(theme::Brush(theme::COLOR_PANEL));
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hWindow, &client);
        HDC hDc = reinterpret_cast<HDC>(wParam);
        theme::Fill(hDc, client, theme::COLOR_PANEL);
        RECT toolbar{0, 0, client.right, TOOLBAR_HEIGHT};
        theme::Fill(hDc, toolbar, theme::Blend(theme::COLOR_PANEL, RGB(255, 255, 255), 0.03f));
        theme::Line(hDc, 0, TOOLBAR_HEIGHT - 1, client.right, TOOLBAR_HEIGHT - 1, theme::COLOR_SEPARATOR);
        return 1;
    }
    case WM_CONTEXTMENU: {
        HMENU hMenu = CreatePopupMenu();
        editor::AppendMenuL(hMenu, MF_STRING, 1, L"Copy\tCtrl+C");
        editor::AppendMenuL(hMenu, MF_STRING, 2, L"Clear");
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            GetCursorPos(&point);
        }

        const int nChoice = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hWindow, nullptr);
        DestroyMenu(hMenu);
        if (nChoice == 1) {
            pPanel->CopySelection();
        } else if (nChoice == 2) {
            pPanel->Clear();
        }

        return 0;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(hWindow, GWLP_USERDATA, 0);
        delete pPanel;
        return DefWindowProcW(hWindow, uMessage, wParam, lParam);
    }

    return DefWindowProcW(hWindow, uMessage, wParam, lParam);
}
} // namespace

HWND CreateConsolePanel(HWND hParent, HINSTANCE hInstance) {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = ConsoleWindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = CONSOLE_WINDOW_CLASS;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    auto* pPanel = new ConsolePanel();
    pPanel->m_hInstance = hInstance;
    HWND hWindow = CreateWindowExW(WS_EX_CONTROLPARENT, CONSOLE_WINDOW_CLASS, L"Console", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                   0, 0, 1, 1, hParent, nullptr, hInstance, pPanel);
    if (!hWindow) {
        delete pPanel;
    }

    return hWindow;
}

void ClearConsolePanel(HWND hPanel) {
    if (auto* pPanel = reinterpret_cast<ConsolePanel*>(GetWindowLongPtrW(hPanel, GWLP_USERDATA))) {
        pPanel->Clear();
    }
}

} // namespace dxs

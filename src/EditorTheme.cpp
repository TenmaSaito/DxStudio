#include "EditorTheme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
// GDI+ provides anti-aliased vector icons (GDI lines are aliased).
#include <algorithm>

namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus

#include <objidl.h>
#include <gdiplus.h>

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace dxs::theme {
namespace {
struct ThemeState {
    std::unordered_map<HWND, ButtonSpec> buttonSpecs;
    std::unordered_set<HWND> hotWindows;
    std::unordered_map<COLORREF, HBRUSH> brushes;
    Fonts fonts;
    ULONG_PTR gdiplusToken = 0;
};

ThemeState& State() {
    static ThemeState state;
    return state;
}

void EnsureGdiplus() {
    auto& state = State();
    if (state.gdiplusToken) return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&state.gdiplusToken, &input, nullptr);
}

Gdiplus::Color ToColor(COLORREF color, BYTE nAlpha = 255) {
    return Gdiplus::Color(nAlpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

// ---- Hover tracking ---------------------------------------------------------------
LRESULT CALLBACK HoverSubclassProc(HWND hWindow, UINT uMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    auto& state = State();
    switch (uMessage) {
    case WM_MOUSEMOVE:
        if (!state.hotWindows.count(hWindow)) {
            state.hotWindows.insert(hWindow);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hWindow, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hWindow, nullptr, FALSE);
        }

        break;
    case WM_MOUSELEAVE:
        state.hotWindows.erase(hWindow);
        InvalidateRect(hWindow, nullptr, FALSE);
        break;
    case WM_ERASEBKGND: return 1; // owner draw paints every pixel; avoids flicker
    case WM_NCDESTROY:
        state.hotWindows.erase(hWindow);
        state.buttonSpecs.erase(hWindow);
        RemoveWindowSubclass(hWindow, HoverSubclassProc, 1);
        break;
    }

    return DefSubclassProc(hWindow, uMessage, wParam, lParam);
}

// ---- Vector icon helpers ------------------------------------------------------------
struct IconCanvas {
    Gdiplus::Graphics& graphics;
    float fX, fY, fSize; // unit square mapped to [fX, fX + fSize]
    COLORREF color;

    Gdiplus::PointF P(float fU, float fV) const { return {fX + fU * fSize, fY + fV * fSize}; }

    float S(float fValue) const { return fValue * fSize; }

    void Stroke(std::initializer_list<Gdiplus::PointF> points, float fWidth = 0.09f, BYTE nAlpha = 255) const {
        Gdiplus::Pen pen(ToColor(color, nAlpha), std::max(1.0f, S(fWidth)));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        std::vector<Gdiplus::PointF> v(points);
        graphics.DrawLines(&pen, v.data(), static_cast<INT>(v.size()));
    }

    void FillPolygon(std::initializer_list<Gdiplus::PointF> points, BYTE nAlpha = 255) const {
        Gdiplus::SolidBrush brush(ToColor(color, nAlpha));
        std::vector<Gdiplus::PointF> v(points);
        graphics.FillPolygon(&brush, v.data(), static_cast<INT>(v.size()));
    }

    void StrokePolygon(std::initializer_list<Gdiplus::PointF> points, float fWidth = 0.08f, BYTE nAlpha = 255) const {
        Gdiplus::Pen pen(ToColor(color, nAlpha), std::max(1.0f, S(fWidth)));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        std::vector<Gdiplus::PointF> v(points);
        graphics.DrawPolygon(&pen, v.data(), static_cast<INT>(v.size()));
    }

    void Rect(float fU, float fV, float fW, float fH, bool bFill, float fWidth = 0.08f, BYTE nAlpha = 255) const {
        if (bFill) {
            Gdiplus::SolidBrush brush(ToColor(color, nAlpha));
            graphics.FillRectangle(&brush, fX + S(fU), fY + S(fV), S(fW), S(fH));
        } else {
            Gdiplus::Pen pen(ToColor(color, nAlpha), std::max(1.0f, S(fWidth)));
            graphics.DrawRectangle(&pen, fX + S(fU), fY + S(fV), S(fW), S(fH));
        }
    }

    void Ellipse(float fU, float fV, float fW, float fH, bool bFill, float fWidth = 0.08f, BYTE nAlpha = 255) const {
        if (bFill) {
            Gdiplus::SolidBrush brush(ToColor(color, nAlpha));
            graphics.FillEllipse(&brush, fX + S(fU), fY + S(fV), S(fW), S(fH));
        } else {
            Gdiplus::Pen pen(ToColor(color, nAlpha), std::max(1.0f, S(fWidth)));
            graphics.DrawEllipse(&pen, fX + S(fU), fY + S(fV), S(fW), S(fH));
        }
    }

    void Arc(float fU, float fV, float fW, float fH, float fStart, float fSweep, float fWidth = 0.09f) const {
        Gdiplus::Pen pen(ToColor(color), std::max(1.0f, S(fWidth)));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawArc(&pen, fX + S(fU), fY + S(fV), S(fW), S(fH), fStart, fSweep);
    }
};

void Arrow(const IconCanvas& c, Gdiplus::PointF from, Gdiplus::PointF to, float fHead) {
    c.Stroke({from, to});
    const float fDx = to.X - from.X, fDy = to.Y - from.Y;
    const float fLength = std::sqrt(fDx * fDx + fDy * fDy);
    if (fLength < 0.001f) return;
    const float fUx = fDx / fLength, fUy = fDy / fLength;
    const float fHeadPx = c.S(fHead);
    c.FillPolygon({to,
                   {to.X - fUx * fHeadPx - fUy * fHeadPx * 0.7f, to.Y - fUy * fHeadPx + fUx * fHeadPx * 0.7f},
                   {to.X - fUx * fHeadPx + fUy * fHeadPx * 0.7f, to.Y - fUy * fHeadPx - fUx * fHeadPx * 0.7f}});
}

void CubeIcon(const IconCanvas& c, bool bFilled) {
    const auto top = c.P(0.5f, 0.08f), left = c.P(0.1f, 0.3f), right = c.P(0.9f, 0.3f), center = c.P(0.5f, 0.52f);
    const auto bottom = c.P(0.5f, 0.94f), bottomLeft = c.P(0.1f, 0.72f), bottomRight = c.P(0.9f, 0.72f);
    if (bFilled) {
        c.FillPolygon({top, right, center, left}, 255);
        c.FillPolygon({left, center, bottom, bottomLeft}, 170);
        c.FillPolygon({center, right, bottomRight, bottom}, 120);
    } else {
        c.StrokePolygon({top, right, bottomRight, bottom, bottomLeft, left}, 0.07f);
        c.Stroke({left, center, right}, 0.07f);
        c.Stroke({center, bottom}, 0.07f);
    }
}

void FolderIcon(const IconCanvas& c, bool bOpen) {
    c.FillPolygon({c.P(0.06f, 0.2f), c.P(0.4f, 0.2f), c.P(0.5f, 0.32f), c.P(0.94f, 0.32f), c.P(0.94f, 0.84f), c.P(0.06f, 0.84f)},
                  210);
    if (bOpen) {
        c.FillPolygon({c.P(0.18f, 0.44f), c.P(1.0f, 0.44f), c.P(0.88f, 0.84f), c.P(0.06f, 0.84f)}, 255);
    }
}

// "i" (bExclamation = false) or "!" centred at (fU, fV), drawn as shapes:
// GDI+ text would enumerate every installed font on first use.
void Glyph(const IconCanvas& c, bool bExclamation, COLORREF color, float fU, float fV) {
    const IconCanvas ink{c.graphics, c.fX, c.fY, c.fSize, color};
    if (bExclamation) {
        ink.Stroke({ink.P(fU, fV - 0.26f), ink.P(fU, fV + 0.06f)}, 0.13f);
        ink.Ellipse(fU - 0.07f, fV + 0.16f, 0.14f, 0.14f, true);
    } else {
        ink.Ellipse(fU - 0.07f, fV - 0.3f, 0.14f, 0.14f, true);
        ink.Stroke({ink.P(fU, fV - 0.06f), ink.P(fU, fV + 0.26f)}, 0.13f);
    }
}

void DrawVectorIcon(const IconCanvas& c, Icon icon) {
    using Gdiplus::PointF;
    switch (icon) {
    case Icon::HAND:
        c.Stroke({c.P(0.3f, 0.55f), c.P(0.3f, 0.2f)});
        c.Stroke({c.P(0.45f, 0.5f), c.P(0.45f, 0.1f)});
        c.Stroke({c.P(0.6f, 0.5f), c.P(0.6f, 0.14f)});
        c.Stroke({c.P(0.75f, 0.55f), c.P(0.75f, 0.28f)});
        c.Stroke({c.P(0.3f, 0.55f), c.P(0.18f, 0.45f), c.P(0.3f, 0.82f), c.P(0.68f, 0.9f), c.P(0.75f, 0.55f)});
        break;
    case Icon::MOVE:
    case Icon::TRANSFORM:
        Arrow(c, c.P(0.5f, 0.5f), c.P(0.5f, 0.06f), 0.16f);
        Arrow(c, c.P(0.5f, 0.5f), c.P(0.5f, 0.94f), 0.16f);
        Arrow(c, c.P(0.5f, 0.5f), c.P(0.06f, 0.5f), 0.16f);
        Arrow(c, c.P(0.5f, 0.5f), c.P(0.94f, 0.5f), 0.16f);
        break;
    case Icon::ROTATE:
        c.Arc(0.14f, 0.14f, 0.72f, 0.72f, -60.0f, 290.0f);
        c.FillPolygon({c.P(0.72f, 0.02f), c.P(0.9f, 0.28f), c.P(0.6f, 0.3f)});
        break;
    case Icon::SCALE:
        c.Rect(0.08f, 0.38f, 0.54f, 0.54f, false);
        Arrow(c, c.P(0.42f, 0.58f), c.P(0.9f, 0.1f), 0.2f);
        break;
    case Icon::PLAY: c.FillPolygon({c.P(0.24f, 0.12f), c.P(0.86f, 0.5f), c.P(0.24f, 0.88f)}); break;
    case Icon::STOP: c.Rect(0.2f, 0.2f, 0.6f, 0.6f, true); break;
    case Icon::PAUSE:
        c.Rect(0.22f, 0.16f, 0.18f, 0.68f, true);
        c.Rect(0.6f, 0.16f, 0.18f, 0.68f, true);
        break;
    case Icon::STEP:
        c.FillPolygon({c.P(0.14f, 0.16f), c.P(0.64f, 0.5f), c.P(0.14f, 0.84f)});
        c.Rect(0.7f, 0.16f, 0.16f, 0.68f, true);
        break;
    case Icon::SAVE:
        c.StrokePolygon({c.P(0.12f, 0.1f), c.P(0.74f, 0.1f), c.P(0.9f, 0.26f), c.P(0.9f, 0.9f), c.P(0.12f, 0.9f)});
        c.Rect(0.3f, 0.1f, 0.38f, 0.26f, false, 0.07f);
        c.Rect(0.26f, 0.56f, 0.5f, 0.34f, true, 0.07f, 200);
        break;
    case Icon::LOAD: FolderIcon(c, true); break;
    case Icon::EXPORT:
        c.Stroke({c.P(0.12f, 0.5f), c.P(0.12f, 0.9f), c.P(0.88f, 0.9f), c.P(0.88f, 0.5f)});
        Arrow(c, c.P(0.5f, 0.7f), c.P(0.5f, 0.06f), 0.22f);
        break;
    case Icon::SHADER:
        c.Ellipse(0.1f, 0.1f, 0.8f, 0.8f, true, 0.08f, 120);
        c.Ellipse(0.1f, 0.1f, 0.8f, 0.8f, false);
        c.Ellipse(0.26f, 0.22f, 0.24f, 0.2f, true, 0.08f, 255);
        break;
    case Icon::ANDROID:
        c.Arc(0.18f, 0.22f, 0.64f, 0.6f, 180.0f, 180.0f, 0.1f);
        c.Stroke({c.P(0.18f, 0.52f), c.P(0.82f, 0.52f)}, 0.1f);
        c.Stroke({c.P(0.3f, 0.28f), c.P(0.2f, 0.1f)}, 0.07f);
        c.Stroke({c.P(0.7f, 0.28f), c.P(0.8f, 0.1f)}, 0.07f);
        c.Ellipse(0.34f, 0.36f, 0.08f, 0.08f, true);
        c.Ellipse(0.58f, 0.36f, 0.08f, 0.08f, true);
        c.Rect(0.22f, 0.6f, 0.56f, 0.3f, true, 0.07f, 200);
        break;
    case Icon::GRAPHICS:
        c.Rect(0.2f, 0.2f, 0.6f, 0.6f, false);
        c.Rect(0.36f, 0.36f, 0.28f, 0.28f, true);
        for (const float fT : {0.34f, 0.5f, 0.66f}) {
            c.Stroke({c.P(fT, 0.04f), c.P(fT, 0.2f)}, 0.06f);
            c.Stroke({c.P(fT, 0.8f), c.P(fT, 0.96f)}, 0.06f);
            c.Stroke({c.P(0.04f, fT), c.P(0.2f, fT)}, 0.06f);
            c.Stroke({c.P(0.8f, fT), c.P(0.96f, fT)}, 0.06f);
        }

        break;
    case Icon::LAYOUT:
        c.Rect(0.08f, 0.12f, 0.36f, 0.76f, false, 0.08f);
        c.Rect(0.52f, 0.12f, 0.4f, 0.34f, false, 0.08f);
        c.Rect(0.52f, 0.54f, 0.4f, 0.34f, false, 0.08f);
        break;
    case Icon::PLUS:
        c.Stroke({c.P(0.5f, 0.14f), c.P(0.5f, 0.86f)}, 0.12f);
        c.Stroke({c.P(0.14f, 0.5f), c.P(0.86f, 0.5f)}, 0.12f);
        break;
    case Icon::SEARCH:
        c.Ellipse(0.1f, 0.1f, 0.54f, 0.54f, false, 0.1f);
        c.Stroke({c.P(0.58f, 0.58f), c.P(0.9f, 0.9f)}, 0.12f);
        break;
    case Icon::HIERARCHY:
        c.Rect(0.06f, 0.12f, 0.26f, 0.18f, true);
        c.Rect(0.42f, 0.44f, 0.5f, 0.16f, true, 0.08f, 220);
        c.Rect(0.42f, 0.74f, 0.5f, 0.16f, true, 0.08f, 220);
        c.Stroke({c.P(0.19f, 0.3f), c.P(0.19f, 0.82f), c.P(0.42f, 0.82f)}, 0.07f);
        c.Stroke({c.P(0.19f, 0.52f), c.P(0.42f, 0.52f)}, 0.07f);
        break;
    case Icon::SCENE:
        c.Stroke({c.P(0.36f, 0.08f), c.P(0.28f, 0.92f)}, 0.08f);
        c.Stroke({c.P(0.72f, 0.08f), c.P(0.64f, 0.92f)}, 0.08f);
        c.Stroke({c.P(0.1f, 0.34f), c.P(0.92f, 0.34f)}, 0.08f);
        c.Stroke({c.P(0.06f, 0.66f), c.P(0.88f, 0.66f)}, 0.08f);
        break;
    case Icon::INSPECTOR:
    case Icon::INFO:
        c.Ellipse(0.06f, 0.06f, 0.88f, 0.88f, true);
        Glyph(c, false, icon == Icon::INFO ? RGB(40, 40, 40) : COLOR_PANEL, 0.5f, 0.5f);
        break;
    case Icon::PROJECT:
    case Icon::FOLDER: FolderIcon(c, false); break;
    case Icon::CONSOLE:
        c.Rect(0.06f, 0.14f, 0.88f, 0.72f, false, 0.08f);
        c.Stroke({c.P(0.22f, 0.36f), c.P(0.38f, 0.5f), c.P(0.22f, 0.64f)}, 0.08f);
        c.Stroke({c.P(0.48f, 0.66f), c.P(0.74f, 0.66f)}, 0.08f);
        break;
    case Icon::KEBAB:
        for (const float fV : {0.18f, 0.5f, 0.82f}) {
            c.Ellipse(0.42f, fV - 0.08f, 0.16f, 0.16f, true);
        }

        break;
    case Icon::CLOSE:
        c.Stroke({c.P(0.22f, 0.22f), c.P(0.78f, 0.78f)}, 0.1f);
        c.Stroke({c.P(0.78f, 0.22f), c.P(0.22f, 0.78f)}, 0.1f);
        break;
    case Icon::MINIMIZE: c.Stroke({c.P(0.2f, 0.62f), c.P(0.8f, 0.62f)}, 0.1f); break;
    case Icon::FLOAT:
        c.Rect(0.32f, 0.12f, 0.56f, 0.5f, false, 0.08f);
        c.Rect(0.12f, 0.38f, 0.56f, 0.5f, true, 0.08f, 160);
        break;
    case Icon::DOCK:
        c.Rect(0.12f, 0.12f, 0.76f, 0.76f, false, 0.08f);
        c.Rect(0.12f, 0.12f, 0.3f, 0.76f, true, 0.08f, 160);
        break;
    case Icon::CHEVRON_RIGHT: c.Stroke({c.P(0.36f, 0.2f), c.P(0.66f, 0.5f), c.P(0.36f, 0.8f)}, 0.11f); break;
    case Icon::CHEVRON_DOWN: c.Stroke({c.P(0.2f, 0.36f), c.P(0.5f, 0.66f), c.P(0.8f, 0.36f)}, 0.11f); break;
    case Icon::DROPDOWN: c.FillPolygon({c.P(0.2f, 0.36f), c.P(0.8f, 0.36f), c.P(0.5f, 0.7f)}); break;
    case Icon::CHECK: c.Stroke({c.P(0.16f, 0.52f), c.P(0.4f, 0.76f), c.P(0.86f, 0.24f)}, 0.14f); break;
    case Icon::CUBE: CubeIcon(c, true); break;
    case Icon::MODEL:
        CubeIcon(c, true);
        c.Stroke({c.P(0.5f, 0.52f), c.P(0.5f, 0.94f)}, 0.05f);
        break;
    case Icon::GAME_OBJECT: CubeIcon(c, false); break;
    case Icon::QUAD: c.FillPolygon({c.P(0.5f, 0.1f), c.P(0.9f, 0.5f), c.P(0.5f, 0.9f), c.P(0.1f, 0.5f)}, 220); break;
    case Icon::CAMERA:
        c.Rect(0.06f, 0.3f, 0.6f, 0.44f, true);
        c.FillPolygon({c.P(0.66f, 0.52f), c.P(0.94f, 0.3f), c.P(0.94f, 0.74f)});
        c.Ellipse(0.12f, 0.08f, 0.22f, 0.22f, false, 0.07f);
        c.Ellipse(0.38f, 0.08f, 0.22f, 0.22f, false, 0.07f);
        break;
    case Icon::IMAGE:
        c.Rect(0.08f, 0.14f, 0.84f, 0.72f, false, 0.08f);
        c.FillPolygon({c.P(0.14f, 0.8f), c.P(0.4f, 0.46f), c.P(0.58f, 0.66f), c.P(0.7f, 0.54f), c.P(0.86f, 0.8f)});
        c.Ellipse(0.64f, 0.24f, 0.14f, 0.14f, true);
        break;
    case Icon::RENDERER:
        c.StrokePolygon({c.P(0.5f, 0.08f), c.P(0.92f, 0.86f), c.P(0.08f, 0.86f)}, 0.07f);
        c.Stroke({c.P(0.29f, 0.47f), c.P(0.71f, 0.47f), c.P(0.5f, 0.86f), c.P(0.29f, 0.47f)}, 0.06f);
        break;
    case Icon::SCRIPT:
    case Icon::FILE:
        c.StrokePolygon({c.P(0.18f, 0.06f), c.P(0.62f, 0.06f), c.P(0.84f, 0.28f), c.P(0.84f, 0.94f), c.P(0.18f, 0.94f)}, 0.07f);
        c.Stroke({c.P(0.62f, 0.06f), c.P(0.62f, 0.28f), c.P(0.84f, 0.28f)}, 0.06f);
        if (icon == Icon::SCRIPT) {
            c.Stroke({c.P(0.42f, 0.44f), c.P(0.3f, 0.6f), c.P(0.42f, 0.76f)}, 0.07f);
            c.Stroke({c.P(0.6f, 0.44f), c.P(0.72f, 0.6f), c.P(0.6f, 0.76f)}, 0.07f);
        }

        break;
    case Icon::WARNING:
        c.FillPolygon({c.P(0.5f, 0.06f), c.P(0.96f, 0.9f), c.P(0.04f, 0.9f)});
        Glyph(c, true, RGB(40, 40, 40), 0.5f, 0.58f);
        break;
    case Icon::ERROR_ICON:
        c.Ellipse(0.06f, 0.06f, 0.88f, 0.88f, true);
        Glyph(c, true, RGB(40, 40, 40), 0.5f, 0.5f);
        break;
    case Icon::TRASH:
        c.Stroke({c.P(0.12f, 0.24f), c.P(0.88f, 0.24f)}, 0.08f);
        c.Stroke({c.P(0.38f, 0.24f), c.P(0.4f, 0.1f), c.P(0.6f, 0.1f), c.P(0.62f, 0.24f)}, 0.07f);
        c.StrokePolygon({c.P(0.22f, 0.24f), c.P(0.78f, 0.24f), c.P(0.72f, 0.92f), c.P(0.28f, 0.92f)}, 0.07f);
        break;
    case Icon::PERSPECTIVE:
        c.StrokePolygon({c.P(0.3f, 0.3f), c.P(0.7f, 0.3f), c.P(0.92f, 0.86f), c.P(0.08f, 0.86f)}, 0.08f);
        break;
    case Icon::ORTHOGRAPHIC: c.Rect(0.14f, 0.2f, 0.72f, 0.64f, false, 0.08f); break;
    case Icon::NONE: break;
    }
}

COLORREF RoleBackground(ButtonRole role, HWND hButton) {
    switch (role) {
    case ButtonRole::TOOL:
    case ButtonRole::TRANSPORT:
    case ButtonRole::MENU_BAR:
    case ButtonRole::DROPDOWN:
        // Toolbar children of the main window sit on the toolbar colour; tool
        // buttons inside a panel sit on the panel's toolbar strip.
        return GetParent(hButton) && GetAncestor(hButton, GA_PARENT) == GetAncestor(hButton, GA_ROOT) ? COLOR_TOOLBAR
                                                                                                      : COLOR_PANEL;
    case ButtonRole::HEADER: return COLOR_HEADER;
    default: return COLOR_PANEL;
    }
}
} // namespace

// ---- Public API -----------------------------------------------------------------------
void DrawIcon(HDC hDc, Icon icon, const RECT& box, COLORREF color) {
    if (icon == Icon::NONE) return;
    EnsureGdiplus();
    Gdiplus::Graphics graphics(hDc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const float fWidth = static_cast<float>(box.right - box.left), fHeight = static_cast<float>(box.bottom - box.top);
    const float fSize = std::max(4.0f, std::min(fWidth, fHeight));
    const IconCanvas canvas{graphics, box.left + (fWidth - fSize) * 0.5f, box.top + (fHeight - fSize) * 0.5f, fSize, color};
    DrawVectorIcon(canvas, icon);
}

void SetButtonSpec(HWND hButton, const ButtonSpec& spec) {
    if (!hButton) return;
    auto& state = State();
    const bool bNew = !state.buttonSpecs.count(hButton);
    state.buttonSpecs[hButton] = spec;
    if (bNew) {
        SetWindowSubclass(hButton, HoverSubclassProc, 1, 0);
    }

    InvalidateRect(hButton, nullptr, FALSE);
}

ButtonSpec GetButtonSpec(HWND hButton) {
    auto& state = State();
    const auto found = state.buttonSpecs.find(hButton);
    return found == state.buttonSpecs.end() ? ButtonSpec{} : found->second;
}

void SetButtonIcon(HWND hButton, Icon icon) {
    auto spec = GetButtonSpec(hButton);
    if (spec.icon == icon) return;
    spec.icon = icon;
    SetButtonSpec(hButton, spec);
}

void SetButtonExpanded(HWND hButton, bool bExpanded) {
    auto spec = GetButtonSpec(hButton);
    if (spec.bExpanded == bExpanded) return;
    spec.bExpanded = bExpanded;
    SetButtonSpec(hButton, spec);
}

bool IsHot(HWND hWindow) {
    return State().hotWindows.count(hWindow) != 0;
}

void DrawButton(const DRAWITEMSTRUCT& item, bool bActive) {
    const auto spec = GetButtonSpec(item.hwndItem);
    HDC hDc = item.hDC;
    const RECT rect = item.rcItem;
    const bool bPressed = (item.itemState & ODS_SELECTED) != 0;
    const bool bDisabled = (item.itemState & ODS_DISABLED) != 0;
    const bool bHot = IsHot(item.hwndItem) && !bDisabled;
    wchar_t aText[160]{};
    GetWindowTextW(item.hwndItem, aText, 160);
    const std::wstring sText = aText;
    const auto& fonts = GetFonts();
    const COLORREF background = spec.background != CLR_INVALID ? spec.background : RoleBackground(spec.role, item.hwndItem);
    Fill(hDc, rect, background);

    COLORREF fill = background;
    COLORREF textColor = bDisabled ? COLOR_TEXT_DISABLED : COLOR_TEXT;
    COLORREF border = CLR_INVALID;
    switch (spec.role) {
    case ButtonRole::PUSH:
        fill = bActive ? COLOR_ACCENT : bPressed ? COLOR_BUTTON_PRESSED : bHot ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
        border = COLOR_BUTTON_BORDER;
        if (bActive) {
            textColor = COLOR_TEXT_BRIGHT;
        }

        break;
    case ButtonRole::PRIMARY:
        fill = bDisabled  ? COLOR_BUTTON
               : bPressed ? Blend(COLOR_ACCENT, RGB(0, 0, 0), 0.25f)
               : bHot     ? COLOR_ACCENT_HOVER
                          : COLOR_ACCENT;
        border = Blend(fill, RGB(0, 0, 0), 0.35f);
        if (!bDisabled) {
            textColor = COLOR_TEXT_BRIGHT;
        }

        break;
    case ButtonRole::TOOL:
    case ButtonRole::TRANSPORT:
    case ButtonRole::DROPDOWN:
    case ButtonRole::ICON:
        fill = bActive    ? COLOR_ACCENT
               : bPressed ? COLOR_BUTTON_PRESSED
               : bHot     ? Blend(background, RGB(255, 255, 255), 0.12f)
                          : background;
        if (bActive) {
            textColor = COLOR_TEXT_BRIGHT;
        }

        if (spec.role == ButtonRole::TRANSPORT || spec.role == ButtonRole::DROPDOWN) {
            if (!bActive && !bHot && !bPressed) {
                fill = Blend(background, RGB(255, 255, 255), 0.06f);
            }

            border = Blend(background, RGB(0, 0, 0), 0.4f);
        }

        break;
    case ButtonRole::MENU_BAR:
        fill = bPressed || bActive ? COLOR_SELECTION : bHot ? Blend(background, RGB(255, 255, 255), 0.14f) : background;
        break;
    case ButtonRole::TAB:
        fill = bActive ? COLOR_TAB_ACTIVE : bHot ? Blend(COLOR_TAB_STRIP, RGB(255, 255, 255), 0.06f) : COLOR_TAB_STRIP;
        if (!bActive) {
            textColor = COLOR_TEXT_DIM;
        }

        break;
    case ButtonRole::HEADER: fill = bHot ? COLOR_HEADER_HOVER : COLOR_HEADER; break;
    case ButtonRole::CHECKBOX: break;
    }

    if (spec.role == ButtonRole::HEADER) {
        // Unity component header: separator, foldout, icon, bold title, menu.
        Fill(hDc, rect, fill);
        Line(hDc, rect.left, rect.top, rect.right, rect.top, COLOR_SEPARATOR);
        const int nMid = (rect.top + rect.bottom) / 2;
        RECT chevron{rect.left + 4, nMid - 6, rect.left + 16, nMid + 6};
        DrawIcon(hDc, spec.bExpanded ? Icon::CHEVRON_DOWN : Icon::CHEVRON_RIGHT, chevron, COLOR_TEXT_DIM);
        RECT iconBox{rect.left + 20, nMid - 8, rect.left + 36, nMid + 8};
        DrawIcon(hDc, spec.icon, iconBox, spec.iconColor == CLR_INVALID ? COLOR_TEXT : spec.iconColor);
        RECT textRect{rect.left + 42, rect.top, rect.right - 28, rect.bottom};
        DrawLabel(hDc, sText, textRect, COLOR_TEXT_BRIGHT, fonts.hBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT kebab{rect.right - 22, nMid - 7, rect.right - 8, nMid + 7};
        DrawIcon(hDc, Icon::KEBAB, kebab, COLOR_TEXT_DIM);
        return;
    }

    if (spec.role == ButtonRole::FOLDOUT) {
        Fill(hDc, rect, background);
        const int nMid = (rect.top + rect.bottom) / 2;
        RECT chevron{rect.left, nMid - 5, rect.left + 10, nMid + 5};
        DrawIcon(hDc, spec.bExpanded ? Icon::CHEVRON_DOWN : Icon::CHEVRON_RIGHT, chevron,
                 bHot ? COLOR_TEXT_BRIGHT : COLOR_TEXT_DIM);
        RECT textRect{rect.left + 14, rect.top, rect.right, rect.bottom};
        DrawLabel(hDc, sText, textRect, textColor, fonts.hRegular, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    if (spec.role == ButtonRole::CHECKBOX) {
        const int nMid = (rect.top + rect.bottom) / 2;
        RECT box{rect.left + 2, nMid - 7, rect.left + 16, nMid + 7};
        RoundFill(hDc, box, 2, bHot ? COLOR_BUTTON_PRESSED : COLOR_INPUT, bHot ? COLOR_FOCUS : COLOR_INPUT_BORDER);
        if (bActive) {
            RECT check = box;
            InflateRect(&check, -1, -1);
            DrawIcon(hDc, Icon::CHECK, check, bDisabled ? COLOR_TEXT_DISABLED : COLOR_TEXT_BRIGHT);
        }

        if (!sText.empty()) {
            RECT textRect{box.right + 6, rect.top, rect.right, rect.bottom};
            DrawLabel(hDc, sText, textRect, textColor, fonts.hRegular, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        return;
    }

    RECT body = rect;
    if (spec.role == ButtonRole::TOOL || spec.role == ButtonRole::ICON || spec.role == ButtonRole::TRANSPORT ||
        spec.role == ButtonRole::DROPDOWN || spec.role == ButtonRole::PUSH || spec.role == ButtonRole::PRIMARY) {
        InflateRect(&body, -1, -1);
        RoundFill(hDc, body, 3, fill, border == CLR_INVALID ? fill : border);
    } else {
        Fill(hDc, body, fill);
    }

    if (spec.role == ButtonRole::TAB && bActive) {
        Line(hDc, rect.left, rect.top, rect.right, rect.top, COLOR_TAB_ACCENT, 2);
    }

    // Content: [icon] [text] [chevron]
    const int nHeight = rect.bottom - rect.top;
    const int nIconSize = std::clamp(nHeight - 12, 10, 16);
    const bool bHasText = !sText.empty() && spec.role != ButtonRole::ICON && spec.role != ButtonRole::TRANSPORT;
    const bool bHasIcon = spec.icon != Icon::NONE;
    const int nChevron = spec.role == ButtonRole::DROPDOWN ? 12 : 0;
    SIZE textSize{};
    HFONT hFont = spec.role == ButtonRole::TAB && bActive ? fonts.hBold : fonts.hRegular;
    if (bHasText) {
        HGDIOBJ hOld = SelectObject(hDc, hFont);
        GetTextExtentPoint32W(hDc, sText.c_str(), static_cast<int>(sText.size()), &textSize);
        SelectObject(hDc, hOld);
    }

    const int nGap = bHasIcon && bHasText ? 5 : 0;
    const int nContent = (bHasIcon ? nIconSize : 0) + nGap + (bHasText ? textSize.cx : 0) + (nChevron ? nChevron + 3 : 0);
    const bool bLeftAligned = spec.role == ButtonRole::TAB || spec.role == ButtonRole::MENU_BAR;
    int nX = bLeftAligned ? rect.left + 8 : rect.left + std::max(4, (static_cast<int>(rect.right - rect.left) - nContent) / 2);
    const int nMid = (rect.top + rect.bottom) / 2;
    if (bHasIcon) {
        RECT iconBox{nX, nMid - nIconSize / 2, nX + nIconSize, nMid - nIconSize / 2 + nIconSize};
        COLORREF iconColor = spec.iconColor == CLR_INVALID || bActive ? textColor : spec.iconColor;
        if (bDisabled) {
            iconColor = COLOR_TEXT_DISABLED;
        }

        DrawIcon(hDc, spec.icon, iconBox, iconColor);
        nX += nIconSize + nGap;
    }

    if (bHasText) {
        RECT textRect{nX, rect.top, std::max<LONG>(nX, rect.right - 4 - (nChevron ? nChevron + 3 : 0)), rect.bottom};
        DrawLabel(hDc, sText, textRect, textColor, hFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        nX += textSize.cx + 3;
    }

    if (nChevron) {
        RECT chevron{std::min<LONG>(nX, rect.right - nChevron - 4), nMid - 5,
                     std::min<LONG>(nX, rect.right - nChevron - 4) + nChevron, nMid + 5};
        DrawIcon(hDc, Icon::DROPDOWN, chevron, COLOR_TEXT_DIM);
    }

    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT) && spec.role != ButtonRole::MENU_BAR) {
        RECT focus = rect;
        InflateRect(&focus, -1, -1);
        Frame(hDc, focus, COLOR_FOCUS);
    }
}

void Fill(HDC hDc, const RECT& rect, COLORREF color) {
    FillRect(hDc, &rect, Brush(color));
}

void Frame(HDC hDc, const RECT& rect, COLORREF color) {
    FrameRect(hDc, &rect, Brush(color));
}

void Line(HDC hDc, int nX0, int nY0, int nX1, int nY1, COLORREF color, int nWidth) {
    HPEN hPen = CreatePen(PS_SOLID, nWidth, color);
    HGDIOBJ hOld = SelectObject(hDc, hPen);
    MoveToEx(hDc, nX0, nY0, nullptr);
    LineTo(hDc, nX1, nY1);
    SelectObject(hDc, hOld);
    DeleteObject(hPen);
}

void RoundFill(HDC hDc, const RECT& rect, int nRadius, COLORREF fill, COLORREF border) {
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ hOldPen = SelectObject(hDc, hPen);
    HGDIOBJ hOldBrush = SelectObject(hDc, Brush(fill));
    RoundRect(hDc, rect.left, rect.top, rect.right, rect.bottom, nRadius * 2, nRadius * 2);
    SelectObject(hDc, hOldBrush);
    SelectObject(hDc, hOldPen);
    DeleteObject(hPen);
}

void DrawLabel(HDC hDc, const std::wstring& sText, RECT rect, COLORREF color, HFONT hFont, UINT uFormat) {
    const int nOldMode = SetBkMode(hDc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(hDc, color);
    HGDIOBJ hOld = SelectObject(hDc, hFont ? hFont : GetFonts().hRegular);
    DrawTextW(hDc, sText.c_str(), static_cast<int>(sText.size()), &rect, uFormat);
    SelectObject(hDc, hOld);
    SetTextColor(hDc, oldColor);
    SetBkMode(hDc, nOldMode);
}

HBRUSH Brush(COLORREF color) {
    auto& brushes = State().brushes;
    const auto found = brushes.find(color);
    if (found != brushes.end()) return found->second;
    HBRUSH hBrush = CreateSolidBrush(color);
    brushes.emplace(color, hBrush);
    return hBrush;
}

COLORREF Blend(COLORREF a, COLORREF b, float fAmount) {
    auto mix = [fAmount](int nA, int nB) {
        return static_cast<BYTE>(std::clamp(nA + (nB - nA) * fAmount, 0.0f, 255.0f));
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void InitializeFonts(UINT uDpi) {
    auto& fonts = State().fonts;
    for (HFONT hFont : {fonts.hRegular, fonts.hBold, fonts.hSmall, fonts.hTitle}) {
        if (hFont) {
            DeleteObject(hFont);
        }
    }

    auto make = [uDpi](int nPixels, int nWeight) {
        return CreateFontW(-MulDiv(nPixels, static_cast<int>(uDpi), 96), 0, 0, 0, nWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    fonts.hRegular = make(12, FW_NORMAL);
    fonts.hBold = make(12, FW_SEMIBOLD);
    fonts.hSmall = make(11, FW_NORMAL);
    fonts.hTitle = make(13, FW_SEMIBOLD);
    EnsureGdiplus();
}

const Fonts& GetFonts() {
    auto& fonts = State().fonts;
    if (!fonts.hRegular) {
        InitializeFonts(96);
    }

    return fonts;
}

void ReleaseResources() {
    auto& state = State();
    for (auto& [color, hBrush] : state.brushes) {
        DeleteObject(hBrush);
    }

    state.brushes.clear();
    for (HFONT hFont : {state.fonts.hRegular, state.fonts.hBold, state.fonts.hSmall, state.fonts.hTitle}) {
        if (hFont) {
            DeleteObject(hFont);
        }
    }

    state.fonts = {};
}

// ---- Dark mode -------------------------------------------------------------------------
namespace {
// Undocumented but widely used uxtheme exports (Windows 10 1809+). They are
// resolved by ordinal and every call is optional.
using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void(WINAPI*)();

HMODULE UxTheme() {
    static HMODULE hModule = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return hModule;
}

template<class T>
T UxOrdinal(int nOrdinal) {
    HMODULE hModule = UxTheme();
    return hModule ? reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(hModule, MAKEINTRESOURCEA(nOrdinal)))) : nullptr;
}
} // namespace

void EnableDarkMode() {
    constexpr int FORCE_DARK = 2;
    if (auto pSetMode = UxOrdinal<SetPreferredAppModeFn>(135)) {
        pSetMode(FORCE_DARK);
    }

    if (auto pFlush = UxOrdinal<FlushMenuThemesFn>(136)) {
        pFlush();
    }

    EnsureGdiplus();
}

void ApplyDarkTitleBar(HWND hWindow) {
    if (auto pAllow = UxOrdinal<AllowDarkModeForWindowFn>(133)) {
        pAllow(hWindow, TRUE);
    }

    const BOOL bDark = TRUE;
    constexpr DWORD DWMWA_DARK_MODE = 20, DWMWA_DARK_MODE_OLD = 19, DWMWA_CAPTION = 35, DWMWA_BORDER = 34;
    if (FAILED(DwmSetWindowAttribute(hWindow, DWMWA_DARK_MODE, &bDark, sizeof(bDark)))) {
        DwmSetWindowAttribute(hWindow, DWMWA_DARK_MODE_OLD, &bDark, sizeof(bDark));
    }

    const COLORREF caption = COLOR_TOOLBAR;
    DwmSetWindowAttribute(hWindow, DWMWA_CAPTION, &caption, sizeof(caption)); // Windows 11 only
    const COLORREF border = RGB(45, 45, 45);
    DwmSetWindowAttribute(hWindow, DWMWA_BORDER, &border, sizeof(border));
}

void ApplyDarkControl(HWND hControl) {
    if (auto pAllow = UxOrdinal<AllowDarkModeForWindowFn>(133)) {
        pAllow(hControl, TRUE);
    }

    SetWindowTheme(hControl, L"DarkMode_Explorer", nullptr);
}

void ApplyDarkInput(HWND hControl) {
    if (auto pAllow = UxOrdinal<AllowDarkModeForWindowFn>(133)) {
        pAllow(hControl, TRUE);
    }

    SetWindowTheme(hControl, L"DarkMode_CFD", nullptr);
}

} // namespace dxs::theme

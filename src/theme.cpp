// Forest Code - 主题实现
#include "theme.h"
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

namespace fc {

Theme g_theme;

// ================= GDI+ 生命周期 =================
static ULONG_PTR g_gdiplusToken = 0;

void GfxInit() {
    if (g_gdiplusToken) return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
}

void GfxShutdown() {
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

static Gdiplus::Color GP(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// ================= Theme =================
HFONT Theme::MakeFont(const std::wstring &face, int pt, UINT dpi, int weight, bool italic) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pt, (int)dpi, 72);
    lf.lfWeight = weight;
    lf.lfItalic = italic ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lstrcpynW(lf.lfFaceName, face.c_str(), LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

void Theme::DestroyFonts() {
    HFONT fs[] = { fUi_, fUiBold_, fUiSmall_, fUiSmallBold_, fIcon_, fIconSmall_, fMono_ };
    for (HFONT h : fs) if (h) DeleteObject(h);
    fUi_ = fUiBold_ = fUiSmall_ = fUiSmallBold_ = fIcon_ = fIconSmall_ = fMono_ = nullptr;
}

void Theme::SetDpi(UINT dpi) {
    if (dpi < 72) dpi = 96;
    if (dpi_ == dpi && fUi_) return;
    dpi_ = dpi;
    DestroyFonts();
    fUi_        = MakeFont(f.ui,    f.uiSize,    dpi, FW_NORMAL);
    fUiBold_    = MakeFont(f.ui,    f.uiSize,    dpi, FW_SEMIBOLD);
    fUiSmall_   = MakeFont(f.ui,    f.uiSmall,   dpi, FW_NORMAL);
    fUiSmallBold_ = MakeFont(f.ui,  f.uiSmall,   dpi, FW_SEMIBOLD);
    fIcon_      = MakeFont(f.icon,  f.iconSize,  dpi, FW_NORMAL);
    fIconSmall_ = MakeFont(f.icon,  f.iconSize - 1, dpi, FW_NORMAL);
    fMono_      = MakeFont(f.code,  f.uiSize,    dpi, FW_NORMAL);
}

void Theme::Init(HWND hwnd) {
    UINT dpi = 96;
    if (hwnd) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        typedef UINT(WINAPI *PFN)(HWND);
        PFN pGetDpiForWindow = (PFN)GetProcAddress(user32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
    }
    if (dpi == 0) dpi = 96;
    SetDpi(dpi);
}

// ================= 绘图辅助 =================
void FillRectC(HDC dc, const RECT &r, COLORREF col) {
    HBRUSH b = CreateSolidBrush(col);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void FillRound(HDC dc, const RECT &r, int radius, COLORREF col) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush br(GP(col));
    int d = std::max(1, radius * 2);
    Gdiplus::GraphicsPath path;
    Gdiplus::Rect rr(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (radius <= 0) { g.FillRectangle(&br, rr); return; }
    path.AddArc(rr.X, rr.Y, d, d, 180, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0, 90);
    path.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90, 90);
    path.CloseFigure();
    g.FillPath(&br, &path);
}

void FillRound2(HDC dc, const RECT &r, int radius, COLORREF top, COLORREF bottom) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::LinearGradientBrush br(
        Gdiplus::Rect(r.left, r.top, std::max(1, (int)(r.right - r.left)), std::max(1, (int)(r.bottom - r.top))),
        GP(top), GP(bottom), Gdiplus::LinearGradientModeVertical);
    int d = std::max(1, radius * 2);
    Gdiplus::Rect rr(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (radius <= 0) { g.FillRectangle(&br, rr); return; }
    Gdiplus::GraphicsPath path;
    path.AddArc(rr.X, rr.Y, d, d, 180, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0, 90);
    path.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90, 90);
    path.CloseFigure();
    g.FillPath(&br, &path);
}

void StrokeRound(HDC dc, const RECT &r, int radius, COLORREF col, int width) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(GP(col), (Gdiplus::REAL)std::max(1, width));
    int d = std::max(1, radius * 2);
    Gdiplus::Rect rr(r.left, r.top, r.right - r.left - 1, r.bottom - r.top - 1);
    if (radius <= 0) { g.DrawRectangle(&pen, rr); return; }
    Gdiplus::GraphicsPath path;
    path.AddArc(rr.X, rr.Y, d, d, 180, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0, 90);
    path.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90, 90);
    path.CloseFigure();
    g.DrawPath(&pen, &path);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int width) {
    HPEN p = CreatePen(PS_SOLID, width, col);
    HGDIOBJ old = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(p);
}

void DrawVLine(HDC dc, int x, int y1, int y2, COLORREF col) { DrawLine(dc, x, y1, x, y2, col); }
void DrawHLine(HDC dc, int x1, int x2, int y, COLORREF col) { DrawLine(dc, x1, y, x2, y, col); }

void DrawTextC(HDC dc, const std::wstring &s, RECT r, COLORREF col, HFONT font, UINT flags) {
    if (s.empty()) return;
    HGDIOBJ oldF = font ? SelectObject(dc, font) : nullptr;
    int oldBk = SetBkMode(dc, TRANSPARENT);
    COLORREF oldC = SetTextColor(dc, col);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags);
    SetTextColor(dc, oldC);
    SetBkMode(dc, oldBk);
    if (oldF) SelectObject(dc, oldF);
}

void DrawTextUtf8(HDC dc, const std::string &s, RECT r, COLORREF col, HFONT font, UINT flags) {
    DrawTextC(dc, U2W(s), r, col, font, flags);
}

int TextWidth(HDC dc, const std::wstring &s, HFONT font) {
    HGDIOBJ oldF = font ? SelectObject(dc, font) : nullptr;
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    if (oldF) SelectObject(dc, oldF);
    return sz.cx;
}

void DrawIconC(HDC dc, unsigned int glyphCode, RECT r, COLORREF col, HFONT font, UINT align) {
    wchar_t buf[2] = { (wchar_t)glyphCode, 0 };
    // 注意：DT_VCENTER 只有配合 DT_SINGLELINE 才生效，漏了就会变成顶对齐
    DrawTextC(dc, buf, r, col, font ? font : g_theme.Icon(), align | DT_NOPREFIX | DT_SINGLELINE);
}

void FillParentBg(HDC dc, HWND hwnd, const RECT &r, COLORREF col) {
    FillRectC(dc, r, col);
}

// ================= 深色模式 =================
typedef enum { APPMODE_DEFAULT, APPMODE_ALLOWDARK, APPMODE_FORCEDARK, APPMODE_FORCELIGHT, APPMODE_MAX } FC_APPMODE;
typedef FC_APPMODE(WINAPI *fnSetPreferredAppMode)(FC_APPMODE);
typedef BOOL(WINAPI *fnAllowDarkModeForWindow)(HWND, BOOL);
typedef void(WINAPI *fnFlushMenuThemes)();
typedef void(WINAPI *fnRefreshImmersiveColorPolicyState)();

static fnSetPreferredAppMode g_setPreferredAppMode = nullptr;
static fnAllowDarkModeForWindow g_allowDarkModeForWindow = nullptr;

void EnableAppDarkMode() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    g_setPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    g_allowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    auto flushMenuThemes = (fnFlushMenuThemes)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    auto refreshPolicy = (fnRefreshImmersiveColorPolicyState)GetProcAddress(ux, MAKEINTRESOURCEA(104));
    if (g_setPreferredAppMode) g_setPreferredAppMode(APPMODE_FORCEDARK);
    if (refreshPolicy) refreshPolicy();
    if (flushMenuThemes) flushMenuThemes();
}

void ApplyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 2004+)
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    COLORREF cap = g_theme.c.bgRoot;
    COLORREF tx  = g_theme.c.text;
    COLORREF bd  = g_theme.c.border;
    DwmSetWindowAttribute(hwnd, 35, &cap, sizeof(cap));  // CAPTION_COLOR
    DwmSetWindowAttribute(hwnd, 36, &tx,  sizeof(tx));   // TEXT_COLOR
    DwmSetWindowAttribute(hwnd, 34, &bd,  sizeof(bd));   // BORDER_COLOR
}

void EnableRoundedCorners(HWND hwnd) {
    int pref = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref));
}

void SetWindowShadow(HWND hwnd) {
    MARGINS m{ 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);
}

void DarkenWindow(HWND hwnd) {
    if (g_allowDarkModeForWindow) g_allowDarkModeForWindow(hwnd, TRUE);
}

} // namespace fc

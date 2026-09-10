// Forest Code - 主题：配色 / 度量 / 字体 / 绘图
#pragma once
#include <windows.h>
#include <string>
#include "util.h"

namespace fc {

// ============ 配色（森林：深绿底 + 浅绿强调） ============
struct Palette {
    // 背景层级（由深到浅）
    COLORREF bgRoot     = RGB(0x0A, 0x15, 0x0F);   // 窗口最底
    COLORREF bgPanel    = RGB(0x11, 0x24, 0x1A);   // 面板 / 侧栏 / 工具栏
    COLORREF bgEditor   = RGB(0x0D, 0x1B, 0x13);   // 编辑器
    COLORREF bgSunken   = RGB(0x09, 0x15, 0x0F);   // 输入框 / 输出区
    COLORREF bgRaised   = RGB(0x18, 0x32, 0x21);   // 选中 / 卡片
    COLORREF bgHover    = RGB(0x1E, 0x3C, 0x28);   // 悬停
    COLORREF bgActive   = RGB(0x24, 0x47, 0x30);   // 按下

    // 描边
    COLORREF border     = RGB(0x25, 0x44, 0x30);
    COLORREF borderSoft = RGB(0x1A, 0x30, 0x22);
    COLORREF borderGlow = RGB(0x33, 0x5E, 0x41);

    // 文字
    COLORREF text       = RGB(0xE3, 0xF4, 0xE7);
    COLORREF textMuted  = RGB(0x9C, 0xC0, 0xAB);
    COLORREF textFaint  = RGB(0x6B, 0x8C, 0x79);
    COLORREF textOnAcc  = RGB(0x06, 0x14, 0x0B);

    // 主色
    COLORREF accent     = RGB(0x4E, 0xD1, 0x7E);   // 亮薄荷绿
    COLORREF accentSoft = RGB(0x8A, 0xE2, 0xAA);
    COLORREF accentDim  = RGB(0x2A, 0x6E, 0x45);
    COLORREF accentDeep = RGB(0x18, 0x3D, 0x27);

    // 语义
    COLORREF ok         = RGB(0x4E, 0xD1, 0x7E);
    COLORREF warn       = RGB(0xE8, 0xC4, 0x68);
    COLORREF error      = RGB(0xF2, 0x76, 0x76);
    COLORREF info       = RGB(0x6F, 0xC8, 0xE8);
    COLORREF violet     = RGB(0xC0, 0xA6, 0xF0);

    // 语法
    COLORREF synDefault   = RGB(0xD8, 0xEC, 0xDD);
    COLORREF synKeyword   = RGB(0x5B, 0xD8, 0x88);
    COLORREF synType      = RGB(0x6F, 0xD9, 0xC0);
    COLORREF synFunction  = RGB(0xA6, 0xE3, 0xB8);
    COLORREF synString    = RGB(0xBE, 0xE4, 0x8E);
    COLORREF synNumber    = RGB(0xF0, 0xA8, 0x68);
    COLORREF synPreproc   = RGB(0xE8, 0xC4, 0x68);
    COLORREF synComment   = RGB(0x5C, 0x7D, 0x68);
    COLORREF synOperator  = RGB(0x9F, 0xBF, 0xAE);
    COLORREF synGlobal    = RGB(0x8A, 0xE2, 0xAA);

    // 编辑器杂项
    COLORREF caret        = RGB(0x4E, 0xD1, 0x7E);
    COLORREF caretLine    = RGB(0x13, 0x24, 0x19);
    COLORREF selection    = RGB(0x1E, 0x4A, 0x30);
    COLORREF gutterBg     = RGB(0x09, 0x13, 0x0D);
    COLORREF gutterText   = RGB(0x46, 0x64, 0x53);
    COLORREF matchHighlight = RGB(0x1B, 0x3A, 0x26);
    COLORREF indentGuide  = RGB(0x18, 0x2C, 0x20);
};

// ============ 度量（未缩放，按 DPI 换算） ============
struct Metrics {
    int titleH   = 38;
    int toolH    = 46;
    int tabH     = 36;
    int statusH  = 26;
    int sideW    = 264;
    int bottomH  = 208;
    int radius   = 7;
    int pad      = 10;
    int rowH     = 26;
};

// ============ 字体配置 ============
struct FontCfg {
    std::wstring ui      = L"Microsoft YaHei UI";
    std::wstring icon    = L"Segoe Fluent Icons";
    std::wstring code    = L"Cascadia Code";
    int uiSize    = 9;
    int uiSmall   = 8;
    int iconSize  = 10;
    int codeSize  = 11;
};

class Theme {
public:
    Palette c;
    Metrics m;
    FontCfg f;

    void Init(HWND hwnd);            // 取 DPI、建字体
    void SetDpi(UINT dpi, bool force = false);   // 重建字体；改了字号要 force
    UINT Dpi() const { return dpi_; }

    // 缩放
    int S(int px) const { return MulDiv(px, (int)dpi_, 96); }
    int TitleH()  const { return S(m.titleH); }
    int ToolH()   const { return S(m.toolH); }
    int TabH()    const { return S(m.tabH); }
    int StatusH() const { return S(m.statusH); }
    int Radius()  const { return S(m.radius); }
    int Pad()     const { return S(m.pad); }
    int RowH()    const { return S(m.rowH); }

    // 字体
    HFONT Ui()       const { return fUi_; }
    HFONT UiBold()   const { return fUiBold_; }
    HFONT UiSmall()  const { return fUiSmall_; }
    HFONT UiSmallBold() const { return fUiSmallBold_; }
    HFONT Icon()     const { return fIcon_; }
    HFONT IconSmall()const { return fIconSmall_; }
    HFONT Mono()     const { return fMono_; }

private:
    UINT dpi_ = 96;
    HFONT fUi_ = nullptr, fUiBold_ = nullptr, fUiSmall_ = nullptr, fUiSmallBold_ = nullptr;
    HFONT fIcon_ = nullptr, fIconSmall_ = nullptr, fMono_ = nullptr;
    void DestroyFonts();
    static HFONT MakeFont(const std::wstring &face, int pt, UINT dpi, int weight, bool italic = false);
};

extern Theme g_theme;

// ============ 绘图辅助 ============
void GfxInit();
void GfxShutdown();

void FillRectC(HDC dc, const RECT &r, COLORREF col);
void FillRound(HDC dc, const RECT &r, int radius, COLORREF col);
void FillRound2(HDC dc, const RECT &r, int radius, COLORREF top, COLORREF bottom);  // 垂直渐变
void StrokeRound(HDC dc, const RECT &r, int radius, COLORREF col, int width = 1);
void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int width = 1);
void DrawVLine(HDC dc, int x, int y1, int y2, COLORREF col);
void DrawHLine(HDC dc, int x1, int x2, int y, COLORREF col);

// 文本（UTF-8 / 宽字符）
void DrawTextC(HDC dc, const std::wstring &s, RECT r, COLORREF col, HFONT font,
               UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
void DrawTextUtf8(HDC dc, const std::string &s, RECT r, COLORREF col, HFONT font,
                  UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
int  TextWidth(HDC dc, const std::wstring &s, HFONT font);

// 品牌标记：小松树矢量（与 exe 图标同款）
void DrawLogoMark(HDC dc, const RECT &r, COLORREF col);

// 图标（Segoe Fluent Icons 码点）
void DrawIconC(HDC dc, unsigned int glyph, RECT r, COLORREF col, HFONT font = nullptr,
               UINT align = DT_CENTER | DT_VCENTER);

// 圆角裁剪 + 背景
void FillParentBg(HDC dc, HWND hwnd, const RECT &r, COLORREF col);

// ============ 深色模式 / 窗口外观 ============
void EnableAppDarkMode();
void DarkenWindow(HWND hwnd);
void ApplyDarkTitleBar(HWND hwnd);
void EnableRoundedCorners(HWND hwnd);
void SetWindowShadow(HWND hwnd);

// 常用图标码点（Segoe Fluent Icons）
namespace glyph {
constexpr wchar_t Play        = 0xE768;
constexpr wchar_t Stop        = 0xE71A;
constexpr wchar_t Refresh     = 0xE72C;
constexpr wchar_t Lightning   = 0xE945;
constexpr wchar_t Add         = 0xE710;
constexpr wchar_t Remove      = 0xE738;
constexpr wchar_t Settings    = 0xE713;
constexpr wchar_t Folder      = 0xE8B7;
constexpr wchar_t FolderOpen  = 0xE838;
constexpr wchar_t FileCode    = 0xE943;
constexpr wchar_t CheckMark   = 0xE73E;
constexpr wchar_t Cancel      = 0xE711;
constexpr wchar_t ChevronRight= 0xE76C;
constexpr wchar_t ChevronDown = 0xE70D;
constexpr wchar_t ChevronUp   = 0xE70E;
constexpr wchar_t Clear       = 0xE894;
constexpr wchar_t Save        = 0xE74E;
constexpr wchar_t Copy        = 0xE8C8;
constexpr wchar_t Delete      = 0xE74D;
constexpr wchar_t Edit        = 0xE70F;
constexpr wchar_t Clock       = 0xE823;
constexpr wchar_t Search      = 0xE721;
constexpr wchar_t Info        = 0xE946;
constexpr wchar_t Warn        = 0xE7BA;
constexpr wchar_t Error       = 0xE783;
constexpr wchar_t Leaf        = 0xE8D2;  // 近似树形
constexpr wchar_t Tree        = 0xE8D2;
constexpr wchar_t Terminal    = 0xE756;
constexpr wchar_t Test        = 0xE9D9;
constexpr wchar_t Beaker      = 0xE9D9;
constexpr wchar_t Chip        = 0xE964;
constexpr wchar_t Pin         = 0xE718;
constexpr wchar_t More        = 0xE712;
constexpr wchar_t Window      = 0xE8A7;
constexpr wchar_t Minus       = 0xE921;
constexpr wchar_t Square      = 0xE922;
constexpr wchar_t Close       = 0xE8BB;
constexpr wchar_t Restore     = 0xE923;
} // namespace glyph

} // namespace fc

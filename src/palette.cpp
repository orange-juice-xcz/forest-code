// Forest Code - 命令面板：Ctrl+O 快速跳文件 / Ctrl+Shift+F 全文搜索
#include "app.h"
#include "theme.h"
#include "util.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cctype>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

namespace fc {
namespace {

const int kEditId = 700;
const int kMaxHits = 300;
const int kMaxFiles = 5000;
const long long kMaxSearchFile = 512 * 1024;   // 超过这个大小的文件不参与全文搜索

struct Hit {
    std::wstring path;      // 绝对路径
    std::wstring name;      // 文件名
    std::wstring rel;       // 相对工作区路径（显示用）
    int line = 0;           // 搜索结果行号（1 起）；0 表示"整个文件"
    std::wstring text;      // 搜索结果：匹配到的整行
};

struct PaletteState {
    HWND hwnd = nullptr, parent = nullptr, edit = nullptr;
    PaletteMode mode = PaletteMode::QuickOpen;
    std::wstring workspace;
    std::vector<std::wstring> files;     // 工作区里的全部文件
    std::vector<Hit> hits;
    int sel = 0, hot = -1, scroll = 0, rowH = 30;
    RECT rcEdit{}, rcList{}, rcHint{};
    bool done = false, ok = false;
    std::wstring outPath;
    int outLine = 0;
};

PaletteState *g_pal = nullptr;

// ---------------------------------------------------------------- 工具
std::wstring LowerW(const std::wstring &s) {
    std::wstring o = s;
    for (auto &c : o) c = (wchar_t)towlower(c);
    return o;
}

bool IsTextFile(const std::wstring &p) {
    static const wchar_t *exts[] = {
        L".cpp", L".cc", L".cxx", L".c", L".h", L".hpp", L".hxx",
        L".md", L".markdown", L".txt", L".ini", L".json",
        L".py", L".java", L".js", L".ts", L".go", L".rs"
    };
    std::wstring e = FileExt(p);
    for (auto x : exts) if (e == x) return true;
    return false;
}

bool ContainsNoCase(const std::string &hay, const std::string &needleLower) {
    if (needleLower.empty() || hay.size() < needleLower.size()) return false;
    const size_t n = needleLower.size();
    for (size_t i = 0; i + n <= hay.size(); ++i) {
        size_t j = 0;
        while (j < n && (char)std::tolower((unsigned char)hay[i + j]) == needleLower[j]) ++j;
        if (j == n) return true;
    }
    return false;
}

// 子序列模糊匹配：query 的字符按顺序出现即命中。返回分数，越小越好；未命中返回 -1。
int Fuzzy(const std::wstring &text, const std::wstring &queryLower) {
    if (queryLower.empty()) return 0;
    size_t from = 0;
    int score = 0, last = -1;
    for (size_t qi = 0; qi < queryLower.size(); ++qi) {
        size_t found = std::wstring::npos;
        for (size_t k = from; k < text.size(); ++k) {
            if (text[k] == queryLower[qi]) { found = k; break; }
        }
        if (found == std::wstring::npos) return -1;
        score += (last < 0) ? (int)found : (int)(found - last - 1);
        last = (int)found;
        from = found + 1;
    }
    return score;
}

void CollectFiles(const std::wstring &dir, std::vector<std::wstring> &out, int depth) {
    if (depth > 12 || (int)out.size() >= kMaxFiles) return;
    for (auto &n : ListDir(dir)) {
        if (!n.empty() && n[0] == L'.') continue;      // .build / .git 之类一律跳过
        std::wstring p = JoinPath(dir, n);
        if (IsDir(p)) CollectFiles(p, out, depth + 1);
        else out.push_back(p);
        if ((int)out.size() >= kMaxFiles) return;
    }
}

std::wstring RelTo(const std::wstring &base, const std::wstring &path) {
    if (!base.empty() && path.size() > base.size() + 1 && path.rfind(base, 0) == 0 &&
        (path[base.size()] == L'\\' || path[base.size()] == L'/'))
        return path.substr(base.size() + 1);
    return FileName(path);
}

std::wstring Query(PaletteState *st) {
    if (!st->edit) return L"";
    int n = GetWindowTextLengthW(st->edit);
    std::wstring w((size_t)n, L'\0');
    if (n) GetWindowTextW(st->edit, &w[0], n + 1);
    return TrimW(w);
}

// ---------------------------------------------------------------- 过滤
void RefreshHits(PaletteState *st) {
    std::wstring q = Query(st);
    st->hits.clear();
    st->sel = 0;
    st->scroll = 0;
    st->hot = -1;

    if (st->mode == PaletteMode::QuickOpen) {
        struct Ranked { int score; Hit h; };
        std::vector<Ranked> ranked;
        std::wstring ql = LowerW(q);
        for (auto &f : st->files) {
            std::wstring name = FileName(f);
            std::wstring rel = RelTo(st->workspace, f);
            int score = 0;
            if (!ql.empty()) {
                int sb = Fuzzy(LowerW(name), ql);
                int sp = Fuzzy(LowerW(rel), ql);
                if (sb >= 0) score = sb;              // 文件名命中优先
                else if (sp >= 0) score = 1000 + sp;
                else continue;
            }
            Ranked r;
            r.score = score;
            r.h.path = f;
            r.h.name = name;
            r.h.rel = rel;
            ranked.push_back(r);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
            if (a.score != b.score) return a.score < b.score;
            return NaturalLess(a.h.rel, b.h.rel);
        });
        for (size_t i = 0; i < ranked.size() && i < (size_t)kMaxHits; ++i)
            st->hits.push_back(ranked[i].h);
        return;
    }

    // 全文搜索：朴素大小写无关子串匹配，够用且可预期
    if (q.empty()) return;
    std::string q8 = W2U(q);
    for (auto &c : q8) c = (char)std::tolower((unsigned char)c);

    for (auto &f : st->files) {
        if ((int)st->hits.size() >= kMaxHits) break;
        if (!IsTextFile(f) || FileSize(f) > kMaxSearchFile) continue;
        std::string text;
        if (!ReadFileUtf8(f, text)) continue;
        std::wstring rel = RelTo(st->workspace, f);
        int lineNo = 0;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t e = text.find('\n', pos);
            std::string line = text.substr(pos, (e == std::string::npos) ? std::string::npos : e - pos);
            ++lineNo;
            if (ContainsNoCase(line, q8)) {
                Hit h;
                h.path = f;
                h.name = FileName(f);
                h.rel = rel;
                h.line = lineNo;
                std::string t = Trim(line);
                if (t.size() > 200) t = t.substr(0, 200) + "...";
                h.text = U2W(t);
                st->hits.push_back(h);
                if ((int)st->hits.size() >= kMaxHits) break;
            }
            if (e == std::string::npos) break;
            pos = e + 1;
        }
    }
}

int VisibleRows(PaletteState *st) {
    int h = st->rcList.bottom - st->rcList.top - g_theme.S(4);
    return h > 0 ? h / g_theme.S(st->rowH) : 0;
}

void EnsureVisible(PaletteState *st) {
    int rows = VisibleRows(st);
    if (rows <= 0) return;
    if (st->sel < st->scroll) st->scroll = st->sel;
    if (st->sel >= st->scroll + rows) st->scroll = st->sel - rows + 1;
    int maxScroll = (int)st->hits.size() > rows ? (int)st->hits.size() - rows : 0;
    if (st->scroll > maxScroll) st->scroll = maxScroll;
    if (st->scroll < 0) st->scroll = 0;
}

RECT RowRect(PaletteState *st, int i) {
    int y = st->rcList.top + g_theme.S(2) + (i - st->scroll) * g_theme.S(st->rowH);
    return RECT{ st->rcList.left, y, st->rcList.right, y + g_theme.S(st->rowH) };
}

int RowAt(PaletteState *st, POINT p) {
    if (!PtInRect(&st->rcList, p)) return -1;
    int idx = st->scroll + (p.y - st->rcList.top - g_theme.S(2)) / g_theme.S(st->rowH);
    if (idx < 0 || idx >= (int)st->hits.size()) return -1;
    return idx;
}

// ---------------------------------------------------------------- 布局 / 绘制
void PalLayout(PaletteState *st) {
    RECT rc; GetClientRect(st->hwnd, &rc);
    int pad = g_theme.S(10);
    st->rcEdit = { pad, pad, rc.right - pad, pad + g_theme.S(34) };
    st->rcList = { pad, st->rcEdit.bottom + g_theme.S(6), rc.right - pad, rc.bottom - g_theme.S(26) };
    st->rcHint = { pad + g_theme.S(2), rc.bottom - g_theme.S(24), rc.right - pad, rc.bottom - g_theme.S(4) };
    if (st->edit)
        SetWindowPos(st->edit, nullptr, st->rcEdit.left, st->rcEdit.top,
                     st->rcEdit.right - st->rcEdit.left, st->rcEdit.bottom - st->rcEdit.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

void PalPaint(PaletteState *st) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(st->hwnd, &ps);
    RECT rc; GetClientRect(st->hwnd, &rc);
    const Palette &c = g_theme.c;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRectC(mem, rc, c.bgPanel);

    // 输入框外框
    StrokeRound(mem, st->rcEdit, g_theme.S(7), c.borderGlow, 1);

    // 结果行
    HRGN clip = CreateRectRgn(st->rcList.left, st->rcList.top, st->rcList.right, st->rcList.bottom);
    SelectClipRgn(mem, clip);
    for (size_t i = 0; i < st->hits.size(); ++i) {
        RECT r = RowRect(st, (int)i);
        if (r.bottom < st->rcList.top || r.top > st->rcList.bottom) continue;
        bool sel = ((int)i == st->sel);
        bool hov = ((int)i == st->hot);
        RECT rr{ r.left, r.top + g_theme.S(1), r.right, r.bottom - g_theme.S(1) };
        if (sel) {
            FillRound(mem, rr, g_theme.S(6), c.bgActive);
            StrokeRound(mem, rr, g_theme.S(6), c.accentDim, 1);
        } else if (hov) {
            FillRound(mem, rr, g_theme.S(6), c.bgHover);
        }

        const Hit &h = st->hits[i];
        RECT ir{ rr.left + g_theme.S(8), rr.top, rr.left + g_theme.S(26), rr.bottom };
        DrawIconC(mem, h.line > 0 ? glyph::Search : glyph::FileCode, ir,
                  sel ? c.accent : c.textFaint, g_theme.IconSmall());

        // 左侧：文件名（搜索模式再带 :行号）
        std::wstring left = h.name;
        if (h.line > 0) left += FormatW(L":%d", h.line);
        int leftW = TextWidth(mem, left, g_theme.Ui()) + g_theme.S(4);
        int maxLeft = (rr.right - rr.left) * 42 / 100;
        if (leftW > maxLeft) leftW = maxLeft;
        RECT lr{ ir.right + g_theme.S(6), rr.top, ir.right + g_theme.S(6) + leftW, rr.bottom };
        DrawTextC(mem, left, lr, sel ? c.text : c.textMuted, g_theme.Ui(),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // 右侧：相对路径 / 匹配行
        RECT tr{ lr.right + g_theme.S(10), rr.top, rr.right - g_theme.S(8), rr.bottom };
        std::wstring right = (h.line > 0) ? h.text : h.rel;
        DrawTextC(mem, right, tr, (h.line > 0) ? c.textFaint : c.textFaint, g_theme.UiSmall(),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectClipRgn(mem, nullptr);
    DeleteObject(clip);

    // 底部提示
    std::wstring hint;
    if (st->mode == PaletteMode::QuickOpen)
        hint = L"↑↓ 选择 · Enter 打开 · Esc 取消";
    else
        hint = L"↑↓ 选择 · Enter 跳到该行 · Esc 取消";
    hint += FormatW(L"   ·   %d 项", (int)st->hits.size());
    if ((int)st->hits.size() >= kMaxHits) hint += L"（只显示前 300 项）";
    DrawTextC(mem, hint, st->rcHint, c.textFaint, g_theme.UiSmall(),
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    DrawHLine(mem, 0, rc.right, st->rcHint.top - g_theme.S(3), c.borderSoft);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(st->hwnd, &ps);
}

// ---------------------------------------------------------------- 交互
void PalAccept(PaletteState *st) {
    if (st->sel < 0 || st->sel >= (int)st->hits.size()) { st->done = true; return; }
    const Hit &h = st->hits[st->sel];
    st->outPath = h.path;
    st->outLine = h.line;
    st->ok = true;
    st->done = true;
}

void PalKey(PaletteState *st, int vk) {
    int rows = VisibleRows(st);
    int page = rows > 1 ? rows - 1 : 1;
    int n = (int)st->hits.size();
    switch (vk) {
    case VK_UP:    if (n) st->sel = (st->sel + n - 1) % n; break;
    case VK_DOWN:  if (n) st->sel = (st->sel + 1) % n; break;
    case VK_PRIOR: st->sel = std::max(0, st->sel - page); break;
    case VK_NEXT:  st->sel = std::min(n > 0 ? n - 1 : 0, st->sel + page); break;
    case VK_HOME:  st->sel = 0; break;
    case VK_END:   st->sel = n > 0 ? n - 1 : 0; break;
    default: return;
    }
    EnsureVisible(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void PalWheel(PaletteState *st, int notches) {
    st->scroll -= notches;
    int rows = VisibleRows(st);
    int maxScroll = (int)st->hits.size() > rows ? (int)st->hits.size() - rows : 0;
    if (st->scroll > maxScroll) st->scroll = maxScroll;
    if (st->scroll < 0) st->scroll = 0;
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

LRESULT CALLBACK PalEditProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam,
                             UINT_PTR, DWORD_PTR ref) {
    PaletteState *st = (PaletteState *)ref;
    if (msg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_RETURN: PalAccept(st); return 0;
        case VK_ESCAPE: st->done = true; return 0;
        case VK_TAB:    return 0;               // 面板里没有别的控件，别把焦点让出去
        case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
            PalKey(st, (int)wParam);
            return 0;
        }
    } else if (msg == WM_MOUSEWHEEL) {
        PalWheel(st, GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;
    } else if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        return 0;                                // 别把回车/ESC 打进输入框
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

LRESULT CALLBACK PalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PaletteState *st = g_pal;
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PalPaint(st); return 0;
    case WM_SIZE: PalLayout(st); return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, g_theme.c.text);
        SetBkColor(dc, g_theme.c.bgSunken);
        static HBRUSH br = nullptr;
        if (!br) br = CreateSolidBrush(g_theme.c.bgSunken);
        return (LRESULT)br;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kEditId && HIWORD(wParam) == EN_CHANGE) {
            RefreshHits(st);
            EnsureVisible(st);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int h = RowAt(st, p);
        if (h != st->hot) {
            st->hot = h;
            if (h >= 0) { st->sel = h; }        // 悬停即选中，点一下就打开
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int h = RowAt(st, p);
        if (h >= 0) { st->sel = h; PalAccept(st); }
        return 0;
    }
    case WM_MOUSEWHEEL:
        PalWheel(st, GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;
    case WM_CLOSE:
        st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool ShowPalette(HWND parent, PaletteMode mode, const std::wstring &workspace,
                 std::wstring &outPath, int &outLine) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PalProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ForestCodePalette";
        RegisterClassExW(&wc);
        registered = true;
    }

    PaletteState st;
    st.parent = parent;
    st.mode = mode;
    st.workspace = workspace;
    st.rowH = 30;
    CollectFiles(workspace, st.files, 0);

    int rows = 10;
    RECT pr; GetWindowRect(parent, &pr);
    int pw = pr.right - pr.left;
    int W = pw * 62 / 100;
    if (W > g_theme.S(780)) W = g_theme.S(780);
    if (W < g_theme.S(460)) W = g_theme.S(460);
    int H = g_theme.S(10) * 2 + g_theme.S(34) + g_theme.S(6) + rows * g_theme.S(st.rowH) + g_theme.S(26);
    int x = pr.left + (pw - W) / 2;
    int y = pr.top + g_theme.S(96);

    // 先建窗口还是先建输入框？输入框的 EN_CHANGE 会回调 PalProc，所以 g_pal 必须就位
    g_pal = &st;
    st.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"ForestCodePalette", L"",
                              WS_POPUP, x, y, W, H, parent, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!st.hwnd) { g_pal = nullptr; return false; }
    EnableRoundedCorners(st.hwnd);

    st.edit = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
                              0, 0, 10, 10, st.hwnd, (HMENU)(INT_PTR)kEditId,
                              GetModuleHandleW(nullptr), nullptr);
    SendMessageW(st.edit, WM_SETFONT, (WPARAM)g_theme.Ui(), TRUE);
    SendMessageW(st.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(g_theme.S(6), g_theme.S(6)));
    SendMessageW(st.edit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)(mode == PaletteMode::QuickOpen ? L"输入文件名，支持模糊匹配…"
                                                         : L"输入要在工作区里搜索的内容…"));
    SetWindowSubclass(st.edit, PalEditProc, 4, (DWORD_PTR)&st);

    PalLayout(&st);
    RefreshHits(&st);
    EnsureVisible(&st);

    EnableWindow(parent, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
    SetFocus(st.edit);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    RemoveWindowSubclass(st.edit, PalEditProc, 4);
    DestroyWindow(st.hwnd);
    g_pal = nullptr;
    SetForegroundWindow(parent);

    if (st.ok) {
        outPath = st.outPath;
        outLine = st.outLine;
    }
    return st.ok;
}

} // namespace fc

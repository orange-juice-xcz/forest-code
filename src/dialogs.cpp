// Forest Code - 自绘对话框
#include "app.h"
#include "theme.h"
#include "util.h"
#include <windowsx.h>
#include <uxtheme.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>

namespace fc {

// ======================================================================
//  通用表单对话框
// ======================================================================
struct FormState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    std::wstring title;
    std::vector<FormField> *fields = nullptr;
    std::vector<HWND> edits;
    std::vector<RECT> labels;
    std::vector<RECT> boxes;
    std::vector<RECT> browseRects;
    RECT btnOk{}, btnCancel{};
    int hot = -1;
    int hotBrowse = -1;
    int press = -1;
    bool done = false;
    bool ok = false;
    int scroll = 0;
};

static FormState *g_form = nullptr;

static bool PickFileDlg(HWND owner, std::wstring &path) {
    wchar_t buf[MAX_PATH * 4]{};
    lstrcpynW(buf, path.c_str(), MAX_PATH * 4);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) { path = buf; return true; }
    return false;
}

static bool PickDirDlg(HWND owner, std::wstring &path) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"选择目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (!idl) return false;
    wchar_t buf[MAX_PATH]{};
    SHGetPathFromIDListW(idl, buf);
    CoTaskMemFree(idl);
    if (!buf[0]) return false;
    path = buf;
    return true;
}

static void FormLayout(FormState *st) {
    RECT rc; GetClientRect(st->hwnd, &rc);
    int pad = g_theme.S(18);
    int labelH = g_theme.S(20);
    int y = pad - st->scroll;
    int w = rc.right - pad * 2;

    st->labels.clear();
    st->boxes.clear();
    st->browseRects.clear();
    for (size_t i = 0; i < st->fields->size(); ++i) {
        FormField &f = (*st->fields)[i];
        RECT lr{ pad, y, rc.right - pad, y + labelH };
        st->labels.push_back(lr);
        y += labelH;
        int h = f.multiline ? g_theme.S(f.height ? f.height : 140) : g_theme.S(30);
        int editW = w;
        RECT brc{ 0, 0, 0, 0 };
        if (f.browse) {
            int bwid = g_theme.S(76);
            editW = w - bwid - g_theme.S(8);
            brc = { pad + editW + g_theme.S(8), y, pad + editW + g_theme.S(8) + bwid, y + h };
        }
        st->browseRects.push_back(brc);
        RECT er{ pad, y, pad + editW, y + h };
        st->boxes.push_back(er);
        if (i < st->edits.size()) {
            SetWindowPos(st->edits[i], nullptr, er.left, er.top, er.right - er.left, er.bottom - er.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        y += h + g_theme.S(14);
    }
    int bh = g_theme.S(32);
    int bw = g_theme.S(92);
    int by = rc.bottom - pad - bh;
    st->btnCancel = { rc.right - pad - bw, by, rc.right - pad, by + bh };
    st->btnOk = { st->btnCancel.left - g_theme.S(10) - bw, by, st->btnCancel.left - g_theme.S(10), by + bh };
}

static void FormPaint(FormState *st) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(st->hwnd, &ps);
    RECT rc; GetClientRect(st->hwnd, &rc);
    const Palette &c = g_theme.c;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRectC(mem, rc, c.bgPanel);

    for (size_t i = 0; i < st->labels.size(); ++i) {
        DrawTextC(mem, (*st->fields)[i].label, st->labels[i], c.textMuted, g_theme.Ui(),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    auto button = [&](RECT r, const std::wstring &text, int idx, bool primary) {
        bool hov = (st->hot == idx), pr = (st->press == idx);
        int radius = g_theme.S(7);
        if (primary) {
            FillRound2(mem, r, radius, hov ? RGB(0x35, 0x86, 0x55) : c.accentDim,
                       hov ? RGB(0x27, 0x68, 0x41) : c.accentDeep);
            StrokeRound(mem, r, radius, hov ? c.accent : c.borderGlow, 1);
            DrawTextC(mem, text, r, c.accentSoft, g_theme.Ui(),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            if (hov) { FillRound(mem, r, radius, c.bgHover); StrokeRound(mem, r, radius, c.borderGlow, 1); }
            else StrokeRound(mem, r, radius, c.border, 1);
            DrawTextC(mem, text, r, hov ? c.text : c.textMuted, g_theme.Ui(),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };
    button(st->btnOk, L"确定", 0, true);
    button(st->btnCancel, L"取消", 1, false);

    for (size_t i = 0; i < st->browseRects.size(); ++i) {
        if ((*st->fields)[i].browse == 0) continue;
        RECT r = st->browseRects[i];
        bool hov = ((int)i == st->hotBrowse);
        int radius = g_theme.S(7);
        if (hov) { FillRound(mem, r, radius, c.bgHover); StrokeRound(mem, r, radius, c.borderGlow, 1); }
        else StrokeRound(mem, r, radius, c.border, 1);
        DrawTextC(mem, L"浏览…", r, hov ? c.text : c.textMuted, g_theme.Ui(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DrawHLine(mem, 0, rc.right, st->btnOk.top - g_theme.S(14), c.borderSoft);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(st->hwnd, &ps);
}

static void FormCollect(FormState *st) {
    for (size_t i = 0; i < st->edits.size(); ++i) {
        HWND h = st->edits[i];
        int n = GetWindowTextLengthW(h);
        std::wstring w((size_t)n, L'\0');
        if (n) GetWindowTextW(h, &w[0], n + 1);
        (*st->fields)[i].value = U2W(FromEdit(w));
    }
}

static LRESULT CALLBACK FormProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FormState *st = g_form;
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: FormPaint(st); return 0;
    case WM_SIZE: FormLayout(st); return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, g_theme.c.text);
        SetBkColor(dc, g_theme.c.bgSunken);
        static HBRUSH br = nullptr;
        if (!br) br = CreateSolidBrush(g_theme.c.bgSunken);
        return (LRESULT)br;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int h = -1;
        if (PtInRect(&st->btnOk, p)) h = 0;
        else if (PtInRect(&st->btnCancel, p)) h = 1;
        int hb = -1;
        for (size_t i = 0; i < st->browseRects.size(); ++i)
            if ((*st->fields)[i].browse && PtInRect(&st->browseRects[i], p)) hb = (int)i;
        if (h != st->hot || hb != st->hotBrowse) {
            st->hot = h; st->hotBrowse = hb;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&st->btnOk, p)) st->press = 0;
        else if (PtInRect(&st->btnCancel, p)) st->press = 1;
        if (st->press >= 0) SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (st->press == 0 && PtInRect(&st->btnOk, p)) { FormCollect(st); st->ok = true; st->done = true; }
        if (st->press == 1 && PtInRect(&st->btnCancel, p)) { st->done = true; }
        for (size_t i = 0; i < st->browseRects.size(); ++i) {
            FormField &f = (*st->fields)[i];
            if (!f.browse || !PtInRect(&st->browseRects[i], p)) continue;
            std::wstring cur = f.value;
            bool ok = (f.browse == 2) ? PickDirDlg(hwnd, cur) : PickFileDlg(hwnd, cur);
            if (ok && i < st->edits.size()) {
                SetWindowTextW(st->edits[i], ToEdit(W2U(cur)).c_str());
                f.value = cur;
            }
            break;
        }
        st->press = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { st->done = true; return 0; }
        if (wParam == VK_RETURN) {
            HWND f = GetFocus();
            bool multiline = false;
            for (HWND h : st->edits) if (h == f) multiline = (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE) != 0;
            if (!multiline) { FormCollect(st); st->ok = true; st->done = true; }
            return 0;
        }
        break;
    case WM_CLOSE:
        st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowFormDialog(HWND parent, const std::wstring &title, std::vector<FormField> &fields) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FormProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ForestCodeForm";
        RegisterClassExW(&wc);
        registered = true;
    }

    FormState st;
    st.parent = parent;
    st.title = title;
    st.fields = &fields;

    int pad = g_theme.S(18);
    int labelH = g_theme.S(20);
    int bodyH = 0;
    for (auto &f : fields) {
        bodyH += labelH;
        bodyH += g_theme.S(f.multiline ? (f.height ? f.height : 140) : 30);
        bodyH += g_theme.S(14);
    }
    int footer = g_theme.S(32) + g_theme.S(28);
    int W = g_theme.S(520);
    int H = bodyH + footer + pad * 2;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - W) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - H) / 2;

    g_form = &st;   // 必须在 CreateWindowExW 之前，否则创建期间的 WM_SIZE 会拿到空指针
    st.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ForestCodeForm", title.c_str(),
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              x, y, W, H, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st.hwnd) { g_form = nullptr; return false; }
    ApplyDarkTitleBar(st.hwnd);

    for (auto &f : fields) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT;
        if (f.multiline) style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
        else style |= ES_AUTOHSCROLL;
        HWND h = CreateWindowExW(0, L"EDIT", ToEdit(W2U(f.value)).c_str(), style,
                                 0, 0, 10, 10, st.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)(f.multiline ? g_theme.Mono() : g_theme.Ui()), TRUE);
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(g_theme.S(6), g_theme.S(6)));
        if (f.multiline) {          // 只有多行框需要深色滚动条；单行框加主题会吞掉文字
            DarkenWindow(h);
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        }
        st.edits.push_back(h);
    }

    FormLayout(&st);
    EnableWindow(parent, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
    if (!st.edits.empty()) SetFocus(st.edits[0]);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(st.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    DestroyWindow(st.hwnd);
    g_form = nullptr;
    SetForegroundWindow(parent);
    return st.ok;
}

// ======================================================================
//  首次启动：选择工作区（Obsidian 式，只问一次）
// ======================================================================
struct FirstRunState {
    HWND hwnd = nullptr, parent = nullptr, edit = nullptr;
    RECT btnBrowse{}, btnOk{}, btnCancel{}, logo{}, rcPath{};
    int hot = -1, press = -1;
    bool done = false, ok = false;
    std::wstring value;
};

static FirstRunState *g_first = nullptr;

static void FirstRunLayout(FirstRunState *st) {
    RECT rc; GetClientRect(st->hwnd, &rc);
    int pad = g_theme.S(26);
    int W = rc.right - pad * 2;

    st->logo = { pad, pad, pad + g_theme.S(44), pad + g_theme.S(44) };
    int y = pad + g_theme.S(74);                    // 标题区之后

    int bh = g_theme.S(32);
    int browseW = g_theme.S(80);
    st->rcPath = { pad, y, pad + W - browseW - g_theme.S(8), y + bh };
    st->btnBrowse = { st->rcPath.right + g_theme.S(8), y, pad + W, y + bh };
    if (st->edit) {
        SetWindowPos(st->edit, nullptr, st->rcPath.left, st->rcPath.top,
                     st->rcPath.right - st->rcPath.left, st->rcPath.bottom - st->rcPath.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int by = rc.bottom - pad - bh;
    int bw = g_theme.S(110);
    st->btnCancel = { rc.right - pad - bw, by, rc.right - pad, by + bh };
    st->btnOk = { st->btnCancel.left - g_theme.S(10) - bw, by, st->btnCancel.left - g_theme.S(10), by + bh };
}

static void FirstRunPaint(FirstRunState *st) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(st->hwnd, &ps);
    RECT rc; GetClientRect(st->hwnd, &rc);
    const Palette &c = g_theme.c;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRectC(mem, rc, c.bgPanel);

    // 品牌
    FillRound(mem, st->logo, g_theme.S(11), c.accentDeep);
    StrokeRound(mem, st->logo, g_theme.S(11), c.accentDim, 1);
    {
        RECT mk = st->logo;
        int inset = g_theme.S(9);
        mk.left += inset; mk.top += inset; mk.right -= inset; mk.bottom -= inset;
        DrawLogoMark(mem, mk, c.accent);
    }
    int pad = g_theme.S(26);
    RECT tr{ st->logo.right + g_theme.S(14), st->logo.top, rc.right - pad, st->logo.top + g_theme.S(26) };
    DrawTextC(mem, L"欢迎使用 Forest Code", tr, c.text, g_theme.UiBold());
    RECT sr{ tr.left, tr.bottom - g_theme.S(2), rc.right - pad, tr.bottom + g_theme.S(22) };
    DrawTextC(mem, L"先选一个工作区，你的题目、代码、测试用例都会放在这里。", sr, c.textMuted, g_theme.UiSmall());

    // 路径标签
    RECT lb{ pad, st->rcPath.top - g_theme.S(22), rc.right - pad, st->rcPath.top };
    DrawTextC(mem, L"工作区位置", lb, c.textMuted, g_theme.Ui());

    // 提示
    RECT hint{ pad, st->rcPath.bottom + g_theme.S(12), rc.right - pad, st->rcPath.bottom + g_theme.S(46) };
    DrawTextC(mem, L"文件夹不存在会自动创建。以后想换，右键侧栏标题即可切换。", hint, c.textFaint, g_theme.UiSmall());

    auto button = [&](RECT r, const std::wstring &text, int idx, bool primary) {
        bool hov = (st->hot == idx);
        int radius = g_theme.S(7);
        if (primary) {
            FillRound2(mem, r, radius, hov ? RGB(0x35, 0x86, 0x55) : c.accentDim,
                       hov ? RGB(0x27, 0x68, 0x41) : c.accentDeep);
            StrokeRound(mem, r, radius, hov ? c.accent : c.borderGlow, 1);
            DrawTextC(mem, text, r, c.accentSoft, g_theme.Ui(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            if (hov) { FillRound(mem, r, radius, c.bgHover); StrokeRound(mem, r, radius, c.borderGlow, 1); }
            else StrokeRound(mem, r, radius, c.border, 1);
            DrawTextC(mem, text, r, hov ? c.text : c.textMuted, g_theme.Ui(),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };
    button(st->btnBrowse, L"浏览…", 2, false);
    button(st->btnOk, L"开始使用", 0, true);
    button(st->btnCancel, L"退出", 1, false);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(st->hwnd, &ps);
}

static std::wstring FirstRunText(FirstRunState *st) {
    int n = GetWindowTextLengthW(st->edit);
    std::wstring w((size_t)n, L'\0');
    if (n) GetWindowTextW(st->edit, &w[0], n + 1);
    return TrimW(w);
}

static LRESULT CALLBACK FirstRunProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FirstRunState *st = g_first;
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: FirstRunPaint(st); return 0;
    case WM_SIZE: FirstRunLayout(st); return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, g_theme.c.text);
        SetBkColor(dc, g_theme.c.bgSunken);
        static HBRUSH br = nullptr;
        if (!br) br = CreateSolidBrush(g_theme.c.bgSunken);
        return (LRESULT)br;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int h = -1;
        if (PtInRect(&st->btnOk, p)) h = 0;
        else if (PtInRect(&st->btnCancel, p)) h = 1;
        else if (PtInRect(&st->btnBrowse, p)) h = 2;
        if (h != st->hot) { st->hot = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&st->btnOk, p)) st->press = 0;
        else if (PtInRect(&st->btnCancel, p)) st->press = 1;
        else if (PtInRect(&st->btnBrowse, p)) st->press = 2;
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int hit = -1;
        if (PtInRect(&st->btnOk, p)) hit = 0;
        else if (PtInRect(&st->btnCancel, p)) hit = 1;
        else if (PtInRect(&st->btnBrowse, p)) hit = 2;
        if (hit == st->press && hit == 2) {
            std::wstring cur = FirstRunText(st);
            if (PickDirDlg(hwnd, cur)) SetWindowTextW(st->edit, cur.c_str());
        } else if (hit == st->press && hit == 0) {
            std::wstring v = FirstRunText(st);
            if (v.empty()) { SetFocus(st->edit); return 0; }
            st->value = v;
            st->ok = true;
            st->done = true;
        } else if (hit == st->press && hit == 1) {
            st->done = true;
        }
        st->press = -1;
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { st->done = true; return 0; }
        if (wParam == VK_RETURN) {
            std::wstring v = FirstRunText(st);
            if (!v.empty()) { st->value = v; st->ok = true; st->done = true; }
            return 0;
        }
        break;
    case WM_CLOSE:
        st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowFirstRunDialog(HWND parent, std::wstring &outWorkspace) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FirstRunProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ForestCodeFirstRun";
        RegisterClassExW(&wc);
        registered = true;
    }

    FirstRunState st;
    st.parent = parent;

    // 默认位置：文档目录下的 ForestCode（比用户目录根更好找）
    std::wstring docs = GetEnv(L"USERPROFILE");
    std::wstring candidate = JoinPath(docs, L"Documents");
    if (!IsDir(candidate)) candidate = docs;
    st.value = JoinPath(candidate, L"ForestCode");

    int W = g_theme.S(600);
    int H = g_theme.S(330);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - W) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - H) / 2;

    g_first = &st;
    st.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ForestCodeFirstRun", L"欢迎使用 Forest Code",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H,
                              parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st.hwnd) { g_first = nullptr; return false; }
    ApplyDarkTitleBar(st.hwnd);

    st.edit = CreateWindowExW(0, L"EDIT", st.value.c_str(),
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                              0, 0, 10, 10, st.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(st.edit, WM_SETFONT, (WPARAM)g_theme.Ui(), TRUE);
    SendMessageW(st.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(g_theme.S(6), g_theme.S(6)));
    SendMessageW(st.edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    FirstRunLayout(&st);

    EnableWindow(parent, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
    SetFocus(st.edit);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(st.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    DestroyWindow(st.hwnd);
    g_first = nullptr;
    SetForegroundWindow(parent);

    if (!st.ok || st.value.empty()) return false;

    outWorkspace = st.value;
    MakeDirs(outWorkspace);
    if (!IsDir(outWorkspace)) {
        MessageBoxW(parent, L"无法创建该文件夹，请换一个位置。", L"Forest Code", MB_OK | MB_ICONERROR);
        return false;
    }
    // 落盘
    Ini ini;
    ini.Load(Settings::SettingsPath());
    ini.Set("workspace", W2U(outWorkspace));
    if (ini.Get("compiler").empty()) ini.Set("compiler", W2U(Workspace::DetectCompiler()));
    ini.Save(Settings::SettingsPath());
    return true;
}
// ======================================================================
//  选择对话框（单选列表）
// ======================================================================
struct ChoiceState {
    HWND hwnd = nullptr, parent = nullptr;
    std::wstring title, message;
    const std::vector<std::wstring> *choices = nullptr;
    int selected = 0, hot = -1, hotBtn = -1;
    RECT btnOk{}, btnCancel{};
    bool done = false, ok = false;
    std::vector<RECT> rows;
};

static ChoiceState *g_choice = nullptr;

static LRESULT CALLBACK ChoiceProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ChoiceState *st = g_choice;
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        const Palette &c = g_theme.c;
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        FillRectC(mem, rc, c.bgPanel);
        int pad = g_theme.S(18);
        RECT mr{ pad, pad, rc.right - pad, pad + g_theme.S(24) };
        DrawTextC(mem, st->message, mr, c.textMuted, g_theme.Ui(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (size_t i = 0; i < st->rows.size(); ++i) {
            RECT r = st->rows[i];
            bool sel = ((int)i == st->selected);
            bool hov = ((int)i == st->hot);
            if (sel) { FillRound(mem, r, g_theme.S(6), c.bgActive); StrokeRound(mem, r, g_theme.S(6), c.accentDim, 1); }
            else if (hov) FillRound(mem, r, g_theme.S(6), c.bgHover);
            RECT ir{ r.left + g_theme.S(10), r.top, r.left + g_theme.S(26), r.bottom };
            DrawIconC(mem, sel ? glyph::CheckMark : glyph::ChevronRight, ir,
                      sel ? c.accent : c.textFaint, g_theme.IconSmall());
            RECT tr{ ir.right + g_theme.S(4), r.top, r.right - g_theme.S(10), r.bottom };
            DrawTextC(mem, (*st->choices)[i], tr, sel ? c.text : c.textMuted, g_theme.Ui(),
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        auto button = [&](RECT r, const wchar_t *text, int idx, bool primary) {
            bool hov = (st->hotBtn == idx);
            int radius = g_theme.S(7);
            if (primary) {
                FillRound2(mem, r, radius, hov ? RGB(0x35, 0x86, 0x55) : c.accentDim,
                           hov ? RGB(0x27, 0x68, 0x41) : c.accentDeep);
                StrokeRound(mem, r, radius, hov ? c.accent : c.borderGlow, 1);
                DrawTextC(mem, text, r, c.accentSoft, g_theme.Ui(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                if (hov) { FillRound(mem, r, radius, c.bgHover); StrokeRound(mem, r, radius, c.borderGlow, 1); }
                else StrokeRound(mem, r, radius, c.border, 1);
                DrawTextC(mem, text, r, hov ? c.text : c.textMuted, g_theme.Ui(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        };
        button(st->btnOk, L"确定", 0, true);
        button(st->btnCancel, L"取消", 1, false);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int h = -1, hb = -1;
        for (size_t i = 0; i < st->rows.size(); ++i) if (PtInRect(&st->rows[i], p)) h = (int)i;
        if (PtInRect(&st->btnOk, p)) hb = 0;
        else if (PtInRect(&st->btnCancel, p)) hb = 1;
        if (h != st->hot || hb != st->hotBtn) { st->hot = h; st->hotBtn = hb; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (size_t i = 0; i < st->rows.size(); ++i)
            if (PtInRect(&st->rows[i], p)) { st->selected = (int)i; InvalidateRect(hwnd, nullptr, FALSE); }
        if (PtInRect(&st->btnOk, p)) { st->ok = true; st->done = true; }
        if (PtInRect(&st->btnCancel, p)) st->done = true;
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) st->done = true;
        else if (wParam == VK_RETURN) { st->ok = true; st->done = true; }
        else if (wParam == VK_DOWN) { st->selected = std::min<int>(st->selected + 1, (int)st->choices->size() - 1); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wParam == VK_UP) { st->selected = std::max(0, st->selected - 1); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_CLOSE: st->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowChoiceDialog(HWND parent, const std::wstring &title, const std::wstring &message,
                      const std::vector<std::wstring> &choices, int &selected) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ChoiceProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ForestCodeChoice";
        RegisterClassExW(&wc);
        registered = true;
    }
    ChoiceState st;
    st.parent = parent;
    st.title = title;
    st.message = message;
    st.choices = &choices;
    st.selected = selected;

    int pad = g_theme.S(18);
    int W = g_theme.S(420);
    int rowH = g_theme.S(36);
    int H = pad * 2 + g_theme.S(24) + g_theme.S(10) + rowH * (int)choices.size() + g_theme.S(28) + g_theme.S(32);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - W) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - H) / 2;

    g_choice = &st;
    st.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ForestCodeChoice", title.c_str(),
                              WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H,
                              parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st.hwnd) { g_choice = nullptr; return false; }
    ApplyDarkTitleBar(st.hwnd);

    int yy = pad + g_theme.S(24) + g_theme.S(10);
    for (size_t i = 0; i < choices.size(); ++i) {
        st.rows.push_back(RECT{ pad, yy, W - pad, yy + rowH - g_theme.S(4) });
        yy += rowH;
    }
    int bh = g_theme.S(32), bw = g_theme.S(92);
    int by = H - pad - bh;
    st.btnCancel = { W - pad - bw, by, W - pad, by + bh };
    st.btnOk = { st.btnCancel.left - g_theme.S(10) - bw, by, st.btnCancel.left - g_theme.S(10), by + bh };

    EnableWindow(parent, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
    SetFocus(st.hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(st.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    DestroyWindow(st.hwnd);
    g_choice = nullptr;
    SetForegroundWindow(parent);
    if (st.ok) selected = st.selected;
    return st.ok;
}

// ======================================================================
//  设置对话框
// ======================================================================
void App::ShowSettingsDialog() {
    int stdChoice = 0;
    std::vector<std::wstring> stds = { L"c++11", L"c++14", L"c++17", L"c++20" };
    for (size_t i = 0; i < stds.size(); ++i) if (stds[i] == ws.settings.stdFlag) stdChoice = (int)i;

    std::vector<FormField> fields;
    auto add = [&](const wchar_t *label, const std::wstring &val, bool multi = false, int h = 0, int browse = 0) {
        FormField f; f.label = label; f.value = val; f.multiline = multi; f.height = h; f.browse = browse;
        fields.push_back(f);
    };
    add(L"工作区目录", ws.settings.workspace, false, 0, 2);
    add(L"编译器 (g++ 完整路径)", ws.settings.compiler, false, 0, 1);
    add(L"C++ 标准 (c++11 / c++14 / c++17 / c++20)", ws.settings.stdFlag);
    add(L"编译选项", ws.settings.compileFlags);
    add(L"时间限制 (毫秒)", std::to_wstring(ws.settings.timeLimitMs));
    add(L"代码字体", ws.settings.codeFont);
    add(L"代码字号 (磅)", std::to_wstring(ws.settings.codeSize));
    add(L"界面字体", ws.settings.uiFont);
    add(L"界面字号 (磅)", std::to_wstring(ws.settings.uiSize));
    add(L"自动保存 (1 / 0)", ws.settings.autoSave ? L"1" : L"0");

    if (!ShowFormDialog(hwnd_, L"Forest Code 设置", fields)) return;

    ws.settings.workspace = fields[0].value;
    ws.settings.compiler = fields[1].value;
    ws.settings.stdFlag = fields[2].value.empty() ? L"c++11" : fields[2].value;
    ws.settings.compileFlags = fields[3].value;
    ws.settings.timeLimitMs = _wtoi(fields[4].value.c_str());
    if (ws.settings.timeLimitMs <= 0) ws.settings.timeLimitMs = 2000;
    ws.settings.codeFont = fields[5].value.empty() ? L"Cascadia Code" : fields[5].value;
    int cs = _wtoi(fields[6].value.c_str());
    ws.settings.codeSize = cs > 0 ? cs : 11;
    ws.settings.uiFont = fields[7].value.empty() ? L"Microsoft YaHei UI" : fields[7].value;
    int us = _wtoi(fields[8].value.c_str());
    ws.settings.uiSize = us > 0 ? us : 9;
    ws.settings.autoSave = (fields[9].value == L"1");
    ws.settings.Save();

    g_theme.f.code = ws.settings.codeFont;
    g_theme.f.codeSize = ws.settings.codeSize;
    g_theme.f.ui = ws.settings.uiFont;
    g_theme.f.uiSize = ws.settings.uiSize;
    g_theme.SetDpi(g_theme.Dpi());
    for (auto &d : docs) {
        d->ed->SetCodeFont(ws.settings.codeFont, ws.settings.codeSize);
        d->ed->SetLang(d->statement ? (FileExt(d->path) == L".md" ? Lang::Markdown : Lang::Plain) : Lang::Cpp);
    }
    for (HWND h : { hIn, hExp, hAct, hOut, hDiag })
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_theme.Mono(), TRUE);

    runner.Configure(ws.settings.compiler, ws.settings.stdFlag, ws.settings.compileFlags,
                     ws.settings.timeLimitMs);
    ws.LoadAll();
    RebuildFileTree();
    Layout();
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetStatus(L"设置已保存");
}

// ======================================================================
//  新建题目
// ======================================================================
void App::ShowNewProblemDialog() {
    std::vector<FormField> fields;
    auto add = [&](const wchar_t *label, const std::wstring &val, bool multi = false, int h = 0) {
        FormField f; f.label = label; f.value = val; f.multiline = multi; f.height = h;
        fields.push_back(f);
    };
    add(L"题目标题", L"");
    add(L"来源（洛谷 / Codeforces / AtCoder / 校内 OJ …）", L"");
    add(L"难度（入门 / 普及- / 普及 / 提高 / 省选 …）", L"");
    add(L"标签（用空格分隔，例如 二分 贪心 dp）", L"");
    add(L"初始解法代码", U2W(ws.DefaultTemplate()), true, 150);

    if (!ShowFormDialog(hwnd_, L"新建题目", fields)) return;
    std::wstring title = fields[0].value;
    if (title.empty()) { SetStatus(L"题目名称不能为空"); return; }

    Problem *p = ws.CreateProblem(title, fields[1].value, fields[2].value, fields[3].value,
                                  fields[4].value, true);
    RebuildFileTree();
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (p) {
        p->expanded = true;
        RebuildFileTree();
        if (!p->solutions.empty()) OpenFile(p->solutions[0].path);
        SetStatus(L"已创建题目：" + title);
    }
}

} // namespace fc

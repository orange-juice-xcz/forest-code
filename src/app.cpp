// Forest Code - 主窗口：创建 / 布局 / 绘制 / 输入
#include "app.h"
#include "theme.h"
#include "util.h"
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <algorithm>
#include "Scintilla.h"

namespace fc {

App &App::Get() { static App a; return a; }

// ====================== 窗口过程 ======================
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App &app = App::Get();
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&app);
    }
    switch (msg) {
    case WM_FC_JOB_DONE:
        app.OnJobDone((RunResult *)lParam);
        return 0;

    case WM_APP + 2:                 // 目录有变化（外部增删改）
        app.OnDirChanged();
        return 0;

    case WM_APP + 3:                 // 就地重命名提交
        app.CommitInlineRename(wParam != 0);
        return 0;

    case WM_TIMER:
        if (wParam == 7) {
            KillTimer(hwnd, 7);
            if (app.treeDirty_) {
                app.treeDirty_ = false;
                app.RebuildFileTree();
                app.ScanTestsForActive();
                app.LoadTestToEditors(app.selTest);
                app.RefreshRunPanel();
            }
        }
        return 0;

    case WM_NCCALCSIZE:
        if (wParam) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lParam;
            if (IsZoomed(hwnd)) {
                int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy;  p->rgrc[0].bottom -= fy;
            }
            return 0;
        } else {
            // 客户区 = 整个窗口
            RECT *rc = (RECT *)lParam;
            RECT wr; GetWindowRect(hwnd, &wr);
            rc->left = 0; rc->top = 0;
            rc->right = wr.right - wr.left;
            rc->bottom = wr.bottom - wr.top;
            return 0;
        }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = g_theme.S(880);
        mmi->ptMinTrackSize.y = g_theme.S(560);
        return 0;
    }

    case WM_NCHITTEST: {
        if (IsZoomed(hwnd)) break;
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc; GetWindowRect(hwnd, &rc);
        int b = g_theme.S(6);
        bool L = p.x < rc.left + b, R = p.x >= rc.right - b;
        bool T = p.y < rc.top + b,  B = p.y >= rc.bottom - b;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        POINT cp = p; ScreenToClient(hwnd, &cp);
        if (cp.y < g_theme.TitleH()) {
            // 窗口按钮区域不当作标题栏
            bool onButton = false;
            for (auto &btn : app.buttons) {
                if (!btn.visible) continue;
                if (btn.id >= 901 && btn.id <= 903) {
                    RECT r = btn.rc;
                    if (cp.x >= r.left && cp.x < r.right && cp.y >= r.top && cp.y < r.bottom) { onButton = true; break; }
                }
            }
            if (!onButton) return HTCAPTION;
        }
        break;
    }

    case WM_SIZE:
        app.OnSize();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        app.OnPaint();
        return 0;

    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        app.OnMouseMove(p);
        return 0;
    }
    case WM_MOUSELEAVE:
        app.OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app.OnLButtonDown(p);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app.OnLButtonUp(p);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app.OnRButtonDown(p);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (p.y < g_theme.TitleH()) {
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        int row = app.FsRowAt(p);          // 双击文件名 -> 就地重命名
        if (row >= 0) { app.selFsRow = row; app.BeginInlineRename(row); }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &p);
        int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        // 热区必须用真实的客户区矩形：sideWidth 是逻辑像素，高 DPI 下会只有半条侧栏能滚
        if (app.sideVisible && p.x >= app.rcSide_.left && p.x < app.rcSide_.right &&
            p.y >= app.rcSide_.top && p.y < app.rcSide_.bottom) {
            int content = g_theme.S(4) + (int)app.fsRows.size() * g_theme.S(28);
            int visible = app.rcSide_.bottom - app.rcSideHead_.bottom;
            int maxScroll = content > visible ? content - visible : 0;
            app.sideScroll -= delta * g_theme.S(48);
            if (app.sideScroll < 0) app.sideScroll = 0;
            if (app.sideScroll > maxScroll) app.sideScroll = maxScroll;
            InvalidateRect(hwnd, &app.rcSide_, FALSE);
        } else if (app.bottomVisible && app.bottomPage == BottomPage::Tests &&
                   PtInRect(&app.rcTestList_, p)) {
            int content = g_theme.S(6) + ((int)app.curTests.size() + 1) * g_theme.S(34);
            int visible = app.rcTestList_.bottom - app.rcTestList_.top;
            int maxScroll = content > visible ? content - visible : 0;
            app.testScroll -= delta * g_theme.S(40);
            if (app.testScroll < 0) app.testScroll = 0;
            if (app.testScroll > maxScroll) app.testScroll = maxScroll;
            InvalidateRect(hwnd, &app.rcTestList_, FALSE);
        }
        return 0;
    }

    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p);
        POINT cp = p; ScreenToClient(hwnd, &cp);
        int b = g_theme.S(5);
        RECT rc; GetClientRect(hwnd, &rc);
        bool onBorder = cp.x < b || cp.x > rc.right - b || cp.y < b || cp.y > rc.bottom - b;
        if (app.dragSplitSide_) { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; }
        if (onBorder && !IsZoomed(hwnd)) {
            bool L = cp.x < b, R = cp.x > rc.right - b, T = cp.y < b, B = cp.y > rc.bottom - b;
            LPCWSTR cur = IDC_ARROW;
            if ((T && L) || (B && R)) cur = IDC_SIZENWSE;
            else if ((T && R) || (B && L)) cur = IDC_SIZENESW;
            else if (L || R) cur = IDC_SIZEWE;
            else cur = IDC_SIZENS;
            SetCursor(LoadCursor(nullptr, cur));
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, g_theme.c.text);
        SetBkColor(dc, g_theme.c.bgSunken);
        static HBRUSH brEdit = nullptr;
        if (!brEdit) brEdit = CreateSolidBrush(g_theme.c.bgSunken);
        return (LRESULT)brEdit;
    }

    case WM_KEYDOWN:
        app.OnKeyDown(wParam);
        return 0;

    case WM_DPICHANGED: {
        RECT *r = (RECT *)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_theme.SetDpi(HIWORD(wParam));
        app.Layout();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_CLOSE:
        // 退出前问一次：有未保存的修改就让用户决定，不要默默全存
        {
            int dirty = 0;
            for (auto &d : app.docs) if (d->ed && d->ed->Modified()) ++dirty;
            if (dirty > 0) {
                std::wstring msg = FormatW(L"有 %d 个文件还没保存，要保存后退出吗？", dirty);
                int r = MessageBoxW(hwnd, msg.c_str(), L"Forest Code",
                                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) return 0;
                if (r == IDYES) app.CmdSaveAll();
            }
            if (app.jobRunning) app.runner.Kill();
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NOTIFY: {
        // Scintilla 把通知发给父窗口，转发给对应的编辑器
        NMHDR *nh = (NMHDR *)lParam;
        if (nh && nh->code >= 2000) {   // SCN_* 通知码从 2000 起
            for (auto &d : app.docs) {
                if (d->ed && d->ed->Hwnd() == nh->hwndFrom) {
                    d->ed->HandleNotify((SCNotification *)lParam);
                    break;
                }
            }
            return 0;
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) >= 3100) app.OnFsMenuCommand(LOWORD(wParam));
        return 0;

    case WM_DROPFILES: {
        HDROP hd = (HDROP)wParam;
        POINT pt{ 0, 0 };
        DragQueryPoint(hd, &pt);
        app.OnDropFiles(pt, hd);
        DragFinish(hd);
        app.Layout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ====================== 初始化 ======================
static LRESULT CALLBACK EditShortcutProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && IsAppShortcut(wParam)) {
        PostMessageW(GetParent(h), WM_KEYDOWN, wParam, lParam);
        return 0;
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

bool App::Init(HINSTANCE inst) {
    inst_ = inst;

    EnableAppDarkMode();
    GfxInit();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ForestCodeMain";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);
    if (!RegisterClassExW(&wc)) return false;

    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, L"ForestCodeMain", L"Forest Code",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
                            nullptr, nullptr, inst, nullptr);
    if (!hwnd_) return false;

    g_theme.Init(hwnd_);
    ApplyDarkTitleBar(hwnd_);
    EnableRoundedCorners(hwnd_);

    // 按 DPI 计算窗口尺寸并居中
    {
        int w = g_theme.S(1180), h = g_theme.S(760);
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int maxW = (wa.right - wa.left) * 94 / 100;
        int maxH = (wa.bottom - wa.top) * 94 / 100;
        if (w > maxW) w = maxW;
        if (h > maxH) h = maxH;
        int x = wa.left + ((wa.right - wa.left) - w) / 2;
        int y = wa.top + ((wa.bottom - wa.top) - h) / 2;
        SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // ---- 首次启动：先让用户安好“家” ----
    {
        Ini probe;
        std::wstring saved;
        if (probe.Load(Settings::SettingsPath())) saved = U2W(probe.Get("workspace"));
        if (saved.empty() || !IsDir(saved)) {
            std::wstring chosen;
            if (!ShowFirstRunDialog(hwnd_, chosen)) return false;   // 用户取消 -> 退出
        }
    }

    // 工作区
    ws.LoadAll();
    ws.settings.codeFont = ws.settings.codeFont.empty() ? L"Cascadia Code" : ws.settings.codeFont;
    g_theme.f.code = ws.settings.codeFont;
    g_theme.f.codeSize = ws.settings.codeSize;
    g_theme.f.ui = ws.settings.uiFont;
    g_theme.f.uiSize = ws.settings.uiSize;
    g_theme.SetDpi(g_theme.Dpi());

    runner.Configure(ws.settings.compiler, ws.settings.stdFlag, ws.settings.compileFlags,
                     ws.settings.timeLimitMs);

    // 底部输入控件
    auto makeEdit = [&](int id, bool readonly) {
        DWORD style = WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL;
        if (readonly) style |= ES_READONLY;
        HWND h = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, hwnd_,
                                 (HMENU)(INT_PTR)id, inst_, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)g_theme.Mono(), TRUE);
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(g_theme.S(6), g_theme.S(6)));
        DarkenWindow(h);
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        SetWindowSubclass(h, EditShortcutProc, 2, 0);
        return h;
    };
    hIn  = makeEdit(401, false);
    hExp = makeEdit(402, false);
    hAct = makeEdit(403, true);
    hOut = makeEdit(404, true);
    hDiag = makeEdit(405, true);

    BuildSnippets();
    RebuildFileTree();
    StartDirWatch();

    // 打开上次文件或草稿
    bool opened = false;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] == L'-') continue;
            if (PathExists(argv[i]) && !IsDir(argv[i])) { opened = OpenFile(argv[i]) != nullptr; break; }
        }
        LocalFree(argv);
    }
    if (!opened && !ws.settings.lastFile.empty() && PathExists(ws.settings.lastFile)) {
        opened = OpenFile(ws.settings.lastFile) != nullptr;
    }


    Layout();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    SetFocus(Active() ? Active()->ed->Hwnd() : hwnd_);
    DragAcceptFiles(hwnd_, TRUE);
    return true;
}

int App::Run() {
    // 不要用 IsDialogMessageW：它会把 WM_MOUSEMOVE 吞掉，导致悬停状态（标签关闭按钮、
    // 侧栏行、测试用例行）永远拿不到，表现为"点不到"。本程序不需要对话框式 Tab 导航。
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GfxShutdown();
    return (int)msg.wParam;
}

// ====================== 布局 ======================
void App::Layout() {
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right, H = rc.bottom;
    const int titleH = g_theme.TitleH();
    const int toolH = g_theme.ToolH();
    const int statusH = g_theme.StatusH();

    rcTitle_ = { 0, 0, W, titleH };
    rcTool_  = { 0, titleH, W, titleH + toolH };
    rcStatus_ = { 0, H - statusH, W, H };

    int contentTop = titleH + toolH;
    int contentBottom = H - statusH;

    int sideW = sideVisible ? g_theme.S(sideWidth) : 0;
    rcSide_ = { 0, contentTop, sideW, contentBottom };
    rcSideHead_ = { 0, contentTop, sideW, contentTop + g_theme.S(34) };

    int bx = sideW;
    int bottomH = bottomVisible ? g_theme.S(bottomHeight) : 0;
    int bottomTop = contentBottom - bottomH;

    rcTab_ = { bx, contentTop, W, contentTop + g_theme.TabH() };
    rcEdit_ = { bx, rcTab_.bottom, W, bottomTop };
    rcBottom_ = { bx, bottomTop, W, contentBottom };

    rcBottomTabs_ = { rcBottom_.left, rcBottom_.top, rcBottom_.right, rcBottom_.top + g_theme.S(34) };

    int listW = g_theme.S(180);
    rcTestList_ = { rcBottom_.left, rcBottomTabs_.bottom, rcBottom_.left + listW, rcBottom_.bottom };
    rcTestEdits_ = { rcTestList_.right + g_theme.S(8), rcBottomTabs_.bottom, rcBottom_.right, rcBottom_.bottom };

    // 编辑器
    for (auto &d : docs) {
        if (d->ed) {
            HWND h = d->ed->Hwnd();
            SetWindowPos(h, nullptr, rcEdit_.left, rcEdit_.top,
                         rcEdit_.right - rcEdit_.left, rcEdit_.bottom - rcEdit_.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // 底部三个编辑框布局
    if (hIn) {
        int pad = g_theme.S(8);
        int labelH = g_theme.S(18);
        RECT e = rcTestEdits_;
        int availW = (e.right - e.left) - pad * 3;
        if (availW < g_theme.S(120)) availW = g_theme.S(120);
        int leftW = availW * 46 / 100;
        int rightW = availW - leftW;
        int availH = (e.bottom - e.top) - pad - labelH * 2;
        if (availH < g_theme.S(40)) availH = g_theme.S(40);
        int h1 = availH * 48 / 100;
        int h2 = availH - h1;

        int xL = e.left + pad;
        int xR = xL + leftW + pad;
        int yTop = e.top + labelH;
        int yBot = yTop + h1 + labelH;

        rcIn_  = { xL, yTop, xL + leftW, yTop + h1 };
        rcExp_ = { xL, yBot, xL + leftW, yBot + h2 };
        rcAct_ = { xR, yTop, xR + rightW, e.bottom - pad };
        rcOut_ = { rcBottom_.left + g_theme.S(10), rcBottomTabs_.bottom + g_theme.S(8),
                   rcBottom_.right - g_theme.S(10), rcBottom_.bottom - g_theme.S(10) };
        rcDiag_ = rcOut_;

        SetWindowPos(hIn, nullptr, rcIn_.left, rcIn_.top, rcIn_.right - rcIn_.left, rcIn_.bottom - rcIn_.top, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hExp, nullptr, rcExp_.left, rcExp_.top, rcExp_.right - rcExp_.left, rcExp_.bottom - rcExp_.top, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hAct, nullptr, rcAct_.left, rcAct_.top, rcAct_.right - rcAct_.left, rcAct_.bottom - rcAct_.top, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hOut, nullptr, rcOut_.left, rcOut_.top, rcOut_.right - rcOut_.left, rcOut_.bottom - rcOut_.top, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hDiag, nullptr, rcDiag_.left, rcDiag_.top, rcDiag_.right - rcDiag_.left, rcDiag_.bottom - rcDiag_.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    UpdateTestEditorsVisibility();

    RebuildButtons();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void App::RebuildButtons() {
    buttons.clear();
    auto add = [&](int id, const wchar_t *text, unsigned glyphCode, bool primary = false,
                   bool iconOnly = false) {
        UIButton b;
        b.id = id; b.text = text ? text : L""; b.glyph = glyphCode;
        b.primary = primary; b.iconOnly = iconOnly;
        buttons.push_back(b);
    };
    // 运行组
    add(1, L"编译", glyph::Play);
    add(2, L"运行", glyph::Lightning);
    add(3, L"编译运行", glyph::Lightning, true);
    add(8, L"全部用例", glyph::Test);
    // 文件组
    add(4, L"新建题目", glyph::Add);
    add(5, L"新建解法", glyph::FileCode);
    // 右侧
    add(6, L"设置", glyph::Settings);
    add(7, L"", glyph::Info, false, true);
    // 侧栏头部小按钮
    add(20, L"", glyph::Add, false, true);
    add(21, L"", glyph::Refresh, false, true);
    add(22, L"", glyph::FolderOpen, false, true);
    // 窗口按钮
    add(901, L"", glyph::Minus, false, true);
    add(902, L"", glyph::Square, false, true);
    add(903, L"", glyph::Close, false, true);
    // 底部标签按钮（自绘为标签，占位）
    add(910, L"测试用例", 0);
    add(911, L"输出", 0);
    add(912, L"编译信息", 0);

    // ---- 计算矩形 ----
    int pad = g_theme.S(8);
    int bh = g_theme.S(32);
    int by = rcTool_.top + (rcTool_.bottom - rcTool_.top - bh) / 2;

    // 窗口按钮
    int wbW = g_theme.S(46);
    int wx = rcTitle_.right - wbW * 3;
    int wbY = rcTitle_.top;
    int wbH = g_theme.TitleH();
    for (auto &b : buttons) {
        if (b.id == 901) b.rc = { wx, wbY, wx + wbW, wbY + wbH };
        if (b.id == 902) b.rc = { wx + wbW, wbY, wx + wbW * 2, wbY + wbH };
        if (b.id == 903) b.rc = { wx + wbW * 2, wbY, wx + wbW * 3, wbY + wbH };
    }

    // 工具栏按钮
    int x = rcTool_.left + pad;
    HDC dc = GetDC(hwnd_);
    SelectObject(dc, g_theme.Ui());
    for (auto &b : buttons) {
        if (b.id < 1 || b.id > 8) continue;
        int tw = b.iconOnly ? bh : TextWidth(dc, b.text, g_theme.Ui()) + g_theme.S(56);
        if (b.text.empty()) tw = bh;
        b.rc = { x, by, x + tw, by + bh };
        x += tw + g_theme.S(6);
        if (b.id == 8) x += g_theme.S(10);      // 组间空隙
        if (b.id == 5) x += g_theme.S(10);
    }
    ReleaseDC(hwnd_, dc);

    // 设置按钮靠右
    for (auto &b : buttons) {
        if (b.id == 6) {
            HDC dc2 = GetDC(hwnd_);
            SelectObject(dc2, g_theme.Ui());
            int tw = TextWidth(dc2, b.text, g_theme.Ui()) + g_theme.S(56);
            ReleaseDC(hwnd_, dc2);
            b.rc = { rcTool_.right - pad - tw, by, rcTool_.right - pad, by + bh };
        }
        if (b.id == 7) {
            // 放在「设置」左边，不能重叠
            int right = rcTool_.right - pad - g_theme.S(132) - g_theme.S(8);
            b.rc = { right - g_theme.S(34), by, right, by + bh };
        }
    }

    // 侧栏头部按钮
    {
        int s = g_theme.S(24);
        int yy = rcSideHead_.top + (rcSideHead_.bottom - rcSideHead_.top - s) / 2;
        int xx = rcSideHead_.right - g_theme.S(8) - s;
        for (auto &b : buttons) {
            if (b.id == 20) b.rc = { xx, yy, xx + s, yy + s };
            if (b.id == 21) b.rc = { xx - s - g_theme.S(2), yy, xx - g_theme.S(2), yy + s };
            if (b.id == 22) b.rc = { xx - s * 2 - g_theme.S(4), yy, xx - s - g_theme.S(4), yy + s };
            if (b.id == 22) b.visible = false;   // 暂不显示
        }
    }

    // 底部标签
    {
        int xx = rcBottom_.left + g_theme.S(8);
        int yy = rcBottomTabs_.top;
        int hh = rcBottomTabs_.bottom - rcBottomTabs_.top;
        HDC dc3 = GetDC(hwnd_);
        SelectObject(dc3, g_theme.UiBold());
        for (auto &b : buttons) {
            if (b.id >= 910 && b.id <= 912) {
                int tw = TextWidth(dc3, b.text, g_theme.UiBold()) + g_theme.S(34);
                b.rc = { xx, yy, xx + tw, yy + hh };
                xx += tw + g_theme.S(2);
            }
        }
        ReleaseDC(hwnd_, dc3);
    }
}

// ====================== 绘制 ======================
void App::OnPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc; GetClientRect(hwnd_, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    PaintAll(mem);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

static void DrawWindowButton(HDC dc, const UIButton &b, const Palette &c, const Theme &t) {
    RECT r = b.rc;
    if (b.hover) {
        COLORREF bg = (b.id == 903) ? RGB(0xC4, 0x2B, 0x2B) : c.bgHover;
        FillRectC(dc, r, bg);
    }
    COLORREF fg = (b.id == 903 && b.hover) ? RGB(0xFF, 0xFF, 0xFF) : c.textMuted;
    if (b.hover) fg = (b.id == 903) ? RGB(0xFF, 0xFF, 0xFF) : c.text;
    RECT ir = r;
    if (b.id == 902) {
        // 最大化按钮画方框
        int s = t.S(9);
        RECT rr{ (r.left + r.right) / 2 - s / 2, (r.top + r.bottom) / 2 - s / 2,
                 (r.left + r.right) / 2 + s / 2, (r.top + r.bottom) / 2 + s / 2 };
        StrokeRound(dc, rr, 0, fg, 1);
    } else {
        DrawIconC(dc, b.glyph, ir, fg, t.Icon());
    }
}

static void DrawToolButton(HDC dc, const UIButton &b, const Palette &c, const Theme &t) {
    RECT r = b.rc;
    int radius = t.Radius();
    COLORREF fg = c.text;
    if (!b.enabled) fg = c.textFaint;

    if (b.primary) {
        COLORREF top = b.hover ? RGB(0x35, 0x86, 0x55) : c.accentDim;
        COLORREF bot = b.hover ? RGB(0x27, 0x68, 0x41) : c.accentDeep;
        if (b.pressed) { top = c.accentDeep; bot = c.accentDeep; }
        FillRound2(dc, r, radius, top, bot);
        StrokeRound(dc, r, radius, b.hover ? c.accent : c.borderGlow, 1);
        fg = c.accentSoft;
    } else if (b.hover || b.pressed) {
        FillRound(dc, r, radius, b.pressed ? c.bgActive : c.bgHover);
        StrokeRound(dc, r, radius, c.borderGlow, 1);
        fg = c.text;
    }

    if (b.glyph) {
        RECT ir = r;
        int iw = t.S(18);
        if (b.text.empty()) {
            DrawIconC(dc, b.glyph, ir, b.primary ? c.accentSoft : fg, t.Icon());
        } else {
            ir.left += t.S(10);
            ir.right = ir.left + iw;
            DrawIconC(dc, b.glyph, ir, b.primary ? c.accentSoft : fg, t.Icon());
            RECT tr = r;
            tr.left = ir.right + t.S(4);
            tr.right -= t.S(10);
            DrawTextC(dc, b.text, tr, fg, t.Ui());
        }
    } else {
        DrawTextC(dc, b.text, r, fg, t.Ui(), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void App::PaintAll(HDC dc) {
    RECT rc; GetClientRect(hwnd_, &rc);
    FillRectC(dc, rc, g_theme.c.bgRoot);
    if (rcEdit_.right > rcEdit_.left) FillRectC(dc, rcEdit_, g_theme.c.bgEditor);
    if (!Active() && rcEdit_.right > rcEdit_.left) DrawEmptyState(dc);
    PaintTitle(dc);
    PaintToolbar(dc);
    PaintSidebar(dc);
    PaintTabs(dc);
    PaintBottom(dc);
    PaintStatus(dc);
}

// 没有打开文件时的引导页
void App::DrawEmptyState(HDC dc) {
    const Palette &c = g_theme.c;
    RECT r = rcEdit_;
    int w = g_theme.S(460);
    int h = g_theme.S(200);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    RECT card{ cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2 };

    // 标记
    RECT logo{ card.left + g_theme.S(24), card.top + g_theme.S(26),
               card.left + g_theme.S(24) + g_theme.S(40), card.top + g_theme.S(26) + g_theme.S(40) };
    FillRound(dc, logo, g_theme.S(10), c.accentDeep);
    StrokeRound(dc, logo, g_theme.S(10), c.accentDim, 1);
    {
        RECT mk = logo;
        int inset = g_theme.S(8);
        mk.left += inset; mk.top += inset; mk.right -= inset; mk.bottom -= inset;
        DrawLogoMark(dc, mk, c.accent);
    }

    RECT t1{ logo.right + g_theme.S(14), logo.top - g_theme.S(2), card.right - g_theme.S(20), logo.top + g_theme.S(22) };
    DrawTextC(dc, L"开始写代码", t1, c.text, g_theme.UiBold());
    RECT t2{ t1.left, t1.bottom, card.right - g_theme.S(20), t1.bottom + g_theme.S(22) };
    DrawTextC(dc, L"点侧栏的 ＋ 一键新建题目", t2, c.textMuted, g_theme.UiSmall());

    int y = logo.bottom + g_theme.S(20);
    auto hint = [&](const wchar_t *k, const wchar_t *v) {
        RECT kr{ card.left + g_theme.S(24), y, card.left + g_theme.S(140), y + g_theme.S(22) };
        DrawTextC(dc, k, kr, c.accentSoft, g_theme.UiSmall());
        RECT vr{ kr.right, y, card.right - g_theme.S(20), y + g_theme.S(22) };
        DrawTextC(dc, v, vr, c.textFaint, g_theme.UiSmall(),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += g_theme.S(24);
    };
    hint(L"F11", L"编译并运行当前文件");
    hint(L"F6", L"跑同目录下的全部 .in/.out 用例");
    hint(L"拖拽", L"把文件从资源管理器拖到侧栏文件夹上");
    hint(L"工作区", ws.settings.workspace.c_str());
}

void App::PaintTitle(HDC dc) {
    const Palette &c = g_theme.c;
    FillRound2(dc, rcTitle_, 0, RGB(0x10, 0x22, 0x18), c.bgRoot);
    DrawHLine(dc, rcTitle_.left, rcTitle_.right, rcTitle_.bottom - 1, c.borderSoft);

    // 品牌标记
    int s = g_theme.S(22);
    int x = g_theme.S(14);
    int y = rcTitle_.top + (rcTitle_.bottom - rcTitle_.top - s) / 2;
    RECT logo{ x, y, x + s, y + s };
    FillRound(dc, logo, g_theme.S(6), c.accentDeep);
    StrokeRound(dc, logo, g_theme.S(6), c.accentDim, 1);
    {
        RECT mark = logo;
        int inset = g_theme.S(4);
        mark.left += inset; mark.top += inset;
        mark.right -= inset; mark.bottom -= inset;
        DrawLogoMark(dc, mark, c.accent);
    }

    RECT tr = rcTitle_;
    tr.left = logo.right + g_theme.S(9);
    tr.right = tr.left + g_theme.S(160);
    DrawTextC(dc, L"Forest Code", tr, c.text, g_theme.UiBold());
    // 版本/副标题
    RECT sr = tr;
    sr.left = tr.right + g_theme.S(2);
    sr.right = sr.left + g_theme.S(120);
    DrawTextC(dc, L"竞赛工作台", sr, c.textFaint, g_theme.UiSmall());

    // 窗口按钮
    for (auto &b : buttons) {
        if (b.id >= 901 && b.id <= 903) DrawWindowButton(dc, b, c, g_theme);
    }
}

void App::PaintToolbar(HDC dc) {
    const Palette &c = g_theme.c;
    FillRectC(dc, rcTool_, c.bgPanel);
    DrawHLine(dc, rcTool_.left, rcTool_.right, rcTool_.bottom - 1, c.border);

    for (auto &b : buttons) {
        if (b.id >= 1 && b.id <= 8 && b.visible) DrawToolButton(dc, b, c, g_theme);
    }
    // 组分隔线
    int sepY1 = rcTool_.top + g_theme.S(11), sepY2 = rcTool_.bottom - g_theme.S(11);
    for (auto &b : buttons) {
        if (b.id == 8) DrawVLine(dc, b.rc.right + g_theme.S(6), sepY1, sepY2, c.border);
        if (b.id == 5) DrawVLine(dc, b.rc.right + g_theme.S(6), sepY1, sepY2, c.border);
    }
    for (auto &b : buttons) {
        if (b.id == 6 || b.id == 7) DrawToolButton(dc, b, c, g_theme);
    }
}

void App::PaintSidebar(HDC dc) {
    if (!sideVisible) return;
    const Palette &c = g_theme.c;
    FillRectC(dc, rcSide_, c.bgPanel);
    DrawVLine(dc, rcSide_.right - 1, rcSide_.top, rcSide_.bottom, c.border);

    // ---- 头部：工作区名 ----
    FillRectC(dc, rcSideHead_, c.bgPanel);
    {
        int d = g_theme.S(5);
        int cy = (rcSideHead_.top + rcSideHead_.bottom) / 2;
        RECT dot{ g_theme.S(14), cy - d / 2, g_theme.S(14) + d, cy - d / 2 + d };
        FillRound(dc, dot, d / 2, c.accent);
    }
    RECT tr = rcSideHead_;
    tr.left += g_theme.S(26);
    tr.right = rcSide_.right - g_theme.S(90);
    std::wstring title = FileName(ws.settings.workspace);
    if (title.empty()) title = L"工作区";
    DrawTextC(dc, title, tr, c.text, g_theme.UiBold(),
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    for (auto &b : buttons) if (b.id >= 20 && b.id <= 22 && b.visible) DrawToolButton(dc, b, c, g_theme);
    DrawHLine(dc, rcSide_.left, rcSide_.right - 1, rcSideHead_.bottom - 1, c.borderSoft);

    // ---- 文件树 ----
    HRGN clip = CreateRectRgn(rcSide_.left, rcSideHead_.bottom, rcSide_.right - 1, rcSide_.bottom);
    SelectClipRgn(dc, clip);

    std::wstring activePath = Active() ? Active()->path : L"";

    for (size_t i = 0; i < fsRows.size(); ++i) {
        const FsRow &it = fsRows[i];
        RECT r = FsRowRect((int)i);
        if (r.bottom < rcSideHead_.bottom || r.top > rcSide_.bottom) continue;

        bool sel = ((int)i == selFsRow);
        bool hov = ((int)i == hotFsRow);
        bool isActive = (!it.isDir && it.path == activePath);

        if (sel || isActive) {
            FillRectC(dc, r, c.bgActive);
            FillRectC(dc, RECT{ r.left, r.top, r.left + g_theme.S(2), r.bottom }, c.accent);
        } else if (hov) {
            FillRectC(dc, r, c.bgHover);
        }

        int x = r.left + g_theme.S(10) + it.depth * g_theme.S(15);

        // 展开箭头（目录）
        if (it.isDir) {
            RECT ar{ x, r.top, x + g_theme.S(14), r.bottom };
            DrawIconC(dc, it.expanded ? glyph::ChevronDown : glyph::ChevronRight,
                      ar, c.textFaint, g_theme.IconSmall());
        }
        x += g_theme.S(16);

        // 图标
        RECT ir{ x, r.top, x + g_theme.S(16), r.bottom };
        unsigned g = glyph::FileCode;
        COLORREF ic = c.textMuted;
        if (it.isDir) {
            g = it.expanded ? glyph::FolderOpen : glyph::Folder;
            ic = c.accent;
        } else {
            std::wstring ext = FileExt(it.name);
            if (ext == L".cpp" || ext == L".cc" || ext == L".cxx" || ext == L".c" || ext == L".h" || ext == L".hpp") {
                g = glyph::FileCode; ic = c.accentSoft;
            } else if (ext == L".md" || ext == L".txt") {
                g = glyph::Info; ic = c.info;
            } else if (ext == L".in" || ext == L".out" || ext == L".ans") {
                g = glyph::Chip; ic = c.textFaint;
            } else {
                g = glyph::FileCode; ic = c.textFaint;
            }
        }
        DrawIconC(dc, g, ir, ic, g_theme.IconSmall());
        x = ir.right + g_theme.S(7);

        // 名字
        RECT nr{ x, r.top, r.right - g_theme.S(22), r.bottom };
        COLORREF tc = (sel || isActive) ? c.text : c.textMuted;
        HFONT f = it.isDir ? g_theme.UiBold() : g_theme.Ui();
        if (isActive) f = g_theme.UiBold();
        DrawTextC(dc, it.name, nr, tc, f,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // 未保存圆点
        if (!it.isDir && isActive && Active() && Active()->ed && Active()->ed->Modified()) {
            int cx = r.right - g_theme.S(12), cy = (r.top + r.bottom) / 2;
            int rr = g_theme.S(3);
            HBRUSH br = CreateSolidBrush(c.accent);
            HGDIOBJ ob = SelectObject(dc, br);
            HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(br);
        }
    }

    // 空工作区提示
    if (fsRows.empty()) {
        RECT er = rcSide_;
        er.top = rcSideHead_.bottom + g_theme.S(20);
        DrawTextC(dc, L"点上面的 ＋ 开始", er, c.textFaint, g_theme.Ui(),
                  DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}
// 手绘关闭叉：图标字体 E8BB 的字形在 em 盒里偏高，用 DT_VCENTER 居中会视觉上浮
static void DrawCloseX(HDC dc, const RECT &r, COLORREF col, int thick) {
    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;
    int s = g_theme.S(4);
    if (s < 2) s = 2;
    HPEN pen = CreatePen(PS_SOLID, thick < 1 ? 1 : thick, col);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, cx - s, cy - s, nullptr);
    LineTo(dc, cx + s + 1, cy + s + 1);
    MoveToEx(dc, cx + s, cy - s, nullptr);
    LineTo(dc, cx - s - 1, cy + s + 1);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void App::PaintTabs(HDC dc) {
    const Palette &c = g_theme.c;
    FillRectC(dc, rcTab_, c.bgPanel);
    DrawHLine(dc, rcTab_.left, rcTab_.right, rcTab_.bottom - 1, c.border);

    int x = rcTab_.left + g_theme.S(6);
    HDC mdc = dc;
    SelectObject(mdc, g_theme.Ui());

    for (size_t i = 0; i < docs.size(); ++i) {
        Doc *d = docs[i].get();
        int tw = TextWidth(mdc, d->title, g_theme.Ui()) + g_theme.S(64);
        if (tw > g_theme.S(240)) tw = g_theme.S(240);
        RECT r{ x, rcTab_.top + g_theme.S(5), x + tw, rcTab_.bottom };
        bool act = ((int)i == activeDoc);
        bool hov = ((int)i == hotDocTab);

        if (act) {
            FillRound(dc, RECT{ r.left, r.top, r.right, r.bottom + g_theme.S(6) }, g_theme.S(6), c.bgEditor);
            FillRectC(dc, RECT{ r.left + g_theme.S(6), r.top, r.right - g_theme.S(6), r.top + g_theme.S(2) }, c.accent);
        } else if (hov) {
            FillRound(dc, r, g_theme.S(6), c.bgHover);
        }

        bool modified = d->ed && d->ed->Modified();
        RECT ir{ r.left + g_theme.S(10), r.top, r.left + g_theme.S(26), r.bottom };
        if (d->statement) DrawIconC(dc, glyph::Info, ir, act ? c.info : c.textFaint, g_theme.IconSmall());

        RECT tr{ ir.right, r.top, r.right - g_theme.S(22), r.bottom };
        DrawTextC(dc, d->title, tr, act ? c.text : c.textMuted, act ? g_theme.UiBold() : g_theme.Ui(),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // 关闭按钮 / 修改标记
        RECT cr{ r.right - g_theme.S(24), r.top, r.right - g_theme.S(6), r.bottom };
        if ((int)i == hotDocClose) {
            FillRound(dc, RECT{ cr.left, r.top + g_theme.S(6), cr.right, r.bottom - g_theme.S(6) },
                      g_theme.S(4), c.bgActive);
            DrawCloseX(dc, cr, c.text, g_theme.S(1));
        } else if (modified) {
            int cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
            int rr = g_theme.S(3);
            HBRUSH br = CreateSolidBrush(c.accent);
            HGDIOBJ ob = SelectObject(dc, br);
            HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(br);
        } else if (act) {
            DrawCloseX(dc, cr, c.textFaint, g_theme.S(1));
        }

        x += tw + g_theme.S(4);
        if (x > rcTab_.right - g_theme.S(40)) break;
    }
}

void App::PaintBottom(HDC dc) {
    if (!bottomVisible) return;
    const Palette &c = g_theme.c;
    FillRectC(dc, rcBottom_, c.bgPanel);
    DrawHLine(dc, rcBottom_.left, rcBottom_.right, rcBottom_.top, c.border);

    // 标签
    FillRectC(dc, rcBottomTabs_, c.bgPanel);
    for (auto &b : buttons) {
        if (b.id < 910 || b.id > 912) continue;
        int page = b.id - 910;
        bool act = ((int)bottomPage == page);
        bool hov = (hotBottomTab == page);
        RECT r = b.rc;
        if (act) {
            FillRound(dc, RECT{ r.left, r.top + g_theme.S(6), r.right, r.bottom + g_theme.S(4) },
                      g_theme.S(6), c.bgRaised);
            FillRectC(dc, RECT{ r.left + g_theme.S(10), r.bottom - g_theme.S(3),
                                r.right - g_theme.S(10), r.bottom - g_theme.S(1) }, c.accent);
        } else if (hov) {
            FillRound(dc, r, g_theme.S(6), c.bgHover);
        }
        DrawTextC(dc, b.text, r, act ? c.text : c.textMuted,
                  act ? g_theme.UiBold() : g_theme.Ui(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    DrawHLine(dc, rcBottomTabs_.left, rcBottomTabs_.right, rcBottomTabs_.bottom, c.borderSoft);

    if (bottomPage != BottomPage::Tests) return;

    // 测试列表
    FillRectC(dc, rcTestList_, c.bgPanel);
    DrawVLine(dc, rcTestList_.right, rcTestList_.top, rcTestList_.bottom, c.borderSoft);

    HRGN clip = CreateRectRgn(rcTestList_.left, rcTestList_.top, rcTestList_.right, rcTestList_.bottom);
    SelectClipRgn(dc, clip);

    int y = rcTestList_.top + g_theme.S(6) - testScroll;
    int rowH = g_theme.S(34);
    int count = (int)curTests.size();

    for (int i = 0; i < count; ++i) {
        RECT r{ rcTestList_.left + g_theme.S(6), y, rcTestList_.right - g_theme.S(8), y + rowH - g_theme.S(4) };
        y += rowH;
        if (r.bottom < rcTestList_.top || r.top > rcTestList_.bottom) continue;
        bool sel = (i == selTest);
        bool hov = (i == hotTestRow);
        if (sel) {
            FillRound(dc, r, g_theme.S(6), c.bgActive);
            StrokeRound(dc, r, g_theme.S(6), c.borderGlow, 1);
        } else if (hov) {
            FillRound(dc, r, g_theme.S(6), c.bgHover);
        }

        const TestCase &tc = curTests[i];
        // 状态徽章
        RECT br{ r.left + g_theme.S(8), r.top + g_theme.S(7), r.left + g_theme.S(24), r.bottom - g_theme.S(7) };
        if (tc.hasResult) {
            FillRound(dc, br, g_theme.S(4), tc.passed ? RGB(0x16, 0x3A, 0x24) : RGB(0x3A, 0x18, 0x1A));
            DrawIconC(dc, tc.passed ? glyph::CheckMark : glyph::Cancel, br,
                      tc.passed ? c.ok : c.error, g_theme.IconSmall());
        } else {
            FillRound(dc, br, g_theme.S(4), c.bgRaised);
            DrawTextC(dc, std::to_wstring(i + 1), br, c.textFaint, g_theme.UiSmall(),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT tr{ br.right + g_theme.S(8), r.top, r.right - g_theme.S(28), r.top + rowH / 2 };
        DrawTextC(dc, L"用例 " + std::to_wstring(i + 1), tr, sel ? c.text : c.textMuted,
                  g_theme.Ui(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT sr{ tr.left, r.top + rowH / 2 - g_theme.S(2), tr.right, r.bottom };
        std::wstring sub;
        if (tc.hasResult) {
            if (tc.timeout) sub = L"超时";
            else sub = FormatW(L"%.0f ms", tc.ms);
            if (tc.hasExpected) sub += tc.passed ? L" · 通过" : L" · 答案错误";
            else sub += L" · 已运行";
        } else {
            sub = tc.hasExpected ? L"未运行" : L"无期望输出";
        }
        DrawTextC(dc, sub, sr, tc.hasResult ? (tc.passed ? c.ok : c.error) : c.textFaint,
                  g_theme.UiSmall(), DT_LEFT | DT_TOP | DT_SINGLELINE);

        // 悬停/选中时显示删除按钮
        if (sel || hov) {
            RECT dr{ r.right - g_theme.S(26), r.top + g_theme.S(4), r.right - g_theme.S(4), r.bottom - g_theme.S(4) };
            if (i == hotTestDel) FillRound(dc, dr, g_theme.S(4), c.bgActive);
            DrawIconC(dc, glyph::Delete, dr, (i == hotTestDel) ? c.error : c.textFaint, g_theme.IconSmall());
        }
    }

    // 新增按钮
    {
        RECT r{ rcTestList_.left + g_theme.S(6), y, rcTestList_.right - g_theme.S(8), y + rowH - g_theme.S(4) };
        bool hov = (hotTestRow == -1);
        if (hov) FillRound(dc, r, g_theme.S(6), c.bgHover);
        RECT ir{ r.left + g_theme.S(8), r.top, r.left + g_theme.S(24), r.bottom };
        DrawIconC(dc, glyph::Add, ir, hov ? c.accent : c.textFaint, g_theme.IconSmall());
        RECT tr{ ir.right + g_theme.S(8), r.top, r.right, r.bottom };
        DrawTextC(dc, L"新增用例", tr, hov ? c.text : c.textFaint, g_theme.Ui(),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    // 右侧三个框的标题 + 边框
    if (hIn) {
        int labelH = g_theme.S(18);
        auto frame = [&](RECT r) {
            RECT f{ r.left - 1, r.top - 1, r.right + 1, r.bottom + 1 };
            StrokeRound(dc, f, 0, c.border, 1);
        };
        frame(rcIn_); frame(rcExp_); frame(rcAct_);
        auto label = [&](RECT r, const wchar_t *text, COLORREF col) {
            DrawTextC(dc, text, r, col, g_theme.UiSmall(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        };
        label(RECT{ rcIn_.left, rcIn_.top - labelH, rcIn_.right, rcIn_.top }, L"输入 (stdin)", c.textMuted);
        label(RECT{ rcExp_.left, rcExp_.top - labelH, rcExp_.right, rcExp_.top }, L"期望输出", c.textMuted);
        label(RECT{ rcAct_.left, rcAct_.top - labelH, rcAct_.right, rcAct_.top }, L"实际输出", c.textMuted);
    }
}

void App::PaintStatus(HDC dc) {
    const Palette &c = g_theme.c;
    FillRectC(dc, rcStatus_, c.bgRoot);
    DrawHLine(dc, rcStatus_.left, rcStatus_.right, rcStatus_.top, c.border);

    int pad = g_theme.S(12);
    RECT r = rcStatus_;
    r.left += pad;
    r.top += 1;

    // 状态灯
    COLORREF dot = jobRunning ? c.warn : c.ok;
    int cx = r.left + g_theme.S(4), cy = (r.top + r.bottom) / 2;
    int rad = g_theme.S(3);
    HBRUSH br = CreateSolidBrush(dot);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - rad, cy - rad, cx + rad + 1, cy + rad + 1);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(br);

    RECT tr = r;
    tr.left = cx + g_theme.S(10);
    tr.right = tr.left + g_theme.S(420);
    DrawTextC(dc, statusText, tr, jobRunning ? c.warn : c.textMuted, g_theme.UiSmall());

    // 右侧信息
    std::wstring right;
    if (Doc *d = Active()) {
        right = FormatW(L"%s   ·   Ln %d, Col %d   ·   UTF-8   ·   %s",
                        ws.settings.stdFlag.c_str(),
                        d->ed->CurrentLine(), d->ed->CurrentCol(),
                        FileName(d->path).c_str());
    } else {
        right = ws.settings.stdFlag;
    }
    RECT rr = rcStatus_;
    rr.right -= pad;
    rr.top += 1;
    DrawTextC(dc, right, rr, c.textFaint, g_theme.UiSmall(),
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

} // namespace fc

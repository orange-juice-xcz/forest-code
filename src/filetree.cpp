// Forest Code - 文件树（Obsidian 式：文件系统即应用）
#include "app.h"
#include "theme.h"
#include "util.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>

namespace fc {

// ======================================================================
//  工具
// ======================================================================
// 自然排序：题目2 排在 题目10 前面

static bool IsHiddenName(const std::wstring &n) {
    return !n.empty() && n[0] == L'.';   // .build / .git 等一律隐藏
}

static bool CopyDirRecursive(const std::wstring &src, const std::wstring &dst) {
    if (!MakeDirs(dst)) return false;
    for (auto &n : ListDir(src)) {
        std::wstring s = JoinPath(src, n), d = JoinPath(dst, n);
        if (IsDir(s)) { if (!CopyDirRecursive(s, d)) return false; }
        else if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) return false;
    }
    return true;
}

// ======================================================================
//  扫描 / 重建
// ======================================================================
void App::ScanDirInto(const std::wstring &dir, int depth) {
    std::vector<std::wstring> dirs, files;
    for (auto &n : ListDir(dir)) {
        if (IsHiddenName(n)) continue;
        if (IsDir(JoinPath(dir, n))) dirs.push_back(n);
        else files.push_back(n);
    }
    std::sort(dirs.begin(), dirs.end(), NaturalLess);
    std::sort(files.begin(), files.end(), NaturalLess);

    for (auto &n : dirs) {
        FsRow row;
        row.path = JoinPath(dir, n);
        row.name = n;
        row.isDir = true;
        row.depth = depth;
        row.expanded = expandedDirs_.count(row.path) > 0;
        fsRows.push_back(row);
        if (row.expanded) ScanDirInto(row.path, depth + 1);
    }
    for (auto &n : files) {
        FsRow row;
        row.path = JoinPath(dir, n);
        row.name = n;
        row.isDir = false;
        row.depth = depth;
        fsRows.push_back(row);
    }
}

void App::RebuildFileTree() {
    fsRows.clear();
    if (!ws.settings.workspace.empty() && IsDir(ws.settings.workspace))
        ScanDirInto(ws.settings.workspace, 0);
    if (selFsRow >= (int)fsRows.size()) selFsRow = (int)fsRows.size() - 1;
    if (hotFsRow >= (int)fsRows.size()) hotFsRow = -1;
    // 树变短了就把滚动位置拉回来，否则会滚成一片空白
    int content = g_theme.S(4) + (int)fsRows.size() * g_theme.S(28);
    int visible = rcSide_.bottom - rcSideHead_.bottom;
    int maxScroll = content > visible ? content - visible : 0;
    if (sideScroll > maxScroll) sideScroll = maxScroll;
    if (sideScroll < 0) sideScroll = 0;
    InvalidateRect(hwnd_, &rcSide_, FALSE);
}

RECT App::FsRowRect(int row) const {
    int rowH = g_theme.S(28);
    int y = rcSideHead_.bottom + g_theme.S(4) - sideScroll + row * rowH;
    return RECT{ rcSide_.left, y, rcSide_.right - 1, y + rowH };
}

int App::FsRowAt(POINT p) const {
    if (!sideVisible) return -1;
    if (p.x < rcSide_.left || p.x >= rcSide_.right) return -1;
    if (p.y <= rcSideHead_.bottom || p.y >= rcSide_.bottom) return -1;
    int rowH = g_theme.S(28);
    int idx = (p.y - rcSideHead_.bottom - g_theme.S(4) + sideScroll) / rowH;
    if (idx < 0 || idx >= (int)fsRows.size()) return -1;
    return idx;
}

// 新建目标目录：选中文件夹则用它，选中文件则用其父目录，否则工作区根
std::wstring App::TargetDir() const {
    if (selFsRow >= 0 && selFsRow < (int)fsRows.size()) {
        const FsRow &r = fsRows[selFsRow];
        return r.isDir ? r.path : ParentDir(r.path);
    }
    return ws.settings.workspace;
}

// ======================================================================
//  ＋ 一键新建题目
// ======================================================================
void App::CmdNewProblemHere() {
    std::wstring dir = TargetDir();
    if (dir.empty() || !IsDir(dir)) { SetStatus(L"工作区不可用"); return; }

    // 找最小空号：题目01、题目02 …
    int n = 1;
    std::wstring folder, base;
    for (; n < 10000; ++n) {
        wchar_t buf[32];
        swprintf(buf, 32, L"题目%02d", n);
        base = buf;
        folder = JoinPath(dir, base);
        if (!PathExists(folder)) break;
    }

    MakeDirs(folder);
    std::wstring md = JoinPath(folder, base + L".md");                    // 题目01.md
    std::wstring cpp = JoinPath(folder, L"代码" + base.substr(2) + L".cpp"); // 代码01.cpp
    WriteFileUtf8(md, "");    // 空文件：不套模板，直接开写
    WriteFileUtf8(cpp, "");

    expandedDirs_.insert(dir);
    expandedDirs_.insert(folder);
    RebuildFileTree();

    // 选中新文件夹
    for (size_t i = 0; i < fsRows.size(); ++i)
        if (fsRows[i].path == folder) { selFsRow = (int)i; break; }

    // 立刻打开 .cpp，光标就位，可以直接敲
    OpenFile(cpp);
    SetStatus(L"已新建 " + base + L"（题面 + 代码）");
}

// 在当前目录里新建一个空 .cpp（“新建解法”走这里）
void App::CmdNewSolution() {
    std::wstring dir = TargetDir();
    if (dir.empty() || !IsDir(dir)) { SetStatus(L"工作区不可用"); return; }
    int n = 1;
    std::wstring base, path;
    for (; n < 10000; ++n) {
        wchar_t buf[32];
        swprintf(buf, 32, L"代码%02d", n);
        base = buf;
        path = JoinPath(dir, base + L".cpp");
        if (!PathExists(path)) break;
    }
    WriteFileUtf8(path, "");
    expandedDirs_.insert(dir);
    RebuildFileTree();
    OpenFile(path);
    // 顺手进入就地重命名，起个有意义的名字
    for (size_t i = 0; i < fsRows.size(); ++i)
        if (fsRows[i].path == path) { selFsRow = (int)i; BeginInlineRename((int)i); break; }
    SetStatus(L"已新建 " + base + L".cpp");
}

void App::CmdNewProblem() { CmdNewProblemHere(); }

// 自由新建文件 / 文件夹
void App::CmdNewFileHere(bool folder) {
    std::wstring dir = TargetDir();
    if (dir.empty() || !IsDir(dir)) { SetStatus(L"工作区不可用"); return; }
    std::wstring base = folder ? L"新建文件夹" : L"新建文件.cpp";
    std::wstring path = JoinPath(dir, base);
    int n = 2;
    while (PathExists(path)) {
        std::wstring stem = folder ? L"新建文件夹" : L"新建文件";
        std::wstring ext = folder ? L"" : L".cpp";
        path = JoinPath(dir, stem + std::to_wstring(n++) + ext);
    }
    if (folder) MakeDirs(path);
    else WriteFileUtf8(path, "");
    expandedDirs_.insert(dir);
    RebuildFileTree();
    for (size_t i = 0; i < fsRows.size(); ++i)
        if (fsRows[i].path == path) {
            selFsRow = (int)i;
            if (!folder) OpenFile(path);
            BeginInlineRename((int)i);
            break;
        }
}

void App::CmdDeletePath(const std::wstring &path) {
    if (path.empty() || !PathExists(path)) return;
    bool isDir = IsDir(path);
    std::wstring msg = isDir ? (L"删除文件夹「" + FileName(path) + L"」及其中所有内容？")
                             : (L"删除文件「" + FileName(path) + L"」？");
    if (MessageBoxW(hwnd_, msg.c_str(), L"Forest Code", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    // 关掉相关文档
    for (int i = (int)docs.size() - 1; i >= 0; --i) {
        const std::wstring &dp = docs[i]->path;
        if (dp == path || (isDir && dp.rfind(path + L"\\", 0) == 0)) CloseDoc(i);
    }
    if (isDir) DeleteDirRecursive(path);
    else DeleteFileSafe(path);
    expandedDirs_.erase(path);
    RebuildFileTree();
    SetStatus(L"已删除 " + FileName(path));
}

void App::RevealPath(const std::wstring &path) {
    if (path.empty()) return;
    if (IsDir(path))
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    else
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

// ======================================================================
//  就地重命名
// ======================================================================
static LRESULT CALLBACK RenameEditProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR, DWORD_PTR ref) {
    App *app = (App *)ref;
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { PostMessageW(app->Hwnd(), WM_APP + 3, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { PostMessageW(app->Hwnd(), WM_APP + 3, 0, 0); return 0; }
    }
    if (msg == WM_KILLFOCUS) PostMessageW(app->Hwnd(), WM_APP + 3, 1, 0);
    return DefSubclassProc(h, msg, wParam, lParam);
}

void App::BeginInlineRename(int row) {
    if (row < 0 || row >= (int)fsRows.size()) return;
    if (hRename) CommitInlineRename(true);
    renameRow = row;
    RECT r = FsRowRect(row);
    int indent = g_theme.S(10) + fsRows[row].depth * g_theme.S(15) + g_theme.S(16) + g_theme.S(7);
    RECT er{ r.left + indent, r.top + g_theme.S(2), r.right - g_theme.S(6), r.bottom - g_theme.S(2) };
    hRename = CreateWindowExW(0, L"EDIT", fsRows[row].name.c_str(),
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
                              er.left, er.top, er.right - er.left, er.bottom - er.top,
                              hwnd_, nullptr, inst_, nullptr);
    if (!hRename) { renameRow = -1; return; }
    SendMessageW(hRename, WM_SETFONT, (WPARAM)g_theme.Ui(), TRUE);
    SendMessageW(hRename, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(g_theme.S(2), g_theme.S(2)));
    SetWindowSubclass(hRename, RenameEditProc, 3, (DWORD_PTR)this);
    // 选中主名（不含扩展名）
    std::wstring stem = fsRows[row].isDir ? fsRows[row].name : FileStem(fsRows[row].name);
    SendMessageW(hRename, EM_SETSEL, 0, (LPARAM)stem.size());
    SetFocus(hRename);
}

void App::CommitInlineRename(bool accept) {
    if (!hRename) return;
    HWND h = hRename;
    hRename = nullptr;
    std::wstring newName;
    if (accept) {
        int n = GetWindowTextLengthW(h);
        if (n > 0) { newName.resize((size_t)n); GetWindowTextW(h, &newName[0], n + 1); }
    }
    RemoveWindowSubclass(h, RenameEditProc, 3);
    DestroyWindow(h);

    int row = renameRow;
    renameRow = -1;
    if (!accept || newName.empty() || row < 0 || row >= (int)fsRows.size()) return;

    FsRow r = fsRows[row];
    if (newName == r.name) return;
    for (wchar_t ch : newName) {
        if (wcschr(L"\\/:*?\"<>|", ch)) { SetStatus(L"文件名不能包含 \\ / : * ? \" < > |"); return; }
    }
    std::wstring np = JoinPath(ParentDir(r.path), newName);
    if (PathExists(np)) { SetStatus(L"同名文件已存在"); return; }
    if (!MoveFileW(r.path.c_str(), np.c_str())) { SetStatus(L"重命名失败"); return; }

    // 已打开的文档跟着换路径
    for (auto &d : docs) {
        if (d->path == r.path) { d->path = np; d->title = FileName(np); }
        else if (r.isDir && d->path.rfind(r.path + L"\\", 0) == 0)
            d->path = np + d->path.substr(r.path.size());
    }
    if (r.isDir) {
        auto it = expandedDirs_.find(r.path);
        if (it != expandedDirs_.end()) { expandedDirs_.erase(it); expandedDirs_.insert(np); }
    }
    RebuildFileTree();
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetStatus(L"已重命名为 " + newName);
}

// ======================================================================
//  右键菜单
// ======================================================================
void App::ShowFsMenu(int row) {
    HMENU m = CreatePopupMenu();
    auto item = [&](UINT id, const wchar_t *text) {
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        mii.wID = id;
        mii.dwTypeData = (LPWSTR)text;
        mii.fState = MFS_ENABLED;
        InsertMenuItemW(m, (UINT)-1, TRUE, &mii);
    };
    auto sep = [&]() {
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_FTYPE;
        mii.fType = MFT_SEPARATOR;
        InsertMenuItemW(m, (UINT)-1, TRUE, &mii);
    };

    if (row >= 0 && row < (int)fsRows.size()) {
        const FsRow &r = fsRows[row];
        if (r.isDir) {
            item(3101, L"新建题目（题面 + 代码）");
            item(3102, L"新建文件");
            item(3103, L"新建文件夹");
            sep();
            item(3104, L"重命名");
            item(3105, L"删除");
            sep();
            item(3106, L"在资源管理器中打开");
        } else {
            item(3111, L"打开");
            sep();
            item(3104, L"重命名");
            item(3105, L"删除");
            sep();
            item(3106, L"在资源管理器中打开");
        }
    } else {
        item(3101, L"新建题目（题面 + 代码）");
        item(3102, L"新建文件");
        item(3103, L"新建文件夹");
        sep();
        item(3106, L"在资源管理器中打开工作区");
    }

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(m);
    if (cmd) OnFsMenuCommand(cmd);
}

void App::OnFsMenuCommand(int id) {
    int row = selFsRow;
    std::wstring path = (row >= 0 && row < (int)fsRows.size()) ? fsRows[row].path : std::wstring();
    switch (id) {
    case 3101: CmdNewProblemHere(); break;
    case 3102: CmdNewFileHere(false); break;
    case 3103: CmdNewFileHere(true); break;
    case 3104: BeginInlineRename(row); break;
    case 3105: CmdDeletePath(path); break;
    case 3106:
        if (path.empty()) RevealPath(ws.settings.workspace);
        else RevealPath(path);
        break;
    case 3111: if (!path.empty()) OpenFile(path); break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ======================================================================
//  拖拽：从资源管理器拖到文件夹上就复制进去
// ======================================================================
void App::OnDropFiles(POINT pt, HDROP hd) {
    int row = FsRowAt(pt);
    std::wstring dest;
    if (row >= 0) {
        const FsRow &r = fsRows[row];
        dest = r.isDir ? r.path : ParentDir(r.path);
    }

    UINT count = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
    if (dest.empty()) {
        // 不在树上：当作“打开文件”
        for (UINT i = 0; i < count; ++i) {
            wchar_t buf[MAX_PATH * 4]{};
            DragQueryFileW(hd, i, buf, MAX_PATH * 4);
            if (!IsDir(buf)) OpenFile(buf);
        }
        return;
    }

    int done = 0;
    for (UINT i = 0; i < count; ++i) {
        wchar_t buf[MAX_PATH * 4]{};
        DragQueryFileW(hd, i, buf, MAX_PATH * 4);
        std::wstring src = buf;
        if (ParentDir(src) == dest) continue;             // 已经在目标目录里
        std::wstring target = JoinPath(dest, FileName(src));
        if (PathExists(target)) {                          // 重名自动加序号
            std::wstring stem = FileStem(src), ext = FileExt(src);
            int k = 2;
            do { target = JoinPath(dest, stem + L" (" + std::to_wstring(k++) + L")" + ext); }
            while (PathExists(target));
        }
        bool ok = IsDir(src) ? CopyDirRecursive(src, target)
                             : (CopyFileW(src.c_str(), target.c_str(), FALSE) != 0);
        if (ok) ++done;
    }
    expandedDirs_.insert(dest);
    RebuildFileTree();
    SetStatus(FormatW(L"已复制 %d 个文件到 %s", done, FileName(dest).c_str()));
}

// ======================================================================
//  目录监听：外部增删改自动同步
// ======================================================================
static DWORD WINAPI DirWatchThread(LPVOID param) {
    App *app = (App *)param;
    std::wstring root = app->ws.settings.workspace;
    HANDLE h = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    app->dirWatch_ = h;
    std::vector<BYTE> buf(64 * 1024);
    while (!app->watchStop_) {
        DWORD bytes = 0;
        if (!ReadDirectoryChangesW(h, buf.data(), (DWORD)buf.size(), TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                                   &bytes, nullptr, nullptr)) break;
        if (bytes > 0) PostMessageW(app->Hwnd(), WM_APP + 2, 0, 0);
    }
    CloseHandle(h);
    app->dirWatch_ = nullptr;
    return 0;
}

void App::StartDirWatch() {
    if (watchThread_) return;
    watchStop_ = false;
    HANDLE t = CreateThread(nullptr, 0, DirWatchThread, this, 0, nullptr);
    if (t) watchThread_ = t;
}

// 停止监听。ReadDirectoryChangesW 是同步阻塞调用，光置标志位它不会醒，
// 必须用 CancelSynchronousIo 把那次调用打断。
void App::StopDirWatch() {
    if (!watchThread_) return;
    watchStop_ = true;
    CancelSynchronousIo(watchThread_);
    WaitForSingleObject(watchThread_, 1000);
    CloseHandle(watchThread_);
    watchThread_ = nullptr;
    dirWatch_ = nullptr;
}

// 换工作区后必须重开：原来的线程还盯着旧目录的句柄，
// 新工作区在资源管理器里的增删改永远不会刷新侧栏。
void App::RestartDirWatch() {
    StopDirWatch();
    StartDirWatch();
}

// ======================================================================
//  约定式测试用例：与当前代码同目录的 X.in + X.out 自动配对
// ======================================================================
void App::ScanTestsForActive() {
    std::vector<TestCase> old = std::move(curTests);
    curTests.clear();
    Doc *d = Active();
    if (!d) { testDir_.clear(); return; }
    std::wstring dir = ParentDir(d->path);
    testDir_ = dir;

    std::vector<std::wstring> ins;
    for (auto &n : ListDir(dir))
        if (FileExt(n) == L".in") ins.push_back(n);
    std::sort(ins.begin(), ins.end(), NaturalLess);

    for (auto &n : ins) {
        TestCase tc;
        tc.inPath = JoinPath(dir, n);
        tc.outPath = JoinPath(dir, FileStem(n) + L".out");
        tc.hasExpected = PathExists(tc.outPath);
        // 大文件只记路径，不读进内存：读进来还会被灌进 EDIT 控件，界面直接卡死
        tc.inTooBig = (FileSize(tc.inPath) > kInlineTestLimit);
        if (!tc.inTooBig) ReadFileUtf8(tc.inPath, tc.inText);
        if (tc.hasExpected) {
            tc.outTooBig = (FileSize(tc.outPath) > kInlineTestLimit);
            if (!tc.outTooBig) ReadFileUtf8(tc.outPath, tc.outText);
        }
        for (auto &o : old) {
            if (o.inPath == tc.inPath) {
                tc.hasResult = o.hasResult; tc.passed = o.passed; tc.timeout = o.timeout;
                tc.ms = o.ms; tc.exitCode = o.exitCode;
                tc.actual = o.actual; tc.stderrText = o.stderrText;
                break;
            }
        }
        curTests.push_back(tc);
    }
    if (selTest >= (int)curTests.size())
        selTest = curTests.empty() ? 0 : (int)curTests.size() - 1;
    if (selTest < 0) selTest = 0;
}

void App::OnDirChanged() {
    // 去抖：外部一次操作会触发多条通知
    treeDirty_ = true;
    SetTimer(hwnd_, 7, 250, nullptr);
}

} // namespace fc

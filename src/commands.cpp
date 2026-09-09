// Forest Code - 命令 / 交互 / 对话框
#include "app.h"
#include "theme.h"
#include "util.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>

namespace fc {

// ======================================================================
//  片段 / 侧栏
// ======================================================================
void App::BuildSnippets() {
    std::vector<Snippet> sn = Workspace::BuiltinSnippets();
    // 追加用户片段
    Ini ini;
    if (ini.Load(JoinPath(AppDataDir(), L"snippets.ini"))) {
        for (int i = 1; i <= 200; ++i) {
            std::string key = Format("snippet%d", i);
            std::string trig = ini.Get(key + ".trigger");
            if (trig.empty()) continue;
            Snippet s;
            s.trigger = trig;
            s.body = ini.Get(key + ".body");
            s.desc = ini.Get(key + ".desc");
            s.builtin = false;
            sn.push_back(s);
        }
    }
    for (auto &d : docs) if (d->ed) d->ed->SetSnippets(sn);
    snippets_ = sn;
}

void App::RebuildSidebar() {
    sideItems.clear();

    // 草稿区
    {
        SideItem it;
        it.kind = SideItem::Kind::Scratch;
        it.label = L"草稿区";
        it.depth = 0;
        it.path = ws.ScratchDir();
        sideItems.push_back(it);
    }
    for (auto &f : ListDir(ws.ScratchDir())) {
        if (IsDir(JoinPath(ws.ScratchDir(), f))) continue;
        if (FileExt(f) != L".cpp" && FileExt(f) != L".txt" && FileExt(f) != L".md") continue;
        SideItem it;
        it.kind = SideItem::Kind::Solution;
        it.label = FileStem(f);
        it.path = JoinPath(ws.ScratchDir(), f);
        it.depth = 1;
        sideItems.push_back(it);
    }

    for (size_t pi = 0; pi < ws.problems.size(); ++pi) {
        Problem &p = ws.problems[pi];
        SideItem it;
        it.kind = SideItem::Kind::Problem;
        it.problem = (int)pi;
        it.label = p.DisplayTitle();
        it.depth = 0;
        it.canExpand = true;
        it.expanded = p.expanded;
        sideItems.push_back(it);

        if (!p.expanded) continue;

        if (!p.statementPath.empty()) {
            SideItem s;
            s.kind = SideItem::Kind::Statement;
            s.problem = (int)pi;
            s.path = p.statementPath;
            s.label = L"题面";
            s.depth = 1;
            sideItems.push_back(s);
        }
        for (auto &sol : p.solutions) {
            SideItem s;
            s.kind = SideItem::Kind::Solution;
            s.problem = (int)pi;
            s.path = sol.path;
            s.label = sol.name;
            s.depth = 1;
            sideItems.push_back(s);
        }
        {
            SideItem s;
            s.kind = SideItem::Kind::NewSolution;
            s.problem = (int)pi;
            s.label = L"新建解法…";
            s.depth = 1;
            sideItems.push_back(s);
        }
        {
            SideItem s;
            s.kind = SideItem::Kind::Tests;
            s.problem = (int)pi;
            s.label = L"测试用例 (" + std::to_wstring(p.tests.size()) + L")";
            s.depth = 1;
            s.canExpand = true;
            s.expanded = p.testsExpanded;
            sideItems.push_back(s);
        }
        if (p.testsExpanded) {
            for (size_t ti = 0; ti < p.tests.size(); ++ti) {
                SideItem s;
                s.kind = SideItem::Kind::TestCase;
                s.problem = (int)pi;
                s.test = (int)ti;
                s.label = L"用例 " + std::to_wstring(ti + 1);
                s.depth = 2;
                sideItems.push_back(s);
            }
        }
    }
    if (selSideItem_ >= (int)sideItems.size()) selSideItem_ = -1;
}

// ======================================================================
//  文档
// ======================================================================
Doc *App::Active() {
    if (activeDoc < 0 || activeDoc >= (int)docs.size()) return nullptr;
    return docs[activeDoc].get();
}

Doc *App::OpenFile(const std::wstring &path, bool statement) {
    for (size_t i = 0; i < docs.size(); ++i) {
        if (docs[i]->path == path) { ActivateDoc((int)i); return docs[i].get(); }
    }
    auto d = std::make_unique<Doc>();
    d->path = path;
    d->title = FileName(path);
    if (statement) d->title = L"题面 · " + FileStem(path);
    d->statement = statement;
    d->ed = std::make_unique<Editor>();
    if (!d->ed->Create(hwnd_, 500 + (int)docs.size())) return nullptr;
    d->ed->SetCodeFont(ws.settings.codeFont, ws.settings.codeSize);
    d->ed->SetLang(statement ? (FileExt(path) == L".md" ? Lang::Markdown : Lang::Plain) : Lang::Cpp);
    d->ed->SetSnippets(snippets_);
    // 先给编辑器真实尺寸，避免 10x10 视口导致滚动位置错乱
    Layout();
    SetWindowPos(d->ed->Hwnd(), nullptr, rcEdit_.left, rcEdit_.top,
                 std::max(1, (int)(rcEdit_.right - rcEdit_.left)),
                 std::max(1, (int)(rcEdit_.bottom - rcEdit_.top)),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    if (!d->ed->Load(path)) {
        d->ed->SetText("");
    }
    if (statement) d->ed->SetReadOnly(true);

    d->ed->onUpdateUi = [this]() {
        RECT r = rcStatus_;
        InvalidateRect(hwnd_, &r, FALSE);
        InvalidateRect(hwnd_, &rcTab_, FALSE);
    };
    HWND h = d->ed->Hwnd();
    ShowWindow(h, SW_HIDE);

    docs.push_back(std::move(d));
    activeDoc = (int)docs.size() - 1;
    Layout();
    ShowActiveEditor();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return docs.back().get();
}

void App::ShowActiveEditor() {
    for (size_t i = 0; i < docs.size(); ++i) {
        HWND h = docs[i]->ed->Hwnd();
        if ((int)i == activeDoc) {
            SetWindowPos(h, HWND_TOP, rcEdit_.left, rcEdit_.top,
                         rcEdit_.right - rcEdit_.left, rcEdit_.bottom - rcEdit_.top,
                         SWP_SHOWWINDOW);
            SetFocus(h);
        } else {
            ShowWindow(h, SW_HIDE);
        }
    }
    ws.settings.lastFile = Active() ? Active()->path : L"";
    LoadTestToEditors(selTest);
    for (auto &d : docs) if (d->ed) d->ed->ThemeScrollbars();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ActivateDoc(int index) {
    if (index < 0 || index >= (int)docs.size()) return;
    activeDoc = index;
    selTest = 0;
    ShowActiveEditor();
}

void App::CloseDoc(int index) {
    if (index < 0 || index >= (int)docs.size()) return;
    Doc *d = docs[index].get();
    if (d->ed->Modified()) {
        std::wstring msg = L"「" + d->title + L"」有未保存的修改，是否保存？";
        int r = MessageBoxW(hwnd_, msg.c_str(), L"Forest Code", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) SaveDoc(d);
    }
    DestroyWindow(d->ed->Hwnd());
    docs.erase(docs.begin() + index);
    if (docs.empty()) activeDoc = -1;
    else if (activeDoc >= (int)docs.size()) activeDoc = (int)docs.size() - 1;
    else if (activeDoc > index) --activeDoc;
    Layout();
    ShowActiveEditor();
}

bool App::SaveDoc(Doc *d) {
    if (!d || !d->ed) return false;
    if (d->statement) return true;
    if (!d->ed->Save(d->path)) {
        MessageBoxW(hwnd_, L"保存失败，请检查文件是否被占用。", L"Forest Code", MB_OK | MB_ICONWARNING);
        return false;
    }
    InvalidateRect(hwnd_, &rcTab_, FALSE);
    SetStatus(L"已保存 " + FileName(d->path));
    return true;
}

bool App::SaveActive() { return SaveDoc(Active()); }

void App::CmdSaveAll() {
    for (auto &d : docs) if (d->ed->Modified()) SaveDoc(d.get());
    ws.settings.Save();
}

// ======================================================================
//  状态 / 诊断
// ======================================================================
void App::SetStatus(const std::wstring &s) {
    statusText = s;
    InvalidateRect(hwnd_, &rcStatus_, FALSE);
}

std::wstring App::ExePathFor(const std::wstring &src) const {
    return JoinPath(ws.BuildDir(), FileStem(src) + L".exe");
}

void App::RefreshDiagnostics() {
    if (!hDiag) return;
    diags = ParseDiagnostics(lastCompileLog);
    std::string text;
    if (lastCompileLog.empty()) {
        text = "还没有编译记录。按 F9 编译当前文件。\n";
    } else if (diags.empty() && lastCompileLog.find("error") == std::string::npos) {
        text = "编译通过，没有错误或警告。\n\n" + lastCompileLog;
    } else {
        text = lastCompileLog;
    }
    SetWindowTextW(hDiag, ToEdit(text).c_str());
    // 在编辑器上标出错误行
    if (Doc *d = Active()) {
        d->ed->ClearMarkers();
        for (auto &g : diags) {
            if (!g.isError) continue;
            if (FileName(g.file) == FileName(d->path)) d->ed->AddErrorMarker(g.line, g.message);
        }
    }
}

void App::RefreshRunPanel() {
    if (hAct) {
        std::string t;
        if (Doc *d = Active()) {
            Problem *p = ws.FindByFile(d->path);
            if (p && selTest >= 0 && selTest < (int)p->tests.size()) {
                TestCase &tc = p->tests[selTest];
                t = tc.actual;
                if (!tc.stderrText.empty()) t += "\n[stderr]\n" + tc.stderrText;
            } else {
                t = lastOutput;
            }
        } else t = lastOutput;
        SetWindowTextW(hAct, ToEdit(t).c_str());
    }
    if (hOut) {
        std::string t = lastOutput;
        if (lastOutput.empty() && lastCompileLog.empty()) t = "还没有运行记录。按 F11 编译并运行。\n";
        SetWindowTextW(hOut, ToEdit(t).c_str());
    }
    InvalidateRect(hwnd_, &rcTestList_, FALSE);
}

// ======================================================================
//  运行
// ======================================================================
void App::StartJob(bool compile, bool run, const std::string &stdinText, int token) {
    Doc *d = Active();
    if (!d) { SetStatus(L"没有打开的文件"); return; }
    if (d->statement) { SetStatus(L"题面文件不能编译"); return; }
    if (d->ed->Modified() && ws.settings.autoSave) SaveDoc(d);

    jobRunning = true;
    jobToken = token;
    if (compile) SetStatus(run ? L"正在编译并运行…" : L"正在编译…");
    else SetStatus(L"正在运行…");
    InvalidateRect(hwnd_, &rcStatus_, FALSE);
    RebuildButtons();

    std::wstring exe = ExePathFor(d->path);
    runner.Configure(ws.settings.compiler, ws.settings.stdFlag, ws.settings.compileFlags,
                     ws.settings.timeLimitMs);
    runner.Start(hwnd_, d->path, exe, stdinText, token, run);
}

void App::OnJobDone(RunResult *r) {
    std::unique_ptr<RunResult> res(r);
    jobRunning = false;

    if (res->compileRan) {
        lastCompileLog = res->compileLog;
        lastCompileMs = (int)res->compileMs;
        RefreshDiagnostics();
    }

    // 全部用例：先编译
    if (res->token == 101) {
        if (!res->compileOk) { runAllIndex = -1; RebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        Doc *d = Active();
        Problem *p = d ? ws.FindByFile(d->path) : nullptr;
        if (p && !p->tests.empty()) {
            runAllIndex = 0;
            runner.RunOnly(hwnd_, ExePathFor(d->path), p->tests[0].inText, d->path, 100);
            SetStatus(FormatW(L"测试 1 / %d …", (int)p->tests.size()));
        } else {
            runAllIndex = -1;
            jobRunning = false;
        }
        RebuildButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 全部用例模式
    if (res->token == 100) {
        Doc *d = Active();
        Problem *p = d ? ws.FindByFile(d->path) : nullptr;
        if (p && runAllIndex >= 0 && runAllIndex < (int)p->tests.size()) {
            TestCase &tc = p->tests[runAllIndex];
            tc.hasResult = true;
            tc.actual = res->out;
            tc.stderrText = res->err;
            tc.timeout = res->timeout;
            tc.exitCode = res->exitCode;
            tc.ms = res->runMs;
            tc.passed = false;
            if (!res->timeout) {
                if (tc.hasExpected) {
                    auto norm = [](std::string s) {
                        std::string o;
                        for (char c : s) if (c != '\r') o += c;
                        while (!o.empty() && (o.back() == '\n' || o.back() == ' ')) o.pop_back();
                        return o;
                    };
                    tc.passed = (norm(tc.actual) == norm(tc.outText));
                } else {
                    tc.passed = (res->exitCode == 0);
                }
            }
            selTest = runAllIndex;
            LoadTestToEditors(selTest);
            RefreshRunPanel();
        }
        int next = runAllIndex + 1;
        if (p && next < (int)p->tests.size()) {
            runAllIndex = next;
            runner.RunOnly(hwnd_, ExePathFor(d->path), p->tests[next].inText, d->path, 100);
            SetStatus(FormatW(L"测试 %d / %d …", next + 1, (int)p->tests.size()));
            return;
        }
        // 汇总
        int pass = 0, total = p ? (int)p->tests.size() : 0;
        if (p) for (auto &t : p->tests) if (t.hasResult && t.passed) ++pass;
        runAllIndex = -1;
        SetStatus(FormatW(L"全部用例完成：%d / %d 通过", pass, total));
        InvalidateRect(hwnd_, nullptr, FALSE);
        RebuildButtons();
        return;
    }

    // 编译失败
    if (res->compileRan && !res->compileOk) {
        SetStatus(FormatW(L"编译失败 (%d ms)", (int)res->compileMs));
        bottomPage = BottomPage::Diagnostics;
        UpdateTestEditorsVisibility();
        InvalidateRect(hwnd_, nullptr, FALSE);
        RebuildButtons();
        return;
    }

    if (res->ran) {
        lastOutput = res->out;
        Doc *d = Active();
        Problem *p = d ? ws.FindByFile(d->path) : nullptr;
        if (p && selTest >= 0 && selTest < (int)p->tests.size()) {
            TestCase &tc = p->tests[selTest];
            tc.hasResult = true;
            tc.actual = res->out;
            tc.stderrText = res->err;
            tc.timeout = res->timeout;
            tc.exitCode = res->exitCode;
            tc.ms = res->runMs;
            tc.passed = false;
            if (!res->timeout) {
                if (tc.hasExpected) {
                    auto norm = [](std::string s) {
                        std::string o;
                        for (char c : s) if (c != '\r') o += c;
                        while (!o.empty() && (o.back() == '\n' || o.back() == ' ')) o.pop_back();
                        return o;
                    };
                    tc.passed = (norm(tc.actual) == norm(tc.outText));
                } else tc.passed = (res->exitCode == 0);
            }
        }
        std::wstring s;
        if (res->timeout) s = L"运行超时";
        else if (res->exitCode != 0) s = FormatW(L"运行结束，返回码 %d", res->exitCode);
        else s = L"运行完成";
        s += FormatW(L"，用时 %.0f ms", res->runMs);
        if (p && selTest >= 0 && selTest < (int)p->tests.size() && p->tests[selTest].hasExpected)
            s += p->tests[selTest].passed ? L" · 通过 ✓" : L" · 答案错误 ✗";
        SetStatus(s);
        bottomPage = BottomPage::Tests;
        UpdateTestEditorsVisibility();
        RefreshRunPanel();
    } else if (res->compileRan) {
        SetStatus(FormatW(L"编译通过 (%d ms)", (int)res->compileMs));
    }

    RebuildButtons();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ======================================================================
//  测试用例面板
// ======================================================================
void App::LoadTestToEditors(int index) {
    if (!hIn) return;
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (!p || index < 0 || index >= (int)p->tests.size()) {
        SetWindowTextW(hIn, L"");
        SetWindowTextW(hExp, L"");
        SetWindowTextW(hAct, L"");
        return;
    }
    TestCase &tc = p->tests[index];
    ReadFileUtf8(tc.inPath, tc.inText);
    if (tc.hasExpected) ReadFileUtf8(tc.outPath, tc.outText);
    SetWindowTextW(hIn, ToEdit(tc.inText).c_str());
    SetWindowTextW(hExp, ToEdit(tc.outText).c_str());
    SetWindowTextW(hAct, ToEdit(tc.actual).c_str());
}

void App::SaveEditorsToTest() {
    if (!hIn) return;
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (!p || selTest < 0 || selTest >= (int)p->tests.size()) return;
    TestCase &tc = p->tests[selTest];

    auto getText = [](HWND h) {
        int n = GetWindowTextLengthW(h);
        std::wstring w((size_t)n, L'\0');
        if (n) GetWindowTextW(h, &w[0], n + 1);
        return FromEdit(w);
    };
    tc.inText = getText(hIn);
    tc.outText = getText(hExp);
    WriteFileUtf8(tc.inPath, tc.inText);
    WriteFileUtf8(tc.outPath, tc.outText);
    tc.hasExpected = !tc.outText.empty();
}

void App::UpdateTestEditorsVisibility() {
    bool tests = bottomVisible && bottomPage == BottomPage::Tests;
    int cmd = tests ? SW_SHOW : SW_HIDE;
    if (hIn)  ShowWindow(hIn, cmd);
    if (hExp) ShowWindow(hExp, cmd);
    if (hAct) ShowWindow(hAct, cmd);
    if (hOut) ShowWindow(hOut, (bottomVisible && bottomPage == BottomPage::Output) ? SW_SHOW : SW_HIDE);
    if (hDiag) ShowWindow(hDiag, (bottomVisible && bottomPage == BottomPage::Diagnostics) ? SW_SHOW : SW_HIDE);
}

void App::CmdAddTest() {
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (!p) { SetStatus(L"请先打开某个题目下的解法文件"); return; }
    SaveEditorsToTest();
    ws.AddTest(p);
    selTest = (int)p->tests.size() - 1;
    RebuildSidebar();
    LoadTestToEditors(selTest);
    InvalidateRect(hwnd_, nullptr, FALSE);
    SetStatus(L"已新增测试用例 " + std::to_wstring(selTest + 1));
}

void App::CmdDeleteTest() {
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (!p || selTest < 0 || selTest >= (int)p->tests.size()) return;
    if (MessageBoxW(hwnd_, FormatW(L"删除用例 %d ？", selTest + 1).c_str(), L"Forest Code",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    ws.DeleteTest(p, selTest);
    if (selTest >= (int)p->tests.size()) selTest = (int)p->tests.size() - 1;
    if (selTest < 0) selTest = 0;
    RebuildSidebar();
    LoadTestToEditors(selTest);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ======================================================================
//  命令
// ======================================================================
void App::CmdCompile() {
    if (jobRunning) { SetStatus(L"正在忙…"); return; }
    StartJob(true, false, "", 1);
}

void App::CmdRun() {
    if (jobRunning) { SetStatus(L"正在忙…"); return; }
    SaveEditorsToTest();
    std::wstring stdinText;
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (p && selTest >= 0 && selTest < (int)p->tests.size()) stdinText = U2W(p->tests[selTest].inText);
    std::wstring exe = ExePathFor(d ? d->path : L"");
    if (!PathExists(exe)) { CmdCompileRun(); return; }
    jobRunning = true; jobToken = 2;
    SetStatus(L"正在运行…");
    RebuildButtons();
    runner.RunOnly(hwnd_, exe, W2U(stdinText), d->path, 2);
}

void App::CmdCompileRun() {
    if (jobRunning) { SetStatus(L"正在忙…"); return; }
    SaveEditorsToTest();
    std::wstring stdinText;
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (p && selTest >= 0 && selTest < (int)p->tests.size()) stdinText = U2W(p->tests[selTest].inText);
    StartJob(true, true, W2U(stdinText), 3);
}

void App::CmdRunAllTests() {
    if (jobRunning) return;
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (!p || p->tests.empty()) { SetStatus(L"当前题目没有测试用例"); return; }
    SaveEditorsToTest();
    for (auto &t : p->tests) { t.hasResult = false; t.actual.clear(); }
    runAllIndex = 0;
    jobRunning = true; jobToken = 100;
    SetStatus(L"正在编译…");
    RebuildButtons();
    // 先编译，成功后逐个跑
    StartJob(true, false, "", 101);
}

void App::CmdNewProblem() {
    ShowNewProblemDialog();
}

void App::CmdNewSolution() {
    Doc *d = Active();
    Problem *p = d ? ws.FindByFile(d->path) : nullptr;
    if (!p) { SetStatus(L"请先选中一个题目"); return; }

    std::vector<FormField> fields;
    FormField f1; f1.label = L"解法名称"; f1.value = FormatW(L"解法 %d", (int)p->solutions.size() + 1); fields.push_back(f1);
    FormField f2; f2.label = L"代码模板"; f2.value = U2W(ws.DefaultTemplate()); f2.multiline = true; f2.height = 150; fields.push_back(f2);
    if (!ShowFormDialog(hwnd_, L"新建解法", fields)) return;
    if (fields[0].value.empty()) return;
    ws.CreateSolution(p, fields[0].value, fields[1].value);
    RebuildSidebar();
    InvalidateRect(hwnd_, nullptr, FALSE);
    // 打开新解法
    for (auto &s : p->solutions) OpenFile(s.path);
    SetStatus(L"已新建解法 " + fields[0].value);
}

void App::CmdSettings() { ShowSettingsDialog(); }

void App::CmdOpenWorkspace() {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"选择 Forest Code 工作区目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (!idl) return;
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(idl, path);
    CoTaskMemFree(idl);
    if (!path[0]) return;
    ws.settings.workspace = path;
    ws.settings.Save();
    ws.LoadAll();
    RebuildSidebar();
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetStatus(L"工作区已切换到 " + std::wstring(path));
}

void App::CmdZoom(int delta) {
    if (Doc *d = Active()) {
        d->ed->Zoom(delta);
        SetStatus(FormatW(L"字号缩放 %+d", delta));
    }
}

void App::CmdShowWhitespace(bool on) {
    ws.settings.showWhitespace = on;
    for (auto &d : docs) {
        SendMessageW(d->ed->Hwnd(), 2021 /*SCI_SETVIEWWS*/, on ? 1 : 0, 0);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::CmdCopyOutput() {
    if (Doc *d = Active()) {
        Problem *p = ws.FindByFile(d->path);
        if (p && selTest >= 0 && selTest < (int)p->tests.size()) {
            CopyTextToClipboard(hwnd_, U2W(p->tests[selTest].actual));
            SetStatus(L"已复制实际输出");
        }
    }
}

void App::CmdTogglePanel() {
    bottomVisible = !bottomVisible;
    Layout();
    SetStatus(bottomVisible ? L"已展开面板" : L"已收起面板");
}

void App::CmdAbout() {
    MessageBoxW(hwnd_,
        L"Forest Code · 竞赛工作台\n\n"
        L"一个为算法竞赛选手手写的超轻量 C++ IDE。\n"
        L"· 原生 Win32，启动快、占用低\n"
        L"· 题目 / 一题多解 / 测试用例一体化管理\n"
        L"· 一键编译运行、多用例对拍\n\n"
        L"编译内核：Scintilla + Lexilla\n"
        L"Made for competitive programming.",
        L"关于 Forest Code", MB_OK | MB_ICONINFORMATION);
}

// ======================================================================
//  输入
// ======================================================================
static bool Hit(const RECT &r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

void App::OnMouseMove(POINT p) {
    // 拖动分隔条
    if (resizing_) {
        int dpi = (int)g_theme.Dpi();
        if (dragSplitSide_ == 1) {
            int logical = (p.x - rcSide_.left) * 96 / dpi;
            sideWidth = std::max(170, std::min(logical, 520));
            Layout();
        } else if (dragSplitSide_ == 2) {
            int logical = (rcBottom_.bottom - p.y) * 96 / dpi;
            bottomHeight = std::max(120, std::min(logical, 620));
            Layout();
        }
        return;
    }

    int hotBtn = -1;
    for (auto &b : buttons) {
        if (!b.visible || !b.enabled) continue;
        if (Hit(b.rc, p)) { hotBtn = b.id; break; }
    }

    int hotTab = -1, hotClose = -1;
    if (Hit(rcTab_, p)) {
        int x = rcTab_.left + g_theme.S(6);
        HDC dc = GetDC(hwnd_);
        SelectObject(dc, g_theme.Ui());
        for (size_t i = 0; i < docs.size(); ++i) {
            int tw = TextWidth(dc, docs[i]->title, g_theme.Ui()) + g_theme.S(46);
            if (tw > g_theme.S(210)) tw = g_theme.S(210);
            RECT r{ x, rcTab_.top + g_theme.S(5), x + tw, rcTab_.bottom };
            if (Hit(r, p)) {
                hotTab = (int)i;
                RECT cr{ r.right - g_theme.S(22), r.top, r.right - g_theme.S(6), r.bottom };
                if (Hit(cr, p)) hotClose = (int)i;
            }
            x += tw + g_theme.S(4);
        }
        ReleaseDC(hwnd_, dc);
    }

    int hotSide = -1;
    if (sideVisible && Hit(rcSide_, p) && p.y > rcSideHead_.bottom) {
        int rowH = g_theme.S(28);
        int idx = (p.y - rcSideHead_.bottom - g_theme.S(4) + sideScroll) / rowH;
        if (idx >= 0 && idx < (int)sideItems.size()) hotSide = idx;
    }

    int hotBottom = -1;
    for (auto &b : buttons) {
        if (b.id >= 910 && b.id <= 912 && Hit(b.rc, p)) hotBottom = b.id - 910;
    }

    int hotTest = -2;
    if (bottomVisible && bottomPage == BottomPage::Tests && Hit(rcTestList_, p)) {
        int rowH = g_theme.S(34);
        Doc *d = Active();
        Problem *pp = d ? ws.FindByFile(d->path) : nullptr;
        int count = pp ? (int)pp->tests.size() : 0;
        int idx = (p.y - rcTestList_.top - g_theme.S(6) + testScroll) / rowH;
        if (idx >= 0 && idx < count) hotTest = idx;
        else if (idx == count) hotTest = -1;   // 新增按钮
        else hotTest = -2;
    }

    // 分隔条光标
    int newSplit = 0;
    int dSide = std::abs(p.x - g_theme.S(sideWidth));
    if (sideVisible && dSide < g_theme.S(5) && p.y > rcSide_.top && p.y < rcSide_.bottom) newSplit = 1;
    if (bottomVisible && std::abs(p.y - rcBottom_.top) < g_theme.S(5) && p.x > rcBottom_.left) newSplit = 2;

    if (hotBtn != hotButton_ || hotTab != hotDocTab || hotClose != hotDocClose ||
        hotSide != hotSideItem_ || hotBottom != hotBottomTab || hotTest != hotTestRow ||
        newSplit != dragSplitSide_) {
        hotButton_ = hotBtn; hotDocTab = hotTab; hotDocClose = hotClose;
        hotSideItem_ = hotSide; hotBottomTab = hotBottom; hotTestRow = hotTest;
        if (!resizing_ && !chromeDragging_) dragSplitSide_ = newSplit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (dragSplitSide_ == 1) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
    else if (dragSplitSide_ == 2) SetCursor(LoadCursor(nullptr, IDC_SIZENS));
}

void App::OnMouseLeave() {
    hotButton_ = -1; hotDocTab = -1; hotDocClose = -1;
    hotSideItem_ = -1; hotBottomTab = -1; hotTestRow = -2;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnLButtonDown(POINT p) {
    // 窗口按钮
    for (auto &b : buttons) {
        if (!b.visible) continue;
        if (Hit(b.rc, p)) {
            if (b.id >= 900) {
                if (b.id == 901) ShowWindow(hwnd_, SW_MINIMIZE);
                else if (b.id == 902) ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                else if (b.id == 903) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return;
            }
            pressedButton_ = b.id;
            b.pressed = true;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 分隔条
    if (dragSplitSide_) {
        resizing_ = true;
        resizeStartPt_ = p;
        GetWindowRect(hwnd_, &resizeStartRect_);
        SetCapture(hwnd_);
        return;
    }

    // 标签页
    if (Hit(rcTab_, p)) {
        if (hotDocClose >= 0) { CloseDoc(hotDocClose); return; }
        if (hotDocTab >= 0) { ActivateDoc(hotDocTab); return; }
        return;
    }

    // 底部标签
    for (auto &b : buttons) {
        if (b.id >= 910 && b.id <= 912 && Hit(b.rc, p)) {
            SaveEditorsToTest();
            bottomPage = (BottomPage)(b.id - 910);
            UpdateTestEditorsVisibility();
            if (bottomPage == BottomPage::Tests) LoadTestToEditors(selTest);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 测试列表
    if (bottomVisible && bottomPage == BottomPage::Tests && Hit(rcTestList_, p)) {
        if (hotTestRow == -1) { CmdAddTest(); return; }
        if (hotTestRow >= 0) {
            SaveEditorsToTest();
            selTest = hotTestRow;
            LoadTestToEditors(selTest);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 侧栏
    if (sideVisible && Hit(rcSide_, p) && p.y > rcSideHead_.bottom && hotSideItem_ >= 0) {
        selSideItem_ = hotSideItem_;
        SideItem it = sideItems[hotSideItem_];
        if (it.canExpand) {
            if (it.kind == SideItem::Kind::Problem && it.problem >= 0)
                ws.problems[it.problem].expanded = !ws.problems[it.problem].expanded;
            else if (it.kind == SideItem::Kind::Tests && it.problem >= 0)
                ws.problems[it.problem].testsExpanded = !ws.problems[it.problem].testsExpanded;
            RebuildSidebar();
        } else {
            switch (it.kind) {
            case SideItem::Kind::Statement: OpenFile(it.path, true); break;
            case SideItem::Kind::Solution: OpenFile(it.path); break;
            case SideItem::Kind::NewSolution: {
                CmdNewSolution();
                break;
            }
            case SideItem::Kind::TestCase:
                selTest = it.test;
                LoadTestToEditors(selTest);
                bottomPage = BottomPage::Tests;
                bottomVisible = true;
                UpdateTestEditorsVisibility();
                Layout();
                break;
            default: break;
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void App::OnLButtonUp(POINT p) {
    if (pressedButton_ >= 0) {
        int id = pressedButton_;
        pressedButton_ = -1;
        for (auto &b : buttons) b.pressed = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        bool stillHot = false;
        for (auto &b : buttons) if (b.id == id && Hit(b.rc, p)) stillHot = true;
        if (stillHot) {
            switch (id) {
            case 1: CmdCompile(); break;
            case 2: CmdRun(); break;
            case 3: CmdCompileRun(); break;
            case 4: CmdNewProblem(); break;
            case 5: CmdNewSolution(); break;
            case 6: CmdSettings(); break;
            case 7: CmdAbout(); break;
            case 8: CmdRunAllTests(); break;
            case 20: CmdNewProblem(); break;
            case 21: ws.RescanProblems(); RebuildSidebar(); SetStatus(L"已刷新"); break;
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (resizing_) {
        resizing_ = false;
        dragSplitSide_ = 0;
        if (GetCapture() == hwnd_) ReleaseCapture();
        return;
    }
}

void App::OnKeyDown(WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (key) {
    case VK_F9:  CmdCompile(); break;
    case VK_F5:  CmdRun(); break;
    case VK_F11: CmdCompileRun(); break;
    case VK_F6:  CmdRunAllTests(); break;
    case VK_F1:  CmdAbout(); break;
    case 'S': if (ctrl && shift) CmdSaveAll(); else if (ctrl) SaveActive(); break;
    case 'N': if (ctrl) CmdNewProblem(); break;
    case 'B': if (ctrl) CmdTogglePanel(); break;
    case 'T': if (ctrl && shift) CmdAddTest(); break;
    case VK_OEM_PLUS:  if (ctrl) CmdZoom(1); break;
    case VK_OEM_MINUS: if (ctrl) CmdZoom(-1); break;
    case VK_ADD: if (ctrl) CmdZoom(1); break;
    case VK_SUBTRACT: if (ctrl) CmdZoom(-1); break;
    case VK_TAB:
        if (ctrl && !docs.empty()) ActivateDoc((activeDoc + 1) % (int)docs.size());
        break;
    case VK_ESCAPE:
        if (bottomVisible) CmdTogglePanel();
        break;
    }
}

void App::OnSize() {
    Layout();
    UpdateTestEditorsVisibility();
}

} // namespace fc

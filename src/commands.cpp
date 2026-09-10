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
// snippets.ini 是逐行解析的，正文里的换行只能写成 \n —— 这里还原转义
static std::string UnescapeSnippet(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            if (c == 'n') o += '\n';
            else if (c == 't') o += '\t';
            else if (c == '\\') o += '\\';
            else { o += '\\'; o += c; }
        } else {
            o += s[i];
        }
    }
    return o;
}

static std::wstring SnippetsPath() { return JoinPath(AppDataDir(), L"snippets.ini"); }

void App::BuildSnippets() {
    std::vector<Snippet> sn = Workspace::BuiltinSnippets();
    // 追加用户片段
    Ini ini;
    if (ini.Load(SnippetsPath())) {
        for (int i = 1; i <= 200; ++i) {
            std::string key = Format("snippet%d", i);
            std::string trig = ini.Get(key + ".trigger");
            if (trig.empty()) continue;
            Snippet s;
            s.trigger = trig;
            s.body = UnescapeSnippet(ini.Get(key + ".body"));
            s.desc = ini.Get(key + ".desc");
            s.builtin = false;
            sn.push_back(s);
        }
    }
    for (auto &d : docs) if (d->ed) d->ed->SetSnippets(sn);
    snippets_ = sn;
}

// 片段以前只能手改 ini，没有任何入口。给出模板并直接在编辑器里打开。
void App::CmdEditSnippets() {
    std::wstring path = SnippetsPath();
    if (!PathExists(path)) {
        WriteFileUtf8(path,
            "# Forest Code 自定义代码片段\n"
            "# 每条片段三行：trigger（触发词）/ body（展开内容）/ desc（说明）\n"
            "# body 里的换行写成 \\n，缩进写成 \\t；$0 是展开后光标停的位置。\n"
            "# 保存后立刻生效：在编辑器里敲触发词，从补全列表里选中，或按 Tab 展开。\n"
            "#\n"
            "# 例：\n"
            "# snippet1.trigger=mint\n"
            "# snippet1.body=const long long MOD = 998244353;\\n\n"
            "# snippet1.desc=模数\n");
    }
    OpenFile(path);
    SetStatus(L"编辑后保存即可生效");
}

// ======================================================================
//  命令面板
// ======================================================================
void App::CmdQuickOpen() {
    std::wstring path;
    int line = 0;
    if (!ShowPalette(hwnd_, PaletteMode::QuickOpen, ws.settings.workspace, path, line)) return;
    if (!PathExists(path)) { SetStatus(L"文件已经不在了"); RebuildFileTree(); return; }
    if (IsDir(path)) { SetStatus(L"那是个文件夹"); return; }
    if (OpenFile(path)) SetStatus(L"已打开 " + FileName(path));
}

void App::CmdFindInFiles() {
    std::wstring path;
    int line = 0;
    if (!ShowPalette(hwnd_, PaletteMode::Search, ws.settings.workspace, path, line)) return;
    if (!PathExists(path)) { SetStatus(L"文件已经不在了"); RebuildFileTree(); return; }
    Doc *d = OpenFile(path);
    if (d && line > 0) {
        d->ed->GotoLine(line, true);
        SetStatus(FormatW(L"%s:%d", FileName(path).c_str(), line));
    } else {
        SetStatus(L"已打开 " + FileName(path));
    }
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
    d->ed->SetLang(LangForPath(path));
    d->ed->SetViewWhitespace(ws.settings.showWhitespace);
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
    ScanTestsForActive();
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
    if (d->path == SnippetsPath()) {   // 改完片段立刻生效，不用重启
        BuildSnippets();
        SetStatus(L"代码片段已重新载入");
        return true;
    }
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

// 编译产物名必须把完整源路径算进去。
// 约定式模型下每个题目里的代码都叫 代码01.cpp，只按文件名命名的话所有题共用
// .build\代码01.exe —— 在 A 题编译过之后切到 B 题按 F5，跑的是 A 的程序配 B 的输入，
// 判出来的 AC/WA 全是假的。
static std::wstring ExeNameFor(const std::wstring &src) {
    std::wstring lower = src;
    for (auto &ch : lower) ch = (wchar_t)towlower(ch);
    unsigned int h = 2166136261u;                      // FNV-1a 32
    for (wchar_t ch : lower) {
        h = (h ^ (unsigned int)(ch & 0xFF)) * 16777619u;
        h = (h ^ (unsigned int)((ch >> 8) & 0xFF)) * 16777619u;
    }
    wchar_t buf[16]{};
    swprintf(buf, 16, L"%08x", h);
    std::wstring stem = FileStem(src);
    if (stem.empty()) stem = L"a";
    return stem + L"_" + buf + L".exe";
}

// a 比 b 新（用于判断产物是否过期）
static bool FileNewer(const std::wstring &a, const std::wstring &b) {
    WIN32_FILE_ATTRIBUTE_DATA fa{}, fb{};
    if (!GetFileAttributesExW(a.c_str(), GetFileExInfoStandard, &fa)) return false;
    if (!GetFileAttributesExW(b.c_str(), GetFileExInfoStandard, &fb)) return true;
    return CompareFileTime(&fa.ftLastWriteTime, &fb.ftLastWriteTime) > 0;
}

// 输出比对：统一换行、忽略行尾空白
static std::string NormOut(std::string s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) if (c != '\r') o += c;
    while (!o.empty() && (o.back() == '\n' || o.back() == ' ')) o.pop_back();
    return o;
}

// 期望输出：用例文件太大时正文不在内存里，比对时再从磁盘读一次
static std::string ExpectedText(const TestCase &tc) {
    if (!tc.outTooBig) return NormOut(tc.outText);
    std::string s;
    ReadFileUtf8(tc.outPath, s);
    return NormOut(s);
}

// 往 EDIT 控件里塞文本前先截断：几 MB 会直接卡死界面
static std::string ClipForEdit(const std::string &s, size_t limit = 200 * 1024) {
    if (s.size() <= limit) return s;
    std::string o = s.substr(0, limit);
    o += Format("\n\n[... 共 %.1f KB，只显示前 %d KB ...]\n",
                s.size() / 1024.0, (int)(limit / 1024));
    return o;
}

static void SetEditRO(HWND h, bool ro) {
    if (h) SendMessageW(h, EM_SETREADONLY, ro ? TRUE : FALSE, 0);
}

std::wstring App::ExePathFor(const std::wstring &src) const {
    if (src.empty()) return JoinPath(ws.BuildDir(), L"a.exe");
    return JoinPath(ws.BuildDir(), ExeNameFor(src));
}

void App::RefreshDiagnostics() {
    if (!hDiag) return;
    diags = ParseDiagnostics(lastCompileLog);
    lastErrCount = lastWarnCount = 0;
    for (auto &g : diags) {
        if (g.isError) ++lastErrCount;
        else ++lastWarnCount;
    }
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

// 编译结果 + 警告数；有编译记录时提示可以点状态栏看详情
std::wstring App::CompileSummary(bool ok, int ms) const {
    std::wstring s;
    if (!ok) {
        s = FormatW(L"编译失败：%d 个错误", lastErrCount);
        if (lastWarnCount) s += FormatW(L"，%d 条警告", lastWarnCount);
        if (lastErrCount == 0) s = L"编译失败";
    } else {
        s = FormatW(L"编译通过 (%d ms)", ms);
        if (lastWarnCount) s += FormatW(L" · %d 条警告", lastWarnCount);
    }
    if (!lastCompileLog.empty()) s += L" · 点击查看编译信息";
    return s;
}

// 「答案错误」定位：找出期望输出与实际输出第一个不同的地方
void App::UpdateDiff() {
    diffValid = false;
    diffLine = 0;
    diffExpLine.clear();
    diffActLine.clear();
    if (selTest < 0 || selTest >= (int)curTests.size()) return;
    const TestCase &tc = curTests[selTest];
    if (!tc.hasResult || tc.passed || !tc.hasExpected || tc.timeout) return;

    std::string exp = ExpectedText(tc);
    std::string act = NormOut(tc.actual);

    auto lines = [](const std::string &s) {
        std::vector<std::string> v;
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t e = s.find('\n', pos);
            v.push_back(s.substr(pos, (e == std::string::npos) ? std::string::npos : e - pos));
            if (e == std::string::npos) break;
            pos = e + 1;
        }
        return v;
    };
    std::vector<std::string> a = lines(exp), b = lines(act);

    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        std::string x = i < a.size() ? a[i] : std::string();
        std::string y = i < b.size() ? b[i] : std::string();
        if (i < a.size() && i < b.size() && x == y) continue;
        diffValid = true;
        diffLine = (int)i + 1;
        diffExpLine = (i < a.size()) ? x : std::string();
        diffActLine = (i < b.size()) ? y : std::string();
        break;
    }
}

std::wstring App::DiffHint() const {
    if (!diffValid) return L"";
    auto clip = [](std::string s) {
        if (s.size() > 40) s = s.substr(0, 40) + "...";
        return U2W(s);
    };
    if (diffExpLine.empty() && !diffActLine.empty())
        return FormatW(L"第 %d 行：期望没有这一行，实际是 \"%s\"", diffLine, clip(diffActLine).c_str());
    if (!diffExpLine.empty() && diffActLine.empty())
        return FormatW(L"第 %d 行：期望 \"%s\"，实际没有这一行", diffLine, clip(diffExpLine).c_str());
    return FormatW(L"第 %d 行不同：期望 \"%s\"，实际 \"%s\"",
                   diffLine, clip(diffExpLine).c_str(), clip(diffActLine).c_str());
}

// 把两个输出框都定位到第一个不同的那一行
void App::PointAtDiff() {
    if (!diffValid || diffLine <= 0) return;
    for (HWND h : { hExp, hAct }) {
        if (!h) continue;
        int li = (int)SendMessageW(h, EM_LINEINDEX, (WPARAM)(diffLine - 1), 0);
        if (li < 0) continue;
        int len = (int)SendMessageW(h, EM_LINELENGTH, (WPARAM)li, 0);
        if (len < 0) len = 0;
        SendMessageW(h, EM_SETSEL, (WPARAM)li, (LPARAM)(li + len));
        SendMessageW(h, EM_SCROLLCARET, 0, 0);
    }
}

void App::RefreshRunPanel() {
    if (hAct) {
        std::string t;
        if (Active()) {
            if (selTest >= 0 && selTest < (int)curTests.size()) {
                TestCase &tc = curTests[selTest];
                t = ClipForEdit(tc.actual);
                if (!tc.stderrText.empty()) t += "\n[stderr]\n" + ClipForEdit(tc.stderrText, 32 * 1024);
            } else {
                t = ClipForEdit(lastOutput);
            }
        } else t = lastOutput;
        SetWindowTextW(hAct, ToEdit(t).c_str());
    }
    if (hOut) {
        std::string t = lastOutput;
        if (lastOutput.empty() && lastCompileLog.empty()) t = "还没有运行记录。按 F11 编译并运行。\n";
        SetWindowTextW(hOut, ToEdit(ClipForEdit(t)).c_str());
    }
    InvalidateRect(hwnd_, &rcTestList_, FALSE);
    UpdateDiff();
    PointAtDiff();
}

// ======================================================================
//  运行
// ======================================================================
void App::StartJob(bool compile, bool run, const std::wstring &stdinFile, int token) {
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
    if (!runner.Start(hwnd_, d->path, exe, stdinFile, "", token, run)) {
        jobRunning = false;
        SetStatus(L"运行器忙，请稍后再试");
        RebuildButtons();
    }
}

void App::OnJobDone(RunResult *r) {
    std::unique_ptr<RunResult> res(r);
    jobRunning = false;

    if (res->compileRan) {
        lastCompileLog = res->compileLog;
        lastCompileMs = (int)res->compileMs;
        RefreshDiagnostics();
    }

    // 全部用例：先编译（token 101），成功后逐个跑（token 100）。
    // 后续每一步都用编译结果里带回来的源文件路径，不再依赖"当前活动文档"——
    // 运行途中关掉标签页也不会空指针或跑错文件。
    if (res->token == 101) {
        runAllIndex = -1;
        if (!res->compileOk || curTests.empty()) {
            if (res->compileOk) SetStatus(L"当前目录没有 .in/.out 测试用例");
            RebuildButtons();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        runAllSrc = res->sourceFile;
        runAllIndex = 0;
        SetStatus(FormatW(L"测试 1 / %d …", (int)curTests.size()));
        jobRunning = runner.RunOnly(hwnd_, ExePathFor(runAllSrc), curTests[0].inPath, "",
                                   runAllSrc, 100);
        if (!jobRunning) SetStatus(L"运行器忙，已中断");
        RebuildButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 全部用例模式
    if (res->token == 100) {
        if (runAllIndex >= 0 && runAllIndex < (int)curTests.size()) {
            TestCase &tc = curTests[runAllIndex];
            tc.hasResult = true;
            tc.actual = res->out;
            tc.stderrText = res->err;
            tc.timeout = res->timeout;
            tc.exitCode = res->exitCode;
            tc.ms = res->runMs;
            tc.passed = false;
            if (!res->timeout) {
                if (tc.hasExpected) {
                    tc.passed = (NormOut(tc.actual) == ExpectedText(tc));
                } else {
                    tc.passed = (res->exitCode == 0);
                }
            }
            selTest = runAllIndex;
            LoadTestToEditors(selTest);
            RefreshRunPanel();
        }
        int next = runAllIndex + 1;
        if (next < (int)curTests.size()) {
            runAllIndex = next;
            SetStatus(FormatW(L"测试 %d / %d …", next + 1, (int)curTests.size()));
            jobRunning = runner.RunOnly(hwnd_, ExePathFor(runAllSrc), curTests[next].inPath,
                                       "", runAllSrc, 100);
            if (!jobRunning) SetStatus(L"运行器忙，已中断");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        // 汇总
        int pass = 0, total = (int)curTests.size();
        for (auto &t : curTests) if (t.hasResult && t.passed) ++pass;
        runAllIndex = -1;
        if (pass == total) SetStatus(FormatW(L"全部用例完成：%d / %d 通过", pass, total));
        else SetStatus(FormatW(L"全部用例完成：%d / %d 通过，%d 个没过（点用例看差异）",
                               pass, total, total - pass));
        InvalidateRect(hwnd_, nullptr, FALSE);
        RebuildButtons();
        return;
    }

    // 编译失败
    if (res->compileRan && !res->compileOk) {
        SetStatus(CompileSummary(false, (int)res->compileMs));
        bottomPage = BottomPage::Diagnostics;
        UpdateTestEditorsVisibility();
        InvalidateRect(hwnd_, nullptr, FALSE);
        RebuildButtons();
        return;
    }

    if (res->ran) {
        lastOutput = res->out;
        if (selTest >= 0 && selTest < (int)curTests.size()) {
            TestCase &tc = curTests[selTest];
            tc.hasResult = true;
            tc.actual = res->out;
            tc.stderrText = res->err;
            tc.timeout = res->timeout;
            tc.exitCode = res->exitCode;
            tc.ms = res->runMs;
            tc.passed = false;
            if (!res->timeout) {
                if (tc.hasExpected) tc.passed = (NormOut(tc.actual) == ExpectedText(tc));
                else tc.passed = (res->exitCode == 0);
            }
        }
        std::wstring s;
        if (res->timeout) s = L"运行超时";
        else if (res->exitCode != 0) s = FormatW(L"运行结束，返回码 %d", res->exitCode);
        else s = L"运行完成";
        s += FormatW(L"，用时 %.0f ms", res->runMs);
        if (selTest >= 0 && selTest < (int)curTests.size() && curTests[selTest].hasExpected)
            s += curTests[selTest].passed ? L" · 通过 ✓" : L" · 答案错误 ✗";
        SetStatus(s);
        bottomPage = BottomPage::Tests;
        UpdateTestEditorsVisibility();
        RefreshRunPanel();
        // RefreshRunPanel 里会重算 diff，这里把定位结果补进状态栏
        if (diffValid) SetStatus(s + L" · " + DiffHint());
    } else if (res->compileRan) {
        SetStatus(CompileSummary(true, (int)res->compileMs));
    }

    RebuildButtons();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ======================================================================
//  测试用例面板
// ======================================================================
void App::LoadTestToEditors(int index) {
    if (!hIn) return;
    if (index < 0 || index >= (int)curTests.size()) {
        SetWindowTextW(hIn, L"");
        SetWindowTextW(hExp, L"");
        SetWindowTextW(hAct, L"");
        return;
    }
    TestCase &tc = curTests[index];
    if (tc.inTooBig) {
        tc.inText.clear();
        SetWindowTextW(hIn, FormatW(L"[%s 共 %.1f MB，太大，不在下面显示]\n[运行时会完整喂给程序]",
                                    FileName(tc.inPath).c_str(),
                                    FileSize(tc.inPath) / 1024.0 / 1024.0).c_str());
        SetEditRO(hIn, true);
    } else {
        ReadFileUtf8(tc.inPath, tc.inText);
        SetEditRO(hIn, false);
        SetWindowTextW(hIn, ToEdit(tc.inText).c_str());
    }
    if (tc.outTooBig) {
        tc.outText.clear();
        SetWindowTextW(hExp, FormatW(L"[%s 共 %.1f MB，太大，不在下面显示]",
                                     FileName(tc.outPath).c_str(),
                                     FileSize(tc.outPath) / 1024.0 / 1024.0).c_str());
        SetEditRO(hExp, true);
    } else {
        if (tc.hasExpected) ReadFileUtf8(tc.outPath, tc.outText);
        SetEditRO(hExp, false);
        SetWindowTextW(hExp, ToEdit(tc.outText).c_str());
    }
    SetWindowTextW(hAct, ToEdit(ClipForEdit(tc.actual)).c_str());
    UpdateDiff();
    PointAtDiff();
}

void App::SaveEditorsToTest() {
    if (!hIn) return;
    if (selTest < 0 || selTest >= (int)curTests.size()) return;
    TestCase &tc = curTests[selTest];

    auto getText = [](HWND h) {
        int n = GetWindowTextLengthW(h);
        std::wstring w((size_t)n, L'\0');
        if (n) GetWindowTextW(h, &w[0], n + 1);
        return FromEdit(w);
    };
    // 太大没载入的字段不能回写：输入框里放的是提示文字，写回去就把用例毁了
    if (tc.inTooBig) return;
    tc.inText = getText(hIn);
    WriteFileUtf8(tc.inPath, tc.inText);
    if (tc.outTooBig) return;
    tc.outText = getText(hExp);
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
    ScanTestsForActive();
    if (testDir_.empty()) { SetStatus(L"请先打开一个代码文件"); return; }
    SaveEditorsToTest();
    // 找最小空号，建 X.in / X.out 一对
    int n = 1;
    std::wstring inPath, outPath;
    for (;; ++n) {
        inPath = JoinPath(testDir_, std::to_wstring(n) + L".in");
        if (!PathExists(inPath)) break;
    }
    outPath = JoinPath(testDir_, std::to_wstring(n) + L".out");
    WriteFileUtf8(inPath, "");
    WriteFileUtf8(outPath, "");
    ScanTestsForActive();
    selTest = (int)curTests.size() - 1;
    RebuildFileTree();
    LoadTestToEditors(selTest);
    InvalidateRect(hwnd_, nullptr, FALSE);
    SetStatus(FormatW(L"已新增用例 %d.in / %d.out", n, n));
}

void App::CmdDeleteTest() {
    if (selTest < 0 || selTest >= (int)curTests.size()) return;
    if (MessageBoxW(hwnd_, FormatW(L"删除用例 %s ？", FileName(curTests[selTest].inPath).c_str()).c_str(),
                    L"Forest Code", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    DeleteFileSafe(curTests[selTest].inPath);
    DeleteFileSafe(curTests[selTest].outPath);
    ScanTestsForActive();
    if (selTest >= (int)curTests.size()) selTest = (int)curTests.size() - 1;
    if (selTest < 0) selTest = 0;
    RebuildFileTree();
    LoadTestToEditors(selTest);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ======================================================================
//  命令
// ======================================================================
void App::CmdCompile() {
    if (jobRunning) { SetStatus(L"正在忙…"); return; }
    StartJob(true, false, L"", 1);
}

void App::CmdRun() {
    if (jobRunning) { SetStatus(L"正在忙…"); return; }
    Doc *d = Active();
    if (!d) { SetStatus(L"没有打开的文件"); return; }
    if (d->statement) { SetStatus(L"题面文件不能运行"); return; }
    SaveEditorsToTest();
    std::wstring stdinFile;
    if (selTest >= 0 && selTest < (int)curTests.size()) stdinFile = curTests[selTest].inPath;
    std::wstring exe = ExePathFor(d->path);
    // 产物不存在、或者源码比产物还新 —— 直接跑等于跑上一版程序，必须重编
    if (!PathExists(exe) || FileNewer(d->path, exe) || d->ed->Modified()) { CmdCompileRun(); return; }
    jobRunning = true; jobToken = 2;
    SetStatus(L"正在运行…");
    RebuildButtons();
    runner.Configure(ws.settings.compiler, ws.settings.stdFlag, ws.settings.compileFlags,
                     ws.settings.timeLimitMs);
    if (!runner.RunOnly(hwnd_, exe, stdinFile, "", d->path, 2)) {
        jobRunning = false;
        SetStatus(L"运行器忙，请稍后再试");
        RebuildButtons();
    }
}

void App::CmdCompileRun() {
    if (jobRunning) { SetStatus(L"正在忙…"); return; }
    SaveEditorsToTest();
    std::wstring stdinFile;
    if (selTest >= 0 && selTest < (int)curTests.size()) stdinFile = curTests[selTest].inPath;
    StartJob(true, true, stdinFile, 3);
}

void App::CmdRunAllTests() {
    if (jobRunning) return;
    ScanTestsForActive();
    if (curTests.empty()) { SetStatus(L"当前目录没有 .in/.out 测试用例"); return; }
    SaveEditorsToTest();
    for (auto &t : curTests) { t.hasResult = false; t.actual.clear(); }
    runAllIndex = 0;
    jobRunning = true; jobToken = 100;
    SetStatus(L"正在编译…");
    RebuildButtons();
    // 先编译，成功后逐个跑
    StartJob(true, false, L"", 101);
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
    RestartDirWatch();          // 监听必须跟着换到新目录
    RebuildFileTree();
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetStatus(L"工作区已切换到 " + std::wstring(path));
}

void App::CmdZoom(int delta) {
    if (Doc *d = Active()) {
        d->ed->Zoom(delta);
        SetStatus(FormatW(L"字号缩放 %+d", delta));
    }
}

void App::CmdCopyOutput() {
    if (Active()) {
        if (selTest >= 0 && selTest < (int)curTests.size()) {
            CopyTextToClipboard(hwnd_, U2W(curTests[selTest].actual));
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
//  侧栏右键菜单
// ======================================================================
void App::OnRButtonDown(POINT p) {
    if (hRename) CommitInlineRename(true);        // 正在改名时先提交

    // 侧栏标题上右键：工作区相关
    if (sideVisible && p.x >= rcSide_.left && p.x < rcSide_.right &&
        p.y >= rcSideHead_.top && p.y < rcSideHead_.bottom) {
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
        item(3121, L"在资源管理器中打开工作区");
        item(3122, L"切换工作区…");
        item(3123, L"编辑代码片段 (snippets.ini)");
        item(3124, L"快速打开文件…    Ctrl+O");
        item(3125, L"在工作区中搜索…  Ctrl+Shift+F");
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd_);
        int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(m);
        if (cmd == 3121) RevealPath(ws.settings.workspace);
        else if (cmd == 3122) CmdOpenWorkspace();
        else if (cmd == 3123) CmdEditSnippets();
        else if (cmd == 3124) CmdQuickOpen();
        else if (cmd == 3125) CmdFindInFiles();
        return;
    }

    int row = FsRowAt(p);
    if (row >= 0) { selFsRow = row; InvalidateRect(hwnd_, &rcSide_, FALSE); }
    ShowFsMenu(row);
}

// ======================================================================
//  输入
// ======================================================================
static bool Hit(const RECT &r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// 标签几何只在这里算一次：绘制、悬停、点击全部共用，避免三处不一致
int App::TabIndexAt(POINT p, bool *onClose) {
    if (onClose) *onClose = false;
    if (!Hit(rcTab_, p)) return -1;
    HDC dc = GetDC(hwnd_);
    SelectObject(dc, g_theme.Ui());
    int x = rcTab_.left + g_theme.S(6);
    int result = -1;
    for (size_t i = 0; i < docs.size(); ++i) {
        int tw = TextWidth(dc, docs[i]->title, g_theme.Ui()) + g_theme.S(64);
        if (tw > g_theme.S(240)) tw = g_theme.S(240);
        RECT r{ x, rcTab_.top + g_theme.S(5), x + tw, rcTab_.bottom };
        if (Hit(r, p)) {
            result = (int)i;
            RECT cr{ r.right - g_theme.S(34), rcTab_.top, r.right, rcTab_.bottom };
            if (onClose && Hit(cr, p)) *onClose = true;
            break;
        }
        x += tw + g_theme.S(4);
    }
    ReleaseDC(hwnd_, dc);
    return result;
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

    bool onCloseHit = false;
    int hotTab = TabIndexAt(p, &onCloseHit);
    int hotClose = onCloseHit ? hotTab : -1;

    int hotSide = FsRowAt(p);

    int hotBottom = -1;
    for (auto &b : buttons) {
        if (b.id >= 910 && b.id <= 912 && Hit(b.rc, p)) hotBottom = b.id - 910;
    }

    int hotTest = -2, hotDel = -1;
    if (bottomVisible && bottomPage == BottomPage::Tests && Hit(rcTestList_, p)) {
        int rowH = g_theme.S(34);
        int count = (int)curTests.size();
        int idx = (p.y - rcTestList_.top - g_theme.S(6) + testScroll) / rowH;
        if (idx >= 0 && idx < count) {
            hotTest = idx;
            int rowTop = rcTestList_.top + g_theme.S(6) - testScroll + idx * rowH;
            if (p.x >= rcTestList_.right - g_theme.S(30) && p.y < rowTop + rowH - g_theme.S(4)) hotDel = idx;
        } else if (idx == count) hotTest = -1;   // 新增按钮
        else hotTest = -2;
    }
    if (hotDel != hotTestDel) { hotTestDel = hotDel; InvalidateRect(hwnd_, &rcTestList_, FALSE); }

    // 分隔条光标
    int newSplit = 0;
    int dSide = std::abs(p.x - g_theme.S(sideWidth));
    if (sideVisible && dSide < g_theme.S(5) && p.y > rcSide_.top && p.y < rcSide_.bottom) newSplit = 1;
    if (bottomVisible && std::abs(p.y - rcBottom_.top) < g_theme.S(5) && p.x > rcBottom_.left) newSplit = 2;

    if (hotBtn != hotButton_ || hotTab != hotDocTab || hotClose != hotDocClose ||
        hotSide != hotFsRow || hotBottom != hotBottomTab || hotTest != hotTestRow ||
        newSplit != dragSplitSide_) {
        hotButton_ = hotBtn; hotDocTab = hotTab; hotDocClose = hotClose;
        hotFsRow = hotSide; hotBottomTab = hotBottom; hotTestRow = hotTest;
        if (!resizing_ && !chromeDragging_) dragSplitSide_ = newSplit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (dragSplitSide_ == 1) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
    else if (dragSplitSide_ == 2) SetCursor(LoadCursor(nullptr, IDC_SIZENS));
}

void App::OnMouseLeave() {
    hotButton_ = -1; hotDocTab = -1; hotDocClose = -1;
    hotFsRow = -1; hotBottomTab = -1; hotTestRow = -2;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnLButtonDown(POINT p) {
    // 状态栏：点一下跳到「编译信息」（状态文字里已经提示了）
    if (Hit(rcStatus_, p) && !lastCompileLog.empty() && p.x < rcStatus_.left + g_theme.S(600)) {
        bottomVisible = true;
        bottomPage = BottomPage::Diagnostics;
        Layout();
        UpdateTestEditorsVisibility();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 底部标签必须最先处理：它们也在 buttons 里，否则会被下面的通用按钮逻辑吃掉
    for (auto &b : buttons) {
        if (b.id >= 910 && b.id <= 912 && b.visible && Hit(b.rc, p)) {
            SaveEditorsToTest();
            bottomPage = (BottomPage)(b.id - 910);
            bottomVisible = true;
            UpdateTestEditorsVisibility();
            if (bottomPage == BottomPage::Tests) LoadTestToEditors(selTest);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 窗口按钮
    for (auto &b : buttons) {
        if (!b.visible) continue;
        if (Hit(b.rc, p)) {
            if (b.id >= 901 && b.id <= 903) {   // 只匹配标题栏的三个窗口按钮
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

    // 标签页：自己算命中，不依赖悬停状态（TME_LEAVE 会随时把它清掉）
    if (Hit(rcTab_, p)) {
        bool onCloseHit = false;
        int tabIdx = TabIndexAt(p, &onCloseHit);
        if (tabIdx >= 0) {
            if (onCloseHit) CloseDoc(tabIdx);
            else ActivateDoc(tabIdx);
        }
        return;
    }


    // 测试列表
    if (bottomVisible && bottomPage == BottomPage::Tests && Hit(rcTestList_, p)) {
        if (hotTestDel >= 0) { selTest = hotTestDel; CmdDeleteTest(); return; }
        if (hotTestRow == -1) { CmdAddTest(); return; }
        if (hotTestRow >= 0) {
            SaveEditorsToTest();
            selTest = hotTestRow;
            LoadTestToEditors(selTest);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 文件树
    int row = FsRowAt(p);
    if (row >= 0) {
        if (hRename) CommitInlineRename(true);
        selFsRow = row;
        FsRow it = fsRows[row];
        if (it.isDir) {
            auto f = expandedDirs_.find(it.path);
            if (f == expandedDirs_.end()) expandedDirs_.insert(it.path);
            else expandedDirs_.erase(f);
            RebuildFileTree();
        } else {
            OpenFile(it.path);
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
            case 21: RebuildFileTree(); SetStatus(L"已刷新"); break;
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
    case VK_F2:
        if (selFsRow >= 0) BeginInlineRename(selFsRow);
        break;
    case VK_DELETE:
        if (selFsRow >= 0 && selFsRow < (int)fsRows.size()) CmdDeletePath(fsRows[selFsRow].path);
        break;
    case 'S': if (ctrl && shift) CmdSaveAll(); else if (ctrl) SaveActive(); break;
    case 'N': if (ctrl) CmdNewProblem(); break;
    case 'B': if (ctrl) CmdTogglePanel(); break;
    case 'T': if (ctrl && shift) CmdAddTest(); break;
    case 'O': if (ctrl) CmdQuickOpen(); break;
    case 'P': if (ctrl) CmdQuickOpen(); break;
    case 'F': if (ctrl && shift) CmdFindInFiles(); break;
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

// Forest Code - 主应用
#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include "editor.h"
#include "runner.h"
#include "workspace.h"

namespace fc {

// ---------------- 自绘按钮 ----------------
struct UIButton {
    int id = 0;
    std::wstring text;
    unsigned glyph = 0;
    RECT rc{};
    bool primary = false;
    bool danger = false;
    bool iconOnly = false;
    bool visible = true;
    bool enabled = true;
    bool hover = false;
    bool pressed = false;
    bool toggled = false;
};

// ---------------- 侧栏节点 ----------------
struct SideItem {
    enum class Kind { Problem, Statement, Solution, Tests, TestCase, NewSolution, Scratch };
    Kind kind = Kind::Problem;
    int problem = -1;
    int test = -1;
    std::wstring path;
    std::wstring label;
    int depth = 0;
    bool expanded = false;
    bool canExpand = false;
};

// ---------------- 打开文档 ----------------
struct Doc {
    std::wstring path;
    std::wstring title;
    bool statement = false;
    std::unique_ptr<Editor> ed;
};

// ---------------- 底部面板页 ----------------
enum class BottomPage { Tests = 0, Output = 1, Diagnostics = 2 };

// ---------------- 简易表单对话框 ----------------
struct FormField {
    std::wstring label;
    std::wstring value;
    bool multiline = false;
    int height = 0;
    bool password = false;
    int browse = 0;      // 0=无 1=选文件 2=选目录
};
bool ShowFormDialog(HWND parent, const std::wstring &title, std::vector<FormField> &fields);
bool ShowChoiceDialog(HWND parent, const std::wstring &title, const std::wstring &message,
                      const std::vector<std::wstring> &choices, int &selected);

class App {
public:
    static App &Get();

    bool Init(HINSTANCE inst);
    int  Run();

    HWND Hwnd() const { return hwnd_; }
    HINSTANCE Inst() const { return inst_; }

    // 状态
    Workspace ws;
    Runner runner;

    // ---- 布局度量 ----
    RECT rcTitle_{}, rcTool_{}, rcSide_{}, rcTab_{}, rcEdit_{}, rcBottom_{}, rcStatus_{};
    RECT rcSideHead_{}, rcBottomTabs_{}, rcTestList_{}, rcTestEdits_{};
    RECT rcIn_{}, rcExp_{}, rcAct_{}, rcOut_{}, rcDiag_{};

    // ---- 控件 ----
    std::vector<UIButton> buttons;
    int hotButton_ = -1;
    int pressedButton_ = -1;
    int hotTab_ = -1;
    int hotSideItem_ = -1;
    int selSideItem_ = -1;

    // ---- 文档 ----
    std::vector<std::unique_ptr<Doc>> docs;
    int activeDoc = -1;
    int hotDocTab = -1;
    int hotDocClose = -1;

    // ---- 侧栏 ----
    std::vector<SideItem> sideItems;
    int sideScroll = 0;

    // ---- 底部面板 ----
    BottomPage bottomPage = BottomPage::Tests;
    HWND hIn = nullptr, hExp = nullptr, hAct = nullptr, hOut = nullptr, hDiag = nullptr;
    int hotBottomTab = -1;
    int selTest = 0;
    int hotTestRow = -1;
    int hotTestDel = -1;
    int ctxItem_ = -1;
    int testScroll = 0;

    // ---- 运行状态 ----
    bool jobRunning = false;
    int jobToken = 0;
    int runAllIndex = -1;
    std::string lastCompileLog;
    std::string lastOutput;
    std::vector<DiagItem> diags;
    std::vector<Snippet> snippets_;
    std::wstring statusText = L"就绪";
    std::wstring statusRight;
    int  lastCompileMs = 0;

    // ---- 功能 ----
    void Layout();
    void PaintAll(HDC dc);
    void PaintTitle(HDC dc);
    void PaintToolbar(HDC dc);
    void PaintSidebar(HDC dc);
    void PaintTabs(HDC dc);
    void PaintBottom(HDC dc);
    void PaintStatus(HDC dc);
    void RebuildButtons();
    void RebuildSidebar();
    void BuildSnippets();

    // 文档
    Doc *Active();
    Doc *OpenFile(const std::wstring &path, bool statement = false);
    void CloseDoc(int index);
    void ActivateDoc(int index);
    bool SaveActive();
    bool SaveDoc(Doc *d);
    void ShowActiveEditor();

    // 命令
    void CmdCompile();
    void CmdRun();
    void CmdCompileRun();
    void CmdRunAllTests();
    void CmdNewProblem();
    void CmdNewSolution();
    void CmdSaveAll();
    void CmdSettings();
    void CmdOpenWorkspace();
    void CmdZoom(int delta);
    void CmdShowWhitespace(bool on);
    void CmdDeleteTest();
    void CmdAddTest();
    void CmdCopyOutput();
    void CmdTogglePanel();
    void CmdAbout();

    // 侧栏右键菜单
    void OnRButtonDown(POINT p);
    void ShowSidebarMenu(int itemIndex);
    void OnMenuCommand(int id);
    void CmdRenameProblem(int problemIndex);
    void CmdDeleteProblem(int problemIndex);
    void CmdDeleteSolution(const std::wstring &path);
    void CmdRevealInExplorer(const std::wstring &path);
    void CmdNewScratch();

    // 运行
    void StartJob(bool compile, bool run, const std::string &stdinText, int token);
    void OnJobDone(RunResult *r);
    void SetStatus(const std::wstring &s);
    void RefreshRunPanel();
    void RefreshDiagnostics();
    std::wstring ExePathFor(const std::wstring &src) const;

    // 测试用例
    void LoadTestToEditors(int index);
    void SaveEditorsToTest();
    void UpdateTestEditorsVisibility();

    // 输入
    void OnMouseMove(POINT p);
    void OnLButtonDown(POINT p);
    void OnLButtonUp(POINT p);
    void OnMouseLeave();
    void OnKeyDown(WPARAM key);
    void OnSize();
    void OnPaint();

    // 对话框
    void ShowSettingsDialog();
    void ShowNewProblemDialog();

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT fontOld_ = nullptr;
    bool chromeDragging_ = false;
    RECT dragStartRect_{};
    POINT dragStartPt_{};
    int  resizeEdge_ = 0;
    bool resizing_ = false;
    RECT resizeStartRect_{};
    POINT resizeStartPt_{};
    int  dragSplitSide_ = 0;   // 0=无 1=侧栏 2=底部
    int  sideWidth = 264;
    int  bottomHeight = 208;
    bool bottomVisible = true;
    bool sideVisible = true;
};

} // namespace fc

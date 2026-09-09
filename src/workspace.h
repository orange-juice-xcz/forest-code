// Forest Code - 工作区 / 题目 / 设置
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "util.h"
#include "editor.h"

namespace fc {

// ---------------- 数据结构 ----------------
struct TestCase {
    std::wstring inPath, outPath;
    std::string inText, outText;
    bool hasExpected = false;
    bool enabled = true;
    // 最近一次运行结果
    bool hasResult = false;
    bool passed = false;
    bool timeout = false;
    double ms = 0;
    int exitCode = 0;
    std::string actual;
    std::string stderrText;
};

struct Solution {
    std::wstring path;
    std::wstring name;
};

struct Problem {
    std::wstring dir;
    std::wstring title;
    std::wstring source;
    std::wstring difficulty;
    std::wstring tags;
    std::wstring statementPath;
    std::vector<Solution> solutions;
    std::vector<TestCase> tests;
    bool expanded = false;        // 侧栏展开状态
    bool testsExpanded = false;

    std::wstring DisplayTitle() const {
        if (!title.empty()) return title;
        return FileName(dir);
    }
};

struct Settings {
    std::wstring workspace;
    std::wstring compiler;
    std::wstring stdFlag = L"c++11";
    std::wstring compileFlags = L"-O2 -Wall";
    int timeLimitMs = 2000;

    std::wstring uiFont = L"Microsoft YaHei UI";
    int uiSize = 9;
    std::wstring codeFont = L"Cascadia Code";
    int codeSize = 11;
    int tabWidth = 4;
    bool showWhitespace = false;
    bool autoSave = true;

    std::wstring lastFile;
    std::vector<std::wstring> recent;

    bool Load();
    bool Save() const;
    static std::wstring SettingsPath();
};

// ---------------- 工作区 ----------------
class Workspace {
public:
    Settings settings;
    std::vector<Problem> problems;

    bool LoadAll();                              // 载入设置 + 扫描题目
    bool RescanProblems();
    std::wstring ProblemsDir() const;
    std::wstring ScratchDir() const;
    std::wstring BuildDir() const;

    Problem *Find(const std::wstring &dir);
    Problem *FindByFile(const std::wstring &file);

    // 新建
    Problem *CreateProblem(const std::wstring &title, const std::wstring &source,
                           const std::wstring &difficulty, const std::wstring &tags,
                           const std::wstring &templateCode, bool withTemplate);
    Solution *CreateSolution(Problem *p, const std::wstring &name, const std::wstring &code);
    TestCase *AddTest(Problem *p);
    bool SaveTest(const Problem &p, int index);
    bool DeleteTest(Problem *p, int index);
    bool DeleteSolution(Problem *p, const std::wstring &path);
    void ReloadProblem(Problem *p);
    bool RenameProblem(Problem *p, const std::wstring &newTitle);

    static std::wstring DetectCompiler();
    static std::vector<Snippet> BuiltinSnippets();
    static std::string DefaultTemplate();
};

// 解析编译错误：file:line:col: error: msg
struct DiagItem {
    std::wstring file;
    int line = 0;
    int col = 0;
    bool isError = false;
    std::string message;
};
std::vector<DiagItem> ParseDiagnostics(const std::string &log);

} // namespace fc

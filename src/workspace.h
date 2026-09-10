// Forest Code - 工作区 / 设置
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "util.h"
#include "editor.h"

namespace fc {

// ---------------- 数据结构 ----------------
// 约定式模型：没有"题目对象"，一个文件夹就是一个题目，
// X.in + X.out 同目录即成对用例，多个 .cpp 就是多解。
//
// 超过这个尺寸的用例不往下方输入框里塞：几 MB 文本灌进 EDIT 控件会把界面卡死。
// 运行时不必进内存 —— 直接把 .in 文件本身当子进程的标准输入。
const long long kInlineTestLimit = 256 * 1024;

struct TestCase {
    std::wstring inPath, outPath;
    std::string inText, outText;
    bool hasExpected = false;
    bool inTooBig = false;        // 文件过大，没有载入 inText
    bool outTooBig = false;
    // 最近一次运行结果
    bool hasResult = false;
    bool passed = false;
    bool timeout = false;
    double ms = 0;
    int exitCode = 0;
    std::string actual;
    std::string stderrText;
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

    void LoadAll();                              // 载入设置 + 建好工作区/产物目录
    std::wstring BuildDir() const;               // <工作区>\.build

    static std::wstring DetectCompiler();
    static std::vector<Snippet> BuiltinSnippets();
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

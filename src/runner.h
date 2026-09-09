// Forest Code - 编译 / 运行引擎
#pragma once
#include <windows.h>
#include <string>

namespace fc {

#define WM_FC_JOB_DONE (WM_APP + 1)

struct RunResult {
    // 编译
    bool compileRan = false;
    bool compileOk = false;
    std::string compileLog;
    double compileMs = 0;

    // 运行
    bool ran = false;
    int exitCode = 0;
    bool timeout = false;
    double runMs = 0;
    std::string out;
    std::string err;

    // 上下文
    int token = 0;
    std::wstring sourceFile;
    std::wstring exePath;
};

class Runner {
public:
    void Configure(const std::wstring &gxx, const std::wstring &stdFlag,
                   const std::wstring &extraFlags, int timeLimitMs);
    const std::wstring &Compiler() const { return gxx_; }
    const std::wstring &StdFlag() const { return stdFlag_; }
    std::wstring CommandLine(const std::wstring &src, const std::wstring &exe) const;

    // 异步：编译（可选紧接着运行）
    void Start(HWND notify, const std::wstring &src, const std::wstring &exe,
               const std::string &stdinText, int token, bool runAfter);
    // 只运行已存在的 exe
    void RunOnly(HWND notify, const std::wstring &exe, const std::string &stdinText,
                 const std::wstring &src, int token);
    void Kill();
    bool Busy() const { return busy_; }
    void SetBusy(bool b) { busy_ = b; }

private:
    std::wstring gxx_ = L"g++";
    std::wstring stdFlag_ = L"c++11";
    std::wstring extraFlags_ = L"-O2 -Wall";
    int timeLimitMs_ = 2000;
    volatile bool busy_ = false;
    HANDLE job_ = nullptr;

    friend struct JobThreadData;
};

} // namespace fc

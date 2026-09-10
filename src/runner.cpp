// Forest Code - 编译 / 运行引擎实现
#include "runner.h"
#include "util.h"
#include <string>
#include <vector>

namespace fc {

// ---------- 管道工具 ----------
// 只读"当前可用"的数据，永远不阻塞。
// 子进程完全可能把管道写端复制给孙进程（fork/exec），那种情况下 ReadFile 会一直等下去。
static void DrainPipe(HANDLE h, std::string &out) {
    if (h == INVALID_HANDLE_VALUE || !h) return;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) return;
        char buf[8192];
        DWORD got = 0;
        DWORD want = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
        if (!ReadFile(h, buf, want, &got, nullptr) || got == 0) return;
        out.append(buf, got);
    }
}

static std::wstring QuoteArg(const std::wstring &a) {
    if (a.find(L' ') == std::wstring::npos && a.find(L'\t') == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (wchar_t ch : a) { if (ch == L'"') r += L'\\'; r += ch; }
    r += L"\"";
    return r;
}

// 把标准输入落成一个临时文件：管道同步写会在子进程不读时永久阻塞，
// 而竞赛题的输入动辄几 MB（1e6 个整数 ≈ 7MB），一旦卡住整个 IDE 就只能重启。
static std::wstring MakeTempStdinFile(const std::string &data) {
    wchar_t dir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, dir)) return {};
    wchar_t name[MAX_PATH]{};
    if (!GetTempFileNameW(dir, L"fci", 0, name)) return {};
    if (!WriteFileBytes(name, data)) { DeleteFileW(name); return {}; }
    return name;
}

// ---------- 配置 ----------
void Runner::Configure(const std::wstring &gxx, const std::wstring &stdFlag,
                       const std::wstring &extraFlags, int timeLimitMs) {
    gxx_ = gxx;
    stdFlag_ = stdFlag;
    extraFlags_ = extraFlags;
    timeLimitMs_ = timeLimitMs;
}

std::wstring Runner::CommandLine(const std::wstring &src, const std::wstring &exe) const {
    std::wstring cmd = QuoteArg(gxx_);
    cmd += L" -std=" + stdFlag_;
    if (!extraFlags_.empty()) cmd += L" " + extraFlags_;
    cmd += L" -o " + QuoteArg(exe);
    cmd += L" " + QuoteArg(src);
    return cmd;
}

// ---------- 作业线程 ----------
struct JobThreadData {
    Runner *self = nullptr;
    HWND notify = nullptr;
    std::wstring src, exe;
    std::wstring stdinFile;
    std::string stdinText;
    int token = 0;
    bool compile = true;
    bool runAfter = false;
    int timeLimitMs = 2000;
};

static HANDLE MakeJob(int memLimitMb) {
    HANDLE j = CreateJobObjectW(nullptr, nullptr);
    if (!j) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (memLimitMb > 0) {
        info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        info.ProcessMemoryLimit = (SIZE_T)memLimitMb * 1024 * 1024;
    }
    SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
    return j;
}

static void RunProcess(const std::wstring &cmdLine, const std::wstring &workDir,
                       const std::wstring &stdinFile, const std::string *stdinText,
                       std::string &out, std::string &err,
                       int &exitCode, bool &timedOut, double &elapsedMs,
                       int timeLimitMs, HANDLE job) {
    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    CreatePipe(&outR, &outW, &sa, 0);
    CreatePipe(&errR, &errW, &sa, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    // ---- 标准输入 ----
    // 优先直接用 .in 文件本身：既不复制几 MB 的数据，也不存在"写管道阻塞"的死锁。
    std::wstring stdinPath;
    HANDLE inH = INVALID_HANDLE_VALUE;
    if (!stdinFile.empty() && PathExists(stdinFile)) {
        inH = CreateFileW(stdinFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (inH == INVALID_HANDLE_VALUE && stdinText && !stdinText->empty()) {
        stdinPath = MakeTempStdinFile(*stdinText);
        if (!stdinPath.empty())
            inH = CreateFileW(stdinPath.c_str(), GENERIC_READ, FILE_SHARE_READ, &sa,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (inH == INVALID_HANDLE_VALUE) {
        // 没有输入（或文件读不出来）：给一个立刻 EOF 的管道读端。
        // 写端在这里就关掉，既不会阻塞，也不会泄漏句柄。
        HANDLE r = nullptr, w = nullptr;
        if (CreatePipe(&r, &w, &sa, 0)) {
            CloseHandle(w);
            inH = r;
        }
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = outW;
    si.hStdError = errW;
    si.hStdInput = (inH == INVALID_HANDLE_VALUE) ? nullptr : inH;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdLine;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                             workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
    if (inH != INVALID_HANDLE_VALUE) CloseHandle(inH);
    if (!stdinPath.empty()) DeleteFileW(stdinPath.c_str());

    if (!ok) {
        CloseHandle(outR); CloseHandle(outW); CloseHandle(errR); CloseHandle(errW);
        exitCode = -1;
        err += "无法启动进程: " + W2U(cmdLine) + "\n";
        return;
    }

    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    double t0 = NowMs();
    DWORD deadline = (DWORD)(timeLimitMs > 0 ? timeLimitMs : INFINITE);
    bool exited = false;
    for (;;) {
        DWORD w = WaitForSingleObject(pi.hProcess, 15);
        DrainPipe(outR, out);
        DrainPipe(errR, err);
        if (w == WAIT_OBJECT_0) { exited = true; break; }
        if (timeLimitMs > 0 && (DWORD)(NowMs() - t0) > deadline) {
            timedOut = true;
            if (job) TerminateJobObject(job, 1);
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 500);
            break;
        }
    }
    elapsedMs = NowMs() - t0;

    CloseHandle(outW); CloseHandle(errW);
    // 进程已退出，缓冲区里的数据仍然可读；这里只做非阻塞收尾
    DrainPipe(outR, out);
    DrainPipe(errR, err);
    CloseHandle(outR); CloseHandle(errR);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    exitCode = (int)code;
    if (!exited && !timedOut) exitCode = -1;
    CloseHandle(pi.hProcess);
}

static DWORD WINAPI JobThread(LPVOID param) {
    JobThreadData *d = (JobThreadData *)param;
    RunResult *r = new RunResult();
    r->token = d->token;
    r->sourceFile = d->src;
    r->exePath = d->exe;

    std::wstring workDir = ParentDir(d->src);
    HANDLE curJob = nullptr;

    if (d->compile) {
        r->compileRan = true;
        double ms = 0;
        bool to = false;
        int code = 0;
        std::string cmd = W2U(d->self->CommandLine(d->src, d->exe));
        DeleteFileSafe(d->exe);   // 避免旧产物导致误判编译成功
        curJob = MakeJob(0);
        d->self->SetJob(curJob);
        std::string so;
        RunProcess(d->self->CommandLine(d->src, d->exe), workDir, L"", nullptr,
                   so, r->compileLog, code, to, ms, 60000, curJob);
        d->self->SetJob(nullptr);
        if (curJob) CloseHandle(curJob);
        curJob = nullptr;
        if (!so.empty()) r->compileLog = so + r->compileLog;
        r->compileMs = ms;
        r->compileOk = (code == 0) && PathExists(d->exe);
        if (!r->compileOk && r->compileLog.empty())
            r->compileLog = "编译失败（无输出）。\n命令行: " + cmd + "\n";
    }

    if (d->runAfter && (!d->compile || r->compileOk)) {
        r->ran = true;
        curJob = MakeJob(0);
        d->self->SetJob(curJob);
        RunProcess(QuoteArg(d->exe), workDir, d->stdinFile, &d->stdinText, r->out, r->err,
                   r->exitCode, r->timeout, r->runMs, d->timeLimitMs, curJob);
        d->self->SetJob(nullptr);
        if (curJob) CloseHandle(curJob);
    }

    // 先解除"忙"，再投递结果。
    // 反过来的话，UI 线程收到消息后立刻接力发起下一个用例时 busy_ 还是 true，
    // RunOnly 会直接 return，逐用例链静默断掉，界面永远停在"正在忙…"。
    d->self->SetBusy(false);
    if (d->notify) PostMessageW(d->notify, WM_FC_JOB_DONE, 0, (LPARAM)r);
    else delete r;

    delete d;
    return 0;
}

// ---------- 对外接口 ----------
bool Runner::Start(HWND notify, const std::wstring &src, const std::wstring &exe,
                   const std::wstring &stdinFile, const std::string &stdinText,
                   int token, bool runAfter) {
    if (busy_) return false;
    busy_ = true;
    JobThreadData *d = new JobThreadData();
    d->self = this;
    d->notify = notify;
    d->src = src;
    d->exe = exe;
    d->stdinFile = stdinFile;
    d->stdinText = stdinText;
    d->token = token;
    d->compile = true;
    d->runAfter = runAfter;
    d->timeLimitMs = timeLimitMs_;
    HANDLE h = CreateThread(nullptr, 0, JobThread, d, 0, nullptr);
    if (h) CloseHandle(h);
    else { busy_ = false; delete d; return false; }
    return true;
}

bool Runner::RunOnly(HWND notify, const std::wstring &exe, const std::wstring &stdinFile,
                     const std::string &stdinText, const std::wstring &src, int token) {
    if (busy_) return false;
    busy_ = true;
    JobThreadData *d = new JobThreadData();
    d->self = this;
    d->notify = notify;
    d->src = src;
    d->exe = exe;
    d->stdinFile = stdinFile;
    d->stdinText = stdinText;
    d->token = token;
    d->compile = false;
    d->runAfter = true;
    d->timeLimitMs = timeLimitMs_;
    HANDLE h = CreateThread(nullptr, 0, JobThread, d, 0, nullptr);
    if (h) CloseHandle(h);
    else { busy_ = false; delete d; return false; }
    return true;
}

// 真杀掉当前作业（连同它启动的整棵进程树）。
// 作业线程自己会走完收尾流程，所以这里不动 busy_。
void Runner::Kill() {
    HANDLE j = (HANDLE)InterlockedExchangePointer((PVOID volatile *)&job_, nullptr);
    if (j) TerminateJobObject(j, 1);
}

} // namespace fc

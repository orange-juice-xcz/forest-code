// Forest Code - 编译 / 运行引擎实现
#include "runner.h"
#include "util.h"
#include <string>
#include <vector>

namespace fc {

// ---------- 管道工具 ----------
static void DrainPipe(HANDLE h, std::string &out, bool *closed = nullptr) {
    if (h == INVALID_HANDLE_VALUE || !h) return;
    DWORD avail = 0;
    while (PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        char buf[8192];
        DWORD got = 0;
        DWORD want = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
        if (!ReadFile(h, buf, want, &got, nullptr) || got == 0) break;
        out.append(buf, got);
    }
    if (closed) *closed = false;
}

static void ReadPipeToEnd(HANDLE h, std::string &out) {
    if (!h || h == INVALID_HANDLE_VALUE) return;
    for (;;) {
        char buf[8192];
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof(buf), &got, nullptr) || got == 0) break;
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
                       const std::string *stdinText, std::string &out, std::string &err,
                       int &exitCode, bool &timedOut, double &elapsedMs,
                       int timeLimitMs, HANDLE job) {
    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    HANDLE inR = nullptr, inW = nullptr;
    CreatePipe(&outR, &outW, &sa, 0);
    CreatePipe(&errR, &errW, &sa, 0);
    CreatePipe(&inR, &inW, &sa, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = outW;
    si.hStdError = errW;
    si.hStdInput = inR;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdLine;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                             workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
    if (!ok) {
        CloseHandle(outR); CloseHandle(outW); CloseHandle(errR); CloseHandle(errW);
        CloseHandle(inR); CloseHandle(inW);
        exitCode = -1;
        err += "无法启动进程: " + W2U(cmdLine) + "\n";
        return;
    }

    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // 写 stdin
    if (stdinText && !stdinText->empty()) {
        DWORD wrote = 0;
        WriteFile(inW, stdinText->data(), (DWORD)stdinText->size(), &wrote, nullptr);
    }
    CloseHandle(inW);
    inW = nullptr;

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
    ReadPipeToEnd(outR, out);
    ReadPipeToEnd(errR, err);
    CloseHandle(outR); CloseHandle(errR);
    if (inW) CloseHandle(inW);

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

    if (d->compile) {
        r->compileRan = true;
        double ms = 0;
        bool to = false;
        int code = 0;
        std::string cmd = W2U(d->self->CommandLine(d->src, d->exe));
        DeleteFileSafe(d->exe);   // 避免旧产物导致误判编译成功
        HANDLE job = MakeJob(0);
        std::string so;
        RunProcess(d->self->CommandLine(d->src, d->exe), workDir, nullptr,
                   so, r->compileLog, code, to, ms, 60000, job);
        if (!so.empty()) r->compileLog = so + r->compileLog;
        if (job) CloseHandle(job);
        r->compileMs = ms;
        r->compileOk = (code == 0) && PathExists(d->exe);
        if (!r->compileOk && r->compileLog.empty())
            r->compileLog = "编译失败（无输出）。\n命令行: " + cmd + "\n";
    }

    if (d->runAfter && (!d->compile || r->compileOk)) {
        r->ran = true;
        HANDLE job = MakeJob(0);
        RunProcess(QuoteArg(d->exe), workDir, &d->stdinText, r->out, r->err,
                   r->exitCode, r->timeout, r->runMs, d->timeLimitMs, job);
        if (job) CloseHandle(job);
    }

    if (d->notify) PostMessageW(d->notify, WM_FC_JOB_DONE, 0, (LPARAM)r);
    else delete r;

    d->self->SetBusy(false);
    delete d;
    return 0;
}

// ---------- 对外接口 ----------
void Runner::Start(HWND notify, const std::wstring &src, const std::wstring &exe,
                   const std::string &stdinText, int token, bool runAfter) {
    if (busy_) return;
    busy_ = true;
    JobThreadData *d = new JobThreadData();
    d->self = this;
    d->notify = notify;
    d->src = src;
    d->exe = exe;
    d->stdinText = stdinText;
    d->token = token;
    d->compile = true;
    d->runAfter = runAfter;
    d->timeLimitMs = timeLimitMs_;
    HANDLE h = CreateThread(nullptr, 0, JobThread, d, 0, nullptr);
    if (h) CloseHandle(h);
    else { busy_ = false; delete d; }
}

void Runner::RunOnly(HWND notify, const std::wstring &exe, const std::string &stdinText,
                     const std::wstring &src, int token) {
    if (busy_) return;
    busy_ = true;
    JobThreadData *d = new JobThreadData();
    d->self = this;
    d->notify = notify;
    d->src = src;
    d->exe = exe;
    d->stdinText = stdinText;
    d->token = token;
    d->compile = false;
    d->runAfter = true;
    d->timeLimitMs = timeLimitMs_;
    HANDLE h = CreateThread(nullptr, 0, JobThread, d, 0, nullptr);
    if (h) CloseHandle(h);
    else { busy_ = false; delete d; }
}

void Runner::Kill() {
    busy_ = false;
}

} // namespace fc

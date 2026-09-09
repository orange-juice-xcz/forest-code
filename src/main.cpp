// Forest Code - 程序入口
#include <windows.h>
#include "app.h"
#include "theme.h"

extern "C" int Scintilla_RegisterClasses(void *hInstance);
extern "C" int Scintilla_ReleaseResources(void);

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 高 DPI：优先 Per-Monitor V2（清单已声明，这里做兜底）
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI *PFN_SetCtx)(HANDLE);
    auto setCtx = (PFN_SetCtx)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (setCtx) {
        setCtx((HANDLE)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    } else {
        SetProcessDPIAware();
    }

    if (!Scintilla_RegisterClasses(hInst)) {
        MessageBoxW(nullptr, L"Scintilla 初始化失败", L"Forest Code", MB_ICONERROR);
        return 1;
    }

    fc::App &app = fc::App::Get();
    if (!app.Init(hInst)) {
        MessageBoxW(nullptr, L"Forest Code 初始化失败", L"Forest Code", MB_ICONERROR);
        return 2;
    }
    int rc = app.Run();
    Scintilla_ReleaseResources();
    return rc;
}

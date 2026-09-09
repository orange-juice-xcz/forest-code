// Forest Code - 基础工具实现
#include "util.h"
#include <shlobj.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace fc {

// ================= 字符串 =================
std::string W2U(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring U2W(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring TrimW(const std::wstring &s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> Split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

std::vector<std::wstring> SplitW(const std::wstring &s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == std::wstring::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

bool StartsWith(const std::string &s, const std::string &p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool EndsWith(const std::string &s, const std::string &p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

bool IEquals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

std::string Format(const char *fmt, ...) {
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    return std::string(buf, (size_t)std::min<int>(n, (int)sizeof(buf) - 1));
}

std::wstring FormatW(const wchar_t *fmt, ...) {
    wchar_t buf[4096];
    va_list ap; va_start(ap, fmt);
    int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    return std::wstring(buf, (size_t)n);
}

// ================= 路径 =================
std::wstring JoinPath(const std::wstring &a, const std::wstring &b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring r = a;
    if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
    std::wstring bb = b;
    while (!bb.empty() && (bb.front() == L'\\' || bb.front() == L'/')) bb.erase(bb.begin());
    return r + bb;
}

std::wstring ParentDir(const std::wstring &p) {
    size_t i = p.find_last_of(L"\\/");
    if (i == std::wstring::npos) return {};
    return p.substr(0, i);
}

std::wstring FileName(const std::wstring &p) {
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? p : p.substr(i + 1);
}

std::wstring FileStem(const std::wstring &p) {
    std::wstring n = FileName(p);
    size_t i = n.find_last_of(L'.');
    return i == std::wstring::npos ? n : n.substr(0, i);
}

std::wstring FileExt(const std::wstring &p) {
    std::wstring n = FileName(p);
    size_t i = n.find_last_of(L'.');
    if (i == std::wstring::npos) return {};
    std::wstring e = n.substr(i);
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}

bool PathExists(const std::wstring &p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsDir(const std::wstring &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool MakeDirs(const std::wstring &p) {
    if (p.empty() || IsDir(p)) return true;
    std::wstring parent = ParentDir(p);
    if (!parent.empty() && parent != p) MakeDirs(parent);
    if (CreateDirectoryW(p.c_str(), nullptr)) return true;
    return IsDir(p);
}

std::vector<std::wstring> ListDir(const std::wstring &dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        out.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return ParentDir(buf);
}

std::wstring GetEnv(const wchar_t *name) {
    wchar_t buf[32768]{};
    DWORD n = GetEnvironmentVariableW(name, buf, 32768);
    return n ? std::wstring(buf, n) : std::wstring();
}

std::wstring AppDataDir() {
    std::wstring base = GetEnv(L"APPDATA");
    if (base.empty()) base = ExeDir();
    return JoinPath(base, L"ForestCode");
}

// ================= 文件 =================
bool ReadFileBytes(const std::wstring &path, std::string &out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0, total = 0;
    while (total < out.size()) {
        if (!ReadFile(h, &out[total], (DWORD)(out.size() - total), &got, nullptr) || got == 0) break;
        total += got;
    }
    CloseHandle(h);
    out.resize(total);
    return true;
}

bool WriteFileBytes(const std::wstring &path, const std::string &data) {
    MakeDirs(ParentDir(path));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = TRUE;
    if (!data.empty()) ok = WriteFile(h, data.data(), (DWORD)data.size(), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == data.size();
}

static bool LooksBinary(const std::string &d) {
    for (size_t i = 0; i < d.size() && i < 4096; ++i)
        if (d[i] == '\0') return true;
    return false;
}

bool ReadFileUtf8(const std::wstring &path, std::string &out) {
    std::string raw;
    if (!ReadFileBytes(path, raw)) return false;
    if (LooksBinary(raw)) return false;
    // BOM
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);
    // 简单 UTF-16 检测
    if (raw.size() >= 2 && ((unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE)) {
        std::wstring w((const wchar_t *)(raw.data() + 2), (raw.size() - 2) / 2);
        out = W2U(w);
        return true;
    }
    out = raw;
    return true;
}

bool WriteFileUtf8(const std::wstring &path, const std::string &data) {
    return WriteFileBytes(path, data);
}

bool DeleteFileSafe(const std::wstring &path) {
    if (!PathExists(path)) return true;
    return DeleteFileW(path.c_str()) != 0;
}

// ================= INI =================
bool Ini::Load(const std::wstring &path) {
    items_.clear();
    std::string text;
    if (!ReadFileUtf8(path, text)) return false;
    for (auto &line : Split(text, '\n')) {
        std::string l = Trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';' || l[0] == '[') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        items_.emplace_back(Trim(l.substr(0, eq)), Trim(l.substr(eq + 1)));
    }
    return true;
}

bool Ini::Save(const std::wstring &path) const {
    std::string text;
    for (auto &kv : items_) text += kv.first + "=" + kv.second + "\n";
    return WriteFileUtf8(path, text);
}

bool Ini::Has(const std::string &key) const {
    for (auto &kv : items_) if (kv.first == key) return true;
    return false;
}

std::string Ini::Get(const std::string &key, const std::string &def) const {
    for (auto &kv : items_) if (kv.first == key) return kv.second;
    return def;
}

int Ini::GetInt(const std::string &key, int def) const {
    std::string v = Get(key);
    if (v.empty()) return def;
    return atoi(v.c_str());
}

bool Ini::GetBool(const std::string &key, bool def) const {
    std::string v = Get(key);
    if (v.empty()) return def;
    return v == "1" || IEquals(v, "true") || IEquals(v, "yes");
}

void Ini::Set(const std::string &key, const std::string &value) {
    for (auto &kv : items_) if (kv.first == key) { kv.second = value; return; }
    items_.emplace_back(key, value);
}

void Ini::SetInt(const std::string &key, int value) { Set(key, Format("%d", value)); }
void Ini::SetBool(const std::string &key, bool value) { Set(key, value ? "1" : "0"); }
void Ini::Clear() { items_.clear(); }

// ================= 剪贴板 =================
bool CopyTextToClipboard(HWND owner, const std::wstring &text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void *p = GlobalLock(h);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    return h != nullptr;
}

bool GetClipboardText(HWND owner, std::wstring &out) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *p = (const wchar_t *)GlobalLock(h);
        if (p) { out = p; GlobalUnlock(h); ok = true; }
    }
    CloseClipboard();
    return ok;
}

// ================= 杂项 =================
double NowMs() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

std::wstring NowStamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return FormatW(L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::wstring ToEdit(const std::string &utf8) {
    std::string s;
    s.reserve(utf8.size() + 64);
    for (size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\n' && (i == 0 || utf8[i - 1] != '\r')) s += "\r\n";
        else s += utf8[i];
    }
    return U2W(s);
}

std::string FromEdit(const std::wstring &w) {
    std::string s = W2U(w);
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        o += s[i];
    }
    return o;
}

bool IsAppShortcut(WPARAM key) {
    switch (key) {
    case VK_F1: case VK_F5: case VK_F6: case VK_F9: case VK_F11:
        return true;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) {
        switch (key) {
        case 'S': case 'N': case 'B': case VK_TAB:
        case VK_OEM_PLUS: case VK_OEM_MINUS: case VK_ADD: case VK_SUBTRACT:
            return true;
        }
    }
    return false;
}

} // namespace fc

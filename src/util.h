// Forest Code - 基础工具：字符串 / 文件 / 路径 / INI
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace fc {

// ---------- 字符串 ----------
std::string  W2U(const std::wstring &w);
std::wstring U2W(const std::string &s);
std::string  Trim(const std::string &s);
std::wstring TrimW(const std::wstring &s);
std::vector<std::string>  Split(const std::string &s, char sep);
std::vector<std::wstring> SplitW(const std::wstring &s, wchar_t sep);
bool StartsWith(const std::string &s, const std::string &p);
bool EndsWith(const std::string &s, const std::string &p);
bool IEquals(const std::string &a, const std::string &b);
std::string Format(const char *fmt, ...);
std::wstring FormatW(const wchar_t *fmt, ...);

// ---------- 路径 ----------
std::wstring JoinPath(const std::wstring &a, const std::wstring &b);
std::wstring ParentDir(const std::wstring &p);
std::wstring FileName(const std::wstring &p);
std::wstring FileStem(const std::wstring &p);
std::wstring FileExt(const std::wstring &p);      // 含点，小写
bool PathExists(const std::wstring &p);
bool IsDir(const std::wstring &p);
bool MakeDirs(const std::wstring &p);
std::vector<std::wstring> ListDir(const std::wstring &dir);   // 仅名字，已排序
std::wstring ExeDir();
std::wstring AppDataDir();     // %APPDATA%\ForestCode
std::wstring GetEnv(const wchar_t *name);

// ---------- 文件 ----------
bool ReadFileBytes(const std::wstring &path, std::string &out);
bool WriteFileBytes(const std::wstring &path, const std::string &data);
bool ReadFileUtf8(const std::wstring &path, std::string &out);
bool WriteFileUtf8(const std::wstring &path, const std::string &data);
bool DeleteFileSafe(const std::wstring &path);

// ---------- INI（UTF-8，保持写入顺序） ----------
class Ini {
public:
    bool Load(const std::wstring &path);
    bool Save(const std::wstring &path) const;
    std::string Get(const std::string &key, const std::string &def = "") const;
    int  GetInt(const std::string &key, int def = 0) const;
    bool GetBool(const std::string &key, bool def = false) const;
    void Set(const std::string &key, const std::string &value);
    void SetInt(const std::string &key, int value);
    void SetBool(const std::string &key, bool value);
    bool Has(const std::string &key) const;
    void Clear();
private:
    std::vector<std::pair<std::string, std::string>> items_;
};

// ---------- 剪贴板 ----------
bool CopyTextToClipboard(HWND owner, const std::wstring &text);
bool GetClipboardText(HWND owner, std::wstring &out);

// ---------- 杂项 ----------
double NowMs();
std::wstring NowStamp();
bool IsAppShortcut(WPARAM key);   // 需要全局生效的快捷键（F 键 / Ctrl 组合）

} // namespace fc

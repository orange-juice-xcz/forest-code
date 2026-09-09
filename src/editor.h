// Forest Code - Scintilla 编辑器封装
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

struct SCNotification;

namespace fc {

struct Snippet {
    std::string trigger;   // 触发词，如 "fori"
    std::string body;      // 展开内容（\n 已转义处理）
    std::string desc;      // 说明
    bool builtin = false;
};

enum class Lang { Cpp, Markdown, Plain };

class Editor {
public:
    bool Create(HWND parent, int id);
    HWND Hwnd() const { return hwnd_; }

    void ApplyTheme();
    void ThemeScrollbars();       // Scintilla 用原生滚动条，需单独深色化
    void SetCodeFont(const std::wstring &face, int pt);
    void SetLang(Lang lang);

    // 文本 / 文件
    std::string GetText() const;
    void SetText(const std::string &utf8);
    bool Load(const std::wstring &path);
    bool Save(const std::wstring &path);
    bool Modified() const;
    void MarkSaved();
    void SetReadOnly(bool ro);
    void GotoLine(int line, bool center = true);
    void RefreshStyles();

    // 补全
    void SetSnippets(std::vector<Snippet> sn) { snippets_ = std::move(sn); }
    void SetExtraWords(const std::vector<std::string> &words) { extraWords_ = words; }
    void TriggerCompletion(bool force = false);
    void RebuildKeywordList();

    // 标记
    void ClearMarkers();
    void AddErrorMarker(int line, const std::string &msg);
    void SetBookmark(int line, bool on);

    // 状态
    int CurrentLine() const;      // 1-based
    int CurrentCol() const;       // 1-based
    int LineCount() const;
    void Zoom(int delta);
    int ZoomLevel() const;

    // 回调
    std::function<void()> onUpdateUi;
    std::function<void(int /*line*/)> onDoubleClickLine;
    std::function<void()> onSaveRequest;

    static LRESULT CALLBACK SubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void HandleNotify(SCNotification *n);   // Scintilla 通知发给父窗口，由主窗口转发过来

private:
    HWND hwnd_ = nullptr;
    Lang lang_ = Lang::Cpp;
    std::wstring fontFace_ = L"Cascadia Code";
    int fontSize_ = 11;
    std::vector<Snippet> snippets_;
    std::vector<std::string> extraWords_;


    void CollectDocWords(std::vector<std::string> &out) const;
    void ShowCompletionList(const std::string &prefix, bool force);
    bool TryExpandSnippet(const std::string &text);
    void HandleCharAdded(int ch);
};

} // namespace fc

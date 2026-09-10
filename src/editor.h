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

// 按扩展名选词法器：.md 就是 Markdown，不要拿 C++ 规则去高亮题面
Lang LangForPath(const std::wstring &path);

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

    // 显示
    void SetViewWhitespace(bool on);

    // 补全
    void SetSnippets(std::vector<Snippet> sn) { snippets_ = std::move(sn); }
    void TriggerCompletion(bool force = false);
    void RebuildKeywordList();

    // 标记
    void ClearMarkers();
    void AddErrorMarker(int line, const std::string &msg);

    // 状态
    int CurrentLine() const;      // 1-based
    int CurrentCol() const;       // 1-based
    int LineCount() const;
    void Zoom(int delta);
    int ZoomLevel() const;

    // 回调
    std::function<void()> onUpdateUi;

    static LRESULT CALLBACK SubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void HandleNotify(SCNotification *n);   // Scintilla 通知发给父窗口，由主窗口转发过来

private:
    HWND hwnd_ = nullptr;
    Lang lang_ = Lang::Cpp;
    std::wstring fontFace_ = L"Cascadia Code";
    int fontSize_ = 11;
    bool showWs_ = false;
    std::vector<Snippet> snippets_;

    // 文档词表缓存：大文件每敲一个字符全量重扫会明显卡顿
    std::vector<std::string> docWords_;
    bool docWordsDirty_ = true;
    double docWordsBuiltAt_ = 0;

    const std::vector<std::string> &DocWords();
    void ShowCompletionList(const std::string &prefix, bool force);
    const Snippet *FindSnippet(const std::string &trigger) const;
    // 用片段正文替换 [startPos, 光标) 区间；返回是否真的展开了
    bool TryExpandSnippet(const std::string &trigger, int startPos);
    bool ExpandSnippetAtCaret();     // Tab 触发：光标前的词正好是触发词
    void HandleCharAdded(int ch);
};

} // namespace fc

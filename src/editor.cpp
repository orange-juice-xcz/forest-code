// Forest Code - Scintilla 编辑器实现
#include "editor.h"
#include "theme.h"
#include "util.h"
#include <commctrl.h>
#include <uxtheme.h>
#include "Scintilla.h"
#include "SciLexer.h"
#include "ILexer.h"
#include "LexerModule.h"

#include <algorithm>
#include <set>
#include <cstring>

extern const Lexilla::LexerModule lmCPP;
extern const Lexilla::LexerModule lmMarkdown;

namespace fc {

Lang LangForPath(const std::wstring &path) {
    std::wstring e = FileExt(path);
    if (e == L".cpp" || e == L".cc" || e == L".cxx" || e == L".c" ||
        e == L".h" || e == L".hpp" || e == L".hxx") return Lang::Cpp;
    if (e == L".md" || e == L".markdown") return Lang::Markdown;
    return Lang::Plain;
}

// Scintilla 消息助手（默认参数，少写两个 0）
static inline LRESULT Sci(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(h, m, w, l);
}

static const char *kKeywords =
    "alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t "
    "class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield "
    "decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto "
    "if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected "
    "public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct "
    "switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void "
    "volatile wchar_t while xor xor_eq";

static const char *kTypes =
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t size_t ptrdiff_t "
    "string wstring u8string vector map set multiset multimap unordered_map unordered_set deque queue "
    "priority_queue stack pair tuple array list forward_list bitset valarray complex function shared_ptr "
    "unique_ptr weak_ptr optional variant any string_view istream ostream istringstream ostringstream "
    "stringstream ifstream ofstream fstream cin cout cerr clog endl ws getline";

static const char *kFunctions =
    "abs max min swap sort stable_sort nth_element reverse unique lower_bound upper_bound binary_search "
    "find find_if count count_if accumulate partial_sum iota fill fill_n copy copy_if move transform "
    "make_pair make_tuple push_back pop_back emplace_back insert erase clear resize reserve begin end rbegin rend "
    "front back size empty at data substr length c_str find_first_of find_last_of replace to_string stoi stoll stod "
    "pow sqrt log log2 exp floor ceil round fabs sin cos tan atan2 gcd lcm __gcd next_permutation prev_permutation "
    "merge set_union set_intersection set_difference priority_queue multiset memset memcpy strlen strcmp "
    "printf scanf puts putchar getchar fgets sscanf sprintf malloc free qsort";

// ---------- 创建 ----------
bool Editor::Create(HWND parent, int id) {
    hwnd_ = CreateWindowExW(0, L"Scintilla", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                            0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                            (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
    if (!hwnd_) return false;
    // 深色滚动条
    DarkenWindow(hwnd_);
    SetWindowTheme(hwnd_, L"DarkMode_Explorer", nullptr);
    SetWindowSubclass(hwnd_, SubclassProc, 1, (DWORD_PTR)this);
    RebuildKeywordList();
    ApplyTheme();
    return true;
}

// ---------- 基础样式 ----------
void Editor::RebuildKeywordList() {
    Sci(hwnd_, SCI_SETKEYWORDS, 0, (LPARAM)kKeywords);
    Sci(hwnd_, SCI_SETKEYWORDS, 1, (LPARAM)kTypes);
    Sci(hwnd_, SCI_SETKEYWORDS, 2, (LPARAM)kFunctions);
    Sci(hwnd_, SCI_SETKEYWORDS, 3, (LPARAM)"");
}

void Editor::ApplyTheme() {
    const Palette &p = g_theme.c;

    Sci(hwnd_, SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DEFAULT);

    Sci(hwnd_, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)W2U(fontFace_).c_str());
    Sci(hwnd_, SCI_STYLESETSIZE, STYLE_DEFAULT, fontSize_);
    Sci(hwnd_, SCI_STYLESETFORE, STYLE_DEFAULT, p.synDefault);
    Sci(hwnd_, SCI_STYLESETBACK, STYLE_DEFAULT, p.bgEditor);
    Sci(hwnd_, SCI_STYLECLEARALL);

    auto st = [&](int style, COLORREF fg, COLORREF bg, bool bold = false, bool italic = false) {
        Sci(hwnd_, SCI_STYLESETFORE, style, fg);
        Sci(hwnd_, SCI_STYLESETBACK, style, bg);
        Sci(hwnd_, SCI_STYLESETBOLD, style, bold ? 1 : 0);
        Sci(hwnd_, SCI_STYLESETITALIC, style, italic ? 1 : 0);
    };

    st(SCE_C_DEFAULT,          p.synDefault,  p.bgEditor);
    st(SCE_C_COMMENT,          p.synComment,  p.bgEditor, false, true);
    st(SCE_C_COMMENTLINE,      p.synComment,  p.bgEditor, false, true);
    st(SCE_C_COMMENTDOC,       p.synComment,  p.bgEditor, false, true);
    st(SCE_C_COMMENTLINEDOC,   p.synComment,  p.bgEditor, false, true);
    st(SCE_C_COMMENTDOCKEYWORD,p.synComment,  p.bgEditor, true, true);
    st(SCE_C_NUMBER,           p.synNumber,   p.bgEditor);
    st(SCE_C_WORD,             p.synKeyword,  p.bgEditor, true);
    st(SCE_C_WORD2,            p.synType,     p.bgEditor);
    st(SCE_C_STRING,           p.synString,   p.bgEditor);
    st(SCE_C_CHARACTER,        p.synString,   p.bgEditor);
    st(SCE_C_STRINGRAW,        p.synString,   p.bgEditor);
    st(SCE_C_STRINGEOL,        p.synString,   p.bgEditor);
    st(SCE_C_VERBATIM,         p.synString,   p.bgEditor);
    st(SCE_C_PREPROCESSOR,     p.synPreproc,  p.bgEditor);
    st(SCE_C_PREPROCESSORCOMMENT, p.synComment, p.bgEditor, false, true);
    st(SCE_C_OPERATOR,         p.synOperator, p.bgEditor);
    st(SCE_C_IDENTIFIER,       p.synDefault,  p.bgEditor);
    st(SCE_C_GLOBALCLASS,      p.synGlobal,   p.bgEditor, true);
    st(SCE_C_ESCAPESEQUENCE,   p.synNumber,   p.bgEditor);
    st(SCE_C_UUID,             p.synNumber,   p.bgEditor);
    st(SCE_C_HASHQUOTEDSTRING, p.synString,   p.bgEditor);

    // 行号

    Sci(hwnd_, SCI_STYLESETFONT, STYLE_LINENUMBER, (LPARAM)W2U(fontFace_).c_str());
    Sci(hwnd_, SCI_STYLESETSIZE, STYLE_LINENUMBER, fontSize_ - 1);
    st(STYLE_LINENUMBER, p.gutterText, p.gutterBg);
    Sci(hwnd_, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    Sci(hwnd_, SCI_SETMARGINWIDTHN, 0, g_theme.S(52));
    Sci(hwnd_, SCI_SETMARGINBACKN, 0, p.gutterBg);
    Sci(hwnd_, SCI_SETMARGINLEFT, 0, 0);
    Sci(hwnd_, SCI_SETMARGINRIGHT, 0, 0);
    // 折叠
    Sci(hwnd_, SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    Sci(hwnd_, SCI_SETMARGINWIDTHN, 1, g_theme.S(18));
    Sci(hwnd_, SCI_SETMARGINMASKN, 1, SC_MASK_FOLDERS);
    Sci(hwnd_, SCI_SETMARGINBACKN, 1, p.gutterBg);
    // 折叠边距用图案填充，必须单独设置颜色，否则是白的
    Sci(hwnd_, SCI_SETFOLDMARGINCOLOUR, 1, p.gutterBg);
    Sci(hwnd_, SCI_SETFOLDMARGINHICOLOUR, 1, RGB(0x0F, 0x1F, 0x16));
    Sci(hwnd_, SCI_SETMARGINSENSITIVEN, 1, 1);
    struct { int m; int mk; } marks[] = {
        {SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS}, {SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS},
        {SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE},     {SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER},
        {SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED},
        {SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED},
        {SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER},
    };
    for (auto &mk : marks) {
        Sci(hwnd_, SCI_MARKERDEFINE, mk.m, mk.mk);
        Sci(hwnd_, SCI_MARKERSETFORE, mk.m, p.gutterBg);
        Sci(hwnd_, SCI_MARKERSETBACK, mk.m, p.accentDim);
    }
    Sci(hwnd_, SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);

    // 光标 / 选区 / 当前行
    Sci(hwnd_, SCI_SETCARETFORE, p.caret);
    Sci(hwnd_, SCI_SETCARETWIDTH, g_theme.S(2));
    Sci(hwnd_, SCI_SETCARETLINEVISIBLE, 1);
    Sci(hwnd_, SCI_SETCARETLINEBACK, p.caretLine);
    // 不要用 SCI_SETCARETLINEBACKALPHA(255)：会把当前行切到 OverText 层，盖住文字
    Sci(hwnd_, SCI_SETCARETLINELAYER, SC_LAYER_BASE);
    Sci(hwnd_, SCI_SETSELBACK, 1, p.selection);
    Sci(hwnd_, SCI_SETADDITIONALSELBACK, 1, p.selection);
    // 缩进参考线
    Sci(hwnd_, SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    Sci(hwnd_, SCI_STYLESETFORE, STYLE_INDENTGUIDE, p.indentGuide);
    Sci(hwnd_, SCI_STYLESETBACK, STYLE_INDENTGUIDE, p.bgEditor);

    // 括号匹配
    Sci(hwnd_, SCI_STYLESETFORE, STYLE_BRACELIGHT, p.accent);
    Sci(hwnd_, SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
    Sci(hwnd_, SCI_STYLESETBACK, STYLE_BRACELIGHT, p.accentDeep);
    Sci(hwnd_, SCI_STYLESETFORE, STYLE_BRACEBAD, p.error);
    Sci(hwnd_, SCI_STYLESETBOLD, STYLE_BRACEBAD, 1);
    Sci(hwnd_, SCI_STYLESETBACK, STYLE_BRACEBAD, p.accentDeep);
    // 空白符
    Sci(hwnd_, SCI_SETWHITESPACEFORE, 1, p.indentGuide);
    Sci(hwnd_, SCI_SETWHITESPACESIZE, g_theme.S(2));
    // 自动补全外观
    Sci(hwnd_, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST, p.synDefault);
    Sci(hwnd_, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_BACK, p.bgRaised);
    Sci(hwnd_, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED, p.textOnAcc);
    Sci(hwnd_, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED_BACK, p.accent);
    Sci(hwnd_, SCI_AUTOCSETSEPARATOR, ' ');
    Sci(hwnd_, SCI_AUTOCSETIGNORECASE, 1);
    Sci(hwnd_, SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
    Sci(hwnd_, SCI_AUTOCSETMAXHEIGHT, 9);   // 单位是行数
    Sci(hwnd_, SCI_AUTOCSETMAXWIDTH, g_theme.S(420));
    Sci(hwnd_, SCI_AUTOCSETCHOOSESINGLE, 1);
    Sci(hwnd_, SCI_AUTOCSETDROPRESTOFWORD, 1);
    // 行为
    Sci(hwnd_, SCI_SETUSETABS, 0);
    Sci(hwnd_, SCI_SETTABWIDTH, 4);
    Sci(hwnd_, SCI_SETINDENT, 4);
    Sci(hwnd_, SCI_SETBACKSPACEUNINDENTS, 1);
    Sci(hwnd_, SCI_SETSCROLLWIDTHTRACKING, 1);
    Sci(hwnd_, SCI_SETMOUSEDWELLTIME, 600);
    Sci(hwnd_, SCI_SETEOLMODE, SC_EOL_LF);
    Sci(hwnd_, SCI_SETEDGEMODE, EDGE_NONE);
    Sci(hwnd_, SCI_SETWRAPMODE, SC_WRAP_NONE);
    Sci(hwnd_, SCI_SETZOOM, 0);
    // 空白符
    Sci(hwnd_, SCI_SETVIEWWS, showWs_ ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE, 0);
    // 错误标记
    Sci(hwnd_, SCI_MARKERDEFINE, 24, SC_MARK_BACKGROUND);
    Sci(hwnd_, SCI_MARKERSETBACK, 24, RGB(0x3A, 0x14, 0x16));
    Sci(hwnd_, SCI_MARKERDEFINE, 25, SC_MARK_BACKGROUND);
    Sci(hwnd_, SCI_MARKERSETBACK, 25, RGB(0x2A, 0x2A, 0x10));
    Sci(hwnd_, SCI_MARKERDEFINE, 26, SC_MARK_CIRCLE);
    Sci(hwnd_, SCI_MARKERSETBACK, 26, p.error);
    Sci(hwnd_, SCI_MARKERSETALPHA, 26, 220);
}

void Editor::ThemeScrollbars() {
    if (!hwnd_) return;
    EnumChildWindows(hwnd_, [](HWND h, LPARAM) -> BOOL {
        wchar_t cls[64]{};
        GetClassNameW(h, cls, 64);
        if (lstrcmpiW(cls, L"SCROLLBAR") == 0) {
            DarkenWindow(h);
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
            RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        }
        return TRUE;
    }, 0);
}

void Editor::SetCodeFont(const std::wstring &face, int pt) {
    fontFace_ = face;
    fontSize_ = pt;
    ApplyTheme();
    RefreshStyles();
}

void Editor::SetViewWhitespace(bool on) {
    showWs_ = on;
    Sci(hwnd_, SCI_SETVIEWWS, on ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE, 0);
}

void Editor::SetLang(Lang lang) {
    lang_ = lang;
    if (lang == Lang::Cpp) {
        Sci(hwnd_, SCI_SETILEXER, 0, (LPARAM)lmCPP.Create());
        Sci(hwnd_, SCI_SETPROPERTY, (WPARAM)"lexer.cpp.track.preprocessor", (LPARAM)"1");
        Sci(hwnd_, SCI_SETPROPERTY, (WPARAM)"lexer.cpp.allow.dollars", (LPARAM)"0");
        Sci(hwnd_, SCI_SETPROPERTY, (WPARAM)"fold", (LPARAM)"1");
        Sci(hwnd_, SCI_SETPROPERTY, (WPARAM)"fold.comment", (LPARAM)"1");
        Sci(hwnd_, SCI_SETPROPERTY, (WPARAM)"fold.preprocessor", (LPARAM)"1");
    } else if (lang == Lang::Markdown) {
        Sci(hwnd_, SCI_SETILEXER, 0, (LPARAM)lmMarkdown.Create());
    } else {
        Sci(hwnd_, SCI_SETILEXER, 0, 0);
    }
    // 设置词法器会重置样式，必须重新应用
    ApplyTheme();
}

void Editor::RefreshStyles() {
    int first = (int)Sci(hwnd_, SCI_GETFIRSTVISIBLELINE, 0, 0);
    int cnt = (int)Sci(hwnd_, SCI_LINESONSCREEN, 0, 0);
    Sci(hwnd_, SCI_COLOURISE, 0, -1);
    Sci(hwnd_, SCI_SETFIRSTVISIBLELINE, first + cnt);  // 强制重绘
    Sci(hwnd_, SCI_SETFIRSTVISIBLELINE, first);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------- 文本 ----------
std::string Editor::GetText() const {
    int len = (int)Sci(hwnd_, SCI_GETLENGTH, 0, 0);
    std::string out((size_t)len, '\0');
    if (len) Sci(hwnd_, SCI_GETTEXT, len + 1, (LPARAM)&out[0]);
    return out;
}

void Editor::SetText(const std::string &utf8) {
    Sci(hwnd_, SCI_SETTEXT, 0, (LPARAM)utf8.c_str());
    docWordsDirty_ = true;
}

bool Editor::Load(const std::wstring &path) {
    std::string text;
    if (!ReadFileUtf8(path, text)) return false;
    Sci(hwnd_, SCI_SETREADONLY, 0);
    SetText(text);
    Sci(hwnd_, SCI_EMPTYUNDOBUFFER, 0, 0);
    Sci(hwnd_, SCI_SETSAVEPOINT, 0, 0);
    Sci(hwnd_, SCI_GOTOPOS, 0, 0);
    ApplyTheme();
    Sci(hwnd_, SCI_COLOURISE, 0, -1);
    return true;
}

bool Editor::Save(const std::wstring &path) {
    if (!WriteFileUtf8(path, GetText())) return false;
    Sci(hwnd_, SCI_SETSAVEPOINT, 0, 0);
    return true;
}

bool Editor::Modified() const {
    return Sci(hwnd_, SCI_GETMODIFY, 0, 0) != 0;
}

void Editor::MarkSaved() {
    Sci(hwnd_, SCI_SETSAVEPOINT, 0, 0);
}

void Editor::SetReadOnly(bool ro) {
    Sci(hwnd_, SCI_SETREADONLY, ro ? 1 : 0);
}

void Editor::GotoLine(int line, bool center) {
    int l = std::max(1, line) - 1;
    int pos = (int)Sci(hwnd_, SCI_POSITIONFROMLINE, l, 0);
    Sci(hwnd_, SCI_GOTOPOS, pos);
    if (center) Sci(hwnd_, SCI_SCROLLCARET, 0, 0);
    SetFocus(hwnd_);
}

int Editor::CurrentLine() const { return (int)Sci(hwnd_, SCI_LINEFROMPOSITION, Sci(hwnd_, SCI_GETCURRENTPOS, 0, 0), 0) + 1; }
int Editor::CurrentCol() const {
    int pos = (int)Sci(hwnd_, SCI_GETCURRENTPOS, 0, 0);
    int line = (int)Sci(hwnd_, SCI_LINEFROMPOSITION, pos, 0);
    return pos - (int)Sci(hwnd_, SCI_POSITIONFROMLINE, line, 0) + 1;
}
int Editor::LineCount() const { return (int)Sci(hwnd_, SCI_GETLINECOUNT, 0, 0); }

void Editor::Zoom(int delta) {
    int z = ZoomLevel() + delta;
    if (z < -8) z = -8;
    if (z > 20) z = 20;
    Sci(hwnd_, SCI_SETZOOM, z, 0);
}

int Editor::ZoomLevel() const { return (int)Sci(hwnd_, SCI_GETZOOM, 0, 0); }

void Editor::ClearMarkers() {
    Sci(hwnd_, SCI_MARKERDELETEALL, 24, 0);
    Sci(hwnd_, SCI_MARKERDELETEALL, 25, 0);
    Sci(hwnd_, SCI_MARKERDELETEALL, 26, 0);
}

void Editor::AddErrorMarker(int line, const std::string &) {
    int l = std::max(1, line) - 1;
    Sci(hwnd_, SCI_MARKERADD, l, 24);
    Sci(hwnd_, SCI_MARKERADD, l, 26);
}

// ---------- 补全 ----------
static bool IsIdentChar(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

// 文档词表：缓存起来，只有文档真的被改动过才重扫。
// 大文件（>=256KB）再加一层时间节流，避免每敲一个字符就全量扫描。
const std::vector<std::string> &Editor::DocWords() {
    if (!docWordsDirty_) return docWords_;
    int len = (int)Sci(hwnd_, SCI_GETLENGTH, 0, 0);
    if (len < 0) len = 0;
    double now = NowMs();
    if (len >= 256 * 1024 && now - docWordsBuiltAt_ < 400.0) return docWords_;
    docWordsBuiltAt_ = now;
    docWordsDirty_ = false;
    docWords_.clear();
    if (len <= 0 || len > 4 * 1024 * 1024) return docWords_;
    const char *buf = (const char *)Sci(hwnd_, SCI_GETCHARACTERPOINTER, 0, 0);
    if (!buf) return docWords_;
    std::set<std::string> uniq;
    int i = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)buf[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
            int s = i;
            while (i < len && IsIdentChar((unsigned char)buf[i])) ++i;
            if (i - s >= 2) uniq.insert(std::string(buf + s, buf + i));
        } else {
            ++i;
        }
    }
    docWords_.assign(uniq.begin(), uniq.end());
    return docWords_;
}

void Editor::TriggerCompletion(bool force) {
    int pos = (int)Sci(hwnd_, SCI_GETCURRENTPOS, 0, 0);
    int start = (int)Sci(hwnd_, SCI_WORDSTARTPOSITION, pos, TRUE);
    std::string prefix;
    if (pos > start) {
        prefix.assign((size_t)(pos - start), '\0');
        Sci_TextRange tr{};
        tr.chrg.cpMin = start; tr.chrg.cpMax = pos;
        tr.lpstrText = &prefix[0];
        Sci(hwnd_, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    }
    ShowCompletionList(prefix, force);
}

void Editor::ShowCompletionList(const std::string &prefix, bool force) {
    if (lang_ != Lang::Cpp) return;

    std::set<std::string> set;
    for (auto &w : Split(kKeywords, ' ')) if (!w.empty()) set.insert(w);
    for (auto &w : Split(kTypes, ' ')) if (!w.empty()) set.insert(w);
    for (auto &w : Split(kFunctions, ' ')) if (!w.empty()) set.insert(w);
    for (auto &s : snippets_) if (!s.trigger.empty()) set.insert(s.trigger);
    // 文档词表里不能留"正在输入的这个词"：它和前缀完全相等，按前缀选中时又总排在
    // 真正的候选（whil 之于 while）前面，于是补全列表一直选中自己 ——
    // 回车后什么都没变，看起来就是"补全没反应"。
    for (auto &w : DocWords()) {
        if (w.empty() || w == prefix) continue;
        set.insert(w);
    }

    std::string list;
    size_t n = 0;
    for (auto &w : set) {
        if (w.empty()) continue;
        list += w;
        list += ' ';
        ++n;
    }
    if (!list.empty()) list.pop_back();
    if (n == 0) return;

    // 前缀为空且非强制：不弹出，避免打扰
    if (prefix.empty() && !force) return;

    Sci(hwnd_, SCI_AUTOCSETIGNORECASE, 1);
    Sci(hwnd_, SCI_AUTOCSHOW, prefix.size(), (LPARAM)list.c_str());
}

const Snippet *Editor::FindSnippet(const std::string &trigger) const {
    for (auto &s : snippets_)
        if (s.trigger == trigger && !s.body.empty()) return &s;
    return nullptr;
}

bool Editor::TryExpandSnippet(const std::string &trigger, int startPos) {
    const Snippet *s = FindSnippet(trigger);
    if (!s) return false;

    int pos = (int)Sci(hwnd_, SCI_GETCURRENTPOS, 0, 0);
    int start = startPos;
    if (start < 0 || start > pos) start = (int)Sci(hwnd_, SCI_WORDSTARTPOSITION, pos, TRUE);
    if (start < 0 || start > pos || start == pos) return false;

    // $0 不是内容，是"展开后光标停哪"的占位符，必须先剔掉
    std::string body = s->body;
    size_t caretOff = std::string::npos;
    size_t z = body.find("$0");
    if (z != std::string::npos) {
        caretOff = z;
        body.erase(z, 2);
    }

    Sci(hwnd_, SCI_BEGINUNDOACTION, 0, 0);
    Sci(hwnd_, SCI_SETTARGETSTART, start, 0);
    Sci(hwnd_, SCI_SETTARGETEND, pos, 0);
    Sci(hwnd_, SCI_REPLACETARGET, (WPARAM)-1, (LPARAM)body.c_str());
    int newPos = start + (int)(caretOff == std::string::npos ? body.size() : caretOff);
    Sci(hwnd_, SCI_GOTOPOS, newPos, 0);
    Sci(hwnd_, SCI_SCROLLCARET, 0, 0);
    Sci(hwnd_, SCI_ENDUNDOACTION, 0, 0);
    return true;
}

// Tab 触发：光标前正好是一个片段触发词时展开（经典 snippet 手感）
bool Editor::ExpandSnippetAtCaret() {
    if (lang_ != Lang::Cpp) return false;
    if (!Sci(hwnd_, SCI_GETSELECTIONEMPTY, 0, 0)) return false;
    int pos = (int)Sci(hwnd_, SCI_GETCURRENTPOS, 0, 0);
    int start = (int)Sci(hwnd_, SCI_WORDSTARTPOSITION, pos, TRUE);
    if (pos <= start) return false;
    std::string word;
    word.assign((size_t)(pos - start), '\0');
    Sci_TextRange tr{};
    tr.chrg.cpMin = start; tr.chrg.cpMax = pos;
    tr.lpstrText = &word[0];
    Sci(hwnd_, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    if (!FindSnippet(word)) return false;
    if (!TryExpandSnippet(word, start)) return false;
    Sci(hwnd_, SCI_AUTOCCANCEL, 0, 0);
    return true;
}

void Editor::HandleCharAdded(int ch) {
    if (lang_ != Lang::Cpp) return;
    if (ch == '\n') {
        // 智能缩进：行尾是 { 则多缩进一级
        int pos = (int)Sci(hwnd_, SCI_GETCURRENTPOS, 0, 0);
        int line = (int)Sci(hwnd_, SCI_LINEFROMPOSITION, pos, 0);
        int prevLen = (int)Sci(hwnd_, SCI_LINELENGTH, line - 1, 0);
        if (prevLen > 0) {
            std::string prev((size_t)prevLen, '\0');
            Sci(hwnd_, SCI_GETLINE, line - 1, (LPARAM)&prev[0]);
            std::string t = Trim(prev);
            if (!t.empty() && (t.back() == '{' || t.back() == '(')) {
                Sci(hwnd_, SCI_TAB, 0, 0);
            }
        }
        return;
    }
    if (IsIdentChar(ch) && !(ch >= '0' && ch <= '9')) {
        TriggerCompletion(false);
    }
}

// ---------- 通知 ----------
void Editor::HandleNotify(SCNotification *n) {
    switch (n->nmhdr.code) {
    case SCN_CHARADDED:
        HandleCharAdded(n->ch);
        break;
    case SCN_UPDATEUI:
        if (onUpdateUi) onUpdateUi();
        break;
    case SCN_MODIFIED:
        if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) docWordsDirty_ = true;
        break;
    case SCN_AUTOCSELECTION: {
        // 选中的正好是片段触发词：
        //  1) 先 SCI_AUTOCCANCEL —— Scintilla 在通知返回后会检查 ac.Active()，
        //     不取消的话它会把我们刚展开的片段再覆盖回触发词（还会吃掉后面的代码）；
        //  2) 用通知里的 position（触发词起点）做替换，不能用 pos - text.size()，
        //     因为列表项（forj）通常比已输入的前缀（fo）长。
        if (n->text && TryExpandSnippet(n->text, (int)n->position))
            Sci(hwnd_, SCI_AUTOCCANCEL, 0, 0);
        break;
    }
    case SCN_MARGINCLICK:
        if (n->margin == 1) {
            int line = (int)Sci(hwnd_, SCI_LINEFROMPOSITION, n->position, 0);
            Sci(hwnd_, SCI_TOGGLEFOLD, line, 0);
        }
        break;
    default:
        break;
    }
}

LRESULT CALLBACK Editor::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR, DWORD_PTR ref) {
    Editor *self = (Editor *)ref;
    switch (msg) {
    case WM_NOTIFY: {
        SCNotification *n = (SCNotification *)lParam;
        if (n && self) self->HandleNotify(n);
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (self) self->TriggerCompletion(true);
            return 0;
        }
        // 片段：触发词 + Tab（补全列表开着时 Tab 是"选中"语义，不要抢）
        if (wParam == VK_TAB && self && !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_SHIFT) & 0x8000) && !Sci(hwnd, SCI_AUTOCACTIVE, 0, 0)) {
            if (self->ExpandSnippetAtCaret()) return 0;
        }
        // 全局快捷键转发给主窗口（编辑器有焦点时 F5/F9/F11/Ctrl+S 等仍要生效）
        if (IsAppShortcut(wParam)) {
            PostMessageW(GetParent(hwnd), WM_KEYDOWN, wParam, lParam);
            return 0;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace fc

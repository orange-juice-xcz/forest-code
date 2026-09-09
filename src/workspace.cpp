// Forest Code - 工作区 / 题目 / 设置实现
#include "workspace.h"
#include "util.h"
#include <algorithm>
#include <cstdio>

namespace fc {

// ================= Settings =================
std::wstring Settings::SettingsPath() {
    return JoinPath(AppDataDir(), L"settings.ini");
}

bool Settings::Load() {
    Ini ini;
    if (!ini.Load(SettingsPath())) {
        if (workspace.empty()) workspace = JoinPath(GetEnv(L"USERPROFILE"), L"ForestCode");
        return false;
    }
    workspace    = U2W(ini.Get("workspace"));
    compiler     = U2W(ini.Get("compiler"));
    stdFlag      = U2W(ini.Get("std", "c++11"));
    compileFlags = U2W(ini.Get("flags", "-O2 -Wall"));
    timeLimitMs  = ini.GetInt("time_limit", 2000);

    uiFont    = U2W(ini.Get("ui_font", "Microsoft YaHei UI"));
    uiSize    = ini.GetInt("ui_size", 9);
    codeFont  = U2W(ini.Get("code_font", "Cascadia Code"));
    codeSize  = ini.GetInt("code_size", 11);
    tabWidth  = ini.GetInt("tab_width", 4);
    showWhitespace = ini.GetBool("show_ws", false);
    autoSave  = ini.GetBool("auto_save", true);

    lastFile = U2W(ini.Get("last_file"));
    recent.clear();
    for (auto &s : Split(ini.Get("recent"), '|')) {
        if (!s.empty()) recent.push_back(U2W(s));
    }
    if (workspace.empty()) workspace = JoinPath(GetEnv(L"USERPROFILE"), L"ForestCode");
    if (compiler.empty()) compiler = Workspace::DetectCompiler();
    return true;
}

bool Settings::Save() const {
    Ini ini;
    ini.Set("workspace", W2U(workspace));
    ini.Set("compiler", W2U(compiler));
    ini.Set("std", W2U(stdFlag));
    ini.Set("flags", W2U(compileFlags));
    ini.SetInt("time_limit", timeLimitMs);

    ini.Set("ui_font", W2U(uiFont));
    ini.SetInt("ui_size", uiSize);
    ini.Set("code_font", W2U(codeFont));
    ini.SetInt("code_size", codeSize);
    ini.SetInt("tab_width", tabWidth);
    ini.SetBool("show_ws", showWhitespace);
    ini.SetBool("auto_save", autoSave);

    ini.Set("last_file", W2U(lastFile));
    std::string rec;
    for (size_t i = 0; i < recent.size() && i < 20; ++i) {
        if (i) rec += "|";
        rec += W2U(recent[i]);
    }
    ini.Set("recent", rec);
    return ini.Save(SettingsPath());
}

// ================= 编译器探测 =================
std::wstring Workspace::DetectCompiler() {
    const wchar_t *cands[] = {
        L"C:\\Qt\\Tools\\mingw1310_64\\bin\\g++.exe",
        L"C:\\msys64\\mingw64\\bin\\g++.exe",
        L"C:\\msys64\\ucrt64\\bin\\g++.exe",
        L"C:\\MinGW\\bin\\g++.exe",
        L"C:\\mingw64\\bin\\g++.exe",
        L"C:\\Program Files\\mingw-w64\\mingw64\\bin\\g++.exe",
        L"C:\\Program Files (x86)\\Dev-Cpp\\MinGW64\\bin\\g++.exe",
        L"D:\\MinGW\\bin\\g++.exe",
        L"D:\\msys64\\mingw64\\bin\\g++.exe",
    };
    for (auto c : cands) if (PathExists(c)) return c;
    // 在 PATH 里找
    wchar_t buf[32768]{};
    DWORD n = SearchPathW(nullptr, L"g++.exe", nullptr, 32768, buf, nullptr);
    if (n) return buf;
    return L"g++";
}

// ================= 片段库 =================
std::vector<Snippet> Workspace::BuiltinSnippets() {
    std::vector<Snippet> s;
    auto add = [&](const char *t, const char *b, const char *d) {
        s.push_back(Snippet{ t, b, d, true });
    };

    add("main", "#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    ios::sync_with_stdio(false);\n    cin.tie(nullptr);\n    $0\n    return 0;\n}\n", "竞赛主模板");
    add("fastio", "ios::sync_with_stdio(false);\ncin.tie(nullptr);\n", "关闭同步，加速 IO");
    add("fori", "for (int i = 0; i < n; ++i) {\n    $0\n}\n", "下标循环 i");
    add("forj", "for (int j = 0; j < m; ++j) {\n    $0\n}\n", "下标循环 j");
    add("fork", "for (int k = 0; k < n; ++k) {\n    $0\n}\n", "下标循环 k");
    add("forr", "for (int i = n - 1; i >= 0; --i) {\n    $0\n}\n", "倒序循环");
    add("foreach", "for (auto &x : v) {\n    $0\n}\n", "范围 for");
    add("rep", "for (int _ = 0; _ < n; ++_) {\n    $0\n}\n", "重复 n 次");
    add("vec", "vector<int> v;\n", "定义 vector");
    add("vec2", "vector<vector<int>> g(n, vector<int>(m));\n", "二维数组");
    add("arr", "int a[n];\n", "定义数组");
    add("pq", "priority_queue<int> pq;\n", "大根堆");
    add("pqmin", "priority_queue<int, vector<int>, greater<int>> pq;\n", "小根堆");
    add("map", "map<int, int> mp;\n", "map");
    add("umap", "unordered_map<int, int> mp;\n", "unordered_map");
    add("set", "set<int> st;\n", "set");
    add("pair", "pair<int, int> p;\n", "pair");
    add("sortv", "sort(v.begin(), v.end());\n", "排序");
    add("sortc", "sort(v.begin(), v.end(), [](const auto &a, const auto &b) {\n    return a < b;\n});\n", "自定义排序");
    add("uniq", "sort(v.begin(), v.end());\nv.erase(unique(v.begin(), v.end()), v.end());\n", "排序去重");
    add("lb", "int p = lower_bound(v.begin(), v.end(), x) - v.begin();\n", "二分查找 lower_bound");
    add("ub", "int p = upper_bound(v.begin(), v.end(), x) - v.begin();\n", "二分查找 upper_bound");
    add("bs", "int lo = 0, hi = n;\nwhile (lo < hi) {\n    int mid = lo + (hi - lo) / 2;\n    if (check(mid)) hi = mid;\n    else lo = mid + 1;\n}\n", "整数二分");
    add("gcd", "int g = __gcd(a, b);\n", "最大公约数");
    add("powmod", "long long qpow(long long a, long long b, long long mod) {\n    long long r = 1 % mod;\n    for (a %= mod; b; b >>= 1, a = a * a % mod)\n        if (b & 1) r = r * a % mod;\n    return r;\n}\n", "快速幂");
    add("dsu", "struct DSU {\n    vector<int> fa, sz;\n    DSU(int n) : fa(n + 1), sz(n + 1, 1) { iota(fa.begin(), fa.end(), 0); }\n    int find(int x) { return fa[x] == x ? x : fa[x] = find(fa[x]); }\n    bool unite(int a, int b) {\n        a = find(a); b = find(b);\n        if (a == b) return false;\n        if (sz[a] < sz[b]) swap(a, b);\n        fa[b] = a; sz[a] += sz[b];\n        return true;\n    }\n};\n", "并查集");
    add("dij", "vector<long long> dist(n + 1, LLONG_MAX);\ndist[1] = 0;\npriority_queue<pair<long long, int>, vector<pair<long long, int>>, greater<>> pq;\npq.push({0, 1});\nwhile (!pq.empty()) {\n    auto [d, u] = pq.top(); pq.pop();\n    if (d > dist[u]) continue;\n    for (auto [v, w] : g[u]) {\n        if (dist[u] + w < dist[v]) {\n            dist[v] = dist[u] + w;\n            pq.push({dist[v], v});\n        }\n    }\n}\n", "Dijkstra");
    add("seg", "struct SegTree {\n    int n;\n    vector<long long> t, lz;\n    SegTree(int n_) : n(n_), t(4 * n_), lz(4 * n_) {}\n    void push(int p, int l, int r) {\n        if (!lz[p]) return;\n        int m = (l + r) >> 1;\n        t[p << 1] += lz[p] * (m - l + 1); lz[p << 1] += lz[p];\n        t[p << 1 | 1] += lz[p] * (r - m); lz[p << 1 | 1] += lz[p];\n        lz[p] = 0;\n    }\n    void add(int p, int l, int r, int L, int R, long long v) {\n        if (L <= l && r <= R) { t[p] += v * (r - l + 1); lz[p] += v; return; }\n        push(p, l, r);\n        int m = (l + r) >> 1;\n        if (L <= m) add(p << 1, l, m, L, R, v);\n        if (R > m) add(p << 1 | 1, m + 1, r, L, R, v);\n        t[p] = t[p << 1] + t[p << 1 | 1];\n    }\n    long long query(int p, int l, int r, int L, int R) {\n        if (L <= l && r <= R) return t[p];\n        push(p, l, r);\n        int m = (l + r) >> 1;\n        long long res = 0;\n        if (L <= m) res += query(p << 1, l, m, L, R);\n        if (R > m) res += query(p << 1 | 1, m + 1, r, L, R);\n        return res;\n    }\n};\n", "线段树（区间加 / 区间和）");
    add("bit", "struct BIT {\n    int n; vector<long long> t;\n    BIT(int n_) : n(n_), t(n_ + 1) {}\n    void add(int i, long long v) { for (; i <= n; i += i & -i) t[i] += v; }\n    long long sum(int i) { long long r = 0; for (; i > 0; i -= i & -i) r += t[i]; return r; }\n    long long range(int l, int r) { return sum(r) - sum(l - 1); }\n};\n", "树状数组");
    add("bfs", "queue<int> q;\nq.push(1);\nvector<int> dist(n + 1, -1);\ndist[1] = 0;\nwhile (!q.empty()) {\n    int u = q.front(); q.pop();\n    for (int v : g[u]) if (dist[v] < 0) {\n        dist[v] = dist[u] + 1;\n        q.push(v);\n    }\n}\n", "BFS");
    add("dfs", "void dfs(int u, int fa) {\n    for (int v : g[u]) {\n        if (v == fa) continue;\n        dfs(v, u);\n    }\n}\n", "DFS");
    add("dbg", "#ifdef LOCAL\n#define dbg(x) cerr << #x << \" = \" << (x) << endl\n#else\n#define dbg(x)\n#endif\n", "调试输出宏");
    add("mod", "const int MOD = 1e9 + 7;\n", "取模常量");
    add("inf", "const int INF = 0x3f3f3f3f;\nconst long long LINF = 0x3f3f3f3f3f3f3f3fLL;\n", "无穷大");
    add("endl", "\n", "换行");
    return s;
}

std::string Workspace::DefaultTemplate() {
    return "#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    ios::sync_with_stdio(false);\n    cin.tie(nullptr);\n\n    return 0;\n}\n";
}

// ================= Workspace =================
std::wstring Workspace::ProblemsDir() const { return JoinPath(settings.workspace, L"problems"); }
std::wstring Workspace::ScratchDir()  const { return JoinPath(settings.workspace, L"scratch"); }
std::wstring Workspace::BuildDir()    const { return JoinPath(settings.workspace, L".build"); }

static std::wstring ReadMetaField(const std::wstring &dir, const char *key) {
    Ini ini;
    if (!ini.Load(JoinPath(dir, L"meta.ini"))) return {};
    return U2W(ini.Get(key));
}

bool Workspace::LoadAll() {
    settings.Load();
    if (settings.compiler.empty() || !PathExists(settings.compiler))
        settings.compiler = DetectCompiler();
    MakeDirs(settings.workspace);
    MakeDirs(BuildDir());          // 只建编译产物目录；其余目录由用户自己决定
    return RescanProblems();
}

void Workspace::ReloadProblem(Problem *p) {
    if (!p) return;
    Problem fresh;
    fresh.dir = p->dir;
    fresh.title = p->title;
    fresh.source = p->source;
    fresh.difficulty = p->difficulty;
    fresh.tags = p->tags;
    fresh.statementPath = p->statementPath;
    fresh.solutions = p->solutions;
    fresh.tests = p->tests;
    // 重新扫描
    *p = fresh;
}

bool Workspace::RescanProblems() {
    // 保留侧栏展开状态
    std::vector<std::pair<std::wstring, std::pair<bool, bool>>> exp;
    for (auto &p : problems) exp.push_back({ p.dir, { p.expanded, p.testsExpanded } });

    problems.clear();
    if (!IsDir(ProblemsDir())) return true;

    for (auto &name : ListDir(ProblemsDir())) {
        std::wstring dir = JoinPath(ProblemsDir(), name);
        if (!IsDir(dir)) continue;

        Problem p;
        p.dir = dir;
        p.title = ReadMetaField(dir, "title");
        p.source = ReadMetaField(dir, "source");
        p.difficulty = ReadMetaField(dir, "difficulty");
        p.tags = ReadMetaField(dir, "tags");

        // 题面
        const wchar_t *stmtNames[] = { L"problem.md", L"statement.md", L"题面.md", L"README.md" };
        for (auto s : stmtNames) {
            std::wstring f = JoinPath(dir, s);
            if (PathExists(f)) { p.statementPath = f; break; }
        }

        // 解法
        for (auto &f : ListDir(dir)) {
            std::wstring full = JoinPath(dir, f);
            if (IsDir(full)) continue;
            if (FileExt(full) == L".cpp" || FileExt(full) == L".cc" || FileExt(full) == L".cxx") {
                p.solutions.push_back(Solution{ full, FileStem(full) });
            }
        }

        // 测试用例
        std::wstring testsDir = JoinPath(dir, L"tests");
        if (IsDir(testsDir)) {
            std::vector<std::wstring> ins;
            for (auto &f : ListDir(testsDir))
                if (FileExt(f) == L".in") ins.push_back(f);
            std::sort(ins.begin(), ins.end());
            for (auto &f : ins) {
                TestCase tc;
                tc.inPath = JoinPath(testsDir, f);
                std::wstring base = FileStem(f);
                tc.outPath = JoinPath(testsDir, base + L".out");
                tc.hasExpected = PathExists(tc.outPath);
                ReadFileUtf8(tc.inPath, tc.inText);
                if (tc.hasExpected) ReadFileUtf8(tc.outPath, tc.outText);
                p.tests.push_back(tc);
            }
        }
        problems.push_back(std::move(p));
    }
    for (auto &p : problems) {
        for (auto &e : exp)
            if (e.first == p.dir) { p.expanded = e.second.first; p.testsExpanded = e.second.second; }
    }
    return true;
}

Problem *Workspace::Find(const std::wstring &dir) {
    for (auto &p : problems) if (p.dir == dir) return &p;
    return nullptr;
}

Problem *Workspace::FindByFile(const std::wstring &file) {
    for (auto &p : problems)
        for (auto &s : p.solutions)
            if (s.path == file) return &p;
    return nullptr;
}

Problem *Workspace::CreateProblem(const std::wstring &title, const std::wstring &source,
                                  const std::wstring &difficulty, const std::wstring &tags,
                                  const std::wstring &templateCode, bool withTemplate) {
    // 目录名：清洗非法字符
    std::wstring safe;
    for (wchar_t ch : title) {
        if (wcschr(L"\\/:*?\"<>|", ch)) safe += L'_';
        else safe += ch;
    }
    if (safe.empty()) safe = L"新题目";
    std::wstring dir = JoinPath(ProblemsDir(), safe);
    int n = 2;
    while (PathExists(dir)) dir = JoinPath(ProblemsDir(), safe + L" (" + std::to_wstring(n++) + L")");
    MakeDirs(dir);
    MakeDirs(JoinPath(dir, L"tests"));

    Ini meta;
    meta.Set("title", W2U(title));
    meta.Set("source", W2U(source));
    meta.Set("difficulty", W2U(difficulty));
    meta.Set("tags", W2U(tags));
    meta.Save(JoinPath(dir, L"meta.ini"));

    if (!PathExists(JoinPath(dir, L"problem.md")))
        WriteFileUtf8(JoinPath(dir, L"problem.md"), "# " + W2U(title) + "\n\n## 题目描述\n\n\n## 输入格式\n\n\n## 输出格式\n\n\n## 样例\n\n```\n\n```\n");

    if (withTemplate) {
        std::string code = templateCode.empty() ? DefaultTemplate() : W2U(templateCode);
        WriteFileUtf8(JoinPath(dir, L"sol_1.cpp"), code);
    }

    // 一个空测试
    WriteFileUtf8(JoinPath(dir, L"tests\\1.in"), "");
    WriteFileUtf8(JoinPath(dir, L"tests\\1.out"), "");

    RescanProblems();
    return Find(dir);
}

Solution *Workspace::CreateSolution(Problem *p, const std::wstring &name, const std::wstring &code) {
    if (!p) return nullptr;
    std::wstring safe;
    for (wchar_t ch : name) safe += (wcschr(L"\\/:*?\"<>|", ch) ? L'_' : ch);
    if (safe.empty()) safe = L"sol";
    std::wstring path = JoinPath(p->dir, safe + L".cpp");
    int n = 2;
    while (PathExists(path)) path = JoinPath(p->dir, safe + L"_" + std::to_wstring(n++) + L".cpp");
    WriteFileUtf8(path, W2U(code));
    p->solutions.push_back(Solution{ path, FileStem(path) });
    return &p->solutions.back();
}

TestCase *Workspace::AddTest(Problem *p) {
    if (!p) return nullptr;
    std::wstring testsDir = JoinPath(p->dir, L"tests");
    MakeDirs(testsDir);
    int idx = 1;
    for (;; ++idx) {
        std::wstring in = JoinPath(testsDir, std::to_wstring(idx) + L".in");
        if (!PathExists(in)) break;
    }
    TestCase tc;
    tc.inPath = JoinPath(testsDir, std::to_wstring(idx) + L".in");
    tc.outPath = JoinPath(testsDir, std::to_wstring(idx) + L".out");
    WriteFileUtf8(tc.inPath, "");
    WriteFileUtf8(tc.outPath, "");
    p->tests.push_back(tc);
    return &p->tests.back();
}

bool Workspace::SaveTest(const Problem &, int index) {
    (void)index;
    return true;
}

bool Workspace::DeleteTest(Problem *p, int index) {
    if (!p || index < 0 || index >= (int)p->tests.size()) return false;
    DeleteFileSafe(p->tests[index].inPath);
    DeleteFileSafe(p->tests[index].outPath);
    p->tests.erase(p->tests.begin() + index);
    return true;
}

bool Workspace::DeleteSolution(Problem *p, const std::wstring &path) {
    if (!p) return false;
    for (size_t i = 0; i < p->solutions.size(); ++i) {
        if (p->solutions[i].path == path) {
            DeleteFileSafe(path);
            p->solutions.erase(p->solutions.begin() + i);
            return true;
        }
    }
    return false;
}

bool Workspace::RenameProblem(Problem *p, const std::wstring &newTitle) {
    if (!p) return false;
    p->title = newTitle;
    Ini meta;
    meta.Load(JoinPath(p->dir, L"meta.ini"));
    meta.Set("title", W2U(newTitle));
    return meta.Save(JoinPath(p->dir, L"meta.ini"));
}

// ================= 诊断解析 =================
std::vector<DiagItem> ParseDiagnostics(const std::string &log) {
    std::vector<DiagItem> out;
    for (auto &line : Split(log, '\n')) {
        std::string l = Trim(line);
        if (l.empty()) continue;
        // 形如: D:\a\b.cpp:12:5: error: message
        size_t c1 = l.find(": error: ");
        size_t c2 = l.find(": warning: ");
        size_t c3 = l.find(": note: ");
        size_t col = std::string::npos;
        bool isErr = false;
        if (c1 != std::string::npos) { col = c1; isErr = true; }
        else if (c2 != std::string::npos) { col = c2; }
        else if (c3 != std::string::npos) { col = c3; }
        if (col == std::string::npos) continue;

        std::string head = l.substr(0, col);
        std::string msg = l.substr(col + 2);
        // head: file:line:col
        auto parts = Split(head, ':');
        if (parts.size() < 3) continue;
        DiagItem d;
        d.col = atoi(parts.back().c_str());
        parts.pop_back();
        d.line = atoi(parts.back().c_str());
        parts.pop_back();
        std::string file;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) file += ":";
            file += parts[i];
        }
        d.file = U2W(file);
        d.isError = isErr;
        d.message = msg;
        if (d.line > 0) out.push_back(d);
    }
    return out;
}

} // namespace fc

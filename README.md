# Forest Code · 竞赛工作台

> 一个为算法竞赛选手手写的**超轻量 C++ IDE**。原生 Win32，17 MB 内存，双击即开。

![Forest Code](preview.png)

---

## 为什么会有它

用 Dev-C++：补全弱、界面丑。用 VS：太重、启动慢、后台吃内存。
竞赛选手真正需要的其实很窄：**写代码 → 编译 → 跑样例 → 对拍**，外加**题目和多种解法的管理**。

Forest Code 只做这件事，并且把它做舒服。

- **启动快**：原生 C++ 直接编译成单个 exe，无 Electron / 无 WebView / 无运行时依赖
- **占用低**：实测常驻内存 **约 18 MB**（VS Code 同场景通常 300 MB+）
- **不占后台**：关窗即退出，没有守护进程

---

## 功能

### 编辑
- Scintilla 编辑器内核，C++ 语法高亮、代码折叠、行号、缩进参考线、当前行高亮、括号匹配
- **代码补全**：C++ 关键字 / STL 容器与算法 / 内置竞赛片段 / 当前文件词，输入即弹（可 `Ctrl+Space` 手动唤出）
- **片段库**：输入 `fori`、`dsu`、`dijkstra`、`segtree`、`fastio` 等触发词自动展开成完整代码
- 拖拽文件到窗口即可打开，支持命令行传文件路径

### 题目管理（一题多解）
工作区就是普通文件夹，**不需要导入、不锁格式**，你随时能用别的编辑器改：

```
<工作区>/
  problems/
    1001 A+B Problem/
      meta.ini              # 标题 / 来源 / 难度 / 标签
      problem.md            # 题面
      sol_1_直接相加.cpp     # 解法一
      sol_2_快速读入.cpp     # 解法二（一题多解）
      tests/
        1.in  1.out         # 测试用例
        2.in  2.out
  scratch/main.cpp          # 随手写草稿的地方
  .build/                   # 编译产物
```

侧栏树里直接展开题目 → 看题面 / 切换解法 / 管理用例。

### 测试用例与对拍
- 多组用例集中管理，输入 / 期望输出 / 实际输出三栏对照
- **F6 一键跑全部用例**，逐条给出 `通过 / 答案错误 / 超时` 与耗时
- 从剪贴板粘贴输入、自动保存用例到磁盘
- 超时自动 kill 进程树（Job Object），不会留下僵尸进程

### 运行
- 编译错误逐条解析，在编辑器里标红出错行
- 编译日志 / 运行输出分标签页查看
- 可配置 C++ 标准（c++11/14/17/20）、编译选项、时间限制、编译器路径

---

## 快捷键

| 键 | 作用 |
|---|---|
| `F11` | 编译并运行（最常用） |
| `F9` | 仅编译 |
| `F5` | 仅运行（不重新编译） |
| `F6` | 运行全部测试用例 |
| `Ctrl+S` / `Ctrl+Shift+S` | 保存 / 全部保存 |
| `Ctrl+Space` | 手动唤出补全 |
| `Ctrl+N` | 新建题目 |
| `Ctrl+Shift+T` | 新增测试用例 |
| `Ctrl+B` | 收起 / 展开底部面板 |
| `Ctrl+ +/-` | 编辑器字号缩放 |
| `Ctrl+Tab` | 切换标签页 |
| `F1` | 关于 |

---

## 构建

只需要 **MinGW g++ 9+**（GCC 13 已验证）和 PowerShell。

```powershell
# 首次构建会自动下载 Scintilla 566 + Lexilla 553 源码到 third_party/
powershell -File build.ps1

# 指定自己的 g++
powershell -File build.ps1 -GXX "C:\msys64\mingw64\bin\g++.exe"

# 清理重建
powershell -File build.ps1 -Clean

# 构建并启动
powershell -File build.ps1 -Run
```

产物：`build\forest-code.exe`（约 2.3 MB，静态链接，可单独拷走）。

---

## 代码结构（改哪里）

| 文件 | 职责 |
|---|---|
| `src/theme.h/.cpp` | **配色 / 度量 / 字体 / 绘图辅助**，想换主题改这里 |
| `src/app.h/.cpp` | 主窗口：自定义无边框标题栏、布局、自绘工具栏 / 侧栏 / 标签栏 / 状态栏 |
| `src/commands.cpp` | 全部命令与鼠标键盘交互：编译运行、题目增删、用例管理、分栏拖拽 |
| `src/dialogs.cpp` | 自绘表单对话框（设置、新建题目） |
| `src/editor.h/.cpp` | Scintilla 封装：主题化、补全引擎、片段展开、错误标记 |
| `src/runner.h/.cpp` | 编译 / 运行引擎：管道捕获、超时 kill、Job Object 隔离 |
| `src/workspace.h/.cpp` | 工作区 / 题目模型、设置持久化、内置片段库、编译诊断解析 |
| `src/util.h/.cpp` | UTF-8/UTF-16、路径、INI、剪贴板等基础工具 |
| `tools/shot.ps1` | 开发期窗口截图工具（用于视觉验收） |

---

## 技术选型

- **界面**：纯 Win32 + GDI/GDI+ 自绘，Per-Monitor V2 高 DPI 适配，无第三方 UI 框架
- **编辑器**：静态链接 [Scintilla](https://www.scintilla.org/) 566 + [Lexilla](https://www.scintilla.org/LexillaDoc.html) 553（HPND 许可）
- **编译器**：任意 g++，通过 `CreateProcess` 调用，stdout/stderr 用管道异步捕获
- **无外部依赖**：不依赖 .NET / WebView2 / Electron / Python

配色以深绿 `#0A150F` 为底、薄荷绿 `#4ED17E` 为强调色，长时间盯屏幕不刺眼。

---

## License

MIT。Scintilla / Lexilla 遵循各自的 HPND 许可。

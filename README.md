# SnapLens

> ⚡ 一键截图 · 即时识别 · AI 翻译 — 纯 C++ Qt6 桌面工具

Windows 桌面工具：**截图 + OCR 文字识别 + AI 翻译**，功能对标 QQ 截图（不含画笔标注）。
本仓库为 **Python 原版（PySide6）的完整 C++ 重构版**，单进程、静态链接，无 Python 运行时依赖。

启动后常驻系统托盘，支持两种使用模式，可随时切换：
- **翻译模式** — 启动即显示文本翻译窗口，适合日常翻译场景
- **截图模式** — 后台静默运行，适合以截图为主的用户

---

## 功能总览

### 截图

- **全局热键**：默认 `Ctrl+Shift+Z`，可在设置中修改
- **框选**：按住左键拖动选定区域
- **窗口点选**：单击自动拾取窗口边界，空白处单击为全屏
- **多屏支持**：每屏独立覆盖层（基于 DWM 可见边框，混合 DPI 物理坐标）

### 十字准星与信息标签

截图时显示全屏十字线，光标附近实时显示坐标标签与像素颜色标签（RGB / Hex 可切换）。

### 像素放大镜

- 倍率 4.0× ~ 20.0×，滚轮实时调节，动画过渡
- 像素网格、边缘策略（裁剪/填充）、倍率/坐标/颜色标签均可配置

### 截图结果操作

| 按钮 | 操作 |
|------|------|
| 保存 | 存为 PNG / JPG / BMP |
| 钉图 | 置顶窗口，可拖动/滚轮缩放/右键菜单 |
| OCR | 文字识别（Tesseract 5.5，进程内调用） |
| AI 翻译 | 发送到 AI 翻译窗口 |
| 确认 | 复制到剪贴板 |

`双击` / `回车` 直接复制并退出；`Esc` 取消；截图模式按 `C` 复制光标下像素颜色。

### OCR 文字识别

Tesseract 5.5（LSTM 引擎）**静态链接**，进程内调用，无外部 DLL / 无 pytesseract 依赖。
支持中英日韩等多语言，语言包可在设置对话框在线下载，缺失时自动降级英文并记录日志。

### AI 翻译

- **截图翻译**：选区 → OCR 提取文字 → AI 翻译 → 显示结果（含 OCR 原文 / 译文 / AI 思考三页签）
- **文本翻译**：主窗口输入文本直接翻译，支持多场景（通用 / IT / 医学 / 金融 / 法律 / 学术 / 文学）
- 流式输出：结果逐 token 实时显示，"AI 思考"可选
- 支持 8 个 OpenAI 兼容服务商：DeepSeek（默认）/ OpenAI / 通义千问 / Kimi / GLM / 混元 / 豆包 / 文心一言
- 设置中可自动获取模型列表（异步，不阻塞 UI），也可手动输入
- 内置参数校验：未配置 Key / 模型 / 地址时本地拦截并给出明确错误提示

### 钉图窗口

无边框置顶，可拖动、滚轮缩放（0.05× ~ 10×）、右键复制 / 另存为 / 关闭。

### 日志系统

统一格式 `[snap LEVEL file:line function] msg`（qDebug / qInfo / qWarning / qCritical），
设置中可按 DEBUG / INFO / WARNING / ERROR 四级独立开关。

---

## 技术架构

纯 C++ Qt6 单进程应用，分层清晰：

```
main.cpp → AppController（应用编排/托盘/热键/生命周期）
    ├── capture/   截图会话（SnipSession）· 覆盖层（Overlay）· 多屏抓取
    ├── ocr/       OCR 引擎（Tesseract 5.5 静态）· OCR 窗口（后台线程）
    ├── translate/ AI 客户端（Qt Network，OpenAI 兼容）· 翻译线程 · 翻译/主窗口
    ├── platform/  平台抽象（接口 + 工厂 + Null 降级）
    │              ├── 全局热键（RegisterHotKey）· Esc 拦截（WH_KEYBOARD_LL）
    │              ├── 窗口枚举（DWM）· 光标/裁剪
    ├── notify/    通知系统（托盘 / 弹窗 / 日志三通道，注册表驱动）
    ├── ui/        设置对话框 · 引导向导 · 托盘 · 钉图 · 缩放视图 · 取色器
    └── log/       统一日志（qDebug + Qt 消息处理器）
```

### 设计亮点

- **平台抽象层**：5 个接口（热键/窗口/光标/Esc 拦截/裁剪）+ 工厂 + Null 降级，为跨平台铺路
- **表驱动设置**：`SETTING_LIST` X-Macro 单一来源生成成员/读写桥接，新增设置项只改两处
- **表驱动重置**：`buildResetTable()` 一张表驱动所有"恢复本页默认"
- **配置快照**：翻译线程构造时一次性拷贝配置，与主线程设置修改彻底解耦（无线程竞争）
- **关窗即取消**：关闭翻译窗口立即 `abort` 网络请求并断开连接（省 token，零 UI 卡顿）
- **异步模型获取**：设置页获取模型列表不阻塞 UI，代次 + 可见性双防护防过期回填

---

## 环境要求

- Windows 10 1809+（64 位）
- **Qt 6.11.x**（MSVC 2022 64-bit，`D:/ProgramFiles/Qt/6.11.1/msvc2022_64` 或修改 `CMakeLists.txt` 的 `QT_ROOT`）
- **Visual Studio 2022**（MSVC 14.5x）
- **CMake 3.20+**（推荐 Ninja 生成器）
- **Tesseract 5.5 SDK**（`sdk/tesseract/`，含 include/lib/tessdata，不在源码仓库中，见下节）

## 准备 OCR 依赖（Tesseract 5.5 SDK）

> `sdk/` 已被 `.gitignore` 排除（第三方依赖不入库）。本项目要求 **Tesseract 5.x 静态库（Release, x64, MSVC）**，
> 两种获取方式任选其一。`CMakeLists.txt` 通过 `sdk/tesseract/{include,lib,tessdata}` 三处固定路径引用。

### 方式一：使用预编译静态 SDK（当前项目所用）

SDK 目录布局需与 `CMakeLists.txt` 的引用完全一致：

```
sdk/tesseract/
├── include/                     # 头文件（编译时）
│   ├── tesseract/               # baseapi.h / capi.h 等（CMake 追加 include/tesseract）
│   └── leptonica/               # allheaders.h 等（CMake 追加 include/leptonica）
├── lib/                         # 静态链接库（编译时，Release x64）
│   ├── tesseract55.lib          # CMake 链接名：tesseract55
│   ├── leptonica-1.87.0.lib     # CMake 链接名：leptonica-1.87.0
│   └── 依赖库：libpng16 / jpeg / tiff / libwebp(+decoder/demux/mux) /
│              libsharpyuv / gif / openjp2 / zs / zstd / lz4 / lzma / bz2 /
│              libcurl / libssl / libcrypto / archive / turbojpeg
└── tessdata/                     # 语言包（运行时，见"语言包"小节）
    ├── chi_sim.traineddata
    └── eng.traineddata
```

> `CMakeLists.txt` 中 `target_link_libraries` 的链接名以**库文件实际文件名**为准，
> 库名与清单不一致时需同步修改该列表（见方式二第 5 步）。

### 方式二：用 vcpkg 静态编译（从源码构建）

1. **安装 vcpkg 并配置 VS2022 C++ 工具链**（若已有可跳过）：
   ```bash
   git clone https://github.com/microsoft/vcpkg
   cd vcpkg && .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   ```

2. **安装 Tesseract 静态库**（`x64-windows-static` triplet 产出纯静态库）：
   ```bash
   # 基础（tesseract + leptonica + 图像解码库：png/jpeg/tiff/webp/gif/openjpeg/zlib）
   .\vcpkg install "tesseract:x64-windows-static"
   # 若 CMakeLists.txt 链接了 archive（libarchive）与 curl（libcurl），则：
   .\vcpkg install "tesseract[archive,curl]:x64-windows-static"
   ```
   可用 `.\vcpkg list` 查看实际安装的库与文件名，`.\vcpkg search tesseract` 查看可用 features。

3. **产物位置**（vcpkg 安装目录）：
   ```
   <vcpkg>/installed/x64-windows-static/
   ├── include/          # tesseract/、leptonica/ 子目录头文件
   ├── lib/              # tesseract.lib、leptonica.lib 及全部依赖 .lib
   └── share/tessdata/   # 若有 tools feature 附带语言包（通常需手动下载）
   ```

4. **复制到项目 `sdk/tesseract/`**：
   ```bash
   # include：保持 tesseract/、leptonica/ 子目录结构（与 CMake include 路径一致）
   xcopy /E <vcpkg>\installed\x64-windows-static\include sdk\tesseract\include
   # lib：复制全部静态库
   xcopy /E <vcpkg>\installed\x64-windows-static\lib sdk\tesseract\lib
   ```

5. **库名适配**（重要）：vcpkg 产出的库名为 `tesseract.lib`、`leptonica.lib`（不带版本号），
   与 `CMakeLists.txt` 现有的 `tesseract55` / `leptonica-1.87.0` 不同。二选一：
   - 简单方式：把 `lib/` 下的 `tesseract.lib`/`leptonica.lib` 复制为对应名称；
   - 推荐方式：以 vcpkg 实际文件名为准，**同步修改 `CMakeLists.txt` 的 `target_link_libraries` 列表**（删除 `tesseract55` 等不存在的名字，保留 vcpkg 产物名），避免同名不同版本混淆。

6. **链接注意事项**：
   - 若链接报 `vcomp140.lib`/`vcomp140d` 相关错误，说明 tesseract 以 OpenMP 构建，需在 `CMakeLists.txt` 加 `/openmp` 或改用不带 OpenMP 的配置；
   - 保证全部依赖库为同一 triplet（`x64-windows-static`），混用动态/静态库会导致链接失败。

### 语言包（tessdata）

语言包 `*.traineddata` 需单独准备，放置于 **`sdk/tesseract/tessdata/`**（运行时查找路径）：
- 下载：`https://github.com/tesseract-ocr/tessdata`（建议 `tessdata_fast` 版，体积小、速度优；中文 `chi_sim` + 英文 `eng` 为最小集）；
- 或在程序内设置对话框 →「OCR 识别」页在线下载（下载到自动检测目录或自定义 `ocr_tessdata_dir`）；
- 缺失语言包时程序自动降级英文并记录日志。

## 构建

```bash
# CLion 打开项目根目录（CMakeLists.txt）或命令行：
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

- 编译选项：C++17、MSVC `/W4 /permissive- /utf-8`、静态运行时 `/MT`（免 vc_redist）
- Tesseract 5.5 静态链接（含 png/jpeg/tiff/webp/curl/ssl 等全部依赖），产物不依赖任何 Tesseract DLL
- 构建后自动复制 tessdata 语言包并同步 exe 到 `dist/`（见 CMakeLists POST_BUILD）
- 新增/删除源文件无需手动重新 configure（`CONFIGURE_DEPENDS` 自动感知）

## 打包发布

```bash
# 1. 构建（POST_BUILD 已自动完成两步：同步 exe 到 dist/ + 复制语言包到 dist/sdk/tesseract/tessdata）
cmake --build cmake-build-release --config Release
# 2. 补全 Qt 运行时依赖
windeployqt --dir dist --release --no-translations --no-system-d3d-compiler \
            --no-opengl-sw --no-compiler-runtime dist/SnapLens.exe
```

发布目录结构：

```
dist/
├── SnapLens.exe
├── Qt6Core.dll / Qt6Gui.dll / Qt6Widgets.dll / Qt6Network.dll / Qt6Svg.dll
├── platforms/qwindows.dll
├── styles/ · imageformats/ · iconengines/ · networkinformation/ · tls/
│   └── tls/qschannelbackend.dll      # Windows 原生 TLS（无需 OpenSSL）
└── sdk/tesseract/tessdata/           # 语言包（POST_BUILD 自动复制）
    ├── chi_sim.traineddata
    └── eng.traineddata
```

- **语言包放置位置**：`dist/sdk/tesseract/tessdata/`（构建时由 CMake POST_BUILD 从 `sdk/tesseract/tessdata/` 自动复制，新增语言包后重新构建即可同步）；
- 运行时 `findTessdataDir()` 按 `exe 同级 sdk/tesseract/tessdata → exe 同级 tesseract/tessdata → exe 同级 tessdata → 系统 Tesseract-OCR 安装目录` 顺序查找，整个 `dist/` 目录保持此布局即可；
- 若设置了自定义 `ocr_tessdata_dir`（设置 → OCR 识别页），语言包也可放置到自定义目录，程序优先使用。

**依赖说明**：

| 依赖 | 需要？ | 原因 |
|------|--------|------|
| OpenSSL | ❌ | Qt 6.11 使用 Schannel TLS 后端（Windows 原生加密） |
| MSVC 运行库 | ❌ | `/MT` 静态链接 |
| Tesseract / Leptonica | ❌ | 静态链接，全部链入 exe |
| ICU | ❌ | Windows 10+ 系统自带 |

整个 `dist/` 目录拷到任意 Windows 10 1809+ 机器即可独立运行。

## 首次运行

首次运行自动弹出引导向导：模式选择 → 快捷键 → AI 翻译（服务商/Key/模型）→ OCR 语言 → 保存目录。
配置持久化到程序目录 `settings.json`（或自定义位置）。

## 目录结构

```
SnapLens-cpp/
├── CMakeLists.txt          构建配置（Qt6 + Tesseract 静态 + 打包步骤）
├── src/                    全部源码
│   ├── application.h/.cpp  应用编排（AppController）
│   ├── settings.h/.cpp     设置（X-Macro 表驱动）
│   ├── main.cpp            入口
│   ├── capture/            截图会话 / 覆盖层 / 多屏抓取
│   ├── ocr/                OCR 引擎 / OCR 窗口
│   ├── translate/          AI 客户端 / 翻译线程 / 翻译与主窗口
│   ├── platform/           平台抽象（接口 + 工厂 + Win32 实现）
│   ├── notify/             通知系统
│   ├── ui/                 设置 / 向导 / 托盘 / 钉图 / 视图
│   └── log/                统一日志
├── resources/              SVG 图标 + Qt 资源
├── sdk/tesseract/          Tesseract 5.5 SDK（编译+运行，不入库）
└── dist/                   发布产物（windeployqt 输出，不入库）
```

## 已知说明 / 设计决策

- 翻译窗口与 OCR 窗口为**单实例替换**（再次截图翻译会替换旧窗口）；钉图支持多实例
- 翻译进行中关闭窗口 = **立即取消请求并断网**（不等待、不浪费 token）；翻译中禁止重译/切语言
- 设置页获取模型列表为异步（不阻塞 UI），保存仅发生在点击"保存"时
- 多屏选区不能跨屏拖拽；窗口点选基于 DWM 可见边框，个别自绘窗口可能有细微偏差
- 退出时 Tesseract 可能打印 `ObjectCache LEAK` 告警——为 Tesseract 库自身静态析构行为，非真实泄漏（exit code 0）

## 文档索引

| 文档 | 内容 |
|------|------|
| `static_analysis_report.md` | 第一/二轮静态分析（架构、表驱动、设置/通知 DRY 重构记录） |
| `static_analysis_report_v3.md` | 第三轮独立复审（3 严重 + 6 中等 + 16 轻微问题清单） |
| `static_analysis_report_v4.md` | 修复复核 + L9-L16 修复记录（**当前 0 遗留**） |
| Python 原版 | `D:\Code\WIP\SnapLens`（README / docs/ 含重构分析、构建笔记） |

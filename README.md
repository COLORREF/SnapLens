# SnapLens

> ⚡ 一键截图 · 即时识别 · AI 翻译 — 不止是截图工具

Windows 桌面工具：**截图 + OCR 文字识别 + AI 翻译**，功能对标 QQ 截图（不含画笔标注）。

启动后常驻系统托盘，支持两种使用模式：
- **翻译模式** — 启动即显示文本翻译窗口，适合日常翻译场景
- **截图模式** — 后台静默运行，适合以截图为主的用户

两种模式均可随时切换（托盘菜单 / 设置），且完整支持对方功能。

---

## 技术架构

当前处于**混合架构过渡期**：项目正在进行从 Python 到 Qt C++ 的渐进式重构。

- **Python 层**（`snaplens/`）：UI、业务编排、设置 — 基于 PySide6
- **C++ 原生层**（`native/`）：性能敏感模块以 DLL 形式提供，通过 ctypes 调用
  - `snaplens_log.dll` — 统一日志（qDebug + Qt 消息处理器，全平台编码适配）
  - `snaplens_platform.dll` — 全局热键、窗口枚举、光标、Esc 拦截（Win32）
  - `snaplens_ai.dll` — AI 翻译 API 通信（Qt Network，支持 OpenAI 兼容接口）
  - `snaplens_ocr.dll` — OCR 文字识别（Tesseract 5.5 C API + Leptonica，进程内调用）

**开发路线**：逐模块用 Qt C++ 重写 → 编译为 DLL → Python 调用 → 最终全部迁移为纯 C++ Qt 应用。

---

## 从源代码运行

### 系统要求

- Windows 10/11
- Python 3.10+
- **Qt 6.x**（MSVC 2022 64-bit）— 编译原生模块所需
- **MSVC 2022** (Visual Studio 2022+) — 编译原生模块
- **CMake 3.20+**

### 1. 安装 Python 依赖

```bash
pip install -r requirements.txt
```

仅依赖 PySide6（UI 框架），OCR 和 AI 通信均通过 C++ DLL 实现，无需额外 Python 包。

### 2. 编译原生 DLL

使用 CLion 或命令行打开 `native/` 目录：

```bash
cd native
cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

编译产物输出到 `native/bin/`：

| DLL | 说明 | 运行时依赖 |
|-----|------|-----------|
| `snaplens_log.dll` | 统一日志 | Qt6::Core |
| `snaplens_platform.dll` | 热键、窗口枚举、Esc 拦截、光标 | snaplens_log.dll |
| `snaplens_ai.dll` | AI 翻译 API 通信 | Qt6::Core + Qt6::Network + snaplens_log.dll |
| `snaplens_ocr.dll` | OCR 文字识别 | Tesseract 5.5 + Leptonica 1.87 + snaplens_log.dll |

> 注意：`snaplens_ai.dll` 运行时需要 Qt 的 DLL 在搜索路径中。Python 绑定层会自动查找 `D:\ProgramFiles\Qt\6.*\msvc2022_64\bin`，也可设置 `QT6_DIR` 环境变量。

### 3. Tesseract SDK

OCR 模块依赖 Tesseract 5.5 C API，SDK 位于 `sdk/tesseract/` 目录：

```
sdk/tesseract/
├── include/          # tesseract/ + leptonica/ 头文件（编译时）
├── lib/              # .lib 链接库（编译时）
├── bin/              # DLL 运行时依赖链（运行时）
│   ├── tesseract55.dll
│   ├── leptonica-1.87.0.dll
│   └── *.dll         # 图像解码库（png/jpeg/tiff/webp/gif 等）
└── tessdata/         # 语言包目录（运行时）
    ├── chi_sim.traineddata
    └── eng.traineddata
```

语言包可在设置对话框中在线下载。SDK 不在源码仓库中，需自行准备。

### 4. 运行

```bash
python main.py
```

首次运行自动弹出引导向导，完成模式选择、快捷键、AI 翻译、OCR、保存目录等配置。

---

## 功能总览

### 截图

- **全局热键**：默认 `Ctrl+Shift+Z`，可在设置中修改
- **框选**：按住左键拖动选定区域
- **窗口点选**：单击自动拾取窗口边界，空白处单击为全屏
- **多屏支持**：每屏独立覆盖层，选区互不影响

### 十字准星与信息标签

截图时显示全屏十字线，光标附近实时显示：

- **坐标标签** — `1920, 1080` 格式，颜色/背景可配
- **像素颜色标签** — `255, 0, 170`（RGB）或 `#FF00AA`（Hex），格式可切换

### 像素放大镜

十字线附近显示像素级放大镜：

- **倍率** 4.0× ~ 20.0×，支持滚轮实时调节
- **像素网格** 可开关，颜色/不透明度可配
- **屏幕边缘策略**：裁剪或填充
- **倍率标签** 可开关

### 截图结果操作

选区确定后弹出工具条：

| 按钮 | 操作 |
|------|------|
| 💾 保存 | 存为 PNG / JPG / BMP |
| 📌 钉图 | 置顶窗口，可拖动/缩放/右键菜单 |
| 🔤 OCR | 文字识别（Tesseract 5.5，进程内 C++ DLL） |
| 🌐 AI 翻译 | 发送到 AI 翻译窗口 |
| ✓ 确认 | 复制到剪贴板 |

- `双击` / `回车` 直接复制并退出；`Esc` 取消

### 像素颜色复制

在截图模式下按配置的快捷键（默认 `C`）复制当前光标下的像素颜色。

### OCR 文字识别

通过 `snaplens_ocr.dll` 进程内调用 Tesseract 5.5 C API（LSTM 引擎，OEM_LSTM_ONLY），支持中英日韩等多语言识别。

- 无外部进程、无 pytesseract 依赖
- 支持内存像素和文件路径两种输入方式
- 语言包缺失时自动降级为英文，并记录日志

### AI 翻译

支持 **截图翻译** 和 **文本翻译** 两种模式：

- **截图翻译** — 选中区域 → OCR 提取文字 → AI 翻译 → 显示结果
- **文本翻译** — 主窗口输入文本，直接调用 AI 翻译
- 流式输出：翻译结果逐 token 实时显示，"AI 思考"过程可选显示
- 支持多场景（通用 / IT / 医学 / 金融 / 法律 / 学术 / 文学）
- 内置参数校验：未配置模型/API 地址/翻译内容时在本地拦截，给出明确错误提示

#### 支持的服务商

| 服务商 | 配置方式 |
|--------|----------|
| **DeepSeek**（默认） | 填入 API Key + 选择模型即可 |
| **OpenAI** | 填入 API Key + API 地址 + 模型 |
| **通义千问** | 填入 API Key + API 地址 + 模型 |
| **Kimi / GLM / 混元 / 豆包 / 文心一言** | 同上，切换服务商即可 |

可在设置中自动获取模型列表，或手动输入模型名称。

### 钉图窗口

- 无边框置顶窗口，可拖动、滚轮缩放（0.05× ~ 10×）
- 右键菜单：复制 / 另存为 / 关闭
- `Esc` 或右上角 × 关闭

### 应用模式

| 模式 | 启动行为 | 推荐场景 |
|------|----------|----------|
| **翻译模式** | 显示文本翻译主窗口 | 日常翻译为主 |
| **截图模式** | 后台静默，仅托盘图标 | 以截图为主 |

### 日志系统

统一格式 `[snap LEVEL file:line] msg` 输出到 stderr，通过 `snaplens_log.dll` (qDebug + Qt 默认消息处理器) 实现。

- 设置中可按 **DEBUG / INFO / WARNING / ERROR** 四级独立开关
- 全模块覆盖：C++ DLL（AI / Platform / OCR）和 Python 层全部接入
- 跨平台编码适配：Windows 控制台原生输出，Linux/macOS 为 UTF-8

---

## 目录结构

```
SnapLens/
├── main.py                  程序入口
├── requirements.txt         Python 依赖（仅 PySide6）
│
├── native/                  C++ 原生模块（统一 CMake 项目）
│   ├── CMakeLists.txt       根：全局编译标准 + 四个子项目
│   ├── log/                 统一日志 DLL（qDebug + Qt 消息处理器）
│   ├── platform/            平台能力 DLL（Win32：热键/窗口/ESC/光标）
│   ├── ai/                  AI 通信 DLL（Qt Network：OpenAI 兼容 API）
│   ├── ocr/                 OCR 识别 DLL（Tesseract 5.5 + Leptonica）
│   └── bin/                 DLL 编译输出目录
│
├── sdk/tesseract/           Tesseract 5.5 开发套件（编译+运行）
│   ├── include/             头文件（编译时）
│   ├── lib/                 链接库（编译时）
│   ├── bin/                 DLL 运行时（tesseract55 + leptonica + 图像解码）
│   └── tessdata/            语言包（运行时）
│
├── snaplens/                Python 核心代码包
│   ├── app.py               应用控制器
│   ├── core/                业务层（settings / capture / ocr / api_client / translator）
│   ├── ai/                  翻译模块（8 厂商注册 + C++ DLL 绑定）
│   ├── log/                 日志模块 Python 绑定（ctypes → snaplens_log.dll）
│   ├── ocr/                 OCR 模块 Python 绑定（ctypes → snaplens_ocr.dll）
│   ├── platform/            平台能力抽象层 + DLL 绑定
│   ├── notify/              通知系统（托盘/弹窗/日志三通道）
│   ├── ui/                  用户界面（主窗口/截图/OCR/翻译/设置/钉图）
│   └── assets/              SVG 图标 + Qt 资源文件
│
└── temp/                    临时文件目录（运行时生成）
```

---

## 编译资源文件

修改 SVG 图标后需重新编译 Qt 资源文件：

```bash
# Windows
compile_qrc.bat

# macOS / Linux
bash compile_qrc.sh
```

---

## 跨平台设计

Qt 本身不提供"系统级全局热键"和"枚举其它应用窗口"两个能力，因此通过 C++ DLL 实现：

- `native/platform/` — Win32 原生实现（RegisterHotKey、DWM 窗口枚举、WH_KEYBOARD_LL 钩子）
- `snaplens/platform/base.py` — Python 接口定义 + Null 降级实现
- `snaplens/platform/native_binding.py` — ctypes 调用 DLL

接入 macOS / Linux 时只需新增对应 native 后端并在 Python 工厂中登记。日志系统（`snaplens_log.dll`）基于 qDebug + Qt 默认消息处理器，跨平台零差异。

---

## 已知说明

- 多屏环境下每张屏幕各有一个覆盖层，选区不能跨屏拖拽
- 窗口点选基于 DWM 可见边框，个别自绘窗口可能与视觉边界略有偏差
- OCR 语言包可在设置对话框中在线下载
- AI 翻译支持 8 个服务商，通过 C++ DLL (Qt Network) 统一实现 OpenAI 兼容通信
- OCR 通过 C++ DLL (Tesseract 5.5 C API) 进程内调用，无需安装外部 Tesseract
- 未配置模型/API 地址/翻译内容时会在本地拦截并给出明确错误提示

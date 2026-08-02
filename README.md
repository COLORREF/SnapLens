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

- **Python 层**（`snaplens/`）：UI、业务编排、设置、OCR — 基于 PySide6
- **C++ 原生层**（`native/`）：性能敏感模块以 DLL 形式提供，通过 ctypes 调用
  - `snaplens_platform.dll` — 全局热键、窗口枚举、光标、Esc 拦截（Win32）
  - `snaplens_ai.dll` — AI 翻译 API 通信（Qt Network，支持 OpenAI 兼容接口）

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

### 2. 编译原生 DLL

使用 CLion 或命令行打开 `native/` 目录：

```bash
cd native
cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

编译产物输出到 `native/bin/`：
- `snaplens_platform.dll`
- `snaplens_ai.dll`

> 注意：`snaplens_ai.dll` 依赖 Qt6（Core + Network），运行时需要 Qt 的 DLL 在搜索路径中。
> Python 绑定层会自动查找 `D:\ProgramFiles\Qt\6.*\msvc2022_64\bin`，也可设置 `QT6_DIR` 环境变量指定路径。

### 3. 配置 Tesseract OCR

AI 图片翻译依赖 OCR 提取文字。程序自动检测系统安装目录（`C:\Program Files\Tesseract-OCR`），或可使用便携版放入项目 `tesseract/` 目录：

```
tesseract/
├── tesseract.exe
├── libtesseract-5.dll
├── libleptonica-6.dll
└── tessdata/
    ├── chi_sim.traineddata   中文简体（必需）
    └── eng.traineddata       英文（必需）
```

语言包可在设置对话框中在线下载。

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
- **复合布局** — 坐标与颜色合并为一行，永不重叠

### 像素放大镜

十字线附近显示像素级放大镜：

- **倍率** 4.0× ~ 20.0×，支持滚轮实时调节
- **像素网格** 可开关，颜色/不透明度可配
- **屏幕边缘策略**：裁剪（缩小）或填充（固定大小 + 纯色填充）
- **倍率标签** 可开关，颜色/不透明度可配

### 截图结果操作

选区确定后弹出工具条：

| 按钮 | 操作 |
|------|------|
| 💾 保存 | 存为 PNG / JPG / BMP |
| 📌 钉图 | 置顶窗口，可拖动/缩放/右键菜单 |
| 🔤 OCR | 文字识别（Tesseract） |
| 🌐 AI 翻译 | 发送到 AI 翻译窗口 |
| ✓ 确认 | 复制到剪贴板 |

- `双击` / `回车` 直接复制并退出；`Esc` 取消

### 像素颜色复制

在截图模式下按配置的快捷键（默认 `C`）复制当前光标下的像素颜色。

### OCR 文字识别

基于 Tesseract OCR 引擎，支持中英日韩等多语言识别。

### AI 翻译

支持 **截图翻译** 和 **文本翻译** 两种模式，底层通过 C++ DLL (Qt Network) 实现 OpenAI 兼容 API 通信：

- **截图翻译** — 选中区域 → OCR 提取文字 → AI 翻译 → 显示结果
- **文本翻译** — 主窗口输入文本，直接调用 AI 翻译
- 流式输出：翻译结果逐 token 实时显示，"AI 思考"过程可选显示
- 支持多场景（通用 / IT / 医学 / 金融 / 法律 / 学术 / 文学）

#### 支持的服务商

| 服务商 | 配置方式 |
|--------|----------|
| **DeepSeek**（默认） | 填入 API Key 即可 |
| **OpenAI** | 填入 API Key + API 地址 |
| **通义千问** | 填入 API Key + API 地址 |
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

---

## 目录结构

```
SnapLens/
├── main.py                  程序入口
├── requirements.txt         Python 依赖清单
│
├── native/                  C++ 原生模块（统一 CMake 项目）
│   ├── CMakeLists.txt       根：全局编译标准 + 子项目
│   ├── ai/                  AI 通信 DLL（Qt Network）
│   │   ├── CMakeLists.txt
│   │   ├── include/ai_client.h       C ABI 公共头
│   │   └── src/                      实现文件
│   ├── platform/            平台能力 DLL（Win32）
│   │   ├── CMakeLists.txt
│   │   ├── include/snaplens_platform.h
│   │   └── src/                      热键 / 窗口枚举 / Esc 拦截 / 光标
│   └── bin/                 DLL 编译输出目录
│
├── snaplens/                Python 核心代码包
│   ├── __init__.py          版本定义
│   ├── app.py               应用控制器
│   │
│   ├── core/
│   │   ├── settings.py      设置读写（JSON 持久化，70+ 项数据驱动）
│   │   ├── capture.py       多屏截图与裁剪（混合 DPI 坐标换算）
│   │   ├── ocr.py           Tesseract OCR 引擎查找与调用
│   │   ├── api_client.py    AI API 客户端（ctypes → C++ DLL）
│   │   ├── text_translator.py   文本翻译后台线程
│   │   └── temp_cleanup.py      临时文件清理
│   │
│   ├── ai/                  翻译模块
│   │   ├── __init__.py      厂商注册表与工厂（8 厂商）
│   │   ├── base.py          AITranslator 抽象接口
│   │   ├── openai_compat.py OpenAI 兼容翻译器
│   │   └── native_binding.py    ctypes → snaplens_ai.dll
│   │
│   ├── platform/            平台能力抽象层 + Python 绑定
│   │   ├── base.py          接口定义
│   │   ├── __init__.py      工厂函数
│   │   └── native_binding.py    ctypes → snaplens_platform.dll
│   │
│   ├── notify/              通知系统（托盘/弹窗/日志三通道）
│   ├── ui/                  用户界面（主窗口/截图/OCR/翻译/设置/钉图）
│   └── assets/              SVG 图标 + Qt 资源文件
│
├── tesseract/               Tesseract OCR 便携版（源码仓库不包含）
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

接入 macOS / Linux 时只需新增对应 native 后端并在 Python 工厂中登记。未接入的平台自动降级为 Null 实现。其余模块（截图、托盘、钉图、设置）均为纯 Qt 实现，无需改动。

---

## 已知说明

- 多屏环境下每张屏幕各有一个覆盖层，选区不能跨屏拖拽
- 窗口点选基于 DWM 可见边框，个别自绘窗口可能与视觉边界略有偏差
- OCR 语言包可在设置对话框中在线下载
- AI 翻译支持 8 个服务商，通过 C++ DLL (Qt Network) 统一实现 OpenAI 兼容通信

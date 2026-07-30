# SnapLens

> ⚡ 一键截图 · 即时识别 · AI 翻译 — 不止是截图工具

Windows 桌面工具：**截图 + OCR 文字识别 + AI 翻译**，功能对标 QQ 截图（不含画笔标注）。

启动后常驻系统托盘，支持两种使用模式：
- **翻译模式** — 启动即显示文本翻译窗口，适合日常翻译场景
- **截图模式** — 后台静默运行，适合以截图为主的用户

两种模式均可随时切换（托盘菜单 / 设置），且完整支持对方功能。

---

## 获取方式

**不想折腾源代码？** ➔ 直接下载 [Releases](https://github.com/yourname/snaplens/releases) 中的已打包版本，解压即用，无需配置 Python 环境或 Tesseract。

**从源代码运行：**

```bash
# 1. 克隆仓库
git clone https://github.com/yourname/snaplens.git
cd snaplens

# 2. 安装 Python 依赖
pip install -r requirements.txt

# 3. 配置 Tesseract OCR（详见下方说明）

# 4. 运行
python main.py
```

**系统要求**：Windows 10/11，Python 3.10+，PySide6。

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
- **屏幕边缘策略**：裁剪（缩小）或填充（固定大小+纯色填充）
- **倍率标签** 可开关，颜色/不透明度可配

### 截图结果操作

选区确定后弹出工具条：

| 按钮 | 操作 |
|------|------|
| 💾 保存 | 存为 PNG / JPG / BMP |
| 📌 钉图 | 置顶窗口，可拖动/缩放/右键菜单 |
| 🔤 OCR | 文字识别（Tesseract） |
| 🌐 AI 翻译 | 发送到 AI 翻译窗口 |
| ⭕ ✕ 取消 | 取消截图 |
| ✓ 确认 | 复制到剪贴板 |

- `双击` / `回车` 直接复制并退出；`Esc` 取消

### 像素颜色复制

在截图模式下按配置的快捷键（默认 `C`）复制当前光标下的像素颜色：

- Hex 格式：`#FF00AA` 或 `FF00AA`
- RGB 格式：`rgb(255,0,170)` 或 `255,0,170`
- 可选包含 Alpha 通道

### OCR 文字识别

基于 Tesseract OCR 引擎，支持中英日韩等多语言识别。

- 从截图工具条直接启动
- **图片翻译会自动调用 OCR 完成文字提取**
- 语言包可在设置对话框中在线下载

### AI 翻译

支持 **截图翻译** 和 **文本翻译** 两种模式：

- **截图翻译** — 选中区域后点击翻译按钮，图片经 OCR → AI 翻译 → 显示结果
- **文本翻译** — 主窗口输入文本，直接调用 AI 翻译
- 流式输出：翻译结果逐 token 实时显示，"AI 思考"过程可选显示
- 支持多场景（通用 / IT / 医学 / 金融 / 法律 / 学术 / 文学）

#### 支持的服务商

| 服务商 | 配置方式 |
|--------|----------|
| **DeepSeek**（默认） | 填入 API Key 即可，默认模型 `deepseek-chat` |
| **OpenAI** | 切换服务商，填入 API Key + API 地址 |
| **通义千问** | 切换服务商，填入 API Key + API 地址 |

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

- 快捷键截图、OCR、AI 翻译在两种模式下均可使用
- 切换方式：托盘菜单或设置对话框

---

## 安装与配置

### Python 依赖

```
PySide6>=6.5       — Qt 图形界面框架
pytesseract>=0.3   — Tesseract OCR Python 封装
Pillow>=10.0       — 图片处理
openai>=1.0        — OpenAI 兼容 API 客户端
```

安装命令：

```bash
pip install -r requirements.txt
```

### OCR 引擎（Tesseract）

SnapLens 依赖 Tesseract OCR 实现文字识别功能。

#### 方式一：直接下载 Release（推荐）

[Releases](https://github.com/yourname/snaplens/releases) 中的已打包版本内置了 Tesseract 便携版（含中英日韩语言包），解压即用，**适合不想折腾的用户**。

#### 方式二：自行配置（从源代码运行开发者适用）

源代码仓库**不包含** Tesseract 便携版（体积 ~160 MB），开发者需自行准备：

1. 从 [UB-Mannheim Tesseract](https://github.com/UB-Mannheim/tesseract/wiki) 下载 Windows 安装包
2. 安装时勾选需要的语言包（简体中文 `chi_sim`、日文 `jpn`、韩文 `kor` 等）
3. 程序会自动检测系统安装目录（`C:\Program Files\Tesseract-OCR`）

或者下载 Tesseract 便携版放入项目 `tesseract/` 目录（需自行获取）：

```
tesseract/
├── tesseract.exe
├── libtesseract-5.dll
├── libleptonica-6.dll
└── tessdata/
    ├── chi_sim.traineddata   中文简体（必需）
    ├── eng.traineddata       英文（必需）
    ├── jpn.traineddata       日文（可选）
    └── kor.traineddata       韩文（可选）
```

> **macOS/Linux 用户**：暂不提供官方支持，会尽快完善

### 编译资源文件

修改 SVG 图标后需重新编译 Qt 资源文件：

```bash
# Windows
compile_qrc.bat

# macOS / Linux
bash compile_qrc.sh
```

依赖 PySide6 自带的 `pyside6-rcc` 工具。编译产物 `snaplens/assets/assets_rc.py` 已提交至仓库，普通用户无需手动编译。

---

## 目录结构

```
SnapLens/
├── main.py                  程序入口
├── requirements.txt         Python 依赖清单
├── .gitignore               Git 忽略规则
├── .gitattributes           Git 属性配置（SVG 不计入语言统计）
├── compile_qrc.bat          Windows QRC 编译脚本
├── compile_qrc.sh           macOS/Linux QRC 编译脚本
│
├── snaplens/                核心代码包
│   ├── __init__.py          版本定义
│   ├── app.py               应用控制器（模式管理/截图/翻译/托盘串联）
│   │
│   ├── core/
│   │   ├── settings.py      设置读写（JSON 持久化，70+ 项数据驱动）
│   │   ├── capture.py       多屏截图与裁剪（混合 DPI 坐标换算）
│   │   ├── ocr.py           Tesseract OCR 引擎查找与调用
│   │   ├── api_client.py    OpenAI 兼容 API 客户端（同步+流式）
│   │   ├── text_translator.py  文本翻译后台线程
│   │   └── temp_cleanup.py     临时文件清理
│   │
│   ├── platform/            平台能力抽象层
│   │   ├── base.py          接口定义（HotkeyProvider / WindowProvider 等）
│   │   ├── __init__.py      工厂函数（按系统选择后端）
│   │   └── win32.py         Windows 后端（RegisterHotKey + DWM 窗口枚举）
│   │
│   ├── notify/              通知系统
│   │   ├── defs.py          通知类型定义（12 种）
│   │   ├── channel.py       三通道实现（托盘/弹窗/日志）
│   │   └── manager.py       通知管理器（统一入口）
│   │
│   ├── ai/                  翻译模块
│   │   ├── base.py          AITranslator 抽象接口
│   │   ├── openai_compat.py OpenAI 兼容翻译器
│   │   ├── deepseek.py      向后兼容别名
│   │   └── __init__.py      厂商注册表与工厂
│   │
│   ├── ui/                  用户界面
│   │   ├── main_window.py   文本翻译主窗口（双面板+场景/语言选择）
│   │   ├── snip.py          截图会话（多屏覆盖层协调）
│   │   ├── overlay.py       选区覆盖层（核心交互 ~990 行）
│   │   ├── pin.py           钉图窗口
│   │   ├── tray.py          托盘图标与菜单（含模式切换）
│   │   ├── settings_dialog.py  设置对话框（8 页标签）
│   │   ├── setup_wizard.py  首次运行引导向导（6 步）
│   │   ├── translate_service.py  图片翻译后台线程
│   │   ├── translate_window.py   图片翻译结果窗口
│   │   ├── ocr_service.py       OCR 识别后台线程
│   │   ├── ocr_window.py        OCR 识别结果窗口
│   │   ├── color_picker.py      颜色选择器组件
│   │   └── zoomable_image.py    可缩放图片查看器
│   │
│   └── assets/
│       ├── assets.qrc           Qt 资源清单
│       ├── assets_rc.py          编译后资源文件（编译产物）
│       ├── Save.svg / Pushpin.svg / Close.svg / Check.svg
│       └── Translate.svg / Ocr.svg
│
├── tesseract/                Tesseract OCR 便携版（Release 专用，源码仓库不包含）
│
└── temp/                     临时文件目录（运行时生成，已 gitignore）
```

---

## 脚本说明

| 脚本 | 用途 | 运行时机 |
|------|------|----------|
| `compile_qrc.bat` | Windows 下编译 Qt 资源文件 | 修改了 `*.svg` 图标后 |
| `compile_qrc.sh` | macOS/Linux 下编译 Qt 资源文件 | 同上 |

两个脚本功能相同，只是平台不同。它们调用 PySide6 自带的 `pyside6-rcc` 工具将 `.qrc` 清单编译为可导入的 Python 模块 `assets_rc.py`。普通用户无需运行。

---

## 跨平台设计

Qt 本身不提供"系统级全局热键"和"枚举其它应用窗口"两个能力，因此代码按平台抽象层组织：

- `platform/base.py` — 定义接口（HotkeyProvider / WindowProvider / CursorProvider 等）
- `platform/__init__.py` — 工厂函数，按 `sys.platform` 选择后端
- `platform/win32.py` — Windows 后端实现

接入 macOS / Linux 时只需新增对应后端文件并在工厂中登记。未接入的平台自动降级为 Null 实现（热键注册失败、窗口点选退化为框选/全屏）。其余模块（截图、托盘、钉图、设置）均为纯 Qt 实现，无需改动。

---

## 已知说明

- 多屏环境下每张屏幕各有一个覆盖层，选区不能跨屏拖拽
- 窗口点选基于 DWM 可见边框，个别自绘窗口可能与视觉边界略有偏差
- OCR 语言包可在设置对话框中在线下载（jsdelivr CDN）
- AI 翻译支持流式输出，翻译进度实时可见

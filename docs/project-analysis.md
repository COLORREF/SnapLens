# SnapLens 项目分析报告

> 分析时间：2026-08-04 | 更新：同日（OCR C++ 重构 + 日志模块完成后） | 目的：完整理解项目现状

---

## 一、项目定位

SnapLens 是 Windows 桌面截图翻译工具（截图 + OCR 文字识别 + AI 翻译），对标 QQ 截图（不含画笔标注）。
常驻系统托盘，支持翻译模式 / 截图模式两种应用模式，可随时切换。

**当前处于混合架构过渡期**：Python（PySide6）UI 与业务编排 + 性能敏感模块 C++ 重构为 DLL，经 ctypes 调用。

---

## 二、整体架构

```
┌────────────────────── Python 层 (snaplens/) ──────────────────────┐
│  UI 层     main_window / snip 覆盖层 / ocr_window /               │
│            translate_window / settings_dialog / tray / pin        │
│  业务层    AppController 编排 · core/ · ai/ · ocr/ · platform/   │
│  绑定层    log/__init__.py · ai/native_binding.py ·               │
│            ocr/native_binding.py · platform/native_binding.py     │
└──────────────────────────────┬────────────────────────────────────┘
                               │ ctypes
┌────────────────────── C++ 原生层 (native/) ───────────────────────┐
│  snaplens_log.dll       ✅ 统一日志 (qDebug + Qt 消息处理器)       │
│  snaplens_platform.dll  ✅ 热键/窗口/Esc/光标 (Win32)              │
│  snaplens_ai.dll        ✅ OpenAI 兼容 API (Qt6 Network)          │
│  snaplens_ocr.dll       ✅ OCR 识别 (Tesseract 5.5 + Leptonica)   │
└──────────────────────────────┬────────────────────────────────────┘
                               │ 链接
┌────────────────────── SDK (sdk/tesseract/) ───────────────────────┐
│  tesseract55.lib/.dll · leptonica-1.87.0 · include/ 头文件        │
│  lib/ + bin/ · tessdata 语言包                                     │
└───────────────────────────────────────────────────────────────────┘
```

**开发路线**：逐模块 Qt C++ 重写 → DLL → Python ctypes 调用 → 最终纯 C++ Qt 应用。

---

## 三、主要模块职责

### Python 层

| 模块 | 职责 | 关键类 / 函数 |
|------|------|---------------|
| `main.py` | 程序入口 | QApplication + AppController 保持引用 |
| `app.py` | 应用编排器 | `AppController`：首次向导 → 托盘 → 热键 → 模式切换 → 截图会话 → 结果动作（save/copy/pin/translate/ocr） |
| `core/settings.py` | 设置读写 | `Settings` + `SETTING_DEFS` 数据驱动注册表（80+ 项，JSON 持久化） |
| `core/capture.py` | 多屏截图 | 混合 DPI 坐标换算，每屏独立覆盖层 |
| `core/ocr.py` | **OCR 引擎调用** | `find_tessdata_dir()`（缓存）/ `extract_text()` → 转发 ocr/native_binding |
| `core/api_client.py` | AI API 客户端 | `call_chat()` / `call_chat_stream()` + 参数校验 → 转发 ai/native_binding |
| `log/__init__.py` | **日志模块绑定** | ctypes → snaplens_log.dll；级别过滤（DEBUG/INFO/WARNING/ERROR） |
| `core/text_translator.py` | 文本翻译后台线程 | QThread 封装 |
| `core/temp_cleanup.py` | 临时文件清理 | `cleanup_temp_dir()` |
| `ai/__init__.py` | 厂商注册表与工厂 | 8 厂商：deepseek/openai/qwen/kimi/glm/hunyuan/doubao/qianfan |
| `ai/base.py` | 翻译抽象接口 | `AITranslator` |
| `ai/openai_compat.py` | OpenAI 兼容翻译器 | `OpenAICompatibleTranslator.translate()`：OCR 提取 → AI 翻译 两步管道 |
| `ai/native_binding.py` | AI DLL ctypes 绑定 | `_load_dll` / `_setup_signatures` / `call_chat` / `call_chat_stream` / `list_models`（接入统一日志） |
| `ocr/native_binding.py` | **OCR DLL ctypes 绑定** | Tesseract 引擎生命周期管理（init/识别/关闭）+ SDK DLL 路径注册 |
| `platform/base.py` | 平台能力接口 | `HotkeyProvider` / `WindowProvider` / `CursorProvider` / `CancelProvider` / `ClipCursorProvider` + Null 降级实现 |
| `platform/native_binding.py` | Platform DLL ctypes 绑定 | 热键注册（Qt 键码 → Win32 VK）、窗口枚举、物理像素光标 |
| `notify/` | 通知系统 | `NotifyManager`：托盘 / 弹窗 / 日志三通道，按设置开关 |
| `ui/ocr_service.py` | **OCR 后台线程** | `OcrService(QThread)`：`finished(str)` / `error(str)` 信号 |
| `ui/ocr_window.py` | OCR 结果窗口 | 左侧缩放预览 + 右侧只读文本 + 复制按钮；临时 PNG 落盘后启动 OcrService |
| `ui/translate_service.py` | 翻译后台线程 | `TranslateService(QThread)`：translated / ocr_text / thinking / error 信号 |
| `ui/snip.py` / `overlay.py` | 截图会话与覆盖层 | 框选、窗口点选、十字准星、放大镜、坐标/颜色标签、工具条 |
| `ui/main_window.py` | 翻译主窗口 | 文本翻译 + 提示词模板 + 布局分栏 |
| `ui/settings_dialog.py` | 设置对话框 | 含 Tesseract 语言包在线下载 |
| `ui/tray.py` | 系统托盘 | 截图/设置/翻译/模式切换/退出 |
| `ui/pin.py` | 钉图窗口 | 置顶、缩放、右键菜单 |

### C++ 原生层

| DLL | 依赖 | 功能 |
|-----|------|------|
| `snaplens_log.dll` | Qt6::Core | 统一日志：qDebug + qSetMessagePattern + Qt 默认消息处理器。全模块通过 C ABI 调用。 |
| `snaplens_platform.dll` | snaplens_log | 全局热键（RegisterHotKey）、DWM 窗口枚举、Esc 低级钩子、光标位置/限制 |
| `snaplens_ai.dll` | Qt6::Core + Network + snaplens_log | OpenAI 兼容 /chat/completions（流式 SSE + 非流式）、/models |
| `snaplens_ocr.dll` | Tesseract 5.5 + Leptonica + snaplens_log | OCR 识别：TessBaseAPI 引擎管理、内存/文件两种输入、LSTM 引擎（OEM_LSTM_ONLY） |

---

## 四、关键设计约定（重构时必须遵守）

1. **C ABI 头文件**：`include/*.h` 声明所有导出函数，`__declspec(dllexport)` + `extern "C"`；头部注释写明返回值约定与线程安全。
2. **返回值约定**：两个 DLL 不一致——platform 用 `1 成功 / 0 失败`；ai 用 `0 成功 / <0 错误码`。新 OCR DLL 建议沿用 ai 的错误码风格（0 成功 / <0 失败）或明确统一。
3. **字符串传递**：`wchar_t*` 宽字符 + 调用方预分配缓冲区 + size 参数（AI 用 `c_wchar_p` 输出缓冲）。
4. **回调**：ctypes `CFUNCTYPE`，**Python 侧必须保持回调引用**（`self._cb_ref`），否则被 GC 导致崩溃；跨线程回调只 emit Qt 信号，不直接操作 UI。
5. **ctypes 绑定文件结构**：模块级 `_dll_cache` 单例 → `_load_dll()`（查找 native/bin → native/cmake-build-*/bin → exe 同级）→ `_setup_signatures()`（显式 restype/argtypes，防 64 位截断）。
6. **DLL 依赖搜索**：Python 3.8+ `ctypes.CDLL` 不搜索 PATH；AI DLL 依赖 Qt，需先 `os.add_dll_directory()` 添加 Qt bin 目录。**OCR DLL 若依赖 tesseract55.dll 等，同样需要在加载前添加 `sdk/tesseract/bin`（或运行时目录）到 DLL 搜索路径。**
7. **DllMain 不做事**：所有资源按需在 init 或 Manager::install 中创建，避免 LoadLock 死锁。
8. **日志**：统一使用 `snaplens_log.dll`（`SNAP_LOG_DEBUG/INFO/WARNING/ERROR` 宏或 Python `log_info/log_error`），格式 `[snap LEVEL file:line] msg`。四级独立开关，默认全开。
9. **CMake**：根 CMakeLists 统一 `CMAKE_CXX_STANDARD 17`、输出到 `native/bin/`、MSVC `/W4 /permissive- /utf-8`、`MSVC_RUNTIME_LIBRARY MultiThreaded`、`CMAKE_NO_SYSTEM_FROM_IMPORTED ON`（MSVC 14.51 修复）。

---

## 五、OCR 模块现状（✅ 已完成 C++ 重构）

### 调用链

```
截图 → QPixmap → 存临时 PNG → OcrService/TranslateService(QThread)
     → core/ocr.py: extract_text() → ocr/native_binding.py
     → snaplens_ocr.dll → TessBaseAPI (Tesseract 5.5 C API) → UTF-8 文本
```

### 新 DLL 接口（snaplens_ocr.h）

```c
int  snap_ocr_init(const char* data_path, const char* language);
void snap_ocr_shutdown(void);
int  snap_ocr_extract_text(const unsigned char* data, int w, int h, ...);
int  snap_ocr_extract_text_file(const char* path, ...);
int  snap_ocr_set_variable(const char* name, const char* value);
const char* snap_ocr_get_version(void);
```

### 关键实现

- **引擎复用**：`OcrEngine` 单例缓存 `TessBaseAPI` 实例，避免反复加载语言模型
- **双输入模式**：内存像素（`TessBaseAPISetImage`）和文件路径（Leptonica `pixRead` → `TessBaseAPISetImage2`）
- **自动降级**：LSTM_ONLY 失败时回退 OEM_DEFAULT
- **日志接入**：通过 `snaplens_log.dll` 记录初始化、识别结果（置信度、文本长度）
- **Python 绑定**：`snaplens/ocr/native_binding.py` 负责 SDK DLL 路径注册、引擎生命周期

---

## 六、SDK 情况（重构弹药）

`dks/tesseract/`（实际为 `sdk/tesseract/`）是 **MSVC 构建的 Tesseract 5.5.0 完整开发库**：

| 目录 | 内容 |
|------|------|
| `include/tesseract/` | 完整头文件：`capi.h`（C API 主入口）、`baseapi.h`、`publictypes.h`、`version.h` 等 |
| `include/leptonica/` | `pix.h`、`allheaders.h` 等图像处理库头文件 |
| `lib/` | Release 库：`tesseract55.lib`、`leptonica-1.87.0.lib` + 依赖（png/jpeg/tiff/webp/curl 等） |
| `lib/debug/` | Debug 库：`tesseract55d.lib` 等 |
| `bin/` | Release DLL：`tesseract55.dll` 及依赖 DLL |
| `bin/debug/` | Debug DLL |

> 注意：`tesseract/` 便携版目录是 **MinGW（MSYS2）构建**（含 libgcc_s_seh、libstdc++-6 等），与 SDK 的 MSVC 库**不能混用**。C++ 重构必须链接 SDK 的 MSVC 版本（tesseract55.lib）。

### C API 关键入口（capi.h）

- `TessBaseAPICreate()` / `TessBaseAPIDelete()`
- `TessBaseAPIInit3(handle, datapath, language)` — datapath=tessdata 父目录，language="chi_sim+eng"（+ 分隔）
- `TessBaseAPISetImage(handle, data, w, h, bpp, bytes_per_line)` — 直接喂内存图像
- `TessBaseAPISetImage2(handle, Pix*)` — 或喂 Leptonica Pix
- `TessBaseAPIRecognize(handle, NULL)` — 执行识别
- `TessBaseAPIGetUTF8Text(handle)` — 输出 UTF-8 文本（需 `TessDeleteText` 释放）
- `TessBaseAPIMeanTextConf(handle)` — 平均置信度
- `TessBaseAPISetPageSegMode(handle, PSM_*)`
- `TessVersion()` / `TessBaseAPIGetAvailableLanguagesAsVector()` — 版本与可用语言
- `TessMonitorCreate()` 等 — 进度/取消回调（可选）

---

## 七、OCR 重构建议（下阶段路线）

### 7.1 新 DLL 设计

```
native/
├── ocr/
│   ├── CMakeLists.txt
│   ├── include/snaplens_ocr.h        # C ABI 公共头
│   └── src/
│       ├── dllmain.cpp               # DllMain（空）+ C ABI 实现
│       └── ocr_engine.h/.cpp         # 内部封装 TessBaseAPI（持有引擎句柄）
```

**推荐 C ABI 接口**（风格对齐现有 DLL）：

```c
// 生命周期
int  snap_ocr_init(const wchar_t* tessdata_dir);   // 0 成功 / <0 失败
void snap_ocr_shutdown(void);

// 一次性提取（内存图像 → 文本），简单场景
int  snap_ocr_extract(const unsigned char* rgba, int width, int height,
                      const wchar_t* langs,        // "chi_sim+eng"
                      wchar_t* text_out, int text_size,
                      wchar_t* error_out, int error_size);

// 句柄式（复用引擎，避免反复初始化语言模型 —— 性能关键）
// handle 生命周期 + set_image + recognize + get_text + get_confidence
```

### 7.2 关键性能决策

- **引擎复用**：Tesseract `Init` 加载语言模型开销大，重构后应缓存 `TessBaseAPI` 实例（或引擎池），而非每次调用都创建/销毁。
- **内存图像直喂**：`TessBaseAPISetImage` 直接接收 RGBA/灰度内存，省去临时文件；Python 侧用 `pixmap.toImage().bits()` 或先转换。
- **DLL 搜索路径**：snaplens_ocr.dll 依赖 tesseract55.dll 及其依赖链（leptonica、libpng 等），ctypes 加载前需 `os.add_dll_directory("sdk/tesseract/bin")`（参考 ai/native_binding 的 Qt 处理）。

### 7.3 兼容性要求

- `extract_text(image_path, ocr_langs)` 签名不变 → 上层 `OcrService`、`openai_compat` 无需改动（或仅改实现体）。
- 保留 Tesseract 查找逻辑（`find_tesseract`），tessdata 目录来源：`sdk/tesseract` / `tesseract/` / `C:\Program Files\Tesseract-OCR`。
- 错误信息经 `error_out` 传回，映射为 Python 异常（参考 `_raise_from_error`）。

### 7.4 部署形态

- 开发期：`snaplens_ocr.dll` + `tesseract55.dll` 及其依赖 DLL 复制到 `native/bin/`（或 exe 同级）。
- 语言包（traineddata）仍走独立 tessdata 目录，维持在线下载逻辑。

---

## 八、运行方式

```bash
pip install -r requirements.txt          # PySide6 / pytesseract / Pillow / openai(已无用，见 R9)
cd native && cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release         # 产物 → native/bin/
python main.py                            # 首次运行弹引导向导
```

- `snaplens_ai.dll` 运行时需 Qt6 DLL 在搜索路径（绑定层自动查找 `D:\ProgramFiles\Qt\6.*\msvc2022_64\bin` 或 `QT6_DIR`）。
- Tesseract：自动检测系统安装或便携版 `tesseract/` 目录；语言包可在设置中下载。
- 开发环境：CLion + CMake + MSVC 14.51（VS 2026）+ Qt 6.11.1，Windows SDK 装在非系统盘（根 CMakeLists 已做 SDK 库路径修复）。

---

## 九、已知遗留问题（静态分析摘要）

- **B1 / Q1-Q3 / Q4-Q8 / R2 / R6 / R8 / R9** — 已于 2026-08-04 全部修复。详见 `static_analysis_report.md`（6 项残余，均 P2）。

---

*OCR 模块 C++ 重构已完成（snaplens_ocr.dll + Python 绑定）。日志模块（snaplens_log.dll）已覆盖全部 C++ 与 Python 代码。*

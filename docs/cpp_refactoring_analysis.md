# SnapLens C++ 重构分析报告

> 分析时间：2026-08-04 | 版本：0.1.0
> 目的：识别当前 Python 层中适合进行 C++ 重构的模块，评估优先级和工作量。

---

## 一、重构现状总览

### 1.1 已完成 C++ 重构（4 个 DLL）

| DLL | 职责 | 依赖 | Python 绑定 |
|-----|------|------|------------|
| `snaplens_log.dll` | 统一日志（qDebug + Qt 消息处理器） | Qt6::Core | `snaplens/log/__init__.py` |
| `snaplens_platform.dll` | 全局热键、DWM 窗口枚举、Esc 拦截、光标 | snaplens_log + user32/kernel32/dwmapi | `snaplens/platform/native_binding.py` |
| `snaplens_ai.dll` | OpenAI 兼容 API 通信（流式 SSE + 非流式） | Qt6::Core + Qt6::Network + snaplens_log | `snaplens/ai/native_binding.py` |
| `snaplens_ocr.dll` | OCR 文字识别（Tesseract 5.5 C API） | Tesseract 5.5 + Leptonica 1.87 + snaplens_log | `snaplens/ocr/native_binding.py` |

### 1.2 现有 Python 层模块分类

| 分类 | 模块 | 是否已绑定 C++ | 是否性能关键 |
|------|------|:---:|:---:|
| **UI 渲染** | `ui/overlay.py`（截图覆盖层 + 像素放大镜） | 部分（光标/取消） | 🔴 极高 |
| **屏幕截取** | `core/capture.py`（多屏截图 + DPR 计算） | 否 | 🟡 中等 |
| **图像管道** | `core/ocr.py` → `ocr/native_binding.py` | ✅ OCR DLL（**但走文件路径**） | 🟡 中等 |
| **翻译管道** | `ai/openai_compat.py` → `core/api_client.py` → AI DLL | ✅ AI DLL | 🟢 低 |
| **设置管理** | `core/settings.py` | 否 | 🟢 低 |
| **通知系统** | `notify/` | 部分（LogChannel→log DLL） | 🟢 低 |
| **UI 组件** | `ui/main_window.py`, `ui/pin.py`, `ui/tray.py` | 否 | 🟢 低 |
| **后台线程** | `ui/ocr_service.py`, `ui/translate_service.py` | 间接（OCR/AI DLL） | 🟢 低 |
| **视觉组件** | `ui/zoomable_image.py`, `ui/color_picker.py` | 否 | 🟢 低 |

---

## 二、推荐 C++ 重构候选

### 候选 1：像素放大镜渲染引擎 🔴 P0 — 高优先级

**当前状态：**
- 位置：`snaplens/ui/overlay.py` — `_MgFrame` 数据类 + `_prepare_mg_frame()` + `_render_mg_pixels()` + `_render_mg_overlays()`
- 每个鼠标移动事件触发一次完整的渲染管道：
  1. Phase 1 `_prepare_mg_frame()` — 源像素区域计算（clip/pad 模式，物理↔逻辑坐标换算，翻转检测）
  2. Phase 2 `_compute_label_targets()` — 标签位置计算 + `_animate_labels()` 动画状态机
  3. Phase 3 `_render_mg_pixels()` — 像素块渲染（`painter.drawPixmap` 最近邻缩放）
  4. Phase 4 `_render_mg_overlays()` — 网格线（逐像素 for 循环画线）、准星、边框
  5. Phase 5 `_render_mg_labels()` — 倍率标签、坐标/颜色标签

**瓶颈分析：**
- `paintEvent` 调用频率极高（每帧鼠标移动），是 **全项目最热的 Python 代码路径**
- 网格线渲染使用 Python `for` 循环逐行逐列画线，在 20x 放大倍率下可能生成数百条线
- Phase 1 的坐标换算（物理像素 ↔ 逻辑像素）每帧执行，涉及浮点运算和条件分支

**重构方案：**
```
新增 snaplens_magnifier.dll（或合并进 snaplens_platform.dll）

C ABI 接口设计：
┌─────────────────────────────────────────────────────────────┐
│  输入                                                        │
│  · 光标物理像素 (cx, cy)                                      │
│  · 屏幕物理像素尺寸 (pm_w, pm_h)                               │
│  · 放大镜参数 (zoom, half_size, edge_mode)                    │
│  · 渲染配置 (grid_color/alpha, cross_color/thickness, etc.)   │
│                                                               │
│  输出 → MgFrameRaw (C struct)                                 │
│  · src_left, src_top, src_w, src_h  (源像素区域)               │
│  · disp_x, disp_y, disp_w, disp_h  (显示区域)                  │
│  · 网格线列表 (预计算的 line 坐标数组)                          │
│  · 翻转检测结果 (flip_x, flip_y)                              │
└─────────────────────────────────────────────────────────────┘
```

**影响范围：**
- `_prepare_mg_frame()` → 替换为 C ABI `snap_magnifier_prepare_frame()`
- `_render_mg_overlays()` 网格线循环 → 替换为预计算坐标数组，Python 侧只做 `painter.drawLine`
- 预期性能提升：网格线帧生成减少 60-80ms（高倍率场景）

**工作量估计：** 2-3 天
- C++ 实现：1 天（纯计算，无 Qt/网络依赖）
- Python 绑定：0.5 天
- 测试集成：0.5-1 天

---

### 候选 2：OCR 像素直传路径优化 🟡 P1 — 中优先级

**当前状态：**
- 调用链：`QPixmap → 存临时 PNG 文件 → core/ocr.py::extract_text(image_path)` → `snap_ocr_extract_text_file()` (C DLL)
- C++ DLL **已支持** 内存像素路径：`snap_ocr_extract_text(data, w, h, bpp, bpl, ...)`
- 但 Python 层**从未使用**这个路径，每次都走临时文件

**瓶颈分析：**
- `QPixmap.save(temp_path, "PNG")` — PNG 编码开销（即使只有几百 KB）
- 磁盘 I/O — 写入 temp PNG → OCR DLL 读回 → 删除临时文件
- `snap_ocr_extract_text_file()` 内部使用 Leptonica `pixRead()` 解码 PNG

**重构方案：**
```
不增加新 DLL。利用现有 snaplens_ocr.dll 的内存像素接口。

修改调用链：
QPixmap → QImage → bits() → snaplens_ocr.dll::snap_ocr_extract_text()
  (避免 PNG 编解码 + 磁盘 I/O)

Python 侧改动：
ocr/native_binding.py 新增函数：
  def extract_text_pixels(pixmap: QPixmap, ocr_langs: str) -> str
```

**具体实现：**

```python
# 新增到 snaplens/ocr/native_binding.py
def extract_text_pixels(pixmap: QPixmap, ocr_langs: str) -> str:
    """直接从 QPixmap 像素数据提取文字（跳过文件路径）。
    
    避免 PNG 编解码和磁盘 I/O 开销。
    """
    image = pixmap.toImage().convertToFormat(QImage.Format.Format_RGB888)
    width = image.width()
    height = image.height()
    bytes_per_line = image.bytesPerLine()
    
    ptr = image.constBits()
    # PySide6 返回 voidptr，需转为 ctypes 指针
    data_ptr = ctypes.cast(int(ptr), ctypes.POINTER(ctypes.c_ubyte))
    
    text_buf = ctypes.create_string_buffer(65536)
    error_buf = ctypes.create_string_buffer(1024)
    
    ret = _get_dll().snap_ocr_extract_text(
        data_ptr, width, height,
        3,  # bytes_per_pixel (RGB)
        bytes_per_line,
        text_buf, 65536,
        error_buf, 1024,
    )
    
    if ret == SNAP_OCR_OK:
        return text_buf.value.decode("utf-8", errors="replace").strip()
    raise RuntimeError(...)
```

**影响范围：**
- `ocr/native_binding.py` — 新增 `extract_text_pixels()`（~30 行）
- `core/ocr.py` — 新增像素路径判断逻辑
- `ui/ocr_service.py` — 可传入 QPixmap 而非文件路径
- `ai/openai_compat.py` — OCR 提取步骤改用像素路径

**工作量估计：** 1 天
- 无需新增 C++ DLL（利用现有接口）
- Python 绑定 + 调用链改写
- 回归测试（确保翻译管道不受影响）

---

### 候选 3：屏幕捕获 DPR 计算 🟢 P2 — 低优先级

**当前状态：**
- 位置：`core/capture.py` — `ScreenShot.physical_origin` 属性 + `_group_by_axis()` / `_sum_column_widths()` / `_sum_row_heights()`
- 多屏 DPR 混合场景下计算各屏幕在"物理像素桌面"中的偏移
- 算法涉及：屏幕分组（列/行），每组取最大物理宽度/高度，累加

**瓶颈分析：**
- 仅在一次截图开始时调用 1 次（每屏 1 次），不在热路径上
- Python 实现已经清晰且无性能问题
- 唯一价值：减少 Python 层的纯算法代码

**重构方案：**
```
如果实施，可作为 snaplens_platform.dll 的新接口：

  int snap_desktop_get_physical_origin(int screen_index,
                                        double* out_x, double* out_y);

该函数需要接收所有屏幕的 geometry + DPR，在 C++ 侧完成计算。
但由于需要 QScreen 信息（Qt），会增加 DLL 的 Qt 依赖。
```

**工作量估计：** 0.5-1 天（但价值有限）

**建议：** 暂不重构，Python 实现已经足够好且不在性能热路径上。

---

### 候选 4：Qt→Win32 键码映射表 🟢 P3 — 低优先级（维护性改动）

**当前状态：**
- 位置：`snaplens/platform/native_binding.py` — `_SPECIAL_KEYS` 字典 + F1-F24 计算逻辑
- 做用：将 Qt 键码（如 `Qt.Key.Key_A`）映射为 Win32 虚拟键码（如 `0x41`）
- 代码量约 30 行，功能稳定，几乎不需要修改

**重构方案：**
```
移动到 snaplens_platform.dll：

  unsigned int snap_key_qt_to_vk(int qt_key_code);  // 0 = 不支持

或者直接在热键注册接口中接收 Qt 键码，由 C++ 侧映射。

但这会打破现有 DLL 的"平台无关"设计（Win32 虚拟键码是 Windows 概念）。
```

**建议：** 暂不重构。代码量小且稳定，移动不会带来明显收益。

---

## 三、优先级排序与路线建议

```
P0 🔴 像素放大镜渲染引擎        → 即刻收益：每帧节省 60-80ms
P1 🟡 OCR 像素直传路径          → 显著收益：消除磁盘 I/O + PNG 编解码
P2 🟢 屏幕捕获 DPR 计算         → 边际收益：非热路径
P3 🟢 Qt→Win32 键码映射        → 几乎无收益：维护性改动
```

### 推荐实施顺序

```
Phase 1 (当前即可开始)
├── 候选 2 (OCR 像素直传)    ← 最快出效果，无需新增 DLL
└── 候选 1 (放大镜引擎)      ← 最大性能收益

Phase 2 (可选)
└── 候选 3 (DPR 计算)        ← 仅在需要完整迁移到纯 C++ 时做
```

---

## 四、长期路线展望

根据项目 README 中声明的「逐模块 Qt C++ 重写 → 最终纯 C++ Qt 应用」路线图，后续重构分两个阶段：

| 阶段 | 内容 | 特征 |
|------|------|------|
| **当前** | 补完所有 DLL 接口 | Python 仍是主进程，DLL 提供底层能力 |
| **未来** | UI 层逐步迁移 | 将 QMainWindow/QWidget → C++，Python 退化为启动器 |
| **最终** | 纯 C++ Qt 应用 | `main.cpp` + CMake，移除 Python 依赖 |

当前阶段应聚焦 **P0 + P1** 候选，确保 Python 层的性能瓶颈已全部消除，再考虑第二阶段。

---

## 五、技术决策参考

### 已确立的 C++ 重构约定（必须遵守）

1. **C ABI 稳定性**：所有导出函数 `extern "C"` + `__declspec(dllexport)`
2. **字符串传递**：AI/Platform 用 `wchar_t*` 宽字符；OCR 用 UTF-8 `char*`
3. **回调模式**：`CFUNCTYPE` + Python 侧保持引用防 GC + 跨线程仅 emit 信号
4. **错误码风格**：建议统一为 `0 = 成功 / <0 = 错误码`
5. **日志**：所有 C++ 模块通过 `snaplens_log.dll` 输出，格式 `[snap LEVEL file:line] msg`
6. **CMake**：C++17、MSVC `/W4 /permissive- /utf-8`、`MultiThreaded(/Debug)` 静态运行时

### DLL 依赖搜索

新增 DLL 的 Python 绑定层必须在 `_load_dll()` 前调用 `os.add_dll_directory()` 注册依赖路径：
- 新 DLL 本身：`native/bin/` 或 exe 同级
- 新 DLL 的依赖：如 Qt DLL、SDK DLL

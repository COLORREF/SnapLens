# C++ 原生模块编译笔记

> SnapLens 中 C++ 原生模块（log / platform / ai / ocr）的构建配置与经验总结。

---

## 一、统一日志方案

### 最终方案：snaplens_log.dll（qDebug + Qt 默认消息处理器）

```
Python (ctypes) / AI / Platform / OCR
    → snap_log_write / snap_log_write_msg (C ABI)
    → LogManager::write()
    → qSetMessagePattern("%{message}")
    → qDebug/qInfo/qWarning/qCritical
    → Qt 默认 stderr 输出（自动编码适配）
```

**设计要点**：
- 所有 C++ 模块通过 `#include <snaplens_log.h>` 使用 `SNAP_LOG_DEBUG/INFO/WARNING/ERROR` 宏
- Python 侧通过 `snaplens/log/__init__.py` 的 ctypes 绑定调用 `snap_log_write_msg`
- 日志格式：`[snap LEVEL file:line] msg`
- 线程安全：LogManager 内部有 mutex
- 跨平台编码：Qt 默认处理 Windows GBK / Linux UTF-8 / macOS UTF-8

### 曾经尝试的方案

| 方案 | 问题 | 结论 |
|------|------|------|
| C printf (fprintf) | 无级别控制，中文乱码（GBK vs UTF-8） | ❌ 已淘汰 |
| WriteConsoleW (Win32) | 引入 Win32 依赖，破坏跨平台 | ❌ 已淘汰 |
| qDebug + 自定义 handler | handler 中 toUtf8 在 Windows 仍乱码 | ❌ 已淘汰 |
| qDebug + 默认 handler | 完美 — Qt 自己处理编码 | ✅ 当前 |

### qSetMessagePattern + 级别映射

```cpp
void LogManager::write(int level, ...) {
    QString formatted = QStringLiteral("[snap %1 %2:%3] %4")...;
    switch (level) {
        case 0: qDebug()    << formatted; break;  // DEBUG
        case 1: qInfo()     << formatted; break;  // INFO
        case 2: qWarning()  << formatted; break;  // WARNING
        case 3: qCritical() << formatted; break;  // ERROR
    }
}
```

---

## 二、模块依赖关系

```
snaplens_log.dll  (依赖 Qt6::Core)
       ↑
  ┌────┼────┬────┐
  │    │    │    │
platform  ai  ocr  Python (ctypes)
(Win32)  (Qt6::Core   (直接调用
         +Network)    C ABI)
```

Platform/OCR 通过 C ABI 调用 log，自身不依赖 Qt。AI 同时使用 Qt Network 和 log。

---

## 三、标准库链接与库路径问题

### Qt6 Network 依赖 mpr.lib

当 Windows SDK 安装在非系统盘时，CMake 环境缺少 VS Developer Prompt 的 `LIB` 变量，导致 `mpr.lib` 找不到。

修复（`native/CMakeLists.txt`）：

```cmake
if(MSVC AND CMAKE_RC_COMPILER)
    cmake_path(GET CMAKE_RC_COMPILER PARENT_PATH _sdk_arch_dir)
    cmake_path(GET _sdk_arch_dir PARENT_PATH _sdk_version_dir)
    cmake_path(GET _sdk_version_dir FILENAME _sdk_version)
    cmake_path(GET _sdk_version_dir PARENT_PATH _sdk_bin_dir)
    cmake_path(GET _sdk_bin_dir PARENT_PATH _sdk_root)
    link_directories("${_sdk_root}/Lib/${_sdk_version}/um/x64")
endif()
```

---

## 四、其他编译问题

### MSVC 14.51 `/external:I` 与 `type_traits`

VS 2026 的 `/external:I` 机制导致通过 `qglobal.h` 间接包含的 `<type_traits>` 找不到：

```cmake
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)  # 必须在 find_package(Qt6) 之前
```

### Qt6 MOC 自动处理

AI 模块的 `ApiClient` 继承 `QObject` 并声明 `Q_OBJECT`，需在 CMake 中启用：

```cmake
set(CMAKE_AUTOMOC ON)  # ai/CMakeLists.txt
```

### Qt6 `QJsonValueRef::operator[]` 不兼容 `const char*`

所有 JSON 键访问需用 `QLatin1StringView`：

```cpp
// 错误（Qt6）
obj["message"]

// 正确
obj[QLatin1StringView("message")]
```

### MSVC 运行时对齐

所有模块使用一致的 `/MT`（静态运行时），避免混用 `/MD` 导致的符号冲突：

```cmake
set_property(TARGET snaplens_xxx PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
```

---

## 五、Python ctypes 加载 DLL 的依赖搜索

Python 3.8+ 的 `ctypes.CDLL()` 使用 `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`，**不搜索系统 PATH**。

各模块的 Python 绑定层在调用 `_load_dll()` 前通过 `os.add_dll_directory()` 注册依赖路径：

| 模块 | 注册路径 | 用途 |
|------|---------|------|
| AI | Qt bin 目录 | Qt6Core / Qt6Network DLL |
| Platform | native/bin | snaplens_log.dll |
| OCR | sdk/tesseract/bin | tesseract55 + leptonica + 图像解码 DLL |
| OCR | native/bin | snaplens_log.dll |

查找 Qt bin 的逻辑：

```python
def _find_qt_bin_dir() -> Path | None:
    for var in ("QT6_DIR", "QTDIR"):
        ...
    for base in (Path("D:/ProgramFiles/Qt"), Path("C:/Qt")):
        versions = sorted(base.glob("6.*"), reverse=True)
        ...
```

---

## 六、OCR 模块编译

OCR 模块通过 Tesseract 5.5 C API + Leptonica 实现，不需要 Qt：

```cmake
# native/ocr/CMakeLists.txt
target_include_directories(snaplens_ocr PRIVATE
    ${TESSERACT_SDK_DIR}/include     # tesseract/ + leptonica/
)

target_link_libraries(snaplens_ocr PRIVATE
    snaplens_log
    tesseract55          # Release
    leptonica-1.87.0
)

# Debug 版本自动选择 tesseract55d / leptonica-1.87.0d
```

更多细节见 `docs/ocr-paths.md`。

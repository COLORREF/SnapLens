# OCR 路径信息汇总

> 所有路径均以项目根目录 `D:\Code\WIP\SnapLens` 为基准，PyInstaller 打包后以 exe 同级目录为基准。

---

## 一、目录总览

```
<base>/                                    # 开发模式 = 项目根；打包模式 = exe 目录
├── snaplens_ocr.dll                       # [运行时] 编译产物
├── native/
│   ├── bin/
│   │   └── snaplens_ocr.dll               # [运行时] CMake 编译输出 (开发)
│   └── ocr/
│       ├── CMakeLists.txt                 # [编译] 决定 include/lib 路径
│       ├── include/snaplens_ocr.h
│       └── src/*.cpp
├── sdk/
│   └── tesseract/
│       ├── include/                       # [编译] 头文件 (tesseract/ + leptonica/)
│       ├── lib/                           # [编译] .lib 链接库 (Release/Debug)
│       ├── bin/                           # [运行时] DLL 依赖链
│       │   ├── tesseract55.dll            #     snaplens_ocr.dll → 此
│       │   ├── leptonica-1.87.0.dll       #     → 依赖以下图像解码 DLL
│       │   ├── libpng16.dll               #     Leptonica 图像格式支持
│       │   ├── jpeg62.dll
│       │   ├── tiff.dll
│       │   ├── libwebp.dll / libwebpdecoder.dll / libwebpmux.dll
│       │   ├── gif.dll
│       │   ├── openjp2.dll
│       │   ├── turbojpeg.dll
│       │   ├── z.dll / zstd.dll / lz4.dll / bz2.dll / liblzma.dll
│       │   └── libsharpyuv.dll / libwebpdemux.dll
│       └── tessdata/                      # [运行时] 语言包目录
│           ├── chi_sim.traineddata
│           ├── eng.traineddata
│           └── ...
└── C:\Program Files\Tesseract-OCR\        # [可选] 系统安装版
    └── tessdata/                          #   后备语言包路径
```

---

## 二、编译时路径

定义在 `native/ocr/CMakeLists.txt`：

| 配置项 | 路径 |
|--------|------|
| SDK 根 | `${CMAKE_SOURCE_DIR}/../sdk/tesseract` |
| include | `sdk/tesseract/include/`（含 `tesseract/` 和 `leptonica/` 子目录） |
| lib (Release) | `sdk/tesseract/lib/tesseract55.lib` |
| lib (Release) | `sdk/tesseract/lib/leptonica-1.87.0.lib` |
| lib (Debug) | `sdk/tesseract/lib/tesseract55d.lib` |
| lib (Debug) | `sdk/tesseract/lib/leptonica-1.87.0d.lib` |
| 产物 | `native/bin/snaplens_ocr.dll` |

CMake 生成器表达式自动切换 Debug/Release 库名：
```cmake
target_link_libraries(snaplens_ocr PRIVATE
    $<$<CONFIG:Debug>:tesseract55d>
    $<$<NOT:$<CONFIG:Debug>>:tesseract55>
    ...
)
```

---

## 三、运行时路径

### 3.1 snaplens_ocr.dll 查找

`_load_dll()` 在 `sanaplens/ocr/native_binding.py`，查找顺序：

| 场景 | 候选路径 |
|------|---------|
| 开发模式 | ① `native/bin/snaplens_ocr.dll` |
| | ② `native/cmake-build-*/bin/snaplens_ocr.dll`（CLion 构建目录） |
| 打包模式 | ③ `<exe_dir>/snaplens_ocr.dll`（exe 同级） |

### 3.2 SDK 依赖 DLL 查找

`snaplens_ocr.dll` 运行时依赖 `tesseract55.dll` + `leptonica-1.87.0.dll` + 图像 DLL 链。

`_find_sdk_bin()` 在 `sanaplens/ocr/native_binding.py`，通过 `os.add_dll_directory()` 注册：

| 场景 | 路径 |
|------|------|
| 开发模式 | `<project>/sdk/tesseract/bin/` |
| 打包模式 | `<exe_dir>/sdk/tesseract/bin/` |

验证条件：目录下存在 `tesseract55.dll`。

### 3.3 tessdata / 语言包查找

两个独立实现（返回值相同，均为 tessdata 目录本身）：

| 位置 | 调用方 |
|------|--------|
| `snaplens/core/ocr.py:find_tessdata_dir()` | settings_dialog, setup_wizard |
| `snaplens/ocr/native_binding.py:find_tessdata_dir()` | extract_text (OCR 流程) |

**查找优先级**（在 base 目录下依次尝试）：

| 优先级 | 子路径 | 示例 |
|--------|--------|------|
| 1 | `sdk/tesseract/tessdata/` | `D:\...\SnapLens\sdk\tesseract\tessdata\` |
| 2 | `tesseract/tessdata/` | `D:\...\SnapLens\tesseract\tessdata\` |
| 3 | `tessdata/` | `D:\...\SnapLens\tessdata\` |
| 4 | `C:\Program Files\Tesseract-OCR\tessdata\` | 系统安装版 |
| 5 | `C:\Program Files (x86)\Tesseract-OCR\tessdata\` | 系统安装版（32位） |

找到目录后验证其中至少有一个 `.traineddata` 文件。

**fallback 机制**（`core/ocr.py` 专用）：
- 所有候选都无语言包 → 返回 `sdk/tesseract/tessdata/` 路径
- 永不返回 None，保证 settings_dialog 下载界面始终可用

### 3.4 Tesseract 引擎 data_path

传给 `snap_ocr_init(data_path, language)` 的值：

```
data_path = <tessdata 目录本身>
例: D:\Code\WIP\SnapLens\sdk\tesseract\tessdata
```

Tesseract 5.5 直接在该目录下查找 `chi_sim.traineddata` 等文件，不会自动追加 `tessdata/` 子路径。

### 3.5 语言包下载路径

`settings_dialog.py` 中 `_OcrLangManager`：

```
下载目标 = find_tessdata_dir() + "/" + code + ".traineddata"
例: D:\Code\WIP\SnapLens\sdk\tesseract\tessdata\chi_sim.traineddata
```

CDN 源：`https://cdn.jsdelivr.net/gh/tesseract-ocr/tessdata_fast@main/{code}.traineddata`

下载前会 `os.makedirs(tessdata_dir, exist_ok=True)` 确保目录存在。

---

## 四、打包后的部署结构

PyInstaller 打包后，exe 同级目录应如下：

```
SnapLens 发布版/
├── SnapLens.exe                  # PyInstaller 打包主程序
├── snaplens_platform.dll         # 热键 / 窗口 / Esc / 光标
├── snaplens_ai.dll               # AI 翻译通信
├── snaplens_ocr.dll              # OCR 识别
├── Qt6Core.dll                   # PyInstaller 自动打包 PySide6 带
├── Qt6Network.dll                # snaplens_ai.dll 依赖
├── Qt6Gui.dll / Qt6Widgets.dll
├── ...
└── sdk/
    └── tesseract/
        ├── bin/                  # tesseract55.dll + leptonica + 图像解码 DLL
        └── tessdata/             # .traineddata 语言包
```

Python 绑定层在 frozen 模式下的查找自动适配：
- `snaplens_ocr.dll` → `<exe_dir>/snaplens_ocr.dll`
- SDK DLL → `<exe_dir>/sdk/tesseract/bin/`
- tessdata → `<exe_dir>/sdk/tesseract/tessdata/`

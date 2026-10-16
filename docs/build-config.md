# CMake 构建配置

主 `CMakeLists.txt` 只创建目标并调用 `cmake/` 下的功能模块：`SnapLensOptions` 定义开关和路径，`SnapLensQt` 查找 Qt，`SnapLensOcr` 链接 OCR SDK，`StaticJpeg` 处理双静态 JPEG ABI，`SnapLensTarget` 负责编译选项，`SnapLensDeploy` 负责发布，`SnapLensLogging` 编译静态 spdlog 与日志适配库。CMake 配置提示和项目自定义错误均使用简体中文；第三方 Qt/CMake 工具自身的输出仍可能是英文。

## 开关与路径

所有路径可通过 `cmake -D名称=值` 覆盖。更换 Qt、OCR SDK 或链接方式时请使用**新的构建目录**，避免 `Qt6_DIR` 等旧缓存混入。

| 变量 | 默认值 / 用途 |
| --- | --- |
| `SNAPLENS_QT_LINKAGE` | `STATIC`；可设 `SHARED`。CMake 会核对 `QT_ROOT` 对应的 Qt 库类型。 |
| `SNAPLENS_OCR_LINKAGE` | `STATIC`；可设 `SHARED`。静态模式使用本仓库 SDK 的版本化库名，动态模式使用下面的导入库。 |
| `SNAPLENS_SPDLOG_DIR` | `sdk/spdlog`，固定为 1.17.0 源码。 |
| `SNAPLENS_BUILD_TESTS` | 默认 `OFF`；启用日志专项 CTest，测试 EXE 不进入发布目录。 |
| `QT_ROOT` | `sdk/qt-6.11.2-static`；必须包含 `lib/cmake/Qt6/Qt6Config.cmake`。 |
| `TESSERACT_SDK_DIR` | `<仓库>/sdk/tesseract`；须包含 `include/tesseract`、`include/leptonica` 和 `lib`。 |
| `ELA_WIDGETTOOLS_DIR` | `<仓库>/sdk/ElaWidgetTools`；包含 `ElaWidgetTools.hpp/.cpp/.qrc`。 |
| `SNAPLENS_DIST_DIR` | 双静态为 `<仓库>/dist-static`，其他组合为 `<仓库>/dist`；发布目录。 |
| `SNAPLENS_TESSERACT_IMPORT_LIB`、`SNAPLENS_LEPTONICA_IMPORT_LIB` | 动态 OCR 时必填：对应 `.lib` **导入库完整路径**。 |
| `SNAPLENS_TESSERACT_DLL`、`SNAPLENS_LEPTONICA_DLL` | 动态 OCR 时必填：对应 `.dll` **运行库完整路径**。 |
| `SNAPLENS_OCR_DLL_DIR` | 动态 OCR 时必填：上述 DLL 及其他非系统依赖所在目录。 |

本机静态 SDK 只有 `.lib`，没有用于测试动态 OCR 模式的 DLL。其他机器若使用 vcpkg 或自建动态 SDK，应保证头文件、导入库、DLL 来自**同一次构建**；不要把静态 `.lib` 当成导入库使用。

## 构建示例

在 VS x64 开发者终端中运行。默认双静态：

```powershell
cmake -S . -B cmake-build-static -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-static -j 4
```

共享 Qt + 静态 OCR：

```powershell
cmake -S . -B cmake-build-qt-shared -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DSNAPLENS_QT_LINKAGE=SHARED -DQT_ROOT=D:/ProgramFiles/Qt/6.11.1/msvc2022_64
cmake --build cmake-build-qt-shared -j 4
```

共享 OCR：以下示例沿用默认静态 Qt；将 OCR 路径换为实际 SDK 产物，不要直接照抄示例文件名。

```powershell
cmake -S . -B cmake-build-ocr-shared -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DSNAPLENS_OCR_LINKAGE=SHARED `
  -DTESSERACT_SDK_DIR=C:/deps/tesseract `
  -DSNAPLENS_TESSERACT_IMPORT_LIB=C:/deps/tesseract/lib/tesseract.lib `
  -DSNAPLENS_LEPTONICA_IMPORT_LIB=C:/deps/tesseract/lib/leptonica.lib `
  -DSNAPLENS_TESSERACT_DLL=C:/deps/tesseract/bin/tesseract.dll `
  -DSNAPLENS_LEPTONICA_DLL=C:/deps/tesseract/bin/leptonica.dll `
  -DSNAPLENS_OCR_DLL_DIR=C:/deps/tesseract/bin
cmake --build cmake-build-ocr-shared -j 4
```

动态 Qt 由 `windeployqt` 部署 Qt DLL 与插件；动态 OCR 从指定的两份 DLL 出发递归收集非 Windows 系统 DLL。双静态只复制 EXE，存在时复制 tessdata 语言包。语言包是独立数据文件，不随静态链接进入 EXE。动态发布前应检查输出目录，避免旧版本 DLL 残留。

## JPEG ABI 与 CRT

本机 Qt 6.11.2 静态 JPEG 插件使用 `JPEG_LIB_VERSION=80`，静态 OCR SDK 使用 `JPEG_LIB_VERSION=62`。两者提供重名 `jpeg_*` 符号，双静态链接时可能链接成功、读图时崩溃。`StaticJpeg.cmake` **仅在 Qt 和 OCR 同时静态**时生效：排除 Qt 原 JPEG 插件，使用同版本源码和 OCR SDK 的头文件重新编译插件，统一链接 OCR SDK 的 `jpeg.lib`。该适配器依赖 Qt 私有 API，升级静态 Qt 时须同步升级 `third_party/qtjpeg`。其余组合的 Qt 和 OCR JPEG 实现分别位于 EXE/DLL 中，不使用此适配器。详见[静态构建记录](static-build.md)。

本机静态库采用 `/MT`；只要其中一侧静态，应用也使用 `/MT`。两侧均动态时应用使用 `/MD`。自备 SDK 与 Qt 若使用不同架构、Debug/Release 或不兼容 CRT，需重编译相应依赖后再链接。本次双静态构建和 UI 冒烟通过；共享 Qt + 静态 OCR 已构建并完成 UI 冒烟。动态 OCR 因本机缺少 DLL，尚未完成实际链接和运行验证。

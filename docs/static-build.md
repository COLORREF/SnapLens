# Qt 与 Tesseract 静态构建

当前默认配置：Qt 6.11.2 静态 Release x64、Tesseract 5.5.2 静态 SDK、MSVC `/MT`。已生成 `dist-static/SnapLens.exe`；不需要 Qt、OCR 或 VC 运行库 DLL，仍使用 Windows 系统 DLL。语言包是独立数据文件。

## 冲突的准确位置

两边均使用 libjpeg-turbo **3.2.0**，但构建时选择了不同的兼容 ABI：

| 消费方 | 头文件 | JPEG_LIB_VERSION |
| --- | --- | --- |
| 已安装 Qt 静态 JPEG 插件 | `D:/ProgramFiles/Qt/6.11.2/msvc2026_x64_MT_static_release/include/QtJpeg/jconfig.h` | 80 |
| OCR SDK / Leptonica | `sdk/tesseract/include/jconfig.h` | 62 |

Qt 的 `plugins/imageformats/qjpeg.lib` 按 ABI 80 编译，SDK 的 `lib/jpeg.lib` 提供 ABI 62。Qt 的 `lib/Qt6BundledLibjpeg.lib` 和 SDK 都导出未加前缀的 `jpeg_CreateCompress`、`jpeg_CreateDecompress` 等符号；静态链接器可以从其中一个库解析符号，并不验证调用者使用的 C 结构体 ABI。

原链接结果使 Qt 插件调用到了 SDK 的 ABI 62 实现。Ela 初始化窗口时解码资源 `:/include/Image/Cirno.jpg`，触发 `Wrong JPEG library version: library is 62, caller expects 80`。这不是图片文件格式问题，也不是静态 Qt 和静态 Tesseract 天生不兼容。只改变链接顺序可能把问题转移到 Leptonica；只改版本宏则会掩盖结构体布局不兼容。

## 已实施方案

`cmake/StaticJpeg.cmake` 在静态构建中排除原有 `Qt6::QJpegPlugin`，用 Qt **v6.11.2 原版 JPEG 插件源码**重新构建 `SnapLensJpegPlugin`，包含 OCR SDK 的 JPEG 头文件并链接同一份 SDK `jpeg.lib`。应用静态注册新插件，因此仍可通过 QImage 正常读写 JPG。

最终链接不再包含原 `qjpeg.lib` 或 `Qt6BundledLibjpeg.lib`。Qt、Leptonica 统一使用 SDK JPEG ABI，Tesseract 和 Qt 仍为静态链接。没有修改已安装的 Qt 或 OCR SDK，也没有禁用 JPG。

插件源码位于 `third_party/qtjpeg/`，原始 URL、SHA-256 与许可证随源码保留，构建无需下载。插件使用 Qt 私有 API，因此当前明确要求 Qt 6.11.2；升级 Qt 时必须同步插件源码、版本检查和来源记录。

## 构建

打开 VS x64 开发者终端，在仓库根目录运行（需要 CMake 和 Ninja 在 PATH 中）：

```powershell
cmake -S . -B cmake-build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_ROOT=D:/ProgramFiles/Qt/6.11.2/msvc2026_x64_MT_static_release
cmake --build cmake-build-static -j 4
```

本页记录默认双静态构建；共享 Qt 或动态 OCR 的开关、路径见[构建配置说明](build-config.md)。切换链接方式时使用新的构建目录，避免旧缓存中的 Qt6_DIR 指向其他 Qt 安装。当前 SDK 是 Release x64 `/MT`，不要混用 Debug、不同架构或不兼容的 CRT。

默认发布到 `dist-static/`，可通过 `-DSNAPLENS_DIST_DIR=...` 覆盖。无需 windeployqt。若 `sdk/tesseract/tessdata/` 存在语言包，会复制到发布目录同名子目录；也可在软件设置中指定已有语言包位置。

## JPEG 适配验证记录（此前环境）

- 全新 `cmake-build-static` Release 构建成功，增量编译新增 JPEG 回归测试成功。
- `SnapLens.map` 中 `jpeg_CreateCompress`、`jpeg_CreateDecompress`、`jpeg_std_error` 均解析到 SDK 的 `jpeg` 对象；链接清单不包含 Qt 原有 JPEG 插件及 bundled JPEG 库。
- `dumpbin /dependents dist-static/SnapLens.exe` 只有 Windows 系统依赖，无 Qt、Tesseract、Leptonica、JPEG、MSVCP 或 VCRUNTIME DLL。
- PATH 仅保留 Windows/System32 与 Windows 时，`cmake-build-static/SnapLens.exe --regression-checks` 退出码 0，报告 PASS。
- 回归包含 Qt 写 JPEG → Leptonica 读、Leptonica 写渐进 JPEG → Qt 读、PNG/BMP、实际英文 OCR，以及关闭生命周期、设置、截图、本地模拟 HTTP JSON/SSE/错误/取消。
- 同样精简 PATH 后，发布目录 `dist-static/SnapLens.exe --ui-smoke-dialogs` 退出码 0。

英文 OCR 回归需要在测试 EXE 同级 `tessdata/eng.traineddata` 放置英文模型。本次使用已有官方 tessdata_fast 测试数据，未将语言包加入源码或发布包。回归结果与截图仍输出至 `cmake-build-release-visual-studio/regression/`。上述验证在本机完成，不替代所有目标 Windows 版本和线上 AI 服务验证。

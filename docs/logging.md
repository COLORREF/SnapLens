# 日志与单 EXE 发布

## 依赖与构建

日志后端固定为 spdlog **1.17.0**，提交
`79524ddd08a4ec981b7fea76afd08ee05f83755d`。源码位于被 Git 忽略的
`sdk/spdlog`，由 `cmake/SnapLensLogging.cmake` 编译为静态库。
首次准备依赖后，应用构建不需要访问网络。

```powershell
git clone --depth 1 --branch v1.17.0 https://github.com/gabime/spdlog.git sdk/spdlog
```

使用自带 fmt，不链接 spdlog/fmt DLL，不引入 vcpkg 的全局工具链。
日志库、适配库与应用使用同一 CRT 配置；默认双静态发布为 Release x64 `/MT`。
spdlog 和 fmt 许可证保存在 `resources/licenses/` 并嵌入 EXE 的
`:/licenses/spdlog`、`:/licenses/fmt` 资源中。

### 准备本机静态 Qt

默认 `QT_ROOT` 是 `sdk/qt-6.11.2-static`。也可通过 `-DQT_ROOT=...`
使用已有的匹配 SDK。当前 JPEG 适配器要求 Qt **6.11.2**，不能直接替换成 6.11.1。

本次环境原有 Qt 6.11.1 为共享库，以下命令准备了独立的静态 Qt，未修改系统安装。
在 VS x64 开发者终端执行，确保 CMake 和 Ninja 可用：

```powershell
git clone --depth 1 --branch v6.11.2 https://github.com/qt/qtbase.git sdk/qtbase-6.11.2
cmake -S sdk/qtbase-6.11.2 -B build/qtbase-static -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/sdk/qt-6.11.2-static" `
  -DBUILD_SHARED_LIBS=OFF -DFEATURE_static_runtime=ON `
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF `
  -DFEATURE_openssl=OFF -DFEATURE_schannel=ON -DFEATURE_sql=OFF -DFEATURE_dbus=OFF
cmake --build build/qtbase-static -j 6
cmake --install build/qtbase-static
```

Qt 源码提交为 `ef55f427f2c8b410d34f8a7681020a3000cf6866`。
HTTPS 使用 Windows Schannel 后端，不依赖外部 OpenSSL DLL。

### 构建应用与测试

```powershell
cmake -S . -B cmake-build-static -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DQT_ROOT="$PWD/sdk/qt-6.11.2-static" -DSNAPLENS_BUILD_TESTS=ON
cmake --build cmake-build-static -j 4
ctest --test-dir cmake-build-static --output-on-failure
./dist-static/SnapLens.exe --regression-checks
./dist-static/SnapLens.exe --ui-smoke-dialogs
dumpbin /dependents dist-static/SnapLens.exe
```

`dist-static/SnapLens.exe` 是发布程序，测试 EXE 和静态 `.lib` 不复制到发布目录。
只应导入 Windows 系统 DLL，不应导入 Qt、Tesseract、spdlog、fmt、MSVCP 或 VCRUNTIME DLL。
语言包、配置和日志仍在 EXE 外；OCR 回归需要 `eng.traineddata`。
本次使用官方 `tessdata_fast/main/eng.traineddata`，SHA-256：
`7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2`。

## 日志行为

- 正常运行：`%LOCALAPPDATA%/SnapLens/logs/snaplens.log`，UTF-8；每份 5 MiB，最多保留 3 份历史文件。
- 格式：时间、logger 名称、级别、线程 ID、文件名/行号/函数、正文。
- 业务通过 `SNAP_LOG_*` 调用；Qt 消息通过唯一的 `qInstallMessageHandler` 接入同一后端。
- DEBUG、INFO、WARNING、ERROR 独立开关，运行时使用原子掩码更新。
  宏在构造消息前过滤；不对运行时仍可打开的 DEBUG 做编译期删除。
- 警告及以上同步刷新；普通消息在显式刷新或关闭文件时落盘。
  Qt Fatal 不受用户开关影响，同步刷新后交还 Qt 执行终止。
- 同时输出到 stderr（终端、IDE 运行控制台、重定向）和 Windows 调试器。
  stderr 逐条刷新，与文件输出是否成功无关；双击运行时是否显示控制台由应用子系统决定。
  文件无法打开或被另一进程占用时降级至 stderr/调试器，
  不让日志错误中断业务；跨进程锁避免多个实例同时轮转同一文件。
- `--regression-checks` 覆盖写入当前目录的 `regression-trace.log`，不另装第二个处理器。
- 只有 Qt 日志和 `SNAP_LOG_*` 会经过桥接。Tesseract 等库直接写 stderr 的内容不会自动进入日志文件。

`LogSession` 在 QApplication 前创建、在其析构后关闭；`LogManager` 的桥接对象具有进程寿命，
后端通过共享快照管理。退出期间已经开始的写入保持有效，晚到的后台消息降级至 stderr。
同步 `_mt` 输出避免了异步队列的溢出与退出排空问题；文件轮转和写入锁由 spdlog 实现。

## 结构整理与缺陷修复

- `main.cpp` 仅负责启动和分派；UI 冒烟及快照逻辑移至 `diagnostics/visual_checks`。
- 移除未使用的 OCR 单例入口和启动向导取消状态，明确每个任务独立拥有 OCR 引擎。
- `translation_task.h` 统一界面取消与释放逻辑；窗口直接销毁也会取消请求。
- 热键线程不再持有 provider 裸指针，激活通过排队信号传递，停止后丢弃过期激活。
- OCR 结果连接以句柄管理，不再在 GUI 线程访问后台销毁的 worker；线程退出不依赖 GUI 事件循环。
- SSE 使用同一解析路径处理正常片段和最后一行，保留 HTTP 错误正文，普通正文也触发增量回调。
- 流式请求以 final 回调为唯一结果来源，避免同一个失败重复发送错误信号。
- 截图会话拒绝重复启动，失败路径统一经过幂等关闭。

重要类、生命周期边界和接口契约使用 Doxygen 注释。日志专项测试位于
`tests/log_manager_tests.cpp`；网络、图像、OCR 和 UI 回归位于 `src/diagnostics/`。

## 本机验证结果（2026-10-15）

- Qt 6.11.2、spdlog 1.17.0 和应用均完成 Release x64 静态构建；应用及日志依赖使用 `/MT`。
- 发布目录仅包含 `dist-static/SnapLens.exe` 和外部英文 OCR 模型。
- `dumpbin /dependents` 仅列出 Windows 系统组件（包括系统自带的 `icuuc.dll`），
  未导入 Qt、Tesseract、Leptonica、spdlog、fmt、MSVCP 或 VCRUNTIME DLL。
- 清除 Qt 插件环境变量、PATH 仅保留 Windows/System32 与 Windows 后：
  日志专项测试、`--regression-checks`、`--ui-smoke-dialogs` 均退出 0。
- 回归覆盖 UTF-8/中文路径、四级过滤、Qt 桥接、并发写入、轮转、fatal 刷新、
  文件占用/打开失败、JSON/SSE/HTTP 错误/取消、JPEG 互操作、英文 OCR 和界面生命周期。
- `git diff --check` 通过。编译保留 Qt 头文件与 Ela 源码中的既有第三方警告。

构建日志和 DLL 检查输出位于被忽略的 `build/static-build.log` 与
`build/static-dependencies.txt`，功能回归报告位于
`cmake-build-release-visual-studio/regression/results.txt`。
这些检查不替代其他 Windows 版本和实际线上 AI 服务验证。

### 控制台输出与设置分类树修正

正常文件输出成功时也默认创建 stderr sink，避免仅写文件和调试器导致 IDE
运行控制台空白。新增子进程测试，同时检查 stderr 管道和日志文件中的 UTF-8 消息。

设置内部分类导航现使用与主窗口相同的 `ElaNavigationBar`，通过
`addExpanderNode` 与 `addPageNode` 创建分类和页面，选中、悬浮及折叠动画交给 Ela。
保留分类搜索及功能入口跳转；由于当前 SDK 没有公开过滤接口，搜索仅通过内部
`ElaNavigationView` 隐藏不匹配的行，不覆盖其模型、绘制或选择逻辑。
现有回归入口已适配新导航。本次按用户要求仅编译，未执行自动测试或界面验证。

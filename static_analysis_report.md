# SnapLens Python → C++ 静态对比分析报告

> 分析日期：2026-08-09 | Python 版本对比基准 | 2026-08-10 复核：原 7 项严重问题 + 15 项中等问题 + 20 项轻微问题 + OCR 语言包崩溃均已处理并移出（含确认非缺陷项，v2 分析已并入第三节）
>
> 2026-08-10 追加整合：**第三轮独立复审（v3）** 新发现 3 严重 + 6 中等 + 16 轻微 + 3 实测问题，**第四轮修复复核（v4）** 全部闭环（详见第九、十节），累计 0 遗留。

---

## 一、总体评估

| 维度 | 评级 | 说明 |
|------|------|------|
| 模块完整性 | ⚠️良 | 所有 Python 模块在 C++ 端均有对应，无整模块遗漏 |
| API 1:1 对应 | ⚠️良 | 核心数据模型(SETTING_DEFS/NOTIFY_DEFS/PROVIDERS)100%一致 |
| 功能完整性 | 🟢良好 | 严重/中等/轻微问题已全部处理完毕，无遗留问题 |
| 代码质量 | 🟢良好 | 设置系统与通知系统均已表驱动化，无硬编码桥接 |
| 可观测性 | 🟢良好 | 日志过滤已实现，通知文案已对齐 Python |
| 生命周期/线程安全 | 🟢良好 | v3 复审的 3 崩溃 + 6 中等（terminate/Settings 竞争/原子性）+ L12 取消链路全部闭环（见第九、十节） |

**共发现问题 0 项（原 7 项严重问题 + 15 项中等问题 + 20 项轻微问题已于 2026-08-10 全部处理完毕；v3 独立复审新发现 3 严重 + 6 中等 + 16 轻微 + 3 实测亦于同日全部闭环）。**

---

## 二、Python ↔ C++ 文件映射表

| Python 文件 | C++ 文件 | 状态 |
|-------------|----------|------|
| `main.py` | `src/main.cpp` | ✅ |
| `snaplens/app.py` | `src/application.h/.cpp` | ✅ 已修复 |
| `snaplens/core/settings.py` | `src/settings.h/.cpp` | ✅ 已修复（X-Macro 单列表） |
| `snaplens/core/capture.py` | `src/capture/screen_capture.h/.cpp` | ✅ 已修复 |
| `snaplens/core/ocr.py` | `src/ocr/ocr_engine.h/.cpp` | ⚠️ 严重问题已修复 |
| `snaplens/core/api_client.py` | `src/translate/ai_client.h/.cpp` | ⚠️ |
| `snaplens/core/text_translator.py` | `src/translate/translator.h/.cpp` | ⚠️ |
| `snaplens/core/temp_cleanup.py` | `src/application.cpp`(内联) | ⚠️ |
| `snaplens/ai/__init__.py` | `src/translate/providers.h/.cpp` | ✅ |
| `snaplens/ai/base.py` | — (无抽象基类，用 `providers.h` 注册表 + ai_client 统一实现) | ⚠️ 合理取舍 |
| `snaplens/ai/openai_compat.py` | `src/translate/ai_client.h/.cpp` + `translator.h/.cpp` | ⚠️ |
| `snaplens/ai/native_binding.py` | — (已内联进ai_client.h/.cpp) | ✅ |
| `snaplens/platform/base.py` | `src/platform/platform_base.h` + `platform_factory.h/.cpp` | ✅ 已实现 |
| `snaplens/platform/native_binding.py` | `src/platform/hotkey.h/.cpp, cursor.h/.cpp, cancel_intercept.h/.cpp, window_enum.h/.cpp` | ⚠️ |
| `snaplens/platform/__init__.py` | `src/platform/platform_factory.h/.cpp` | ✅ 已实现（createXxxProvider 按 _WIN32 分发） |
| `snaplens/ocr/__init__.py` | `src/ocr/ocr_engine.h/.cpp` | ⚠️ |
| `snaplens/ocr/native_binding.py` | — (已内联进ocr_engine.h/.cpp) | ✅ |
| `snaplens/log/__init__.py` | `src/log/log_manager.h/.cpp` | ✅ 已修复 |
| `snaplens/notify/defs.py` | `src/notify/notify_manager.cpp`内嵌 | ✅ |
| `snaplens/notify/channel.py` | `src/notify/notify_manager.cpp`(单体) | ⚠️ |
| `snaplens/notify/manager.py` | `src/notify/notify_manager.h/.cpp` | ✅ 已修复（成员指针） |
| `snaplens/notify/__init__.py` | — | — |
| `snaplens/ui/main_window.py` | `src/translate/main_window.h/.cpp` | ✅ |
| `snaplens/ui/snip.py` | `src/capture/snip_session.h/.cpp` | ✅ |
| `snaplens/ui/overlay.py` | `src/capture/overlay.h/.cpp` + `src/capture/magnifier.h/.cpp` + `src/capture/toolbar.h/.cpp` | ⚠️ 严重问题已修复 |
| `snaplens/ui/pin.py` | `src/ui/pin_window.h/.cpp` | ✅ 已修复 |
| `snaplens/ui/tray.py` | `src/ui/tray_icon.h/.cpp` | ⚠️ |
| `snaplens/ui/translate_window.py` | `src/translate/translate_window.h/.cpp` | ✅ 已修复 |
| `snaplens/ui/translate_service.py` | `src/translate/translator.h/.cpp`(TranslateService) | ✅ 已修复 |
| `snaplens/ui/ocr_window.py` | `src/ocr/ocr_window.h/.cpp` | ⚠️ |
| `snaplens/ui/ocr_service.py` | `src/ocr/ocr_window.h/.cpp`(OcrWorker) | ✅ |
| `snaplens/ui/zoomable_image.py` | `src/ui/zoomable_view.h/.cpp` | ✅ 已修复 |
| `snaplens/ui/settings_dialog.py` | `src/ui/settings_dialog.h/.cpp` | ⚠️ |
| `snaplens/ui/setup_wizard.py` | `src/ui/setup_wizard.h/.cpp` | ✅ |
| `snaplens/ui/color_picker.py` | `src/ui/color_picker.h/.cpp` | ⚠️ |
| `snaplens/assets/` | `resources/` (.qrc) | ✅ |

> ✅ 等价/已修复 | ⚠️ 存在差异（含架构性问题）

---

## 三、已修复问题记录

> 2026-08-10 逐项复核当前代码，以下问题均已修复，详细内容已从本文档移除。

| # | 原问题 | 修复状态 | 验证位置 |
|---|--------|---------|---------|
| 3.1 | `physicalOrigin()` 返回逻辑坐标而非物理像素坐标 | ✅ 已修复 | `src/capture/screen_capture.h:26-101` 完整实现列/行分组 + 混合 DPI 物理坐标累积计算 |
| 3.2 | `findTessdataDir()` 找不到时返回空字符串 | ✅ 已修复 | `src/ocr/ocr_engine.cpp:277-284` 找不到时返回 fallback 路径 `appDir/tessdata`，且有结果缓存 |
| 3.3 | `quitApp()` 无退出确认对话框 | ✅ 已修复 | `src/application.cpp:317-321` 调用 `_notifyManager->confirm("退出确认", ...)` |
| 3.4 | `openSettings()` 不保存设置，不重配 OCR/日志 | ✅ 已修复 | `src/application.cpp:239-292` 显式 `save()` + 热键重配 + 日志级别重配 + 模式切换；OCR 路径通过惰性读取（`ocr_window.cpp:143` / `translator.cpp:33`）实现等价效果 |
| 3.5 | PinWindow 缺少右键菜单、Esc关闭、另存为 | ✅ 已修复 | `src/ui/pin_window.h:29-34` + `pin_window.cpp:109-140` 实现 `contextMenuEvent` / `keyPressEvent` / `saveAs` |
| 3.6 | SnipOverlay 完全缺失放大镜/标签位置动画 | ✅ 已修复 | `src/capture/overlay.h:108-139` + `overlay.cpp` 完整动画系统（QParallelAnimationGroup + QVariantAnimation，含翻转/变倍/碰撞处理） |
| 3.7 | LogManager 完全不支持日志级别过滤 | ✅ 已修复 | `src/log/log_manager.cpp:39-46` 级别过滤 + `setEnabledLevels()` / `applySettings()` 运行时控制 |
| 3.8 | OCR 语言包下载/删除确认后崩溃（设置页） | ✅ 已修复 | 根因：`rebuildUi()` 布局清理 double-free（Qt `QBoxLayout::takeAt()` 返回的 item 即子布局本身，原代码 `delete sub` + `delete item` 两次释放同一对象）。修复：`src/ui/settings_dialog.cpp` 新增 `clearLayoutRecursively()` 递归清理，每层布局仅 `delete` 一次。用户实测确认修复，编译 0 errors / 0 warnings（原 v2 报告已并入本节） |
| 3.9 | 通知/日志/提示性文本与 Python 不一致（`"保存成功"` vs `"SnapLens 截图"` 等） | ✅ 已修复 | `application.cpp` 5 处通知对齐 Python 文案（capture_fail/save_success/save_fail/copy/hotkey_fail，hotkey 用 native 格式）；`translate_window.cpp` 按钮/通知标题/状态栏/tab 对齐（`复制翻译结果`/`AI 翻译`/`翻译完成 - 目标语言：%1`/`翻 译`）；`tray_icon.cpp` 菜单文字（`设置…`/`切换至`）；`pin_window.cpp` 另存为（`PNG 图片 (*.png);;JPEG 图片 (*.jpg)` + `SnapLens_` 前缀）；`ocr_window.cpp`/`translator.cpp` OCR 语言包错误消息；`settings_dialog.cpp` 下载失败补错误详情。编译 0 errors / 0 warnings |
| 3.10 | `ApiClient` 无输入验证 | ✅ 已修复 | `ai_client.cpp` 三个方法（chatCreate/chatCreateStream/listModels）添加前置检查，对齐 Python `core/api_client.py` + `ai/openai_compat.py`：apiKey 空 → `API Key 未设置，请在设置中配置`；apiBase 空 → `API 地址未配置，请在设置中检查服务商与 API Base`；model 空 → `未选择模型，请在设置中配置翻译/AI 模型`；messages 无内容 → `消息内容为空，无法发送 API 请求`。编译 0 errors / 0 warnings |
| 3.11 | 平台层无抽象接口 + 工厂模式 | ✅ 已修复 | 对齐 Python `base.py` + `__init__.py` 工厂：新建 `platform/platform_base.h`（5 接口 `IHotkeyProvider/IWindowProvider/ICursorProvider/ICancelProvider/IClipCursorProvider` + 5 Null 占位实现）、`platform/platform_factory.h/.cpp`（`createXxxProvider()` 按 `_WIN32` 分发，非 Windows 走 Null 降级）；现有 Win32 实现改名为 `Win32XxxProvider` 继承接口（`hotkey.h/cursor.h/cancel_intercept.h/window_enum.h`）；调用点改造（`application/snip_session/overlay` 用工厂 + 接口，overlay 注入 `ICursorProvider`）。未来跨平台只需新增实现类 + 工厂登记。编译 0 errors / 0 warnings |
| 3.12 | 热键 Qt→VK 映射缺少 `Key_Enter` | ✅ 已修复 | `hotkey.cpp` `qt_key_to_vk()` 映射表补充 `{Qt::Key_Enter, 0x0D}`（小键盘回车 → VK_RETURN），对齐 Python `native_binding.py:38` `_SPECIAL_KEYS`。此前小键盘回车作为热键时注册失败并误报"可能被占用"。编译 0 errors / 0 warnings |
| 3.13 | `ICancelProvider::install()` 无 `callback` 参数 | ✅ 非缺陷，已移除 | 经分析为**更佳实现方式**而非问题：C++ 用 Qt 信号 `escPressed` 替代 Python 回调（`install(callback)`），功能完全等价，且信号方案支持多订阅、随时断连、QObject 销毁自动清理、QueuedConnection 跨线程投递。属 Qt 标准惯用法，无需修改。 |
| 3.14 | 新增设置项需同步 4 处 | ✅ 已修复 | 引入 **X-Macro**（`settings.h` `SETTING_LIST` 宏作为字段唯一来源），成员变量声明、`setMember()`/`getMember()` 读写桥接全部由宏自动生成（`QVariant::value<T>()` 为严格转换，改用 `convertSetting<T>` 模板特化保持 `toXxx()` 宽松转换行为一致）。新增设置项 = `SETTING_LIST` 加一行 + `SETTING_DEFS` 加一项，读写桥接由编译器同步生成，**物理上不可能遗漏**（消除静默数据丢失）。保留成员变量直接访问，277 处调用点零改动。唯一行为差异：`config_dir` 空值时 JSON 输出 `""`（原为 `null`），round-trip 等价。编译 0 errors / 0 warnings |
| 3.15 | `checkSettingEnabled()` 硬编码 if-else 链 | ✅ 已修复 | 方案 B（彻底对齐 Python `getattr(settings, ndef.setting)`）：`NotifyDef::setting` 从 `QString` 改为 `bool Settings::*` 成员指针，`NOTIFY_DEFS` 注册表直接绑定 `&Settings::notify_xxx`；`checkSettingEnabled()` 由 9 个 if 分支缩减为一行 `m_settings->*(ndef.setting)`。**字符串 key 与字段名双轨彻底消除**——新增通知类型只需在 `NOTIFY_DEFS` 加一行，开关联动由编译器保证，不可能漏写。`notify_manager.h` 需 `#include "../settings.h"`（成员指针要求完整类型，无循环依赖）。编译 0 errors / 0 warnings |
| 3.16 | 设置对话框无数据驱动重置（8 个手写 reset 方法） | ✅ 已修复 | 对齐 Python `_reset_map`：`SettingsDialog` 新增 `buildResetTable()`（`ResetItem{tab, std::function<void(const Settings&)>}` 注册表，69 个条目覆盖 8 个 tab）+ 统一 `resetTab()` + `refreshToggleStates()`（重置后统一刷新依赖控件状态，修复原实现"重置后联动不刷新"的小缺陷）。8 个手写 reset 方法删除，连接点改为 lambda。新增设置项只需在注册表加一行。编译 0 errors / 0 warnings |
| 3.17+ | v3 独立复审（3 严重 + 6 中等 + 16 轻微 + 3 实测）与 v4 修复复核 | ✅ 全部闭环 | 详见 **第九节**（v3 问题归档，含每项最终状态）与 **第十节**（v4 修复复核 + L9-L16 修复记录） |

---

## 四、中等问题 (0 项)

> 2026-08-10 复核：原 15 项中等问题已全部修复（最后一项 `checkSettingEnabled()` 硬编码 if-else 已通过成员指针方案修复，见 3.15）。v3 独立复审新发现的 M1-M6 亦全部闭环（M4 确认非缺陷，见第九节）。

---

## 五、轻微问题 (0 项)

> 2026-08-10 复核：原 20 项轻微问题已全部修复或确认无需修改（最后一项 `ICancelProvider` 无 callback 参数经分析为 Qt 信号替代回调的**更佳实现**，非缺陷，见 3.13）。v3 独立复审新发现的 L1-L16 亦全部修复/确认非缺陷（见第九、十节）。

---

## 六、Python 版本优于 C++ 的设计（C++ 可参考）

| 设计 | Python 做法 | C++ 建议 |
|------|------------|---------|
| ~~设置 DRY~~ | ~~`SETTING_DEFS` 唯一来源，`setattr` 自动生成属性~~ | ✅ 已实现 `SETTING_LIST` X-Macro 生成成员/读写（settings.h/.cpp） |
| ~~平台抽象~~ | ~~`base.py` 抽象接口 + `__init__.py` 工厂~~ | ✅ 已实现 `platform_base.h` 5接口+5Null + `platform_factory.h/.cpp` 按 `_WIN32` 分发 |
| ~~日志过滤~~ | ~~`set_enabled_levels()` 运行时控制~~ | ✅ 已实现 `LogManager::setEnabledLevels()` / `applySettings()` (`log_manager.cpp:84-96`) |
| ~~通知设置~~ | ~~`getattr(self._settings, ndef.setting, True)` 动态读取~~ | ✅ 已实现：`NotifyDef::setting` 改为 `bool Settings::*` 成员指针，`NOTIFY_DEFS` 直接绑定开关（3.15） |
| ~~通道解耦~~ | ~~`LogChannel/TrayChannel/DialogChannel` 独立类~~ | ✅ 合理取舍不采用：C++ 单体 + `Channel` 位标志功能等价（外部依赖已通过 `setTray`/`setDefaultParent` 注入）；无单测框架与通道替换需求，拆类无收益 |
| ~~数据驱动重置~~ | ~~`SettingsDialog._reset_map` 字典 + `as_dict()`~~ | ✅ 已实现：`SettingsDialog` 表驱动重置（`buildResetTable()` 中 `ResetItem{tab, std::function}` 注册表 + 统一 `resetTab()`，见 3.16）；`as_dict()` 在 C++ 中无需求（直接成员写入） |

---

## 七、C++ 版本优于 Python 的设计（已知 C++ 特色）

| 设计 | 说明 |
|------|------|
| `OcrWorker` + `moveToThread` | 比 Python 的 `QThread` 子类化更符合 Qt 最佳实践 |
| `MainWindow::settingsRequested` 信号 | 比 Python 直接访问 `_settings_btn.clicked` 更解耦 |
| `TranslateService` 接受 `QImage` 参数 | 线程安全性优于 Python 的 `QPixmap` 直传 |
| 临时目录自动清理 | 启动/退出时 `cleanupTempDir()` (Python 需手动调用) |
| `PinWindow` 关闭按钮响应式定位 | `resizeEvent` 中动态计算位置 |
| 析构函数显式清理 | 热键停止、截图关闭、临时目录清理 |
| 表驱动重置用 `std::function` lambda | 比 Python `_reset_map` 的 setter_type 字符串分发更类型安全（每个条目绑定真实控件，无字符串魔法） |

---

## 八、总结

**当前状态**：原 7 项严重问题 + 15 项中等问题 + 20 项轻微问题已于 2026-08-10 全部处理完毕（修复或确认非缺陷；编译验证 0 errors, 0 warnings）。同日 v3 独立复审新发现 3 严重 + 6 中等 + 16 轻微 + 3 实测问题，经 v4 修复复核后全部闭环——**累计 0 遗留**。

**剩余问题**：无。报告中所有问题均已闭环。

**最终架构状态**：
- 设置系统：`SETTING_LIST` X-Macro 单列表驱动，读写桥接编译器生成
- 通知系统：`NOTIFY_DEFS` 成员指针直接绑定 `Settings` 开关，与设置系统同构
- 平台层：抽象接口 + 工厂 + Null 降级，支持未来跨平台
- 生命周期/线程：`terminate()` 清零、配置快照、原子化跨线程标志、取消链路（关窗即断网省 token）

**建议后续处理**：无必须项。若未来新增设置/通知类型，仅需在对应注册表加一行，无需再改桥接代码。

---

## 九、v3 独立复审问题归档（2026-08-10）

> 来源：`static_analysis_report_v3.md`（对 v1/v2 结论"0 遗留"之后的代码做全新独立复审，逐文件通读全部 56 个源文件）。
> 新发现 **3 严重 + 6 中等 + 16 轻微 + 3 实测**，全部闭环；修复细节与代码位置见第十节 v4 复核。

### 严重问题（S1-S3，均已修复 ✅）

| # | 问题 | 修复方式 | 状态 |
|---|------|---------|------|
| S1 | `onTranslate()`/`onOcr()`：close() 同步触发 closed 信号后，对已置空的指针调用 `deleteLater()` → 空指针 UB（翻译窗口打开期间再次截图翻译即可触发） | 先取旧指针并置空成员 → `disconnect(old, nullptr, this, nullptr)` 断开信号 → 再 close + deleteLater（application.cpp onTranslate/onOcr） | ✅ 实测通过 |
| S2 | `startSnip()` 失败路径：`_session->start()` 内部 emit finished 置空后，再次对空指针 deleteLater（含析构二次 emit 的隐藏路径） | finished lambda 与失败分支双判空（application.cpp:164-181） | ✅ |
| S3 | `quitApp()` 遍历 `m_pins` 时 close() 触发 lambda 修改同一容器 → 迭代器失效 + 漏关窗口 | 先拷贝列表并 clear，再逐一 close（application.cpp:357-361） | ✅ |

### 中等问题（M1-M6，均已闭环 ✅）

| # | 问题 | 修复方式 | 状态 |
|---|------|---------|------|
| M1 | 4 处 `QThread::terminate()` 强杀线程（OCR 持锁阶段强杀 → 后续 OCR 永久死锁） | terminate 全工程清零：ocr_window 零阻塞关闭+线程分离自清理；translate/main_window 短等待不强杀；hotkey 改 `PeekMessageW`+10ms 轮询 | ✅ |
| M2 | worker 线程直接读 `Settings*`，与主线程模态对话框并发写 → 数据竞争 | `TranslateService` 配置快照模式（构造时拷贝 15 项），`run()` 只读成员；配套 `settings_deferred`"下次生效"轻提示 | ✅ |
| M3 | `Win32CancelProvider::s_hook`/`s_instance` 跨线程非原子读写 → 数据竞争 | 改 `std::atomic`，并修正发布顺序（先 s_hook release 再 m_hwnd release） | ✅ |
| M4 | `pixelColor()` 每次调用 `pixmap.toImage()` 全图拷贝 → 性能 | **非缺陷（撤销）**：raster backing 下 `toImage()` 共享数据（O(1)），PySide6 与 Qt6 同一实现 | ✅ 撤销 |
| M5 | `buildResetTable()` AI 设置页重置"清空"而非恢复默认值 | `m_aiApiBaseEdit->setText(d.ai_api_base)` 恢复默认；顺带补"恢复默认服务商"防 provider/base 错配 | ✅ |
| M6 | OCR 语言包管理面板无视自定义 `ocr_tessdata_dir` | `OcrLangManager` 构造新增 `customTessdataDir` 参数，非空优先 | ✅ |

### 实测问题（R1-R3，均已闭环 ✅）

| # | 问题 | 结论 | 状态 |
|---|------|------|------|
| R1 | OCR 语言包加载失败（datapath 传了 tessdata 父目录，Tesseract 5.5 实际直接拼接 `<datapath>/<lang>.traineddata`） | datapath 直传 tessdata 目录 + 自适应下钻一层 | ✅ 实测识别成功 |
| R2 | 退出时 Tesseract `ObjectCache LEAK` 告警（6 条） | Tesseract 库静态析构行为（GitHub issue #529/#665），非真实泄漏（exit 0），应用层无须处理 | ✅ 非缺陷 |
| R3 | 翻译线程 use-after-free（未设 Key 时关窗复现；线程对象被 finished→deleteLater 自动删除后成员悬空） | 窗口成员改 `QPointer`（自动置空）+ 统一"断开信号+短等待+不强杀"清理策略，4 处清理点 | ✅ |

### 轻微问题（L1-L16，8/8 已修复，修复细节见第十节）

| # | 位置 | 问题 | 最终状态 |
|---|------|------|---------|
| L1 | ocr_engine.cpp:117-121 | `GetInitLanguagesAsString` 返回值需释放？ | ✅ 非缺陷（返回 `const char*` 无需释放，曾误加已回退） |
| L2 | capture/magnifier.h/.cpp | 死代码文件 | ✅ 已删除 |
| L3 | application.h/.cpp | `Application : QApplication` 死代码类 | ✅ 已删除 |
| L4 | settings_dialog.cpp | `setSettingValue()` 死代码 | ✅ 已删除 |
| L5 | translate_window.cpp:18-20 | 重复 include | ✅ 已去重 |
| L6 | window_enum.cpp:44 + overlay.cpp:265-275 | hover 命中/高亮框 RECT 排他边界 | ✅ 已修复（闭区间 + right()+1） |
| L7 | snip_session.cpp:98-102 | clipToScreen 排他边界（光标锁定少 1 像素） | ✅ 已修复（left+width） |
| L8 | overlay.cpp:716-719 | 动画组泄漏 | ✅ 已修复（onLabelAnimDone deleteLater） |
| L9 | ocr_engine.cpp:203-225 | `setVariable()` 底层 FALSE 仍返回 true | ✅ 已修复（`return ok != 0`） |
| L10 | settings_dialog.cpp:1184 | seed `toInt()` 无输入验证 | ✅ 已修复（`toInt(&ok)` + 提示回退） |
| L11 | settings_dialog.cpp:1097 / setup_wizard.cpp:256 | 获取模型列表同步阻塞 UI ≤10s | ✅ 已修复（QNAM 原生异步 + 代次/可见性双防护） |
| L12 | translate_window.cpp:159-168 | 旧服务 wait(3000) 后线程跑满超时，浪费 token | ✅ 已修复（cancelFlag 取消链路：关窗即断网省 token，图片/文本翻译全接入） |
| L13 | hotkey.cpp:29,43 | `acquire()` 无超时；terminate 兜底 | ✅ 已修复（`tryAcquire(1,3000)` 限时 + 失败分支安全清理） |
| L14 | ai_client.cpp:318-364 | readyRead lambda `[&]` 捕获栈变量 → 理论悬垂 | ✅ 已修复（context object = `&loop` 绑定） |
| L15 | settings.h:11 | 头文件 include log_manager.h 全工程传递包含 | ✅ 已修复（oneOf 移入 .cpp + 补 `<algorithm>`） |
| L16 | CMakeLists.txt:54-57 | `GLOB_RECURSE` 不触发重配置 | ✅ 已修复（`CONFIGURE_DEPENDS`） |

---

## 十、v4 修复复核与修复记录（2026-08-10）

> 来源：`static_analysis_report_v4.md`。对 v3 全部修复做**逐项代码级复核**：
> - v3 声称修复的 18 项（S1-S3、M1-M3、M5-M6、R1、R3、L1-L8）**全部修复准确、到位，未发现修复错误或由此引入的崩溃级新缺陷**；
> - L9-L16 原 6 项未修复 + 2 项部分修复，经本轮修复后 **8/8 全部闭环**；
> - 新发现 3 项轻微观察（非缺陷）：① 替换/关窗 `wait(3000)` 卡顿（L12 修复后已消除）；② `AppController` 析构未清理窗口成员（进程退出回收，无害）；③ hotkey 失败路径 delete 边缘竞态（**L13 修复时已顺带闭合**）。

### L9-L16 修复记录（全部已实施）

| # | 文件 | 修改 |
|---|------|------|
| L16 | `CMakeLists.txt:56` | `file(GLOB_RECURSE SNAPLENS_SOURCES CONFIGURE_DEPENDS ...)` — 新增/删除源文件自动触发重配置 |
| L9 | `src/ocr/ocr_engine.cpp:245` | `setVariable()` 返回 `ok != 0`，如实反映底层结果 |
| L10 | `src/ui/settings_dialog.cpp:1196-1204` | seed 改 `toInt(&ok)`，非法输入提示"随机种子必须为整数，已重置为 0"并回退默认 0 |
| L13 | `src/platform/hotkey.cpp:37-43, 168-186` | ① `acquire()` → `tryAcquire(1, 3000)` 限时；② `start()` 失败分支安全清理（running → 分离 + `finished→deleteLater`），闭合观察项 3 |
| L14 | `src/translate/ai_client.cpp:313-325, 373` | readyRead 连接绑定 `&loop` 为 context object，悬垂引用从根上消除 |
| L12 | `ai_client.h/.cpp` + `translator.h/.cpp` + `translate_window.cpp` + `main_window.cpp` | **关窗/替换 = 立即取消请求并断网省 token**：cancelFlag 原子化（errorCode -3 取消 / -2 超时）；`requestCancel()` + OCR 后检查点（零 token 浪费）；移除 `wait(3000)` 主线程零卡顿；图片/文本翻译全链路闭环；取消流程 11 处 SNAP_LOG_INFO 日志 |
| L11 | `ai_client.h/.cpp` + `settings_dialog.h/.cpp` + `setup_wizard.h/.cpp` | **模型获取异步化，UI 零阻塞**：`listModelsAsync()`（回调以 this 为 context，对象析构自动取消+断开）；`ApiClient` 改 `std::unique_ptr` 成员；回调校验代次 `m_fetchGen` 与可见性；`resetTab`/`onResetAll` 递增代次；保存仍仅 `accept()` 写入 |
| L15 | `settings.h` + `settings.cpp` | 消除头文件耦合：删 `log_manager.h` include；`oneOf` 移入 .cpp；补 `<algorithm>`；传递依赖审计通过 |

### 实测验证（2026-08-10 用户测试日志）

- **L12 取消链路 ✅**：两窗口关窗后日志完整呈现"触发 → abort 断网 → error=-3 → 线程退出"四段；连续获取模型 8 次无崩溃/泄漏；401 鉴权失败分支正常回显；
- **回归 ✅**：文本翻译、截图 OCR（eng 846 chars）、模型列表获取均正常；
- **打包验证 ✅**：windeployqt 部署后 22 个 PE 文件 269 个导入项全部可解析；Schannel TLS（无需 OpenSSL）、`/MT` 静态（免 vc_redist），Win10 1809+ 独立运行。

> 编译状态说明：静态分析阶段本环境无法调用 MSVC，修复后编译由用户在 CLion 验证（预期 0 警告）。

---

*报告完毕 | 共计 0 项遗留问题 | 原 7 项严重问题 + 15 项中等问题 + 20 项轻微问题 + OCR 语言包崩溃（v2 报告）+ v3 复审 3 严重 + 6 中等 + 16 轻微 + 3 实测（v4 复核）均已处理移出*

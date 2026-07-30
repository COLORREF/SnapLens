# PrScr 静态分析报告

> 分析时间：2026-07-27 | 分析范围：全部 31 个 `.py` 源文件 | 版本：0.1.0

---

## 一、Bug

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| ~~B1~~ | 🟡 P1 | `prscr/ui/overlay.py` | 903-920 | ~~颜色标签背景色配置无效~~ ✅ 已修复 (2026-07-27) | 坐标和颜色标签各自使用对应的 `_coord_label_bg_color` / `_color_label_bg_color` 及对应 alpha 值 |
| ~~B2~~ | 🟢 P2 | `prscr/ai/deepseek.py` | — | ~~冗余异常包装~~ ✅ 已修复 (2026-07-27) | deepseek.py 已简化为 OpenCompatibleTranslator 别名，不再包含异常处理逻辑 |
| ~~B3~~ | 🟢 P2 | `prscr/ui/translate_window.py` | 206-211 | ~~重新翻译时未终止旧线程~~ ✅ 已修复 (2026-07-27) | 改为同步互斥方案：`_set_loading(True)` 时禁用 `_lang_combo`，翻译中无法切换语言或点击重译，从源头杜绝并发线程。比 abort 机制更简洁，零 API 额度浪费 |
| ~~B4~~ | 🟢 P2 | `prscr/ui/overlay.py` | 672, 812 | ~~_info_label_texts() 同帧调用两次~~ ✅ 已修复 (2026-07-27) | _MgFrame 新增 coord_text/color_text 字段缓存，_compute_label_targets() 存入 frame，_render_mg_labels() 直接读取不再二次调用

---

## 二、性能问题

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| ~~P1~~ | — | `prscr/ui/overlay.py` | 936-940 | ~~每帧 toImage() 全图转换~~ ❌ 无效 (2026-07-27) | 实测三方案对比：全图 toImage() avg=0.006~0.011ms/frame，快于 1px-crop (0.016ms) 和放大镜区域 (0.027ms)。在 60fps 预算中占比 <0.1%，不构成性能瓶颈。B4（同帧二次调用）修复后已无实际影响 |
| ~~P2~~ | 🟡 P1 | `prscr/ui/settings_dialog.py` | — | ~~重置/序列化逻辑手动逐字段~~ ✅ 已修复 (2026-07-27) | 构建 `_reset_map` 映射表（key→widget+setter+tab），`as_dict()` 自动从 SETTING_DEFS 生成，`_on_reset_all()` 和 7 个 `_reset_*_tab()` 统一委托 `_reset_keys()`。从 ~258 行减至 ~140 行
| ~~P3~~ | 🟢 P2 | `prscr/ui/overlay.py` | 158-192 | ~~属性提取逐行重复 35 次~~ ✅ 已修复 (2026-07-27) | 删除所有 `if s else default` 死代码（settings 永不为 None），35 行简化为直接 `s.xxx` 访问
| P4 | 🟢 P2 | `prscr/ui/overlay.py` | 479-557 | `_animate_labels()` 复杂度高 | ❌ 不修复 (2026-07-27) — 方法处理的是本身就很复杂的状态机（2种触发器×2种元素×动画中断×首帧初始化），代码文档清晰、近期已重构修复过 3 个 bug，拆分不会降低认知复杂度 |

---

## 三、代码质量问题

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| ~~Q1~~ | 🟡 P1 | `prscr/ai/__init__.py` | 33-35 | ~~未知 provider 静默回退~~ ✅ 已修复 (2026-07-27) | 通过 PROVIDER_CONFIGS 注册表 + `_one_of` 验证器，未知 provider 现在抛出 `ValueError` |
| ~~Q2~~ | 🟢 P2 | `prscr/ui/ocr_window.py` | 全局 | ~~文本控件不一致~~ ✅ 已修复 (2026-07-27) | OCR 窗口 `QTextEdit` → `QPlainTextEdit`，与翻译窗口统一
| ~~Q3~~ | 🟢 P2 | `prscr/ui/translate_window.py` | 176-183 | ~~硬编码暗色主题样式~~ ✅ 已修复 (2026-07-27) | 移除 _make_text_panel() QSS、QFont、状态标签 setStyleSheet，全面使用原生 Qt 渲染 |
| ~~Q4~~ | 🟢 P2 | `prscr/ui/ocr_window.py` | 84-92 | ~~同上，硬编码暗色主题~~ ✅ 已修复 (2026-07-27) | 同上，移除 QSS、QFont、状态标签颜色
| ~~Q5~~ | 🟢 P2 | `prscr/core/temp_cleanup.py` | 32-34 | ~~仅清理文件不清理子目录~~ ✅ 已修复 (2026-07-27) | 新增 `shutil.rmtree` 处理子目录，文件用 `os.unlink`，覆盖所有条目类型
| ~~Q6~~ | 🟢 P2 | `prscr/app.py` | 122-123 | ~~OSError 静默吞异常~~ ✅ 已修复 (2026-07-27) | `except OSError: pass` → `except OSError as e: _log.error(...)` |
| ~~Q7~~ | 🟢 P2 | `prscr/ui/snip.py` | 62-65 | ~~grabKeyboard() 失败静默~~ ✅ 已修复 (2026-07-27) | `pass` → `_log.warning(...)` |
| ~~Q8~~ | 🟢 P2 | `prscr/ui/snip.py` | 111-113 | ~~releaseKeyboard() 失败静默~~ ✅ 已修复 (2026-07-27) | 同上 |

---

## 四、潜在隐患

| ID | 优先级 | 文件/位置 | 标题 | 详情 |
|----|--------|----------|------|------|
| ~~R1~~ | 🟡 P1 | `prscr/core/capture.py` | 26-29 | ~~混合 DPI 多屏坐标偏差~~ ✅ 已修复 (2026-07-27) | `physical_origin` 改为累加左侧所有屏的物理宽高，而非简单 `geometry.x() × 本屏 dpr` |
| R2 | 🟡 P1 | `prscr/platform/base.py` | 76-84 | `NullCursorProvider` 非整数 DPR 精度损失 | 用 `QCursor.pos()` × `DPR` 再用 `round()` 取整，在 1.5x DPR 时产生 ±1 像素误差。此仅影响 macOS/Linux（当前不适用），但若未来接入需注意 |
| R3 | 🟢 P2 | `prscr/ui/settings_dialog.py` | 91-93 | Tesseract 语言包下载无校验 | 通过 jsdelivr CDN 下载 `traineddata` 文件，无 SHA256/hash 校验，存在供应链风险 |
| R4 | 🟢 P2 | `prscr/ui/translate_window.py` + `ocr_window.py` | 全局 | 临时文件无限增长 | 仅在 `cleanup_on_window_close=True` 时清理临时图片文件。若用户关闭该选项，`temp/` 目录随每次翻译/OCR 操作增长，无限积累 |
| R5 | 🟢 P2 | `prscr/core/ocr.py` | 88-92 | OCR 语言回退静默 | `extract_text()` 在指定语言包缺失时静默回退为英文（`lang="eng"`），不通知用户部分语言未生效 |
| R6 | 🟢 P2 | `prscr/app.py` | 181-191 | 热键注册失败 UI 无持续标识 | `_apply_hotkey()` 仅在失败时发一次通知，托盘菜单文字仍显示失效的快捷键，无持续视觉提示（如红色/禁用标记） |
| R7 | 🟢 P2 | `prscr/core/settings.py` | 57-58 | 未知值静默回退首位 | `_one_of()` 验证器在值不在选项中时回退到 `choices[0]`，不记录日志。用户可能不知道某个含拼写错误的值已被替换为默认值 |
| R8 | 🟢 P2 | `prscr/ui/main_window.py` | 359 | `str.format()` 潜在 KeyError | `_build_full_prompt()` 对 `prompt_template.format(source_text=...)`，若用户手动修改提示词模板删除了 `{source_text}` 占位符以外的 `{` 字符，可能触发 KeyError 导致 UI 异常 |

---

## 五、统计总览

| 类别 | 🔴 P0 | 🟡 P1 | 🟢 P2 | 合计 |
|------|-------|-------|-------|------|
| Bug | 0 | 0 | 1 | **1** (已修复 3) |
| 性能问题 | 0 | 0 | 3 | **3** (已关闭 1) |
| 代码质量问题 | 0 | 0 | 7 | **7** (已修复 1) |
| 潜在隐患 | 0 | 2 | 6 | **8** |
| **合计** | **0** | **2** | **16** | **18** |

### 优先级定义

| 标记 | 含义 |
|------|------|
| 🔴 P0 | 严重影响用户体验或功能（性能瓶颈、必崩溃） |
| 🟡 P1 | 明显影响使用（功能 Bug、已知数据不一致、明显维护风险） |
| 🟢 P2 | 改善项（代码整洁、一致性、边缘场景、潜在风险） |

### 需立即处理的 P0 + 高优 P1 项

1. ~~P1 — `overlay.py` `toImage()` 性能~~ → 实测无效，已关闭
2. ~~P2 — 重置逻辑数据驱动重构~~ → 已修复

# SnapLens 静态分析报告

> 分析时间：2026-08-03 | 分析范围：全部 37 个 `.py` 源文件 + C++ 原生模块 | 版本：0.1.0

本报告仅包含当前代码（重构后）中仍存在的问题。已修复或不再适用的问题已移除。

---

## 一、Bug

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| B1 | 🔴 P0 | `snaplens/core/settings.py` | 313-319 | `save()` 中 `os.makedirs` 失败时 `NameError` | `path = os.path.join(...)` 在第 314 行定义，若第 313 行 `os.makedirs` 抛 OSError，第 319 行 `_log.error(..., path, e)` 引用未定义变量，二次崩溃。 |

---

## 二、性能问题

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| P1 | 🟢 P2 | `snaplens/ui/overlay.py` | 479-557 | `_animate_labels()` 复杂度高 | 不修复 — 状态机本身复杂（2 种触发器 × 2 种元素 × 动画中断），代码文档清晰，拆分不会降低认知复杂度。 |

---

## 三、代码质量问题

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| Q1 | 🟡 P1 | `snaplens/core/settings.py` | 54-60 | `_one_of()` 静默回退不记日志 | 当配置值不在允许列表时静默回退到 `choices[0]`，用户不知道该值被忽略。影响 `ai_provider`、`save_format`、`color_format`、`app_mode` 等关键项。 |
| Q2 | 🟡 P1 | `snaplens/ui/main_window.py` | 369-379 | `_build_full_prompt()` 中 `str.format()` 无容错 | 连到 `textChanged` 信号，用户每输入一个字符都会调用。若提示词模板含意外 `{` 或 `}`，直接 KeyError 崩溃。 |
| Q3 | 🟡 P1 | `snaplens/core/api_client.py` | 91 | `call_chat_stream()` 的 `cancel_flag` 参数未传递 | 签名接受 `cancel_flag`，但调用 `_native_call_chat_stream()` 时未传递。底层 `ai/native_binding.py` 自建了一个，上层参数成为死代码。 |
| Q4 | 🟢 P2 | `snaplens/core/settings.py` | 262 | `_EXTRA_FIELDS` 死代码 | `_EXTRA_FIELDS` 定义了 `{"save_dir", "temp_dir", "settings_version"}`，但整个项目从未引用。 |
| Q5 | 🟢 P2 | `snaplens/ai/deepseek.py` | 9 | `DeepSeekTranslator` 别名无人引用 | 所有翻译通过 `ai/__init__.py` 工厂创建，该模块无任何 import 方。 |
| Q6 | 🟢 P2 | `snaplens/ai/openai_compat.py` | 1-9 | 模块文档过时 | 仍声称使用 "openai SDK"，实际已切换到 C++ DLL + ctypes。 |
| Q7 | 🟢 P2 | `snaplens/core/settings.py` | 16 | 未使用的 import `field` | `from dataclasses import dataclass, field` — `field` 未被使用。 |
| Q8 | 🟢 P3 | `snaplens/ui/settings_dialog.py` | 96 | import 位置不符合 PEP 8 | `from ..core.ocr import find_tessdata_dir` 夹在常量和类定义之间，应在文件顶部。 |

---

## 四、潜在隐患

| ID | 优先级 | 文件/位置 | 标题 | 详情 |
|----|--------|----------|------|------|
| R1 | 🟡 P1 | `snaplens/platform/base.py` | 76-84 | `NullCursorProvider` 非整数 DPR 精度损失 | `QCursor.pos()` × `DPR` 再用 `round()` 取整，在 1.5x DPR 时产生 ±1 px 误差。仅影响 macOS/Linux（当前不适用）。 |
| R2 | 🟢 P2 | `snaplens/core/ocr.py` | 88-93 | OCR 语言包缺失时静默回退 eng | `extract_text()` 在指定语言包缺失时静默回退英文，无日志通知用户部分语言未生效。 |
| R3 | 🟢 P2 | `snaplens/ui/settings_dialog.py` | 299-303 | Tesseract 语言包下载无 hash 校验 | CDN (jsdelivr) 下载 `traineddata` 文件，无 SHA256 校验，存在供应链风险。 |
| R4 | 🟢 P2 | `snaplens/ui/translate_window.py` + `ocr_window.py` | 全局 | 临时文件积累风险 | 长期会话中重复翻译/OCR 会累计临时文件；程序崩溃时不会清理；删除失败静默吞异常。 |
| R5 | 🟢 P2 | `snaplens/app.py` | 221-232 | 热键注册失败无持续 UI 标识 | 仅发一次托盘通知（3 秒消失），托盘菜单仍显示失效的快捷键，无持续标记。 |
| R6 | 🟢 P2 | `snaplens/core/temp_cleanup.py` | 40-43 | OSError 静默吞异常 | 内外两层 `except OSError: pass`，清理失败无任何日志。 |
| R7 | 🟢 P2 | `snaplens/core/temp_cleanup.py` | 15-50 | 无差别删除 temp 目录所有内容 | 若用户误将 `temp_dir` 设为重要目录，会造成不可逆数据丢失。 |
| R8 | 🟢 P2 | `snaplens/ai/native_binding.py` | 多处 | `print()` 调试输出未清理 | 约 15 处 `print("[snap_ai PY] ...")` 在正式版中不应出现，应由 logging.debug 替代。 |
| R9 | 🟢 P2 | `requirements.txt` | 4 | `openai>=1.0` 依赖已无用 | 项目已全面切换至 C++ DLL（ctypes 调用），openai SDK 不再被任何代码引用。 |

---

## 五、统计总览

| 类别 | 🔴 P0 | 🟡 P1 | 🟢 P2 | P3 | 合计 |
|------|-------|-------|-------|-----|------|
| Bug | 1 | 0 | 0 | 0 | **1** |
| 性能问题 | 0 | 0 | 1 | 0 | **1** |
| 代码质量问题 | 0 | 3 | 4 | 1   | **8** |
| 潜在隐患 | 0 | 1 | 8 | 0   | **9** |
| **合计** | **1** | **4** | **13** | **1** | **19** |

### 优先级定义

| 标记 | 含义 |
|------|------|
| 🔴 P0 | 崩溃 / 数据丢失 / 核心功能不可用 |
| 🟡 P1 | 明显影响使用或存在数据不一致风险 |
| 🟢 P2 | 改善项（代码整洁、潜在风险、边缘场景） |
| P3 | 轻微（格式、文档、import 规范） |

### 建议修复顺序

1. **B1 (P0)** — settings.py save() NameError：一行修复
2. **Q1 (P1)** — `_one_of()` 加日志：一行修复
3. **Q2 (P1)** — `str.format()` 加 try/except：防止输入崩溃
4. **Q3 (P1)** — 移除或正确传递 `cancel_flag` 参数
5. **R9 (P2)** — 移除 `openai` 依赖
6. **R8 (P2)** — ai binding print → logging.debug
7. **Q4 (P2)** — 删除 `_EXTRA_FIELDS` 死代码
8. **R2-R7 (P2)** — 各潜在隐患按需修复

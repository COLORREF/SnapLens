# SnapLens 静态分析报告

> 分析时间：2026-08-04 | 版本：0.1.0

---
## 一、Bug

无。

---
## 二、性能问题

| ID | 优先级 | 文件 | 行号 | 标题 | 详情 |
|----|--------|------|------|------|------|
| P1 | 🟢 P2 | `snaplens/ui/overlay.py` | ~ | `_animate_labels()` 复杂度高 | 不修复 — 状态机本身复杂，代码文档清晰。 |

---
## 三、代码质量问题

无。

---
## 四、潜在隐患

| ID | 优先级 | 文件 | 标题 | 详情 |
|----|--------|------|------|------|
| R1 | 🟡 P1 | `snaplens/platform/base.py` | `NullCursorProvider` 非整数 DPR 精度损失 | 仅影响 macOS/Linux（当前不适用） |
| R2 | 🟢 P2 | `snaplens/ui/settings_dialog.py` | 语言包下载无 hash 校验 | CDN 下载 traineddata，无 SHA256 校验 |
| R3 | 🟢 P2 | `snaplens/ui/translate_window.py` + `ocr_window.py` | 临时文件积累风险 | 长期会话重复操作会累计临时文件 |
| R4 | 🟢 P2 | `snaplens/app.py` | 热键注册失败无持续 UI 标识 | 仅发一次托盘通知，无持续标记 |
| R5 | 🟢 P2 | `snaplens/core/temp_cleanup.py` | 无差别删除 temp 目录 | 若用户误配 temp_dir 为重要目录有风险 |

---
## 五、统计总览

| 类别 | 🔴 P0 | 🟡 P1 | 🟢 P2 | 合计 |
|------|-------|-------|-------|------|
| Bug | 0 | 0 | 0 | **0** |
| 性能 | 0 | 0 | 1 | **1** |
| 代码质量 | 0 | 0 | 0 | **0** |
| 潜在隐患 | 0 | 1 | 4 | **5** |
| **合计** | **0** | **1** | **5** | **6** |

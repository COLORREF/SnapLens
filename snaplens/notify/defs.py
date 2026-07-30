"""通知类型定义 — 所有通知场景的唯一注册来源。

设计：
- NOTIFY_DEFS 是通知类型定义的唯一数据源（类似 SETTING_DEFS）；
- 新增通知类型只需在此列表中添加一行；
- NotifyManager 运行时从此表读取通道/级别/设置关联。
"""
from dataclasses import dataclass, field
from enum import Flag, auto


class Channel(Flag):
    """通知分发通道，支持位组合（如 TRAY | LOG）。"""
    TRAY = auto()    # 系统托盘通知（QSystemTrayIcon.showMessage）
    DIALOG = auto()  # 模态弹窗（QMessageBox）
    LOG = auto()     # 控制台日志（logging 模块）


# ----------------------------------------------------------------- 级别
class NotifyLevel:
    """通知级别 → 图标 / 日志级别 的映射依据。

    不采用 Enum：避免 NotifyDef 中额外 import，保持简洁。
    """
    INFO = "info"         # 一般信息（保存成功、复制成功）
    WARNING = "warning"   # 警告（截图失败、热键失败、OCR 失败）
    ERROR = "error"       # 错误（翻译失败）

    _ALL = {INFO, WARNING, ERROR}

    @classmethod
    def validate(cls, value: str) -> bool:
        return value in cls._ALL


# ----------------------------------------------------------------- 通知类型注册
@dataclass
class NotifyDef:
    """单个通知类型的定义 — 所有配置信息的唯一来源。

    字段：
        id:       通知类型唯一标识（如 "save_success"）
        setting:  关联的设置项 key（如 "notify_save_success"）。
                  为 None 时表示无需设置项控制（始终发送）。
        channels: 默认分发通道（Channel 位组合）
        level:    通知级别（NotifyLevel.INFO / WARNING / ERROR）
    """
    id: str
    setting: str | None
    channels: Channel
    level: str


NOTIFY_DEFS: list[NotifyDef] = [
    # ---- 截图结果 ----
    NotifyDef("save_success", "notify_save_success",
              Channel.TRAY | Channel.LOG, NotifyLevel.INFO),
    NotifyDef("save_fail", "notify_save_fail",
              Channel.TRAY | Channel.LOG, NotifyLevel.WARNING),
    NotifyDef("copy", "notify_copy",
              Channel.TRAY | Channel.LOG, NotifyLevel.INFO),

    # ---- 截图异常 ----
    NotifyDef("capture_fail", "notify_capture_fail",
              Channel.TRAY | Channel.LOG, NotifyLevel.WARNING),
    NotifyDef("hotkey_fail", "notify_hotkey_fail",
              Channel.TRAY | Channel.LOG, NotifyLevel.WARNING),

    # ---- AI 翻译 ----
    NotifyDef("translate_success", "notify_translate_success",
              Channel.TRAY | Channel.LOG, NotifyLevel.INFO),
    NotifyDef("translate_fail", "notify_translate_fail",
              Channel.TRAY | Channel.LOG, NotifyLevel.ERROR),

    # ---- OCR 识别 ----
    NotifyDef("ocr_fail", "notify_ocr_fail",
              Channel.TRAY | Channel.LOG, NotifyLevel.WARNING),

    # ---- OCR 语言包下载（设置面板内操作） ----
    NotifyDef("lang_download_success", "notify_lang_download",
              Channel.TRAY, NotifyLevel.INFO),
    NotifyDef("lang_download_fail", "notify_lang_download",
              Channel.TRAY | Channel.LOG, NotifyLevel.WARNING),
]

# 快速查找表（id → NotifyDef）
_NOTIFY_DEF_MAP: dict[str, NotifyDef] = {d.id: d for d in NOTIFY_DEFS}


def get_def(notify_id: str) -> NotifyDef | None:
    """根据 id 查找通知类型定义。"""
    return _NOTIFY_DEF_MAP.get(notify_id)

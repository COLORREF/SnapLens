"""应用设置的读写（JSON 文件持久化）。

设计：
- SETTING_DEFS 是所有配置项定义的唯一来源。
- __init__ / to_dict() / update_from_dict() 均自此自动生成，
  新增配置项只需在 SETTING_DEFS 中加一行。

配置路径解析：
- 默认：<程序目录>/settings.json
- 用户可在设置中指定自定义目录，程序会在程序目录下创建 .config_location
  文件记录自定义路径；下次启动时优先读取该文件。
"""
import logging
import os
import sys
from dataclasses import dataclass, field
from typing import Any, Callable

from PySide6.QtCore import QJsonDocument, QStandardPaths

_log = logging.getLogger(__name__)

_SETTINGS_FILE = "settings.json"
_BOOTSTRAP_FILE = ".config_location"
_SETTINGS_VERSION = 1

# ----------------------------------------------------------------- 验证器工厂


def _clamp_int(lo: int, hi: int) -> Callable[[Any], int]:
    """返回限制在 [lo, hi] 的整数验证器。"""

    def _f(v):
        try:
            return max(lo, min(hi, int(v)))
        except (TypeError, ValueError):
            return lo

    return _f


def _clamp_float(lo: float, hi: float) -> Callable[[Any], float]:
    """返回限制在 [lo, hi] 的浮点数验证器。"""

    def _f(v):
        try:
            return max(lo, min(hi, float(v)))
        except (TypeError, ValueError):
            return lo

    return _f


def _one_of(*choices):
    """返回枚举验证器：不在列表中的值回退到第一个选项。"""

    def _f(v):
        return v if v in choices else choices[0]

    return _f

# ----------------------------------------------------------------- 配置项注册表


@dataclass
class SettingDef:
    """单个配置项的定义 — 所有信息的唯一来源。

    新增配置项只需在此列表中添加一行，读取/校验/序列化/同步全部自动生成。
    """
    key: str                           # JSON 键名（也是 Settings 属性名）
    default: Any                       # 默认值
    validator: Callable[[Any], Any] | None = None  # 返回规范化后的值


# ai_translation_prompt 默认文本较长，提取为常量
_DEFAULT_TRANSLATION_PROMPT = (
    "请将以下文字内容翻译为{target_lang}。\n\n"
    "注意事项：\n"
    "1. 原文来自 OCR 识别，可能存在个别字符识别错误。"
    "如果遇到明显不合理的词语或字符，请根据上下文合理推测原文意图，"
    "并在翻译时自动修正补全。\n"
    "2. 如果遇到无法理解的乱码、无意义的符号串、"
    "或与上下文完全不相关的长串字母/数字（明显是 OCR 误识别产生的噪音），"
    "请在翻译结果末尾单独注明"
    "'[注：部分内容疑似 OCR 识别错误，已跳过]'，"
    "不要强行翻译无意义内容。\n"
    "3. 只输出翻译后的文本，保持原文的段落结构和换行，"
    "不要添加任何解释、注释或额外信息（上述 OCR 错误注解除外）。\n"
    "4. 如果原文为空或没有可翻译的内容，请回复'图片中未检测到文字'。"
)

_DEFAULT_TEXT_TRANSLATION_PROMPT = (
    "你是一个{scenario}领域的专业翻译助手。\n"
    "请将以下文本翻译为{target_lang}，无论原文是什么语言。\n\n"
    "要求：\n"
    "1. 保持原文语义和专业术语的准确性\n"
    "2. 保持原文的段落结构和格式\n"
    "3. 只输出翻译结果，不要添加任何解释\n\n"
    "原文：\n"
    "{source_text}"
)

SETTING_DEFS: list[SettingDef] = [
    # ---- 截图 ----
    SettingDef("hotkey", "Ctrl+Shift+Z"),
    SettingDef("toolbar_opacity", 100, _clamp_int(1, 100)),
    SettingDef("crosshair_color", "#FFFFFF"),
    SettingDef("crosshair_alpha", 47, _clamp_int(10, 100)),
    SettingDef("crosshair_thickness", 1, _clamp_int(1, 10)),
    SettingDef("crosshair_invert", True),
    SettingDef("crosshair_enabled", True),
    # ---- 系统光标 ----
    SettingDef("cursor_enabled", False),
    # ---- 放大镜 ----
    SettingDef("magnifier_enabled", True),
    SettingDef("magnifier_zoom", 4.0, _clamp_float(4.0, 20.0)),
    SettingDef("magnifier_size", 15, _clamp_int(5, 30)),
    SettingDef("magnifier_wheel_zoom", True),
    SettingDef("magnifier_cross_color", "#DC1414"),
    SettingDef("magnifier_cross_alpha", 100, _clamp_int(30, 100)),
    SettingDef("magnifier_cross_thickness", 1, _clamp_int(1, 10)),
    SettingDef("magnifier_cross_invert", False),
    SettingDef("grid_enabled", True),
    SettingDef("grid_color", "#FFFFFF"),
    SettingDef("grid_alpha", 8, _clamp_int(1, 100)),
    SettingDef("edge_mode", "pad", _one_of("crop", "pad")),
    SettingDef("edge_color", "#000000"),
    SettingDef("zoom_label_enabled", True),
    SettingDef("zoom_label_color", "#FFFFFF"),
    SettingDef("zoom_label_alpha", 80, _clamp_int(1, 100)),
    # ---- 坐标与颜色 ----
    SettingDef("coord_label_enabled", True),
    SettingDef("coord_label_text_color", "#FFFFFF"),
    SettingDef("coord_label_bg_color", "#000000"),
    SettingDef("coord_label_bg_alpha", 100, _clamp_int(10, 100)),
    SettingDef("color_label_enabled", True),
    SettingDef("color_label_text_color", "#FFFFFF"),
    SettingDef("color_label_bg_color", "#000000"),
    SettingDef("color_label_bg_alpha", 100, _clamp_int(10, 100)),
    SettingDef("color_format", "rgb", _one_of("rgb", "hex")),
    SettingDef("copy_color_key", "C"),
    SettingDef("copy_hex_prefix", True),
    SettingDef("copy_rgb_prefix", True),
    # ---- 存储 ----
    SettingDef("save_format", "png", _one_of("png", "jpg", "bmp")),
    # config_dir 为 None 表示程序目录
    SettingDef("config_dir", None),
    # temp_dir 为 None 时使用系统临时目录
    SettingDef("temp_dir", None),
    # ---- 临时文件清理 ----
    SettingDef("cleanup_on_startup", True),
    SettingDef("cleanup_on_window_close", True),
    # close_to_tray: True=最小化到托盘, False=退出程序, None=每次询问
    SettingDef("close_to_tray", None),
    # layout_orientation: True=左右布局, False=上下布局
    SettingDef("layout_orientation", True),
    # ---- 通知 ----
    SettingDef("notify_copy", True),
    SettingDef("notify_save_success", True),
    SettingDef("notify_save_fail", True),
    SettingDef("notify_capture_fail", True),
    SettingDef("notify_hotkey_fail", True),
    SettingDef("notify_translate_success", True),
    SettingDef("notify_translate_fail", True),
    SettingDef("notify_ocr_fail", True),
    SettingDef("notify_lang_download", True),
    SettingDef("log_enabled", True),         # 全局日志开关：关闭后所有通知不写 LOG
    # ---- AI 翻译 ----
    SettingDef("ai_provider", "deepseek", _one_of(
        "deepseek", "openai", "qwen", "kimi", "glm", "hunyuan", "doubao", "qianfan",
    )),
    SettingDef("ai_api_key", ""),
    SettingDef("ai_api_base", "https://api.deepseek.com/v1"),
    SettingDef("ai_model", ""),     # 模型名称（用户手动输入或通过 API 自动获取）
    SettingDef("ai_target_lang", "简体中文"),
    SettingDef("ai_timeout", 30, _clamp_int(5, 120)),
    SettingDef("ai_temperature", 0.1, _clamp_float(0.0, 2.0)),
    SettingDef("ai_max_tokens", 4096, _clamp_int(1, 8192)),
    SettingDef("ai_top_p", 1.0, _clamp_float(0.0, 1.0)),
    SettingDef("ai_frequency_penalty", 0.0, _clamp_float(-2.0, 2.0)),
    SettingDef("ai_presence_penalty", 0.0, _clamp_float(-2.0, 2.0)),
    SettingDef("ai_seed", 0),  # 0 表示不指定（随机）
    SettingDef("ai_translation_prompt", _DEFAULT_TRANSLATION_PROMPT),
    SettingDef("ai_ocr_langs", "chi_sim+eng+jpn+kor"),
    SettingDef("ai_confirm_before_translate", False),
    SettingDef("ai_stream_thinking", True),  # 流式推送 AI 思考过程
    # ---- 文本翻译 ----
    SettingDef("text_translation_prompt", _DEFAULT_TEXT_TRANSLATION_PROMPT),
    # ---- 布局 ----
    SettingDef("left_panel_merged", True),     # 左侧 原文/提示词 是否合并
    SettingDef("right_panel_merged", False),   # 右侧 译文/AI思考 是否合并
    SettingDef("left_panel_ratio", 0.7, _clamp_float(0.1, 0.9)),    # 左侧分隔比例
    SettingDef("right_panel_ratio", 0.7, _clamp_float(0.1, 0.9)),   # 右侧分隔比例
    SettingDef("main_split_ratio", 0.5, _clamp_float(0.2, 0.8)),   # 左右区域占比
    # ---- 应用模式 ----
    # translate = 翻译模式（启动时显示文本翻译窗口，截图通过热键触发）
    # screenshot = 截图模式（后台静默运行，仅托盘；翻译通过截图工具条或托盘菜单触发）
    SettingDef("app_mode", "translate", _one_of("translate", "screenshot")),
    # ---- 元数据 ----
    SettingDef("settings_version", _SETTINGS_VERSION),
]

# 从 SETTING_DEFS 自动生成的默认值字典
_DEFAULTS: dict[str, Any] = {d.key: d.default for d in SETTING_DEFS}


def _program_dir() -> str:
    """程序所在目录（开发模式 = main.py 所在目录；打包后 = exe 所在目录）。"""
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(sys.argv[0]))


def _resolve_config_dir() -> str:
    """解析配置目录：优先读取引导文件，否则回退到程序目录。"""
    bootstrap = os.path.join(_program_dir(), _BOOTSTRAP_FILE)
    try:
        with open(bootstrap, "r", encoding="utf-8") as f:
            custom = f.read().strip()
            if os.path.isdir(custom):
                return custom
    except (OSError, ValueError):
        pass
    return _program_dir()


def _write_bootstrap(custom_dir: str) -> None:
    """将自定义配置目录写入引导文件。"""
    try:
        with open(os.path.join(_program_dir(), _BOOTSTRAP_FILE),
                  "w", encoding="utf-8") as f:
            f.write(custom_dir)
    except OSError as e:
        _log.warning("写入引导文件失败: %s", e)


def _default_save_dir() -> str:
    pics = QStandardPaths.writableLocation(
        QStandardPaths.StandardLocation.PicturesLocation
    )
    return os.path.join(pics or os.path.expanduser("~"), "SnapLens")


def _default_temp_dir() -> str:
    """默认临时目录：程序目录下的 temp 子文件夹。"""
    return os.path.join(_program_dir(), "temp")


class Settings:
    """应用设置：从 Python dict 构造，所有字段由 SETTING_DEFS 驱动。

    用法：
        settings = Settings.load()          # 从文件加载
        settings.hotkey                     # 属性式访问
        settings.update_from_dict(d)        # 批量更新（如从设置对话框回写）
        settings.save()                     # 持久化
    """

    # ---- 名单外字段（不在 SETTING_DEFS 中，需手动处理） ----
    # save_dir / temp_dir 的默认值与运行时环境相关，无法静态定义
    _EXTRA_FIELDS = {"save_dir", "temp_dir", "settings_version"}

    def __init__(self, data: dict):
        # 1) 从 SETTING_DEFS 自动生成属性
        for d in SETTING_DEFS:
            raw = data.get(d.key, d.default)
            if d.validator is not None:
                raw = d.validator(raw)
            setattr(self, d.key, raw)

        # 2) 特殊字段：动态默认值
        self.save_dir = os.path.normpath(
            data.get("save_dir") or _default_save_dir()
        )
        self.temp_dir = os.path.normpath(
            data.get("temp_dir") or _default_temp_dir()
        )

    # ---- 序列化 ----

    def to_dict(self) -> dict:
        """序列化为可用于 JSON 导出的 dict。"""
        result: dict[str, Any] = {}
        for d in SETTING_DEFS:
            result[d.key] = getattr(self, d.key)
        result["save_dir"] = self.save_dir
        result["temp_dir"] = self.temp_dir
        return result

    def update_from_dict(self, data: dict) -> None:
        """从 dict 批量更新属性（如设置对话框回写时使用）。"""
        for d in SETTING_DEFS:
            if d.key in data:
                raw = data[d.key]
                if d.validator is not None:
                    raw = d.validator(raw)
                setattr(self, d.key, raw)
        if "save_dir" in data:
            self.save_dir = os.path.normpath(data["save_dir"] or _default_save_dir())
        if "temp_dir" in data:
            self.temp_dir = os.path.normpath(data["temp_dir"] or _default_temp_dir())

    # ---- 持久化 ----

    def save(self) -> None:
        """保存设置到 config_dir（或程序目录），同步更新引导文件。

        与旧版不同：保存失败时记录错误日志，不再静默吞异常。
        """
        target_dir = self.config_dir or _program_dir()
        try:
            os.makedirs(target_dir, exist_ok=True)
            path = os.path.join(target_dir, _SETTINGS_FILE)
            doc = QJsonDocument.fromVariant(self.to_dict())
            with open(path, "wb") as f:
                f.write(doc.toJson(QJsonDocument.JsonFormat.Indented).data())
        except OSError as e:
            _log.error("保存设置失败 (%s): %s", path, e)
            return
        # 写入引导文件（自定义目录时）
        if self.config_dir:
            _write_bootstrap(self.config_dir)

    # ---- 加载 ----

    @classmethod
    def defaults_dict(cls) -> dict:
        """从 SETTING_DEFS 生成默认配置 dict。"""
        return dict(_DEFAULTS)

    @classmethod
    def config_file_exists(cls) -> bool:
        """检查配置文件是否已存在（用于判断是否首次运行）。"""
        return os.path.isfile(
            os.path.join(_resolve_config_dir(), _SETTINGS_FILE)
        )

    @classmethod
    def load(cls) -> "Settings":
        """读取设置；文件不存在时返回默认配置（不自动保存，留给引导向导处理）。"""
        path = os.path.join(_resolve_config_dir(), _SETTINGS_FILE)
        try:
            with open(path, "rb") as f:
                doc = QJsonDocument.fromJson(f.read())
            if doc.isNull():
                return cls(cls.defaults_dict())
            data = doc.toVariant()
            if not isinstance(data, dict):
                return cls(cls.defaults_dict())
        except FileNotFoundError:
            # 首次运行：返回默认配置，由引导向导负责首次保存
            return cls(cls.defaults_dict())
        except (OSError, ValueError) as e:
            _log.warning("读取设置文件失败: %s", e)
            return cls(cls.defaults_dict())
        return cls(data)

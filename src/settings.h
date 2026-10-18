#ifndef SNAPLENS_SETTINGS_H
#define SNAPLENS_SETTINGS_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QMap>
#include <functional>
#include <algorithm>  // std::clamp（clampInt/clampFloat 使用；原经 log_manager.h 传递包含）
// L15：不再 include log/log_manager.h —— settings.h 被全工程传递包含，
// 日志系统依赖由 oneOf 的实现在 settings.cpp 中承担（头文件仅保留声明）。

// ============================================================================
// 验证器工厂
// ============================================================================

namespace validators {

// 返回限制在 [lo, hi] 的整数验证器
inline std::function<QVariant(const QVariant&)> clampInt(int lo, int hi)
{
    return [lo, hi](const QVariant& v) -> QVariant {
        bool ok = false;
        int val = v.toInt(&ok);
        if (!ok) return QVariant(lo);
        return QVariant(std::clamp(val, lo, hi));
    };
}

// 返回限制在 [lo, hi] 的浮点数验证器
inline std::function<QVariant(const QVariant&)> clampFloat(double lo, double hi)
{
    return [lo, hi](const QVariant& v) -> QVariant {
        bool ok = false;
        double val = v.toDouble(&ok);
        if (!ok) return QVariant(lo);
        return QVariant(std::clamp(val, lo, hi));
    };
}

// 返回枚举验证器：不在列表中的值回退到第一个选项。
// 实现在 settings.cpp（内部使用 SNAP_LOG_WARNING 日志，避免头文件
// 依赖日志系统造成全工程传递包含耦合 —— L15）。
std::function<QVariant(const QVariant&)> oneOf(QVector<QString> choices);

} // namespace validators

// ============================================================================
// SettingDef — 单个配置项定义
// ============================================================================

struct SettingDef {
    QString key;                                              // JSON 键名 / 属性名
    QVariant defaultValue;                                    // 默认值
    std::function<QVariant(const QVariant&)> validator = {};  // 返回规范化后的值
};

// ============================================================================
// SETTING_DEFS — 所有配置项注册表
// ============================================================================

// ai_translation_prompt 默认文本
inline const char* DEFAULT_TRANSLATION_PROMPT = R"PROMPT(请将以下文字内容翻译为{target_lang}。

注意事项：
1. 原文来自 OCR 识别，可能存在个别字符识别错误。如果遇到明显不合理的词语或字符，请根据上下文合理推测原文意图，并在翻译时自动修正补全。
2. 如果遇到无法理解的乱码、无意义的符号串、或与上下文完全不相关的长串字母/数字（明显是 OCR 误识别产生的噪音），请在翻译结果末尾单独注明'[注：部分内容疑似 OCR 识别错误，已跳过]'，不要强行翻译无意义内容。
3. 只输出翻译后的文本，保持原文的段落结构和换行，不要添加任何解释、注释或额外信息（上述 OCR 错误注解除外）。
4. 如果原文为空或没有可翻译的内容，请回复'图片中未检测到文字'。)PROMPT";

inline const char* DEFAULT_TEXT_TRANSLATION_PROMPT = R"PROMPT(你是一个{scenario}领域的专业翻译助手。
请将以下文本翻译为{target_lang}，无论原文是什么语言。

要求：
1. 保持原文语义和专业术语的准确性
2. 保持原文的段落结构和格式
3. 只输出翻译结果，不要添加任何解释

原文：
{source_text})PROMPT";

// 2：新增采样开关 / 思考与推理强度 / 停止序列 / 流式用量，并放开输出上限。
inline const int SETTINGS_VERSION = 2;

inline const QVector<SettingDef> SETTING_DEFS = {
    // ---- 截图 ----
    {"hotkey",             "Ctrl+Shift+Z"},
    {"toolbar_opacity",    100,       validators::clampInt(1, 100)},
    {"crosshair_color",    "#FFFFFF"},
    {"crosshair_alpha",    47,        validators::clampInt(10, 100)},
    {"crosshair_thickness", 1,        validators::clampInt(1, 10)},
    {"crosshair_invert",   true},
    {"crosshair_enabled",  true},
    // ---- 系统光标 ----
    {"cursor_enabled",     false},
    {"cursor_keyboard_enabled", true},
    {"cursor_keyboard_mode", "arrows", validators::oneOf({"arrows", "wasd", "both"})},
    // ---- 放大镜 ----
    {"magnifier_enabled",        true},
    {"magnifier_zoom",           4.0,       validators::clampFloat(4.0, 20.0)},
    {"magnifier_fixed_zoom",     4.0,       validators::clampFloat(4.0, 20.0)},
    {"magnifier_size",           15,        validators::clampInt(5, 30)},
    {"magnifier_wheel_zoom",     true},
    {"magnifier_cross_color",    "#DC1414"},
    {"magnifier_cross_alpha",    100,       validators::clampInt(30, 100)},
    {"magnifier_cross_thickness", 1,        validators::clampInt(1, 10)},
    {"magnifier_cross_invert",   false},
    {"grid_enabled",              true},
    {"grid_color",                "#FFFFFF"},
    {"grid_alpha",                8,         validators::clampInt(1, 100)},
    {"edge_mode",                 "pad",     validators::oneOf({"crop", "pad"})},
    {"edge_color",                "#000000"},
    {"zoom_label_enabled",        true},
    {"zoom_label_text_color",     "#FFFFFF"},
    {"zoom_label_text_alpha",     100,       validators::clampInt(10, 100)},
    {"zoom_label_bg_color",       "#000000"},
    {"zoom_label_bg_alpha",       67,        validators::clampInt(10, 100)},
    // ---- 坐标与颜色 ----
    {"coord_label_enabled",    true},
    {"coord_label_text_color", "#FFFFFF"},
    {"coord_label_text_alpha", 100,       validators::clampInt(10, 100)},
    {"coord_label_bg_color",   "#000000"},
    {"coord_label_bg_alpha",   67,        validators::clampInt(10, 100)},
    {"color_label_enabled",    true},
    {"color_label_text_color", "#FFFFFF"},
    {"color_label_text_alpha", 100,       validators::clampInt(10, 100)},
    {"color_label_bg_color",   "#000000"},
    {"color_label_bg_alpha",   67,        validators::clampInt(10, 100)},
    {"color_swatch_enabled",   true},
    {"color_format_key",       "Shift"},
    {"color_format",           "rgb",    validators::oneOf({"rgb", "hex"})},
    {"copy_color_key",         "C"},
    {"copy_hex_prefix",        true},
    {"copy_rgb_prefix",        true},
    // ---- 存储 ----
    {"save_format",    "png",    validators::oneOf({"png", "jpg", "bmp"})},
    // config_dir 为 null (QJsonValue::Null) 表示程序目录
    {"config_dir",     QVariant()},
    // close_to_tray: true=最小化到托盘, false=退出程序, null=每次询问
    {"close_to_tray",  QVariant()},
    // layout_orientation: true=左右布局, false=上下布局
    {"layout_orientation", true},
    // ---- 通知 ----
    {"notify_copy",              true},
    {"notify_save_success",      true},
    {"notify_save_fail",         true},
    {"notify_capture_fail",      true},
    {"notify_hotkey_fail",       true},
    {"notify_translate_success", true},
    {"notify_translate_fail",    true},
    {"notify_ocr_fail",          true},
    {"notify_lang_download",     true},
    {"log_debug_enabled",        true},
    {"log_info_enabled",         true},
    {"log_warning_enabled",      true},
    {"log_error_enabled",        true},
    // ---- AI 翻译 ----
    {"ai_provider",  "deepseek", validators::oneOf({
        "deepseek", "openai", "qwen", "kimi", "glm", "hunyuan", "doubao", "qianfan", "custom"})},
    {"ai_api_key",          ""},
    {"ai_api_base",         "https://api.deepseek.com/v1"},
    {"ai_model",            ""},  // 模型名称（用户手动输入或通过 API 自动获取）
    {"ai_custom_models",    ""},  // 自定义服务的候选模型，逗号或换行分隔
    {"ai_target_lang",      "简体中文"},
    {"ai_timeout",          30,     validators::clampInt(5, 120)}, // 连接与发送阶段
    {"ai_response_idle_timeout", 0, validators::clampInt(0, 3600)},
    {"ai_enabled_parameters", ""},
    {"ai_extra_body", ""},
    {"ai_thinking_format", "auto"},
    {"ai_max_tokens_field", "auto"},
    {"ai_model_profiles", "{}"},
    {"ai_model_metadata", "{}"},
    {"ai_temperature",      0.1,    validators::clampFloat(0.0, 2.0)},
    {"ai_max_tokens",       4096,   validators::clampInt(1, 2147483647)},
    {"ai_top_p",            1.0,    validators::clampFloat(0.0, 1.0)},
    {"ai_frequency_penalty", 0.0,   validators::clampFloat(-2.0, 2.0)},
    {"ai_presence_penalty", 0.0,    validators::clampFloat(-2.0, 2.0)},
    {"ai_seed",             0},     // 是否发送由 ai_enabled_parameters 决定，允许显式 0
    // 采样总开关（智谱 GLM 的 do_sample）：off 时服务商忽略 temperature/top_p；auto = 不发送，用服务商默认
    {"ai_do_sample",        "auto", validators::oneOf({"auto", "on", "off"})},
    // 思考（推理）开关：auto = 不发送，on/off 显式启用或禁用，不支持时由上游报告错误
    {"ai_thinking",         "auto", validators::oneOf({"auto", "on", "off"})},
    // 推理强度：auto = 不发送；显式档位原样发送
    {"ai_reasoning_effort", "auto"},
    // 停止序列：每行一个，数量限制由模型决定
    {"ai_stop",             ""},
    // 流式响应附带 token 用量（stream_options.include_usage）；个别服务商不支持时可关闭
    {"ai_stream_usage",     false},
    {"ai_translation_prompt", QString::fromUtf8(DEFAULT_TRANSLATION_PROMPT)},
    {"ai_ocr_langs",        "chi_sim+eng+jpn+kor"},
    {"ai_confirm_before_translate", false},
    {"ai_stream_thinking",  true},  // 流式推送 AI 思考过程
    // ---- OCR 路径 ----
    // 空字符串 = 自动检测；设置后优先使用自定义路径
    {"ocr_tessdata_dir",    ""},     // tessdata 语言包目录
    // ---- 文本翻译 ----
    {"text_translation_prompt", QString::fromUtf8(DEFAULT_TEXT_TRANSLATION_PROMPT)},
    // ---- 布局 ----
    {"left_panel_merged",  true},                               // 左侧 原文/提示词 是否合并
    {"right_panel_merged", false},                              // 右侧 译文/AI思考 是否合并
    {"left_panel_ratio",   0.7,    validators::clampFloat(0.1, 0.9)},  // 左侧分隔比例
    {"right_panel_ratio",  0.7,    validators::clampFloat(0.1, 0.9)},  // 右侧分隔比例
    {"main_split_ratio",   0.5,    validators::clampFloat(0.2, 0.8)},  // 左右区域占比
    // ---- 应用模式 ----
    // translate = 翻译模式（启动时显示文本翻译窗口，截图通过热键触发）
    // screenshot = 截图模式（后台静默运行，仅托盘；翻译通过截图工具条或托盘菜单触发）
    {"app_mode", "translate", validators::oneOf({"translate", "screenshot"})},
    // ---- 外观 ----
    // system = 跟随系统深浅色；light / dark = 强制指定
    {"theme_mode", "system", validators::oneOf({"system", "light", "dark"})},
    // ---- 元数据 ----
    {"settings_version", SETTINGS_VERSION},
};

// ============================================================================
// SETTING_LIST — 设置字段的唯一来源（X-Macro）
//
// 每个设置字段在此声明一次 (type, name)，随后由宏自动生成：
//   - 成员变量声明（SETTING_FIELD，见 Settings 类）
//   - setMember() / getMember() 读写桥接（见 settings.cpp）
//
// 新增设置项时：
//   1. 在 SETTING_LIST 加一行 X(type, name)；
//   2. 在 SETTING_DEFS 加一项 {key, 默认值, 验证器}（与 name 同名）。
// 读写桥接由编译器生成，物理上不可能遗漏（消除静默数据丢失）。
// ============================================================================

#define SETTING_LIST(X) \
    X(QString, hotkey) \
    X(int, toolbar_opacity) \
    X(QString, crosshair_color) \
    X(int, crosshair_alpha) \
    X(int, crosshair_thickness) \
    X(bool, crosshair_invert) \
    X(bool, crosshair_enabled) \
    X(bool, cursor_enabled) \
    X(bool, cursor_keyboard_enabled) \
    X(QString, cursor_keyboard_mode) \
    X(bool, magnifier_enabled) \
    X(double, magnifier_zoom) \
    X(double, magnifier_fixed_zoom) \
    X(int, magnifier_size) \
    X(bool, magnifier_wheel_zoom) \
    X(QString, magnifier_cross_color) \
    X(int, magnifier_cross_alpha) \
    X(int, magnifier_cross_thickness) \
    X(bool, magnifier_cross_invert) \
    X(bool, grid_enabled) \
    X(QString, grid_color) \
    X(int, grid_alpha) \
    X(QString, edge_mode) \
    X(QString, edge_color) \
    X(bool, zoom_label_enabled) \
    X(QString, zoom_label_text_color) \
    X(int, zoom_label_text_alpha) \
    X(QString, zoom_label_bg_color) \
    X(int, zoom_label_bg_alpha) \
    X(bool, coord_label_enabled) \
    X(QString, coord_label_text_color) \
    X(int, coord_label_text_alpha) \
    X(QString, coord_label_bg_color) \
    X(int, coord_label_bg_alpha) \
    X(bool, color_label_enabled) \
    X(QString, color_label_text_color) \
    X(int, color_label_text_alpha) \
    X(QString, color_label_bg_color) \
    X(int, color_label_bg_alpha) \
    X(bool, color_swatch_enabled) \
    X(QString, color_format_key) \
    X(QString, color_format) \
    X(QString, copy_color_key) \
    X(bool, copy_hex_prefix) \
    X(bool, copy_rgb_prefix) \
    X(QString, save_format) \
    X(QString, config_dir) \
    X(QVariant, close_to_tray) \
    X(bool, layout_orientation) \
    X(bool, notify_copy) \
    X(bool, notify_save_success) \
    X(bool, notify_save_fail) \
    X(bool, notify_capture_fail) \
    X(bool, notify_hotkey_fail) \
    X(bool, notify_translate_success) \
    X(bool, notify_translate_fail) \
    X(bool, notify_ocr_fail) \
    X(bool, notify_lang_download) \
    X(bool, log_debug_enabled) \
    X(bool, log_info_enabled) \
    X(bool, log_warning_enabled) \
    X(bool, log_error_enabled) \
    X(QString, ai_provider) \
    X(QString, ai_api_key) \
    X(QString, ai_api_base) \
    X(QString, ai_model) \
    X(QString, ai_custom_models) \
    X(QString, ai_target_lang) \
    X(int, ai_response_idle_timeout) \
    X(int, ai_timeout) \
    X(QString, ai_enabled_parameters) \
    X(QString, ai_extra_body) \
    X(QString, ai_thinking_format) \
    X(QString, ai_max_tokens_field) \
    X(QString, ai_model_profiles) \
    X(QString, ai_model_metadata) \
    X(double, ai_temperature) \
    X(int, ai_max_tokens) \
    X(double, ai_top_p) \
    X(double, ai_frequency_penalty) \
    X(double, ai_presence_penalty) \
    X(int, ai_seed) \
    X(QString, ai_do_sample) \
    X(QString, ai_thinking) \
    X(QString, ai_reasoning_effort) \
    X(QString, ai_stop) \
    X(bool, ai_stream_usage) \
    X(QString, ai_translation_prompt) \
    X(QString, ai_ocr_langs) \
    X(bool, ai_confirm_before_translate) \
    X(bool, ai_stream_thinking) \
    X(QString, ocr_tessdata_dir) \
    X(QString, text_translation_prompt) \
    X(bool, left_panel_merged) \
    X(bool, right_panel_merged) \
    X(double, left_panel_ratio) \
    X(double, right_panel_ratio) \
    X(double, main_split_ratio) \
    X(QString, app_mode) \
    X(QString, theme_mode) \
    X(int, settings_version)

// ============================================================================
// Settings 类
// ============================================================================

/** @brief 表驱动的配置快照；持久化采用 QSaveFile 原子替换。
 * @details 实例不是并发容器，后台任务应在 GUI 线程构造时复制所需字段。
 */
class Settings {
public:
    // 从 dict 构造（所有字段由 SETTING_DEFS 驱动）
    explicit Settings(const QJsonObject& data);

    // 序列化
    QJsonObject toDict() const;

    // 从 dict 批量更新
    void updateFromDict(const QJsonObject& data);

    /** @brief 保存配置并更新目录指向；诊断模式禁用持久化时直接成功返回。 */
    bool save() const;
    void setPersistenceEnabled(bool enabled) { m_persistenceEnabled = enabled; }

    // 工厂方法
    static QJsonObject defaultsDict();
    static bool configFileExists();
    static Settings load();

    // ---- 成员变量（由 SETTING_LIST 宏自动生成，与 SETTING_DEFS 一一对应）----
    #define SETTING_FIELD(type, name) type name = {};
    SETTING_LIST(SETTING_FIELD)
    #undef SETTING_FIELD

    // 特殊字段：动态默认值
    QString save_dir;

private:
    bool m_persistenceEnabled = true;
};

#endif // SNAPLENS_SETTINGS_H

#include "settings.h"
#include "log/log_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

// ---------------------------------------------------------------------------
// 内部常量
// ---------------------------------------------------------------------------
static const char* SETTINGS_FILE = "settings.json";
static const char* BOOTSTRAP_FILE = ".config_location";

// ---------------------------------------------------------------------------
// 路径辅助函数
// ---------------------------------------------------------------------------

static QString programDir()
{
    return QCoreApplication::applicationDirPath();
}

static QString resolveConfigDir()
{
    QString bootstrap = QDir(programDir()).filePath(BOOTSTRAP_FILE);
    QFile f(bootstrap);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString custom = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!custom.isEmpty() && QDir(custom).exists()) {
            return custom;
        }
    }
    return programDir();
}

static void writeBootstrap(const QString& customDir)
{
    QString bootstrap = QDir(programDir()).filePath(BOOTSTRAP_FILE);
    QFile f(bootstrap);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(customDir.toUtf8());
        f.close();
    } else {
        SNAP_LOG_WARNING("写入引导文件失败: " + f.errorString());
    }
}

static QString defaultSaveDir()
{
    QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir::cleanPath(
        QDir(pics.isEmpty() ? QDir::homePath() : pics).filePath("SnapLens"));
}

// ---------------------------------------------------------------------------
// validators::oneOf 实现（L15：从头文件移入 .cpp，消除 settings.h 对日志系统
// 的传递包含依赖；日志行为保持不变）
// ---------------------------------------------------------------------------

std::function<QVariant(const QVariant&)> validators::oneOf(QVector<QString> choices)
{
    return [choices](const QVariant& v) -> QVariant {
        QString s = v.toString();
        if (choices.contains(s)) return QVariant(s);
        SNAP_LOG_WARNING(QString("配置值 '%1' 不在有效选项 %2 中，回退为 '%3'")
                             .arg(s, choices.join(','), choices.first()));
        return QVariant(choices.first());
    };
}

// ---------------------------------------------------------------------------
// 成员设置辅助：根据 key 将 QVariant 写入 Settings 对应成员
//
// setMember / getMember 由 SETTING_LIST 宏（settings.h）自动生成，
// 与 SETTING_DEFS 一一对应，新增设置项时读写桥接由编译器同步生成。
// ---------------------------------------------------------------------------

// 宽松类型转换：保持与原先 toInt()/toBool()/toDouble()/toString() 一致的行为
// （QVariant::value<T>() 为严格转换，字符串 "100" 无法转 int，故不用）
template<typename T> T convertSetting(const QVariant& v);
template<> QString convertSetting<QString>(const QVariant& v) { return v.toString(); }
template<> int     convertSetting<int>(const QVariant& v)     { return v.toInt(); }
template<> bool    convertSetting<bool>(const QVariant& v)    { return v.toBool(); }
template<> double  convertSetting<double>(const QVariant& v)  { return v.toDouble(); }
template<> QVariant convertSetting<QVariant>(const QVariant& v) { return v; }

static void setMember(Settings* s, const QString& key, const QVariant& value)
{
#define SETTING_SET(type, name) \
    if (key == QLatin1String(#name)) { s->name = convertSetting<type>(value); return; }
    SETTING_LIST(SETTING_SET)
#undef SETTING_SET
    // 未知 key：静默忽略（与原先行为一致）
}

static QVariant getMember(const Settings* s, const QString& key)
{
#define SETTING_GET(type, name) \
    if (key == QLatin1String(#name)) return s->name;
    SETTING_LIST(SETTING_GET)
#undef SETTING_GET
    return QVariant();
}

// ---------------------------------------------------------------------------
// Settings 实现
// ---------------------------------------------------------------------------

Settings::Settings(const QJsonObject& data)
{
    for (const auto& d : SETTING_DEFS) {
        QVariant raw = data.contains(d.key) ? data.value(d.key).toVariant() : d.defaultValue;
        if (d.validator) {
            raw = d.validator(raw);
        }
        setMember(this, d.key, raw);
    }

    if (data.contains("save_dir") && !data.value("save_dir").isNull()
        && !data.value("save_dir").toString().isEmpty()) {
        save_dir = QDir::cleanPath(data.value("save_dir").toString());
    } else {
        save_dir = QDir::cleanPath(defaultSaveDir());
    }
}

QJsonObject Settings::toDict() const
{
    QJsonObject result;
    for (const auto& d : SETTING_DEFS) {
        result[d.key] = QJsonValue::fromVariant(getMember(this, d.key));
    }
    result["save_dir"] = save_dir;
    return result;
}

void Settings::updateFromDict(const QJsonObject& data)
{
    for (const auto& d : SETTING_DEFS) {
        if (data.contains(d.key)) {
            QVariant raw = data.value(d.key).toVariant();
            if (d.validator) {
                raw = d.validator(raw);
            }
            setMember(this, d.key, raw);
        }
    }
    if (data.contains("save_dir")) {
        QJsonValue sv = data.value("save_dir");
        if (sv.isNull() || sv.toString().isEmpty()) {
            save_dir = QDir::cleanPath(defaultSaveDir());
        } else {
            save_dir = QDir::cleanPath(sv.toString());
        }
    }
}

void Settings::save() const
{
    QString targetDir = config_dir.isEmpty() ? programDir() : config_dir;
    QString path = QDir(targetDir).filePath(SETTINGS_FILE);

    QDir().mkpath(targetDir);

    QJsonDocument doc(toDict());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        SNAP_LOG_ERROR(QString("保存设置失败 (%1): %2").arg(path, f.errorString()));
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();

    if (!config_dir.isEmpty()) {
        writeBootstrap(config_dir);
    }
}

QJsonObject Settings::defaultsDict()
{
    QJsonObject result;
    for (const auto& d : SETTING_DEFS) {
        result[d.key] = QJsonValue::fromVariant(d.defaultValue);
    }
    return result;
}

bool Settings::configFileExists()
{
    return QFileInfo::exists(
        QDir(resolveConfigDir()).filePath(SETTINGS_FILE));
}

Settings Settings::load()
{
    QString path = QDir(resolveConfigDir()).filePath(SETTINGS_FILE);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // FileNotFound is expected on first run, other errors deserve a warning
        if (f.exists() || f.error() != QFileDevice::NoError) {
            SNAP_LOG_WARNING(QString("读取设置文件失败 (%1): %2").arg(path, f.errorString()));
        }
        return Settings(defaultsDict());
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    f.close();

    if (parseError.error != QJsonParseError::NoError || doc.isNull()) {
        SNAP_LOG_WARNING(QString("设置文件 JSON 解析失败 (%1): %2")
                             .arg(path, parseError.errorString()));
        return Settings(defaultsDict());
    }

    if (!doc.isObject()) {
        SNAP_LOG_WARNING(QString("设置文件格式错误 (%1): 不是 JSON 对象").arg(path));
        return Settings(defaultsDict());
    }

    return Settings(doc.object());
}
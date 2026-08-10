#include "ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>

#include <QMutexLocker>

#include "log/log_manager.h"

namespace {

// 目录下是否存在 *.traineddata 语言包文件
bool dirHasTraineddata(const QString& dir)
{
    return !QDir(dir).entryList({QStringLiteral("*.traineddata")}, QDir::Files).isEmpty();
}

} // namespace

OcrEngine& OcrEngine::instance()
{
    static OcrEngine inst;
    return inst;
}

OcrEngine::~OcrEngine()
{
    SNAP_LOG_DEBUG(QStringLiteral("~OcrEngine"));
    shutdown();
}

bool OcrEngine::init(const QString& tessdataDir, const QString& language)
{
    QMutexLocker locker(&m_mutex);

    if (m_initialized && m_currentLanguage == language) {
        SNAP_LOG_DEBUG(QStringLiteral("init: already initialized with same language, reusing"));
        return true;
    }

    if (m_initialized) {
        SNAP_LOG_DEBUG(QStringLiteral("init: language changed (%1 → %2), reinitializing")
                           .arg(m_currentLanguage, language));
        // Shutdown old engine (lock already held)
        if (m_api) {
            TessBaseAPIEnd(m_api);
            TessBaseAPIDelete(m_api);
            m_api = nullptr;
        }
        m_initialized = false;
        m_currentLanguage.clear();
    }

    if (tessdataDir.isEmpty()) {
        SNAP_LOG_ERROR(QStringLiteral("tessdataDir is empty"));
        return false;
    }
    if (language.isEmpty()) {
        SNAP_LOG_ERROR(QStringLiteral("language is empty"));
        return false;
    }

    SNAP_LOG_DEBUG(QStringLiteral("init: tessdataDir=%1 language=%2")
                       .arg(tessdataDir, language));

    m_api = TessBaseAPICreate();
    if (!m_api) {
        SNAP_LOG_ERROR(QStringLiteral("TessBaseAPICreate returned NULL"));
        return false;
    }

    // Tesseract 5.5 静态 SDK 实测行为：datapath 必须直接指向 tessdata 目录本身，
    // 其内部拼接 <datapath>/<lang>.traineddata 查找语言包。
    // （旧版"datapath = tessdata 父目录、自动追加 /tessdata"的约定在此 SDK 下不成立，
    //   传入父目录会查找 <parent>/<lang>.traineddata 而加载失败。）
    // 自适应：若 tessdataDir 下没有语言包但其 <tessdataDir>/tessdata 子目录有，
    // 则说明调用方传入的是父目录，自动下钻一层（兼容自定义目录填父目录的场景）。
    QString tessdataPath = tessdataDir;
    if (!dirHasTraineddata(tessdataPath)) {
        const QString subDir = QDir(tessdataPath).filePath(QStringLiteral("tessdata"));
        if (dirHasTraineddata(subDir)) {
            tessdataPath = subDir;
        }
    }
    QByteArray dataPathBytes = tessdataPath.toUtf8();
    QByteArray langBytes = language.toUtf8();

    // OEM_LSTM_ONLY: LSTM-only engine (Tesseract 5.x recommended, faster and more accurate)
    int ret = TessBaseAPIInit2(m_api, dataPathBytes.constData(), langBytes.constData(),
                               tesseract::OEM_LSTM_ONLY);
    if (ret != 0) {
        SNAP_LOG_ERROR(QStringLiteral("TessBaseAPIInit2 failed ret=%1 "
                                      "(tessdataDir=%2, language=%3, OEM_LSTM_ONLY)")
                           .arg(ret)
                           .arg(tessdataDir, language));

        // Fallback to OEM_DEFAULT for compatibility with legacy models
        SNAP_LOG_DEBUG(QStringLiteral("init: retrying with OEM_DEFAULT..."));
        TessBaseAPIDelete(m_api);
        m_api = TessBaseAPICreate();
        if (!m_api) {
            SNAP_LOG_ERROR(QStringLiteral("TessBaseAPICreate (retry) returned NULL"));
            return false;
        }
        ret = TessBaseAPIInit2(m_api, dataPathBytes.constData(), langBytes.constData(),
                               tesseract::OEM_DEFAULT);
        if (ret != 0) {
            SNAP_LOG_ERROR(QStringLiteral("TessBaseAPIInit2 (retry) failed ret=%1").arg(ret));
            TessBaseAPIDelete(m_api);
            m_api = nullptr;
            return false;
        }
        SNAP_LOG_DEBUG(QStringLiteral("init: OEM_DEFAULT fallback OK"));
    }

    // 注意：TessBaseAPIGetInitLanguagesAsString 返回 const char*（API 内部管理），
    // 无需 TessDeleteText 释放——需要释放的是返回 char* 的接口（如 GetUTF8Text，见 extractText）。
    const char* loadedLangs = TessBaseAPIGetInitLanguagesAsString(m_api);
    SNAP_LOG_DEBUG(QStringLiteral("init: OK loaded_languages=%1 version=%2")
                       .arg(QString::fromUtf8(loadedLangs ? loadedLangs : "(null)"),
                            QString::fromUtf8(TessVersion())));

    m_initialized = true;
    m_currentLanguage = language;
    return true;
}

void OcrEngine::shutdown()
{
    QMutexLocker locker(&m_mutex);
    if (m_api) {
        SNAP_LOG_DEBUG(QStringLiteral("shutdown: destroying engine"));
        TessBaseAPIEnd(m_api);
        TessBaseAPIDelete(m_api);
        m_api = nullptr;
    }
    m_initialized = false;
    m_currentLanguage.clear();
    SNAP_LOG_DEBUG(QStringLiteral("shutdown: complete"));
}

bool OcrEngine::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized && m_api != nullptr;
}

QString OcrEngine::extractText(const unsigned char* imageData, int width, int height,
                               int bytesPerPixel, int bytesPerLine)
{
    QMutexLocker locker(&m_mutex);

    if (!m_initialized || !m_api) {
        SNAP_LOG_ERROR(QStringLiteral("engine not initialized"));
        return QString();
    }
    if (!imageData) {
        SNAP_LOG_ERROR(QStringLiteral("imageData is NULL"));
        return QString();
    }
    if (width <= 0 || height <= 0) {
        SNAP_LOG_ERROR(QStringLiteral("invalid dimensions %1x%2").arg(width).arg(height));
        return QString();
    }
    if (bytesPerLine <= 0) {
        bytesPerLine = width * bytesPerPixel;
    }

    SNAP_LOG_DEBUG(QStringLiteral("extractText: image=%1x%2 bpp=%3 bpl=%4")
                       .arg(width).arg(height).arg(bytesPerPixel).arg(bytesPerLine));

    TessBaseAPISetImage(m_api, imageData, width, height,
                        bytesPerPixel, bytesPerLine);

    int ret = TessBaseAPIRecognize(m_api, nullptr);
    if (ret != 0) {
        SNAP_LOG_ERROR(QStringLiteral("TessBaseAPIRecognize failed ret=%1").arg(ret));
        TessBaseAPIClear(m_api);
        return QString();
    }

    char* utf8Text = TessBaseAPIGetUTF8Text(m_api);
    if (!utf8Text) {
        SNAP_LOG_ERROR(QStringLiteral("GetUTF8Text returned NULL"));
        TessBaseAPIClear(m_api);
        return QString();
    }

    QString result = QString::fromUtf8(utf8Text);
    SNAP_LOG_DEBUG(QStringLiteral("extractText: result len=%1 chars").arg(result.length()));

    int meanConf = TessBaseAPIMeanTextConf(m_api);
    SNAP_LOG_DEBUG(QStringLiteral("extractText: mean_confidence=%1").arg(meanConf));

    TessDeleteText(utf8Text);
    TessBaseAPIClear(m_api);
    return result;
}

QString OcrEngine::extractTextFile(const QString& imagePath)
{
    SNAP_LOG_DEBUG(QStringLiteral("extractTextFile: path=%1").arg(imagePath));

    QImage image(imagePath);
    if (image.isNull()) {
        SNAP_LOG_ERROR(QStringLiteral("extractTextFile: failed to load image: %1").arg(imagePath));
        return QString();
    }

    // Convert to RGB888 format for Tesseract
    QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
    if (rgbImage.isNull()) {
        SNAP_LOG_ERROR(QStringLiteral("extractTextFile: failed to convert image to RGB888"));
        return QString();
    }

    return extractText(rgbImage.constBits(), rgbImage.width(), rgbImage.height(),
                       3, rgbImage.bytesPerLine());
}

bool OcrEngine::setVariable(const char* name, const char* value)
{
    QMutexLocker locker(&m_mutex);
    if (!m_initialized || !m_api) {
        SNAP_LOG_ERROR(QStringLiteral("setVariable: engine not initialized"));
        return false;
    }
    if (!name || !value) {
        SNAP_LOG_ERROR(QStringLiteral("setVariable: null name or value"));
        return false;
    }

    SNAP_LOG_DEBUG(QStringLiteral("setVariable: %1 = %2")
                       .arg(QString::fromUtf8(name), QString::fromUtf8(value)));

    int ok = TessBaseAPISetVariable(m_api, name, value);
    if (!ok) {
        SNAP_LOG_WARNING(QStringLiteral("setVariable: TessBaseAPISetVariable returned FALSE "
                                        "for '%1' (variable may not exist)")
                             .arg(QString::fromUtf8(name)));
    }
    // 如实反映底层结果：返回 FALSE 时调用方应能感知失败（变量不存在/值非法），
    // 而非一律视为成功（此前恒 return true 会掩盖失败）。
    return ok != 0;
}

QString OcrEngine::version()
{
    return QString::fromUtf8(TessVersion());
}

QString OcrEngine::findTessdataDir()
{
    // Cache the result to avoid repeated filesystem scans
    static QString s_cachedDir;
    static bool s_cached = false;
    if (s_cached) return s_cachedDir;

    QString appDir = QCoreApplication::applicationDirPath();
    SNAP_LOG_DEBUG(QStringLiteral("findTessdataDir: appDir=%1").arg(appDir));

    // Search candidate parent directories
    QStringList parents;
    parents << appDir;

    // System-wide Tesseract installations
    parents << QStringLiteral("C:/Program Files/Tesseract-OCR")
            << QStringLiteral("C:/Program Files (x86)/Tesseract-OCR");

    // Sub-paths to check within each parent
    static const char* subPaths[] = {
        "sdk/tesseract/tessdata",
        "tesseract/tessdata",
        "tessdata"
    };

    for (const QString& parent : parents) {
        for (const char* sub : subPaths) {
            QString dirPath = QDir(parent).filePath(QString::fromUtf8(sub));
            QDir dir(dirPath);
            if (!dir.exists()) {
                continue;
            }
            // Check for .traineddata files
            QStringList entries = dir.entryList({QStringLiteral("*.traineddata")},
                                                QDir::Files);
            if (!entries.isEmpty()) {
                QString result = QDir::toNativeSeparators(dir.absolutePath());
                SNAP_LOG_INFO(QStringLiteral("findTessdataDir: found %1").arg(result));
                s_cachedDir = result;
                s_cached = true;
                return result;
            }
        }
    }

    SNAP_LOG_WARNING(QStringLiteral("findTessdataDir: NOT FOUND in any candidate path"));
    // Return a default fallback path so callers can still attempt init;
    // the language-download UI also needs a writable tessdata directory.
    QString fallback = QDir(appDir).filePath("tessdata");
    SNAP_LOG_INFO(QStringLiteral("findTessdataDir: using fallback %1").arg(fallback));
    s_cachedDir = fallback;
    s_cached = true;
    return fallback;
}
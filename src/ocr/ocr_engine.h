#ifndef SNAPLENS_OCR_OCR_ENGINE_H
#define SNAPLENS_OCR_OCR_ENGINE_H

#include <QString>
#include <QMutex>
#include <tesseract/capi.h>

// Tesseract's capi.h defines BOOL/TRUE/FALSE as macros for C ABI compatibility.
// This clashes with Windows SDK's "typedef int BOOL" in minwindef.h, causing:
//   error C2632: "int"后的"int"非法  (typedef int BOOL → typedef int int)
// Undefine them immediately to restore Windows SDK definitions.
#ifdef BOOL
#undef BOOL
#undef TRUE
#undef FALSE
#endif

/** @brief 拥有一个 Tesseract 实例；每个识别任务独立创建，成员操作由互斥锁保护。 */
class OcrEngine {
public:
    OcrEngine() = default;
    ~OcrEngine();
    
    /** @brief 加载语言模型；目录和语言均相同时复用已初始化的引擎。
     * @param tessdataDir 包含 .traineddata 的目录。
     * @param language 以 + 分隔的语言代码，如 chi_sim+eng。
     * @return 模型是否成功加载。
     */
    bool init(const QString& tessdataDir, const QString& language);
    
    void shutdown();
    bool isInitialized() const;
    
    /** @brief 同步识别像素数据；调用期间数据必须保持有效，失败时返回空字符串。 */
    QString extractText(const unsigned char* imageData, int width, int height,
                        int bytesPerPixel, int bytesPerLine);
    
    // Extract text from image file path. Thread-safe.
    QString extractTextFile(const QString& imagePath);
    
    // Set Tesseract variable
    bool setVariable(const char* name, const char* value);
    
    // Get Tesseract version
    static QString version();
    
    // Find tessdata directory (searches: sdk/tesseract/tessdata, tesseract/tessdata, tessdata)
    static QString findTessdataDir();
    
private:
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;
    
    TessBaseAPI* m_api = nullptr;
    bool m_initialized = false;
    QString m_currentLanguage;
    QString m_currentDirectory;
    
    mutable QMutex m_mutex;
};

#endif // SNAPLENS_OCR_OCR_ENGINE_H

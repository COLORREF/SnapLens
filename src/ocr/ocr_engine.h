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

class OcrEngine {
public:
    static OcrEngine& instance();
    
    // Initialize engine with language data
    // tessdataDir: path to tessdata directory (containing .traineddata files)
    // language: Tesseract language codes (+ separated, e.g. "chi_sim+eng")
    bool init(const QString& tessdataDir, const QString& language);
    
    void shutdown();
    bool isInitialized() const;
    
    // Extract text from raw pixel data (RGB888). Thread-safe.
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
    OcrEngine() = default;
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;
    
    TessBaseAPI* m_api = nullptr;
    bool m_initialized = false;
    QString m_currentLanguage;
    
    mutable QMutex m_mutex;
};

#endif // SNAPLENS_OCR_OCR_ENGINE_H
// ocr_engine.h - Tesseract OCR 引擎封装（内部实现，不对外导出）
//
// 封装 Tesseract 5.5 C API 的单例引擎，提供初始化、文字提取、
// 参数配置等能力。单例确保语言模型只加载一次，后续调用直接复用。
//
#ifndef SNAP_OCR_ENGINE_H
#define SNAP_OCR_ENGINE_H

#include <tesseract/capi.h>

class OcrEngine {
public:
    static OcrEngine& instance();

    // 初始化引擎（加载指定语言模型）
    // data_path: tessdata/ 的父目录
    // language:  Tesseract 语言代码（+ 分隔）
    // 返回 0 成功，<0 失败
    int init(const char* data_path, const char* language);

    // 关闭引擎，释放 Tesseract 资源
    void shutdown();

    // 引擎是否已成功初始化
    bool isInitialized() const;

    // 从内存像素数据提取文字（UTF-8）
    int extractText(const unsigned char* image_data,
                    int width, int height,
                    int bytes_per_pixel, int bytes_per_line,
                    char* text_out, int text_size);

    // 设置 Tesseract 变量
    int setVariable(const char* name, const char* value);

    // 获取内部 TessBaseAPI 句柄（供 dllmain 中直接操作）
    TessBaseAPI* api() { return api_; }

private:
    OcrEngine() = default;
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    TessBaseAPI* api_ = nullptr;
    bool initialized_ = false;
};

#endif  // SNAP_OCR_ENGINE_H

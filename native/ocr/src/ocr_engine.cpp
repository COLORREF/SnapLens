// ocr_engine.cpp - OcrEngine 单例实现
//
#include "ocr_engine.h"
#include "common.h"

#include <cstring>

#include "snaplens_ocr.h"

OcrEngine& OcrEngine::instance() {
    static OcrEngine inst;
    return inst;
}

OcrEngine::~OcrEngine() {
    SNAP_LOG_DEBUG("~OcrEngine");
    shutdown();
}

int OcrEngine::init(const char* data_path, const char* language) {
    if (initialized_) {
        SNAP_LOG_DEBUG("init: already initialized, shutting down first");
        shutdown();
    }

    if (!data_path || !data_path[0]) {
        SNAP_LOG_ERROR("data_path is empty");
        return SNAP_OCR_ERR_PARAM;
    }
    if (!language || !language[0]) {
        SNAP_LOG_ERROR("language is empty");
        return SNAP_OCR_ERR_PARAM;
    }

    SNAP_LOG_DEBUG("init: data_path=%s language=%s", data_path, language);

    api_ = TessBaseAPICreate();
    if (!api_) {
        SNAP_LOG_ERROR("TessBaseAPICreate returned NULL");
        return SNAP_OCR_ERR_NOT_INIT;
    }

    // OEM_LSTM_ONLY: 只用 LSTM 引擎（Tesseract 5.x 默认推荐，更快更准）
    int ret = TessBaseAPIInit2(api_, data_path, language, tesseract::OEM_LSTM_ONLY);
    if (ret != 0) {
        SNAP_LOG_ERROR("TessBaseAPIInit2 failed ret=%d "
                     "(data_path=%s, language=%s, OEM_LSTM_ONLY)",
                     ret, data_path, language);

        // 尝试回退 OEM_DEFAULT（兼容旧模型）
        SNAP_LOG_DEBUG("init: retrying with OEM_DEFAULT...");
        TessBaseAPIDelete(api_);
        api_ = TessBaseAPICreate();
        if (!api_) {
            SNAP_LOG_ERROR("TessBaseAPICreate (retry) returned NULL");
            return SNAP_OCR_ERR_NOT_INIT;
        }
        ret = TessBaseAPIInit2(api_, data_path, language, tesseract::OEM_DEFAULT);
        if (ret != 0) {
            SNAP_LOG_ERROR("TessBaseAPIInit2 (retry) failed ret=%d", ret);
            TessBaseAPIDelete(api_);
            api_ = nullptr;
            return ret < 0 ? ret : SNAP_OCR_ERR_NOT_INIT;
        }
        SNAP_LOG_DEBUG("init: OEM_DEFAULT fallback OK");
    }

    // 打印已加载的语言列表
    const char* loaded_langs = TessBaseAPIGetInitLanguagesAsString(api_);
    SNAP_LOG_DEBUG("init: OK loaded_languages=%s version=%s",
                 loaded_langs ? loaded_langs : "(null)", TessVersion());

    initialized_ = true;
    return SNAP_OCR_OK;
}

void OcrEngine::shutdown() {
    if (api_) {
        SNAP_LOG_DEBUG("shutdown: destroying engine");
        TessBaseAPIEnd(api_);
        TessBaseAPIDelete(api_);
        api_ = nullptr;
    }
    initialized_ = false;
    SNAP_LOG_DEBUG("shutdown: complete");
}

bool OcrEngine::isInitialized() const {
    return initialized_ && api_ != nullptr;
}

int OcrEngine::extractText(const unsigned char* image_data,
                           int width, int height,
                           int bytes_per_pixel, int bytes_per_line,
                           char* text_out, int text_size) {
    if (!isInitialized()) {
        SNAP_LOG_ERROR("engine not initialized");
        return SNAP_OCR_ERR_NOT_INIT;
    }
    if (!image_data) {
        SNAP_LOG_ERROR("image_data is NULL");
        return SNAP_OCR_ERR_IMAGE;
    }
    if (width <= 0 || height <= 0) {
        SNAP_LOG_ERROR("invalid dimensions %dx%d", width, height);
        return SNAP_OCR_ERR_IMAGE;
    }
    if (bytes_per_line <= 0) {
        bytes_per_line = width * bytes_per_pixel;
    }

    SNAP_LOG_DEBUG("extractText: image=%dx%d bpp=%d bpl=%d",
                 width, height, bytes_per_pixel, bytes_per_line);

    TessBaseAPISetImage(api_, image_data, width, height,
                        bytes_per_pixel, bytes_per_line);

    int ret = TessBaseAPIRecognize(api_, nullptr);
    if (ret != 0) {
        SNAP_LOG_ERROR("TessBaseAPIRecognize failed ret=%d", ret);
        TessBaseAPIClear(api_);
        return SNAP_OCR_ERR_RECOGNIZE;
    }

    char* utf8_text = TessBaseAPIGetUTF8Text(api_);
    if (!utf8_text) {
        SNAP_LOG_ERROR("GetUTF8Text returned NULL");
        TessBaseAPIClear(api_);
        return SNAP_OCR_ERR_RECOGNIZE;
    }

    size_t len = strlen(utf8_text);
    SNAP_LOG_DEBUG("extractText: result len=%zu chars", len);

    if (text_out && text_size > 0) {
        size_t copy_len = len < static_cast<size_t>(text_size - 1)
                          ? len : static_cast<size_t>(text_size - 1);
        memcpy(text_out, utf8_text, copy_len);
        text_out[copy_len] = '\0';
    }

    // 获取平均置信度用于日志
    int mean_conf = TessBaseAPIMeanTextConf(api_);
    SNAP_LOG_DEBUG("extractText: mean_confidence=%d", mean_conf);

    TessDeleteText(utf8_text);
    TessBaseAPIClear(api_);
    return SNAP_OCR_OK;
}

int OcrEngine::setVariable(const char* name, const char* value) {
    if (!isInitialized()) {
        SNAP_LOG_ERROR("setVariable: ERROR engine not initialized");
        return SNAP_OCR_ERR_NOT_INIT;
    }
    if (!name || !value) {
        SNAP_LOG_ERROR("setVariable: ERROR null name or value");
        return SNAP_OCR_ERR_PARAM;
    }

    SNAP_LOG_DEBUG("setVariable: %s = %s", name, value);
    BOOL ok = TessBaseAPISetVariable(api_, name, value);
    if (!ok) {
        SNAP_LOG_WARNING("setVariable: WARNING TessBaseAPISetVariable returned FALSE "
                     "for '%s' (variable may not exist)", name);
    }
    return SNAP_OCR_OK;
}

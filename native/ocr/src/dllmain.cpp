// dllmain.cpp - DLL 入口 + C ABI 函数实现
//
// 将 OcrEngine 的 C++ 接口转换为 C ABI，
// 供 Python ctypes 调用。
//
// extract_text_file 直接从 DLL 层面实现（组合 Leptonica + Tesseract），
// 避免让 Python 侧再引入 PIL/Leptonica 依赖。
//
#include <windows.h>
#include "../include/snaplens_ocr.h"
#include "ocr_engine.h"
#include "common.h"

#include <leptonica/allheaders.h>
#include <cstring>

// ============================================================================
// DLL 入口
// ============================================================================
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            SNAP_LOG_DEBUG("snaplens_ocr.dll PROCESS_ATTACH");
            break;
        case DLL_PROCESS_DETACH:
            SNAP_LOG_DEBUG("snaplens_ocr.dll PROCESS_DETACH");
            OcrEngine::instance().shutdown();
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

// ============================================================================
// 辅助函数
// ============================================================================
namespace {

// 安全拷贝字符串到输出缓冲区（含终止符）
void copy_str(const char* src, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    if (!src) {
        out[0] = '\0';
        return;
    }
    strncpy_s(out, out_size, src, _TRUNCATE);
}

}  // namespace

// ============================================================================
// C ABI 实现
// ============================================================================
extern "C" {

// ---------- 生命周期 ----------

SNAP_OCR_API int snap_ocr_init(const char* data_path, const char* language) {
    SNAP_LOG_DEBUG("snap_ocr_init: data_path=%s language=%s",
                 data_path ? data_path : "(null)",
                 language ? language : "(null)");
    return OcrEngine::instance().init(data_path, language);
}

SNAP_OCR_API void snap_ocr_shutdown(void) {
    SNAP_LOG_DEBUG("snap_ocr_shutdown");
    OcrEngine::instance().shutdown();
}

SNAP_OCR_API int snap_ocr_is_initialized(void) {
    int ok = OcrEngine::instance().isInitialized() ? 1 : 0;
    SNAP_LOG_DEBUG("snap_ocr_is_initialized: %d", ok);
    return ok;
}

// ---------- 内存像素提取 ----------

SNAP_OCR_API int snap_ocr_extract_text(
    const unsigned char* image_data,
    int width, int height,
    int bytes_per_pixel, int bytes_per_line,
    char* text_out, int text_size,
    char* error_out, int error_size) {

    if (!OcrEngine::instance().isInitialized()) {
        SNAP_LOG_ERROR("not initialized");
        copy_str("OCR engine not initialized", error_out, error_size);
        return SNAP_OCR_ERR_NOT_INIT;
    }
    if (!image_data) {
        SNAP_LOG_ERROR("null image_data");
        copy_str("image_data is NULL", error_out, error_size);
        return SNAP_OCR_ERR_IMAGE;
    }

    int ret = OcrEngine::instance().extractText(
        image_data, width, height,
        bytes_per_pixel, bytes_per_line,
        text_out, text_size);

    if (ret != SNAP_OCR_OK) {
        if (ret == SNAP_OCR_ERR_IMAGE) {
            copy_str("invalid image data", error_out, error_size);
        } else if (ret == SNAP_OCR_ERR_RECOGNIZE) {
            copy_str("Tesseract recognition failed", error_out, error_size);
        }
    }
    return ret;
}

// ---------- 文件路径提取 ----------

SNAP_OCR_API int snap_ocr_extract_text_file(
    const char* image_path,
    char* text_out, int text_size,
    char* error_out, int error_size) {

    // 参数校验
    if (!image_path || !*image_path) {
        SNAP_LOG_ERROR("empty path");
        copy_str("image_path is empty", error_out, error_size);
        return SNAP_OCR_ERR_PARAM;
    }

    if (!OcrEngine::instance().isInitialized()) {
        SNAP_LOG_ERROR("engine not initialized");
        copy_str("OCR engine not initialized, call snap_ocr_init first",
                 error_out, error_size);
        return SNAP_OCR_ERR_NOT_INIT;
    }

    SNAP_LOG_DEBUG("snap_ocr_extract_text_file: path=%s", image_path);

    // Leptonica 加载图片（支持 PNG/JPEG/BMP/TIFF 等）
    Pix* pix = pixRead(image_path);
    if (!pix) {
        SNAP_LOG_ERROR("pixRead failed for %s",
                     image_path);
        copy_str("failed to read image file (unsupported format or corrupt)",
                 error_out, error_size);
        return SNAP_OCR_ERR_FILE;
    }

    int w = pixGetWidth(pix);
    int h = pixGetHeight(pix);
    int d = pixGetDepth(pix);
    SNAP_LOG_DEBUG("snap_ocr_extract_text_file: pix=%dx%d depth=%d", w, h, d);

    // 将 Leptonica Pix 交给 Tesseract（免去像素拷贝）
    TessBaseAPI* handle = OcrEngine::instance().api();
    TessBaseAPISetImage2(handle, pix);

    // 执行识别
    int ret = TessBaseAPIRecognize(handle, nullptr);
    if (ret != 0) {
        SNAP_LOG_ERROR("Recognize failed ret=%d",
                     ret);
        copy_str("OCR recognition failed", error_out, error_size);
        pixDestroy(&pix);
        TessBaseAPIClear(handle);
        return SNAP_OCR_ERR_RECOGNIZE;
    }

    char* utf8_text = TessBaseAPIGetUTF8Text(handle);
    int mean_conf = TessBaseAPIMeanTextConf(handle);
    SNAP_LOG_DEBUG("snap_ocr_extract_text_file: mean_confidence=%d", mean_conf);

    // 清理 Pix 和 Tess 状态
    pixDestroy(&pix);
    TessBaseAPIClear(handle);

    if (!utf8_text) {
        SNAP_LOG_ERROR("GetUTF8Text returned NULL");
        copy_str("GetUTF8Text returned NULL", error_out, error_size);
        return SNAP_OCR_ERR_RECOGNIZE;
    }

    size_t len = strlen(utf8_text);
    SNAP_LOG_DEBUG("snap_ocr_extract_text_file: OK result_len=%zu chars", len);

    if (text_out && text_size > 0) {
        size_t copy_len = len < static_cast<size_t>(text_size - 1)
                          ? len : static_cast<size_t>(text_size - 1);
        memcpy(text_out, utf8_text, copy_len);
        text_out[copy_len] = '\0';
    }

    TessDeleteText(utf8_text);
    return SNAP_OCR_OK;
}

// ---------- 引擎配置 ----------

SNAP_OCR_API int snap_ocr_set_variable(const char* name, const char* value) {
    SNAP_LOG_DEBUG("snap_ocr_set_variable: %s=%s", name, value);
    return OcrEngine::instance().setVariable(name, value);
}

// ---------- 版本 ----------

SNAP_OCR_API const char* snap_ocr_get_version(void) {
    const char* ver = TessVersion();
    SNAP_LOG_DEBUG("snap_ocr_get_version: %s", ver ? ver : "(null)");
    return ver;
}

}  // extern "C"

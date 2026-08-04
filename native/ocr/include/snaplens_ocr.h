// snaplens_ocr.h - SnapLens OCR 文字识别 C ABI 公共接口
//
// 该头文件定义了 snaplens_ocr.dll 导出的所有 C 函数，
// 供 Python 通过 ctypes 调用，也可供未来 C++ 重构版本直接链接。
//
// 底层使用 Tesseract 5.5 C API (capi.h) + Leptonica 1.87，
// 支持从内存像素或文件路径提取文字。
//
// 所有函数返回值约定：
//   0 表示成功，< 0 表示失败（错误码见下方 SNAP_OCR_ERR_*）
//   指针参数：out 形式输出，调用方负责分配内存
//
#ifndef SNAPLENS_OCR_H
#define SNAPLENS_OCR_H

#ifdef SNAP_OCR_EXPORTS
    #define SNAP_OCR_API __declspec(dllexport)
#else
    #define SNAP_OCR_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 错误码
// ============================================================================
#define SNAP_OCR_OK              0
#define SNAP_OCR_ERR_NOT_INIT   -1   // 引擎未初始化
#define SNAP_OCR_ERR_IMAGE      -2   // 图像数据无效
#define SNAP_OCR_ERR_RECOGNIZE  -3   // 识别过程失败
#define SNAP_OCR_ERR_PARAM      -4   // 参数无效
#define SNAP_OCR_ERR_FILE       -5   // 文件读写失败

// ============================================================================
// 生命周期
// ============================================================================

// 初始化 OCR 引擎。
// 加载指定语言的模型到内存，后续 extract 调用复用引擎实例。
//
// data_path: tessdata 目录路径（直接包含 .traineddata 文件的目录）
//            如 "D:/.../sdk/tesseract/tessdata"
// language:  语言代码，+ 分隔多语言（如 "chi_sim+eng"）
//
// 返回：SNAP_OCR_OK 或负错误码。
//       重复调用会先关闭现有引擎再重新初始化。
//
SNAP_OCR_API int snap_ocr_init(const char* data_path, const char* language);

// 关闭 OCR 引擎，释放内存。可重复调用，幂等。
SNAP_OCR_API void snap_ocr_shutdown(void);

// 查询引擎是否初始化成功。1=已就绪，0=未初始化。
SNAP_OCR_API int snap_ocr_is_initialized(void);

// ============================================================================
// 文字提取
// ============================================================================

// 从内存中的原始像素数据提取文字。
//
// image_data:       像素字节（RGBA 或 RGB 格式，逐行连续）
// width / height:   图像尺寸（像素）
// bytes_per_pixel:  每像素字节数（3=RGB, 4=RGBA）
// bytes_per_line:   每行字节数（传入 0 则自动按 width * bpp 计算）
//
// text_out / text_size:  输出文本缓冲区及大小（UTF-8，含终止符）
// error_out / error_size:  错误信息缓冲区（失败时填充）
//
// 返回：SNAP_OCR_OK 或错误码
//
SNAP_OCR_API int snap_ocr_extract_text(
    const unsigned char* image_data,
    int width, int height,
    int bytes_per_pixel, int bytes_per_line,
    char* text_out, int text_size,
    char* error_out, int error_size);

// 从图片文件提取文字。
// 适合已有临时文件或用户指定路径的场景，内部用 Leptonica 加载图片。
//
// image_path: 图片文件路径（PNG/JPEG/BMP 等，Leptonica 支持的格式）
// text_out / text_size:  输出文本缓冲区（UTF-8）
// error_out / error_size:  错误信息缓冲区
//
// 返回：SNAP_OCR_OK 或错误码
//
SNAP_OCR_API int snap_ocr_extract_text_file(
    const char* image_path,
    char* text_out, int text_size,
    char* error_out, int error_size);

// ============================================================================
// 引擎配置
// ============================================================================

// 设置 Tesseract 引擎变量（如 "tessedit_char_whitelist" 等）。
// 必须在 snap_ocr_init 之后调用。
//
// name:  变量名
// value: 变量值
//
// 返回：SNAP_OCR_OK（设置不成功也返回 OK，内部 log warning）
//
SNAP_OCR_API int snap_ocr_set_variable(const char* name, const char* value);

// 获取 Tesseract 版本字符串，始终可用（无需初始化引擎）。
// 返回指向静态字符串的指针，不可释放。
SNAP_OCR_API const char* snap_ocr_get_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SNAPLENS_OCR_H

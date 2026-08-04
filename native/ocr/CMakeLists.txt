# snaplens_ocr — OCR 文字识别 DLL（Tesseract 5.5 + Leptonica 1.87）
#
# 依赖 SDK 中的 Tesseract 5.5.0 完整开发库（MSVC 构建），
# 无需 Qt 依赖，纯 C++ 实现。
#
# DLL 运行时依赖：
#   tesseract55.dll / leptonica-1.87.0.dll 及图像解码 DLL（tiff/png/jpeg/webp 等）
#   位于 sdk/tesseract/bin/ 目录
#

# Tesseract SDK 根路径
set(TESSERACT_SDK_DIR "${CMAKE_SOURCE_DIR}/../sdk/tesseract")

add_library(snaplens_ocr SHARED
    src/dllmain.cpp
    src/ocr_engine.cpp
)

# 头文件路径
target_include_directories(snaplens_ocr
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${TESSERACT_SDK_DIR}/include     # tesseract/ 和 leptonica/ 都在此目录下
)

# 库搜索路径
target_link_directories(snaplens_ocr PRIVATE ${TESSERACT_SDK_DIR}/lib)

# 链接 Tesseract + Leptonica
# Debug 版本自动选择 tesseract55d / leptonica-1.87.0d
target_link_libraries(snaplens_ocr PRIVATE
    snaplens_log
    $<$<CONFIG:Debug>:tesseract55d>
    $<$<NOT:$<CONFIG:Debug>>:tesseract55>
    $<$<CONFIG:Debug>:leptonica-1.87.0d>
    $<$<NOT:$<CONFIG:Debug>>:leptonica-1.87.0>
)

# 导出宏
target_compile_definitions(snaplens_ocr PRIVATE SNAP_OCR_EXPORTS)

# MSVC 编译选项
if(MSVC)
    target_compile_options(snaplens_ocr PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET snaplens_ocr PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

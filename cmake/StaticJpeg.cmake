# 仅 Qt/OCR 双静态时使用：当前两边虽然都是 libjpeg-turbo 3.2.0，
# Qt 原 JPEG 插件按 ABI 80 编译，OCR SDK 的 jpeg.lib 按 ABI 62 编译。
# 两者导出相同 jpeg_* 符号，链接成功仍可能在读图时崩溃。
# 排除 Qt 原插件，以同版本源码和 SDK 头文件重新编译插件，共用 SDK jpeg.lib。
function(snaplens_configure_static_jpeg app_target)
    if(NOT Qt6_VERSION VERSION_EQUAL "6.11.2")
        message(FATAL_ERROR "JPEG 插件源码依赖 Qt 6.11.2 私有头文件；当前 Qt ${Qt6_VERSION}。请同步更新 third_party/qtjpeg 后再升级 Qt。")
    endif()
    if(NOT EXISTS "${TESSERACT_INCLUDE_DIR}/jpeglib.h" OR NOT EXISTS "${TESSERACT_LIB_DIR}/jpeg.lib")
        message(FATAL_ERROR "双静态构建需要 OCR SDK 中的 jpeglib.h 与 jpeg.lib：${TESSERACT_SDK_DIR}")
    endif()
    qt_import_plugins(${app_target} EXCLUDE Qt6::QJpegPlugin)
    qt_add_plugin(SnapLensJpegPlugin STATIC CLASS_NAME QJpegPlugin PLUGIN_TYPE imageformats)
    target_sources(SnapLensJpegPlugin PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/qtjpeg/main.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/qtjpeg/qjpeghandler.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/qtjpeg/qjpeghandler_p.h"
        "${PROJECT_SOURCE_DIR}/third_party/qtjpeg/jpeg.json"
    )
    target_include_directories(SnapLensJpegPlugin BEFORE PRIVATE
        "${TESSERACT_INCLUDE_DIR}" "${PROJECT_SOURCE_DIR}/third_party/qtjpeg")
    target_link_libraries(SnapLensJpegPlugin PRIVATE
        Qt6::Core Qt6::Gui Qt6::CorePrivate Qt6::GuiPrivate
        "${TESSERACT_LIB_DIR}/jpeg.lib")
    set_property(TARGET SnapLensJpegPlugin PROPERTY MSVC_RUNTIME_LIBRARY
        "${SNAPLENS_MSVC_RUNTIME}$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        target_compile_options(SnapLensJpegPlugin PRIVATE /utf-8)
    endif()
    target_link_libraries(${app_target} PRIVATE SnapLensJpegPlugin)
    qt_import_plugins(${app_target} INCLUDE SnapLensJpegPlugin)
    if(MSVC)
        # 链接映射可核对 jpeg_* 最终来自哪一份静态库。
        target_link_options(${app_target} PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/SnapLens.map")
    endif()
endfunction()

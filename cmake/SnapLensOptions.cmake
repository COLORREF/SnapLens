# 构建入口的可覆盖路径和链接模式。更换 Qt 安装时请使用新的构建目录，
# 避免 CMake 缓存中的 Qt6_DIR 继续指向旧版本。
set(SNAPLENS_QT_LINKAGE "STATIC" CACHE STRING "Qt 链接方式：STATIC 或 SHARED")
set_property(CACHE SNAPLENS_QT_LINKAGE PROPERTY STRINGS STATIC SHARED)
set(SNAPLENS_OCR_LINKAGE "STATIC" CACHE STRING "Tesseract/Leptonica 链接方式：STATIC 或 SHARED")
set_property(CACHE SNAPLENS_OCR_LINKAGE PROPERTY STRINGS STATIC SHARED)

set(QT_ROOT "D:/ProgramFiles/Qt/6.11.2/msvc2026_x64_MT_static_release"
    CACHE PATH "Qt 安装根目录（包含 lib/cmake/Qt6）")
set(TESSERACT_SDK_DIR "${PROJECT_SOURCE_DIR}/sdk/tesseract"
    CACHE PATH "OCR SDK 根目录（包含 include 和 lib）")
set(ELA_WIDGETTOOLS_DIR "${PROJECT_SOURCE_DIR}/sdk/ElaWidgetTools"
    CACHE PATH "ElaWidgetTools 单文件源码目录")
if(SNAPLENS_QT_LINKAGE STREQUAL "STATIC" AND SNAPLENS_OCR_LINKAGE STREQUAL "STATIC")
    set(_default_dist "${PROJECT_SOURCE_DIR}/dist-static")
else()
    set(_default_dist "${PROJECT_SOURCE_DIR}/dist")
endif()
set(SNAPLENS_DIST_DIR "${_default_dist}"
    CACHE PATH "发布目录；动态构建建议覆盖为 dist")

# 动态 OCR SDK 的文件名随发行包变化；指定导入库的完整路径。
# 静态模式沿用本仓库 sdk/tesseract/lib 下的版本化库名。
set(SNAPLENS_TESSERACT_IMPORT_LIB "" CACHE FILEPATH "动态 Tesseract 导入库（.lib）")
set(SNAPLENS_LEPTONICA_IMPORT_LIB "" CACHE FILEPATH "动态 Leptonica 导入库（.lib）")
set(SNAPLENS_OCR_DLL_DIR "" CACHE PATH "动态 OCR 的 DLL 目录（包含其运行时依赖）")
set(SNAPLENS_TESSERACT_DLL "" CACHE FILEPATH "动态 Tesseract 运行库（.dll）")
set(SNAPLENS_LEPTONICA_DLL "" CACHE FILEPATH "动态 Leptonica 运行库（.dll）")

foreach(_mode_var SNAPLENS_QT_LINKAGE SNAPLENS_OCR_LINKAGE)
    if(NOT ${_mode_var} MATCHES "^(STATIC|SHARED)$")
        message(FATAL_ERROR "${_mode_var} 只能设为 STATIC 或 SHARED；当前值：${${_mode_var}}")
    endif()
endforeach()

if(NOT EXISTS "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "找不到 Qt 配置：${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake；请设置 QT_ROOT。")
endif()
if(NOT EXISTS "${ELA_WIDGETTOOLS_DIR}/ElaWidgetTools.hpp"
   OR NOT EXISTS "${ELA_WIDGETTOOLS_DIR}/ElaWidgetTools.cpp"
   OR NOT EXISTS "${ELA_WIDGETTOOLS_DIR}/ElaWidgetTools.qrc")
    message(FATAL_ERROR "ElaWidgetTools 源码不完整：${ELA_WIDGETTOOLS_DIR}；请设置 ELA_WIDGETTOOLS_DIR。")
endif()
if(NOT EXISTS "${TESSERACT_SDK_DIR}/include/tesseract/baseapi.h"
   OR NOT EXISTS "${TESSERACT_SDK_DIR}/include/leptonica/allheaders.h")
    message(FATAL_ERROR "OCR SDK 头文件不完整：${TESSERACT_SDK_DIR}/include；请设置 TESSERACT_SDK_DIR。")
endif()

set(TESSERACT_INCLUDE_DIR "${TESSERACT_SDK_DIR}/include")
set(TESSERACT_LIB_DIR "${TESSERACT_SDK_DIR}/lib")

# 链进 EXE 的当前静态库使用 /MT；两边都是 DLL 时使用 /MD。
# 跨 DLL 边界仍应避免由一侧分配、另一侧释放内存。
if(SNAPLENS_QT_LINKAGE STREQUAL "STATIC" OR SNAPLENS_OCR_LINKAGE STREQUAL "STATIC")
    set(SNAPLENS_MSVC_RUNTIME "MultiThreaded")
else()
    set(SNAPLENS_MSVC_RUNTIME "MultiThreadedDLL")
endif()

message(STATUS "Qt：${SNAPLENS_QT_LINKAGE}，路径：${QT_ROOT}")
message(STATUS "OCR：${SNAPLENS_OCR_LINKAGE}，SDK：${TESSERACT_SDK_DIR}")
message(STATUS "ElaWidgetTools：${ELA_WIDGETTOOLS_DIR}")
message(STATUS "发布目录：${SNAPLENS_DIST_DIR}")

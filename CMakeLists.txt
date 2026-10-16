cmake_minimum_required(VERSION 3.20)
project(SnapLens VERSION 0.3.0 DESCRIPTION "Windows 多功能工具箱" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# MSVC 14.51：避免把导入的 Qt 头文件标记为 SYSTEM。
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

# 路径与开关见 SnapLensOptions.cmake；本机默认 Qt/OCR 均为静态。
include(cmake/SnapLensOptions.cmake)
include(cmake/SnapLensQt.cmake)
snaplens_find_qt()
include(cmake/SnapLensLogging.cmake)

# 新增 src 下的 .cpp/.h 时，CONFIGURE_DEPENDS 自动更新目标源文件。
file(GLOB_RECURSE SNAPLENS_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.h")
list(FILTER SNAPLENS_SOURCES EXCLUDE REGEX "/src/log/.*")
qt_add_executable(SnapLens
    ${SNAPLENS_SOURCES}
    "${PROJECT_SOURCE_DIR}/resources/resources.qrc"
    "${ELA_WIDGETTOOLS_DIR}/ElaWidgetTools.hpp"
    "${ELA_WIDGETTOOLS_DIR}/ElaWidgetTools.cpp"
    "${ELA_WIDGETTOOLS_DIR}/ElaWidgetTools.qrc")

include(cmake/SnapLensTarget.cmake)
include(cmake/SnapLensOcr.cmake)
snaplens_configure_target(SnapLens)
target_link_libraries(SnapLens PRIVATE SnapLensLogging)
snaplens_link_qt(SnapLens)
snaplens_link_ocr(SnapLens)

# 仅双静态组合会把两份 libjpeg 链进同一 EXE；详见模块及 docs/static-build.md。
if(SNAPLENS_QT_LINKAGE STREQUAL "STATIC" AND SNAPLENS_OCR_LINKAGE STREQUAL "STATIC")
    include(cmake/StaticJpeg.cmake)
    snaplens_configure_static_jpeg(SnapLens)
endif()

include(cmake/SnapLensDeploy.cmake)
snaplens_configure_deploy(SnapLens)

cmake_minimum_required(VERSION 3.20)

# snaplens_log — 统一日志 DLL
# 基于 qDebug + 自定义 Qt 消息处理器，输出统一格式日志到 stderr。
# AI / Platform / OCR 模块通过 C ABI 调用，无需各自依赖 Qt。

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(snaplens_log SHARED
    src/dllmain.cpp
    src/log_impl.cpp
)

target_include_directories(snaplens_log
    PUBLIC include
    PRIVATE src
)

target_link_libraries(snaplens_log
    PRIVATE Qt6::Core
)

target_compile_definitions(snaplens_log PRIVATE SNAP_LOG_EXPORTS)

# 编译标准
set_target_properties(snaplens_log PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb"
)

# MSVC: 禁止 .exp
if(MSVC)
    set_target_properties(snaplens_log PROPERTIES
        LINK_FLAGS "/NOEXP"
    )
    target_compile_options(snaplens_log PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET snaplens_log PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

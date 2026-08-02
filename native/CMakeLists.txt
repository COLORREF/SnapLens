cmake_minimum_required(VERSION 3.20)

project(SnapLensNative
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "SnapLens C++ 原生模块（AI 通信 + 平台能力）"
)

# ---- 全局编译标准 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 14.51 修复：阻止 Qt/IMPORTED 目标被标记为 SYSTEM includes
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

# ---- 输出目录：统一到 native/bin/，仅保留 DLL ----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
# .exp / .lib / .pdb 对 ctypes 无用，全部丢到 bin/ 外
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pdb)

# 链接器：禁止生成 .exp 导出文件
if(MSVC)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NOEXP")
endif()

# ---- 修复 Windows SDK 库路径（SDK 安装在非系统盘时） ----
# Qt6 Network 依赖 mpr.lib，位于 SDK 的 um/x64 目录；
# CLion 的 CMake 环境缺少 VS Developer Prompt 的 LIB 变量，需手动添加。
if(MSVC AND CMAKE_RC_COMPILER)
    cmake_path(GET CMAKE_RC_COMPILER PARENT_PATH _sdk_arch_dir)
    cmake_path(GET _sdk_arch_dir PARENT_PATH _sdk_version_dir)
    cmake_path(GET _sdk_version_dir FILENAME _sdk_version)
    cmake_path(GET _sdk_version_dir PARENT_PATH _sdk_bin_dir)
    cmake_path(GET _sdk_bin_dir PARENT_PATH _sdk_root)
    link_directories("${_sdk_root}/Lib/${_sdk_version}/um/x64")
endif()

# ---- 子项目 ----
add_subdirectory(platform)
add_subdirectory(ai)

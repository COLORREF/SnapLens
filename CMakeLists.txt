# ==============================================================================
# SnapLens — 截图翻译工具 (C++ Qt6 版本)
# Tesseract 以静态库链接，不再依赖任何 DLL 运行时
# ==============================================================================
cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# 项目基本信息
# ---------------------------------------------------------------------------
project(SnapLens
    VERSION 0.3.0
    DESCRIPTION "Windows 截图翻译工具"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# C++ 标准
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 14.51 修复：阻止 Qt/IMPORTED 目标被标记为 SYSTEM includes
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

# ---------------------------------------------------------------------------
# Qt6 路径
# ---------------------------------------------------------------------------
set(QT_ROOT "D:/ProgramFiles/Qt/6.11.1/msvc2022_64")
list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")

# Qt6 组件
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# ---------------------------------------------------------------------------
# 自动启用 Qt 元对象/UI/资源编译器
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Tesseract SDK 静态库路径
# 所有库位于 sdk/tesseract/lib/ 目录
# 头文件位于 sdk/tesseract/include/ 目录
# ---------------------------------------------------------------------------
set(TESSERACT_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk/tesseract")
set(TESSERACT_INCLUDE_DIR "${TESSERACT_SDK_DIR}/include")
set(TESSERACT_LIB_DIR "${TESSERACT_SDK_DIR}/lib")

# ---------------------------------------------------------------------------
# 收集源文件
# CONFIGURE_DEPENDS：新增/删除源文件时自动触发 CMake 重新配置，
# 避免"新增 .cpp/.h 后不重新 configure 导致编译不到"的常见问题。
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SNAPLENS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# 资源文件
set(SNAPLENS_RESOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/resources.qrc"
)

# ---------------------------------------------------------------------------
# 生成可执行文件
# ---------------------------------------------------------------------------
qt_add_executable(SnapLens
    ${SNAPLENS_SOURCES}
    ${SNAPLENS_RESOURCES}
)

# ---------------------------------------------------------------------------
# 包含目录
# ---------------------------------------------------------------------------
target_include_directories(SnapLens PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${TESSERACT_INCLUDE_DIR}"               # Tesseract + Leptonica 头文件
    "${TESSERACT_INCLUDE_DIR}/tesseract"      # Tesseract 内部头文件（baseapi.h 等）
    "${TESSERACT_INCLUDE_DIR}/leptonica"      # Leptonica 头文件（allheaders.h 等）
)

# ---------------------------------------------------------------------------
# Tesseract 静态库搜索路径
# ---------------------------------------------------------------------------
target_link_directories(SnapLens PRIVATE "${TESSERACT_LIB_DIR}")

# ---------------------------------------------------------------------------
# 链接库
# ---------------------------------------------------------------------------
target_link_libraries(SnapLens PRIVATE
    # Qt6
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    # Tesseract 静态库（SDK 仅提供 Release 构建，Debug 也使用同一套库）
    tesseract55
    leptonica-1.87.0
    # Tesseract 依赖的第三方静态库
    libpng16
    jpeg
    tiff
    libwebp
    libwebpdecoder
    libwebpdemux
    libwebpmux
    libsharpyuv
    gif
    openjp2
    zs
    zstd
    lz4
    lzma
    bz2
    libcurl
    libssl
    libcrypto
    archive
    turbojpeg
)

# ---------------------------------------------------------------------------
# Win32 系统库
# ---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(SnapLens PRIVATE
        user32      # RegisterHotKey / SendInput / GetCursorPos / ClipCursor
        kernel32    # GetModuleHandle / GetProcAddress
        dwmapi      # DwmGetWindowAttribute
        gdi32       # GDI 图形操作
        ws2_32      # Winsock (libcurl 需要)
        crypt32     # CryptoAPI (libcurl / libssl 需要)
        wldap32     # LDAP (libcurl 需要)
        normaliz    # Unicode 规范化 (libcurl 需要)
        advapi32    # 注册表 (libssl 需要)
        secur32     # SSPI (libcurl 需要)
        shell32     # Shell API
        ole32       # COM
        oleaut32    # COM Automation
        shlwapi     # PathFileExists / SHCreateMemStream (Tesseract 静态链接需要)
        bcrypt      # BCryptGenRandom (libcurl 需要)
        iphlpapi    # if_nametoindex / GetAdaptersAddresses (libcurl 需要)
        xmllite     # CreateXmlReader (archive/libxml2 需要)
    )
endif()

# ---------------------------------------------------------------------------
# MSVC 编译选项
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(SnapLens PRIVATE /utf-8 /MP /W4 /permissive- /wd4996)
    target_compile_definitions(SnapLens PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    # 静态链接 MSVC 运行时（/MT），避免依赖 vcruntime 等 DLL
    set_property(TARGET SnapLens PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# ---------------------------------------------------------------------------
# 构建后复制 tessdata 语言包 + 打包到 dist 目录
# ---------------------------------------------------------------------------
if(WIN32)
    set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
    set(TESSDATA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/sdk/tesseract/tessdata")

    # 统一 POST_BUILD 步骤：先处理输出目录，再处理 dist 目录
    set(_post_build_cmds)

    # ── 输出目录 · tessdata（存在才复制）──
    if(EXISTS "${TESSDATA_SRC}")
        list(APPEND _post_build_cmds
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:SnapLens>/sdk/tesseract/tessdata"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TESSDATA_SRC}"
                "$<TARGET_FILE_DIR:SnapLens>/sdk/tesseract/tessdata"
        )
    endif()

    # ── dist 目录 · 仅更新 exe（不删除已有文件）──
    list(APPEND _post_build_cmds
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/sdk/tesseract/tessdata"

        # ── dist 目录 · 复制 exe（覆盖旧版本）──
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SnapLens>"
            "${DIST_DIR}/$<TARGET_FILE_NAME:SnapLens>"
    )

    # ── dist 目录 · tessdata（仅当源存在时同步新增/更新文件，不删除已有文件）──
    if(EXISTS "${TESSDATA_SRC}")
        list(APPEND _post_build_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TESSDATA_SRC}"
                "${DIST_DIR}/sdk/tesseract/tessdata"
        )
    endif()

    add_custom_command(TARGET SnapLens POST_BUILD
        ${_post_build_cmds}
        COMMENT "打包 SnapLens → dist 目录"
    )
endif()

# ==============================================================================
# 发布说明
#
# 编译后自动打包到 dist/ 目录：
#   dist/
#   ├── SnapLens.exe
#   └── sdk/tesseract/tessdata/
#       ├── chi_sim.traineddata
#       ├── eng.traineddata
#       └── ...
#
# Qt 运行时需通过系统 PATH 提供（已配置环境变量则无需复制）。
# 如需独立部署，使用 windeployqt：
#   windeployqt --dir dist dist/SnapLens.exe
#
# 完整独立发布目录结构：
#   SnapLens.exe (静态链接 Tesseract，无需额外 DLL)
#   Qt6Core.dll / Qt6Gui.dll / Qt6Widgets.dll / Qt6Network.dll
#   platforms/qwindows.dll
#   styles/qwindowsvistastyle.dll
#   sdk/tesseract/tessdata/ (语言包)
# ==============================================================================
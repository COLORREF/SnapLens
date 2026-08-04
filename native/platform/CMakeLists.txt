# snaplens_platform — Win32 平台能力 DLL（热键、窗口枚举、Esc 拦截、光标限制）

add_library(snaplens_platform SHARED
    src/dllmain.cpp
    src/hotkey.cpp
    src/window_enum.cpp
    src/cancel.cpp
)

target_include_directories(snaplens_platform
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(snaplens_platform PRIVATE SNAP_PLATFORM_EXPORTS)

# 系统库
if(WIN32)
    target_link_libraries(snaplens_platform PRIVATE
        snaplens_log
        user32      # RegisterHotKey, EnumWindows, GetCursorPos, ClipCursor
        kernel32    # GetCurrentProcessId
        dwmapi      # DwmGetWindowAttribute
    )
endif()

# MSVC 选项
if(MSVC)
    target_compile_options(snaplens_platform PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET snaplens_platform PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# 应用目标的共通编译设置。此模块不选择静态/动态库。
function(snaplens_configure_target target)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    # 消费方不重复显示单文件 Ela 头的第三方警告；ElaWidgetTools.cpp 仍正常编译。
    target_include_directories(${target} SYSTEM PRIVATE "${ELA_WIDGETTOOLS_DIR}")

    if(WIN32)
        target_link_libraries(${target} PRIVATE
            d3d11 dxgi user32 kernel32 dwmapi gdi32
            ws2_32 crypt32 wldap32 normaliz advapi32 secur32
            shell32 ole32 oleaut32 shlwapi bcrypt iphlpapi xmllite)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /MP /W4 /permissive- /wd4996)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        # 至少一侧为静态库时 /MT；两侧均共享时 /MD。
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY
            "${SNAPLENS_MSVC_RUNTIME}$<$<CONFIG:Debug>:Debug>")
    endif()
endfunction()

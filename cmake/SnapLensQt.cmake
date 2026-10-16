# 只从 QT_ROOT 指定的安装查找 Qt，防止机器上其他版本混入。
# 必须用 macro 保持 find_package 的目录作用域，否则静态插件导入目标不可见。
macro(snaplens_find_qt)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network
        PATHS "${QT_ROOT}" NO_DEFAULT_PATH)
    get_target_property(_actual_type Qt6::Core TYPE)
    if(SNAPLENS_QT_LINKAGE STREQUAL "STATIC" AND NOT _actual_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "QT_ROOT 指向共享 Qt，但 SNAPLENS_QT_LINKAGE=STATIC：${QT_ROOT}")
    endif()
    if(SNAPLENS_QT_LINKAGE STREQUAL "SHARED" AND _actual_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "QT_ROOT 指向静态 Qt，但 SNAPLENS_QT_LINKAGE=SHARED：${QT_ROOT}")
    endif()
    message(STATUS "已找到 Qt ${Qt6_VERSION}（${_actual_type}）")
endmacro()

function(snaplens_link_qt target)
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
endfunction()

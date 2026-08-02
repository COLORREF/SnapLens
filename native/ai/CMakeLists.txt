# snaplens_ai — OpenAI 兼容 API 通信 DLL（Qt Network 后端）

# Qt MOC 自动处理 Q_OBJECT（须在 add_library 之前）
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(snaplens_ai SHARED
    src/dllmain.cpp
    src/api_client.cpp
)

target_include_directories(snaplens_ai
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(snaplens_ai PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(snaplens_ai PRIVATE SNAP_AI_EXPORTS)

# MSVC 选项
if(MSVC)
    target_compile_options(snaplens_ai PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET snaplens_ai PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

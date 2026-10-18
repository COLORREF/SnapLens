# Reusable transport/capability library. This target has no Widgets, OCR,
# application Settings, or SnapLens logging dependency.
find_package(Qt6 6.3 REQUIRED COMPONENTS Core Network)
add_library(SnapLensAi STATIC
    ai_client.cpp ai_client.h
    model_capabilities.cpp model_capabilities.h
    reply_waiter.cpp reply_waiter.h
    providers.h)
add_library(SnapLens::Ai ALIAS SnapLensAi)
set_target_properties(SnapLensAi PROPERTIES AUTOMOC ON)
target_compile_features(SnapLensAi PUBLIC cxx_std_17)
target_include_directories(SnapLensAi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SnapLensAi PUBLIC Qt6::Core Qt6::Network)
if(MSVC)
    target_compile_options(SnapLensAi PUBLIC /utf-8)
    target_compile_options(SnapLensAi PRIVATE /W4 /permissive-)
    if(DEFINED SNAPLENS_MSVC_RUNTIME)
        set_property(TARGET SnapLensAi PROPERTY MSVC_RUNTIME_LIBRARY
            "${SNAPLENS_MSVC_RUNTIME}$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

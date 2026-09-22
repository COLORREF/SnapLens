# 当前预编译 SDK 的静态依赖清单。动态模式只链接 Tesseract/Leptonica
# 导入库，其 JPEG/PNG 等运行时依赖由发布模块从 DLL 目录解析复制。
function(snaplens_link_ocr target)
    target_include_directories(${target} PRIVATE
        "${TESSERACT_INCLUDE_DIR}"
        "${TESSERACT_INCLUDE_DIR}/tesseract"
        "${TESSERACT_INCLUDE_DIR}/leptonica")

    if(SNAPLENS_OCR_LINKAGE STREQUAL "STATIC")
        set(_ocr_libs
            tesseract55 leptonica-1.87.0 libpng16 jpeg tiff
            libwebp libwebpdecoder libwebpdemux libwebpmux libsharpyuv
            gif openjp2 zs zstd lz4 lzma bz2 libcurl libssl
            libcrypto archive turbojpeg)
        foreach(_lib IN LISTS _ocr_libs)
            if(NOT EXISTS "${TESSERACT_LIB_DIR}/${_lib}.lib")
                message(FATAL_ERROR "缺少静态 OCR 库：${TESSERACT_LIB_DIR}/${_lib}.lib；请检查 TESSERACT_SDK_DIR。")
            endif()
        endforeach()
        target_link_directories(${target} PRIVATE "${TESSERACT_LIB_DIR}")
        target_link_libraries(${target} PRIVATE ${_ocr_libs})
    else()
        foreach(_lib_path SNAPLENS_TESSERACT_IMPORT_LIB SNAPLENS_LEPTONICA_IMPORT_LIB)
            if(NOT EXISTS "${${_lib_path}}")
                message(FATAL_ERROR "动态 OCR 模式缺少导入库：${_lib_path}=${${_lib_path}}；请指定完整 .lib 路径。")
            endif()
        endforeach()
        if(NOT IS_DIRECTORY "${SNAPLENS_OCR_DLL_DIR}")
            message(FATAL_ERROR "动态 OCR 模式需要 SNAPLENS_OCR_DLL_DIR 指向 DLL 目录：${SNAPLENS_OCR_DLL_DIR}")
        endif()
        foreach(_dll_path SNAPLENS_TESSERACT_DLL SNAPLENS_LEPTONICA_DLL)
            if(NOT EXISTS "${${_dll_path}}")
                message(FATAL_ERROR "动态 OCR 模式缺少运行库：${_dll_path}=${${_dll_path}}；请指定完整 .dll 路径。")
            endif()
        endforeach()
        target_link_libraries(${target} PRIVATE
            "${SNAPLENS_TESSERACT_IMPORT_LIB}"
            "${SNAPLENS_LEPTONICA_IMPORT_LIB}")
    endif()
endfunction()

# 静态构建只复制 EXE 和可选语言包；动态构建另外部署 DLL。
function(snaplens_configure_deploy target)
    if(NOT WIN32)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SNAPLENS_DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${target}>"
            "${SNAPLENS_DIST_DIR}/$<TARGET_FILE_NAME:${target}>"
        COMMENT "复制 SnapLens 到 ${SNAPLENS_DIST_DIR}")

    # tessdata 是运行时模型数据，不是 DLL；源目录存在时才复制。
    set(_tessdata "${TESSERACT_SDK_DIR}/tessdata")
    if(EXISTS "${_tessdata}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/sdk/tesseract/tessdata"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_tessdata}" "$<TARGET_FILE_DIR:${target}>/sdk/tesseract/tessdata"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${SNAPLENS_DIST_DIR}/sdk/tesseract/tessdata"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_tessdata}" "${SNAPLENS_DIST_DIR}/sdk/tesseract/tessdata")
    endif()

    if(SNAPLENS_QT_LINKAGE STREQUAL "SHARED")
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
        find_program(_windeployqt windeployqt HINTS "${_qt_bin}" NO_DEFAULT_PATH)
        if(NOT _windeployqt)
            message(FATAL_ERROR "共享 Qt 发布需要 windeployqt；请检查 QT_ROOT/bin：${_qt_bin}")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${_windeployqt}" --release --no-translations
                --no-opengl-sw --no-system-d3d-compiler --compiler-runtime
                --dir "${SNAPLENS_DIST_DIR}" "$<TARGET_FILE:${target}>"
            COMMENT "部署共享 Qt 到 ${SNAPLENS_DIST_DIR}")
    endif()

    if(SNAPLENS_OCR_LINKAGE STREQUAL "SHARED")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DOCR_TESSERACT_DLL=${SNAPLENS_TESSERACT_DLL}"
                "-DOCR_LEPTONICA_DLL=${SNAPLENS_LEPTONICA_DLL}"
                "-DOCR_DLL_DIR=${SNAPLENS_OCR_DLL_DIR}"
                "-DOCR_DIST_DIR=${SNAPLENS_DIST_DIR}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DeployOcrDlls.cmake"
            COMMENT "部署动态 OCR 及其依赖到 ${SNAPLENS_DIST_DIR}")
    endif()
endfunction()

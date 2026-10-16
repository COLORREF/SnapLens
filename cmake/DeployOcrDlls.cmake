# 构建后脚本：从用户指定的两份 OCR DLL 出发，递归收集非系统 DLL。
# 仅在 SNAPLENS_OCR_LINKAGE=SHARED 时调用。
file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${OCR_TESSERACT_DLL}" "${OCR_LEPTONICA_DLL}"
    DIRECTORIES "${OCR_DLL_DIR}"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _missing)
if(_missing)
    message(FATAL_ERROR "动态 OCR 依赖未找到：${_missing}；请补齐 OCR_DLL_DIR=${OCR_DLL_DIR}。")
endif()

set(_copied "${OCR_TESSERACT_DLL};${OCR_LEPTONICA_DLL};${_resolved}")
list(REMOVE_DUPLICATES _copied)
foreach(_dll IN LISTS _copied)
    file(TO_CMAKE_PATH "${_dll}" _normalized)
    if(_normalized MATCHES "^[A-Za-z]:/[Ww]indows/")
        continue()
    endif()
    file(COPY "${_dll}" DESTINATION "${OCR_DIST_DIR}")
endforeach()
message(STATUS "动态 OCR 运行库已复制到：${OCR_DIST_DIR}")

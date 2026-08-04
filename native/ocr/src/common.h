// common.h - OCR 模块内部公共包含
//
// 统一日志通过 snaplens_log.dll C ABI（#include <snaplens_log.h>）。
// 使用 SNAP_LOG_DEBUG/INFO/WARNING/ERROR 宏，自动附加文件名和行号。
//
#ifndef SNAP_OCR_COMMON_H
#define SNAP_OCR_COMMON_H

// 统一日志（通过 snaplens_log.dll C ABI）
#include <snaplens_log.h>

#include <cstdio>
#include <cstring>

#endif  // SNAP_OCR_COMMON_H

#!/usr/bin/env bash
# 编译 Qt 资源文件（.qrc → _rc.py）
# 使用 PySide6 自带的 pyside6-rcc 工具
# 依赖：pip install PySide6
# 资源修改后运行此脚本重新生成 assets_rc.py

set -euo pipefail

SRC="snaplens/assets/assets.qrc"
OUT="snaplens/assets/assets_rc.py"

echo "编译 ${SRC} → ${OUT}"

if command -v pyside6-rcc &> /dev/null; then
    pyside6-rcc "${SRC}" -o "${OUT}"
elif python -c "from PySide6 import pyside6_rcc" 2>/dev/null; then
    python -m PySide6.pyside6-rcc "${SRC}" -o "${OUT}"
else
    echo "错误：找不到 pyside6-rcc，请确认 PySide6 已正确安装。"
    exit 1
fi

echo "✓ 编译成功"

@echo off
REM 编译 Qt 资源文件（.qrc → _rc.py）
REM 使用 PySide6 自带的 pyside6-rcc 工具
REM 依赖：pip install PySide6
REM 资源修改后运行此脚本重新生成 assets_rc.py

set SRC=snaplens\assets\assets.qrc
set OUT=snaplens\assets\assets_rc.py

echo 编译 %SRC% → %OUT%
pyside6-rcc %SRC% -o %OUT%

if %errorlevel% equ 0 (
    echo ✓ 编译成功
) else (
    echo ✗ 编译失败，请确认 PySide6 已安装且 pyside6-rcc 在 PATH 中
    echo   可尝试: python -m PySide6.pyside6-rcc %SRC% -o %OUT%
    exit /b 1
)

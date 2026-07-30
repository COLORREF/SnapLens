"""SnapLens 入口：截图+OCR+AI翻译，常驻系统托盘。

运行方式：
    pip install -r requirements.txt
    python main.py
"""
import sys

from PySide6.QtWidgets import QApplication

from snaplens.app import AppController


def main() -> int:
    QApplication.setApplicationName("SnapLens")
    QApplication.setOrganizationName("SnapLens")

    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

    controller = AppController(app)  # 保持引用，防止 GC 回收
    if controller.cancelled:
        return 0  # 首次运行向导被取消，不进入事件循环
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

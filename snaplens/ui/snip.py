"""截图会话：协调多屏覆盖层，并把选区结果分发给具体动作。

一次截图流程：
1. start() 先抓取整个虚拟桌面并枚举顶层窗口；
2. 每个屏幕创建一个 SnipOverlay；
3. 用户在某个覆盖层完成选区 -> 点击工具条按钮；
4. 会话裁剪图像、关闭所有覆盖层，再执行 保存/钉图/复制 回调。
"""
import logging

from PySide6.QtCore import QObject, QRect, Signal

from ..core.capture import CapturedDesktop
from ..platform import create_cancel_provider, create_clip_cursor_provider, \
    create_window_provider

_log = logging.getLogger(__name__)
from .overlay import SnipOverlay


class SnipSession(QObject):
    """一次完整的截图交互。回调签名均为 func(QPixmap) -> None。"""

    finished = Signal()  # 会话结束（无论成功与否），供外部释放引用

    def __init__(self, on_save, on_pin, on_copy,
                 on_translate=None,
                 on_ocr=None,
                 settings=None,
                 parent=None):
        super().__init__(parent)
        self._on_save = on_save
        self._on_pin = on_pin
        self._on_copy = on_copy
        self._on_translate = on_translate
        self._on_ocr = on_ocr
        self._settings = settings
        self._desktop = None
        self._overlays: list[SnipOverlay] = []
        self._active_overlay = None
        self._active_rect = QRect()
        self._closed = False
        self._cancel_provider = None   # 平台原生 Esc 拦截
        self._clip_provider = None     # 平台光标限制

    def start(self) -> bool:
        self._desktop = CapturedDesktop()
        if not self._desktop.shots:
            self.finished.emit()
            return False
        windows = create_window_provider().enum_windows()
        for shot in self._desktop.shots:
            overlay = SnipOverlay(shot, windows, settings=self._settings)
            overlay.selectionStarted.connect(self._on_selection_started)
            overlay.regionSelected.connect(self._on_region_selected)
            overlay.cancelled.connect(self.close)
            overlay.actionChosen.connect(self._on_action)
            self._overlays.append(overlay)
        for overlay in self._overlays:
            overlay.show()
        # Esc 通过平台原生事件拦截兜底（不依赖焦点 / 键盘抓取）
        self._cancel_provider = create_cancel_provider()
        self._cancel_provider.install(self.close)
        # 光标限制：在用户开始选区时锁定，防止漂移到其他屏幕
        self._clip_provider = create_clip_cursor_provider()
        # 激活窗口并抓取键盘，使 Enter / Ctrl+S 等快捷键可用
        self._overlays[0].raise_()
        self._overlays[0].activateWindow()
        self._overlays[0].setFocus()
        try:
            self._overlays[0].grabKeyboard()
        except Exception as e:
            _log.warning("grabKeyboard() 失败: %s", e)
        return True

    # ------------------------------------------------------------ 覆盖层信号
    def _on_selection_started(self, overlay):
        """某屏开始新选区时，清掉其它屏幕已有的选区，并锁定光标在当前屏幕。"""
        # 锁定光标在当前屏幕范围内（物理像素）
        if self._clip_provider is not None:
            origin = overlay.shot.physical_origin
            pm = overlay.shot.pixmap
            left = int(origin.x())
            top = int(origin.y())
            right = left + pm.width() - 1
            bottom = top + pm.height() - 1
            self._clip_provider.clip_to_rect(left, top, right, bottom)
        for other in self._overlays:
            if other is not overlay:
                other.clear_selection()

    def _on_region_selected(self, overlay, rect):
        for other in self._overlays:
            if other is not overlay:
                other.clear_selection()
                other.hide()  # 隐藏未选中的 overlay，避免鼠标在上方时继续绘制十字线
        self._active_overlay = overlay
        self._active_rect = rect

    def _on_action(self, action: str):
        if action == "cancel" or self._active_overlay is None:
            self.close()
            return
        # 先裁剪，再关闭覆盖层，最后执行动作（钉图/保存窗口不被覆盖层遮挡）
        pixmap = self._desktop.crop(self._active_overlay.shot, self._active_rect)
        self.close()
        if action == "save":
            self._on_save(pixmap)
        elif action == "pin":
            self._on_pin(pixmap)
        elif action == "copy":
            self._on_copy(pixmap)
        elif action == "translate" and self._on_translate is not None:
            self._on_translate(pixmap)
        elif action == "ocr" and self._on_ocr is not None:
            self._on_ocr(pixmap)

    # ------------------------------------------------------------ 收尾
    def close(self):
        if self._closed:
            return
        self._closed = True
        # 先卸载 Esc 拦截，避免关闭覆盖层期间重复触发
        if self._cancel_provider is not None:
            self._cancel_provider.uninstall()
            self._cancel_provider = None
        # 释放光标限制
        if self._clip_provider is not None:
            self._clip_provider.release()
            self._clip_provider = None
        for overlay in self._overlays:
            try:
                overlay.releaseKeyboard()
            except Exception as e:
                _log.warning("releaseKeyboard() 失败: %s", e)
            overlay.close()
        self._overlays.clear()
        self.finished.emit()

"""截图选区覆盖层：每个屏幕一个全屏无边框窗口。

交互（对齐 QQ 截图的常用习惯）：
- 左键拖动：框选任意区域；
- 单击：选中鼠标下的窗口（无窗口处单击 = 整屏）；
- 选���确定后弹出工具条：保存 / 钉图 / 确认复制 / 取消；
- 双击或回车：复制并退出；Esc：取消。
"""
from dataclasses import dataclass

from PySide6.QtCore import (QEasingCurve, QParallelAnimationGroup, QPoint,
                              QPointF, QRect, QRectF, QSize, QSizeF, Qt,
                              QVariantAnimation, Signal)
from PySide6.QtGui import QColor, QGuiApplication, QIcon, \
    QKeySequence, QPainter, QPen
from PySide6.QtWidgets import (QGraphicsOpacityEffect, QHBoxLayout,
                               QToolButton, QWidget)

from ..assets import assets_rc  # noqa: F401  (编译后的 Qt 资源)

from ..core.capture import ScreenShot
from ..platform import create_cursor_provider

SELECTION_COLOR = QColor(0, 120, 215)          # 选区边框蓝
MASK_COLOR = QColor(0, 0, 0, 110)              # 遮罩暗化
TOOLBAR_BG = QColor(45, 45, 45)                # 工具条背景色

_ICON_SIZE = 20  # 图标像素尺寸


# ---------------------------------------------------------------- 放大镜帧参数
@dataclass
class _MgFrame:
    """单帧放大镜绘制所需的所有计算参数。

    由 _prepare_mg_frame() 填充，渲染方法从中读取，避免散落局部变量。
    """
    # 缩放
    zoom: float = 4.0
    half: int = 15               # 源半边长（物理像素）
    # 显示区尺寸（逻辑像素）
    disp_w: float = 0.0
    disp_h: float = 0.0
    # 放大镜放置位置（逻辑像素，目标值）
    target_x: float = 0.0
    target_y: float = 0.0
    # 源区域（物理像素）
    src_left: int = 0
    src_top: int = 0
    src_w: int = 0
    src_h: int = 0
    # 光标物理像素（相对本屏）
    phys_cx: int = 0
    phys_cy: int = 0
    # pad 模式字段
    is_pad: bool = False
    full_left: int = 0
    full_top: int = 0
    full_w: int = 0
    full_h: int = 0
    # 标签
    zoom_label_h: float = 0.0    # 倍率标签高度
    has_labels: bool = False
    zoom_label_enabled: bool = True
    coord_text: str = ""         # 缓存 _info_label_texts() 结果，避免同帧二次调用
    color_text: str = ""


class SelectionToolbar(QWidget):
    """选区确定后的操作工具条（纯图标 + 可调不透明度）。
    
    布局：保存 | 钉图 | --弹性空间-- | 确认复制 ✓ | 取消 ✕
    """

    actionChosen = Signal(str)  # "save" / "pin" / "copy" / "cancel"

    def __init__(self, opacity: float = 1.0, parent=None):
        super().__init__(parent)
        self.setAutoFillBackground(True)

        palette = self.palette()
        palette.setColor(self.backgroundRole(), TOOLBAR_BG)
        self.setPalette(palette)

        if opacity < 1.0:
            effect = QGraphicsOpacityEffect(self)
            effect.setOpacity(opacity)
            self.setGraphicsEffect(effect)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(2)

        # 左侧操作按钮
        left_buttons = (
            (":/Save.svg", "save", "保存到文件"),
            (":/Pushpin.svg", "pin", "钉图置顶"),
            (":/Translate.svg", "translate", "AI 翻译图片文字"),
            (":/Ocr.svg", "ocr", "OCR 识别文字"),
        )
        for path, action, tip in left_buttons:
            button = QToolButton(self)
            button.setIcon(QIcon(path))
            button.setIconSize(QSize(_ICON_SIZE, _ICON_SIZE))
            button.setToolTip(tip)
            button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            button.setCursor(Qt.CursorShape.ArrowCursor)
            button.setAutoRaise(True)
            button.clicked.connect(
                lambda _checked=False, a=action: self.actionChosen.emit(a)
            )
            layout.addWidget(button)

        # 弹性空间，把确认/取消推到最右侧
        layout.addStretch()

        # 右侧：确认复制（绿色勾）+ 取消（红色 X）
        right_buttons = (
            (":/Close.svg", "cancel", "取消截图"),
            (":/Check.svg", "copy", "确认并复制到剪贴板"),
        )
        for path, action, tip in right_buttons:
            button = QToolButton(self)
            button.setIcon(QIcon(path))
            button.setIconSize(QSize(_ICON_SIZE, _ICON_SIZE))
            button.setToolTip(tip)
            button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            button.setCursor(Qt.CursorShape.ArrowCursor)
            button.setAutoRaise(True)
            button.clicked.connect(
                lambda _checked=False, a=action: self.actionChosen.emit(a)
            )
            layout.addWidget(button)


class SnipOverlay(QWidget):
    """单屏选区覆盖层。坐标一律使用本控件逻辑坐标对外输出。"""

    selectionStarted = Signal(object)          # 开始一次新选区（本覆盖层）
    regionSelected = Signal(object, QRect)     # 选区完成（本覆盖层, 选区矩形）
    cancelled = Signal()
    actionChosen = Signal(str)

    MIN_DRAG = 4  # 拖动距离小于该值视为单击

    def __init__(self, shot: ScreenShot, windows: list,
                 settings=None,
                 parent=None):
        super().__init__(
            parent,
            Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.Tool,
        )
        self.shot = shot
        self._windows = windows

        # 从 Settings 对象提取所有显示配置
        s = settings
        self._toolbar_opacity = s.toolbar_opacity / 100.0
        self._magnifier_enabled = s.magnifier_enabled
        self._magnifier_zoom = s.magnifier_zoom
        self._magnifier_size = s.magnifier_size
        self._magnifier_cross_color = s.magnifier_cross_color
        self._magnifier_cross_alpha = s.magnifier_cross_alpha
        self._magnifier_cross_thickness = s.magnifier_cross_thickness
        self._magnifier_cross_invert = s.magnifier_cross_invert
        self._crosshair_color = s.crosshair_color
        self._crosshair_alpha = s.crosshair_alpha
        self._crosshair_thickness = s.crosshair_thickness
        self._crosshair_invert = s.crosshair_invert
        self._crosshair_enabled = s.crosshair_enabled
        self._cursor_enabled = s.cursor_enabled
        self._grid_enabled = s.grid_enabled
        self._grid_color = s.grid_color
        self._grid_alpha = s.grid_alpha
        self._magnifier_wheel_zoom = s.magnifier_wheel_zoom
        self._edge_mode = s.edge_mode
        self._edge_color = s.edge_color
        self._zoom_label_enabled = s.zoom_label_enabled
        self._zoom_label_color = s.zoom_label_color
        self._zoom_label_alpha = s.zoom_label_alpha
        self._coord_label_enabled = s.coord_label_enabled
        self._coord_label_text_color = s.coord_label_text_color
        self._coord_label_bg_color = s.coord_label_bg_color
        self._coord_label_bg_alpha = s.coord_label_bg_alpha
        self._color_label_enabled = s.color_label_enabled
        self._color_label_text_color = s.color_label_text_color
        self._color_label_bg_color = s.color_label_bg_color
        self._color_label_bg_alpha = s.color_label_bg_alpha
        self._color_format = s.color_format
        self._copy_color_key = s.copy_color_key
        self._copy_hex_prefix = s.copy_hex_prefix
        self._copy_rgb_prefix = s.copy_rgb_prefix

        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)
        # 系统光标：按设置决定显示/隐藏
        if self._cursor_enabled:
            self.setCursor(Qt.CursorShape.CrossCursor)
        else:
            self.setCursor(Qt.CursorShape.BlankCursor)
        self.setGeometry(shot.geometry)

        self._state = "idle"            # idle | dragging | selected
        self._anchor = QPoint()         # 拖动起点
        self._cursor = QPoint()         # 当前鼠标位置（控件坐标）
        self._selection = QRect()       # 选区
        self._hover = None              # 悬停窗口的高亮矩形（QRectF）
        self._toolbar = None
        # 标签位置动画（各自独立）
        self._coord_pos = QPointF(-1, -1)     # 坐标标签当前位置
        self._color_pos = QPointF(-1, -1)     # 颜色标签当前位置
        self._coord_target = QPointF()         # 坐标标签目标位置
        self._color_target = QPointF()         # 颜色标签目标位置
        self._coord_size = QSizeF()            # 坐标标签尺寸
        self._color_size = QSizeF()            # 颜色标签尺寸
        self._mg_pos = QPointF(-1, -1)        # 放大镜当前位置
        self._mg_target = QPointF()            # 放大镜目标位置
        self._mg_side_x = 0                    # 上次放大镜在光标的哪一侧 (±1)
        self._mg_side_y = 0                    # 上次放大镜在光标的哪一侧 (±1)
        self._label_anim_group: QParallelAnimationGroup | None = None
        self._label_needs_anim = False
        self._flip_anim = False                # 翻转动画：放大镜和标签各自独立动画

    # ------------------------------------------------------------ 坐标换算
    @property
    def _dpr(self) -> float:
        return self.shot.dpr

    def _local_to_phys(self, point: QPoint) -> tuple:
        """控件逻辑坐标 -> 桌面物理像素坐标。"""
        dpr = self._dpr
        origin = self.shot.physical_origin
        return origin.x() + point.x() * dpr, origin.y() + point.y() * dpr

    def _cursor_pixmap_pos(self) -> tuple[int, int]:
        """当前光标在本屏 pixmap 中的物理像素坐标（钳制到有效范围）。

        与 _local_to_phys 不同：此方法直接使用控件坐标 × DPR，
        不经过 physical_origin 的桌面绝对偏移。用于从本屏 pixmap
        读取像素颜色（非主屏时也能正确取值）。
        """
        dpr = self._dpr
        cx, cy = self._cursor.x(), self._cursor.y()
        pm = self.shot.pixmap
        px = max(0, min(int(cx * dpr), pm.width() - 1))
        py = max(0, min(int(cy * dpr), pm.height() - 1))
        return px, py

    def _phys_to_local(self, rect4) -> QRectF:
        """物理像素矩形 (l, t, r, b) -> 控件逻辑矩形。"""
        dpr = self._dpr
        origin = self.shot.physical_origin
        left, top, right, bottom = rect4
        return QRectF((left - origin.x()) / dpr, (top - origin.y()) / dpr,
                      (right - left) / dpr, (bottom - top) / dpr)

    # ------------------------------------------------------------ 状态切换
    def clear_selection(self):
        """清除当前选区（其它屏幕开始新选区时由会话调用）。"""
        self._state = "idle"
        self._selection = QRect()
        self._remove_toolbar()
        # 恢复截图模式的光标
        if self._cursor_enabled:
            self.setCursor(Qt.CursorShape.CrossCursor)
        else:
            self.setCursor(Qt.CursorShape.BlankCursor)
        self.update()

    def _enter_selected(self, rect: QRect):
        self._selection = rect
        self._state = "selected"
        self._show_toolbar()
        self.setCursor(Qt.CursorShape.ArrowCursor)  # 选区确定后显示箭头，便于操作工具条
        self.regionSelected.emit(self, self._selection)
        self.update()

    def _remove_toolbar(self):
        if self._toolbar is not None:
            self._toolbar.deleteLater()
            self._toolbar = None

    def _show_toolbar(self):
        self._remove_toolbar()
        toolbar = SelectionToolbar(self._toolbar_opacity, self)
        toolbar.actionChosen.connect(self.actionChosen)
        toolbar.adjustSize()
        size = toolbar.size()
        sel = self._selection
        # 默认放在选区右下角外侧，放不下则改到上方/内部
        x = min(sel.right() - size.width() + 1, self.width() - size.width() - 4)
        x = max(4, x)
        y = sel.bottom() + 6
        if y + size.height() > self.height() - 4:
            y = sel.top() - size.height() - 6
        if y < 4:
            y = max(4, min(sel.bottom() - size.height() - 4,
                           self.height() - size.height() - 4))
        toolbar.move(x, y)
        toolbar.show()
        toolbar.raise_()
        self._toolbar = toolbar

    # ------------------------------------------------------------ 鼠标事件
    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        self.selectionStarted.emit(self)  # 通知会话清理其它屏幕的选区
        self._remove_toolbar()
        self._state = "dragging"
        self._anchor = event.position().toPoint()
        self._cursor = self._anchor
        self._selection = QRect()
        self.update()

    def mouseMoveEvent(self, event):
        self._cursor = event.position().toPoint()
        if self._state == "dragging":
            self._selection = QRect(self._anchor, self._cursor) \
                .normalized().intersected(self.rect())
        elif self._state == "idle":
            self._update_hover()
        self.update()

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton or self._state != "dragging":
            return
        point = event.position().toPoint()
        if (point - self._anchor).manhattanLength() < self.MIN_DRAG:
            # 单击：优先取悬停窗口，否则视为选择整屏
            if self._hover is not None and not self._hover.isEmpty():
                self._enter_selected(self._hover.toAlignedRect())
            else:
                self._enter_selected(self.rect())
        else:
            if self._selection.isEmpty():
                self._state = "idle"
                self.update()
                return
            self._enter_selected(self._selection)

    def mouseDoubleClickEvent(self, event):
        # 双击 = 确认（复���并退出），与 QQ 截图一致
        if self._state == "selected":
            self.actionChosen.emit("copy")

    def wheelEvent(self, event):
        """鼠标滚轮实时调整放大倍率（需启用 magnifier_wheel_zoom）。"""
        if (not self._magnifier_wheel_zoom
                or not self._magnifier_enabled
                or self._state == "selected"):
            return
        delta = event.angleDelta().y()
        if delta > 0:
            self._magnifier_zoom = min(20.0, self._magnifier_zoom + 0.5)
        elif delta < 0:
            self._magnifier_zoom = max(4.0, self._magnifier_zoom - 0.5)
        if delta != 0:
            self._label_needs_anim = True  # 倍率变化时触发标签动画
            self.update()

    def _update_hover(self):
        """命中检测：按 Z 序找鼠标下的第一个窗口。"""
        px, py = self._local_to_phys(self._cursor)
        self._hover = None
        for win in self._windows:
            left, top, right, bottom = win["rect"]
            if left <= px < right and top <= py < bottom:
                rect = self._phys_to_local(win["rect"]) \
                    .intersected(QRectF(self.rect()))
                if not rect.isEmpty():
                    self._hover = rect
                break

    # ------------------------------------------------------------ 键盘事件
    def _match_copy_color_key(self, event) -> bool:
        """判断按键事件是否匹配配置的复制颜色快捷键。"""
        seq = QKeySequence(self._copy_color_key)
        if seq.count() != 1:
            return False
        combo = seq[0]
        return (event.key() == combo.key()
                and event.modifiers() == combo.keyboardModifiers())

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape:
            self.cancelled.emit()
        elif (self._state != "selected"
              and self._color_label_enabled
              and self._match_copy_color_key(event)):
            # 复制像素颜色到剪贴板
            px, py = self._cursor_pixmap_pos()
            r, g, b, a = self._pixel_color(px, py)
            text = self._format_color_copy(r, g, b)
            QGuiApplication.clipboard().setText(text)
        elif self._state == "selected":
            if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
                self.actionChosen.emit("copy")
            elif event.matches(QKeySequence.StandardKey.Save):
                self.actionChosen.emit("save")

    # ------------------------------------------------------------ 绘制
    def paintEvent(self, _event):
        """主绘制入口：按层次委托给独立的绘制方法。"""
        painter = QPainter(self)
        self._draw_background(painter)
        self._draw_selection_hover(painter)
        self._draw_idle_overlays(painter)
        self._draw_hint(painter)
        painter.end()

    # -------------------------------------------------------- 层 1：背景
    def _draw_background(self, painter: QPainter):
        """冻结的屏幕截图 + 全局暗化遮罩。"""
        pixmap = self.shot.pixmap
        painter.drawPixmap(self.rect(), pixmap)
        painter.fillRect(self.rect(), MASK_COLOR)

    # -------------------------------------------------------- 层 2：选区 / 悬停窗口
    def _draw_selection_hover(self, painter: QPainter):
        """选区亮区（dragging/selected）或窗口悬停高亮（idle）。"""
        pixmap = self.shot.pixmap
        dpr = self._dpr

        if self._state in ("dragging", "selected") and not self._selection.isEmpty():
            sel = QRectF(self._selection)
            src = QRectF(sel.x() * dpr, sel.y() * dpr,
                         sel.width() * dpr, sel.height() * dpr)
            painter.drawPixmap(sel, pixmap, src)
            painter.setPen(QPen(SELECTION_COLOR, 1.5))
            painter.drawRect(sel)
            self._draw_size_label(painter, sel)
        elif self._state == "idle" and self._hover is not None:
            rect = self._hover
            src = QRectF(rect.x() * dpr, rect.y() * dpr,
                         rect.width() * dpr, rect.height() * dpr)
            painter.drawPixmap(rect, pixmap, src)
            painter.setPen(QPen(SELECTION_COLOR, 1.5))
            painter.drawRect(rect)

    # -------------------------------------------------------- 层 3：准星 + 放大镜 / 标签
    def _draw_idle_overlays(self, painter: QPainter):
        """idle 状态下鼠标悬浮时：十字准星 + 放大镜（或纯标签）。"""
        if self._state == "selected" or not self.underMouse():
            return
        cx, cy = self._cursor.x(), self._cursor.y()
        self._draw_crosshair_lines(painter, cx, cy)
        if self._magnifier_enabled:
            self._draw_magnifier(painter, cx, cy)
        else:
            self._draw_info_label(painter, cx, cy)

    # -------------------------------------------------------- 层 4：底部提示

    def _draw_size_label(self, painter: QPainter, sel: QRectF):
        """在选区左上角附近显示尺寸，如 800 × 600。"""
        text = f"{int(sel.width())} × {int(sel.height())}"
        metrics = painter.fontMetrics()
        w = metrics.horizontalAdvance(text) + 12
        h = metrics.height() + 8
        x = sel.left()
        y = sel.top() - h - 4
        if y < 0:  # 上方没空间就画到选区内部
            y = sel.top() + 4
        painter.fillRect(QRectF(x, y, w, h), QColor(0, 0, 0, 170))
        painter.setPen(QColor(255, 255, 255))
        painter.drawText(QRectF(x, y, w, h), Qt.AlignmentFlag.AlignCenter, text)

    def _draw_crosshair_lines(self, painter: QPainter, cx: int, cy: int):
        """绘制全屏十字线（独立于放大镜/信息标签）。"""
        if not self._crosshair_enabled:
            return
        dpr = self._dpr
        line_w = self._crosshair_thickness / dpr
        # 垂直线断点：距水平线中心偏移一个完整线宽，
        # 确保水平和垂直的 stroke 完全不重叠
        gap = line_w

        if self._crosshair_invert:
            painter.save()
            painter.setCompositionMode(
                QPainter.CompositionMode.CompositionMode_Difference
            )
            painter.setPen(QPen(QColor(255, 255, 255), line_w))
            painter.drawLine(QPointF(0, cy + 0.5), QPointF(self.width(), cy + 0.5))
            painter.drawLine(QPointF(cx + 0.5, 0), QPointF(cx + 0.5, cy + 0.5 - gap))
            painter.drawLine(QPointF(cx + 0.5, cy + 0.5 + gap), QPointF(cx + 0.5, self.height()))
            painter.restore()
        else:
            c = QColor(self._crosshair_color)
            c.setAlphaF(self._crosshair_alpha / 100.0)
            painter.setPen(QPen(c, line_w))
            painter.drawLine(QPointF(0, cy + 0.5), QPointF(self.width(), cy + 0.5))
            painter.drawLine(QPointF(cx + 0.5, 0), QPointF(cx + 0.5, cy + 0.5 - gap))
            painter.drawLine(QPointF(cx + 0.5, cy + 0.5 + gap), QPointF(cx + 0.5, self.height()))

    # -------------------------------------------------------- 标签位置动画
    def _animate_labels(self, has_labels: bool):
        """统一管理放大镜和标签的位移动画。

        规则：
        - 变倍模式（_label_needs_anim）：放大镜直接跳转到目标，标签独立动画
        - 翻转模式（_flip_anim）：放大镜和标签各自独立线性动画到目标
        - 两个标志同时为 True 时，变倍模式优先（放大镜不动画）
        - 变倍时若标签动画正在运行：先停止旧动画，snap 放大镜，重启标签动画
        """
        # 变倍模式：无论如何立即 snap 放大镜（必须在 Running 检查之前）
        if self._label_needs_anim:
            self._mg_pos = QPointF(self._mg_target)

        # 动画正在运行时：变倍需重启标签动画；翻转则保持运行不打断
        if self._label_anim_group is not None and \
                self._label_anim_group.state() == QParallelAnimationGroup.State.Running:
            if self._label_needs_anim and has_labels:
                self._label_anim_group.stop()
                self._label_anim_group = None
            else:
                return

        # 变倍模式（含标签）优先于翻转模式，避免变倍时放大镜意外动画
        needs_labels_zoom = self._label_needs_anim and has_labels
        needs_mg_flip = self._flip_anim and not self._label_needs_anim
        needs_labels_flip = self._flip_anim and has_labels
        self._flip_anim = False
        self._label_needs_anim = False

        if not needs_mg_flip and not needs_labels_zoom and not needs_labels_flip:
            # 无需动画：所有元素直接跳到目标
            self._mg_pos = QPointF(self._mg_target)
            self._coord_pos = QPointF(self._coord_target)
            self._color_pos = QPointF(self._color_target)
            return

        # 首次绘制：直接初始化目标位置，不做动画
        if self._mg_pos.x() < 0:
            self._mg_pos = QPointF(self._mg_target)
        if self._coord_pos.x() < 0:
            self._coord_pos = QPointF(self._coord_target)
        if self._color_pos.x() < 0:
            self._color_pos = QPointF(self._color_target)

        if self._label_anim_group is not None:
            self._label_anim_group.stop()

        def _make_anim(start: QPointF, end: QPointF) -> QVariantAnimation:
            a = QVariantAnimation(self)
            a.setDuration(120)
            a.setStartValue(start)
            a.setEndValue(end)
            a.setEasingCurve(QEasingCurve.Type.Linear)
            return a

        group = QParallelAnimationGroup(self)

        if needs_mg_flip:
            # 翻转模式：放大镜线性动画
            mg_anim = _make_anim(self._mg_pos, self._mg_target)
            mg_anim.valueChanged.connect(lambda v: self._on_mg_pos(v))
            group.addAnimation(mg_anim)
        else:
            # 变倍模式或无翻转：放大镜直接跳到目标
            self._mg_pos = QPointF(self._mg_target)

        if needs_labels_zoom or needs_labels_flip:
            # 标签各自独立动画到目标（不使用偏移跟随，避免闪烁）
            coord_anim = _make_anim(self._coord_pos, self._coord_target)
            coord_anim.valueChanged.connect(lambda v: self._on_coord_pos(v))
            group.addAnimation(coord_anim)

            color_anim = _make_anim(self._color_pos, self._color_target)
            color_anim.valueChanged.connect(lambda v: self._on_color_pos(v))
            group.addAnimation(color_anim)

        group.finished.connect(self._on_label_anim_done)
        group.start()
        self._label_anim_group = group

    def _on_mg_pos(self, value: QPointF):
        self._mg_pos = value
        self.update()

    def _on_coord_pos(self, value: QPointF):
        self._coord_pos = value
        self.update()

    def _on_color_pos(self, value: QPointF):
        self._color_pos = value
        self.update()

    def _on_label_anim_done(self):
        self._label_anim_group = None

    # -------------------------------------------------------- 放大镜
    def _draw_magnifier(self, painter: QPainter, cx: int, cy: int):
        """放大镜绘制入口：准备帧 → 动画 → 渲染像素 → 渲染叠加层 → 渲染标签。"""
        # Phase 1: 计算本帧所有显示参数
        f = self._prepare_mg_frame(cx, cy)
        if f is None:
            return

        # Phase 2: 计算标签目标 + 动画 + 应用显示位置
        self._compute_label_targets(f, painter)
        self._animate_labels(f.has_labels)
        self._apply_mg_display_pos(f)

        # Phase 3-5: 分层渲染
        self._render_mg_pixels(painter, f)
        self._render_mg_overlays(painter, f)
        self._render_mg_labels(painter, f)

    # -------------------------------------------------------- Phase 1: 计算帧参数
    def _prepare_mg_frame(self, cx: int, cy: int) -> _MgFrame | None:
        """计算本帧放大镜的所有显示参数（不涉及任何绘制）。

        返回 _MgFrame，若裁剪后无可显示区域则返回 None。
        同时更新 _mg_target、翻转检测等实例状态。
        """
        f = _MgFrame()
        f.zoom = self._magnifier_zoom
        f.half = self._magnifier_size
        pixmap = self.shot.pixmap
        pm_w, pm_h = pixmap.width(), pixmap.height()

        # 光标物理像素坐标
        phys_cx, phys_cy = create_cursor_provider().physical_position()
        phys_cx -= int(self.shot.physical_origin.x())
        phys_cy -= int(self.shot.physical_origin.y())
        f.phys_cx = max(0, min(phys_cx, pm_w - 1))
        f.phys_cy = max(0, min(phys_cy, pm_h - 1))

        f.is_pad = (self._edge_mode == "pad")
        zoom = f.zoom
        half = f.half

        if f.is_pad:
            f.full_left = f.phys_cx - half
            f.full_top = f.phys_cy - half
            f.full_w = 2 * half
            f.full_h = 2 * half

            f.src_left = max(0, f.full_left)
            f.src_top = max(0, f.full_top)
            src_right = min(pm_w, f.phys_cx + half)
            src_bottom = min(pm_h, f.phys_cy + half)
            f.src_w = src_right - f.src_left
            f.src_h = src_bottom - f.src_top
            f.disp_w = f.full_w * zoom
            f.disp_h = f.full_h * zoom
        else:
            f.src_left = max(0, f.phys_cx - half)
            f.src_top = max(0, f.phys_cy - half)
            src_right = min(pm_w, f.phys_cx + half)
            src_bottom = min(pm_h, f.phys_cy + half)
            f.src_w = src_right - f.src_left
            f.src_h = src_bottom - f.src_top
            if f.src_w <= 0 or f.src_h <= 0:
                return None
            f.disp_w = f.src_w * zoom
            f.disp_h = f.src_h * zoom

        # 定位：默认光标右上方
        gap = 40
        mg_x = cx + gap
        mg_y = cy - f.disp_h - gap
        if mg_x + f.disp_w > self.width():
            mg_x = cx - f.disp_w - gap
        if mg_y < 0:
            mg_y = cy + gap
        f.target_x = max(0.0, min(float(mg_x), self.width() - f.disp_w))
        f.target_y = max(0.0, min(float(mg_y), self.height() - f.disp_h))

        self._mg_target = QPointF(f.target_x, f.target_y)

        # 翻转检测
        new_side_x = 1 if f.target_x >= cx else -1
        new_side_y = 1 if f.target_y >= cy else -1
        if self._mg_side_x != 0 and (self._mg_side_x != new_side_x or self._mg_side_y != new_side_y):
            self._flip_anim = True
        self._mg_side_x = new_side_x
        self._mg_side_y = new_side_y

        f.zoom_label_enabled = self._zoom_label_enabled
        return f

    # -------------------------------------------------------- Phase 2: 标签目标 + 动画 + 显示位置
    def _compute_label_targets(self, f: _MgFrame, painter: QPainter):
        """计算坐标/颜色标签的目标位置（基于放大镜目标位置 target_x/y）。"""
        _pre_zoom_label_h = (painter.fontMetrics().height() + 6) if self._zoom_label_enabled else 0
        f.zoom_label_h = float(_pre_zoom_label_h)

        coord, color, _ = self._info_label_texts()
        f.coord_text = coord      # 缓存，_render_mg_labels() 复用，避免同帧二次调用 toImage()
        f.color_text = color
        f.has_labels = bool(coord or color)
        if not f.has_labels:
            return

        metrics = painter.fontMetrics()
        text_h = metrics.height() + 8
        coord_w = metrics.horizontalAdvance(coord) + 12 if coord else 0
        color_w = metrics.horizontalAdvance(color) + 12 if color else 0
        zoom = f.zoom

        if zoom >= 10:
            label_w = max(coord_w, color_w)
            self._coord_target = QPointF(f.target_x + 3, f.target_y + 3 + _pre_zoom_label_h + 2)
            self._coord_size = QSizeF(label_w, text_h)
            self._color_target = QPointF(
                f.target_x + 3,
                f.target_y + 3 + _pre_zoom_label_h + 2 + text_h if coord else f.target_y + 3 + _pre_zoom_label_h + 2)
            self._color_size = QSizeF(label_w, text_h)
        elif zoom >= 7:
            total_w = coord_w + color_w
            base_x = f.target_x + (f.disp_w - total_w) / 2
            base_y = f.target_y + f.disp_h + 4
            self._coord_target = QPointF(base_x, base_y)
            self._coord_size = QSizeF(coord_w, text_h)
            self._color_target = QPointF(base_x + coord_w, base_y)
            self._color_size = QSizeF(color_w, text_h)
        else:
            label_w = max(coord_w, color_w)
            base_x = f.target_x + (f.disp_w - label_w) / 2
            base_y = f.target_y + f.disp_h + 4
            self._coord_target = QPointF(base_x, base_y)
            self._coord_size = QSizeF(label_w, text_h)
            self._color_target = QPointF(base_x, base_y + text_h if coord else base_y)
            self._color_size = QSizeF(label_w, text_h)

    def _apply_mg_display_pos(self, f: _MgFrame):
        """从动画后的 _mg_pos 读取本帧实际显示位置写入 frame（target 兜底）。"""
        f.mg_x = float(self._mg_pos.x()) if self._mg_pos.x() >= 0 else f.target_x
        f.mg_y = float(self._mg_pos.y()) if self._mg_pos.y() >= 0 else f.target_y

    # -------------------------------------------------------- Phase 3: 像素渲染
    def _render_mg_pixels(self, painter: QPainter, f: _MgFrame):
        """绘制放大镜暗色背景 + 像素块（pad/crop 模式）。"""
        pixmap = self.shot.pixmap
        mg_x, mg_y = f.mg_x, f.mg_y
        disp_w, disp_h = f.disp_w, f.disp_h
        zoom = f.zoom

        # 暗色背景
        painter.fillRect(QRectF(mg_x, mg_y, disp_w, disp_h), QColor(0, 0, 0, 220))

        if f.is_pad:
            if f.src_w > 0 and f.src_h > 0:
                offset_x = (f.src_left - f.full_left) * zoom
                offset_y = (f.src_top - f.full_top) * zoom
                src_rect = QRectF(f.src_left, f.src_top, f.src_w, f.src_h)
                dst_rect = QRectF(mg_x + offset_x, mg_y + offset_y,
                                  f.src_w * zoom, f.src_h * zoom)

                edge_qcolor = QColor(self._edge_color)
                if f.src_left > f.full_left:
                    painter.fillRect(QRectF(mg_x, mg_y, offset_x, disp_h), edge_qcolor)
                if f.src_left + f.src_w < f.phys_cx + f.half:
                    right_gap = (f.phys_cx + f.half - f.src_left - f.src_w) * zoom
                    painter.fillRect(
                        QRectF(mg_x + disp_w - right_gap, mg_y, right_gap, disp_h), edge_qcolor)
                if f.src_top > f.full_top:
                    painter.fillRect(QRectF(mg_x, mg_y, disp_w, offset_y), edge_qcolor)
                if f.src_top + f.src_h < f.phys_cy + f.half:
                    bottom_gap = (f.phys_cy + f.half - f.src_top - f.src_h) * zoom
                    painter.fillRect(
                        QRectF(mg_x, mg_y + disp_h - bottom_gap, disp_w, bottom_gap), edge_qcolor)

                painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
                painter.drawPixmap(dst_rect, pixmap, src_rect)
                painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
            else:
                painter.fillRect(QRectF(mg_x, mg_y, disp_w, disp_h), QColor(self._edge_color))
        else:
            src_rect = QRectF(f.src_left, f.src_top, f.src_w, f.src_h)
            dst_rect = QRectF(mg_x, mg_y, disp_w, disp_h)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
            painter.drawPixmap(dst_rect, pixmap, src_rect)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)

    # -------------------------------------------------------- Phase 4: 叠加层（网格 + 准星 + 边框）
    def _render_mg_overlays(self, painter: QPainter, f: _MgFrame):
        """绘制放大镜的像素网格线、中心准星、外边框。"""
        mg_x, mg_y = f.mg_x, f.mg_y
        disp_w, disp_h = f.disp_w, f.disp_h
        zoom, half = f.zoom, f.half

        # 像素网格线
        if self._grid_enabled:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
            grid_width = 1.0 / self._dpr
            grid_qcolor = QColor(self._grid_color)
            grid_qcolor.setAlphaF(self._grid_alpha / 100.0)
            total_cols = int(disp_w / zoom)
            total_rows = int(disp_h / zoom)
            for col in range(1, total_cols):
                x = mg_x + col * zoom
                painter.fillRect(QRectF(x, mg_y, grid_width, disp_h), grid_qcolor)
            for row in range(1, total_rows):
                y = mg_y + row * zoom
                painter.fillRect(QRectF(mg_x, y, disp_w, grid_width), grid_qcolor)

        # 中心准星
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        dpr = self._dpr
        if f.is_pad:
            center_x = int(mg_x + half * zoom + zoom / 2) + 0.5
            center_y = int(mg_y + half * zoom + zoom / 2) + 0.5
        else:
            center_x = int(mg_x + (f.phys_cx - f.src_left) * zoom + zoom / 2) + 0.5
            center_y = int(mg_y + (f.phys_cy - f.src_top) * zoom + zoom / 2) + 0.5

        if self._magnifier_cross_invert:
            self._draw_invert_crosshair_v2(painter, f, center_x, center_y)
        else:
            cross_color = QColor(self._magnifier_cross_color)
            cross_color.setAlphaF(self._magnifier_cross_alpha / 100.0)
            painter.setPen(QPen(cross_color, self._magnifier_cross_thickness / dpr))
            painter.drawLine(QPointF(center_x, mg_y), QPointF(center_x, mg_y + disp_h))
            painter.drawLine(QPointF(mg_x, center_y), QPointF(mg_x + disp_w, center_y))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        # 外边框
        painter.setPen(QPen(SELECTION_COLOR, 1))
        painter.drawRect(QRectF(mg_x, mg_y, disp_w, disp_h))

    # -------------------------------------------------------- Phase 5: 标签渲染
    def _render_mg_labels(self, painter: QPainter, f: _MgFrame):
        """绘制倍率标签 + 坐标/颜色标签（使用动画后的显示位置）。"""
        # 倍率标签（左上角内侧）
        if f.zoom_label_enabled:
            self._draw_zoom_label(painter, f.mg_x, f.mg_y, f.zoom)

        # 坐标 / 颜色标签
        if f.has_labels:
            self._draw_info_label_rects(
                painter, f.coord_text, f.color_text,
                self._coord_pos, self._color_pos,
                self._coord_size, self._color_size,
            )

    # -------------------------------------------------------- 反色准星（新签，使用 _MgFrame）
    def _draw_invert_crosshair_v2(self, painter: QPainter, f: _MgFrame,
                                   center_x: float, center_y: float):
        """反色模式准星（使用 _MgFrame 参数）。"""
        dpr = self._dpr
        thickness = self._magnifier_cross_thickness
        alpha = self._magnifier_cross_alpha / 100.0
        pen_color = QColor(255, 255, 255, int(255 * alpha))

        painter.save()
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Difference)
        painter.setPen(QPen(pen_color, thickness / dpr))
        painter.drawLine(QPointF(center_x, f.mg_y),
                         QPointF(center_x, f.mg_y + f.disp_h))
        painter.drawLine(QPointF(f.mg_x, center_y),
                         QPointF(f.mg_x + f.disp_w, center_y))
        painter.restore()

    # -------------------------------------------------------- 倍率标签
    def _draw_zoom_label(self, painter: QPainter,
                         mg_x: float, mg_y: float, zoom: float) -> float:
        """在放大镜左上角内侧绘制当前倍率（如 4.0×），返回标签高度。"""
        text = f"{zoom:.1f}×"
        metrics = painter.fontMetrics()
        text_w = metrics.horizontalAdvance(text) + 8
        text_h = metrics.height() + 6
        pad = 3
        x = mg_x + pad
        y = mg_y + pad

        zoom_color = QColor(self._zoom_label_color)
        zoom_color.setAlphaF(self._zoom_label_alpha / 100.0)
        painter.fillRect(QRectF(x, y, text_w, text_h), QColor(0, 0, 0, 170))
        painter.setPen(zoom_color)
        painter.drawText(QRectF(x, y, text_w, text_h),
                         Qt.AlignmentFlag.AlignCenter, text)
        return text_h

    # -------------------------------------------------------- 信息标签（坐标 + 颜色）
    def _info_label_texts(self) -> tuple:
        """返回 (坐标文本, 颜色文本, 颜色RGBA) 或空字符串表示禁用。

        坐标直接走系统级 GetCursorPos（物理像素），与放大镜渲染同源，
        避免"物理→Qt逻辑→物理"往返转换导致的精度丢失（如 2559 永远
        不显示、2555 重复出现等问题）。
        """
        coord_text = ""
        color_text = ""
        color_rgba = None
        if self._coord_label_enabled:
            # 直接用系统物理光标位置，不走 _cursor（整数截断）→_local_to_phys（浮点→int）
            px, py = create_cursor_provider().physical_position()
            coord_text = f"坐标：{px}, {py}"
        if self._color_label_enabled:
            # 用 _cursor_pixmap_pos 直接取本屏 pixmap 像素
            # （不经过桌面绝对坐标，非主屏也能正确取值）
            px, py = self._cursor_pixmap_pos()
            r, g, b, a = self._pixel_color(px, py)
            color_text = f"色值：{self._format_color_text(r, g, b)}"
            color_rgba = (r, g, b, a)
        return coord_text, color_text, color_rgba

    def _draw_info_label(self, painter: QPainter, cx: int, cy: int, gap: int = 12):
        """在准星附近绘制复合标签：坐标 + 颜色（无放大镜时）。"""
        coord, color, _ = self._info_label_texts()
        if not coord and not color:
            return
        metrics = painter.fontMetrics()
        text_h = metrics.height() + 8
        coord_w = metrics.horizontalAdvance(coord) + 12 if coord else 0
        color_w = metrics.horizontalAdvance(color) + 12 if color else 0
        label_w = max(coord_w, color_w)
        total_h = (text_h if coord else 0) + (text_h if color else 0)

        x = cx + gap
        y = cy + gap
        if x + label_w > self.width():
            x = cx - label_w - gap
        if y + total_h > self.height():
            y = cy - total_h - gap
        x = max(0, min(x, self.width() - label_w))
        y = max(0, min(y, self.height() - total_h))

        coord_pos = QPointF(x, y)
        color_pos = QPointF(x, y + text_h if coord else y)
        self._draw_info_label_rects(
            painter, coord, color,
            coord_pos, color_pos,
            QSizeF(label_w, text_h), QSizeF(label_w, text_h),
        )

    def _draw_info_label_rects(self, painter: QPainter,
                                coord: str, color: str,
                                coord_pos: QPointF, color_pos: QPointF,
                                coord_size: QSizeF, color_size: QSizeF):
        """绘制两个独立标签：各自有位置、尺寸、背景色。"""
        if coord:
            coord_bg = QColor(self._coord_label_bg_color)
            coord_bg.setAlphaF(self._coord_label_bg_alpha / 100.0)
            painter.fillRect(QRectF(coord_pos, coord_size), coord_bg)
            painter.setPen(QColor(self._coord_label_text_color))
            painter.drawText(QRectF(coord_pos, coord_size),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter, coord)
        if color:
            color_bg = QColor(self._color_label_bg_color)
            color_bg.setAlphaF(self._color_label_bg_alpha / 100.0)
            painter.fillRect(QRectF(color_pos, color_size), color_bg)
            painter.setPen(QColor(self._color_label_text_color))
            painter.drawText(QRectF(color_pos, color_size),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter, color)

    def _draw_hint(self, painter: QPainter):
        """底部操作提示（仅在 idle 状态显示）。"""
        if self._state != "idle":
            return
        text = "拖动选择区域 · 单击窗口截图 · 双击/回车复制 · Esc 取消"
        metrics = painter.fontMetrics()
        w = metrics.horizontalAdvance(text) + 24
        h = metrics.height() + 12
        x = (self.width() - w) / 2
        painter.fillRect(QRectF(x, 24, w, h), QColor(0, 0, 0, 150))
        painter.setPen(QColor(255, 255, 255))
        painter.drawText(QRectF(x, 24, w, h), Qt.AlignmentFlag.AlignCenter, text)

    # -------------------------------------------------------- 像素颜色标签
    def _pixel_color(self, px: int, py: int):
        """读取物理像素坐标 (px, py) 处的颜色，返回 (r, g, b, a)。"""
        img = self.shot.pixmap.toImage()
        qc = img.pixelColor(px, py)
        return qc.red(), qc.green(), qc.blue(), qc.alpha()

    def _format_color_text(self, r, g, b) -> str:
        """按当前格式设置返回颜色文本。"""
        if self._color_format == "rgb":
            return f"{r}, {g}, {b}"
        else:  # hex
            return f"#{r:02X}{g:02X}{b:02X}"

    def _format_color_copy(self, r, g, b) -> str:
        """按当前复制格式返回剪贴板文本，与显示一致。"""
        if self._color_format == "rgb":
            vals = f"{r}, {g}, {b}"
            if self._copy_rgb_prefix:
                return f"rgb({vals})"
            return vals
        else:  # hex
            h = f"{r:02X}{g:02X}{b:02X}"
            return f"#{h}" if self._copy_hex_prefix else h

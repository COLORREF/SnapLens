"""屏幕截取与图像裁剪。

坐标约定（重要）：
- Qt 逻辑坐标：控件尺寸、QCursor.pos() 等使用的坐标；
- 物理像素坐标：QPixmap 实际像素、Win32 窗口矩形使用的坐标；
- 两者通过每个屏幕的 devicePixelRatio（DPR）换算。
"""
from dataclasses import dataclass

from PySide6.QtCore import QPoint, QPointF, QRect, QRectF
from PySide6.QtGui import QGuiApplication, QPixmap, QScreen


@dataclass
class ScreenShot:
    """单个屏幕的截图及其几何信息。"""
    screen: QScreen
    pixmap: QPixmap      # 物理像素尺寸 = 逻辑尺寸 * DPR
    geometry: QRect      # 屏幕在虚拟桌面中的逻辑坐标

    @property
    def dpr(self) -> float:
        return self.pixmap.devicePixelRatio()

    @property
    def physical_origin(self) -> QPointF:
        """该屏左上角在"物理像素桌面"中的坐标。

        按屏幕空间位置（非枚举顺序）推算。核心思路：
        1. 找出所有在左侧的屏幕，按"列"分组（x 范围有重叠的归为同列）；
        2. 每列取最大物理宽度（同列中不同屏幕的 DPR 可能不同）；
        3. 各列宽度累加即得 x 偏移。y 偏移同理按"行"分组。

        支持任意排列（水平、垂直、2×2、3×3 网格等）和混合 DPI 场景。
        """
        my = self.screen.geometry()
        others = []
        for screen in QGuiApplication.screens():
            if screen == self.screen:
                continue
            g = screen.geometry()
            dpr = screen.devicePixelRatio()
            others.append({
                "geo": g,
                "dpr": dpr,
                "phys_w": g.width() * dpr,
                "phys_h": g.height() * dpr,
            })

        # ---- X 偏移：左侧各列物理宽度之和 ----
        # 收集所有竖直重叠且完全在左侧的屏幕
        left_screens = []
        for o in others:
            g = o["geo"]
            # 竖直方向必须有重叠（用 >= 处理边缘相接）
            if g.top() < my.bottom() and g.bottom() > my.top():
                if g.right() <= my.left():
                    left_screens.append(o)
        # 按"列"分组：x 范围有重叠的为同列，每列取最大物理宽度
        x = _sum_column_widths(left_screens)

        # ---- Y 偏移：上方各行物理高度之和 ----
        top_screens = []
        for o in others:
            g = o["geo"]
            # 水平方向必须有重叠（用 >= 处理边缘相接）
            if g.left() < my.right() and g.right() > my.left():
                if g.bottom() <= my.top():
                    top_screens.append(o)
        y = _sum_row_heights(top_screens)

        return QPointF(x, y)


def _group_by_axis(screens: list, axis: str) -> list:
    """将屏幕按 x 或 y 方向的重叠关系分组（"列"或"行"）。

    axis="x" → 按水平重叠分组（同列）；axis="y" → 按垂直重叠分组（同行）。
    返回列表的列表，每个子列表是同一组内的屏幕数据。
    """
    groups: list[list] = []
    for s in screens:
        g = s["geo"]
        placed = False
        for grp in groups:
            # 已有组在该轴上的范围
            if axis == "x":
                grp_left = min(m["geo"].left() for m in grp)
                grp_right = max(m["geo"].right() for m in grp)
                # x 范围重叠即同列
                if g.left() < grp_right and grp_left < g.right():
                    grp.append(s)
                    placed = True
                    break
            else:  # axis == "y"
                grp_top = min(m["geo"].top() for m in grp)
                grp_bottom = max(m["geo"].bottom() for m in grp)
                # y 范围重叠即同行
                if g.top() < grp_bottom and grp_top < g.bottom():
                    grp.append(s)
                    placed = True
                    break
        if not placed:
            groups.append([s])
    return groups


def _sum_column_widths(left_screens: list) -> float:
    """左侧屏幕按列分组，每列取最大物理宽度后累加。"""
    total = 0.0
    for col in _group_by_axis(left_screens, "x"):
        total += max(s["phys_w"] for s in col)
    return total


def _sum_row_heights(top_screens: list) -> float:
    """上方屏幕按行分组，每行取最大物理高度后累加。"""
    total = 0.0
    for row in _group_by_axis(top_screens, "y"):
        total += max(s["phys_h"] for s in row)
    return total


class CapturedDesktop:
    """一次截图动作抓取的整个虚拟桌面（支持多屏）。"""

    def __init__(self):
        self.shots: list[ScreenShot] = []
        for screen in QGuiApplication.screens():
            pixmap = screen.grabWindow(0)  # 抓取整个屏幕
            if pixmap.isNull():
                continue
            self.shots.append(ScreenShot(screen, pixmap, screen.geometry()))

    def crop(self, shot: ScreenShot, local_rect: QRect) -> QPixmap:
        """从某屏截图中裁出选区。local_rect 为该屏控件逻辑坐标。"""
        dpr = shot.dpr
        phys = QRectF(
            local_rect.x() * dpr, local_rect.y() * dpr,
            local_rect.width() * dpr, local_rect.height() * dpr,
        ).toAlignedRect().intersected(QRect(QPoint(0, 0), shot.pixmap.size()))
        out = shot.pixmap.copy(phys)
        # 保留 DPR，钉图窗口才能以用户看到的逻辑尺寸显示
        out.setDevicePixelRatio(dpr)
        return out

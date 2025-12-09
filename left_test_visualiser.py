#!/usr/bin/env python3
"""
Left Test Visualiser — FIXED VERSION
No remove() errors, fully interactive.
"""
import matplotlib
matplotlib.use("TkAgg")



import matplotlib.pyplot as plt
import numpy as np

# ---------- Geometry ----------
def area2(a, b, c):
    ax, ay = a
    bx, by = b
    cx, cy = c
    return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)

def Left(a, b, c):
    return area2(a, b, c) > 0

def Collinear(a, b, c):
    return area2(a, b, c) == 0


# ---------- Draggable point ----------
class DraggablePoint:
    def __init__(self, artist, idx, callback):
        self.artist = artist
        self.idx = idx
        self.callback = callback
        self.press = None

        fig = artist.figure
        self.cidpress = fig.canvas.mpl_connect('button_press_event', self.on_press)
        self.cidrelease = fig.canvas.mpl_connect('button_release_event', self.on_release)
        self.cidmotion = fig.canvas.mpl_connect('motion_notify_event', self.on_motion)

    def on_press(self, event):
        if event.inaxes != self.artist.axes:
            return
        contains, _ = self.artist.contains(event)
        if not contains:
            return
        x0, y0 = self.artist.get_data()
        self.press = (x0[0], y0[0], event.xdata, event.ydata)

    def on_motion(self, event):
        if self.press is None or event.inaxes != self.artist.axes:
            return
        x0, y0, ex0, ey0 = self.press
        dx = event.xdata - ex0
        dy = event.ydata - ey0
        newx, newy = x0 + dx, y0 + dy
        self.callback(self.idx, (newx, newy))

    def on_release(self, event):
        self.press = None



# ---------- Visualiser ----------
def left_test_visualiser():
    # Initial vertices
    pts = [
        (-2.0, 2.0),   # A
        (2.0, 7.0),    # B
        (4.0, 1.0)     # C
    ]

    fig, ax = plt.subplots(figsize=(7, 6))
    ax.set_aspect('equal')
    ax.grid(True)
    ax.set_title("Left Test Visualiser — Drag A, B, or C")

    # Draw AB line
    (line_ab,) = ax.plot([pts[0][0], pts[1][0]],
                         [pts[0][1], pts[1][1]], 'k-', lw=2)

    # SINGLE triangle patch (polygon)
    triangle = plt.Polygon(pts, closed=True, alpha=0.15, color='gray')
    ax.add_patch(triangle)

    # Points
    (ptA,) = ax.plot(pts[0][0], pts[0][1], 'ko', ms=8)
    (ptB,) = ax.plot(pts[1][0], pts[1][1], 'ko', ms=8)
    (ptC,) = ax.plot(pts[2][0], pts[2][1], 'ro', ms=8)

    labelA = ax.text(pts[0][0], pts[0][1], " A")
    labelB = ax.text(pts[1][0], pts[1][1], " B")
    labelC = ax.text(pts[2][0], pts[2][1], " C", color='red')

    # Status
    status = ax.text(0.02, 0.98, "", transform=ax.transAxes, va='top',
                     bbox=dict(facecolor='white', alpha=0.7))

    # --- REDRAW FUNCTION ---
    def redraw():
        A, B, C = pts

        # update AB line
        line_ab.set_data([A[0], B[0]], [A[1], B[1]])

        # update triangle polygon vertices
        triangle.set_xy([A, B, C])

        # update point markers
        ptA.set_data([A[0]], [A[1]])
        ptB.set_data([B[0]], [B[1]])
        ptC.set_data([C[0]], [C[1]])
        labelA.set_position((A[0], A[1]))
        labelB.set_position((B[0], B[1]))
        labelC.set_position((C[0], C[1]))

        # area & orientation
        A2 = area2(A, B, C)

        if A2 > 0:
            orient = "LEFT (Area2 > 0)"
            color = "green"
        elif A2 < 0:
            orient = "RIGHT (Area2 < 0)"
            color = "blue"
        else:
            orient = "COLLINEAR (Area2 = 0)"
            color = "orange"

        status.set_text(f"Area2 = {A2:.3f}\nOrientation: {orient}")
        labelC.set_color(color)
        ptC.set_color(color)

        fig.canvas.draw_idle()


    # --- Callback on drag ---
    def move_point(idx, xy):
        pts[idx] = (float(xy[0]), float(xy[1]))
        redraw()


    # Make points draggable
    DraggablePoint(ptA, 0, move_point)
    DraggablePoint(ptB, 1, move_point)
    DraggablePoint(ptC, 2, move_point)

    redraw()
    plt.show()



if __name__ == "__main__":
    left_test_visualiser()

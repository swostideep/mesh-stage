#!/usr/bin/env python3
import matplotlib
matplotlib.use("MacOSX")

import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.widgets import Button
import subprocess
import threading
import queue
import os
import time

# ===============================
# 1. Paths
# ===============================

WORKDIR = "/Users/swostideepnayak/Desktop/Geometric computation in c"
TRIANGULATE = os.path.join(WORKDIR, "triangulate")
POLYFILE = os.path.join(WORKDIR, "poly.txt")

print("[DEBUG] WORKDIR =", WORKDIR)

# ===============================
# 2. Read C output into event list
# ===============================

def run_c_and_collect_events():
    """Run C once and store ALL its outputs in a list."""
    print("[DEBUG] Collecting events...")
    proc = subprocess.Popen(
        [TRIANGULATE],
        cwd=WORKDIR,
        stdout=subprocess.PIPE,
        stdin=open(POLYFILE, "r"),
        stderr=subprocess.STDOUT,
        shell=False
    )

    events = []
    for raw in proc.stdout:
        line = raw.decode("utf-8").strip()
        if line:
            print("[C OUTPUT]", line)
            events.append(line)

    print("[DEBUG] Total Events Collected:", len(events))
    return events


# ===============================
# 3. Button callbacks
# ===============================

is_paused = False

def toggle_pause(event):
    global is_paused
    is_paused = not is_paused
    print("[DEBUG] Play/Pause ->", is_paused)

def next_step(event):
    global is_paused
    is_paused = True
    print("[DEBUG] NEXT step")
    plt.gcf().canvas.start_event_loop(0.01)


# ===============================
# 4. Main visualiser
# ===============================

def run_visualiser():
    global is_paused

    # FIRST: buffer all events
    events = run_c_and_collect_events()

    # internal geometry state
    vertices = []
    active = []
    diagonals = []
    removed = set()
    last_diag = None
    poly_ready = False

    # setup window
    fig, ax = plt.subplots()

    # buttons
    play_ax = plt.axes([0.8, 0.01, 0.1, 0.05])
    next_ax = plt.axes([0.68, 0.01, 0.1, 0.05])

    play_btn = Button(play_ax, "Play/Pause")
    next_btn = Button(next_ax, "Next")

    play_btn.on_clicked(toggle_pause)
    next_btn.on_clicked(next_step)

    ax.set_aspect("equal", "box")
    ax.set_title("Triangulation Visualiser")

    # FRAME COUNTER
    frame_idx = 0

    # ======================
    # EVENT PROCESSOR
    # ======================
    def process_event(event):
        nonlocal vertices, active, diagonals, removed, last_diag, poly_ready

        parts = event.split()

        if parts[0] == "VERTEX":
            idx = int(parts[1])
            x = float(parts[2])
            y = float(parts[3])
            while idx >= len(vertices):
                vertices.append(None)
            vertices[idx] = (x, y)
            active.append(idx)

        elif parts[0] == "POLY_READY":
            poly_ready = True

        elif parts[0] == "DIAGONAL":
            i = int(parts[1])
            j = int(parts[2])
            diagonals.append((i, j))
            last_diag = (i, j)

        elif parts[0] == "REMOVE":
            r = int(parts[1])
            removed.add(r)
            active = [v for v in active if v != r]

        elif parts[0] == "DONE":
            print("[DEBUG] C FINISHED")

    # ======================
    # ANIMATION UPDATE
    # ======================
    def update(frame):
        nonlocal frame_idx

        if is_paused:
            return

        if frame_idx >= len(events):
            return  # no more events

        # process ONE event per frame
        process_event(events[frame_idx])
        frame_idx += 1

        # redraw
        ax.clear()
        ax.grid(True)
        ax.set_title("Triangulation Visualiser")

        # polygon
        if poly_ready and len(active) >= 2:
            xs = [vertices[i][0] for i in active]
            ys = [vertices[i][1] for i in active]
            xs.append(xs[0])
            ys.append(ys[0])
            ax.plot(xs, ys, "k-o")

        # diagonals
        for (i, j) in diagonals:
            x1, y1 = vertices[i]
            x2, y2 = vertices[j]
            if (i, j) == last_diag:
                ax.plot([x1, x2], [y1, y2], "r-", linewidth=3)
            else:
                ax.plot([x1, x2], [y1, y2], "orange", linestyle="--")

        # removed vertices
        for r in removed:
            if vertices[r] is not None:
                ax.plot(vertices[r][0], vertices[r][1], "x", color="gray")

    # start animation
    ani = animation.FuncAnimation(fig, update, interval=600)
    plt.show()


run_visualiser()

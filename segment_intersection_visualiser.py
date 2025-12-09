#!/usr/bin/env python3
import matplotlib
matplotlib.use("TkAgg")

import matplotlib.pyplot as plt
import subprocess
import threading
import queue
import matplotlib.animation as animation

C_BINARY = "./segment_intersection"
INPUT_FILE = "points.txt"


def enqueue_output(pipe, q):
    for raw in iter(pipe.readline, b''):
        if not raw:
            break
        q.put(raw.decode("utf-8").strip())
    pipe.close()


def run_visualiser():
    pts = {}
    segs = []
    intersect_flag = "UNKNOWN"

    # launch C program
    proc = subprocess.Popen(
        [C_BINARY],
        stdin=open(INPUT_FILE, "r"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        shell=False
    )

    q = queue.Queue()
    t = threading.Thread(target=enqueue_output, args=(proc.stdout, q))
    t.daemon = True
    t.start()

    fig, ax = plt.subplots()
    ax.set_aspect("equal", "box")
    ax.set_title("Segment Intersection Visualiser")
    ax.grid(True)

    # --- UPDATE FRAME ---
    def update(_):
        nonlocal intersect_flag

        updated = False
        while True:
            try:
                line = q.get_nowait()
            except queue.Empty:
                break

            updated = True
            parts = line.split()
            if not parts:
                continue

            if parts[0] == "POINT":
                name = parts[1]
                x = float(parts[2])
                y = float(parts[3])
                pts[name] = (x, y)

            elif parts[0] == "SEGMENT":
                segs.append((parts[1], parts[2], parts[3]))

            elif parts[0] == "INTERSECT":
                intersect_flag = parts[1]

            elif parts[0] == "DONE":
                pass

        if not updated:
            return

        ax.clear()
        ax.grid(True)
        ax.set_title("Segment Intersection Visualiser")

        # draw segments
        for _, p1, p2 in segs:
            x1, y1 = pts[p1]
            x2, y2 = pts[p2]
            ax.plot([x1, x2], [y1, y2], 'k-', lw=2)

        # draw points
        for name, (x, y) in pts.items():
            ax.plot(x, y, 'ro')
            ax.text(x, y, f" {name}", fontsize=12)

        # intersection result
        color = "green" if intersect_flag == "TRUE" else "red"
        ax.text(0.02, 0.98,
                f"INTERSECT: {intersect_flag}",
                transform=ax.transAxes,
                va="top",
                color=color,
                fontsize=14)

        fig.canvas.draw_idle()

    # IMPORTANT: store animation object
    global ani
    ani = animation.FuncAnimation(fig, update, interval=200, cache_frame_data=False)

    plt.show()


run_visualiser()

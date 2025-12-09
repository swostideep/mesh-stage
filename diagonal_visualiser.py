#!/usr/bin/env python3
import matplotlib
matplotlib.use("TkAgg")

import matplotlib.pyplot as plt
import subprocess
import threading
import queue
import matplotlib.animation as animation

C_BINARY = "./diagonal_test"
INPUT_FILE = "diagonal.txt"


def enqueue_output(pipe, q):
    for raw in iter(pipe.readline, b''):
        if not raw:
            break
        q.put(raw.decode("utf-8").strip())
    pipe.close()


def run_visualiser():
    pts = {}
    check = None
    result = "UNKNOWN"

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
    ax.grid(True)
    ax.set_title("Diagonal Detection Visualiser")

    def update(_):
        nonlocal check, result

        updated = False
        while True:
            try:
                line = q.get_nowait()
            except queue.Empty:
                break

            updated = True
            parts = line.split()

            if parts[0] == "VERTEX":
                idx = int(parts[1])
                x = float(parts[2])
                y = float(parts[3])
                pts[idx] = (x, y)

            elif parts[0] == "CHECK":
                check = (int(parts[1]), int(parts[2]))

            elif parts[0] == "DIAGONAL":
                result = parts[1]

        if not updated:
            return

        ax.clear()
        ax.grid(True)
        ax.set_title("Diagonal Detection Visualiser")

        # draw polygon edges
        if len(pts) >= 3:
            xs = [pts[i][0] for i in sorted(pts.keys())]
            ys = [pts[i][1] for i in sorted(pts.keys())]
            xs.append(xs[0])
            ys.append(ys[0])
            ax.plot(xs, ys, "k-o")

        # draw diagonal
        if check:
            i, j = check
            xi, yi = pts[i]
            xj, yj = pts[j]

            color = "green" if result == "TRUE" else "red"
            ax.plot([xi, xj], [yi, yj], color=color, linewidth=3)

        fig.canvas.draw_idle()

    global ani
    ani = animation.FuncAnimation(fig, update, interval=200, cache_frame_data=False)

    plt.show()


run_visualiser()

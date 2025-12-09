#!/usr/bin/env python3
import matplotlib
matplotlib.use("TkAgg")

import matplotlib.pyplot as plt
import subprocess
import threading
import queue
import matplotlib.animation as animation

C_BINARY = "./incone_test"
INPUT_FILE = "incone.txt"


def enqueue_output(pipe, q):
    for raw in iter(pipe.readline, b""):
        if not raw:
            break
        q.put(raw.decode("utf-8").strip())
    pipe.close()


def run_visualiser():
    pts = {}
    check = None
    result = "UNKNOWN"

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
    ax.grid(True)
    ax.set_title("InCone Visualiser")

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
                pts[idx] = (float(parts[2]), float(parts[3]))

            elif parts[0] == "CHECK":
                check = (int(parts[1]), int(parts[2]))

            elif parts[0] == "INCONE":
                result = parts[1]

        if not updated:
            return

        ax.clear()
        ax.grid(True)
        ax.set_title("InCone Visualiser")

        # draw polygon
        if pts:
            order = sorted(pts.keys())
            xs = [pts[i][0] for i in order] + [pts[order[0]][0]]
            ys = [pts[i][1] for i in order] + [pts[order[0]][1]]
            ax.plot(xs, ys, "k-o")

        # draw cone and test diag
        if check and pts:
            a, b = check
            n = len(pts)

            a_prev = (a - 1 + n) % n
            a_next = (a + 1) % n

            ax.text(0.03, 0.97, f"INCONE = {result}",
                    transform=ax.transAxes,
                    fontsize=14,
                    color="green" if result == "TRUE" else "red",
                    va="top")

            # cone boundaries
            ax.plot([pts[a][0], pts[a_prev][0]], [pts[a][1], pts[a_prev][1]],
                    'cyan', linestyle='--')
            ax.plot([pts[a][0], pts[a_next][0]], [pts[a][1], pts[a_next][1]],
                    'cyan', linestyle='--')

            # test line
            color = "green" if result == "TRUE" else "red"
            ax.plot([pts[a][0], pts[b][0]], [pts[a][1], pts[b][1]], color=color, linewidth=3)

            # labels
            for k,v in pts.items():
                ax.text(v[0], v[1], f" {k}", fontsize=12)

        fig.canvas.draw_idle()

    # IMPORTANT: store animation object
    global ani
    ani = animation.FuncAnimation(fig, update, interval=250, cache_frame_data=False)

    plt.show()


if __name__ == "__main__":
    run_visualiser()

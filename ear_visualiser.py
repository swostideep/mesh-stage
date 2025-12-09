#!/usr/bin/env python3
import matplotlib
matplotlib.use("TkAgg")

import matplotlib.pyplot as plt
import subprocess, threading, queue
import matplotlib.animation as animation

C_BINARY = "./ear_test"
INPUT_FILE = "ear.txt"

def enqueue_output(pipe, q):
    for raw in iter(pipe.readline, b""):
        if not raw:
            break
        q.put(raw.decode("utf-8").strip())
    pipe.close()

def run_visualiser():
    pts = {}
    ears = {}   # i : (TRUE/FALSE, v0, v2)

    proc = subprocess.Popen(
        [C_BINARY],
        stdin=open(INPUT_FILE,"r"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        shell=False
    )

    q = queue.Queue()
    t = threading.Thread(target=enqueue_output,args=(proc.stdout,q))
    t.daemon=True
    t.start()

    fig,ax = plt.subplots()
    ax.set_aspect("equal","box")
    ax.grid(True)
    ax.set_title("Ear Detection Visualiser")

    def update(_):
        updated=False
        while True:
            try: line=q.get_nowait()
            except queue.Empty: break
            updated=True

            p=line.split()

            if p[0]=="VERTEX":
                pts[int(p[1])] = (float(p[2]),float(p[3]))

            elif p[0]=="EAR":
                i=int(p[1])
                isEar=p[2]
                v0=int(p[3])
                v2=int(p[4])
                ears[i]=(isEar,v0,v2)

        if not updated: return

        ax.clear()
        ax.grid(True)
        ax.set_title("Ear Detection Visualiser")

        if pts:
            order=sorted(pts.keys())
            xs=[pts[i][0] for i in order]+[pts[order[0]][0]]
            ys=[pts[i][1] for i in order]+[pts[order[0]][1]]
            ax.plot(xs,ys,"k-o")

        for i,(flag,v0,v2) in ears.items():
            x,y=pts[i]

            # label point
            ax.text(x,y,f" {i}",fontsize=12)

            if flag=="TRUE":
                ax.plot(x,y,"go",ms=10)
                # ear diagonal between v0 and v2
                xv=[pts[v0][0],pts[v2][0]]
                yv=[pts[v0][1],pts[v2][1]]
                ax.plot(xv,yv,"green",linestyle="--")
            else:
                ax.plot(x,y,"ro",ms=10)

        fig.canvas.draw_idle()

    global ani
    ani = animation.FuncAnimation(fig,update,interval=250,cache_frame_data=False)
    plt.show()

if __name__=="__main__":
    run_visualiser()

#!/usr/bin/env python3
import matplotlib
matplotlib.use("TkAgg")

import matplotlib.pyplot as plt
import subprocess, threading, queue
import matplotlib.animation as animation

C_BINARY = "./triangulation_steps"
INPUT_FILE = "triangulation.txt"

def enqueue_output(pipe, q):
    for raw in iter(pipe.readline, b""):
        if not raw: break
        q.put(raw.decode('utf-8').strip())
    pipe.close()

def run_visualiser():
    pts = {}
    diagonals = []
    removed = set()
    ears = {}

    proc = subprocess.Popen(
        [C_BINARY],
        stdin=open(INPUT_FILE,'r'),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        shell=False
    )

    q = queue.Queue()
    t = threading.Thread(target=enqueue_output,args=(proc.stdout,q))
    t.daemon=True
    t.start()

    fig,ax=plt.subplots()
    ax.set_aspect("equal","box")
    ax.grid(True)
    ax.set_title("Full Triangulation Visualiser")

    def update(_):
        updated=False

        while True:
            try: line=q.get_nowait()
            except queue.Empty: break
            updated=True

            parts=line.split()

            if parts[0]=="VERTEX":
                idx=int(parts[1])
                pts[idx]=(float(parts[2]),float(parts[3]))

            elif parts[0]=="EAR":
                i=int(parts[1])
                flag=parts[2]
                v0=int(parts[3])
                v2=int(parts[4])
                ears[i]=(flag,v0,v2)

            elif parts[0]=="DIAGONAL":
                i=int(parts[1]); j=int(parts[2])
                diagonals.append((i,j))

            elif parts[0]=="REMOVE":
                removed.add(int(parts[1]))

        if not updated: return

        ax.clear()
        ax.grid(True)
        ax.set_title("Full Triangulation Visualiser")

        if pts:
            active=[i for i in pts if i not in removed]
            active_sorted=sorted(active)
            xs=[pts[i][0] for i in active_sorted]+[pts[active_sorted[0]][0]]
            ys=[pts[i][1] for i in active_sorted]+[pts[active_sorted[0]][1]]
            ax.plot(xs,ys,"k-o")

        for (i,j) in diagonals:
            x1,y1=pts[i]; x2,y2=pts[j]
            ax.plot([x1,x2],[y1,y2],'green',linestyle='--',linewidth=2)

        for i,(flag,v0,v2) in ears.items():
            if i in removed: continue
            x,y=pts[i]
            color="green" if flag=="TRUE" else "red"
            ax.plot(x,y,"o",color=color)
            ax.text(x,y,f" {i}")

        for r in removed:
            x,y=pts[r]
            ax.plot(x,y,'x',color='gray')

        fig.canvas.draw_idle()

    global ani
    ani=animation.FuncAnimation(fig,update,interval=250,cache_frame_data=False)
    plt.show()

if __name__=="__main__":
    run_visualiser()

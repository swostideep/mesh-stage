---
title: Surface Mesher API
emoji: ⚙️
colorFrom: gray
colorTo: blue
sdk: docker
pinned: false
---

# SM Surface Mesher | High-Performance CAD Discretization Engine

[![Live App](https://img.shields.io/badge/Live-Vercel-000000?style=for-the-badge&logo=vercel)](https://mesh-stage.vercel.app)
[![API Status](https://img.shields.io/badge/API-Hugging%20Face-ffd21e?style=for-the-badge&logo=huggingface)](https://huggingface.co/spaces/swostideep/mesh-stage-API)
[![Engine](https://img.shields.io/badge/Engine-C%2B%2B%20%2F%20OpenCASCADE-00599C?style=for-the-badge&logo=c%2B%2B)](https://dev.opencascade.org/)

**SM Surface Mesher** is a distributed, cloud-native engineering tool designed for high-fidelity surface triangulation of complex CAD models. By integrating the **OpenCASCADE (OCCT)** geometric kernel with a custom **C++ Voronoi-based engine**, the platform delivers simulation-ready meshes for FEA/CFD applications.

**Live Demo:** [https://mesh-stage.vercel.app](https://mesh-stage.vercel.app)

---

## 🏗 System Architecture

The application is built on a **Decoupled Monorepo** architecture to handle intensive computational geometry without compromising user experience:

1.  **Frontend (Vercel):** A responsive Three.js/WebGL interface providing real-time 3D visualization and diagnostic reporting.
2.  **Backend (Hugging Face Spaces):** A Node.js/Express API managing user authentication (JWT) and a Redis-backed job queue.
3.  **Compute Engine (Docker):** A high-performance C++ binary optimized with `-O3`, `-march=native`, and **TCMalloc** for ultra-fast memory allocation.



---

## 🚀 Technical Highlights
* **Asynchronous Processing:** Uses **BullMQ (Redis)** to queue CAD files, allowing the engine to utilize up to 16-vCPU parallel processing.
* **Intelligent Topology Healing:** Employs OCCT algorithms to repair manifold errors and stitch faces before discretization.
* **Smart Laplacian Smoothing:** Iterative coordinate refinement to minimize element skewness and improve mesh quality.
* **Real-time SSE Logging:** Streams engine output (Phase 1-4) directly to the client via Server-Sent Events.

---

## 💻 Local Development Setup

To run this project locally, you will need to compile the C++ core and link the Node.js environment.

### 1. Prerequisites
* **Node.js** (v18+) & **Redis Server**
* **C++ Build Tools** (GCC/Clang & CMake)
* **OpenCASCADE (OCCT) Libraries:**
    * *macOS:* `brew install opencascade`
    * *Linux:* `sudo apt install libocct-dev`

### 2. Compile the C++ Engine Core
```bash
# 1. Navigate to engine and build
cd sm_engine
mkdir build && cd build
cmake ..
make -j$(nproc)

# 2. Deploy binary to backend folder
mkdir -p ../../sm_backend/engine
cp voronoi_mesh ../../sm_backend/engine/

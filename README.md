---
title: Surface Mesher API
emoji: ⚙️
colorFrom: gray
colorTo: blue
sdk: docker
app_port: 7860
pinned: false
---

# SM Surface Mesher

A surface meshing engine for STEP, IGES and BREP models. It reads a CAD file
through OpenCASCADE, builds a constrained Delaunay triangulation of each face on
an exact integer lattice, refines it against local surface curvature, and welds
the faces into a single watertight mesh with a quality report.

- **App:** https://mesh-stage.vercel.app
- **API:** https://huggingface.co/spaces/swostideep/mesh-stage-API

---

## Measured results

A 31-face solid (filleted block, three through bores, spherical boss) on 8
threads. `Free edges` counts mesh edges used by a single triangle, so zero means
the shell is closed.

| Density | Nodes  | Elements | Free edges | Non-manifold | Mean skew | Max skew | Wall time |
|--------:|-------:|---------:|-----------:|-------------:|----------:|---------:|----------:|
| 0.20    |  2,033 |    4,070 |          0 |            0 |      0.29 |     0.90 |     55 ms |
| 0.12    |  4,413 |    8,830 |          0 |            0 |      0.29 |     0.89 |     27 ms |
| 0.08    | 10,175 |   20,354 |          0 |            0 |      0.27 |     0.92 |     30 ms |
| 0.05    | 20,014 |   40,032 |          0 |            0 |      0.26 |     0.95 |     39 ms |
| 0.03    | 40,713 |   81,430 |          0 |            0 |      0.25 |     0.95 |     59 ms |
| 0.02    | 70,643 |  141,290 |          0 |            0 |      0.24 |     0.96 |     92 ms |

Core triangulator in isolation, meshing a 24-tooth gear profile with a bore:

| Target edge | Elements | Time    | Elements/s |
|------------:|---------:|--------:|-----------:|
|        4.00 |    7,899 |   27 ms |    297,649 |
|        2.00 |   32,048 |   85 ms |    377,123 |
|        1.00 |  128,669 |  451 ms |    285,039 |
|        0.35 |  794,618 | 7,513 ms|    105,763 |

Reproduce both with `sm_bench` and `sm_tests`; see *Building* below.

### Against the previous core

The original triangulator, timed on the same machine with the same polygon
input, sustained about **2,700 elements/second** and could not exceed roughly
2,500 elements because refinement was capped at 1,500 Steiner points. It also
returned an empty mesh for a 16-gon without reporting an error. The current core
sustains **285,000–410,000 elements/second** and has no comparable ceiling.

The gap is algorithmic, not micro-optimisation:

| Operation             | Before                                        | After |
|-----------------------|-----------------------------------------------|-------|
| Point location        | Scan every triangle, including dead ones       | Visibility walk from the last insertion, Hilbert-ordered input |
| Cavity construction   | Scan all triangles, then O(k²) edge dedup      | Depth-first from the containing triangle, proportional to the cavity |
| Constraint insertion  | Split segments until the edge happens to appear| Retriangulate the crossed strip once |
| Adjacency             | Rebuilt from a `std::map` every refinement step| Maintained incrementally as half-edge indices |
| Edge flipping         | Restart the scan and rebuild after every flip  | Flip stack, only affected edges revisited |
| Delaunay audit        | O(triangles × vertices) in-circle tests        | O(triangles), one test per shared edge |
| Dead triangles        | Never reclaimed                                | Free list |

---

## Supported formats

| Format | Extension | Treatment |
|--------|-----------|-----------|
| STEP   | `.step` `.stp` | Meshed. Curvature-adaptive, density applies. |
| IGES   | `.iges` `.igs` | Meshed. Curvature-adaptive, density applies. |
| BREP   | `.brep` `.brp` | Meshed. OpenCASCADE's native serialisation. |
| STL    | `.stl`         | View only. Binary and ASCII. |
| OBJ    | `.obj`         | View only. |

The split is not arbitrary. STEP, IGES and BREP carry boundary representation —
trimmed surfaces with topology — which is the input the mesher actually
consumes, so it can sample them at whatever density is asked for.

STL and OBJ are already triangulated. The surface the mesher would sample no
longer exists, so there is nothing to refine and the density setting has no
effect on them. They are still worth accepting: they get welded (STL repeats
every shared vertex once per touching triangle, so without welding the mesh has
no connectivity at all) and then run through the same audit, which answers the
question people usually have about a downloaded STL — is it watertight, is it
manifold, how bad are the elements. Nothing is silently rewritten; the geometry
that comes out is the geometry that went in.

### What is not supported, and why

**SLDPRT, Parasolid (`.x_t`), ACIS (`.sat`)** — closed formats. Reading them
needs a commercial kernel (HOOPS Exchange, CAD Exchanger, Datakit); there is no
free or legal path, and the licences generally forbid redistribution in a hosted
application. This is a licensing wall, not a missing feature. Every one of these
CAD systems exports STEP natively, which is the intended route.

**`.blend`** — Blender is not a B-rep modeller. A `.blend` holds polygons, not
trimmed NURBS with topology, so there is nothing for this engine to mesh.
Exporting OBJ or STL from Blender and using the view-only path is the equivalent
operation, and it already works.

## How it works

**1. Import and heal.** `ShapeUpgrade_UnifySameDomain`, `ShapeFix_Shape` and
`BRepBuilderAPI_Sewing` normalise the incoming shape so later stages see one
consistent shell.

**2. Size the edges once.** Every model edge is sampled a single time against a
chordal sag tolerance, then densified so no boundary segment exceeds the target
size. Because both faces sharing an edge read the same sample list, their
boundaries match node for node.

**3. Triangulate each face.** Faces are independent, so they are handed to an
OpenMP pool largest-first with dynamic scheduling — face cost varies by orders
of magnitude, and a static split leaves cores waiting on one straggler. Each
face is meshed in a parameter space rescaled by the local surface stretch, so
element quality is optimised for the 3D triangle rather than for the
parametrisation's distortion of it.

**4. Weld and audit.** Face meshes merge through a spatial hash, then the result
is checked for free edges, non-manifold edges, inverted elements and skewness
before anything is written.

### Inside the triangulator

- **Exact predicates.** Orientation and in-circle tests run in `__int128` on a
  2²² integer lattice. Magnitudes are bounded so no input can overflow them,
  which means the predicates return the true sign rather than a rounded one.
- **Insertion order.** Points are inserted in biased randomised rounds, Hilbert
  sorted within each round. Consecutive insertions land near each other, so the
  location walk terminates in a few steps.
- **Constraints.** Segments are inserted by retriangulating the strip of
  triangles they cross (Anglada), not by subdividing until an edge appears.
- **Refinement.** Ruppert refinement driven by a priority queue, worst element
  first. Encroachment is answered from a uniform grid over segment diametral
  circles, so the query is O(1) rather than a scan of every segment.
- **Domain classification.** Inside and outside are separated by a crossing
  parity flood: each constrained edge crossed toggles the state. A fill that
  merely stops at the boundary cannot reach an interior hole, and would leave
  bores filled with elements.

---

## Building

The core library has no OpenCASCADE dependency, so it builds and its tests run
with nothing but a compiler and CMake. The CAD front end is added only if OCCT
is found.

```bash
# macOS
brew install cmake libomp opencascade

# Debian / Ubuntu
sudo apt install build-essential cmake libocct-data-exchange-dev \
                 libocct-foundation-dev libocct-modeling-algorithms-dev \
                 libocct-modeling-data-dev libocct-ocaf-dev
```

```bash
cmake -S sm_engine -B sm_engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build sm_engine/build -j

./sm_engine/build/sm_tests      # 65 invariant checks
./sm_engine/build/sm_bench      # throughput sweep

# Mesh a model directly
./sm_engine/build/voronoi_mesh part.step 0.05 out.obj
./sm_engine/build/voronoi_mesh scan.stl  0.05 out.obj   # view-only passthrough
```

For a B-rep input, `voronoi_mesh` writes two files: the mesh, and
`out_geometry.obj` — a reference tessellation of the healed CAD shape that the
viewer uses for its geometry/mesh toggle. A view-only input produces just the
mesh, and the viewer's Geometry button stays disabled because there is no
separate source geometry to show.

Exit status is `0` on success, `1` when the file cannot be read or yields no
elements, and `2` for a bad invocation or an unsupported extension.

### Running the API locally

With no configuration the server uses an in-memory store and an in-process
queue, so a fresh clone runs end to end without MongoDB or Redis:

```bash
cd sm_backend
npm install
mkdir -p engine && cp ../sm_engine/build/voronoi_mesh engine/
npm start                       # http://localhost:7860/health
```

Set `MONGODB_URI` and `REDIS_URL` to switch to persistent storage and a durable
queue; see `sm_backend/.env.example`. Both are required when `NODE_ENV=production`.

Serve the frontend from `sm_frontend/` on any static server. When loaded from
localhost it targets `http://localhost:7860` automatically; otherwise set
`window.SM_API_BASE`.

---

## Repository layout

```
sm_engine/
  include/sm/       predicates, geometry, half-edge mesh, public API
  src/              triangulator, refinement, smoothing and flips
  cad/              OpenCASCADE import, face meshing, welding, OBJ output
  tests/            invariant suite
  bench/            throughput benchmark
sm_backend/
  src/config.js     one place for every tunable
  src/db/           MongoDB store and in-memory fallback
  src/routes/       auth and job endpoints
  src/services/     engine process, queue, progress streams, storage
sm_frontend/
  index.html        landing page
  auth.html         sign in and registration
  dashboard.html    3D meshing workspace
  js/api.js         API base URL and fetch helpers
```

---

## Resource behaviour

The API targets a container with two vCPUs and an ephemeral disk.

- One job at a time, two OpenMP threads. More of either oversubscribes the
  container and slows both jobs without finishing either sooner.
- Job submission returns `202` with a job id immediately. Progress arrives on a
  per-job event stream. Holding the HTTP request open until the engine finished
  meant any long mesh died against the platform's proxy timeout even though the
  job itself completed.
- Uploads are capped, sniffed for a real signature of the format they claim to
  be, and swept after a retention window so a long-running container cannot fill
  its own disk.
- The runtime image contains the engine binary, its shared-library closure and
  production Node modules — not the compilers and OCCT headers used to build it.

---

## Known limitations

- **Maximum skewness sits around 0.95** on elements adjacent to face boundaries.
  Boundaries are frozen during refinement, which is what guarantees neighbouring
  faces weld watertight; the cost is that the worst boundary-adjacent elements
  cannot be improved by flipping. Mean skewness is 0.24. Closing this gap needs
  boundary-layer smoothing that moves nodes along the CAD edge.
- **Surface meshing only.** There is no volume mesher; output is a triangulated
  shell for surface FEA, visualisation or as input to a tetrahedral mesher.
- **Single-container queue.** Horizontal scaling would need the rate-limit
  counters and progress streams moved into Redis.
- Assemblies are meshed as one shell; per-body separation is not exposed.

---

## Licence

MIT.

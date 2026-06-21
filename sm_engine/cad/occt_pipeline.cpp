#include "occt_pipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unordered_map>

#include "sm/mesher.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Surface.hxx>
#include <IGESControl_Reader.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt2d.hxx>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace sm {
namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// Tolerance-bucketed node welder.
//
// The previous implementation used std::map with a comparator that compared
// coordinates against a tolerance. Such a comparator is not a strict weak
// ordering (a==b and b==c does not imply a==c), which is undefined behaviour
// for the ordered containers and produced both missed and spurious welds.
// Bucketing into a hash grid and scanning the 27 neighbouring cells gives the
// intended tolerance semantics with well-defined behaviour and O(1) lookup.
class NodeWelder {
public:
    explicit NodeWelder(double tolerance) : tol_(tolerance), inv_(1.0 / tolerance) {}

    int32_t insert(const Vec3& p, std::vector<Vec3>& nodes, int* welds) {
        const int64_t cx = int64_t(std::floor(p.x * inv_));
        const int64_t cy = int64_t(std::floor(p.y * inv_));
        const int64_t cz = int64_t(std::floor(p.z * inv_));

        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    auto it = cells_.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == cells_.end()) continue;
                    for (int32_t idx : it->second) {
                        if (distance(nodes[size_t(idx)], p) <= tol_) {
                            ++*welds;
                            return idx;
                        }
                    }
                }
            }
        }

        const int32_t idx = int32_t(nodes.size());
        nodes.push_back(p);
        cells_[key(cx, cy, cz)].push_back(idx);
        return idx;
    }

private:
    static int64_t key(int64_t x, int64_t y, int64_t z) {
        uint64_t h = uint64_t(x) * 0x9E3779B97F4A7C15ull;
        h ^= uint64_t(y) * 0xC2B2AE3D27D4EB4Full;
        h ^= uint64_t(z) * 0x165667B19E3779F9ull;
        return int64_t(h);
    }

    double tol_;
    double inv_;
    std::unordered_map<int64_t, std::vector<int32_t>> cells_;
};

struct FaceWork {
    TopoDS_Face face;
    std::vector<Vec2> uv;                        // boundary points, metric-normalised
    std::vector<std::array<int32_t, 2>> segments;
    double uMin = 0, uMax = 1, vMin = 0, vMax = 1;
    double su = 1.0, sv = 1.0;                   // metric scale factors
    bool valid = false;
};

struct FaceResult {
    std::vector<Vec3> nodes;
    std::vector<std::array<int32_t, 3>> triangles;
    bool ok = false;
};

// Absolute sizing limits derived from the model's own dimensions, so the same
// density setting behaves the same way on a 10 mm bracket and a 4 m airframe
// panel. The user-facing number is a fraction of the bounding-box diagonal,
// never a raw millimetre value.
struct Sizing {
    double sag = 0.01;      // permitted chord height between mesh and surface
    double maxEdge = 1.0;
    double minEdge = 0.05;
};

// Chord height h of a circular arc of radius R spanned by an edge of length L
// is h = R - sqrt(R^2 - L^2/4), which inverts to L = 2*sqrt(2*R*h) for small
// h. Driving element size from a sag tolerance is what makes the mesh follow
// curvature instead of following an arbitrary constant.
double edgeLengthForCurvature(double curvature, const Sizing& s) {
    if (!(curvature > 1e-12)) return s.maxEdge;
    const double radius = 1.0 / curvature;
    return std::clamp(2.0 * std::sqrt(2.0 * radius * s.sag), s.minEdge, s.maxEdge);
}

int resolveThreadCount(int requested) {
#ifdef _OPENMP
    int hw = omp_get_max_threads();
#else
    int hw = 1;
#endif
    if (requested > 0) return std::max(1, requested);
    if (const char* env = std::getenv("SM_THREADS")) {
        const int v = std::atoi(env);
        if (v > 0) return v;
    }
    return std::max(1, hw);
}

// Average surface stretch at a parameter point, used both to normalise UV
// space and to convert a target 3D edge length into parameter units.
void surfaceScale(const Handle(Geom_Surface) & surf, double u, double v, double* su,
                  double* sv) {
    gp_Pnt p;
    gp_Vec d1u, d1v;
    surf->D1(u, v, p, d1u, d1v);
    *su = d1u.Magnitude();
    *sv = d1v.Magnitude();
    if (!(*su > 1e-12)) *su = 1.0;
    if (!(*sv > 1e-12)) *sv = 1.0;
}

double curvatureSize(const Handle(Geom_Surface) & surf, double u, double v,
                     const Sizing& sizing) {
    GeomLProp_SLProps props(surf, u, v, 2, 1e-6);
    if (!props.IsCurvatureDefined()) return sizing.maxEdge;
    const double k = std::max(std::fabs(props.MaxCurvature()), std::fabs(props.MinCurvature()));
    return edgeLengthForCurvature(k, sizing);
}

FaceResult meshFace(const FaceWork& work, const CadOptions& opts, const Sizing& sizing) {
    FaceResult result;
    if (!work.valid || work.uv.size() < 3) return result;

    // Each thread evaluates its own copy of the surface. OpenCASCADE caches
    // evaluation state inside the surface object, so sharing one handle
    // across threads is a data race even though nothing is logically mutated.
    Handle(Geom_Surface) surf =
        Handle(Geom_Surface)::DownCast(BRep_Tool::Surface(work.face)->Copy());
    if (surf.IsNull()) return result;

    MeshRequest request;
    request.points = work.uv;
    request.segments = work.segments;
    request.options.targetEdgeLength = sizing.maxEdge;
    // Per-face budget, not the whole-model budget: one pathological face must
    // not be able to exhaust the memory allowance for every other face.
    request.options.maxSteinerPoints = std::clamp(opts.maxElements / 16, 5000, 400000);
    request.options.maxRadiusEdgeRatio = 1.42;
    request.options.smoothingPasses = 4;

    // Face boundaries are already discretised globally. Freezing them here is
    // what makes neighbouring faces weld into a watertight shell instead of
    // meeting along two independently subdivided, mismatched polylines.
    request.options.allowSegmentSplitting = false;

    const double su = work.su, sv = work.sv;
    request.sizeField = [&](double nu, double nv) -> double {
        const double u = std::clamp(nu / su, work.uMin, work.uMax);
        const double v = std::clamp(nv / sv, work.vMin, work.vMax);
        return curvatureSize(surf, u, v, sizing);
    };

    const MeshResult mesh = generateMesh(request);
    if (mesh.triangles.empty()) return result;

    result.nodes.reserve(mesh.points.size());
    for (const Vec2& p : mesh.points) {
        const double u = std::clamp(p.x / su, work.uMin, work.uMax);
        const double v = std::clamp(p.y / sv, work.vMin, work.vMax);
        const gp_Pnt g = surf->Value(u, v);
        result.nodes.push_back({g.X(), g.Y(), g.Z()});
    }
    result.triangles = mesh.triangles;
    result.ok = true;
    return result;
}

bool readShape(const std::string& path, TopoDS_Shape* shape) {
    const size_t dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });

    if (ext == "step" || ext == "stp") {
        STEPControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return false;
        reader.TransferRoots();
        *shape = reader.OneShape();
    } else if (ext == "iges" || ext == "igs") {
        IGESControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return false;
        reader.TransferRoots();
        *shape = reader.OneShape();
    } else {
        return false;
    }
    return !shape->IsNull();
}

Vec3 triangleNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    Vec3 n{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
    const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 1e-30) {
        n.x /= len;
        n.y /= len;
        n.z /= len;
    }
    return n;
}

// Quality-driven edge flips on the assembled 3D mesh.
//
// The per-face pass optimises angles in parameter space. On a sphere or a cone
// the parametrisation stretches by a factor that varies across the face, so a
// triangle that looks well shaped in UV can still be a sliver once projected.
// This pass measures the element that actually ships. Flips are confined to
// pairs inside one CAD face whose normals nearly agree, so the operation
// rearranges connectivity without moving the surface.
int optimiseSurfaceMesh(SurfaceMesh* mesh, int passes) {
    int totalFlips = 0;

    for (int pass = 0; pass < passes; ++pass) {
        std::unordered_map<int64_t, std::array<int32_t, 2>> edgeTris;
        edgeTris.reserve(mesh->triangles.size() * 3);

        for (int32_t t = 0; t < int32_t(mesh->triangles.size()); ++t) {
            const auto& tri = mesh->triangles[size_t(t)];
            for (int i = 0; i < 3; ++i) {
                int32_t a = tri[size_t(i)], b = tri[size_t((i + 1) % 3)];
                if (a > b) std::swap(a, b);
                const int64_t key = (int64_t(a) << 32) | uint32_t(b);
                auto it = edgeTris.find(key);
                if (it == edgeTris.end()) {
                    edgeTris.emplace(key, std::array<int32_t, 2>{t, -1});
                } else if (it->second[1] == -1) {
                    it->second[1] = t;
                } else {
                    it->second[0] = -1;  // non-manifold: leave it alone
                }
            }
        }

        int flips = 0;
        std::vector<uint8_t> touched(mesh->triangles.size(), 0);

        for (const auto& kv : edgeTris) {
            const int32_t t0 = kv.second[0], t1 = kv.second[1];
            if (t0 < 0 || t1 < 0) continue;
            if (touched[size_t(t0)] || touched[size_t(t1)]) continue;
            if (mesh->faceOfTriangle[size_t(t0)] != mesh->faceOfTriangle[size_t(t1)]) continue;

            const int32_t ea = int32_t(kv.first >> 32);
            const int32_t eb = int32_t(kv.first & 0xffffffff);

            auto opposite = [&](int32_t t) {
                for (int i = 0; i < 3; ++i) {
                    const int32_t v = mesh->triangles[size_t(t)][size_t(i)];
                    if (v != ea && v != eb) return v;
                }
                return int32_t(-1);
            };
            const int32_t c = opposite(t0), d = opposite(t1);
            if (c < 0 || d < 0 || c == d) continue;

            const Vec3& pa = mesh->nodes[size_t(ea)];
            const Vec3& pb = mesh->nodes[size_t(eb)];
            const Vec3& pc = mesh->nodes[size_t(c)];
            const Vec3& pd = mesh->nodes[size_t(d)];

            const Vec3 n0 = triangleNormal(mesh->nodes[size_t(mesh->triangles[size_t(t0)][0])],
                                           mesh->nodes[size_t(mesh->triangles[size_t(t0)][1])],
                                           mesh->nodes[size_t(mesh->triangles[size_t(t0)][2])]);
            const Vec3 n1 = triangleNormal(mesh->nodes[size_t(mesh->triangles[size_t(t1)][0])],
                                           mesh->nodes[size_t(mesh->triangles[size_t(t1)][1])],
                                           mesh->nodes[size_t(mesh->triangles[size_t(t1)][2])]);
            const double align = n0.x * n1.x + n0.y * n1.y + n0.z * n1.z;
            if (align < 0.985) continue;  // keep curvature, only touch flat pairs

            const double before = std::max(skewness3D(pa, pb, pc), skewness3D(pb, pa, pd));
            const double after = std::max(skewness3D(pc, pa, pd), skewness3D(pc, pd, pb));
            if (after >= before - 0.02) continue;

            // The quad runs c -> ea -> d -> eb; splitting it on the c-d
            // diagonal gives these two, and only in this order does the
            // winding still agree with the surface normal.
            const Vec3 m0 = triangleNormal(pc, pa, pd);
            const Vec3 m1 = triangleNormal(pc, pd, pb);
            if (m0.x * n0.x + m0.y * n0.y + m0.z * n0.z < 0.5) continue;
            if (m1.x * n1.x + m1.y * n1.y + m1.z * n1.z < 0.5) continue;

            mesh->triangles[size_t(t0)] = {c, ea, d};
            mesh->triangles[size_t(t1)] = {c, d, eb};
            touched[size_t(t0)] = touched[size_t(t1)] = 1;
            ++flips;
        }

        totalFlips += flips;
        if (flips == 0) break;
    }
    return totalFlips;
}

// Counts how many triangles use each undirected edge. A closed surface has
// every edge shared exactly twice; anything else is reported rather than
// silently shipped.
void checkWatertight(const SurfaceMesh& mesh, CadReport* report) {
    std::unordered_map<int64_t, int> uses;
    uses.reserve(mesh.triangles.size() * 3);
    for (const auto& t : mesh.triangles) {
        for (int i = 0; i < 3; ++i) {
            int32_t a = t[size_t(i)], b = t[size_t((i + 1) % 3)];
            if (a > b) std::swap(a, b);
            ++uses[(int64_t(a) << 32) | uint32_t(b)];
        }
    }
    for (const auto& kv : uses) {
        if (kv.second == 1) ++report->freeEdges;
        if (kv.second > 2) ++report->nonManifoldEdges;
    }
}

}  // namespace

bool runCadPipeline(const std::string& inputPath, const CadOptions& options,
                    SurfaceMesh* out, SurfaceMesh* preview, CadReport* report) {
    const int threads = resolveThreadCount(options.threads);
#ifdef _OPENMP
    omp_set_num_threads(threads);
#endif

    auto t0 = Clock::now();
    TopoDS_Shape raw;
    if (!readShape(inputPath, &raw)) return false;
    report->loadMs = msSince(t0);

    t0 = Clock::now();
    TopoDS_Shape shape = raw;
    if (options.healTopology) {
        ShapeUpgrade_UnifySameDomain unifier(shape, true, true, true);
        unifier.Build();
        shape = unifier.Shape();

        Handle(ShapeFix_Shape) fix = new ShapeFix_Shape(shape);
        fix->SetPrecision(1e-4);
        fix->SetMinTolerance(1e-6);
        fix->SetMaxTolerance(1e-2);
        fix->Perform();
        shape = fix->Shape();

        BRepBuilderAPI_Sewing sewer(1e-3);
        sewer.Add(shape);
        sewer.Perform();
        shape = sewer.SewedShape();
        BRepLib::BuildCurves3d(shape);
    }
    report->healMs = msSince(t0);

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        faces.push_back(TopoDS::Face(ex.Current()));
    }
    report->faces = int(faces.size());
    report->edges = edgeMap.Extent();
    if (faces.empty()) return false;

    // Absolute sizing derived from the model itself. The density argument is a
    // fraction of the bounding-box diagonal, which keeps the same slider
    // position meaningful across parts of wildly different physical size.
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double bx1, by1, bz1, bx2, by2, bz2;
    bounds.Get(bx1, by1, bz1, bx2, by2, bz2);
    const double diagonal =
        std::sqrt((bx2 - bx1) * (bx2 - bx1) + (by2 - by1) * (by2 - by1) +
                  (bz2 - bz1) * (bz2 - bz1));

    Sizing sizing;
    const double density = std::clamp(options.deflection, 0.005, 0.5);
    sizing.maxEdge = std::max(density * diagonal * 0.5, 1e-9);
    sizing.sag = std::max(density * diagonal * 0.004, 1e-12);
    sizing.minEdge = std::max(sizing.maxEdge * options.minSizeFactor, 1e-9);

    // Edges are discretised once, globally, so two faces sharing an edge
    // receive identical seed points and weld without a crack.
    t0 = Clock::now();
    std::vector<std::vector<double>> edgeSeeds(size_t(edgeMap.Extent()) + 1);
    const int edgeCount = edgeMap.Extent();
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (int i = 1; i <= edgeCount; ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(edgeMap(i));
        if (BRep_Tool::Degenerated(edge)) continue;
        BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection sampler(curve, sizing.sag);
        if (!sampler.IsDone()) continue;

        std::vector<double> params;
        params.reserve(size_t(sampler.NbPoints()));
        for (int j = 1; j <= sampler.NbPoints(); ++j) params.push_back(sampler.Parameter(j));
        if (params.size() < 2) continue;

        // Deflection sampling alone leaves a straight edge as a single long
        // chord. Densify so no boundary segment exceeds the target size,
        // otherwise the face interior refines while its border stays coarse.
        std::vector<double>& seeds = edgeSeeds[size_t(i)];
        seeds.push_back(params.front());
        for (size_t j = 1; j < params.size(); ++j) {
            const double a = params[j - 1], b = params[j];
            const double chord = curve.Value(a).Distance(curve.Value(b));
            const int splits = int(std::ceil(chord / sizing.maxEdge));
            for (int k = 1; k <= std::max(1, splits); ++k) {
                seeds.push_back(a + (b - a) * double(k) / double(std::max(1, splits)));
            }
        }
    }
    report->discretiseMs = msSince(t0);

    // Boundary extraction stays serial: BRep_Tool::CurveOnSurface reads
    // topology shared between neighbouring faces and populates lazy caches
    // inside it. It is cheap next to the meshing that follows.
    t0 = Clock::now();
    std::vector<FaceWork> work(faces.size());
    for (size_t f = 0; f < faces.size(); ++f) {
        FaceWork& w = work[f];
        w.face = faces[f];
        BRepTools::UVBounds(w.face, w.uMin, w.uMax, w.vMin, w.vMax);
        const double uRange = std::max(w.uMax - w.uMin, 1e-9);
        const double vRange = std::max(w.vMax - w.vMin, 1e-9);

        Handle(Geom_Surface) surf = BRep_Tool::Surface(w.face);
        if (surf.IsNull()) continue;

        // Rescaling parameter space by the local surface stretch means the
        // triangulator optimises angles that correspond to the real 3D
        // element, not to the parametrisation's distortion of it.
        surfaceScale(surf, w.uMin + uRange * 0.5, w.vMin + vRange * 0.5, &w.su, &w.sv);

        std::unordered_map<int64_t, int32_t> dedup;
        const double weldTol = std::max(sizing.minEdge * 0.05, 1e-12);
        auto addPoint = [&](double u, double v) -> int32_t {
            const double nu = u * w.su, nv = v * w.sv;
            const int64_t kx = int64_t(std::llround(nu / weldTol));
            const int64_t ky = int64_t(std::llround(nv / weldTol));
            const int64_t key = (kx << 32) ^ ky;
            auto it = dedup.find(key);
            if (it != dedup.end()) return it->second;
            const int32_t idx = int32_t(w.uv.size());
            w.uv.push_back({nu, nv});
            dedup.emplace(key, idx);
            return idx;
        };

        for (TopExp_Explorer ex(w.face, TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(ex.Current());
            Standard_Real first = 0, last = 0;
            Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, w.face, first, last);
            if (pcurve.IsNull()) continue;

            std::vector<double> params;
            if (BRep_Tool::Degenerated(edge)) {
                // A degenerate edge collapses to a point in 3D but is still a
                // real line in parameter space: the v = +/-pi/2 rows of a
                // sphere, for instance. Skipping it leaves the UV domain open
                // along that side, so the triangulator meshes to the convex
                // hull instead of to the face, and the pole ends up with a
                // hole in it. Sample the 2D curve directly.
                const gp_Pnt2d a = pcurve->Value(first);
                const gp_Pnt2d b = pcurve->Value(last);
                const double span = std::hypot((b.X() - a.X()) * w.su, (b.Y() - a.Y()) * w.sv);
                const int steps =
                    std::clamp(int(std::ceil(span / std::max(sizing.maxEdge, 1e-12))), 1, 512);
                for (int k = 0; k <= steps; ++k) {
                    params.push_back(first + (last - first) * double(k) / double(steps));
                }
            } else {
                const int id = edgeMap.FindIndex(edge);
                if (id <= 0 || id >= int(edgeSeeds.size())) continue;
                const std::vector<double>& seeds = edgeSeeds[size_t(id)];
                if (seeds.size() < 2) continue;
                params.assign(seeds.begin(), seeds.end());
            }
            if (params.size() < 2) continue;

            const bool reversed = (edge.Orientation() == TopAbs_REVERSED);
            int32_t prev = -1;
            for (size_t k = 0; k < params.size(); ++k) {
                double t = reversed ? params[params.size() - 1 - k] : params[k];
                t = std::clamp(t, first, last);
                const gp_Pnt2d p = pcurve->Value(t);
                const int32_t cur = addPoint(p.X(), p.Y());
                if (prev >= 0 && prev != cur) w.segments.push_back({prev, cur});
                prev = cur;
            }
        }
        w.valid = w.uv.size() >= 3 && !w.segments.empty();
    }

    // Largest faces first with dynamic hand-out. Face cost varies by orders of
    // magnitude between a small fillet and a large freeform patch, so a static
    // split leaves cores idle waiting on one straggler.
    std::vector<int> order(faces.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return work[size_t(a)].uv.size() > work[size_t(b)].uv.size();
    });
    report->assembleMs = msSince(t0);

    t0 = Clock::now();
    std::vector<FaceResult> results(faces.size());
    const int total = int(order.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 1)
#endif
    for (int k = 0; k < total; ++k) {
        const int f = order[size_t(k)];
        results[size_t(f)] = meshFace(work[size_t(f)], options, sizing);
    }
    report->meshMs = msSince(t0);

    // Weld face meshes into one indexed surface. Serial by design: it is a
    // few percent of the runtime and keeps node numbering deterministic,
    // which matters when a result has to be reproduced.
    // Loose enough to close the gap left by two faces evaluating the same
    // shared edge through different parametrisations, tight enough that it
    // can never merge two nodes of the same element.
    const double weldTol = std::max(sizing.minEdge * 0.25, 1e-12);
    NodeWelder welder(weldTol);
    out->nodes.clear();
    out->triangles.clear();
    out->faceOfTriangle.clear();

    for (size_t f = 0; f < results.size(); ++f) {
        const FaceResult& r = results[f];
        if (!r.ok) {
            ++report->failedFaces;
            continue;
        }
        std::vector<int32_t> remap(r.nodes.size());
        for (size_t i = 0; i < r.nodes.size(); ++i) {
            remap[i] = welder.insert(r.nodes[i], out->nodes, &report->duplicateNodesWelded);
        }
        for (const auto& t : r.triangles) {
            const int32_t a = remap[size_t(t[0])], b = remap[size_t(t[1])], c = remap[size_t(t[2])];
            if (a == b || b == c || a == c) continue;  // collapsed by welding

            // Welding can also leave a triangle with three distinct but
            // near-collinear nodes. Those carry no area and would dominate
            // the skewness report, so they are dropped here.
            const Vec3& pa = out->nodes[size_t(a)];
            const Vec3& pb = out->nodes[size_t(b)];
            const Vec3& pc = out->nodes[size_t(c)];
            const double ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z;
            const double vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
            const double nx = uy * vz - uz * vy;
            const double ny = uz * vx - ux * vz;
            const double nz = ux * vy - uy * vx;
            const double area = 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);
            if (area < weldTol * weldTol) {
                ++report->degenerateDropped;
                continue;
            }
            out->triangles.push_back({a, b, c});
            out->faceOfTriangle.push_back(int32_t(f) + 1);
        }
    }
    out->faceCount = int32_t(faces.size());

    report->surfaceFlips = optimiseSurfaceMesh(out, 6);

    report->nodes = int(out->nodes.size());
    report->elements = int(out->triangles.size());

    double skewSum = 0.0;
    for (const auto& t : out->triangles) {
        const double s = skewness3D(out->nodes[size_t(t[0])], out->nodes[size_t(t[1])],
                                    out->nodes[size_t(t[2])]);
        skewSum += s;
        report->maxSkewness = std::max(report->maxSkewness, s);
        if (s > 0.5) ++report->highSkewCount;
    }
    report->meanSkewness = out->triangles.empty() ? 0.0 : skewSum / double(out->triangles.size());
    checkWatertight(*out, report);

    // Reference tessellation of the healed solid, used by the viewer to show
    // the incoming geometry alongside the generated mesh.
    if (options.writePreview && preview != nullptr) {
        BRepMesh_IncrementalMesh mesher(shape, sizing.sag, Standard_False, 0.5,
                                        Standard_True);
        NodeWelder pw(weldTol);
        int ignored = 0;
        int32_t faceIndex = 0;
        for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
            ++faceIndex;
            TopLoc_Location loc;
            const TopoDS_Face face = TopoDS::Face(ex.Current());
            Handle(Poly_Triangulation) poly = BRep_Tool::Triangulation(face, loc);
            if (poly.IsNull()) continue;
            const gp_Trsf& trsf = loc.Transformation();
            std::vector<int32_t> remap(size_t(poly->NbNodes()) + 1, -1);
            for (int i = 1; i <= poly->NbNodes(); ++i) {
                gp_Pnt p = poly->Node(i).Transformed(trsf);
                remap[size_t(i)] = pw.insert({p.X(), p.Y(), p.Z()}, preview->nodes, &ignored);
            }
            const bool flip = (face.Orientation() == TopAbs_REVERSED);
            for (int i = 1; i <= poly->NbTriangles(); ++i) {
                int n1 = 0, n2 = 0, n3 = 0;
                poly->Triangle(i).Get(n1, n2, n3);
                if (flip) std::swap(n2, n3);
                preview->triangles.push_back(
                    {remap[size_t(n1)], remap[size_t(n2)], remap[size_t(n3)]});
                preview->faceOfTriangle.push_back(faceIndex);
            }
        }
        preview->faceCount = faceIndex;
    }

    return !out->triangles.empty();
}

bool writeObj(const std::string& path, const SurfaceMesh& mesh, bool withColour) {
    std::ofstream out(path);
    if (!out) return false;

    std::vector<double> vertexSkew(mesh.nodes.size(), 0.0);
    std::vector<int> vertexCount(mesh.nodes.size(), 0);
    if (withColour) {
        for (const auto& t : mesh.triangles) {
            const double s = skewness3D(mesh.nodes[size_t(t[0])], mesh.nodes[size_t(t[1])],
                                        mesh.nodes[size_t(t[2])]);
            for (int i = 0; i < 3; ++i) {
                vertexSkew[size_t(t[size_t(i)])] += s;
                vertexCount[size_t(t[size_t(i)])]++;
            }
        }
    }

    // Buffered assembly: writing millions of vertices one operator<< at a
    // time dominates the runtime otherwise.
    std::ostringstream buffer;
    buffer.setf(std::ios::fixed);
    buffer.precision(6);

    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const Vec3& p = mesh.nodes[i];
        buffer << "v " << p.x << ' ' << p.y << ' ' << p.z;
        if (withColour) {
            const double s = vertexCount[i] > 0 ? vertexSkew[i] / vertexCount[i] : 0.0;
            double r, g, b;
            if (s < 0.25) {
                r = 0.0; g = s * 4.0; b = 1.0;
            } else if (s < 0.5) {
                r = 0.0; g = 1.0; b = 1.0 - (s - 0.25) * 4.0;
            } else if (s < 0.75) {
                r = (s - 0.5) * 4.0; g = 1.0; b = 0.0;
            } else {
                r = 1.0; g = std::max(0.0, 1.0 - (s - 0.75) * 4.0); b = 0.0;
            }
            buffer << ' ' << r << ' ' << g << ' ' << b;
        }
        buffer << '\n';
    }

    int32_t currentGroup = -1;
    for (size_t i = 0; i < mesh.triangles.size(); ++i) {
        const int32_t group =
            i < mesh.faceOfTriangle.size() ? mesh.faceOfTriangle[i] : 1;
        if (group != currentGroup) {
            buffer << "g Surface_" << group << '\n';
            currentGroup = group;
        }
        const auto& t = mesh.triangles[i];
        buffer << "f " << (t[0] + 1) << ' ' << (t[1] + 1) << ' ' << (t[2] + 1) << '\n';
    }

    out << buffer.str();
    return bool(out);
}

}  // namespace sm

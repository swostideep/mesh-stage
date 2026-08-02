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
#include <BRep_Builder.hxx>
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

// Tolerance-bucketed node welder. A tolerance-based comparator is not a strict
// weak ordering, so std::map cannot be used here; a hash grid scanned over the
// 27 neighbouring cells gives the same semantics with O(1) lookup.
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

// Sizing limits derived from the model's own dimensions, so one density
// setting behaves the same on a 10 mm bracket and a 4 m panel.
struct Sizing {
    double sag = 0.01;      // permitted chord height between mesh and surface
    double maxEdge = 1.0;
    double minEdge = 0.05;
};

// Chord height h of an arc of radius R over an edge of length L inverts to
// L = 2*sqrt(2*R*h), which is what ties element size to local curvature.
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

    // OpenCASCADE caches evaluation state inside the surface object, so each
    // thread needs its own copy or the shared handle becomes a data race.
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

    // Boundaries are discretised globally; freezing them here is what lets
    // neighbouring faces weld watertight instead of along mismatched polylines.
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

bool readShape(const std::string& path, SourceKind kind, TopoDS_Shape* shape) {
    if (kind == SourceKind::Step) {
        STEPControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return false;
        reader.TransferRoots();
        *shape = reader.OneShape();
    } else if (kind == SourceKind::Iges) {
        IGESControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return false;
        reader.TransferRoots();
        *shape = reader.OneShape();
    } else if (kind == SourceKind::Brep) {
        // Native serialisation: exact topology already, no translation step.
        BRep_Builder builder;
        if (!BRepTools::Read(*shape, path.c_str(), builder)) return false;
    } else {
        return false;
    }
    return !shape->IsNull();
}

// --- Already-triangulated input -------------------------------------------

// Triangles as read, before welding: three positions each with no shared
// indexing, which is how STL stores them and where OBJ is normalised to.
struct RawMesh {
    std::vector<Vec3> verts;
    std::vector<int32_t> groups;
    int32_t groupCount = 0;
};

bool readStlBinary(const std::string& path, RawMesh* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 84) return false;

    in.seekg(80, std::ios::beg);
    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), 4);
    if (!in) return false;

    // The only reliable way to tell binary from ASCII: a binary file is
    // exactly 84 + 50*count bytes. Plenty of exporters write "solid" into the
    // binary header, so sniffing the first word gets it wrong.
    if (std::streamoff(84) + std::streamoff(count) * 50 != size) return false;

    out->verts.reserve(size_t(count) * 3);
    out->groups.assign(size_t(count), 1);
    out->groupCount = count > 0 ? 1 : 0;

    for (uint32_t i = 0; i < count; ++i) {
        float buf[12];
        in.read(reinterpret_cast<char*>(buf), sizeof(buf));
        if (!in) return false;
        in.seekg(2, std::ios::cur);  // attribute byte count, unused
        for (int v = 0; v < 3; ++v) {
            out->verts.push_back({double(buf[3 + v * 3]), double(buf[4 + v * 3]),
                                  double(buf[5 + v * 3])});
        }
    }
    return !out->verts.empty();
}

bool readStlAscii(const std::string& path, RawMesh* out) {
    std::ifstream in(path);
    if (!in) return false;

    std::string token;
    std::vector<Vec3> pending;
    while (in >> token) {
        if (token != "vertex") continue;
        double x = 0, y = 0, z = 0;
        if (!(in >> x >> y >> z)) break;
        pending.push_back({x, y, z});
        if (pending.size() == 3) {
            out->verts.insert(out->verts.end(), pending.begin(), pending.end());
            out->groups.push_back(1);
            pending.clear();
        }
    }
    out->groupCount = out->groups.empty() ? 0 : 1;
    return !out->verts.empty();
}

bool readObjMesh(const std::string& path, RawMesh* out) {
    std::ifstream in(path);
    if (!in) return false;

    std::vector<Vec3> positions;
    int32_t group = 1;
    int32_t maxGroup = 1;
    std::string line;

    // "f" indices may be v, v/vt, v//vn or v/vt/vn, and may be negative to
    // count back from the most recently declared vertex.
    auto parseIndex = [&](const std::string& field) -> int32_t {
        const size_t slash = field.find('/');
        const std::string head = (slash == std::string::npos) ? field : field.substr(0, slash);
        if (head.empty()) return 0;
        long idx = 0;
        try {
            idx = std::stol(head);
        } catch (...) {
            return 0;
        }
        if (idx > 0) return int32_t(idx - 1);
        if (idx < 0) return int32_t(long(positions.size()) + idx);
        return -1;
    };

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;

        if (tag == "v") {
            double x = 0, y = 0, z = 0;
            ls >> x >> y >> z;
            positions.push_back({x, y, z});
        } else if (tag == "g" || tag == "o") {
            // Preserve authored grouping so the model tree stays meaningful.
            group = ++maxGroup;
        } else if (tag == "f") {
            std::vector<int32_t> face;
            std::string field;
            while (ls >> field) {
                const int32_t idx = parseIndex(field);
                if (idx >= 0 && idx < int32_t(positions.size())) face.push_back(idx);
            }
            // Fan-triangulate anything with more than three corners.
            for (size_t k = 2; k < face.size(); ++k) {
                out->verts.push_back(positions[size_t(face[0])]);
                out->verts.push_back(positions[size_t(face[k - 1])]);
                out->verts.push_back(positions[size_t(face[k])]);
                out->groups.push_back(group);
            }
        }
    }

    out->groupCount = maxGroup;
    return !out->verts.empty();
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

// Quality-driven edge flips on the assembled 3D mesh. The per-face pass works
// in parameter space, where a stretched parametrisation can hide a sliver that
// only shows up once projected. Confined to near-coplanar pairs inside a single
// face, so connectivity changes but the surface does not move.
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

// A closed surface shares every edge exactly twice; anything else is reported
// rather than silently shipped.
void checkWatertight(const SurfaceMesh& mesh, CadReport* report);

// Shared by both paths, so an imported STL is measured the same way as a mesh
// the engine generated itself.
void auditSurfaceMesh(const SurfaceMesh& mesh, CadReport* report) {
    report->nodes = int(mesh.nodes.size());
    report->elements = int(mesh.triangles.size());

    double skewSum = 0.0;
    report->maxSkewness = 0.0;
    report->highSkewCount = 0;
    for (const auto& t : mesh.triangles) {
        const double s = skewness3D(mesh.nodes[size_t(t[0])], mesh.nodes[size_t(t[1])],
                                    mesh.nodes[size_t(t[2])]);
        skewSum += s;
        report->maxSkewness = std::max(report->maxSkewness, s);
        if (s > 0.5) ++report->highSkewCount;
    }
    report->meanSkewness =
        mesh.triangles.empty() ? 0.0 : skewSum / double(mesh.triangles.size());
    checkWatertight(mesh, report);
}

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

SourceKind detectSourceKind(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });

    if (ext == "step" || ext == "stp") return SourceKind::Step;
    if (ext == "iges" || ext == "igs") return SourceKind::Iges;
    if (ext == "brep" || ext == "brp") return SourceKind::Brep;
    if (ext == "stl") return SourceKind::Stl;
    if (ext == "obj") return SourceKind::Obj;
    return SourceKind::Unknown;
}

const char* sourceKindName(SourceKind kind) {
    switch (kind) {
        case SourceKind::Step: return "STEP";
        case SourceKind::Iges: return "IGES";
        case SourceKind::Brep: return "BREP";
        case SourceKind::Stl:  return "STL";
        case SourceKind::Obj:  return "OBJ";
        default:               return "unknown";
    }
}

bool runMeshImport(const std::string& inputPath, const CadOptions& options,
                   SurfaceMesh* out, CadReport* report) {
    const SourceKind kind = detectSourceKind(inputPath);
    report->source = kind;
    report->passthrough = true;

    auto t0 = Clock::now();
    RawMesh raw;
    if (kind == SourceKind::Stl) {
        // Binary is checked first because its size formula is decisive; the
        // ASCII parser would happily consume a binary file's stray text.
        if (!readStlBinary(inputPath, &raw)) {
            raw = RawMesh{};
            if (!readStlAscii(inputPath, &raw)) return false;
        }
    } else if (kind == SourceKind::Obj) {
        if (!readObjMesh(inputPath, &raw)) return false;
    } else {
        return false;
    }
    report->loadMs = msSince(t0);
    if (raw.verts.size() < 3) return false;

    t0 = Clock::now();

    // STL repeats every shared vertex once per touching triangle, so without
    // welding the mesh has no connectivity and every edge reads as free.
    Vec3 lo = raw.verts[0], hi = raw.verts[0];
    for (const Vec3& p : raw.verts) {
        lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
        lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
        lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
    }
    const double diagonal = distance(lo, hi);
    const double weldTol = std::max(diagonal * 1e-6, 1e-12);

    NodeWelder welder(weldTol);
    out->nodes.clear();
    out->triangles.clear();
    out->faceOfTriangle.clear();
    out->nodes.reserve(raw.verts.size() / 3);

    const size_t triCount = raw.verts.size() / 3;
    for (size_t i = 0; i < triCount; ++i) {
        const int32_t a = welder.insert(raw.verts[i * 3 + 0], out->nodes, &report->duplicateNodesWelded);
        const int32_t b = welder.insert(raw.verts[i * 3 + 1], out->nodes, &report->duplicateNodesWelded);
        const int32_t c = welder.insert(raw.verts[i * 3 + 2], out->nodes, &report->duplicateNodesWelded);
        if (a == b || b == c || a == c) {
            ++report->degenerateDropped;
            continue;
        }
        const Vec3& pa = out->nodes[size_t(a)];
        const Vec3& pb = out->nodes[size_t(b)];
        const Vec3& pc = out->nodes[size_t(c)];
        const double ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z;
        const double vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        if (0.5 * std::sqrt(nx * nx + ny * ny + nz * nz) < weldTol * weldTol) {
            ++report->degenerateDropped;
            continue;
        }
        out->triangles.push_back({a, b, c});
        out->faceOfTriangle.push_back(i < raw.groups.size() ? raw.groups[i] : 1);
    }
    out->faceCount = raw.groupCount;
    report->faces = raw.groupCount;
    report->assembleMs = msSince(t0);

    // No refinement, smoothing or flipping: the element sizes came from the
    // exporter and rewriting them would misrepresent the file being inspected.
    auditSurfaceMesh(*out, report);
    return !out->triangles.empty();
}

bool runCadPipeline(const std::string& inputPath, const CadOptions& options,
                    SurfaceMesh* out, SurfaceMesh* preview, CadReport* report) {
    const int threads = resolveThreadCount(options.threads);
#ifdef _OPENMP
    omp_set_num_threads(threads);
#endif

    const SourceKind kind = detectSourceKind(inputPath);
    report->source = kind;
    report->passthrough = false;
    if (!isBoundaryRep(kind)) return false;

    auto t0 = Clock::now();
    TopoDS_Shape raw;
    if (!readShape(inputPath, kind, &raw)) return false;
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

    // Density is a fraction of the bounding-box diagonal, which keeps one
    // slider position meaningful across parts of very different size.
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

        // Deflection sampling leaves a straight edge as one long chord, so
        // densify or the interior refines while the border stays coarse.
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

    // Serial: CurveOnSurface populates lazy caches in topology shared between
    // neighbouring faces. Cheap next to the meshing that follows.
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

        // Rescaling by local surface stretch means angles are optimised for
        // the real 3D element, not the parametrisation's distortion of it.
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
                // Collapses to a point in 3D but is still a real line in
                // parameter space (a sphere's v = +/-pi/2 rows). Skipping it
                // leaves the UV domain open and the pole comes out holed.
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

    // Largest first, handed out dynamically: face cost varies by orders of
    // magnitude and a static split leaves cores waiting on one straggler.
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

    // Serial by design: a few percent of the runtime, and it keeps node
    // numbering deterministic. The tolerance is loose enough to close the gap
    // between two faces evaluating a shared edge through different
    // parametrisations, tight enough never to merge nodes of one element.
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

            // Welding can leave three distinct but near-collinear nodes,
            // which carry no area and would dominate the skewness report.
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

    auditSurfaceMesh(*out, report);

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

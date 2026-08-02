#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sm/geometry.h"

namespace sm {

struct CadOptions {
    double deflection = 0.05;   // chordal tolerance driving edge discretisation
    double growthRate = 1.2;    // permitted size ratio between adjacent elements
    double curvatureAlpha = 5.0;
    double minSizeFactor = 0.15;
    bool healTopology = true;
    bool writePreview = true;
    int threads = 0;            // 0 selects the container-aware default
    int maxElements = 4000000;  // memory guard for constrained hosts
};

struct SurfaceMesh {
    std::vector<Vec3> nodes;
    std::vector<std::array<int32_t, 3>> triangles;
    std::vector<int32_t> faceOfTriangle;
    int32_t faceCount = 0;
};

// Two classes of input reach this engine and they are not interchangeable.
//
// STEP, IGES and BREP carry boundary representation: trimmed surfaces with
// real topology, which is what the mesher actually consumes. Those go through
// the full curvature-adaptive pipeline.
//
// STL and OBJ are already triangulated. There is no surface left to sample, so
// no amount of density setting can refine them — they are welded, audited and
// passed through so the viewer and the quality report still work.
enum class SourceKind { Unknown, Step, Iges, Brep, Stl, Obj };

SourceKind detectSourceKind(const std::string& path);
const char* sourceKindName(SourceKind kind);
inline bool isBoundaryRep(SourceKind kind) {
    return kind == SourceKind::Step || kind == SourceKind::Iges || kind == SourceKind::Brep;
}

struct CadReport {
    SourceKind source = SourceKind::Unknown;
    bool passthrough = false;   // true when the input arrived already meshed
    int faces = 0;
    int edges = 0;
    int failedFaces = 0;
    int nodes = 0;
    int elements = 0;
    int duplicateNodesWelded = 0;
    int degenerateDropped = 0;
    int surfaceFlips = 0;
    int freeEdges = 0;        // boundary edges: non-zero means not watertight
    int nonManifoldEdges = 0;
    double maxSkewness = 0.0;
    double meanSkewness = 0.0;
    double minAngle = 180.0;
    int highSkewCount = 0;
    int invertedCount = 0;
    double loadMs = 0.0;
    double healMs = 0.0;
    double discretiseMs = 0.0;
    double meshMs = 0.0;
    double assembleMs = 0.0;
};

// Reads STEP/IGES/BREP, heals the topology, meshes every face and welds the
// result into a single indexed surface mesh. Returns false when the file
// cannot be read or contains no usable faces.
bool runCadPipeline(const std::string& inputPath, const CadOptions& options,
                    SurfaceMesh* out, SurfaceMesh* preview, CadReport* report);

// Reads an already-triangulated STL or OBJ, welds duplicated vertices and
// audits the result. No refinement happens: the density setting has nothing to
// act on because the surface it would sample is gone.
bool runMeshImport(const std::string& inputPath, const CadOptions& options,
                   SurfaceMesh* out, CadReport* report);

// Writes an OBJ with per-vertex skewness colours and one group per CAD face.
bool writeObj(const std::string& path, const SurfaceMesh& mesh, bool withColour);

}  // namespace sm

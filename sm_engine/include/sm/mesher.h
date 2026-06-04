#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sm/geometry.h"
#include "sm/mesh.h"

namespace sm {

// Target edge length at a point in input space. Returning a smaller value
// refines; returning <= 0 falls back to the uniform target. The CAD front end
// wires this to OpenCASCADE surface curvature.
using SizeField = std::function<double(double, double)>;

struct RefineOptions {
    // Upper bound on circumradius / shortest edge. 1.0 corresponds to a 30
    // degree minimum angle; Ruppert's algorithm is only guaranteed to
    // terminate above sqrt(2), so that is the default.
    double maxRadiusEdgeRatio = 1.42;

    // Uniform target edge length used wherever the size field is silent.
    double targetEdgeLength = 0.0;

    // Hard ceiling on inserted Steiner points, sized for the memory envelope
    // of a free-tier container rather than for an unbounded workstation run.
    int maxSteinerPoints = 200000;

    int smoothingPasses = 4;
    bool topologicalOptimisation = true;

    // Refuse to split a subsegment below this fraction of the domain span.
    // Stops the classic Ruppert cascade on small input angles.
    double minSegmentFraction = 1e-4;

    // When false the input segments are frozen: no Steiner point is ever
    // added to the boundary. Two CAD faces meeting along an edge are meshed
    // independently, so the only way their shared boundary comes out with
    // identical nodes is if neither is allowed to subdivide it. Interior
    // refinement then falls back to centroid insertion near the boundary.
    bool allowSegmentSplitting = true;
};

struct MeshStats {
    int inputPoints = 0;
    int steinerPoints = 0;
    int triangles = 0;
    int edgeFlips = 0;
    int verticesSmoothed = 0;
    double maxSkewness = 0.0;
    double meanSkewness = 0.0;
    double minAngle = 180.0;
    int highSkewCount = 0;
    int invertedCount = 0;
    int delaunayViolations = 0;
    double refineMilliseconds = 0.0;
};

struct MeshRequest {
    std::vector<Vec2> points;
    std::vector<std::array<int32_t, 2>> segments;
    SizeField sizeField;
    RefineOptions options;
};

struct MeshResult {
    std::vector<Vec2> points;
    std::vector<std::array<int32_t, 3>> triangles;
    MeshStats stats;
};

// Builds a constrained Delaunay triangulation of the PSLG, refines it against
// the size field, then smooths and flips to drive skewness down.
MeshResult generateMesh(const MeshRequest& request);

// Diagnostics over a finished mesh. Runs in O(triangles) using the adjacency
// already present, not the O(triangles * vertices) brute force.
MeshStats auditMesh(const TriMesh& mesh);

}  // namespace sm

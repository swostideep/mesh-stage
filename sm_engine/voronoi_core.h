#ifndef VORONOI_CORE_H
#define VORONOI_CORE_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <functional>
#include <unordered_set>
#include <unordered_map>
using namespace std;

// ---------------------------------------------------------------------------
// Point2D — lightweight 2D floating-point coordinate used for circumcenters.
// ---------------------------------------------------------------------------
struct Point2D { double x, y; };

// ---------------------------------------------------------------------------
// EdgeKey — canonical (min, max) vertex-index pair that uniquely identifies
// an undirected edge. Supports both std::map (operator<) and hash containers
// (EdgeKeyHash / EdgeSet) so edge queries can be O(log n) or O(1) as needed.
// ---------------------------------------------------------------------------
struct EdgeKey {
    int v1, v2;
    EdgeKey(int a, int b) : v1(min(a,b)), v2(max(a,b)) {}
    bool operator==(const EdgeKey& o) const { return v1 == o.v1 && v2 == o.v2; }
    bool operator<(const EdgeKey& o)  const {
        if (v1 != o.v1) return v1 < o.v1;
        return v2 < o.v2;
    }
};

// Fowler–Noll–Vo-inspired hash for EdgeKey — enables O(1) edge-set queries.
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& ek) const noexcept {
        size_t a = static_cast<size_t>(ek.v1);
        size_t b = static_cast<size_t>(ek.v2);
        return a ^ (b * 2654435761ULL + 0x9e3779b9ULL + (a << 6) + (a >> 2));
    }
};

// O(1) active-edge presence set. Passed into bowyerWatsonInsert and
// constrainBoundarySegments and maintained incrementally as triangles are
// added or removed, avoiding full-mesh scans for edge-existence queries.
using EdgeSet = unordered_set<EdgeKey, EdgeKeyHash>;

// ---------------------------------------------------------------------------
// Triangle — the fundamental mesh cell.
//   v[3]             — vertex indices into the pointsOut coordinate array
//   adj[3]           — pointers to the triangle sharing edge (v[i], v[i+1%3])
//   active           — false means the triangle has been removed (Bowyer-Watson)
//   isConstrained[3] — true if edge i is a locked CDT boundary segment
//   isExterior       — true if the triangle lies outside the input domain
//                      (connected to the super-triangle via flood fill)
//   skipRefinement   — reserved flag for future selective refinement
// ---------------------------------------------------------------------------
struct Triangle {
    int v[3];
    Triangle* adj[3];
    bool active;
    bool isConstrained[3];
    bool isExterior;
    bool skipRefinement;
    Triangle(int a, int b, int c) {
        v[0] = a; v[1] = b; v[2] = c;
        for (int i = 0; i < 3; i++) { adj[i] = nullptr; isConstrained[i] = false; }
        active = true; isExterior = false; skipRefinement = false;
    }
};

// Directed edge used transiently during cavity polygon construction.
struct Edge {
    int v1, v2;
    bool operator==(const Edge& other) const {
        return (v1 == other.v1 && v2 == other.v2) || (v1 == other.v2 && v2 == other.v1);
    }
};

// ===========================================================================
// EXACT ARITHMETIC PREDICATES
// Both functions use __int128 arithmetic on integer-scaled coordinates to
// avoid all floating-point rounding errors in the core triangulation kernel.
// ===========================================================================

// orient2d — returns +1 if (a,b,c) are counter-clockwise, -1 if clockwise,
//            0 if collinear. Used to test triangle winding and convexity.
long long orient2d(const vector<int>& a, const vector<int>& b, const vector<int>& c);

// incircle — returns true if point d lies strictly inside the circumcircle of
//            the CCW triangle (a,b,c). Core test for the Delaunay property.
bool incircle(const vector<int>& a, const vector<int>& b, const vector<int>& c,
              const vector<int>& d);

// ===========================================================================
// GEOMETRY HELPERS
// ===========================================================================

// computeCircumcenter — returns the circumcenter of triangle (a,b,c) in
//   floating-point. Falls back to the midpoint of the longest edge when the
//   triangle is degenerate (collinear vertices, |D| < 1e-9).
Point2D computeCircumcenter(const vector<int>& a, const vector<int>& b, const vector<int>& c);

// computeTriangleArea — shoelace formula; always returns a non-negative area.
double computeTriangleArea(const vector<int>& a, const vector<int>& b, const vector<int>& c);

// computeCircumradiusEdgeRatio — returns R / L_min where R is the circumradius
//   and L_min is the shortest edge length. This is Ruppert's "B" quality metric:
//   a triangle is "good" when B < 1.5 (min angle >= ~26.6 degrees).
//   Returns 9999 for degenerate (zero-area) triangles.
double computeCircumradiusEdgeRatio(const vector<int>& a, const vector<int>& b, const vector<int>& c);

// computeSpatialTargetArea — fallback sizing field used when no CAD curvature
//   callback is provided. Returns a target triangle area that grows with
//   distance from the mesh centroid (finer near the centre, coarser at edges).
double computeSpatialTargetArea(int x, int y, int midX, int midY, double globalMaxArea);

// computeInteriorAngles — returns the three interior angles of triangle (a,b,c)
//   in degrees using the Law of Cosines. Arguments to acos are clamped to
//   [-1, 1] to prevent NaN from floating-point rounding.
vector<double> computeInteriorAngles(const vector<int>& a, const vector<int>& b, const vector<int>& c);

// computeNESSkewness — computes the ANSYS Normalized Equiangular Skewness (NES).
//   Range: 0.0 (perfect equilateral) to 1.0 (completely degenerate).
//   Formula: max( (maxAngle - 60) / 120,  (60 - minAngle) / 60 )
double computeNESSkewness(const vector<int>& a, const vector<int>& b, const vector<int>& c);

// ===========================================================================
// CORE MESH OPERATIONS
// ===========================================================================

// bowyerWatsonInsert — inserts vertex pIdx into the triangulation using the
//   Bowyer-Watson algorithm. Finds all triangles whose circumcircle contains
//   pIdx, removes them, and re-triangulates the resulting cavity. The EdgeSet
//   is maintained incrementally so callers can do O(1) edge-existence queries.
void bowyerWatsonInsert(int pIdx,
                        const vector<vector<int>>& pointsOut,
                        vector<Triangle>& triangulation,
                        EdgeSet& edgeSet);

// laplacianSmooth — applies up to 3 passes of Laplacian smoothing to all
//   interior Steiner points. Input vertices (indices < numOriginalPoints) and
//   all vertices on constrained edges are locked in place. Each candidate move
//   is validated with orient2d to guarantee no triangle inversion.
void laplacianSmooth(vector<vector<int>>& pointsOut,
                     vector<Triangle>& triangulation,
                     int numOriginalPoints);

// flipEdgesForSkewness — post-processing pass that performs topological edge
//   flips to reduce NES skewness. For each non-constrained interior edge, the
//   quadrilateral formed by the two adjacent triangles is tested: if the
//   proposed diagonal gives a lower maximum skewness (by >= 1%), the flip is
//   applied. Runs until no improving flip exists or the 5000-flip safety cap
//   is reached. Requires up-to-date adjacency (call rebuildAdjacencyAndMarkExterior first).
void flipEdgesForSkewness(vector<vector<int>>& pointsOut,
                          vector<Triangle>& triangulation,
                          int N,
                          const vector<pair<int,int>>& currentSegments);

// buildConstrainedDelaunayMesh — full pipeline entry point.
//   1. Inserts all inPoints via Bowyer-Watson to build the initial Delaunay triangulation.
//   2. Calls constrainBoundarySegments to enforce all CDT constraint edges.
//   3. Runs Ruppert's refinement loop: splits the worst triangle (by circumradius-edge
//      ratio) or its most-encroached constraint segment until all triangles meet
//      MAX_RATIO <= 1.5 and the adaptive area target from sizeFunction.
//   4. Calls laplacianSmooth, then flipEdgesForSkewness as post-processing.
//   If sizeFunction is nullptr, computeSpatialTargetArea is used as fallback.
void buildConstrainedDelaunayMesh(
        const vector<vector<int>>& inPoints,
        const vector<pair<int,int>>& segments,
        vector<Triangle>& triangulation,
        vector<vector<int>>& pointsOut,
        std::function<double(double, double)> sizeFunction = nullptr);

// ===========================================================================
// DIAGNOSTICS & EXPORT
// ===========================================================================

// reportMeshQuality — prints a structured quality report to stdout covering:
//   orientation correctness (orient2d), Delaunay property (neighbour incircle
//   tests only — O(n)), area constraint compliance, circumradius-edge ratio,
//   NES skewness statistics, and minimum interior angle found.
void reportMeshQuality(const vector<vector<int>>& pointsOut,
                       const vector<Triangle>& triangulation,
                       double maxArea);

// exportToOBJ — writes the mesh to a Wavefront OBJ file with per-vertex RGB
//   colour data encoding average NES skewness (blue = good, red = bad).
//   The 2D UV mesh is lifted to a shallow paraboloid (z = dist² * 0.001)
//   for visual inspection in 3D viewers.
void exportToOBJ(const string& filename,
                 const vector<vector<int>>& pointsOut,
                 const vector<Triangle>& triangulation);

#endif // VORONOI_CORE_H

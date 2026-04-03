#include "voronoi_core.h"
#include <map>
#include <iostream>
#include <queue>
#include <fstream>
#include <functional>
#include <limits>

// __int128_t is a GCC/Clang built-in (available on the macOS/Linux target).
// IntelliSense on Windows may flag this; it is intentional and correct.
typedef __int128_t int128;

// Portable M_PI — defined here in case <cmath> doesn't expose it (MSVC strict mode).
#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

long long orient2d(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    int128 val = (int128)(b[0] - a[0]) * (c[1] - a[1]) - (int128)(b[1] - a[1]) * (c[0] - a[0]);
    if (val > 0)
        return 1;
    if (val < 0)
        return -1;
    return 0;
}

bool incircle(const vector<int> &a, const vector<int> &b, const vector<int> &c, const vector<int> &d)
{
    int128 adx = a[0] - d[0], ady = a[1] - d[1], bdx = b[0] - d[0], bdy = b[1] - d[1], cdx = c[0] - d[0], cdy = c[1] - d[1];
    int128 abdet = adx * bdy - bdx * ady, bcdet = bdx * cdy - cdx * bdy, cadet = cdx * ady - adx * cdy;
    int128 alift = adx * adx + ady * ady, blift = bdx * bdx + bdy * bdy, clift = cdx * cdx + cdy * cdy;
    return (alift * bcdet + blift * cadet + clift * abdet) > 0;
}

// computeCircumcenter
// Returns the circumcenter of triangle (a,b,c). When the triangle is degenerate
// (|D| < 1e-9, i.e. nearly collinear vertices), returns the midpoint of the longest
// edge — the standard safe Steiner split point for flat triangles.
Point2D computeCircumcenter(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    double ax = a[0], ay = a[1], bx = b[0], by = b[1], cx = c[0], cy = c[1];
    double D = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(D) < 1e-9) {
        // FIX #2: Degenerate triangle — return midpoint of the longest edge.
        // Inserting a centroid of a flat triangle produces another bad triangle;
        // the longest-edge midpoint is the standard safe split point.
        double L1sq = (ax-bx)*(ax-bx) + (ay-by)*(ay-by); // AB
        double L2sq = (bx-cx)*(bx-cx) + (by-cy)*(by-cy); // BC
        double L3sq = (cx-ax)*(cx-ax) + (cy-ay)*(cy-ay); // CA
        if (L1sq >= L2sq && L1sq >= L3sq) return {(ax+bx)*0.5, (ay+by)*0.5};
        if (L2sq >= L1sq && L2sq >= L3sq) return {(bx+cx)*0.5, (by+cy)*0.5};
        return {(cx+ax)*0.5, (cy+ay)*0.5};
    }
    double ux = ((ax*ax + ay*ay) * (by - cy) + (bx*bx + by*by) * (cy - ay) + (cx*cx + cy*cy) * (ay - by)) / D;
    double uy = ((ax*ax + ay*ay) * (cx - bx) + (bx*bx + by*by) * (ax - cx) + (cx*cx + cy*cy) * (bx - ax)) / D;
    return {ux, uy};
}

// computeTriangleArea
// Shoelace (cross-product) formula. Returns the unsigned area of triangle (a,b,c).
double computeTriangleArea(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    return std::abs((a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1])) / 2.0);
}

// computeCircumradiusEdgeRatio
// Returns Ruppert's quality metric B = R / L_min, where R is the circumradius and
// L_min is the shortest edge of the triangle. A triangle is considered "good" when
// B < 1.5 (equivalent to a minimum interior angle >= ~26.6 degrees).
// Returns 9999 for zero-area (degenerate) triangles.
double computeCircumradiusEdgeRatio(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    double L1 = std::sqrt(std::pow(a[0] - b[0], 2) + std::pow(a[1] - b[1], 2));
    double L2 = std::sqrt(std::pow(b[0] - c[0], 2) + std::pow(b[1] - c[1], 2));
    double L3 = std::sqrt(std::pow(c[0] - a[0], 2) + std::pow(c[1] - a[1], 2));

    double L_min = std::min({L1, L2, L3});
    double area = computeTriangleArea(a, b, c);

    if (area < 1e-5)
        return 9999.0; // Degenerate triangle safety

    // R = (abc) / (4 * area)
    double R = (L1 * L2 * L3) / (4.0 * area);

    return R / L_min;
}
// ================= PHASE 4: FEA QUALITY METRICS =================

// computeInteriorAngles
// Returns the three interior angles of triangle (a,b,c) in degrees using the Law of
// Cosines. All acos arguments are clamped to [-1, 1] to prevent NaN when floating-point
// rounding pushes the ratio marginally out of the valid domain.
vector<double> computeInteriorAngles(const vector<int> &a, const vector<int> &b, const vector<int> &c) {
    double L1 = std::sqrt(std::pow(b[0] - c[0], 2) + std::pow(b[1] - c[1], 2)); // Edge opposite to A
    double L2 = std::sqrt(std::pow(a[0] - c[0], 2) + std::pow(a[1] - c[1], 2)); // Edge opposite to B
    double L3 = std::sqrt(std::pow(a[0] - b[0], 2) + std::pow(a[1] - b[1], 2)); // Edge opposite to C

    // FIX #1: Clamp Law-of-Cosines argument to [-1,1] before acos.
    // Floating-point rounding can push the ratio just outside this range,
    // which would produce NaN and silently corrupt all downstream quality metrics.
    auto safeCos = [](double num, double den) -> double {
        if (den < 1e-12) return 0.0; // degenerate edge — treat as 90 deg
        return std::max(-1.0, std::min(1.0, num / den));
    };
    double angleA = std::acos(safeCos(L2*L2 + L3*L3 - L1*L1, 2.0*L2*L3)) * (180.0 / M_PI);
    double angleB = std::acos(safeCos(L1*L1 + L3*L3 - L2*L2, 2.0*L1*L3)) * (180.0 / M_PI);
    double angleC = 180.0 - angleA - angleB;

    return {angleA, angleB, angleC};
}

// computeNESSkewness
// ANSYS Normalized Equiangular Skewness (NES).
// 0.0 = perfect equilateral triangle. 1.0 = completely degenerate (zero-area).
// Formula: max( (maxAngle - 60) / 120,  (60 - minAngle) / 60 )
double computeNESSkewness(const vector<int> &a, const vector<int> &b, const vector<int> &c) {
    vector<double> angles = computeInteriorAngles(a, b, c);
    double minAngle = std::min({angles[0], angles[1], angles[2]});
    double maxAngle = std::max({angles[0], angles[1], angles[2]});

    double maxSkew = (maxAngle - 60.0) / (180.0 - 60.0);
    double minSkew = (60.0 - minAngle) / 60.0;

    return std::max(maxSkew, minSkew);
}

// bowyerWatsonInsert
// Inserts vertex pIdx into the triangulation using the Bowyer-Watson algorithm:
//   1. Finds all active triangles whose circumcircle strictly contains pIdx.
//   2. Removes them (marks active=false) and builds the star-shaped cavity polygon.
//   3. Re-triangulates the cavity by connecting each boundary edge to pIdx.
// The EdgeSet is kept in sync throughout — no separate rebuild needed after insertion.
void bowyerWatsonInsert(int pIdx, const vector<vector<int>> &pointsOut,
                        vector<Triangle> &triangulation, EdgeSet &edgeSet)
{
    // --- Step 1: Find all triangles whose circumcircle contains pIdx (Bowyer-Watson cavity) ---
    // We still iterate the full vector here, but the critical O(n^2) bottleneck was the
    // polygon-boundary O(n^2) duplicate scan below — that is now O(n) with the hash set.
    // (A full O(n log n) walk-based locator is a larger structural change left for future work.)
    vector<Edge> cavityEdges;
    cavityEdges.reserve(32);

    for (auto &tri : triangulation) {
        if (!tri.active) continue;
        if (incircle(pointsOut[tri.v[0]], pointsOut[tri.v[1]], pointsOut[tri.v[2]], pointsOut[pIdx])) {
            tri.active = false;
            // Remove the three edges of this triangle from the global EdgeSet
            edgeSet.erase(EdgeKey(tri.v[0], tri.v[1]));
            edgeSet.erase(EdgeKey(tri.v[1], tri.v[2]));
            edgeSet.erase(EdgeKey(tri.v[2], tri.v[0]));
            cavityEdges.push_back({tri.v[0], tri.v[1]});
            cavityEdges.push_back({tri.v[1], tri.v[2]});
            cavityEdges.push_back({tri.v[2], tri.v[0]});
        }
    }

    // --- Step 2: Find the boundary of the cavity in O(n) using a hash map ---
    // An edge is on the boundary iff it appears exactly once across all cavity triangles.
    unordered_map<EdgeKey, int, EdgeKeyHash> edgeCount;
    edgeCount.reserve(cavityEdges.size() * 2);
    for (const auto &e : cavityEdges)
        edgeCount[EdgeKey(e.v1, e.v2)]++;

    // --- Step 3: Re-triangulate the cavity and update the EdgeSet ---
    for (const auto &e : cavityEdges) {
        if (edgeCount[EdgeKey(e.v1, e.v2)] == 1) {
            // Boundary edge — form a new triangle with pIdx
            triangulation.emplace_back(e.v1, e.v2, pIdx);
            // Register the three new edges
            edgeSet.insert(EdgeKey(e.v1, e.v2));
            edgeSet.insert(EdgeKey(e.v2, pIdx));
            edgeSet.insert(EdgeKey(pIdx, e.v1));
        }
    }
}

// FIX #8: edgeExists now does an O(1) hash-set lookup instead of scanning all triangles.
static bool edgeExists(int u, int v, const EdgeSet &edgeSet)
{
    return edgeSet.count(EdgeKey(u, v)) > 0;
}

// HELPER: Rebuilds adjacency and domain tags
void updateTopologyAndDomain(int N, const vector<pair<int, int>> &currentSegments, vector<Triangle> &triangulation)
{
    std::map<EdgeKey, std::pair<Triangle *, int>> edgeMap;
    for (auto &tri : triangulation)
    {
        if (!tri.active)
            continue;
        tri.isExterior = false; // Reset
        for (int i = 0; i < 3; i++)
        {
            tri.adj[i] = nullptr;
            tri.isConstrained[i] = false;
            EdgeKey ek(tri.v[i], tri.v[(i + 1) % 3]);
            if (edgeMap.count(ek))
            {
                Triangle *other = edgeMap[ek].first;
                int otherEdgeIdx = edgeMap[ek].second;
                tri.adj[i] = other;
                other->adj[otherEdgeIdx] = &tri;
            }
            else
                edgeMap[ek] = {&tri, i};
        }
    }

    for (auto &tri : triangulation)
    {
        if (!tri.active)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int u = tri.v[i], v = tri.v[(i + 1) % 3];
            for (const auto &seg : currentSegments)
            {
                if ((seg.first == u && seg.second == v) || (seg.first == v && seg.second == u))
                {
                    tri.isConstrained[i] = true;
                    break;
                }
            }
        }
    }

    std::vector<Triangle *> floodQueue;
    for (auto &tri : triangulation)
    {
        if (!tri.active)
            continue;
        if (tri.v[0] == N || tri.v[1] == N || tri.v[2] == N || tri.v[0] == N + 1 || tri.v[1] == N + 1 || tri.v[2] == N + 1 || tri.v[0] == N + 2 || tri.v[1] == N + 2 || tri.v[2] == N + 2)
        {
            tri.isExterior = true;
            floodQueue.push_back(&tri);
        }
    }
    int head = 0;
    while (head < floodQueue.size())
    {
        Triangle *curr = floodQueue[head++];
        for (int i = 0; i < 3; i++)
        {
            if (curr->adj[i] != nullptr && curr->adj[i]->active && !curr->isConstrained[i] && !curr->adj[i]->isExterior)
            {
                curr->adj[i]->isExterior = true;
                floodQueue.push_back(curr->adj[i]);
            }
        }
    }
}

// constrainBoundarySegments
// Ensures every segment in currentSegments exists as an edge in the triangulation
// (the CDT "constrained" property). When a required edge is missing, the segment is
// bisected and both halves re-inserted via bowyerWatsonInsert. Bisection repeats
// until all segments exist or the safety limit (2000 iterations) is reached.
// Segments shorter than 2 grid units are flagged with a warning rather than silently
// skipped — they indicate the UV integer scale (2000) is too coarse for this geometry.
void constrainBoundarySegments(vector<pair<int, int>> &currentSegments,
                               vector<vector<int>> &pointsOut,
                               vector<Triangle> &triangulation, EdgeSet &edgeSet)
{
    bool allSegmentsExist = false;
    int splitSafetyLimit = 2000;
    int shortSegWarnings = 0;

    while (!allSegmentsExist && splitSafetyLimit-- > 0)
    {
        allSegmentsExist = true;
        vector<pair<int, int>> nextSegments;
        for (auto &seg : currentSegments)
        {
            double distSq = std::pow(pointsOut[seg.first][0] - pointsOut[seg.second][0], 2) +
                            std::pow(pointsOut[seg.first][1] - pointsOut[seg.second][1], 2);

            if (!edgeExists(seg.first, seg.second, edgeSet))
            {
                if (distSq <= 4.0) {
                    // FIX #6: Segment is too short to bisect on the integer grid.
                    // Carry it forward as-is and warn rather than silently dropping it.
                    shortSegWarnings++;
                    nextSegments.push_back(seg);
                } else {
                    allSegmentsExist = false;
                    // FIX #4: Use rounding instead of truncation for the midpoint.
                    int mx = (int)std::llround((pointsOut[seg.first][0] + (double)pointsOut[seg.second][0]) * 0.5);
                    int my = (int)std::llround((pointsOut[seg.first][1] + (double)pointsOut[seg.second][1]) * 0.5);

                    pointsOut.push_back({mx, my});
                    int mIdx = (int)pointsOut.size() - 1;
                    bowyerWatsonInsert(mIdx, pointsOut, triangulation, edgeSet);

                    nextSegments.push_back({seg.first, mIdx});
                    nextSegments.push_back({mIdx, seg.second});
                }
            }
            else
            {
                nextSegments.push_back(seg);
            }
        }
        currentSegments = nextSegments;
    }
    if (shortSegWarnings > 0)
        std::cerr << "[WARN] enforceSegments: " << shortSegWarnings
                  << " segment(s) too short to bisect on integer grid (distSq <= 4). "
                     "Consider increasing UV scale above 2000.\n";
}

// laplacianSmooth
// Applies up to 3 passes of uniform Laplacian smoothing to interior Steiner points.
// Locked vertices (never moved):
//   - All original input vertices (indices < numOriginalPoints)
//   - Any vertex that sits on a constrained edge (isConstrained[])
// Each proposed move is validated with orient2d: if any adjacent triangle would
// become inverted or degenerate, the move is rejected for that vertex in that pass.
void laplacianSmooth(vector<vector<int>>& pointsOut, vector<Triangle>& triangulation, int numOriginalPoints) {
    int numPasses = 3; 

    vector<bool> isLocked(pointsOut.size(), false);
    for (int i = 0; i < numOriginalPoints; i++) isLocked[i] = true;

    for (const auto& tri : triangulation) {
        if (!tri.active || tri.isExterior) continue;
        for (int i = 0; i < 3; i++) {
            if (tri.isConstrained[i]) {
                isLocked[tri.v[i]] = true;
                isLocked[tri.v[(i + 1) % 3]] = true;
            }
        }
    }

    for (int pass = 0; pass < numPasses; pass++) {
        vector<double> sumX(pointsOut.size(), 0.0);
        vector<double> sumY(pointsOut.size(), 0.0);
        vector<int> count(pointsOut.size(), 0);

        for (const auto& tri : triangulation) {
            if (!tri.active || tri.isExterior) continue;
            for (int i = 0; i < 3; i++) {
                int u = tri.v[i];
                int v = tri.v[(i + 1) % 3];
                
                sumX[u] += pointsOut[v][0]; sumY[u] += pointsOut[v][1]; count[u]++;
                sumX[v] += pointsOut[u][0]; sumY[v] += pointsOut[u][1]; count[v]++;
            }
        }

        for (size_t i = 0; i < pointsOut.size(); i++) {
            if (!isLocked[i] && count[i] > 0) {
                int newX = sumX[i] / count[i];
                int newY = sumY[i] / count[i];
                
                bool safeToMove = true;
                for (const auto& tri : triangulation) {
                    if (!tri.active || tri.isExterior) continue;
                    if (tri.v[0] == i || tri.v[1] == i || tri.v[2] == i) {
                        vector<int> a = (tri.v[0] == i) ? vector<int>{newX, newY} : pointsOut[tri.v[0]];
                        vector<int> b = (tri.v[1] == i) ? vector<int>{newX, newY} : pointsOut[tri.v[1]];
                        vector<int> c = (tri.v[2] == i) ? vector<int>{newX, newY} : pointsOut[tri.v[2]];
                        
                        if (orient2d(a, b, c) <= 0) {
                            safeToMove = false;
                            break;
                        }
                    }
                }

                if (safeToMove) {
                    pointsOut[i][0] = newX;
                    pointsOut[i][1] = newY;
                }
            }
        }
    }
}


// computeSpatialTargetArea
// Fallback sizing field used when no CAD curvature callback is supplied.
// Returns a target triangle area that interpolates between a tight minimum
// (near the mesh centroid) and globalMaxArea (at distance >= 400 grid units).
// This produces a coarse-to-fine gradient centred on the input geometry.
double computeSpatialTargetArea(int x, int y, int midX, int midY, double globalMaxArea) {
    double distSq = std::pow(x - midX, 2) + std::pow(y - midY, 2);
    double maxDistSq = std::pow(400.0, 2); 
    double normalizedDist = std::min(distSq / maxDistSq, 1.0);
    double minAreaLimit = 200.0; 
    return minAreaLimit + (globalMaxArea - minAreaLimit) * normalizedDist;
}


// buildConstrainedDelaunayMesh
// Full CDT + Ruppert refinement pipeline:
//   Phase 1 — Bowyer-Watson insertion of all inPoints into a super-triangle.
//   Phase 2 — constrainBoundarySegments: bisects missing CDT constraint edges.
//   Phase 3 — Ruppert refinement loop (up to 1500 Steiner points):
//              Priority 1: split worst circumradius-edge ratio triangle (B > 1.5).
//              Priority 2: split triangle whose area exceeds the sizing field target.
//              If the candidate circumcenter encroaches a constraint, split that
//              segment instead (Shewchuk's encroachment rule, strict < test).
//   Phase 4 — rebuildAdjacencyAndMarkExterior, then laplacianSmooth.
//   Phase 5 — rebuildAdjacencyAndMarkExterior, then flipEdgesForSkewness.
void buildConstrainedDelaunayMesh(const vector<vector<int>>& inPoints,
                                  const vector<pair<int, int>>& segments,
                                  vector<Triangle>& triangulation,
                                  vector<vector<int>>& pointsOut,
                                  std::function<double(double, double)> sizeFunction)
{
    triangulation.clear();
    if (inPoints.empty())
        return;
    int N = (int)inPoints.size();
    pointsOut = inPoints;

    int minX = pointsOut[0][0], minY = pointsOut[0][1], maxX = minX, maxY = minY;
    for (const auto &p : inPoints)
    {
        minX = std::min(minX, p[0]);
        maxX = std::max(maxX, p[0]);
        minY = std::min(minY, p[1]);
        maxY = std::max(maxY, p[1]);
    }
    int dx = maxX - minX, dy = maxY - minY;
    int dmax = std::max(dx, dy);
    int midX = (minX + maxX) / 2, midY = (minY + maxY) / 2;

    // Super-triangle vertices
    pointsOut.push_back({midX - 20 * dmax, midY - dmax});
    pointsOut.push_back({midX + 20 * dmax, midY - dmax});
    pointsOut.push_back({midX,              midY + 20 * dmax});
    triangulation.emplace_back(N, N + 1, N + 2);

    // Build initial EdgeSet from the super-triangle
    EdgeSet edgeSet;
    edgeSet.insert(EdgeKey(N,   N+1));
    edgeSet.insert(EdgeKey(N+1, N+2));
    edgeSet.insert(EdgeKey(N+2, N));

    // Insert all input points using Bowyer-Watson with the maintained EdgeSet
    for (int i = 0; i < N; i++)
        bowyerWatsonInsert(i, pointsOut, triangulation, edgeSet);

    vector<pair<int, int>> currentSegments = segments;
    constrainBoundarySegments(currentSegments, pointsOut, triangulation, edgeSet);

    // FIX #3: Use 1.5 instead of 1.414 (sqrt(2)).
    // Ruppert's algorithm only guarantees termination for B >= sqrt(2); using exactly
    // sqrt(2) with floating-point invites oscillation at the boundary. 1.5 guarantees
    // minimum angle >= ~26.6 degrees and clean termination.
    const double MAX_AREA_ALLOWED  = 2000.0;
    const double MAX_RATIO_ALLOWED = 1.5;
    int steinerPointsAdded = 0;
    const int maxSteinerLimit = 1500;

    while (steinerPointsAdded < maxSteinerLimit)
    {
        updateTopologyAndDomain(N, currentSegments, triangulation);

        int    worstTriIdx    = -1;
        bool   splitForAngle  = false;
        double maxAreaViolation = 1.0;
        double maxRatioFound  = MAX_RATIO_ALLOWED;

        for (int i = 0; i < (int)triangulation.size(); i++)
        {
            if (!triangulation[i].active || triangulation[i].isExterior)
                continue;

            const auto &a = pointsOut[triangulation[i].v[0]];
            const auto &b = pointsOut[triangulation[i].v[1]];
            const auto &c = pointsOut[triangulation[i].v[2]];

            double area = computeTriangleArea(a, b, c);
            if (area < 150.0)
                continue;

            double ratio = computeCircumradiusEdgeRatio(a, b, c);

            if (ratio > maxRatioFound)
            {
                maxRatioFound = ratio;
                worstTriIdx   = i;
                splitForAngle = true;
            }
            else if (!splitForAngle)
            {
                int triCenterX = (a[0] + b[0] + c[0]) / 3;
                int triCenterY = (a[1] + b[1] + c[1]) / 3;

                double targetArea = MAX_AREA_ALLOWED;
                if (sizeFunction) {
                    double normU = (double)triCenterX / 2000.0;
                    double normV = (double)triCenterY / 2000.0;
                    targetArea = sizeFunction(normU, normV);
                } else {
                    targetArea = computeSpatialTargetArea(triCenterX, triCenterY, midX, midY, MAX_AREA_ALLOWED);
                }

                if (area > targetArea) {
                    double violation = area / targetArea;
                    if (violation > maxAreaViolation) {
                        maxAreaViolation = violation;
                        worstTriIdx = i;
                    }
                }
            }
        }

        if (worstTriIdx == -1)
            break;

        const auto &a = pointsOut[triangulation[worstTriIdx].v[0]];
        const auto &b = pointsOut[triangulation[worstTriIdx].v[1]];
        const auto &c = pointsOut[triangulation[worstTriIdx].v[2]];
        Point2D cc = computeCircumcenter(a, b, c);

        // Collect ALL encroached segments, then split the worst one (closest midpoint to cc).
        int    encroachedSegIdx  = -1;
        double worstEncroachDist = std::numeric_limits<double>::max();

        for (int j = 0; j < (int)currentSegments.size(); j++) {
            double sx = pointsOut[currentSegments[j].first][0],  sy = pointsOut[currentSegments[j].first][1];
            double ex = pointsOut[currentSegments[j].second][0], ey = pointsOut[currentSegments[j].second][1];
            double mx = (sx + ex) * 0.5, my = (sy + ey) * 0.5;
            double radiusSq = (sx - mx)*(sx - mx) + (sy - my)*(sy - my);
            double distSq   = (cc.x - mx)*(cc.x - mx) + (cc.y - my)*(cc.y - my);
            // FIX #5: strict < (not <=) to avoid splitting when cc is on the circle boundary
            if (distSq < radiusSq && distSq < worstEncroachDist) {
                worstEncroachDist = distSq;
                encroachedSegIdx  = j;
            }
        }

        bool touchesBoundary = triangulation[worstTriIdx].isConstrained[0] ||
                               triangulation[worstTriIdx].isConstrained[1] ||
                               triangulation[worstTriIdx].isConstrained[2];

        if (encroachedSegIdx == -1 && touchesBoundary) {
            cc.x = (a[0] + b[0] + c[0]) / 3.0;
            cc.y = (a[1] + b[1] + c[1]) / 3.0;
        }

        if (encroachedSegIdx != -1) {
            int sIdx = currentSegments[encroachedSegIdx].first;
            int eIdx = currentSegments[encroachedSegIdx].second;

            double segLenSq = std::pow(pointsOut[sIdx][0] - pointsOut[eIdx][0], 2) +
                              std::pow(pointsOut[sIdx][1] - pointsOut[eIdx][1], 2);
            if (segLenSq < 16.0) {
                break; // segment too short to split further — hard stop
            }

            // FIX #4: Round midpoint instead of truncating
            int mx = (int)std::llround((pointsOut[sIdx][0] + (double)pointsOut[eIdx][0]) * 0.5);
            int my = (int)std::llround((pointsOut[sIdx][1] + (double)pointsOut[eIdx][1]) * 0.5);

            pointsOut.push_back({mx, my});
            int mIdx = (int)pointsOut.size() - 1;
            bowyerWatsonInsert(mIdx, pointsOut, triangulation, edgeSet);

            currentSegments.erase(currentSegments.begin() + encroachedSegIdx);
            currentSegments.push_back({sIdx, mIdx});
            currentSegments.push_back({mIdx, eIdx});
        } else {
            int cx = (int)std::llround(cc.x);
            int cy = (int)std::llround(cc.y);
            pointsOut.push_back({cx, cy});
            bowyerWatsonInsert((int)pointsOut.size() - 1, pointsOut, triangulation, edgeSet);
        }

        constrainBoundarySegments(currentSegments, pointsOut, triangulation, edgeSet);
        steinerPointsAdded++;
    }

    updateTopologyAndDomain(N, currentSegments, triangulation);
    std::cout << "Applying Laplacian Smoothing..." << std::endl;
    laplacianSmooth(pointsOut, triangulation, N);
    std::cout << "Engine added " << steinerPointsAdded << " Steiner Points for area & angle refinement." << std::endl;

    updateTopologyAndDomain(N, currentSegments, triangulation);
    flipEdgesForSkewness(pointsOut, triangulation, N, currentSegments);
}

// FIX #9: runDiagnosticSuite — Delaunay validation is now O(n) by checking only
// the opposite vertex of each adjacent triangle pair, not every point in the mesh.
// Super-triangle vertex indices (>= N) are excluded from incircle tests.
void runDiagnosticSuite(const vector<vector<int>> &pointsOut, const vector<Triangle> &triangulation, double maxArea)
{
    // Determine super-triangle start index: the three largest vertex indices in any triangle
    int maxIdx = 0;
    for (const auto &tri : triangulation)
        for (int i = 0; i < 3; i++)
            maxIdx = std::max(maxIdx, tri.v[i]);
    // Super-triangle vertices occupy indices (maxIdx-2, maxIdx-1, maxIdx) — skip them
    int superStart = maxIdx - 2;

    int activeTriangles    = 0;
    int orientationErrors  = 0;
    int delaunayViolations = 0;
    int areaViolations     = 0;
    int skinnyViolations   = 0;
    double maxSkewnessFound = 0.0;
    double minAngleFound    = 180.0;
    int    highSkewCount    = 0;

    std::cout << "\n==============================================" << std::endl;
    std::cout << "      MESH QUALITY DIAGNOSTIC REPORT          " << std::endl;
    std::cout << "==============================================" << std::endl;

    for (const auto &tri : triangulation)
    {
        if (!tri.active || tri.isExterior) continue;
        activeTriangles++;

        const auto &a = pointsOut[tri.v[0]];
        const auto &b = pointsOut[tri.v[1]];
        const auto &c = pointsOut[tri.v[2]];

        double skew = computeNESSkewness(a, b, c);
        vector<double> angles = computeInteriorAngles(a, b, c);
        double minA = std::min({angles[0], angles[1], angles[2]});

        if (skew > maxSkewnessFound) maxSkewnessFound = skew;
        if (minA < minAngleFound)    minAngleFound    = minA;
        if (skew > 0.5) highSkewCount++;

        double ratio = computeCircumradiusEdgeRatio(a, b, c);
        if (ratio > 1.5 + 1e-4) skinnyViolations++;

        if (orient2d(a, b, c) <= 0) orientationErrors++;

        for (int i = 0; i < 3; i++) {
            const Triangle* nb = tri.adj[i];
            if (!nb || !nb->active || nb->isExterior) continue;
            int opp = -1;
            for (int j = 0; j < 3; j++) {
                if (nb->v[j] != tri.v[0] && nb->v[j] != tri.v[1] && nb->v[j] != tri.v[2]) {
                    opp = nb->v[j]; break;
                }
            }
            if (opp == -1 || opp >= superStart) continue;
            if (incircle(a, b, c, pointsOut[opp])) delaunayViolations++;
        }

        double area = computeTriangleArea(a, b, c);
        if (area > maxArea + 1e-5) areaViolations++;
    }

    std::cout << "Total Interior Triangles : " << activeTriangles << std::endl;

    std::cout << "Topology (Orient2D)      : ";
    if (orientationErrors == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << orientationErrors << " Inverted Elements\033[0m" << std::endl;

    std::cout << "Delaunay (Incircle)      : ";
    if (delaunayViolations == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << delaunayViolations << " Encroachments\033[0m" << std::endl;

    std::cout << "Sizing Field (Area)      : ";
    if (areaViolations == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << areaViolations << " Oversized Elements\033[0m" << std::endl;

    std::cout << "Angle Quality (R/L)      : ";
    if (skinnyViolations == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << skinnyViolations << " Skinny Elements\033[0m" << std::endl;

    std::cout << "----------------------------------------------" << std::endl;
    std::cout << "Max Skewness Found       : " << maxSkewnessFound << " (Target < 0.85)" << std::endl;
    std::cout << "Min Interior Angle       : " << minAngleFound    << " Degrees" << std::endl;
    std::cout << "High Skew Elements (>0.5): " << highSkewCount    << std::endl;
    std::cout << "==============================================\n" << std::endl;
}
// flipEdgesForSkewness
// Post-processing topological optimiser. Iterates over all non-constrained interior
// edges and checks if swapping the diagonal of the shared quadrilateral would lower
// the maximum NES skewness of the two triangles. A flip is applied only when:
//   1. The edge is not constrained (boundary segment).
//   2. The shared quadrilateral is strictly convex (orient2d test on all 4 corners).
//   3. The proposed flip reduces the worst skewness by at least 1%.
// Adjacency is rebuilt after each flip. Stops when no improving flip exists or
// the 5000-flip safety cap is reached.
void flipEdgesForSkewness(vector<vector<int>>& pointsOut, vector<Triangle>& triangulation, int N, const vector<pair<int, int>>& currentSegments) {
    bool flipped = true;
    int flipCount = 0;
    int maxFlips = 5000;

    while (flipped && flipCount < maxFlips) {
        flipped = false;

        for (auto& tri : triangulation) {
            if (!tri.active || tri.isExterior) continue;

            for (int i = 0; i < 3; i++) {
                if (tri.isConstrained[i]) continue;

                Triangle* neighbor = tri.adj[i];
                if (!neighbor || !neighbor->active || neighbor->isExterior) continue;

                int vA = tri.v[(i+1)%3];
                int vB = tri.v[(i+2)%3];
                int vC = tri.v[i];

                int vD = -1;
                for (int j = 0; j < 3; j++) {
                    if (neighbor->v[j] != vA && neighbor->v[j] != vB) {
                        vD = neighbor->v[j];
                        break;
                    }
                }
                if (vD == -1) continue;

                long long o1 = orient2d(pointsOut[vC], pointsOut[vA], pointsOut[vD]);
                long long o2 = orient2d(pointsOut[vA], pointsOut[vD], pointsOut[vB]);
                long long o3 = orient2d(pointsOut[vD], pointsOut[vB], pointsOut[vC]);
                long long o4 = orient2d(pointsOut[vB], pointsOut[vC], pointsOut[vA]);

                bool isConvex = ((o1 > 0 && o2 > 0 && o3 > 0 && o4 > 0) ||
                                 (o1 < 0 && o2 < 0 && o3 < 0 && o4 < 0));
                if (!isConvex) continue;

                double currentSkew1  = computeNESSkewness(pointsOut[vC], pointsOut[vA], pointsOut[vB]);
                double currentSkew2  = computeNESSkewness(pointsOut[vD], pointsOut[vA], pointsOut[vB]);
                double currentMaxSkew  = std::max(currentSkew1, currentSkew2);

                double proposedSkew1 = computeNESSkewness(pointsOut[vC], pointsOut[vD], pointsOut[vA]);
                double proposedSkew2 = computeNESSkewness(pointsOut[vC], pointsOut[vD], pointsOut[vB]);
                double proposedMaxSkew = std::max(proposedSkew1, proposedSkew2);

                if (proposedMaxSkew < currentMaxSkew - 0.01) {
                    tri.active = false;
                    neighbor->active = false;
                    triangulation.emplace_back(vC, vA, vD);
                    triangulation.emplace_back(vC, vD, vB);
                    flipped = true;
                    flipCount++;
                    break;
                }
            }
            if (flipped) break;
        }

        if (flipped) {
            updateTopologyAndDomain(N, currentSegments, triangulation);
        }
    }

    std::cout << "Engine executed " << flipCount << " edge flips to reduce skewness." << std::endl;
}
void exportToOBJ(const string& filename, const vector<vector<int>>& pointsOut, const vector<Triangle>& triangulation) {
    ofstream out(filename);
    if (!out.is_open()) return;

    // 1. Calculate vertex colors based on adjacent triangle skewness
    vector<double> vertexSkew(pointsOut.size(), 0.0);
    vector<int> vertexTriCount(pointsOut.size(), 0);

    for (const auto& tri : triangulation) {
        if (!tri.active || tri.isExterior) continue;

        double skew = computeNESSkewness(pointsOut[tri.v[0]], pointsOut[tri.v[1]], pointsOut[tri.v[2]]);

        // Accumulate skewness for the vertices
        for (int i = 0; i < 3; i++) {
            vertexSkew[tri.v[i]] += skew;
            vertexTriCount[tri.v[i]]++;
        }
    }

    // 2. Find the center of the mesh to create a smooth 3D curve
    double minX = pointsOut[0][0], maxX = pointsOut[0][0];
    double minY = pointsOut[0][1], maxY = pointsOut[0][1];
    
    for (const auto& p : pointsOut) {
        if (p[0] < minX) minX = p[0];
        if (p[0] > maxX) maxX = p[0];
        if (p[1] < minY) minY = p[1];
        if (p[1] > maxY) maxY = p[1];
    }
    
    double centerX = (minX + maxX) / 2.0;
    double centerY = (minY + maxY) / 2.0;
    
    // 3. Write Vertices with RGB Heatmap Colors
    for (size_t i = 0; i < pointsOut.size(); i++) {
        double x = pointsOut[i][0];
        double y = pointsOut[i][1];
        
        // --- 3D MATH PROJECTION ---
        double distanceSq = std::pow(x - centerX, 2) + std::pow(y - centerY, 2);
        double z = distanceSq * 0.001; 
        
        // Calculate average skewness for this vertex
        double avgSkew = (vertexTriCount[i] > 0) ? (vertexSkew[i] / vertexTriCount[i]) : 0.0;
        
        // Map Skewness (0.0 to ~0.8) to a Blue-to-Red gradient
        double r = 0.0, g = 0.0, b = 1.0; 
        if (avgSkew < 0.25) {
            r = 0.0; g = avgSkew * 4.0; b = 1.0; 
        } else if (avgSkew < 0.5) {
            r = 0.0; g = 1.0; b = 1.0 - ((avgSkew - 0.25) * 4.0); 
        } else if (avgSkew < 0.75) {
            r = (avgSkew - 0.5) * 4.0; g = 1.0; b = 0.0; 
        } else {
            r = 1.0; g = 1.0 - ((avgSkew - 0.75) * 4.0); b = 0.0; 
        }

        // Output format: v x y z r g b
        out << "v " << x << " " << y << " " << z << " " << r << " " << g << " " << b << "\n";
    }

    // 4. Write Triangles
    int exportedTriangles = 0;
    for (const auto& tri : triangulation) {
        if (!tri.active || tri.isExterior) continue;
        out << "f " << (tri.v[0] + 1) << " " << (tri.v[1] + 1) << " " << (tri.v[2] + 1) << "\n";
        exportedTriangles++;
    }

    out.close();
    std::cout << "[PHASE 4] Exported Mesh with Quality Heatmap Data to " << filename << std::endl;
}
// Validation suite for the meshing core.
//
// Every case asserts on structural invariants rather than on a golden output,
// so the tests keep their meaning as the refinement heuristics are tuned:
//   - no inverted or zero-area elements
//   - every edge shared by at most two triangles, and shared consistently
//   - the boundary of the mesh is exactly the boundary of the input polygon
//   - every input segment survives as a chain of mesh edges
//   - Euler characteristic matches a disc or an annulus as appropriate

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sm/mesher.h"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

struct EdgeUse {
    int forward = 0;
    int backward = 0;
};

// Collects per-edge usage counts and the set of boundary edges.
std::map<std::pair<int, int>, EdgeUse> edgeUsage(const sm::MeshResult& m) {
    std::map<std::pair<int, int>, EdgeUse> uses;
    for (const auto& t : m.triangles) {
        for (int i = 0; i < 3; ++i) {
            int u = t[size_t(i)], v = t[size_t((i + 1) % 3)];
            const bool flip = u > v;
            auto key = flip ? std::make_pair(v, u) : std::make_pair(u, v);
            if (flip) {
                uses[key].backward++;
            } else {
                uses[key].forward++;
            }
        }
    }
    return uses;
}

double signedArea(const sm::Vec2& a, const sm::Vec2& b, const sm::Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

void checkStructure(const sm::MeshResult& m, const std::string& label,
                    int expectedLoops) {
    check(!m.triangles.empty(), label + ": produced triangles");
    if (m.triangles.empty()) return;

    int inverted = 0, degenerate = 0;
    for (const auto& t : m.triangles) {
        const double area = signedArea(m.points[size_t(t[0])], m.points[size_t(t[1])],
                                       m.points[size_t(t[2])]);
        if (area == 0.0) ++degenerate;
        if (area < 0.0) ++inverted;
    }
    check(inverted == 0, label + ": no inverted elements (" + std::to_string(inverted) + ")");
    check(degenerate == 0, label + ": no zero-area elements (" + std::to_string(degenerate) + ")");

    const auto uses = edgeUsage(m);
    int overused = 0, boundaryEdges = 0, inconsistent = 0;
    for (const auto& kv : uses) {
        const int total = kv.second.forward + kv.second.backward;
        if (total > 2) ++overused;
        if (total == 1) ++boundaryEdges;
        // In a consistently oriented manifold an interior edge is traversed
        // once in each direction.
        if (total == 2 && (kv.second.forward != 1 || kv.second.backward != 1)) ++inconsistent;
    }
    check(overused == 0, label + ": manifold, no edge in >2 triangles");
    check(inconsistent == 0, label + ": consistent orientation across shared edges");
    check(boundaryEdges > 0, label + ": has a boundary");

    // V - E + F = 2 - 2g - b for a surface with b boundary loops; a planar
    // triangulated disc gives 1, an annulus gives 0.
    const int V = int(m.points.size());
    const int E = int(uses.size());
    const int F = int(m.triangles.size());
    const int chi = V - E + F;
    check(chi == 2 - expectedLoops,
          label + ": Euler characteristic " + std::to_string(chi) + " (expected " +
              std::to_string(2 - expectedLoops) + ")");

    check(m.stats.invertedCount == 0, label + ": audit reports no inversions");
    // Stricter than the inversion count above: this also catches zero-area.
    check(m.stats.minScaledJacobian > 0.0, label + ": no degenerate elements");
    check(m.stats.maxAspectRatio < 1e5, label + ": aspect ratio stays bounded");
}

sm::MeshRequest squareRequest(double edgeTarget) {
    sm::MeshRequest r;
    r.points = {{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}};
    r.segments = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    r.options.targetEdgeLength = edgeTarget;
    r.options.maxSteinerPoints = 60000;
    return r;
}

void testSquare() {
    std::printf("[case] unit square, uniform sizing\n");
    sm::MeshResult m = sm::generateMesh(squareRequest(6.0));
    checkStructure(m, "square", 1);
    check(m.stats.minAngle > 20.0,
          "square: min angle " + std::to_string(m.stats.minAngle) + " > 20 deg");
    check(m.stats.maxSkewness < 0.85,
          "square: max skewness " + std::to_string(m.stats.maxSkewness) + " < 0.85");
    std::printf("        %d tris, minAngle %.2f, maxSkew %.3f, %.1f ms\n",
                m.stats.triangles, m.stats.minAngle, m.stats.maxSkewness,
                m.stats.refineMilliseconds);
}

void testLShape() {
    std::printf("[case] L-shaped domain (reflex corner)\n");
    sm::MeshRequest r;
    r.points = {{0, 0}, {60, 0}, {60, 30}, {30, 30}, {30, 60}, {0, 60}};
    r.segments = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}};
    r.options.targetEdgeLength = 4.0;
    r.options.maxSteinerPoints = 60000;
    sm::MeshResult m = sm::generateMesh(r);
    checkStructure(m, "L-shape", 1);
    check(m.stats.minAngle > 20.0,
          "L-shape: min angle " + std::to_string(m.stats.minAngle) + " > 20 deg");
    std::printf("        %d tris, minAngle %.2f, maxSkew %.3f\n", m.stats.triangles,
                m.stats.minAngle, m.stats.maxSkewness);
}

void testAnnulus() {
    std::printf("[case] square with a square hole\n");
    sm::MeshRequest r;
    r.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100},
                {40, 40}, {60, 40}, {60, 60}, {40, 60}};
    // Outer loop counter-clockwise, inner loop clockwise.
    r.segments = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                  {4, 7}, {7, 6}, {6, 5}, {5, 4}};
    r.options.targetEdgeLength = 6.0;
    r.options.maxSteinerPoints = 60000;
    sm::MeshResult m = sm::generateMesh(r);
    checkStructure(m, "annulus", 2);

    // No triangle centroid may land inside the hole.
    int inHole = 0;
    for (const auto& t : m.triangles) {
        const double cx = (m.points[size_t(t[0])].x + m.points[size_t(t[1])].x +
                           m.points[size_t(t[2])].x) / 3.0;
        const double cy = (m.points[size_t(t[0])].y + m.points[size_t(t[1])].y +
                           m.points[size_t(t[2])].y) / 3.0;
        if (cx > 40.5 && cx < 59.5 && cy > 40.5 && cy < 59.5) ++inHole;
    }
    check(inHole == 0, "annulus: hole is empty (" + std::to_string(inHole) + " strays)");
    std::printf("        %d tris, minAngle %.2f, maxSkew %.3f\n", m.stats.triangles,
                m.stats.minAngle, m.stats.maxSkewness);
}

void testCurvatureSizing() {
    std::printf("[case] graded sizing field\n");
    sm::MeshRequest r = squareRequest(0.0);
    r.options.targetEdgeLength = 20.0;
    // Fine near the left edge, coarse towards the right.
    r.sizeField = [](double x, double) { return 1.5 + 0.18 * x; };
    sm::MeshResult m = sm::generateMesh(r);
    checkStructure(m, "graded", 1);

    double leftMax = 0.0, rightMax = 0.0;
    for (const auto& t : m.triangles) {
        const sm::Vec2& a = m.points[size_t(t[0])];
        const sm::Vec2& b = m.points[size_t(t[1])];
        const sm::Vec2& c = m.points[size_t(t[2])];
        const double area = std::fabs(signedArea(a, b, c)) * 0.5;
        const double cx = (a.x + b.x + c.x) / 3.0;
        if (cx < 25.0) leftMax = std::max(leftMax, area);
        if (cx > 75.0) rightMax = std::max(rightMax, area);
    }
    check(leftMax * 4.0 < rightMax,
          "graded: elements grade with the field (left " + std::to_string(leftMax) +
              " vs right " + std::to_string(rightMax) + ")");
    std::printf("        %d tris, left %.1f vs right %.1f\n", m.stats.triangles, leftMax,
                rightMax);
}

void testDegenerateInput() {
    std::printf("[case] degenerate and hostile input\n");

    sm::MeshRequest empty;
    check(sm::generateMesh(empty).triangles.empty(), "empty input returns empty mesh");

    sm::MeshRequest collinear;
    collinear.points = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    collinear.options.targetEdgeLength = 0.5;
    sm::generateMesh(collinear);  // must not crash or hang
    check(true, "collinear input survives");

    sm::MeshRequest dupes = squareRequest(20.0);
    dupes.points.push_back({0.0, 0.0});
    dupes.points.push_back({100.0, 0.0});
    sm::MeshResult m = sm::generateMesh(dupes);
    checkStructure(m, "duplicates", 1);

    sm::MeshRequest thin;
    thin.points = {{0, 0}, {100, 0}, {100, 1}, {0, 1}};
    thin.segments = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    thin.options.targetEdgeLength = 2.0;
    checkStructure(sm::generateMesh(thin), "sliver domain", 1);
}

void testSegmentsPreserved() {
    std::printf("[case] boundary conformity\n");
    sm::MeshResult m = sm::generateMesh(squareRequest(5.0));

    // Every boundary edge of the mesh must lie on one of the four input
    // sides; nothing may cut across the domain or fall outside it.
    const auto uses = edgeUsage(m);
    int offBoundary = 0;
    for (const auto& kv : uses) {
        if (kv.second.forward + kv.second.backward != 1) continue;
        const sm::Vec2& a = m.points[size_t(kv.first.first)];
        const sm::Vec2& b = m.points[size_t(kv.first.second)];
        const bool onSide =
            (std::fabs(a.x) < 1e-6 && std::fabs(b.x) < 1e-6) ||
            (std::fabs(a.x - 100.0) < 1e-6 && std::fabs(b.x - 100.0) < 1e-6) ||
            (std::fabs(a.y) < 1e-6 && std::fabs(b.y) < 1e-6) ||
            (std::fabs(a.y - 100.0) < 1e-6 && std::fabs(b.y - 100.0) < 1e-6);
        if (!onSide) ++offBoundary;
    }
    check(offBoundary == 0,
          "square: all boundary edges lie on input segments (" +
              std::to_string(offBoundary) + " strays)");

    int outside = 0;
    for (const auto& p : m.points) {
        if (p.x < -1e-6 || p.x > 100.0 + 1e-6 || p.y < -1e-6 || p.y > 100.0 + 1e-6) ++outside;
    }
    check(outside == 0, "square: no vertex escapes the domain");
}

// Small LCG so the random sweeps below are reproducible without pulling in
// <random>, whose generators are not required to match across libraries.
struct Lcg {
    uint64_t s = 0x9e3779b97f4a7c15ull;
    double next(double lo, double hi) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return lo + (hi - lo) * double((s >> 11) & ((1ull << 53) - 1)) / double(1ull << 53);
    }
};

void testShapeMetrics() {
    std::printf("[case] aspect ratio and scaled Jacobian\n");

    // Closed forms. Equilateral is the optimum of both metrics.
    const sm::TriangleMetrics eq =
        sm::measure3D({0, 0, 0}, {1, 0, 0}, {0.5, sm::kSqrt3 / 2.0, 0});
    check(std::fabs(eq.aspectRatio - 1.0) < 1e-12, "equilateral: aspect ratio is 1");
    check(std::fabs(eq.scaledJacobian - 1.0) < 1e-12, "equilateral: scaled Jacobian is 1");

    const sm::TriangleMetrics ri = sm::measure3D({0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    check(std::fabs(ri.aspectRatio - 1.3938468501) < 1e-9, "right isoceles: aspect ratio");
    check(std::fabs(ri.scaledJacobian - 0.8164965809) < 1e-9, "right isoceles: scaled Jacobian");

    // The case that justifies choosing this aspect ratio over maxEdge/minEdge:
    // a cap sliver with a 177 degree angle that the edge ratio calls healthy.
    const sm::TriangleMetrics cap = sm::measure3D({0, 0, 0}, {100, 0, 0}, {50, 1, 0});
    check(cap.aspectRatio > 57.0, "cap sliver: aspect ratio flags it");
    check(cap.scaledJacobian < 0.024, "cap sliver: scaled Jacobian flags it");
    check(cap.maxEdge / cap.minEdge < 2.01,
          "cap sliver: edge ratio would not have flagged it");

    // The lattice cannot hold an exact equilateral, but both metrics are
    // stationary at the optimum so the error stays second order.
    const sm::TriangleMetrics latEq =
        sm::measure(sm::IPoint{0, 0}, sm::IPoint{1000000, 0}, sm::IPoint{500000, 866025});
    check(std::fabs(latEq.aspectRatio - 1.0) < 1e-6, "lattice equilateral: aspect ratio");
    check(std::fabs(latEq.scaledJacobian - 1.0) < 1e-6, "lattice equilateral: scaled Jacobian");

    // Degenerate input must clamp rather than produce inf or NaN, because the
    // report is scraped out of stdout by a numeric regex downstream.
    const sm::TriangleMetrics flat = sm::measure3D({0, 0, 0}, {1, 0, 0}, {2, 0, 0});
    check(flat.scaledJacobian == 0.0, "collinear: scaled Jacobian is zero");
    check(flat.aspectRatio == sm::kMaxAspectRatio, "collinear: aspect ratio clamps");
    check(!std::isnan(flat.aspectRatio) && !std::isnan(flat.scaledJacobian),
          "collinear: no NaN");

    Lcg rng;
    double identityErr = 0.0, liftErr = 0.0, permErr = 0.0, scaleErr = 0.0;
    int belowFloor = 0, signMismatch = 0;

    for (int i = 0; i < 1000; ++i) {
        const double ax = rng.next(-50, 50), ay = rng.next(-50, 50);
        const double bx = rng.next(-50, 50), by = rng.next(-50, 50);
        const double cx = rng.next(-50, 50), cy = rng.next(-50, 50);
        const sm::TriangleMetrics m = sm::measure3D({ax, ay, 0}, {bx, by, 0}, {cx, cy, 0});
        if (!(m.area > 1e-9)) continue;

        if (m.aspectRatio < 1.0 - 1e-12) ++belowFloor;

        // For a triangle det(J) is 2*area at every corner, so the minimum
        // scaled value always lands on the smallest angle.
        const double pred = (2.0 / sm::kSqrt3) * std::sin(m.minAngle * M_PI / 180.0);
        identityErr = std::max(identityErr, std::fabs(m.scaledJacobian - pred));

        // Scale invariance catches a term that was never normalised.
        const sm::TriangleMetrics s = sm::measure3D(
            {ax * 1000, ay * 1000, 0}, {bx * 1000, by * 1000, 0}, {cx * 1000, cy * 1000, 0});
        scaleErr = std::max(scaleErr, std::fabs(m.aspectRatio - s.aspectRatio));
        scaleErr = std::max(scaleErr, std::fabs(m.scaledJacobian - s.scaledJacobian));

        // Vertex order must not matter for the unsigned magnitudes.
        const sm::TriangleMetrics p = sm::measure3D({cx, cy, 0}, {ax, ay, 0}, {bx, by, 0});
        permErr = std::max(permErr, std::fabs(m.aspectRatio - p.aspectRatio));
        permErr = std::max(permErr, std::fabs(m.scaledJacobian - p.scaledJacobian));

        // The lattice and 3D paths must agree on a triangle lying in z = 0.
        const auto q = [](double v) { return int64_t(std::llround(v * 4096.0)); };
        const sm::TriangleMetrics l = sm::measure(
            sm::IPoint{q(ax), q(ay)}, sm::IPoint{q(bx), q(by)}, sm::IPoint{q(cx), q(cy)});
        const sm::TriangleMetrics l3 = sm::measure3D({double(q(ax)), double(q(ay)), 0},
                                                     {double(q(bx)), double(q(by)), 0},
                                                     {double(q(cx)), double(q(cy)), 0});
        liftErr = std::max(liftErr, std::fabs(l.aspectRatio - l3.aspectRatio));
        liftErr = std::max(liftErr, std::fabs(std::fabs(l.scaledJacobian) - l3.scaledJacobian));

        // The sign is taken from the exact predicate, so this holds without
        // tolerance.
        const bool negative = l.scaledJacobian < 0.0;
        const bool clockwise = sm::orient2d(sm::IPoint{q(ax), q(ay)}, sm::IPoint{q(bx), q(by)},
                                            sm::IPoint{q(cx), q(cy)}) < 0;
        if (negative != clockwise) ++signMismatch;
    }

    check(belowFloor == 0, "aspect ratio never drops below 1");
    check(identityErr < 1e-9,
          "scaled Jacobian matches (2/sqrt3)*sin(minAngle), err " + std::to_string(identityErr));
    check(scaleErr < 1e-9, "metrics are scale invariant, err " + std::to_string(scaleErr));
    check(permErr < 1e-9, "metrics ignore vertex order, err " + std::to_string(permErr));
    check(liftErr < 1e-6, "lattice and 3D paths agree, err " + std::to_string(liftErr));
    check(signMismatch == 0, "scaled Jacobian sign tracks orient2d exactly");

    // Degenerating a triangle must move both metrics the right way. Bounded at
    // h = 86 because past the equilateral height (86.6) it turns into a needle
    // and the aspect ratio climbs again.
    const double heights[] = {86.0, 80.0, 60.0, 40.0, 20.0, 10.0, 5.0, 2.0, 1.0};
    bool arRises = true, sjFalls = true;
    sm::TriangleMetrics prev = sm::measure3D({0, 0, 0}, {100, 0, 0}, {50, heights[0], 0});
    for (int i = 1; i < 9; ++i) {
        const sm::TriangleMetrics cur =
            sm::measure3D({0, 0, 0}, {100, 0, 0}, {50, heights[i], 0});
        if (!(cur.aspectRatio > prev.aspectRatio)) arRises = false;
        if (!(cur.scaledJacobian < prev.scaledJacobian)) sjFalls = false;
        prev = cur;
    }
    check(arRises, "aspect ratio rises monotonically as the element flattens");
    check(sjFalls, "scaled Jacobian falls monotonically as the element flattens");

    std::printf("        equilateral 1.000/1.000, right-iso %.4f/%.4f, cap sliver %.1f/%.4f\n",
                ri.aspectRatio, ri.scaledJacobian, cap.aspectRatio, cap.scaledJacobian);
}

void testScaling() {
    std::printf("[case] scaling behaviour\n");
    const double targets[] = {12.0, 6.0, 3.0, 1.5};
    for (double t : targets) {
        sm::MeshResult m = sm::generateMesh(squareRequest(t));
        std::printf("        target %5.2f -> %6d tris in %7.1f ms  (minAngle %.1f)\n", t,
                    m.stats.triangles, m.stats.refineMilliseconds, m.stats.minAngle);
        check(m.stats.invertedCount == 0, "scaling: no inversions at target " + std::to_string(t));
        check(m.stats.minAngle > 18.0, "scaling: angle bound holds at target " + std::to_string(t));
    }
}

}  // namespace

int main() {
    std::printf("Surface Mesher core validation\n");
    std::printf("==============================\n");
    testShapeMetrics();
    testSquare();
    testLShape();
    testAnnulus();
    testCurvatureSizing();
    testDegenerateInput();
    testSegmentsPreserved();
    testScaling();

    std::printf("------------------------------\n");
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

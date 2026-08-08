#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "sm/predicates.h"

namespace sm {

constexpr double kSqrt3 = 1.7320508075688772;

// Aspect ratio is unbounded as area goes to zero. Capping it keeps the value
// printable: `inf` does not survive the stdout scrape the API relies on.
constexpr double kMaxAspectRatio = 1e6;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline double distance(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Edge lengths are computed relative to vertex a so the subtraction happens on
// small numbers even when the lattice coordinates themselves are large.
struct TriangleMetrics {
    double area = 0.0;
    double minEdge = 0.0;
    double maxEdge = 0.0;
    double circumradius = 0.0;
    double minAngle = 0.0;
    double maxAngle = 0.0;

    // Verdict/CUBIT aspect ratio: 1.0 for equilateral, rising without bound as
    // the element flattens. Chosen over maxEdge/minEdge because that ratio is
    // blind to cap slivers - edges 1,1,1.99 hide a 168 degree angle and still
    // score 1.99, since it never looks at area.
    double aspectRatio = 1.0;

    // 1.0 equilateral, 0 degenerate, negative when the lattice winding is
    // inverted. For a triangle det(J) is 2*area at every corner, so this works
    // out to (2/sqrt(3))*sin(minAngle) - the magnitude adds nothing over
    // minAngle. It is here for the sign, which detects inversion, and because
    // solver pre-checks ask for it by name. Quads and tets are the case where
    // the magnitude carries real information.
    double scaledJacobian = 1.0;
};

// Interior angle opposite `opp`, in degrees. Clamped because rounding can push
// the cosine past +/-1 on near-degenerate triangles.
inline double angleFromSides(double opp, double s1, double s2) {
    if (s1 <= 0.0 || s2 <= 0.0) return 0.0;
    const double cosv =
        std::clamp((s1 * s1 + s2 * s2 - opp * opp) / (2.0 * s1 * s2), -1.0, 1.0);
    return std::acos(cosv) * (180.0 / M_PI);
}

// Shared by the lattice and 3D paths so the two cannot drift apart. Expects
// m->area to be set already; fills the unsigned magnitudes only.
inline void fillShapeMetrics(TriangleMetrics* m, double l1, double l2, double l3) {
    if (!(m->area > 0.0)) {
        m->aspectRatio = kMaxAspectRatio;
        m->scaledJacobian = 0.0;
        return;
    }

    const double lmax = std::max({l1, l2, l3});
    const double perimeter = l1 + l2 + l3;
    m->aspectRatio =
        std::min(lmax * perimeter / (4.0 * kSqrt3 * m->area), kMaxAspectRatio);

    // The smallest angle sits opposite the shortest edge, so the corner that
    // minimises det(J)/(|e1||e2|) is the one spanned by the two longest.
    double s[3] = {l1, l2, l3};
    std::sort(s, s + 3);
    m->scaledJacobian =
        std::clamp((2.0 / kSqrt3) * (2.0 * m->area) / (s[1] * s[2]), 0.0, 1.0);
}

inline TriangleMetrics measure(const IPoint& a, const IPoint& b, const IPoint& c) {
    const double bx = double(b.x - a.x), by = double(b.y - a.y);
    const double cx = double(c.x - a.x), cy = double(c.y - a.y);

    TriangleMetrics m;
    const double cross = bx * cy - by * cx;
    m.area = std::fabs(cross) * 0.5;

    const double lab = std::sqrt(bx * bx + by * by);
    const double lac = std::sqrt(cx * cx + cy * cy);
    const double dx = cx - bx, dy = cy - by;
    const double lbc = std::sqrt(dx * dx + dy * dy);

    m.minEdge = std::min({lab, lac, lbc});
    m.maxEdge = std::max({lab, lac, lbc});

    if (m.area > 0.0) {
        m.circumradius = (lab * lac * lbc) / (4.0 * m.area);
    } else {
        m.circumradius = std::numeric_limits<double>::infinity();
    }

    const double angA = angleFromSides(lbc, lab, lac);
    const double angB = angleFromSides(lac, lab, lbc);
    const double angC = 180.0 - angA - angB;

    m.minAngle = std::min({angA, angB, angC});
    m.maxAngle = std::max({angA, angB, angC});

    fillShapeMetrics(&m, lab, lac, lbc);
    // Sign comes from the exact predicate rather than `cross`: on large lattice
    // coordinates the double can lose a sign that orient2d still resolves, and
    // this keeps (scaledJacobian < 0) == (orient2d < 0) exactly true.
    if (orient2d(a, b, c) < 0) m.scaledJacobian = -m.scaledJacobian;
    return m;
}

// Normalised equiangular skewness, the metric ANSYS and most FEA
// pre-processors report. 0 is equilateral, 1 is fully degenerate.
inline double skewnessFromAngles(double minAngle, double maxAngle) {
    const double hi = (maxAngle - 60.0) / 120.0;
    const double lo = (60.0 - minAngle) / 60.0;
    return std::max({hi, lo, 0.0});
}

inline double skewness(const IPoint& a, const IPoint& b, const IPoint& c) {
    const TriangleMetrics m = measure(a, b, c);
    return skewnessFromAngles(m.minAngle, m.maxAngle);
}

// Same metrics for a triangle in space. scaledJacobian stays unsigned here: a
// surface triangle has no orientation of its own without a reference normal.
inline TriangleMetrics measure3D(const Vec3& a, const Vec3& b, const Vec3& c) {
    const double bx = b.x - a.x, by = b.y - a.y, bz = b.z - a.z;
    const double cx = c.x - a.x, cy = c.y - a.y, cz = c.z - a.z;
    const double nx = by * cz - bz * cy;
    const double ny = bz * cx - bx * cz;
    const double nz = bx * cy - by * cx;

    TriangleMetrics m;
    m.area = 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);

    const double lab = distance(a, b);
    const double lac = distance(a, c);
    const double lbc = distance(b, c);

    m.minEdge = std::min({lab, lac, lbc});
    m.maxEdge = std::max({lab, lac, lbc});

    if (m.area > 0.0) {
        m.circumradius = (lab * lac * lbc) / (4.0 * m.area);
    } else {
        m.circumradius = std::numeric_limits<double>::infinity();
    }

    const double angA = angleFromSides(lbc, lab, lac);
    const double angB = angleFromSides(lac, lab, lbc);
    const double angC = 180.0 - angA - angB;

    m.minAngle = std::min({angA, angB, angC});
    m.maxAngle = std::max({angA, angB, angC});

    fillShapeMetrics(&m, lab, lac, lbc);
    return m;
}

inline double skewness3D(const Vec3& a, const Vec3& b, const Vec3& c) {
    const TriangleMetrics m = measure3D(a, b, c);
    return skewnessFromAngles(m.minAngle, m.maxAngle);
}

// Circumcentre in lattice space. Falls back to the centroid when the triangle
// is too flat for the division to mean anything.
inline Vec2 circumcenter(const IPoint& a, const IPoint& b, const IPoint& c) {
    const double bx = double(b.x - a.x), by = double(b.y - a.y);
    const double cx = double(c.x - a.x), cy = double(c.y - a.y);
    const double d = 2.0 * (bx * cy - by * cx);
    if (std::fabs(d) < 1e-12) {
        return {double(a.x + b.x + c.x) / 3.0, double(a.y + b.y + c.y) / 3.0};
    }
    const double bl = bx * bx + by * by;
    const double cl = cx * cx + cy * cy;
    return {double(a.x) + (cy * bl - by * cl) / d,
            double(a.y) + (bx * cl - cx * bl) / d};
}

}  // namespace sm

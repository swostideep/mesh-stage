#pragma once

#include <algorithm>
#include <cmath>

#include "sm/predicates.h"

namespace sm {

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
};

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

    // Law of cosines, clamped because rounding can push the ratio past +/-1
    // on near-degenerate triangles.
    auto angleAt = [](double opp, double s1, double s2) {
        if (s1 <= 0.0 || s2 <= 0.0) return 0.0;
        double cosv = (s1 * s1 + s2 * s2 - opp * opp) / (2.0 * s1 * s2);
        cosv = std::clamp(cosv, -1.0, 1.0);
        return std::acos(cosv) * (180.0 / M_PI);
    };
    const double angA = angleAt(lbc, lab, lac);
    const double angB = angleAt(lac, lab, lbc);
    const double angC = 180.0 - angA - angB;

    m.minAngle = std::min({angA, angB, angC});
    m.maxAngle = std::max({angA, angB, angC});
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

inline double skewness3D(const Vec3& a, const Vec3& b, const Vec3& c) {
    const double l1 = distance(b, c), l2 = distance(a, c), l3 = distance(a, b);
    auto angleAt = [](double opp, double s1, double s2) {
        if (s1 <= 0.0 || s2 <= 0.0) return 0.0;
        double cosv = (s1 * s1 + s2 * s2 - opp * opp) / (2.0 * s1 * s2);
        cosv = std::clamp(cosv, -1.0, 1.0);
        return std::acos(cosv) * (180.0 / M_PI);
    };
    const double angA = angleAt(l1, l2, l3);
    const double angB = angleAt(l2, l1, l3);
    const double angC = 180.0 - angA - angB;
    return skewnessFromAngles(std::min({angA, angB, angC}),
                              std::max({angA, angB, angC}));
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

#pragma once

#include <cstdint>

namespace sm {

using i128 = __int128;

// Vertices live on an integer lattice so the predicates below are exact rather
// than merely accurate. Coordinates map into [0, kQuantScale] with the
// super-triangle at roughly eight times that span, keeping every intermediate
// product inside the 127 bits __int128 provides:
//
//   coordinate delta   <= 2^26
//   lift (dx^2 + dy^2) <= 2^53
//   2x2 minor          <= 2^53
//   lift * minor       <= 2^106
//   sum of three terms <= 2^108
//
// That leaves ~19 bits of headroom, so no input can overflow the predicates.
constexpr int64_t kQuantBits = 22;
constexpr int64_t kQuantScale = int64_t(1) << kQuantBits;

struct IPoint {
    int64_t x = 0;
    int64_t y = 0;
};

// Sign of the determinant of (b - a, c - a).
// +1 when a->b->c turns left, -1 when it turns right, 0 when collinear.
inline int orient2d(const IPoint& a, const IPoint& b, const IPoint& c) {
    const i128 det = i128(b.x - a.x) * i128(c.y - a.y) -
                     i128(b.y - a.y) * i128(c.x - a.x);
    return (det > 0) - (det < 0);
}

// True when d falls strictly inside the circumcircle of the
// counter-clockwise triangle (a, b, c). Callers are responsible for the
// winding; a clockwise triangle inverts the result.
inline bool inCircle(const IPoint& a, const IPoint& b, const IPoint& c,
                     const IPoint& d) {
    const i128 adx = a.x - d.x, ady = a.y - d.y;
    const i128 bdx = b.x - d.x, bdy = b.y - d.y;
    const i128 cdx = c.x - d.x, cdy = c.y - d.y;

    const i128 bc = bdx * cdy - cdx * bdy;
    const i128 ca = cdx * ady - adx * cdy;
    const i128 ab = adx * bdy - bdx * ady;

    const i128 alift = adx * adx + ady * ady;
    const i128 blift = bdx * bdx + bdy * bdy;
    const i128 clift = cdx * cdx + cdy * cdy;

    return alift * bc + blift * ca + clift * ab > 0;
}

// Twice the signed area. Exact, and cheap enough to use as a degeneracy guard.
inline i128 signedArea2(const IPoint& a, const IPoint& b, const IPoint& c) {
    return i128(b.x - a.x) * i128(c.y - a.y) -
           i128(b.y - a.y) * i128(c.x - a.x);
}

// True when p lies inside the diametral circle of segment (s, e). This is the
// encroachment test that drives subsegment splitting during refinement.
inline bool encroaches(const IPoint& s, const IPoint& e, const IPoint& p) {
    // (p - s) . (p - e) < 0 means p sees the segment at an obtuse angle,
    // which is exactly the condition for lying inside the diametral circle.
    const i128 dot = i128(s.x - p.x) * i128(e.x - p.x) +
                     i128(s.y - p.y) * i128(e.y - p.y);
    return dot < 0;
}

}  // namespace sm

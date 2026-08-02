#include "triangulator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>

namespace sm {
namespace {

// Hilbert index on a 2^16 x 2^16 grid. Sorting insertions along the curve
// keeps consecutive points close in space, which is what makes the location
// walk terminate in a couple of steps instead of crossing the mesh.
uint64_t hilbertIndex(uint32_t x, uint32_t y) {
    constexpr int kOrder = 16;
    uint64_t d = 0;
    for (uint32_t s = 1u << (kOrder - 1); s > 0; s >>= 1) {
        const uint32_t rx = (x & s) ? 1u : 0u;
        const uint32_t ry = (y & s) ? 1u : 0u;
        d += uint64_t(s) * uint64_t(s) * ((3u * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) {
                x = s - 1 - x;
                y = s - 1 - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

}  // namespace

Triangulator::Triangulator(const RefineOptions& options) : opts_(options) {}

IPoint Triangulator::quantise(const Vec2& p) const {
    return IPoint{int64_t(std::llround((p.x - origin_.x) * scale_)),
                  int64_t(std::llround((p.y - origin_.y) * scale_))};
}

Vec2 Triangulator::toInputSpace(const IPoint& p) const {
    return Vec2{origin_.x + double(p.x) * invScale_,
                origin_.y + double(p.y) * invScale_};
}

void Triangulator::begin(const std::vector<Vec2>& input) {
    inputCount_ = int32_t(input.size());
    if (inputCount_ == 0) return;

    double minX = input[0].x, maxX = input[0].x;
    double minY = input[0].y, maxY = input[0].y;
    for (const Vec2& p : input) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    // A single scale for both axes keeps the lattice isotropic, so angle and
    // skewness measured on the lattice mean the same thing as in input space.
    const double span = std::max({maxX - minX, maxY - minY, 1e-12});
    origin_ = Vec2{minX, minY};
    scale_ = double(kQuantScale) / span;
    invScale_ = 1.0 / scale_;
    latticeSpan_ = kQuantScale;

    pending_.resize(size_t(inputCount_));
    for (int32_t i = 0; i < inputCount_; ++i) pending_[size_t(i)] = quantise(input[size_t(i)]);

    mesh_.reserveTriangles(size_t(inputCount_) * 4 + 16);
    mesh_.points.reserve(size_t(inputCount_) * 2 + 8);

    for (int32_t i = 0; i < inputCount_; ++i) mesh_.addVertex(pending_[size_t(i)]);

    // Super-triangle large enough that every input point sits well inside it,
    // yet small enough that the predicates keep their headroom.
    const int64_t s = latticeSpan_;
    const int64_t cx = s / 2, cy = s / 2, m = 2 * s;
    superBase_ = mesh_.addVertex(IPoint{cx - 3 * m, cy - m});
    mesh_.addVertex(IPoint{cx + 3 * m, cy - m});
    mesh_.addVertex(IPoint{cx, cy + 3 * m});

    const int32_t root = mesh_.addTriangle(superBase_, superBase_ + 1, superBase_ + 2);
    walkHint_ = root;

    cavityStamp_.assign(size_t(inputCount_) * 4 + 16, 0);
    fanFromP_.assign(mesh_.points.size(), kNoHalf);
    fanToP_.assign(mesh_.points.size(), kNoHalf);
    fanStamp_.assign(mesh_.points.size(), 0);
}

int32_t Triangulator::locate(const IPoint& p, int32_t hint, int32_t* onEdge) const {
    *onEdge = kNoHalf;
    int32_t t = hint;
    if (t < 0 || t >= mesh_.triangleCount() || !mesh_.live(t)) {
        for (int32_t i = 0; i < mesh_.triangleCount(); ++i) {
            if (mesh_.live(i)) {
                t = i;
                break;
            }
        }
    }

    // Stochastic visibility walk: rotating the edge we test first stops the
    // walk from cycling on cocircular configurations.
    uint32_t rng = uint32_t(t) * 2654435761u + 1u;
    const int32_t guard = 4 * mesh_.triangleCount() + 64;

    for (int32_t step = 0; step < guard; ++step) {
        const int32_t base = t * 3;
        rng = rng * 1103515245u + 12345u;
        const int start = int((rng >> 16) % 3u);

        int32_t crossing = kNoHalf;
        int32_t zeroEdge = kNoHalf;
        for (int k = 0; k < 3; ++k) {
            const int32_t he = base + (start + k) % 3;
            const int o = orient2d(mesh_.position(he), mesh_.position(nextHalf(he)), p);
            if (o < 0) {
                crossing = he;
                break;
            }
            if (o == 0) zeroEdge = he;
        }

        if (crossing == kNoHalf) {
            *onEdge = zeroEdge;
            return t;
        }
        const int32_t tw = mesh_.twin[size_t(crossing)];
        if (tw == kNoHalf) return t;  // convex hull reached
        t = triOf(tw);
    }
    return t;
}

bool Triangulator::buildCavity(const IPoint& p, int32_t seedTri, int32_t pierceHalf) {
    cavity_.clear();
    wall_.clear();
    stack_.clear();

    if (cavityStamp_.size() < size_t(mesh_.triangleCount())) {
        cavityStamp_.resize(size_t(mesh_.triangleCount()) * 2 + 16, 0);
    }
    ++stampCounter_;

    auto marked = [&](int32_t t) { return cavityStamp_[size_t(t)] == stampCounter_; };
    auto mark = [&](int32_t t) { cavityStamp_[size_t(t)] = stampCounter_; };

    mark(seedTri);
    stack_.push_back(seedTri);

    // Splitting a subsegment has to open the wall on both sides at once,
    // otherwise the neighbouring triangle keeps a hanging node.
    if (pierceHalf != kNoHalf) {
        const int32_t tw = mesh_.twin[size_t(pierceHalf)];
        if (tw != kNoHalf && !marked(triOf(tw))) {
            mark(triOf(tw));
            stack_.push_back(triOf(tw));
        }
    }

    while (!stack_.empty()) {
        const int32_t t = stack_.back();
        stack_.pop_back();
        cavity_.push_back(t);

        for (int i = 0; i < 3; ++i) {
            const int32_t he = t * 3 + i;
            const int32_t tw = mesh_.twin[size_t(he)];
            const bool blocked = mesh_.isConstrained(he) && he != pierceHalf &&
                                 (pierceHalf == kNoHalf || tw != pierceHalf);

            if (tw == kNoHalf || blocked) {
                wall_.push_back({tw, mesh_.vertex(he), mesh_.vertex(nextHalf(he)),
                                 mesh_.constrained[size_t(he)],
                                 uint8_t(mesh_.exterior(t) ? 1 : 0)});
                continue;
            }
            const int32_t nt = triOf(tw);
            if (marked(nt)) continue;

            const int32_t nb = nt * 3;
            if (inCircle(mesh_.points[size_t(mesh_.corner[size_t(nb)])],
                         mesh_.points[size_t(mesh_.corner[size_t(nb) + 1])],
                         mesh_.points[size_t(mesh_.corner[size_t(nb) + 2])], p)) {
                mark(nt);
                stack_.push_back(nt);
            } else {
                wall_.push_back({tw, mesh_.vertex(he), mesh_.vertex(nextHalf(he)),
                                 mesh_.constrained[size_t(he)],
                                 uint8_t(mesh_.exterior(t) ? 1 : 0)});
            }
        }
    }

    // A wall edge whose far side also ended up inside the cavity means the
    // region wrapped around a constraint. Drop back to the seed triangle,
    // which is always a legal (if less optimal) cavity.
    for (const BoundaryEdge& e : wall_) {
        if (e.outerTwin != kNoHalf && marked(triOf(e.outerTwin))) {
            cavity_.assign(1, seedTri);
            wall_.clear();
            for (int i = 0; i < 3; ++i) {
                const int32_t he = seedTri * 3 + i;
                wall_.push_back({mesh_.twin[size_t(he)], mesh_.vertex(he),
                                 mesh_.vertex(nextHalf(he)),
                                 mesh_.constrained[size_t(he)],
                                 uint8_t(mesh_.exterior(seedTri) ? 1 : 0)});
            }
            return true;
        }
    }
    return !wall_.empty();
}

int32_t Triangulator::insertVertex(int32_t vertexId, int32_t hint, int32_t pierceHalf) {
    const IPoint p = mesh_.points[size_t(vertexId)];

    int32_t onEdge = kNoHalf;
    int32_t seed = (pierceHalf != kNoHalf) ? triOf(pierceHalf) : locate(p, hint, &onEdge);

    if (pierceHalf == kNoHalf) {
        // Duplicate input points collapse onto the existing vertex.
        for (int i = 0; i < 3; ++i) {
            const IPoint& q = mesh_.position(seed * 3 + i);
            if (q.x == p.x && q.y == p.y) return kNoTri;
        }
        if (onEdge != kNoHalf) pierceHalf = onEdge;
    }

    if (!buildCavity(p, seed, pierceHalf)) return kNoTri;

    // Snapshot the wall before the cavity dies, then recycle the slots.
    const std::vector<BoundaryEdge> wall = wall_;
    for (int32_t t : cavity_) mesh_.killTriangle(t);

    if (fanStamp_.size() < mesh_.points.size()) {
        fanFromP_.resize(mesh_.points.size(), kNoHalf);
        fanToP_.resize(mesh_.points.size(), kNoHalf);
        fanStamp_.resize(mesh_.points.size(), 0);
    }
    ++fanCounter_;

    newTris_.clear();
    for (const BoundaryEdge& e : wall) {
        if (e.from == vertexId || e.to == vertexId) continue;
        if (orient2d(mesh_.points[size_t(e.from)], mesh_.points[size_t(e.to)], p) <= 0) continue;

        const int32_t t = mesh_.addTriangle(e.from, e.to, vertexId);
        newTris_.push_back(t);
        const int32_t base = t * 3;

        mesh_.flags[size_t(t)] = uint8_t(kTriLive | (e.exterior ? kTriExterior : 0));
        mesh_.link(base + 0, e.outerTwin);
        mesh_.constrained[size_t(base)] = e.constrained;
        if (e.outerTwin != kNoHalf) mesh_.constrained[size_t(e.outerTwin)] = e.constrained;

        // base+1 is (to -> p), base+2 is (p -> from); pair them with the
        // neighbouring wedges as those appear.
        if (fanStamp_[size_t(e.to)] != fanCounter_) {
            fanStamp_[size_t(e.to)] = fanCounter_;
            fanFromP_[size_t(e.to)] = kNoHalf;
            fanToP_[size_t(e.to)] = kNoHalf;
        }
        if (fanStamp_[size_t(e.from)] != fanCounter_) {
            fanStamp_[size_t(e.from)] = fanCounter_;
            fanFromP_[size_t(e.from)] = kNoHalf;
            fanToP_[size_t(e.from)] = kNoHalf;
        }
        fanToP_[size_t(e.to)] = base + 1;
        fanFromP_[size_t(e.from)] = base + 2;
    }

    for (const BoundaryEdge& e : wall) {
        for (int32_t v : {e.from, e.to}) {
            if (fanStamp_[size_t(v)] != fanCounter_) continue;
            const int32_t in = fanToP_[size_t(v)];
            const int32_t out = fanFromP_[size_t(v)];
            if (in != kNoHalf && out != kNoHalf) mesh_.link(in, out);
        }
    }

    if (newTris_.empty()) return kNoTri;
    walkHint_ = newTris_.back();
    return walkHint_;
}

void Triangulator::insertInputVertices() {
    if (inputCount_ == 0) return;

    // Biased randomised insertion order: a handful of geometrically growing
    // rounds, each Hilbert sorted. The randomisation bounds the worst case,
    // the Hilbert sort delivers the cache locality.
    std::vector<int32_t> order(static_cast<size_t>(inputCount_), 0);
    std::iota(order.begin(), order.end(), 0);

    std::mt19937 rng(0x5eed5eed);
    std::shuffle(order.begin(), order.end(), rng);

    const double invSpan = 65535.0 / double(latticeSpan_ > 0 ? latticeSpan_ : 1);
    std::vector<uint64_t> keys(static_cast<size_t>(inputCount_), 0);
    for (int32_t i = 0; i < inputCount_; ++i) {
        const IPoint& q = pending_[size_t(i)];
        const uint32_t hx = uint32_t(std::clamp(double(q.x) * invSpan, 0.0, 65535.0));
        const uint32_t hy = uint32_t(std::clamp(double(q.y) * invSpan, 0.0, 65535.0));
        keys[size_t(i)] = hilbertIndex(hx, hy);
    }

    size_t begin = 0;
    size_t roundSize = 8;
    while (begin < order.size()) {
        const size_t end = std::min(order.size(), begin + roundSize);
        std::sort(order.begin() + long(begin), order.begin() + long(end),
                  [&](int32_t a, int32_t b) { return keys[size_t(a)] < keys[size_t(b)]; });
        begin = end;
        roundSize *= 2;
    }

    for (int32_t idx : order) insertVertex(idx, walkHint_, kNoHalf);
}

int32_t Triangulator::outgoingHalf(int32_t vertex) const {
    const int32_t he = mesh_.vertexHalf[size_t(vertex)];
    if (he != kNoHalf && mesh_.live(triOf(he)) && mesh_.vertex(he) == vertex) return he;
    for (int32_t t = 0; t < mesh_.triangleCount(); ++t) {
        if (!mesh_.live(t)) continue;
        for (int i = 0; i < 3; ++i) {
            if (mesh_.corner[size_t(t) * 3 + i] == vertex) return t * 3 + i;
        }
    }
    return kNoHalf;
}

bool Triangulator::segmentPresent(int32_t a, int32_t b, int32_t* half) const {
    const int32_t start = outgoingHalf(a);
    if (start == kNoHalf) return false;
    int32_t e = start;
    do {
        if (mesh_.vertex(nextHalf(e)) == b) {
            *half = e;
            return true;
        }
        const int32_t prev = prevHalf(e);
        // Walking the far side catches vertices on the convex hull, whose
        // ring is a fan rather than a full cycle.
        if (mesh_.vertex(prev) == b) {
            *half = prev;
            return true;
        }
        e = mesh_.twin[size_t(prev)];
        if (e == kNoHalf) return false;
    } while (e != start);
    return false;
}

void Triangulator::triangulatePseudoPolygon(const std::vector<int32_t>& poly, int lo,
                                            int hi, std::vector<int32_t>& out) {
    if (hi - lo < 2) return;
    if (hi - lo == 2) {
        int32_t a = poly[size_t(lo)], b = poly[size_t(lo + 1)], c = poly[size_t(hi)];
        if (orient2d(mesh_.points[size_t(a)], mesh_.points[size_t(b)],
                     mesh_.points[size_t(c)]) < 0) {
            std::swap(b, c);
        }
        out.push_back(mesh_.addTriangle(a, b, c));
        return;
    }

    // Pick the apex whose circumcircle is empty of the remaining polygon
    // vertices; that reproduces the Delaunay triangulation of the cavity.
    int best = lo + 1;
    for (int k = lo + 2; k < hi; ++k) {
        const IPoint& a = mesh_.points[size_t(poly[size_t(lo)])];
        const IPoint& b = mesh_.points[size_t(poly[size_t(best)])];
        const IPoint& c = mesh_.points[size_t(poly[size_t(hi)])];
        const IPoint& d = mesh_.points[size_t(poly[size_t(k)])];
        const bool ccw = orient2d(a, b, c) > 0;
        if (ccw ? inCircle(a, b, c, d) : inCircle(a, c, b, d)) best = k;
    }

    int32_t a = poly[size_t(lo)], b = poly[size_t(best)], c = poly[size_t(hi)];
    if (orient2d(mesh_.points[size_t(a)], mesh_.points[size_t(b)],
                 mesh_.points[size_t(c)]) < 0) {
        std::swap(b, c);
    }
    out.push_back(mesh_.addTriangle(a, b, c));

    triangulatePseudoPolygon(poly, lo, best, out);
    triangulatePseudoPolygon(poly, best, hi, out);
}

void Triangulator::relinkRegion(const std::vector<int32_t>& newTris,
                                const std::vector<BoundaryEdge>& wall) {
    // Small local hash of undirected edge -> half-edge. The region is a strip
    // of a few triangles, so a flat vector beats a node-based map here.
    struct Slot {
        int64_t key;
        int32_t half;
    };
    std::vector<Slot> table;
    table.reserve(newTris.size() * 3);

    auto keyOf = [](int32_t u, int32_t v) {
        return (int64_t(std::min(u, v)) << 32) | uint32_t(std::max(u, v));
    };

    for (int32_t t : newTris) {
        for (int i = 0; i < 3; ++i) {
            const int32_t he = t * 3 + i;
            mesh_.twin[size_t(he)] = kNoHalf;
            mesh_.constrained[size_t(he)] = 0;
        }
    }

    for (int32_t t : newTris) {
        for (int i = 0; i < 3; ++i) {
            const int32_t he = t * 3 + i;
            const int64_t key = keyOf(mesh_.vertex(he), mesh_.vertex(nextHalf(he)));
            auto it = std::find_if(table.begin(), table.end(),
                                   [&](const Slot& s) { return s.key == key; });
            if (it != table.end()) {
                mesh_.link(he, it->half);
                it->key = -1;
            } else {
                table.push_back({key, he});
            }
        }
    }

    for (const BoundaryEdge& e : wall) {
        if (e.outerTwin == kNoHalf) continue;
        const int64_t key = keyOf(e.from, e.to);
        auto it = std::find_if(table.begin(), table.end(),
                               [&](const Slot& s) { return s.key == key; });
        if (it == table.end()) continue;
        mesh_.link(it->half, e.outerTwin);
        mesh_.constrained[size_t(it->half)] = e.constrained;
        mesh_.constrained[size_t(e.outerTwin)] = e.constrained;
        it->key = -1;
    }
}

void Triangulator::forceSegment(int32_t a, int32_t b) {
    int32_t existing = kNoHalf;
    if (segmentPresent(a, b, &existing)) {
        mesh_.setConstrained(existing, true);
        return;
    }

    const IPoint& pa = mesh_.points[size_t(a)];
    const IPoint& pb = mesh_.points[size_t(b)];

    // Find the wedge at a that the segment leaves through.
    const int32_t start = outgoingHalf(a);
    if (start == kNoHalf) return;

    int32_t entry = kNoHalf;
    int32_t e = start;
    int guard = 0;
    do {
        const int32_t c1 = mesh_.vertex(nextHalf(e));
        const int32_t c2 = mesh_.vertex(prevHalf(e));
        const int o1 = orient2d(pa, mesh_.points[size_t(c1)], pb);
        const int o2 = orient2d(pa, mesh_.points[size_t(c2)], pb);
        if (o1 > 0 && o2 < 0) {
            entry = nextHalf(e);
            break;
        }
        e = mesh_.twin[size_t(prevHalf(e))];
        if (e == kNoHalf) break;
    } while (e != start && ++guard < 4096);

    if (entry == kNoHalf) return;

    std::vector<int32_t> leftPoly{a}, rightPoly{a};
    std::vector<int32_t> killed;
    std::vector<BoundaryEdge> wall;

    auto recordWall = [&](int32_t tri, const std::vector<int32_t>& skip) {
        for (int i = 0; i < 3; ++i) {
            const int32_t he = tri * 3 + i;
            const int32_t tw = mesh_.twin[size_t(he)];
            if (tw != kNoHalf &&
                std::find(skip.begin(), skip.end(), triOf(tw)) != skip.end()) {
                continue;
            }
            wall.push_back({tw, mesh_.vertex(he), mesh_.vertex(nextHalf(he)),
                            mesh_.constrained[size_t(he)],
                            uint8_t(mesh_.exterior(tri) ? 1 : 0)});
        }
    };

    int32_t cross = entry;
    killed.push_back(triOf(cross));
    {
        const int32_t u = mesh_.vertex(cross);
        const int32_t w = mesh_.vertex(nextHalf(cross));
        if (orient2d(pa, pb, mesh_.points[size_t(u)]) > 0) {
            leftPoly.push_back(u);
            rightPoly.push_back(w);
        } else {
            rightPoly.push_back(u);
            leftPoly.push_back(w);
        }
    }

    bool reachedB = false;
    for (int step = 0; step < 1 << 20; ++step) {
        if (mesh_.isConstrained(cross)) return;  // crossing constraints need a split
        const int32_t tw = mesh_.twin[size_t(cross)];
        if (tw == kNoHalf) return;
        const int32_t nt = triOf(tw);
        killed.push_back(nt);

        const int32_t apex = mesh_.vertex(prevHalf(tw));
        if (apex == b) {
            reachedB = true;
            break;
        }
        if (orient2d(pa, pb, mesh_.points[size_t(apex)]) > 0) {
            leftPoly.push_back(apex);
        } else {
            rightPoly.push_back(apex);
        }

        auto straddles = [&](int32_t h) {
            const int s1 = orient2d(pa, pb, mesh_.points[size_t(mesh_.vertex(h))]);
            const int s2 = orient2d(pa, pb, mesh_.points[size_t(mesh_.vertex(nextHalf(h)))]);
            return s1 * s2 < 0;
        };
        const int32_t e1 = nextHalf(tw), e2 = prevHalf(tw);
        cross = straddles(e1) ? e1 : e2;
    }
    if (!reachedB) return;

    leftPoly.push_back(b);
    rightPoly.push_back(b);

    const uint8_t exteriorFlag = uint8_t(mesh_.exterior(killed.front()) ? 1 : 0);
    for (int32_t t : killed) recordWall(t, killed);
    for (int32_t t : killed) mesh_.killTriangle(t);

    // Both chains are listed from a to b; the left one runs clockwise in that
    // order, so it is reversed before triangulation.
    std::reverse(leftPoly.begin(), leftPoly.end());

    std::vector<int32_t> created;
    triangulatePseudoPolygon(rightPoly, 0, int(rightPoly.size()) - 1, created);
    triangulatePseudoPolygon(leftPoly, 0, int(leftPoly.size()) - 1, created);

    for (int32_t t : created) {
        mesh_.flags[size_t(t)] = uint8_t(kTriLive | (exteriorFlag ? kTriExterior : 0));
    }

    relinkRegion(created, wall);

    int32_t made = kNoHalf;
    if (segmentPresent(a, b, &made)) mesh_.setConstrained(made, true);
    if (!created.empty()) walkHint_ = created.back();
}

void Triangulator::insertSegments(const std::vector<std::array<int32_t, 2>>& segments) {
    for (const auto& s : segments) {
        if (s[0] == s[1]) continue;
        if (s[0] < 0 || s[1] < 0) continue;
        if (s[0] >= inputCount_ || s[1] >= inputCount_) continue;
        forceSegment(s[0], s[1]);
    }
    collectSegments();
}

void Triangulator::classifyDomain() {
    const int32_t triCount = mesh_.triangleCount();
    for (int32_t t = 0; t < triCount; ++t) {
        if (mesh_.live(t)) mesh_.flags[size_t(t)] = kTriLive;
    }

    // Crossing-parity flood. Starting outside the model, every constrained
    // edge we step over toggles between inside and outside. A simple
    // "stop at the boundary" fill cannot reach an interior hole, which is why
    // parity is tracked instead: it handles nested loops and does not care
    // which way round the caller wound them.
    std::vector<uint8_t> visited(size_t(triCount), 0);
    std::vector<int32_t> queue;
    queue.reserve(size_t(triCount));

    for (int32_t t = 0; t < triCount; ++t) {
        if (!mesh_.live(t)) continue;
        bool touches = false;
        for (int i = 0; i < 3; ++i) {
            if (mesh_.corner[size_t(t) * 3 + i] >= superBase_) {
                touches = true;
                break;
            }
        }
        if (touches) {
            visited[size_t(t)] = 1;
            mesh_.flags[size_t(t)] |= kTriExterior;
            queue.push_back(t);
        }
    }

    for (size_t head = 0; head < queue.size(); ++head) {
        const int32_t t = queue[head];
        const bool outside = mesh_.exterior(t);
        for (int i = 0; i < 3; ++i) {
            const int32_t he = t * 3 + i;
            const int32_t tw = mesh_.twin[size_t(he)];
            if (tw == kNoHalf) continue;
            const int32_t nt = triOf(tw);
            if (!mesh_.live(nt) || visited[size_t(nt)]) continue;
            visited[size_t(nt)] = 1;
            const bool nextOutside = mesh_.isConstrained(he) ? !outside : outside;
            if (nextOutside) mesh_.flags[size_t(nt)] |= kTriExterior;
            queue.push_back(nt);
        }
    }
}

void Triangulator::collectSegments() {
    segments_.clear();
    for (int32_t t = 0; t < mesh_.triangleCount(); ++t) {
        if (!mesh_.live(t)) continue;
        for (int i = 0; i < 3; ++i) {
            const int32_t he = t * 3 + i;
            if (!mesh_.isConstrained(he)) continue;
            const int32_t tw = mesh_.twin[size_t(he)];
            if (tw != kNoHalf && tw < he) continue;  // record each edge once
            segments_.push_back({mesh_.vertex(he), mesh_.vertex(nextHalf(he)), true});
        }
    }
    rebuildSegmentGrid();
}

}  // namespace sm

#include <algorithm>
#include <cmath>
#include <queue>

#include "triangulator.h"

namespace sm {

void Triangulator::rebuildSegmentGrid() {
    gridDim_ = std::clamp(int(std::sqrt(double(segments_.size()) + 1.0)), 1, 256);
    gridCell_ = double(latticeSpan_) / double(gridDim_);
    grid_.assign(size_t(gridDim_) * size_t(gridDim_), {});
    for (int32_t i = 0; i < int32_t(segments_.size()); ++i) {
        if (!segments_[size_t(i)].alive) continue;
        const IPoint& a = mesh_.points[size_t(segments_[size_t(i)].a)];
        const IPoint& b = mesh_.points[size_t(segments_[size_t(i)].b)];
        const double cx = double(a.x + b.x) * 0.5, cy = double(a.y + b.y) * 0.5;
        const double r = 0.5 * std::hypot(double(b.x - a.x), double(b.y - a.y));

        // A segment is registered in every cell its diametral circle touches,
        // which reduces the encroachment query to a single cell lookup.
        const int lo0 = std::clamp(int((cx - r) / gridCell_), 0, gridDim_ - 1);
        const int hi0 = std::clamp(int((cx + r) / gridCell_), 0, gridDim_ - 1);
        const int lo1 = std::clamp(int((cy - r) / gridCell_), 0, gridDim_ - 1);
        const int hi1 = std::clamp(int((cy + r) / gridCell_), 0, gridDim_ - 1);
        for (int gy = lo1; gy <= hi1; ++gy) {
            for (int gx = lo0; gx <= hi0; ++gx) {
                grid_[size_t(gy) * size_t(gridDim_) + size_t(gx)].push_back(i);
            }
        }
    }
}

bool Triangulator::anyEncroached(const IPoint& p, int32_t* segIndex) const {
    if (grid_.empty()) return false;
    const int gx = std::clamp(int(double(p.x) / gridCell_), 0, gridDim_ - 1);
    const int gy = std::clamp(int(double(p.y) / gridCell_), 0, gridDim_ - 1);
    for (int32_t idx : grid_[size_t(gy) * size_t(gridDim_) + size_t(gx)]) {
        const Segment& s = segments_[size_t(idx)];
        if (!s.alive) continue;
        if (s.a == 0 && s.b == 0) continue;
        const IPoint& a = mesh_.points[size_t(s.a)];
        const IPoint& b = mesh_.points[size_t(s.b)];
        if ((a.x == p.x && a.y == p.y) || (b.x == p.x && b.y == p.y)) continue;
        if (encroaches(a, b, p)) {
            *segIndex = idx;
            return true;
        }
    }
    return false;
}

void Triangulator::splitSegment(int32_t segIndex) {
    Segment& seg = segments_[size_t(segIndex)];
    if (!seg.alive) return;

    const int32_t a = seg.a, b = seg.b;
    const IPoint pa = mesh_.points[size_t(a)];
    const IPoint pb = mesh_.points[size_t(b)];

    const double len = std::hypot(double(pb.x - pa.x), double(pb.y - pa.y));
    if (len < opts_.minSegmentFraction * double(latticeSpan_)) {
        seg.alive = false;  // small input angle: stop chasing it
        return;
    }

    const IPoint mid{(pa.x + pb.x) >> 1, (pa.y + pb.y) >> 1};
    if ((mid.x == pa.x && mid.y == pa.y) || (mid.x == pb.x && mid.y == pb.y)) {
        seg.alive = false;
        return;
    }

    int32_t he = kNoHalf;
    if (!segmentPresent(a, b, &he)) {
        seg.alive = false;
        return;
    }

    const int32_t vId = mesh_.addVertex(mid);
    if (insertVertex(vId, walkHint_, he) == kNoTri) {
        mesh_.points.pop_back();
        mesh_.vertexHalf.pop_back();
        seg.alive = false;
        return;
    }
    ++steiner_;
    seg.alive = false;

    // The two halves of the split segment have to be re-flagged; the fan
    // edges around the new vertex are created unconstrained.
    const int32_t start = outgoingHalf(vId);
    if (start != kNoHalf) {
        int32_t e = start;
        for (int guard = 0; guard < 512; ++guard) {
            const int32_t other = mesh_.vertex(nextHalf(e));
            if (other == a || other == b) mesh_.setConstrained(e, true);
            const int32_t nxt = mesh_.twin[size_t(prevHalf(e))];
            if (nxt == kNoHalf || nxt == start) break;
            e = nxt;
        }
    }

    auto registerSegment = [&](int32_t u, int32_t v) {
        segments_.push_back({u, v, true});
        const int32_t idx = int32_t(segments_.size()) - 1;
        const IPoint& p0 = mesh_.points[size_t(u)];
        const IPoint& p1 = mesh_.points[size_t(v)];
        const double cx = double(p0.x + p1.x) * 0.5, cy = double(p0.y + p1.y) * 0.5;
        const double r = 0.5 * std::hypot(double(p1.x - p0.x), double(p1.y - p0.y));
        const int lo0 = std::clamp(int((cx - r) / gridCell_), 0, gridDim_ - 1);
        const int hi0 = std::clamp(int((cx + r) / gridCell_), 0, gridDim_ - 1);
        const int lo1 = std::clamp(int((cy - r) / gridCell_), 0, gridDim_ - 1);
        const int hi1 = std::clamp(int((cy + r) / gridCell_), 0, gridDim_ - 1);
        for (int gy = lo1; gy <= hi1; ++gy) {
            for (int gx = lo0; gx <= hi0; ++gx) {
                grid_[size_t(gy) * size_t(gridDim_) + size_t(gx)].push_back(idx);
            }
        }
    };
    registerSegment(a, vId);
    registerSegment(vId, b);
}

double Triangulator::targetEdgeLengthAt(const IPoint& centroid,
                                        const SizeField& field) const {
    if (field) {
        const Vec2 world = toInputSpace(centroid);
        const double l = field(world.x, world.y);
        if (l > 0.0) return l * scale_;  // input units -> lattice units
    }
    if (opts_.targetEdgeLength > 0.0) return opts_.targetEdgeLength * scale_;
    return 0.0;
}

bool Triangulator::triangleIsBad(int32_t t, const SizeField& field,
                                 double* priority) const {
    const int32_t base = t * 3;
    const IPoint& a = mesh_.points[size_t(mesh_.corner[size_t(base)])];
    const IPoint& b = mesh_.points[size_t(mesh_.corner[size_t(base) + 1])];
    const IPoint& c = mesh_.points[size_t(mesh_.corner[size_t(base) + 2])];

    const TriangleMetrics m = measure(a, b, c);
    if (m.area <= 0.0) return false;

    // A shape violation is more urgent than a size violation, so it is scored
    // higher and drains from the queue first.
    if (m.minEdge > 0.0) {
        const double ratio = m.circumradius / m.minEdge;
        if (ratio > opts_.maxRadiusEdgeRatio) {
            *priority = 1e6 + ratio;
            return true;
        }
    }

    const IPoint centroid{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
    const double target = targetEdgeLengthAt(centroid, field);
    if (target > 0.0 && 2.0 * m.circumradius > target) {
        *priority = (2.0 * m.circumradius) / target;
        return true;
    }
    return false;
}

void Triangulator::refine(const SizeField& sizeField) {
    if (mesh_.triangleCount() == 0) return;

    struct Candidate {
        double priority;
        int32_t tri;
        bool operator<(const Candidate& o) const { return priority < o.priority; }
    };
    std::priority_queue<Candidate> queue;

    auto pushIfBad = [&](int32_t t) {
        if (t < 0 || t >= mesh_.triangleCount()) return;
        if (!mesh_.interior(t)) return;
        double p = 0.0;
        if (triangleIsBad(t, sizeField, &p)) queue.push({p, t});
    };

    // Clear encroachment left over from the input itself before refining, or
    // the first circumcentres will immediately be rejected.
    for (int32_t i = 0; opts_.allowSegmentSplitting && i < int32_t(segments_.size()); ++i) {
        if (!segments_[size_t(i)].alive) continue;
        int32_t he = kNoHalf;
        if (!segmentPresent(segments_[size_t(i)].a, segments_[size_t(i)].b, &he)) continue;
        bool bad = false;
        for (int32_t h : {he, mesh_.twin[size_t(he)]}) {
            if (h == kNoHalf) continue;
            const int32_t apex = mesh_.vertex(prevHalf(h));
            if (apex >= superBase_) continue;
            if (encroaches(mesh_.points[size_t(segments_[size_t(i)].a)],
                           mesh_.points[size_t(segments_[size_t(i)].b)],
                           mesh_.points[size_t(apex)])) {
                bad = true;
                break;
            }
        }
        if (bad) splitSegment(i);
    }

    for (int32_t t = 0; t < mesh_.triangleCount(); ++t) pushIfBad(t);

    const long long iterationCap = 40LL * opts_.maxSteinerPoints + 10000;
    long long iterations = 0;

    while (!queue.empty() && steiner_ < opts_.maxSteinerPoints &&
           ++iterations < iterationCap) {
        const Candidate top = queue.top();
        queue.pop();
        const int32_t t = top.tri;
        if (!mesh_.interior(t)) continue;

        double p = 0.0;
        if (!triangleIsBad(t, sizeField, &p)) continue;

        const int32_t base = t * 3;
        const IPoint a = mesh_.points[size_t(mesh_.corner[size_t(base)])];
        const IPoint b = mesh_.points[size_t(mesh_.corner[size_t(base) + 1])];
        const IPoint c = mesh_.points[size_t(mesh_.corner[size_t(base) + 2])];

        const Vec2 cc = circumcenter(a, b, c);
        const IPoint target{int64_t(std::llround(cc.x)), int64_t(std::llround(cc.y))};

        int32_t encroached = kNoHalf;
        if (anyEncroached(target, &encroached)) {
            if (!opts_.allowSegmentSplitting) {
                // Boundary is frozen, so this triangle cannot be improved.
                // Retreating to the centroid is tempting but unsound: unlike
                // the circumcentre it does not reduce the circumradius, so a
                // size-violating triangle stays size-violating and the queue
                // refills forever. Leave it and move on.
                continue;
            }
            splitSegment(encroached);
            // The split rebuilt the neighbourhood, so its triangles have to
            // be re-examined; without this the queue drains early and
            // refinement stops well short of the requested size.
            for (int32_t nt : newTris_) pushIfBad(nt);
            queue.push(top);  // revisit once the boundary is conforming
            continue;
        }

        // Reject circumcentres that escape the domain; they belong to
        // triangles that the boundary treatment will fix instead.
        int32_t onEdge = kNoHalf;
        const int32_t host = locate(target, walkHint_, &onEdge);
        if (!mesh_.interior(host)) continue;

        const int32_t vId = mesh_.addVertex(target);
        const int32_t made = insertVertex(vId, host, kNoHalf);
        if (made == kNoTri) {
            mesh_.points.pop_back();
            mesh_.vertexHalf.pop_back();
            continue;
        }
        ++steiner_;

        for (int32_t nt : newTris_) pushIfBad(nt);
    }
}

}  // namespace sm

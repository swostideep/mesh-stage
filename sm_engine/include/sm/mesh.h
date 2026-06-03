#pragma once

#include <cstdint>
#include <vector>

#include "sm/predicates.h"

namespace sm {

// Triangle topology in half-edge form.
//
// Triangle t owns corners 3t, 3t+1, 3t+2. Half-edge e runs from vertex(e) to
// vertex(next(e)) and is stored on the corner opposite the vertex it faces.
// twin(e) is the matching half-edge in the adjacent triangle, or kNoHalf on a
// boundary. Everything is an index, so growing the arrays never invalidates a
// reference the way a vector of pointer-linked triangles would.
//
// Dead triangles go on a free list and get reused, which keeps peak memory
// proportional to the live mesh instead of to the total number of insertions.

constexpr int32_t kNoHalf = -1;
constexpr int32_t kNoTri = -1;

inline int32_t triOf(int32_t half) { return half / 3; }
inline int32_t nextHalf(int32_t half) { return (half % 3 == 2) ? half - 2 : half + 1; }
inline int32_t prevHalf(int32_t half) { return (half % 3 == 0) ? half + 2 : half - 1; }

enum TriFlag : uint8_t {
    kTriLive = 1u << 0,
    kTriExterior = 1u << 1,
    kTriQueued = 1u << 2,
};

class TriMesh {
public:
    std::vector<IPoint> points;

    std::vector<int32_t> corner;      // 3 per triangle: vertex index
    std::vector<int32_t> twin;        // 3 per triangle: opposite half-edge
    std::vector<uint8_t> constrained; // 3 per triangle: half-edge is a segment
    std::vector<uint8_t> flags;       // 1 per triangle

    // One outgoing half-edge per vertex, used to walk a vertex ring in O(deg)
    // instead of scanning the triangle array.
    std::vector<int32_t> vertexHalf;

    int32_t triangleCount() const { return int32_t(flags.size()); }
    int32_t vertexCount() const { return int32_t(points.size()); }

    bool live(int32_t t) const { return (flags[size_t(t)] & kTriLive) != 0; }
    bool exterior(int32_t t) const { return (flags[size_t(t)] & kTriExterior) != 0; }
    bool interior(int32_t t) const {
        return (flags[size_t(t)] & (kTriLive | kTriExterior)) == kTriLive;
    }

    int32_t vertex(int32_t half) const { return corner[size_t(half)]; }
    const IPoint& position(int32_t half) const {
        return points[size_t(corner[size_t(half)])];
    }

    int32_t addVertex(const IPoint& p) {
        points.push_back(p);
        vertexHalf.push_back(kNoHalf);
        return int32_t(points.size()) - 1;
    }

    // Allocates a counter-clockwise triangle, recycling a dead slot when one
    // is available.
    int32_t addTriangle(int32_t a, int32_t b, int32_t c) {
        int32_t t;
        if (!freeTris_.empty()) {
            t = freeTris_.back();
            freeTris_.pop_back();
        } else {
            t = int32_t(flags.size());
            corner.resize(size_t(t) * 3 + 3);
            twin.resize(size_t(t) * 3 + 3);
            constrained.resize(size_t(t) * 3 + 3);
            flags.resize(size_t(t) + 1);
        }
        const size_t base = size_t(t) * 3;
        corner[base + 0] = a;
        corner[base + 1] = b;
        corner[base + 2] = c;
        twin[base + 0] = twin[base + 1] = twin[base + 2] = kNoHalf;
        constrained[base + 0] = constrained[base + 1] = constrained[base + 2] = 0;
        flags[size_t(t)] = kTriLive;
        vertexHalf[size_t(a)] = int32_t(base) + 0;
        vertexHalf[size_t(b)] = int32_t(base) + 1;
        vertexHalf[size_t(c)] = int32_t(base) + 2;
        ++liveCount_;
        return t;
    }

    void killTriangle(int32_t t) {
        if (!live(t)) return;
        flags[size_t(t)] = 0;
        freeTris_.push_back(t);
        --liveCount_;
    }

    void link(int32_t a, int32_t b) {
        if (a != kNoHalf) twin[size_t(a)] = b;
        if (b != kNoHalf) twin[size_t(b)] = a;
    }

    bool isConstrained(int32_t half) const { return constrained[size_t(half)] != 0; }

    void setConstrained(int32_t half, bool on) {
        constrained[size_t(half)] = on ? 1 : 0;
        const int32_t t = twin[size_t(half)];
        if (t != kNoHalf) constrained[size_t(t)] = on ? 1 : 0;
    }

    int32_t liveTriangles() const { return liveCount_; }

    void reserveTriangles(size_t n) {
        corner.reserve(n * 3);
        twin.reserve(n * 3);
        constrained.reserve(n * 3);
        flags.reserve(n);
    }

    // Rebuilds vertexHalf so every vertex points at a live corner. Cheap
    // (single linear pass) and only needed after bulk topology surgery.
    void refreshVertexHalves() {
        std::fill(vertexHalf.begin(), vertexHalf.end(), kNoHalf);
        for (int32_t t = 0; t < triangleCount(); ++t) {
            if (!live(t)) continue;
            for (int i = 0; i < 3; ++i) {
                vertexHalf[size_t(corner[size_t(t) * 3 + i])] = t * 3 + i;
            }
        }
    }

private:
    std::vector<int32_t> freeTris_;
    int32_t liveCount_ = 0;
};

}  // namespace sm

#include <algorithm>
#include <cmath>
#include <vector>

#include "triangulator.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace sm {
namespace {

// Compressed adjacency: vertex -> incident interior triangles. Built in two
// linear passes so the smoother can reach a vertex ring in O(degree) instead
// of scanning the whole triangle array per vertex.
struct VertexRings {
    std::vector<int32_t> offset;
    std::vector<int32_t> data;
};

VertexRings buildRings(const TriMesh& mesh) {
    VertexRings rings;
    rings.offset.assign(size_t(mesh.vertexCount()) + 1, 0);
    for (int32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (!mesh.interior(t)) continue;
        for (int i = 0; i < 3; ++i) ++rings.offset[size_t(mesh.corner[size_t(t) * 3 + i]) + 1];
    }
    for (size_t i = 1; i < rings.offset.size(); ++i) rings.offset[i] += rings.offset[i - 1];

    rings.data.resize(size_t(rings.offset.back()));
    std::vector<int32_t> cursor(rings.offset.begin(), rings.offset.end() - 1);
    for (int32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (!mesh.interior(t)) continue;
        for (int i = 0; i < 3; ++i) {
            const int32_t v = mesh.corner[size_t(t) * 3 + i];
            rings.data[size_t(cursor[size_t(v)]++)] = t;
        }
    }
    return rings;
}

}  // namespace

int Triangulator::smooth(int passes) {
    if (passes <= 0 || mesh_.triangleCount() == 0) return 0;

    const int32_t vcount = mesh_.vertexCount();
    std::vector<uint8_t> locked(size_t(vcount), 0);

    // Input geometry, boundary vertices and the super-triangle corners all
    // stay put; only interior Steiner points are free to move.
    for (int32_t v = 0; v < inputCount_; ++v) locked[size_t(v)] = 1;
    for (int32_t v = superBase_; v < vcount; ++v) locked[size_t(v)] = 1;
    for (int32_t t = 0; t < mesh_.triangleCount(); ++t) {
        if (!mesh_.live(t)) continue;
        for (int i = 0; i < 3; ++i) {
            const int32_t he = t * 3 + i;
            const int32_t tw = mesh_.twin[size_t(he)];
            const bool boundary = mesh_.isConstrained(he) || tw == kNoHalf ||
                                  mesh_.exterior(t) != mesh_.exterior(triOf(tw));
            if (boundary) {
                locked[size_t(mesh_.vertex(he))] = 1;
                locked[size_t(mesh_.vertex(nextHalf(he)))] = 1;
            }
        }
    }

    const VertexRings rings = buildRings(mesh_);
    std::vector<IPoint> proposal(mesh_.points);
    std::vector<IPoint> previous(mesh_.points);
    std::vector<uint8_t> moved(size_t(vcount), 0);
    int totalMoved = 0;

    for (int pass = 0; pass < passes; ++pass) {
        std::fill(moved.begin(), moved.end(), 0);
        previous = mesh_.points;

        // Jacobi sweep: every proposal is computed from the previous
        // positions only, so the loop carries no dependency and parallelises
        // cleanly across vertices.
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int32_t v = 0; v < vcount; ++v) {
            if (locked[size_t(v)]) continue;
            const int32_t begin = rings.offset[size_t(v)];
            const int32_t end = rings.offset[size_t(v) + 1];
            if (end - begin < 3) continue;

            double sx = 0.0, sy = 0.0;
            int count = 0;
            for (int32_t k = begin; k < end; ++k) {
                const int32_t t = rings.data[size_t(k)];
                for (int i = 0; i < 3; ++i) {
                    const int32_t w = mesh_.corner[size_t(t) * 3 + i];
                    if (w == v) continue;
                    sx += double(mesh_.points[size_t(w)].x);
                    sy += double(mesh_.points[size_t(w)].y);
                    ++count;
                }
            }
            if (count == 0) continue;

            const IPoint candidate{int64_t(std::llround(sx / count)),
                                   int64_t(std::llround(sy / count))};
            if (candidate.x == mesh_.points[size_t(v)].x &&
                candidate.y == mesh_.points[size_t(v)].y) {
                continue;
            }

            // Reject any move that would invert or flatten an incident
            // triangle. Checked against the pre-pass positions.
            bool safe = true;
            for (int32_t k = begin; k < end && safe; ++k) {
                const int32_t t = rings.data[size_t(k)];
                IPoint tri[3];
                for (int i = 0; i < 3; ++i) {
                    const int32_t w = mesh_.corner[size_t(t) * 3 + i];
                    tri[i] = (w == v) ? candidate : mesh_.points[size_t(w)];
                }
                if (orient2d(tri[0], tri[1], tri[2]) <= 0) safe = false;
            }
            if (!safe) continue;

            proposal[size_t(v)] = candidate;
            moved[size_t(v)] = 1;
        }

        for (int32_t v = 0; v < vcount; ++v) {
            if (moved[size_t(v)]) {
                mesh_.points[size_t(v)] = proposal[size_t(v)];
                ++totalMoved;
            }
        }

        // Individually safe moves can still collide when applied together.
        // Reverting the offenders restores a configuration that was valid,
        // so the repair always terminates.
        for (int repair = 0; repair < 3; ++repair) {
            int reverted = 0;
            for (int32_t t = 0; t < mesh_.triangleCount(); ++t) {
                if (!mesh_.interior(t)) continue;
                const int32_t base = t * 3;
                if (orient2d(mesh_.points[size_t(mesh_.corner[size_t(base)])],
                             mesh_.points[size_t(mesh_.corner[size_t(base) + 1])],
                             mesh_.points[size_t(mesh_.corner[size_t(base) + 2])]) > 0) {
                    continue;
                }
                for (int i = 0; i < 3; ++i) {
                    const int32_t v = mesh_.corner[size_t(base) + i];
                    if (!moved[size_t(v)]) continue;
                    mesh_.points[size_t(v)] = previous[size_t(v)];
                    moved[size_t(v)] = 0;
                    --totalMoved;
                    ++reverted;
                }
            }
            if (reverted == 0) break;
        }

        for (int32_t v = 0; v < vcount; ++v) proposal[size_t(v)] = mesh_.points[size_t(v)];
    }

    return totalMoved;
}

int Triangulator::optimiseTopology() {
    if (!opts_.topologicalOptimisation) return 0;

    // Work list of candidate diagonals. Flipping an edge only affects the
    // four edges around it, so those get pushed back and nothing else has to
    // be revisited.
    std::vector<int32_t> stack;
    std::vector<uint8_t> queued(size_t(mesh_.triangleCount()) * 3, 0);
    stack.reserve(size_t(mesh_.triangleCount()) * 3);

    for (int32_t t = 0; t < mesh_.triangleCount(); ++t) {
        if (!mesh_.interior(t)) continue;
        for (int i = 0; i < 3; ++i) stack.push_back(t * 3 + i);
    }

    const int maxFlips = 8 * mesh_.triangleCount() + 1024;
    int flips = 0;

    while (!stack.empty() && flips < maxFlips) {
        const int32_t e = stack.back();
        stack.pop_back();
        if (e >= int32_t(queued.size())) continue;
        queued[size_t(e)] = 0;

        const int32_t t = triOf(e);
        if (!mesh_.interior(t)) continue;
        if (mesh_.isConstrained(e)) continue;
        const int32_t tw = mesh_.twin[size_t(e)];
        if (tw == kNoHalf) continue;
        const int32_t nt = triOf(tw);
        if (!mesh_.interior(nt)) continue;

        const int32_t u = mesh_.vertex(e);
        const int32_t v = mesh_.vertex(nextHalf(e));
        const int32_t c = mesh_.vertex(prevHalf(e));
        const int32_t d = mesh_.vertex(prevHalf(tw));

        const IPoint& pu = mesh_.points[size_t(u)];
        const IPoint& pv = mesh_.points[size_t(v)];
        const IPoint& pc = mesh_.points[size_t(c)];
        const IPoint& pd = mesh_.points[size_t(d)];

        // Only a strictly convex quadrilateral can take the flip.
        if (orient2d(pu, pd, pv) <= 0) continue;
        if (orient2d(pd, pv, pc) <= 0) continue;
        if (orient2d(pv, pc, pu) <= 0) continue;
        if (orient2d(pc, pu, pd) <= 0) continue;

        const double before = std::max(skewness(pu, pv, pc), skewness(pv, pu, pd));
        const double after = std::max(skewness(pu, pd, pc), skewness(pd, pv, pc));
        if (after >= before - 1e-3) continue;

        const int32_t e0 = t * 3, e1 = t * 3 + 1, e2 = t * 3 + 2;
        const int32_t n0 = nt * 3, n1 = nt * 3 + 1, n2 = nt * 3 + 2;

        const int32_t outVC = mesh_.twin[size_t(nextHalf(e))];   // v -> c
        const int32_t outCU = mesh_.twin[size_t(prevHalf(e))];   // c -> u
        const int32_t outUD = mesh_.twin[size_t(nextHalf(tw))];  // u -> d
        const int32_t outDV = mesh_.twin[size_t(prevHalf(tw))];  // d -> v
        const uint8_t conVC = mesh_.constrained[size_t(nextHalf(e))];
        const uint8_t conCU = mesh_.constrained[size_t(prevHalf(e))];
        const uint8_t conUD = mesh_.constrained[size_t(nextHalf(tw))];
        const uint8_t conDV = mesh_.constrained[size_t(prevHalf(tw))];

        mesh_.corner[size_t(e0)] = u;
        mesh_.corner[size_t(e1)] = d;
        mesh_.corner[size_t(e2)] = c;
        mesh_.corner[size_t(n0)] = d;
        mesh_.corner[size_t(n1)] = v;
        mesh_.corner[size_t(n2)] = c;

        mesh_.link(e0, outUD);
        mesh_.link(e2, outCU);
        mesh_.link(n0, outDV);
        mesh_.link(n1, outVC);
        mesh_.link(e1, n2);

        mesh_.constrained[size_t(e0)] = conUD;
        if (outUD != kNoHalf) mesh_.constrained[size_t(outUD)] = conUD;
        mesh_.constrained[size_t(e2)] = conCU;
        if (outCU != kNoHalf) mesh_.constrained[size_t(outCU)] = conCU;
        mesh_.constrained[size_t(n0)] = conDV;
        if (outDV != kNoHalf) mesh_.constrained[size_t(outDV)] = conDV;
        mesh_.constrained[size_t(n1)] = conVC;
        if (outVC != kNoHalf) mesh_.constrained[size_t(outVC)] = conVC;
        mesh_.constrained[size_t(e1)] = 0;
        mesh_.constrained[size_t(n2)] = 0;

        mesh_.vertexHalf[size_t(u)] = e0;
        mesh_.vertexHalf[size_t(d)] = e1;
        mesh_.vertexHalf[size_t(c)] = e2;
        mesh_.vertexHalf[size_t(v)] = n1;

        ++flips;
        for (int32_t h : {e0, e2, n0, n1}) {
            if (h < int32_t(queued.size()) && !queued[size_t(h)]) {
                queued[size_t(h)] = 1;
                stack.push_back(h);
            }
        }
    }

    return flips;
}

}  // namespace sm

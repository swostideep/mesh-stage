#include "sm/mesher.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "triangulator.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace sm {

MeshStats auditMesh(const TriMesh& mesh) {
    MeshStats stats;

    double maxSkew = 0.0;
    double skewSum = 0.0;
    double minAngle = 180.0;
    int highSkew = 0;
    int inverted = 0;
    int count = 0;
    int violations = 0;

    const int32_t triCount = mesh.triangleCount();

#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(max : maxSkew) \
    reduction(+ : skewSum, highSkew, inverted, count, violations) reduction(min : minAngle)
#endif
    for (int32_t t = 0; t < triCount; ++t) {
        if (!mesh.interior(t)) continue;
        const int32_t base = t * 3;
        const IPoint& a = mesh.points[size_t(mesh.corner[size_t(base)])];
        const IPoint& b = mesh.points[size_t(mesh.corner[size_t(base) + 1])];
        const IPoint& c = mesh.points[size_t(mesh.corner[size_t(base) + 2])];

        const TriangleMetrics m = measure(a, b, c);
        const double skew = skewnessFromAngles(m.minAngle, m.maxAngle);

        ++count;
        skewSum += skew;
        if (skew > maxSkew) maxSkew = skew;
        if (skew > 0.5) ++highSkew;
        if (m.minAngle < minAngle) minAngle = m.minAngle;
        if (orient2d(a, b, c) <= 0) ++inverted;

        // Constrained Delaunay check: only the vertex opposite each shared,
        // unconstrained edge can violate the empty-circumcircle property.
        // That makes this O(triangles) rather than O(triangles * vertices).
        for (int i = 0; i < 3; ++i) {
            const int32_t he = base + i;
            if (mesh.isConstrained(he)) continue;
            const int32_t tw = mesh.twin[size_t(he)];
            if (tw == kNoHalf || tw < he) continue;
            if (!mesh.interior(triOf(tw))) continue;
            const IPoint& d = mesh.points[size_t(mesh.vertex(prevHalf(tw)))];
            if (inCircle(a, b, c, d)) ++violations;
        }
    }

    stats.triangles = count;
    stats.maxSkewness = maxSkew;
    stats.meanSkewness = count > 0 ? skewSum / count : 0.0;
    stats.minAngle = count > 0 ? minAngle : 0.0;
    stats.highSkewCount = highSkew;
    stats.invertedCount = inverted;
    stats.delaunayViolations = violations;
    return stats;
}

MeshResult generateMesh(const MeshRequest& request) {
    MeshResult result;
    if (request.points.size() < 3) return result;

    const auto start = std::chrono::steady_clock::now();

    Triangulator tri(request.options);
    tri.begin(request.points);
    tri.insertInputVertices();
    tri.insertSegments(request.segments);
    tri.classifyDomain();
    tri.refine(request.sizeField);

    const int flips1 = tri.optimiseTopology();
    const int smoothed = tri.smooth(request.options.smoothingPasses);
    const int flips2 = tri.optimiseTopology();

    const auto stop = std::chrono::steady_clock::now();

    const TriMesh& mesh = tri.mesh();

    // Compact: emit only vertices referenced by an interior triangle.
    std::vector<int32_t> remap(size_t(mesh.vertexCount()), -1);
    result.triangles.reserve(size_t(mesh.liveTriangles()));

    // Interior triangles never touch the super-triangle corners, so walking
    // them is enough to drop the scaffolding along with any unused vertex.
    for (int32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (!mesh.interior(t)) continue;
        std::array<int32_t, 3> out{};
        for (int i = 0; i < 3; ++i) {
            const int32_t v = mesh.corner[size_t(t) * 3 + i];
            if (remap[size_t(v)] < 0) {
                remap[size_t(v)] = int32_t(result.points.size());
                result.points.push_back(tri.toInputSpace(mesh.points[size_t(v)]));
            }
            out[size_t(i)] = remap[size_t(v)];
        }
        result.triangles.push_back(out);
    }

    result.stats = auditMesh(mesh);
    result.stats.inputPoints = tri.inputVertexCount();
    result.stats.steinerPoints = tri.steinerCount();
    result.stats.edgeFlips = flips1 + flips2;
    result.stats.verticesSmoothed = smoothed;
    result.stats.refineMilliseconds =
        std::chrono::duration<double, std::milli>(stop - start).count();
    return result;
}

}  // namespace sm

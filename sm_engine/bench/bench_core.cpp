// Throughput benchmark for the meshing core.
//
// Reports elements per second and peak element counts across a sweep of target
// edge lengths, which is the number that actually matters when sizing a job
// against a container CPU budget.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sm/mesher.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

sm::MeshRequest gearProfile(int teeth, double target) {
    sm::MeshRequest r;
    // A toothed outline plus a bore gives a boundary with many short segments
    // and a reflex-heavy profile, which is a fair stand-in for a CAD face.
    const int steps = teeth * 4;
    for (int i = 0; i < steps; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(steps);
        const double radius = (i % 4 < 2) ? 100.0 : 78.0;
        r.points.push_back({radius * std::cos(a), radius * std::sin(a)});
    }
    for (int i = 0; i < steps; ++i) {
        r.segments.push_back({int32_t(i), int32_t((i + 1) % steps)});
    }

    const int base = int(r.points.size());
    const int boreSteps = 48;
    for (int i = 0; i < boreSteps; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(boreSteps);
        r.points.push_back({22.0 * std::cos(a), 22.0 * std::sin(a)});
    }
    for (int i = 0; i < boreSteps; ++i) {
        // Reversed winding so the bore reads as a hole.
        r.segments.push_back({int32_t(base + (i + 1) % boreSteps), int32_t(base + i)});
    }

    r.options.targetEdgeLength = target;
    r.options.maxSteinerPoints = 400000;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const int teeth = (argc > 1) ? std::atoi(argv[1]) : 24;

#ifdef _OPENMP
    std::printf("OpenMP threads : %d\n", omp_get_max_threads());
#else
    std::printf("OpenMP threads : 1 (compiled without OpenMP)\n");
#endif
    std::printf("Profile        : %d-tooth gear with bore\n\n", teeth);
    std::printf("%10s %10s %10s %12s %10s %10s %10s\n", "target", "nodes", "elements",
                "ms", "elem/s", "minAngle", "maxSkew");

    const double targets[] = {8.0, 4.0, 2.0, 1.0, 0.5, 0.35};
    for (double t : targets) {
        sm::MeshRequest r = gearProfile(teeth, t);
        const auto t0 = std::chrono::steady_clock::now();
        sm::MeshResult m = sm::generateMesh(r);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double rate = ms > 0.0 ? double(m.stats.triangles) / (ms / 1000.0) : 0.0;

        std::printf("%10.2f %10zu %10d %12.1f %10.0f %10.2f %10.3f\n", t, m.points.size(),
                    m.stats.triangles, ms, rate, m.stats.minAngle, m.stats.maxSkewness);

        if (m.stats.invertedCount != 0 || m.stats.delaunayViolations != 0) {
            std::printf("  WARNING inverted=%d delaunayViolations=%d\n",
                        m.stats.invertedCount, m.stats.delaunayViolations);
        }
    }
    return 0;
}

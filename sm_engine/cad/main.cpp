// Command line front end for the surface mesher.
//
// Usage: voronoi_mesh <input.step> [deflection] [output.obj] [defeatureTol]
//                     [patchHoles] [growthRate] [proximity]
//
// The phase banners and the summary keys are part of the contract with the API
// server, which streams this output to the browser and scrapes the node,
// element and skewness counts out of it.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "occt_pipeline.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

std::string previewPathFor(const std::string& output) {
    const size_t dot = output.find_last_of('.');
    if (dot == std::string::npos) return output + "_geometry.obj";
    return output.substr(0, dot) + "_geometry" + output.substr(dot);
}

double argDouble(int argc, char** argv, int index, double fallback) {
    if (argc <= index) return fallback;
    try {
        return std::stod(argv[index]);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <input.step|.iges|.brep|.stl|.obj> [density] [output.obj]\n",
                     argv[0]);
        return 2;
    }

    const std::string input = argv[1];
    const double deflection = argDouble(argc, argv, 2, 0.05);
    const std::string output = (argc > 3) ? argv[3] : "surface_mesh.obj";

    sm::CadOptions options;
    options.deflection = deflection > 0.0 ? deflection : 0.05;
    options.growthRate = argDouble(argc, argv, 6, 1.2);
    options.healTopology = true;
    options.writePreview = true;

    if (const char* cap = std::getenv("SM_MAX_ELEMENTS")) {
        const int v = std::atoi(cap);
        if (v > 1000) options.maxElements = v;
    }

    std::cout << "\n[--- PHASE 1: ENVIRONMENT & INITIALIZATION ---]" << std::endl;
#ifdef _OPENMP
    std::cout << "[SYSTEM] Compute Target   : " << omp_get_max_threads() << " threads" << std::endl;
#else
    std::cout << "[SYSTEM] Compute Target   : serial build" << std::endl;
#endif
    std::cout << "[SYSTEM] Loading Geometry : " << input << std::endl;
    std::cout << "[SYSTEM] Chordal Target   : " << options.deflection << std::endl;
    std::cout << "[SYSTEM] Element Ceiling  : " << options.maxElements << std::endl;

    sm::SurfaceMesh mesh;
    sm::SurfaceMesh preview;
    sm::CadReport report;

    const sm::SourceKind kind = sm::detectSourceKind(input);
    if (kind == sm::SourceKind::Unknown) {
        std::cerr << "[ERROR] Unsupported format. Accepts STEP, IGES, BREP, STL and OBJ."
                  << std::endl;
        return 2;
    }
    std::cout << "[SYSTEM] Source Format    : " << sm::sourceKindName(kind) << std::endl;

    if (sm::isBoundaryRep(kind)) {
        std::cout << "\n[--- PHASE 2: TOPOLOGY AUDIT & HEALING ---]" << std::endl;
        std::cout.flush();

        if (!sm::runCadPipeline(input, options, &mesh, &preview, &report)) {
            std::cerr << "[ERROR] Unable to mesh " << input
                      << ". The file may be unreadable or contain no surfaces." << std::endl;
            return 1;
        }

        std::cout << "  -> Healed shape: " << report.faces << " faces, " << report.edges
                  << " edges (" << report.healMs << " ms)" << std::endl;

        std::cout << "\n[--- PHASE 3: SURFACE DISCRETIZATION ---]" << std::endl;
        std::cout << "  -> Edge discretisation : " << report.discretiseMs << " ms" << std::endl;
        std::cout << "  -> Boundary extraction : " << report.assembleMs << " ms" << std::endl;
        std::cout << "  -> Parallel face mesh  : " << report.meshMs << " ms" << std::endl;
        if (report.failedFaces > 0) {
            std::cout << "  -> Skipped faces       : " << report.failedFaces << std::endl;
        }
    } else {
        // Already triangulated. Saying so plainly matters: the density slider
        // is live in the UI and a user is entitled to wonder why moving it
        // changes nothing for this file.
        std::cout << "\n[--- PHASE 2: MESH IMPORT ---]" << std::endl;
        std::cout << "[SYSTEM] Input is already triangulated. Curvature-adaptive"
                  << " refinement does not apply;" << std::endl;
        std::cout << "         the density setting is ignored and the mesh is"
                  << " passed through as authored." << std::endl;
        std::cout.flush();

        if (!sm::runMeshImport(input, options, &mesh, &report)) {
            std::cerr << "[ERROR] Unable to read " << input
                      << ". The file may be corrupt or contain no triangles." << std::endl;
            return 1;
        }

        std::cout << "  -> Parsed  : " << report.loadMs << " ms" << std::endl;
        std::cout << "  -> Welded  : " << report.assembleMs << " ms" << std::endl;
    }

    std::cout << "\n[--- PHASE 4: GLOBAL MESH AUDIT ---]" << std::endl;
    if (!sm::writeObj(output, mesh, true)) {
        std::cerr << "[ERROR] Could not write " << output << std::endl;
        return 1;
    }
    std::cout << "  -> Mesh written to " << output << std::endl;

    if (options.writePreview && !preview.triangles.empty()) {
        const std::string previewPath = previewPathFor(output);
        if (sm::writeObj(previewPath, preview, false)) {
            std::cout << "  -> Reference geometry written to " << previewPath << std::endl;
        }
    }

    const double totalMs = report.loadMs + report.healMs + report.discretiseMs +
                           report.assembleMs + report.meshMs;

    std::cout << "\n==============================================" << std::endl;
    std::cout << "      GLOBAL MESH QUALITY REPORT              " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "Total Nodes              : " << report.nodes << std::endl;
    std::cout << "Total Elements           : " << report.elements << std::endl;
    std::cout << "Max 3D Skewness Found    : " << report.maxSkewness << std::endl;
    std::cout << "Mean Skewness            : " << report.meanSkewness << std::endl;
    std::cout << "High Skew Elements       : " << report.highSkewCount << std::endl;
    std::cout << "Welded Duplicate Nodes   : " << report.duplicateNodesWelded << std::endl;
    std::cout << "Degenerate Dropped       : " << report.degenerateDropped << std::endl;
    std::cout << "Surface Edge Flips       : " << report.surfaceFlips << std::endl;
    std::cout << "Faces Skipped            : " << report.failedFaces << std::endl;
    std::cout << "Free Edges (watertight)  : " << report.freeEdges << std::endl;
    std::cout << "Non-Manifold Edges       : " << report.nonManifoldEdges << std::endl;
    std::cout << "Total Wall Time          : " << totalMs << " ms" << std::endl;
    std::cout << "==============================================\n" << std::endl;

    return 0;
}

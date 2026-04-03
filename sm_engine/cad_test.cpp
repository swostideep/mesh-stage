#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <future> 
#include <thread>
#include <functional> 
#include "voronoi_core.h"

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>                         
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <BRep_Tool.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Geom2d_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <BRepAdaptor_Curve.hxx>              
#include <Geom2dAdaptor_Curve.hxx>
#include <gp_Pnt2d.hxx>
#include <Geom_Surface.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_Sewing.hxx> 
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepLib.hxx>
#include <TopTools_IndexedMapOfShape.hxx>     
#include <GeomLProp_SLProps.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>

using namespace std;

// FIX #11: PointCompare with tolerance bands violates std::map's strict-weak-ordering
// requirement: A < B and B < A can both be false for A != B, causing silent map corruption
// (undefined behaviour). Solution: snap each coordinate to a coarse grid first, then
// compare exactly. The grid size matches the original 1e-2 merge tolerance.
static constexpr double SNAP_TOL = 1e-2;

struct SnapKey {
    long long xi, yi, zi;
    SnapKey(const gp_Pnt& p)
        : xi(static_cast<long long>(std::round(p.X() / SNAP_TOL))),
          yi(static_cast<long long>(std::round(p.Y() / SNAP_TOL))),
          zi(static_cast<long long>(std::round(p.Z() / SNAP_TOL))) {}
    bool operator==(const SnapKey& o) const { return xi==o.xi && yi==o.yi && zi==o.zi; }
    bool operator<(const SnapKey& o)  const {
        if (xi != o.xi) return xi < o.xi;
        if (yi != o.yi) return yi < o.yi;
        return zi < o.zi;
    }
};

struct SnapKeyHash {
    size_t operator()(const SnapKey& k) const noexcept {
        size_t h = static_cast<size_t>(k.xi);
        h ^= static_cast<size_t>(k.yi) * 2654435761ULL + 0x9e3779b9ULL + (h<<6) + (h>>2);
        h ^= static_cast<size_t>(k.zi) * 2246822519ULL + 0x9e3779b9ULL + (h<<6) + (h>>2);
        return h;
    }
};

// GlobalNodeMap — snap-keyed hash map for O(1) 3D vertex deduplication.
// Using SnapKey instead of a tolerance-band comparator ensures the map satisfies
// strict-weak-ordering (a requirement std::map enforces with UB if violated).
using GlobalNodeMap = unordered_map<SnapKey, int, SnapKeyHash>;

// registerOrFindGlobalVertex
// Looks up a 3D point p in the global node registry. If found, returns its existing
// index. If not found, appends it to uniqueNodes, records it in globalNodes, and
// returns the new index. Deduplication uses SnapKey (grid snap to 1e-2) so nearby
// points on shared CAD edges are merged into a single shared node.
int registerOrFindGlobalVertex(const gp_Pnt& p, GlobalNodeMap& globalNodes, vector<gp_Pnt>& uniqueNodes) {
    SnapKey key(p);
    auto it = globalNodes.find(key);
    if (it != globalNodes.end()) return it->second;
    int idx = (int)uniqueNodes.size();
    uniqueNodes.push_back(p);
    globalNodes[key] = idx;
    return idx;
}

struct FaceMeshResult {
    bool success = false;
    vector<gp_Pnt> localNodes;
    vector<vector<int>> localTriangles;
};

// compute3DSkewness
// Computes ANSYS NES skewness for a triangle defined by three 3D points (gp_Pnt).
// Uses the same formula as computeNESSkewness in voronoi_core but operates on
// 3D Euclidean distances rather than 2D integer-grid coordinates.
// acos arguments are clamped to [-1,1] to prevent NaN on near-degenerate triangles.
double compute3DSkewness(gp_Pnt a, gp_Pnt b, gp_Pnt c) {
    double L1 = b.Distance(c), L2 = a.Distance(c), L3 = a.Distance(b);
    auto safeCos = [](double num, double den) -> double {
        if (den < 1e-12) return 0.0;
        return std::max(-1.0, std::min(1.0, num / den));
    };
    double angleA = std::acos(safeCos(L2*L2 + L3*L3 - L1*L1, 2.0*L2*L3)) * (180.0 / M_PI);
    double angleB = std::acos(safeCos(L1*L1 + L3*L3 - L2*L2, 2.0*L1*L3)) * (180.0 / M_PI);
    double angleC = 180.0 - angleA - angleB;
    double minAngle = std::min({angleA, angleB, angleC});
    double maxAngle = std::max({angleA, angleB, angleC});
    double maxSkew = (maxAngle - 60.0) / (180.0 - 60.0);
    double minSkew = (60.0 - minAngle) / 60.0;
    return std::max(maxSkew, minSkew);
}

// computeCurvatureAdaptiveSize
// Queries OpenCASCADE surface curvature at parameter (u, v) and returns a target
// edge length scaled inversely with curvature magnitude:
//   targetSize = baseSize / (1 + alpha * curvature)
// The result is clamped to a minimum of 15% of baseSize so highly curved regions
// don't produce infinitesimally small triangles. Falls back to baseSize when
// curvature is undefined (planar or degenerate surface patches).
double computeCurvatureAdaptiveSize(Handle(Geom_Surface) surf, double u, double v, double baseSize) {
    GeomLProp_SLProps props(surf, u, v, 2, 1e-4);
    if (!props.IsCurvatureDefined()) return baseSize; 
    double k1 = props.MaxCurvature(); double k2 = props.MinCurvature();
    double curvatureMagnitude = std::max(std::abs(k1), std::abs(k2));
    double alpha = 5.0;              
    double minSize = baseSize * 0.15; 
    double targetSize = baseSize / (1.0 + alpha * curvatureMagnitude);
    return std::max(targetSize, minSize); 
}

// meshSingleFace
// Triangulates one CAD face and returns a FaceMeshResult containing:
//   localNodes     — 3D positions of all mesh vertices on this face
//   localTriangles — index triples referencing localNodes
// Pipeline:
//   1. Samples the face boundary edges using pre-computed globalEdgeSeeds parameters.
//   2. Scales UV coordinates to integer space [0, 2000] and deduplicates with a
//      hash map (O(1) per point).
//   3. Calls buildConstrainedDelaunayMesh with a curvature-adaptive sizing callback.
//   4. Filters exterior triangles using BRepClass_FaceClassifier.
//   5. Maps 2D UV triangles back to 3D via Geom_Surface::Value.
// Thread-safe: reads edgeMap and globalEdgeSeeds by const-ref, all output is local.
FaceMeshResult meshSingleFace(TopoDS_Face face, double meshDensity,
                              const TopTools_IndexedMapOfShape& edgeMap,
                              const vector<vector<double>>& globalEdgeSeeds) {
    FaceMeshResult result;
    Standard_Real uMin, uMax, vMin, vMax;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    double uRange = max(uMax - uMin, 1e-6); double vRange = max(vMax - vMin, 1e-6);

    vector<vector<int>> inputPoints;
    vector<pair<int, int>> boundarySegments;
    // FIX #12: hash map for O(1) UV point deduplication (keyed on packed uint64 UV)
    unordered_map<uint64_t, int> uvIndexMap;
    TopExp_Explorer edgeExplorer(face, TopAbs_EDGE);

    while (edgeExplorer.More()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExplorer.Current());
        Standard_Real first, last;
        Handle(Geom2d_Curve) curve2d = BRep_Tool::CurveOnSurface(edge, face, first, last);

        if (!curve2d.IsNull() && !BRep_Tool::Degenerated(edge)) {
            int edgeId = edgeMap.FindIndex(edge);
            const vector<double>& t_params = globalEdgeSeeds[edgeId];
            bool isReversed = (edge.Orientation() == TopAbs_REVERSED);
            int prevIdx = -1;

            for (size_t i = 0; i < t_params.size(); i++) {
                double t = isReversed ? t_params[t_params.size() - 1 - i] : t_params[i];
                if (t < first) t = first; if (t > last) t = last;

                gp_Pnt2d p2d = curve2d->Value(t);
                int scaledU = (int)((p2d.X() - uMin) / uRange * 2000.0);
                int scaledV = (int)((p2d.Y() - vMin) / vRange * 2000.0);
                // Clamp to [0, 2000] to avoid out-of-range keys
                scaledU = std::max(0, std::min(2000, scaledU));
                scaledV = std::max(0, std::min(2000, scaledV));

                // FIX #12: O(1) hash lookup instead of O(n) linear scan.
                // Pack (u, v) into a single 64-bit key; both fit in 16 bits (0..2000).
                uint64_t uvKey = (static_cast<uint64_t>(scaledU) << 16) |
                                  static_cast<uint64_t>(scaledV);
                int currIdx = -1;
                auto hit = uvIndexMap.find(uvKey);
                if (hit != uvIndexMap.end()) {
                    currIdx = hit->second;
                } else {
                    currIdx = (int)inputPoints.size();
                    inputPoints.push_back({scaledU, scaledV});
                    uvIndexMap[uvKey] = currIdx;
                }
                if (prevIdx != -1 && prevIdx != currIdx) boundarySegments.push_back({prevIdx, currIdx});
                prevIdx = currIdx;
            }
        }
        edgeExplorer.Next();
    }

    if (inputPoints.size() >= 3) {
        vector<Triangle> triangles; vector<vector<int>> pointsOut;
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        
        auto adaptiveSizing = [&](double normU, double normV) -> double {
            double actualU = uMin + (normU * uRange); double actualV = vMin + (normV * vRange);
            double targetEdgeLength3D = computeCurvatureAdaptiveSize(surf, actualU, actualV, meshDensity);
            double maxRange = max(uRange, vRange);
            double scaledEdge = (targetEdgeLength3D / maxRange) * 2000.0;
            return (scaledEdge * scaledEdge) * 0.5;
        };

        buildConstrainedDelaunayMesh(inputPoints, boundarySegments, triangles, pointsOut, adaptiveSizing);
        BRepClass_FaceClassifier classifier;
        // FIX #11 (local): Use snap-key hash map for local 3D vertex deduplication
        unordered_map<SnapKey, int, SnapKeyHash> localMap;

        for (const auto& tri : triangles) {
            if (!tri.active || tri.isExterior) continue;

            double u_mid_norm = (pointsOut[tri.v[0]][0] + pointsOut[tri.v[1]][0] + pointsOut[tri.v[2]][0]) / 6000.0;
            double v_mid_norm = (pointsOut[tri.v[0]][1] + pointsOut[tri.v[1]][1] + pointsOut[tri.v[2]][1]) / 6000.0;
            double u_actual = uMin + (u_mid_norm * uRange); double v_actual = vMin + (v_mid_norm * vRange);

            classifier.Perform(face, gp_Pnt2d(u_actual, v_actual), 1e-4);
            if (classifier.State() == TopAbs_OUT) continue;

            vector<int> triIndices;
            for(int i=0; i<3; i++) {
                double u_norm = pointsOut[tri.v[i]][0] / 2000.0; double v_norm = pointsOut[tri.v[i]][1] / 2000.0;
                gp_Pnt p3d = surf->Value(uMin + u_norm * uRange, vMin + v_norm * vRange);

                SnapKey sk(p3d);
                auto it = localMap.find(sk);
                if(it != localMap.end()) { triIndices.push_back(it->second); }
                else {
                    int localIdx = (int)result.localNodes.size(); result.localNodes.push_back(p3d);
                    localMap[sk] = localIdx; triIndices.push_back(localIdx);
                }
            }
            result.localTriangles.push_back(triIndices);
        }
        result.success = true;
    }
    return result;
}

int main(int argc, char* argv[]) {
    string inputFile = (argc > 1) ? argv[1] : "model.step";
    double meshDensity = (argc > 2) ? stod(argv[2]) : 0.05;
    string outputFile = (argc > 3) ? argv[3] : "true_cad_mesh.obj"; 
    string ext = inputFile.substr(inputFile.find_last_of(".") + 1);
    TopoDS_Shape rawShape;

    cout << "\n[--- PHASE 1: ENVIRONMENT & INITIALIZATION ---]" << endl;
    cout << "[SYSTEM] Initialize SM Surface Mesher Engine Visual Core" << endl;
    cout << "[SYSTEM] Hardware Target : " << thread::hardware_concurrency() << " Cores Allocated" << endl;
    cout << "[SYSTEM] Loading Geometry: " << inputFile << endl;
    cout << "[SYSTEM] Visual Target   : Deflection Density " << meshDensity << endl;

    if (ext == "step" || ext == "stp") {
        STEPControl_Reader reader;
        if(reader.ReadFile(inputFile.c_str()) != IFSelect_RetDone) return 1;
        reader.TransferRoots(); rawShape = reader.OneShape();
    } else if (ext == "iges" || ext == "igs") {
        IGESControl_Reader reader;
        if(reader.ReadFile(inputFile.c_str()) != IFSelect_RetDone) return 1;
        reader.TransferRoots(); rawShape = reader.OneShape();
    }

    cout << "\n[--- PHASE 2: TOPOLOGY AUDIT & HEALING ---]" << endl;
    ShapeUpgrade_UnifySameDomain unifier(rawShape, true, true, true);
    unifier.Build(); TopoDS_Shape unifiedShape = unifier.Shape();

    Handle(ShapeFix_Shape) shapeFix = new ShapeFix_Shape(unifiedShape);
    shapeFix->SetPrecision(1e-4); shapeFix->SetMinTolerance(1e-5); shapeFix->SetMaxTolerance(1e-2); 
    shapeFix->Perform(); TopoDS_Shape fixedShape = shapeFix->Shape();

    BRepBuilderAPI_Sewing sewer(1e-3); sewer.Add(fixedShape); sewer.Perform();
    TopoDS_Shape cadModel = sewer.SewedShape();
    BRepLib::BuildCurves3d(cadModel);

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(cadModel, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(cadModel, TopAbs_EDGE, edgeMap);
    
    vector<TopoDS_Face> allFaces;
    for (TopExp_Explorer ex(cadModel, TopAbs_FACE); ex.More(); ex.Next()) { allFaces.push_back(TopoDS::Face(ex.Current())); }
    int totalFaces = allFaces.size();

    cout << "  -> Extracted " << totalFaces << " Valid Faces, " << edgeMap.Extent() << " Edges." << endl;
    
    vector<vector<double>> globalEdgeSeeds(edgeMap.Extent() + 1);
    for (int i = 1; i <= edgeMap.Extent(); i++) {
        TopoDS_Edge edge = TopoDS::Edge(edgeMap(i));
        if (BRep_Tool::Degenerated(edge)) continue; 
        BRepAdaptor_Curve curve3d(edge);
        GCPnts_QuasiUniformDeflection discretizer(curve3d, meshDensity);
        if (discretizer.IsDone()) {
            for (int j = 1; j <= discretizer.NbPoints(); j++) { globalEdgeSeeds[i].push_back(discretizer.Parameter(j)); }
        }
    }

    cout << "\n[--- PHASE 3: SURFACE DISCRETIZATION ---]" << endl;
    GlobalNodeMap globalNodes;
    vector<gp_Pnt> uniqueNodes; map<int, vector<vector<int>>> masterTrianglesByFace;

    int batchSize = thread::hardware_concurrency(); if (batchSize < 4) batchSize = 4; 
    int facesProcessed = 0;

    for (int i = 0; i < totalFaces; i += batchSize) {
        vector<future<FaceMeshResult>> futures;
        int currentBatchSize = min(batchSize, totalFaces - i);
        
        for (int j = 0; j < currentBatchSize; j++) {
            futures.push_back(async(launch::async, meshSingleFace, allFaces[i + j], meshDensity, ref(edgeMap), ref(globalEdgeSeeds)));
        }

        for (auto& f : futures) {
            FaceMeshResult res = f.get(); facesProcessed++; int facesLeft = totalFaces - facesProcessed;
            cout << "> Processing Face " << facesProcessed << " of " << totalFaces << " (" << facesLeft << " pending)..." << endl;
            if (res.success) {
                vector<int> localToGlobalMap;
                for (const auto& p : res.localNodes) { localToGlobalMap.push_back(registerOrFindGlobalVertex(p, globalNodes, uniqueNodes)); }
                for (const auto& tri : res.localTriangles) { masterTrianglesByFace[facesProcessed].push_back({localToGlobalMap[tri[0]], localToGlobalMap[tri[1]], localToGlobalMap[tri[2]]}); }
            }
        }
    }

    cout << "\n[--- PHASE 4: GLOBAL MESH AUDIT ---]" << endl;
    vector<double> vertexSkew(uniqueNodes.size(), 0.0); vector<int> vertexTriCount(uniqueNodes.size(), 0);
    double globalMaxSkew = 0.0; int badElements = 0; int totalTriangles = 0; 

    for(const auto& facePair : masterTrianglesByFace) {
        for(const auto& t : facePair.second) {
            totalTriangles++;
            double skew = compute3DSkewness(uniqueNodes[t[0]], uniqueNodes[t[1]], uniqueNodes[t[2]]);
            if (skew > globalMaxSkew) globalMaxSkew = skew;
            if (skew > 0.5) badElements++;
            for (int i=0; i<3; i++) { vertexSkew[t[i]] += skew; vertexTriCount[t[i]]++; }
        }
    }

    ofstream obj(outputFile);
    for(size_t i = 0; i < uniqueNodes.size(); i++) {
        double avgSkew = (vertexTriCount[i] > 0) ? (vertexSkew[i] / vertexTriCount[i]) : 0.0;
        double r = 0.0, g = 0.0, b = 1.0; 
        if (avgSkew < 0.25) { r = 0.0; g = avgSkew * 4.0; b = 1.0; } else if (avgSkew < 0.5) { r = 0.0; g = 1.0; b = 1.0 - ((avgSkew - 0.25) * 4.0); }
        else if (avgSkew < 0.75) { r = (avgSkew - 0.5) * 4.0; g = 1.0; b = 0.0; } else { r = 1.0; g = 1.0 - ((avgSkew - 0.75) * 4.0); b = 0.0; }
        obj << "v " << uniqueNodes[i].X() << " " << uniqueNodes[i].Y() << " " << uniqueNodes[i].Z() << " " << r << " " << g << " " << b << "\n";
    }
    for(const auto& facePair : masterTrianglesByFace) {
        obj << "g Surface_" << facePair.first << "\n";
        for(const auto& t : facePair.second) { obj << "f " << t[0]+1 << " " << t[1]+1 << " " << t[2]+1 << "\n"; }
    }
    obj.close();

    cout << "\n==============================================" << endl;
    cout << "      GLOBAL MESH QUALITY REPORT              " << endl;
    cout << "==============================================" << endl;
    cout << "Total Nodes              : " << uniqueNodes.size() << endl;
    cout << "Total Elements           : " << totalTriangles << endl;
    cout << "Max 3D Skewness Found    : " << globalMaxSkew << endl;
    cout << "==============================================\n" << endl;
    return 0;
}
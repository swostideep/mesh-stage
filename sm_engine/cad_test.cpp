#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <map>
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

struct PointCompare {
    bool operator()(const gp_Pnt& p1, const gp_Pnt& p2) const {
        double tol = 1e-2; 
        if (abs(p1.X() - p2.X()) > tol) return p1.X() < p2.X();
        if (abs(p1.Y() - p2.Y()) > tol) return p1.Y() < p2.Y();
        if (abs(p1.Z() - p2.Z()) > tol) return p1.Z() < p2.Z();
        return false;
    }
};

int getGlobalVertex(gp_Pnt p, map<gp_Pnt, int, PointCompare>& globalNodes, vector<gp_Pnt>& uniqueNodes) {
    auto it = globalNodes.find(p);
    if (it != globalNodes.end()) return it->second;
    int idx = uniqueNodes.size();
    uniqueNodes.push_back(p);
    globalNodes[p] = idx;
    return idx;
}

struct FaceMeshResult {
    bool success = false;
    vector<gp_Pnt> localNodes;
    vector<vector<int>> localTriangles;
};

double get3DTriangleSkewness(gp_Pnt a, gp_Pnt b, gp_Pnt c) {
    double L1 = b.Distance(c); double L2 = a.Distance(c); double L3 = a.Distance(b); 
    double angleA = acos((L2*L2 + L3*L3 - L1*L1) / (2.0 * L2 * L3)) * (180.0 / M_PI);
    double angleB = acos((L1*L1 + L3*L3 - L2*L2) / (2.0 * L1 * L3)) * (180.0 / M_PI);
    double angleC = 180.0 - angleA - angleB;
    double minAngle = std::min({angleA, angleB, angleC});
    double maxAngle = std::max({angleA, angleB, angleC});
    double maxSkew = (maxAngle - 60.0) / (180.0 - 60.0);
    double minSkew = (60.0 - minAngle) / 60.0;
    return std::max(maxSkew, minSkew);
}

double getLocalMeshSize(Handle(Geom_Surface) surf, double u, double v, double baseSize) {
    GeomLProp_SLProps props(surf, u, v, 2, 1e-4);
    if (!props.IsCurvatureDefined()) return baseSize; 
    double k1 = props.MaxCurvature(); double k2 = props.MinCurvature();
    double curvatureMagnitude = std::max(std::abs(k1), std::abs(k2));
    double alpha = 5.0;              
    double minSize = baseSize * 0.15; 
    double targetSize = baseSize / (1.0 + alpha * curvatureMagnitude);
    return std::max(targetSize, minSize); 
}

FaceMeshResult processFace(TopoDS_Face face, double meshDensity, 
                           const TopTools_IndexedMapOfShape& edgeMap, 
                           const vector<vector<double>>& globalEdgeSeeds) {
    FaceMeshResult result;
    Standard_Real uMin, uMax, vMin, vMax;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    double uRange = max(uMax - uMin, 1e-6); double vRange = max(vMax - vMin, 1e-6);

    vector<vector<int>> inputPoints;
    vector<pair<int, int>> boundarySegments;
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
                
                int currIdx = -1;
                for(int j=0; j<(int)inputPoints.size(); j++) {
                    if(abs(inputPoints[j][0]-scaledU) < 2 && abs(inputPoints[j][1]-scaledV) < 2) { currIdx = j; break; }
                }
                if(currIdx == -1) { inputPoints.push_back({scaledU, scaledV}); currIdx = inputPoints.size() - 1; }
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
            double targetEdgeLength3D = getLocalMeshSize(surf, actualU, actualV, meshDensity);
            double maxRange = max(uRange, vRange);
            double scaledEdge = (targetEdgeLength3D / maxRange) * 2000.0; 
            return (scaledEdge * scaledEdge) * 0.5; 
        };

        computeDelaunay(inputPoints, boundarySegments, triangles, pointsOut, adaptiveSizing);
        BRepClass_FaceClassifier classifier; map<gp_Pnt, int, PointCompare> localMap; 

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
                
                auto it = localMap.find(p3d);
                if(it != localMap.end()) { triIndices.push_back(it->second); } 
                else {
                    int localIdx = result.localNodes.size(); result.localNodes.push_back(p3d);
                    localMap[p3d] = localIdx; triIndices.push_back(localIdx);
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
    map<gp_Pnt, int, PointCompare> globalNodes;
    vector<gp_Pnt> uniqueNodes; map<int, vector<vector<int>>> masterTrianglesByFace;

    int batchSize = thread::hardware_concurrency(); if (batchSize < 4) batchSize = 4; 
    int facesProcessed = 0;

    for (int i = 0; i < totalFaces; i += batchSize) {
        vector<future<FaceMeshResult>> futures;
        int currentBatchSize = min(batchSize, totalFaces - i);
        
        for (int j = 0; j < currentBatchSize; j++) {
            futures.push_back(async(launch::async, processFace, allFaces[i + j], meshDensity, ref(edgeMap), ref(globalEdgeSeeds)));
        }

        for (auto& f : futures) {
            FaceMeshResult res = f.get(); facesProcessed++; int facesLeft = totalFaces - facesProcessed;
            cout << "> Processing Face " << facesProcessed << " of " << totalFaces << " (" << facesLeft << " pending)..." << endl;
            if (res.success) {
                vector<int> localToGlobalMap;
                for (const auto& p : res.localNodes) { localToGlobalMap.push_back(getGlobalVertex(p, globalNodes, uniqueNodes)); }
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
            double skew = get3DTriangleSkewness(uniqueNodes[t[0]], uniqueNodes[t[1]], uniqueNodes[t[2]]);
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
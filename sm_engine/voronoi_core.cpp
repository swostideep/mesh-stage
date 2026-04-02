#include "voronoi_core.h"
#include <map>
#include <iostream>
#include <queue>
#include <fstream>
#include <functional> 

typedef __int128_t int128;

long long orient2d(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    int128 val = (int128)(b[0] - a[0]) * (c[1] - a[1]) - (int128)(b[1] - a[1]) * (c[0] - a[0]);
    if (val > 0)
        return 1;
    if (val < 0)
        return -1;
    return 0;
}

bool incircle(const vector<int> &a, const vector<int> &b, const vector<int> &c, const vector<int> &d)
{
    int128 adx = a[0] - d[0], ady = a[1] - d[1], bdx = b[0] - d[0], bdy = b[1] - d[1], cdx = c[0] - d[0], cdy = c[1] - d[1];
    int128 abdet = adx * bdy - bdx * ady, bcdet = bdx * cdy - cdx * bdy, cadet = cdx * ady - adx * cdy;
    int128 alift = adx * adx + ady * ady, blift = bdx * bdx + bdy * bdy, clift = cdx * cdx + cdy * cdy;
    return (alift * bcdet + blift * cadet + clift * abdet) > 0;
}

Point2D getCircumcenter(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    double ax = a[0], ay = a[1], bx = b[0], by = b[1], cx = c[0], cy = c[1];
    double D = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(D) < 1e-9)
        return {(ax + bx + cx) / 3.0, (ay + by + cy) / 3.0};
    double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / D;
    double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / D;
    return {ux, uy};
}

// Shoelace formula for Triangle Area
double getTriangleArea(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    return std::abs((a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1])) / 2.0);
}

// Calculates the Radius-Edge Ratio (B = R / L_min)
double getRadiusEdgeRatio(const vector<int> &a, const vector<int> &b, const vector<int> &c)
{
    double L1 = std::sqrt(std::pow(a[0] - b[0], 2) + std::pow(a[1] - b[1], 2));
    double L2 = std::sqrt(std::pow(b[0] - c[0], 2) + std::pow(b[1] - c[1], 2));
    double L3 = std::sqrt(std::pow(c[0] - a[0], 2) + std::pow(c[1] - a[1], 2));

    double L_min = std::min({L1, L2, L3});
    double area = getTriangleArea(a, b, c);

    if (area < 1e-5)
        return 9999.0; // Degenerate triangle safety

    // R = (abc) / (4 * area)
    double R = (L1 * L2 * L3) / (4.0 * area);

    return R / L_min;
}
// ================= PHASE 4: FEA QUALITY METRICS =================

// Helper to calculate the 3 interior angles of a triangle in degrees
vector<double> getTriangleAngles(const vector<int> &a, const vector<int> &b, const vector<int> &c) {
    double L1 = std::sqrt(std::pow(b[0] - c[0], 2) + std::pow(b[1] - c[1], 2)); // Edge opposite to A
    double L2 = std::sqrt(std::pow(a[0] - c[0], 2) + std::pow(a[1] - c[1], 2)); // Edge opposite to B
    double L3 = std::sqrt(std::pow(a[0] - b[0], 2) + std::pow(a[1] - b[1], 2)); // Edge opposite to C

    // Law of Cosines
    double angleA = std::acos((L2*L2 + L3*L3 - L1*L1) / (2.0 * L2 * L3)) * (180.0 / M_PI);
    double angleB = std::acos((L1*L1 + L3*L3 - L2*L2) / (2.0 * L1 * L3)) * (180.0 / M_PI);
    double angleC = 180.0 - angleA - angleB;

    return {angleA, angleB, angleC};
}

// Calculates ANSYS-Standard Normalized Equiangular Skewness (NES)
// 0.0 = Perfect Equilateral, 1.0 = Completely Degenerate/Flat
double getSkewness(const vector<int> &a, const vector<int> &b, const vector<int> &c) {
    vector<double> angles = getTriangleAngles(a, b, c);
    double minAngle = std::min({angles[0], angles[1], angles[2]});
    double maxAngle = std::max({angles[0], angles[1], angles[2]});

    double maxSkew = (maxAngle - 60.0) / (180.0 - 60.0);
    double minSkew = (60.0 - minAngle) / 60.0;

    return std::max(maxSkew, minSkew);
}

void insertPoint(int pIdx, const vector<vector<int>> &pointsOut, vector<Triangle> &triangulation)
{
    vector<Edge> polygon;
    for (auto &tri : triangulation)
    {
        if (tri.active && incircle(pointsOut[tri.v[0]], pointsOut[tri.v[1]], pointsOut[tri.v[2]], pointsOut[pIdx]))
        {
            tri.active = false;
            polygon.push_back({tri.v[0], tri.v[1]});
            polygon.push_back({tri.v[1], tri.v[2]});
            polygon.push_back({tri.v[2], tri.v[0]});
        }
    }
    for (size_t j = 0; j < polygon.size(); j++)
    {
        bool shared = false;
        for (size_t k = 0; k < polygon.size(); k++)
        {
            if (j != k && polygon[j] == polygon[k])
            {
                shared = true;
                break;
            }
        }
        if (!shared)
            triangulation.emplace_back(polygon[j].v1, polygon[j].v2, pIdx);
    }
}

bool edgeExists(int u, int v, const vector<Triangle> &tri)
{
    for (const auto &t : tri)
    {
        if (!t.active)
            continue;
        if ((t.v[0] == u && t.v[1] == v) || (t.v[1] == u && t.v[0] == v) || (t.v[1] == u && t.v[2] == v) || (t.v[2] == u && t.v[1] == v) || (t.v[2] == u && t.v[0] == v) || (t.v[0] == u && t.v[2] == v))
            return true;
    }
    return false;
}

// HELPER: Rebuilds adjacency and domain tags
void updateTopologyAndDomain(int N, const vector<pair<int, int>> &currentSegments, vector<Triangle> &triangulation)
{
    std::map<EdgeKey, std::pair<Triangle *, int>> edgeMap;
    for (auto &tri : triangulation)
    {
        if (!tri.active)
            continue;
        tri.isExterior = false; // Reset
        for (int i = 0; i < 3; i++)
        {
            tri.adj[i] = nullptr;
            tri.isConstrained[i] = false;
            EdgeKey ek(tri.v[i], tri.v[(i + 1) % 3]);
            if (edgeMap.count(ek))
            {
                Triangle *other = edgeMap[ek].first;
                int otherEdgeIdx = edgeMap[ek].second;
                tri.adj[i] = other;
                other->adj[otherEdgeIdx] = &tri;
            }
            else
                edgeMap[ek] = {&tri, i};
        }
    }

    for (auto &tri : triangulation)
    {
        if (!tri.active)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int u = tri.v[i], v = tri.v[(i + 1) % 3];
            for (const auto &seg : currentSegments)
            {
                if ((seg.first == u && seg.second == v) || (seg.first == v && seg.second == u))
                {
                    tri.isConstrained[i] = true;
                    break;
                }
            }
        }
    }

    std::vector<Triangle *> floodQueue;
    for (auto &tri : triangulation)
    {
        if (!tri.active)
            continue;
        if (tri.v[0] == N || tri.v[1] == N || tri.v[2] == N || tri.v[0] == N + 1 || tri.v[1] == N + 1 || tri.v[2] == N + 1 || tri.v[0] == N + 2 || tri.v[1] == N + 2 || tri.v[2] == N + 2)
        {
            tri.isExterior = true;
            floodQueue.push_back(&tri);
        }
    }
    int head = 0;
    while (head < floodQueue.size())
    {
        Triangle *curr = floodQueue[head++];
        for (int i = 0; i < 3; i++)
        {
            if (curr->adj[i] != nullptr && curr->adj[i]->active && !curr->isConstrained[i] && !curr->adj[i]->isExterior)
            {
                curr->adj[i]->isExterior = true;
                floodQueue.push_back(curr->adj[i]);
            }
        }
    }
}

// The Auto-Repair Tool
void enforceSegments(vector<pair<int, int>> &currentSegments, vector<vector<int>> &pointsOut, vector<Triangle> &triangulation)
{
    bool allSegmentsExist = false;
    int splitSafetyLimit = 2000;

    while (!allSegmentsExist && splitSafetyLimit-- > 0)
    {
        allSegmentsExist = true;
        vector<pair<int, int>> nextSegments;
        for (auto &seg : currentSegments)
        {
            double distSq = std::pow(pointsOut[seg.first][0] - pointsOut[seg.second][0], 2) + 
                            std::pow(pointsOut[seg.first][1] - pointsOut[seg.second][1], 2);

            if (!edgeExists(seg.first, seg.second, triangulation) && distSq > 4.0)
            {
                allSegmentsExist = false;
                int mx = (pointsOut[seg.first][0] + pointsOut[seg.second][0]) / 2;
                int my = (pointsOut[seg.first][1] + pointsOut[seg.second][1]) / 2;
                
                pointsOut.push_back({mx, my});
                int mIdx = pointsOut.size() - 1;
                insertPoint(mIdx, pointsOut, triangulation);
                
                nextSegments.push_back({seg.first, mIdx});
                nextSegments.push_back({mIdx, seg.second});
            }
            else
            {
                nextSegments.push_back(seg);
            }
        }
        currentSegments = nextSegments;
    }
}

// ================= PHASE 5: SMART LAPLACIAN SMOOTHING =================
void smoothMesh(vector<vector<int>>& pointsOut, vector<Triangle>& triangulation, int numOriginalPoints) {
    int numPasses = 3; 

    vector<bool> isLocked(pointsOut.size(), false);
    for (int i = 0; i < numOriginalPoints; i++) isLocked[i] = true;

    for (const auto& tri : triangulation) {
        if (!tri.active || tri.isExterior) continue;
        for (int i = 0; i < 3; i++) {
            if (tri.isConstrained[i]) {
                isLocked[tri.v[i]] = true;
                isLocked[tri.v[(i + 1) % 3]] = true;
            }
        }
    }

    for (int pass = 0; pass < numPasses; pass++) {
        vector<double> sumX(pointsOut.size(), 0.0);
        vector<double> sumY(pointsOut.size(), 0.0);
        vector<int> count(pointsOut.size(), 0);

        for (const auto& tri : triangulation) {
            if (!tri.active || tri.isExterior) continue;
            for (int i = 0; i < 3; i++) {
                int u = tri.v[i];
                int v = tri.v[(i + 1) % 3];
                
                sumX[u] += pointsOut[v][0]; sumY[u] += pointsOut[v][1]; count[u]++;
                sumX[v] += pointsOut[u][0]; sumY[v] += pointsOut[u][1]; count[v]++;
            }
        }

        for (size_t i = 0; i < pointsOut.size(); i++) {
            if (!isLocked[i] && count[i] > 0) {
                int newX = sumX[i] / count[i];
                int newY = sumY[i] / count[i];
                
                bool safeToMove = true;
                for (const auto& tri : triangulation) {
                    if (!tri.active || tri.isExterior) continue;
                    if (tri.v[0] == i || tri.v[1] == i || tri.v[2] == i) {
                        vector<int> a = (tri.v[0] == i) ? vector<int>{newX, newY} : pointsOut[tri.v[0]];
                        vector<int> b = (tri.v[1] == i) ? vector<int>{newX, newY} : pointsOut[tri.v[1]];
                        vector<int> c = (tri.v[2] == i) ? vector<int>{newX, newY} : pointsOut[tri.v[2]];
                        
                        if (orient2d(a, b, c) <= 0) {
                            safeToMove = false;
                            break;
                        }
                    }
                }

                if (safeToMove) {
                    pointsOut[i][0] = newX;
                    pointsOut[i][1] = newY;
                }
            }
        }
    }
}


double getTargetArea(int x, int y, int midX, int midY, double globalMaxArea) {
    double distSq = std::pow(x - midX, 2) + std::pow(y - midY, 2);
    double maxDistSq = std::pow(400.0, 2); 
    double normalizedDist = std::min(distSq / maxDistSq, 1.0);
    double minAreaLimit = 200.0; 
    return minAreaLimit + (globalMaxArea - minAreaLimit) * normalizedDist;
}


void computeDelaunay(const vector<vector<int>>& inPoints, 
                     const vector<pair<int, int>>& segments, 
                     vector<Triangle>& triangulation, 
                     vector<vector<int>>& pointsOut,
                     std::function<double(double, double)> sizeFunction)
{
    triangulation.clear();
    if (inPoints.empty())
        return;
    int N = inPoints.size();
    pointsOut = inPoints;

    int minX = pointsOut[0][0], minY = pointsOut[0][1], maxX = minX, maxY = minY;
    for (const auto &p : inPoints)
    {
        minX = std::min(minX, p[0]);
        maxX = std::max(maxX, p[0]);
        minY = std::min(minY, p[1]);
        maxY = std::max(maxY, p[1]);
    }
    int dx = maxX - minX, dy = maxY - minY, dmax = std::max(dx, dy), midX = (minX + maxX) / 2, midY = (minY + maxY) / 2;

    pointsOut.push_back({midX - 20 * dmax, midY - dmax});
    pointsOut.push_back({midX + 20 * dmax, midY - dmax});
    pointsOut.push_back({midX, midY + 20 * dmax});
    triangulation.emplace_back(N, N + 1, N + 2);

    for (int i = 0; i < N; i++)
        insertPoint(i, pointsOut, triangulation);

    vector<pair<int, int>> currentSegments = segments;
    enforceSegments(currentSegments, pointsOut, triangulation);

    double MAX_AREA_ALLOWED = 2000.0;
    double MAX_RATIO_ALLOWED = 1.414; 
    int steinerPointsAdded = 0;
    int maxSteinerLimit = 1500;

    while (steinerPointsAdded < maxSteinerLimit)
    {
        updateTopologyAndDomain(N, currentSegments, triangulation);

        int worstTriIdx = -1;
        bool splitForAngle = false;
        double maxAreaViolation = 1.0;
        double maxRatioFound = MAX_RATIO_ALLOWED;

        for (int i = 0; i < triangulation.size(); i++)
        {
            if (!triangulation[i].active || triangulation[i].isExterior)
                continue;
            
            const auto &a = pointsOut[triangulation[i].v[0]];
            const auto &b = pointsOut[triangulation[i].v[1]];
            const auto &c = pointsOut[triangulation[i].v[2]];

            double area = getTriangleArea(a, b, c);
            
            if (area < 150.0) 
                continue;

            double ratio = getRadiusEdgeRatio(a, b, c);
            
            if (ratio > maxRatioFound)
            {
                maxRatioFound = ratio;
                worstTriIdx = i;
                splitForAngle = true;
            }
            else if (!splitForAngle)
            { 
                // --- THE CALLBACK BRIDGE ---
                int triCenterX = (a[0] + b[0] + c[0]) / 3;
                int triCenterY = (a[1] + b[1] + c[1]) / 3;
                
                double targetArea = MAX_AREA_ALLOWED;

                // Check if the OpenCASCADE callback was provided
                if (sizeFunction) {
                    // Convert integer scale (2000.0) back to 0.0 - 1.0 Normalized Scale
                    double normU = (double)triCenterX / 2000.0;
                    double normV = (double)triCenterY / 2000.0;
                    
                    // Call OpenCASCADE to get the true CAD Curvature at this exact spot!
                    targetArea = sizeFunction(normU, normV);
                } else {
                    // Fallback to spatial distance if no CAD function is provided
                    targetArea = getTargetArea(triCenterX, triCenterY, midX, midY, MAX_AREA_ALLOWED);
                }
                
                if (area > targetArea) 
                {
                    double violation = area / targetArea;
                    if (violation > maxAreaViolation) {
                        maxAreaViolation = violation;
                        worstTriIdx = i;
                    }
                }
            }
        }

        if (worstTriIdx == -1)
            break; 

        const auto &a = pointsOut[triangulation[worstTriIdx].v[0]];
        const auto &b = pointsOut[triangulation[worstTriIdx].v[1]];
        const auto &c = pointsOut[triangulation[worstTriIdx].v[2]];

        Point2D cc = getCircumcenter(a, b, c);
        int encroachedSegIdx = -1;
        
        for (size_t j = 0; j < currentSegments.size(); j++) {
            double sx = pointsOut[currentSegments[j].first][0], sy = pointsOut[currentSegments[j].first][1];
            double ex = pointsOut[currentSegments[j].second][0], ey = pointsOut[currentSegments[j].second][1];
            
            double mx = (sx + ex) / 2.0;
            double my = (sy + ey) / 2.0;
            
            double radiusSq = std::pow(sx - mx, 2) + std::pow(sy - my, 2);
            double distSq = std::pow(cc.x - mx, 2) + std::pow(cc.y - my, 2);
            
            if (distSq <= radiusSq) { 
                encroachedSegIdx = j; 
                break; 
            }
        }

        bool touchesBoundary = triangulation[worstTriIdx].isConstrained[0] || 
                               triangulation[worstTriIdx].isConstrained[1] || 
                               triangulation[worstTriIdx].isConstrained[2];
                               
        if (encroachedSegIdx == -1 && touchesBoundary) {
            cc.x = (a[0] + b[0] + c[0]) / 3.0;
            cc.y = (a[1] + b[1] + c[1]) / 3.0;
        }

        if (encroachedSegIdx != -1) {
            int sIdx = currentSegments[encroachedSegIdx].first;
            int eIdx = currentSegments[encroachedSegIdx].second;
            
            double segLenSq = std::pow(pointsOut[sIdx][0] - pointsOut[eIdx][0], 2) + 
                              std::pow(pointsOut[sIdx][1] - pointsOut[eIdx][1], 2);
                              
            if (segLenSq < 16.0) {
                break; 
            }
            
            int mx = (pointsOut[sIdx][0] + pointsOut[eIdx][0]) / 2;
            int my = (pointsOut[sIdx][1] + pointsOut[eIdx][1]) / 2;
            
            pointsOut.push_back({mx, my});
            int mIdx = pointsOut.size() - 1;
            insertPoint(mIdx, pointsOut, triangulation);
            
            currentSegments.erase(currentSegments.begin() + encroachedSegIdx);
            currentSegments.push_back({sIdx, mIdx});
            currentSegments.push_back({mIdx, eIdx});
        } else {
            pointsOut.push_back({(int)cc.x, (int)cc.y});
            insertPoint(pointsOut.size() - 1, pointsOut, triangulation);
        }

        enforceSegments(currentSegments, pointsOut, triangulation);
        steinerPointsAdded++;
    }
    std::cout << "Applying Smart Laplacian Smoothing..." << std::endl;
    smoothMesh(pointsOut, triangulation, N);
    std::cout << "Engine added " << steinerPointsAdded << " Steiner Points for Area & Angle refinement!" << std::endl;
    
    updateTopologyAndDomain(N, currentSegments, triangulation);
    optimizeMeshTopologically(pointsOut, triangulation, N, currentSegments);
}

// ================= INDUSTRY-GRADE DIAGNOSTIC SUITE =================
void runDiagnosticSuite(const vector<vector<int>> &pointsOut, const vector<Triangle> &triangulation, double maxArea)
{
    int activeTriangles = 0;
    int orientationErrors = 0;
    int delaunayViolations = 0;
    int areaViolations = 0;
    int skinnyViolations = 0;
    double maxSkewnessFound = 0.0;
    double minAngleFound = 180.0;
    int highSkewCount = 0;
    std::cout << "\n==============================================" << std::endl;
    std::cout << "      MESH QUALITY DIAGNOSTIC REPORT          " << std::endl;
    std::cout << "==============================================" << std::endl;

    for (const auto &tri : triangulation)
    {
        if (!tri.active || tri.isExterior)
            continue;
            
        activeTriangles++;

        const auto &a = pointsOut[tri.v[0]];
        const auto &b = pointsOut[tri.v[1]];
        const auto &c = pointsOut[tri.v[2]];

        double skew = getSkewness(a, b, c);
        vector<double> angles = getTriangleAngles(a, b, c);
        double minA = std::min({angles[0], angles[1], angles[2]});

        if (skew > maxSkewnessFound) maxSkewnessFound = skew;
        if (minA < minAngleFound) minAngleFound = minA;
        if (skew > 0.5) highSkewCount++;

        double ratio = getRadiusEdgeRatio(a, b, c);
        if (ratio > 1.414 + 1e-4)
        {
            skinnyViolations++;
        }

        if (orient2d(a, b, c) <= 0)
        {
            orientationErrors++;
        }

        for (size_t i = 0; i < pointsOut.size(); i++)
        {
            if (i == tri.v[0] || i == tri.v[1] || i == tri.v[2])
                continue;
            if (incircle(a, b, c, pointsOut[i]))
            {
                delaunayViolations++;
            }
        }

        double area = getTriangleArea(a, b, c);
        if (area > maxArea + 1e-5)
        {
            areaViolations++;
        }
    }

    std::cout << "Total Interior Triangles : " << activeTriangles << std::endl;

    std::cout << "Topology (Orient2D)      : ";
    if (orientationErrors == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << orientationErrors << " Inverted Elements\033[0m" << std::endl;

    std::cout << "Delaunay (Incircle)      : ";
    if (delaunayViolations == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << delaunayViolations << " Encroachments\033[0m" << std::endl;

    std::cout << "Sizing Field (Area)      : ";
    if (areaViolations == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << areaViolations << " Oversized Elements\033[0m" << std::endl;

    std::cout << "Angle Quality (R/L)      : ";
    if (skinnyViolations == 0) std::cout << "[PASSED] \033[32mOK\033[0m" << std::endl;
    else std::cout << "[FAILED] \033[31m" << skinnyViolations << " Skinny Elements\033[0m" << std::endl;

    std::cout << "----------------------------------------------" << std::endl;
    std::cout << "Max Skewness Found       : " << maxSkewnessFound << " (Target < 0.85)" << std::endl;
    std::cout << "Min Interior Angle       : " << minAngleFound << " Degrees" << std::endl;
    std::cout << "High Skew Elements (>0.5): " << highSkewCount << std::endl;

    std::cout << "==============================================\n" << std::endl;
}
// ================= PHASE 4: TOPOLOGICAL EDGE FLIPPING =================


void optimizeMeshTopologically(vector<vector<int>>& pointsOut, vector<Triangle>& triangulation, int N, const vector<pair<int, int>>& currentSegments) {
    bool flipped = true;
    int flipCount = 0;
    int maxFlips = 5000; 

    while (flipped && flipCount < maxFlips) {
        flipped = false;
        
        for (auto& tri : triangulation) {
            if (!tri.active || tri.isExterior) continue;

            for (int i = 0; i < 3; i++) {
                if (tri.isConstrained[i]) continue; 

                Triangle* neighbor = tri.adj[i];
                if (!neighbor || !neighbor->active || neighbor->isExterior) continue;

                int vA = tri.v[(i+1)%3]; // Shared edge pt 1
                int vB = tri.v[(i+2)%3]; // Shared edge pt 2
                int vC = tri.v[i];       // Opposite pt in Triangle 1

                int vD = -1;             // Opposite pt in Triangle 2 (Neighbor)
                for (int j = 0; j < 3; j++) {
                    if (neighbor->v[j] != vA && neighbor->v[j] != vB) {
                        vD = neighbor->v[j];
                        break;
                    }
                }
                if (vD == -1) continue;

                // RULE 2: The quadrilateral MUST be strictly convex to allow a flip.
                // We test this using the orientation of the 4 corners.
                long long o1 = orient2d(pointsOut[vC], pointsOut[vA], pointsOut[vD]);
                long long o2 = orient2d(pointsOut[vA], pointsOut[vD], pointsOut[vB]);
                long long o3 = orient2d(pointsOut[vD], pointsOut[vB], pointsOut[vC]);
                long long o4 = orient2d(pointsOut[vB], pointsOut[vC], pointsOut[vA]);
                
                bool isConvex = ((o1 > 0 && o2 > 0 && o3 > 0 && o4 > 0) || 
                                 (o1 < 0 && o2 < 0 && o3 < 0 && o4 < 0));
                if (!isConvex) continue; 

                // RULE 3: Does flipping actually improve the Skewness?
                double currentSkew1 = getSkewness(pointsOut[vC], pointsOut[vA], pointsOut[vB]);
                double currentSkew2 = getSkewness(pointsOut[vD], pointsOut[vA], pointsOut[vB]);
                double currentMaxSkew = std::max(currentSkew1, currentSkew2);

                double proposedSkew1 = getSkewness(pointsOut[vC], pointsOut[vD], pointsOut[vA]);
                double proposedSkew2 = getSkewness(pointsOut[vC], pointsOut[vD], pointsOut[vB]);
                double proposedMaxSkew = std::max(proposedSkew1, proposedSkew2);

                // If the flip improves the worst triangle by at least 1%, DO IT.
                if (proposedMaxSkew < currentMaxSkew - 0.01) {
                    // Deactivate old triangles
                    tri.active = false;
                    neighbor->active = false;
                    
                    // Inject the newly flipped triangles
                    triangulation.emplace_back(vC, vA, vD);
                    triangulation.emplace_back(vC, vD, vB);
                    
                    flipped = true;
                    flipCount++;
                    break; // Break the inner loop, as our topology array just changed
                }
            }
            if (flipped) break;
        }
        
        // If we flipped something, we must rebuild the adjacency map before checking again
        if (flipped) {
            updateTopologyAndDomain(N, currentSegments, triangulation);
        }
    }
    
    std::cout << "Engine executed " << flipCount << " Topological Edge Flips to heal skewness." << std::endl;
}
void exportToOBJ(const string& filename, const vector<vector<int>>& pointsOut, const vector<Triangle>& triangulation) {
    ofstream out(filename);
    if (!out.is_open()) return;

    // 1. Calculate vertex colors based on adjacent triangle skewness
    vector<double> vertexSkew(pointsOut.size(), 0.0);
    vector<int> vertexTriCount(pointsOut.size(), 0);

    for (const auto& tri : triangulation) {
        if (!tri.active || tri.isExterior) continue;
        
        double skew = getSkewness(pointsOut[tri.v[0]], pointsOut[tri.v[1]], pointsOut[tri.v[2]]);
        
        // Accumulate skewness for the vertices
        for (int i = 0; i < 3; i++) {
            vertexSkew[tri.v[i]] += skew;
            vertexTriCount[tri.v[i]]++;
        }
    }

    // 2. Find the center of the mesh to create a smooth 3D curve
    double minX = pointsOut[0][0], maxX = pointsOut[0][0];
    double minY = pointsOut[0][1], maxY = pointsOut[0][1];
    
    for (const auto& p : pointsOut) {
        if (p[0] < minX) minX = p[0];
        if (p[0] > maxX) maxX = p[0];
        if (p[1] < minY) minY = p[1];
        if (p[1] > maxY) maxY = p[1];
    }
    
    double centerX = (minX + maxX) / 2.0;
    double centerY = (minY + maxY) / 2.0;
    
    // 3. Write Vertices with RGB Heatmap Colors
    for (size_t i = 0; i < pointsOut.size(); i++) {
        double x = pointsOut[i][0];
        double y = pointsOut[i][1];
        
        // --- 3D MATH PROJECTION ---
        double distanceSq = std::pow(x - centerX, 2) + std::pow(y - centerY, 2);
        double z = distanceSq * 0.001; 
        
        // Calculate average skewness for this vertex
        double avgSkew = (vertexTriCount[i] > 0) ? (vertexSkew[i] / vertexTriCount[i]) : 0.0;
        
        // Map Skewness (0.0 to ~0.8) to a Blue-to-Red gradient
        double r = 0.0, g = 0.0, b = 1.0; 
        if (avgSkew < 0.25) {
            r = 0.0; g = avgSkew * 4.0; b = 1.0; 
        } else if (avgSkew < 0.5) {
            r = 0.0; g = 1.0; b = 1.0 - ((avgSkew - 0.25) * 4.0); 
        } else if (avgSkew < 0.75) {
            r = (avgSkew - 0.5) * 4.0; g = 1.0; b = 0.0; 
        } else {
            r = 1.0; g = 1.0 - ((avgSkew - 0.75) * 4.0); b = 0.0; 
        }

        // Output format: v x y z r g b
        out << "v " << x << " " << y << " " << z << " " << r << " " << g << " " << b << "\n";
    }

    // 4. Write Triangles
    int exportedTriangles = 0;
    for (const auto& tri : triangulation) {
        if (!tri.active || tri.isExterior) continue;
        out << "f " << (tri.v[0] + 1) << " " << (tri.v[1] + 1) << " " << (tri.v[2] + 1) << "\n";
        exportedTriangles++;
    }

    out.close();
    std::cout << "[PHASE 4] Exported Mesh with Quality Heatmap Data to " << filename << std::endl;
}
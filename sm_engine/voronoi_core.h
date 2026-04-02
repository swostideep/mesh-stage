#ifndef VORONOI_CORE_H
#define VORONOI_CORE_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <functional>
using namespace std;

struct Point2D { double x, y; };

struct EdgeKey {
    int v1, v2;
    EdgeKey(int a, int b) { v1 = min(a, b); v2 = max(a, b); }
    bool operator<(const EdgeKey& other) const {
        if (v1 != other.v1) return v1 < other.v1;
        return v2 < other.v2;
    }
};

struct Triangle {
    int v[3];
    Triangle* adj[3]; 
    bool active;
    bool isConstrained[3]; 
    bool isExterior;
    bool skipRefinement;
    Triangle(int a, int b, int c) {
        v[0] = a; v[1] = b; v[2] = c;
        for(int i=0; i<3; i++) { adj[i] = nullptr; isConstrained[i] = false; }
        active = true; isExterior = false; skipRefinement = false;
    }
};

struct Edge {
    int v1, v2;
    bool operator==(const Edge& other) const {
        return (v1 == other.v1 && v2 == other.v2) || (v1 == other.v2 && v2 == other.v1);
    }
};

long long orient2d(const vector<int>& a, const vector<int>& b, const vector<int>& c);
bool incircle(const vector<int>& a, const vector<int>& b, const vector<int>& c, const vector<int>& d);
Point2D getCircumcenter(const vector<int>& a, const vector<int>& b, const vector<int>& c);
double getTriangleArea(const vector<int>& a, const vector<int>& b, const vector<int>& c); // NEW

double getRadiusEdgeRatio(const vector<int>& a, const vector<int>& b, const vector<int>& c);
double getTargetArea(int x, int y, int midX, int midY, double globalMaxArea);
void insertPoint(int pIdx, const vector<vector<int>>& pointsOut, vector<Triangle>& triangulation);
// Add this right above your computeDelaunay declaration
void smoothMesh(vector<vector<int>>& pointsOut, vector<Triangle>& triangulation, int numOriginalPoints);
void optimizeMeshTopologically(vector<vector<int>>& pointsOut, vector<Triangle>& triangulation, int N, const vector<pair<int, int>>& currentSegments);
void computeDelaunay(const vector<vector<int>>& inPoints, 
                     const vector<pair<int, int>>& segments, 
                     vector<Triangle>& triangulation, 
                     vector<vector<int>>& pointsOut,
                     std::function<double(double, double)> sizeFunction = nullptr);

// Replace validateDelaunay with this comprehensive suite
void runDiagnosticSuite(const vector<vector<int>>& pointsOut, const vector<Triangle>& triangulation, double maxArea);
void exportToOBJ(const string& filename, const vector<vector<int>>& pointsOut, const vector<Triangle>& triangulation);
#endif
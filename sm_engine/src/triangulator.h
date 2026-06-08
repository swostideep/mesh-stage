#pragma once

#include <cstdint>
#include <vector>

#include "sm/mesh.h"
#include "sm/mesher.h"

namespace sm {

// Constrained Delaunay triangulator over the integer lattice.
//
// Vertices go in through a Hilbert-ordered, biased-randomised sequence so the
// point location walk stays short. Every insertion touches only the cavity it
// invalidates, so the cost is proportional to the local change rather than to
// the size of the mesh.
class Triangulator {
public:
    explicit Triangulator(const RefineOptions& options);

    // Maps input coordinates onto the lattice and seeds the enclosing
    // super-triangle. Must be called before anything else.
    void begin(const std::vector<Vec2>& input);

    void insertInputVertices();
    void insertSegments(const std::vector<std::array<int32_t, 2>>& segments);
    void classifyDomain();
    void refine(const SizeField& sizeField);
    int smooth(int passes);
    int optimiseTopology();

    // Lattice -> input space.
    Vec2 toInputSpace(const IPoint& p) const;

    const TriMesh& mesh() const { return mesh_; }
    TriMesh& mesh() { return mesh_; }
    int steinerCount() const { return steiner_; }
    int inputVertexCount() const { return inputCount_; }

private:
    struct BoundaryEdge {
        int32_t outerTwin;   // half-edge on the far side of the cavity wall
        int32_t from;
        int32_t to;
        uint8_t constrained;
        uint8_t exterior;    // inherited by the triangle built on this edge
    };

    IPoint quantise(const Vec2& p) const;

    int32_t locate(const IPoint& p, int32_t hint, int32_t* onEdge) const;
    bool buildCavity(const IPoint& p, int32_t seedTri, int32_t pierceHalf);
    int32_t insertVertex(int32_t vertexId, int32_t hint, int32_t pierceHalf);

    void relinkRegion(const std::vector<int32_t>& newTris,
                      const std::vector<BoundaryEdge>& wall);

    bool segmentPresent(int32_t a, int32_t b, int32_t* half) const;
    int32_t outgoingHalf(int32_t vertex) const;
    void forceSegment(int32_t a, int32_t b);
    void triangulatePseudoPolygon(const std::vector<int32_t>& poly, int lo, int hi,
                                  std::vector<int32_t>& out);

    void collectSegments();
    void rebuildSegmentGrid();
    bool anyEncroached(const IPoint& p, int32_t* segIndex) const;
    void splitSegment(int32_t segIndex);
    double targetEdgeLengthAt(const IPoint& centroid, const SizeField& field) const;
    bool triangleIsBad(int32_t t, const SizeField& field, double* priority) const;
    void enqueueBad(int32_t t, const SizeField& field);

    RefineOptions opts_;
    TriMesh mesh_;

    double scale_ = 1.0;
    double invScale_ = 1.0;
    Vec2 origin_{};
    int64_t latticeSpan_ = kQuantScale;

    int32_t inputCount_ = 0;
    int32_t superBase_ = 0;   // first of the three super-triangle vertices
    int steiner_ = 0;
    int32_t walkHint_ = 0;

    std::vector<IPoint> pending_;   // quantised input, Hilbert ordered

    // Cavity scratch, reused across insertions to avoid per-call allocation.
    std::vector<int32_t> cavity_;
    std::vector<BoundaryEdge> wall_;
    std::vector<int32_t> stack_;
    std::vector<int32_t> cavityStamp_;
    int32_t stampCounter_ = 0;

    // Vertex-indexed scratch for stitching the new fan together.
    std::vector<int32_t> fanFromP_;
    std::vector<int32_t> fanToP_;
    std::vector<int32_t> fanStamp_;
    int32_t fanCounter_ = 0;

    std::vector<int32_t> newTris_;

    // Segment bookkeeping for encroachment queries.
    struct Segment {
        int32_t a;
        int32_t b;
        bool alive;
    };
    std::vector<Segment> segments_;
    std::vector<std::vector<int32_t>> grid_;
    int gridDim_ = 1;
    double gridCell_ = 1.0;

    std::vector<int32_t> badQueue_;
};

}  // namespace sm

#pragma once
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstring>
#include <queue>
#include <numeric>

using Vec3 = std::array<double, 3>;
using Tri = std::array<int, 3>;

struct DiagnoseResult {
    int vertices = 0, triangles = 0;
    int boundary = 0, nonManifold = 0;
    int windingInconsistencies = 0, oppositeWindingPairs = 0;
    int components = 0;
    bool isWatertight = false;
};

struct RepairReport {
    int merged = 0, degenerateRemoved = 0, oppositeWindingRemoved = 0;
    int exactDuplicatesRemoved = 0, normalsFlipped = 0;
    int nmFixed = 0, nmVerticesSplit = 0, holesFilled = 0;
    int smallShellsRemoved = 0;
    int selfIntersectionsRepaired = 0;
    bool deepRepairApplied = false;
    int deepRepairResolution = 0;
};

struct RepairResult {
    std::vector<Vec3> V;
    std::vector<Tri> T;
    RepairReport report;
};

struct RepairOptions {
    int mergePrecision = -1;
    double degenerateThreshold = -1;
    double holeAreaMultiplier = -1;
    double localRemeshLimit = 0.3;
    int edgeFlipPasses = 20;
    int nmEdgeIterations = 50;
    int oversizeMultiplier = 20;
    bool removeSmallShells = false;
    double smallShellThreshold = 0.01;
    bool removeSpuriousShells = true;
    bool repairSelfIntersections = false;
    int deepRepair = 0;
    int voxelResolution = -1;
    bool voxelSmoothing = true;
    int voxelSmoothIterations = 3;
    double voxelSmoothLambda = 0.5;
    bool voxelProjectToSurface = true;
    double voxelProjectMaxDist = -1;
    bool _wiRecoveryRetry = false;
};

struct ScaleInfo {
    double bboxDiag = 0;
    double medianEdge = 0;
    double avgArea = 0;
    int triCount = 0;
};

struct EdgeInfo {
    std::vector<int> fwd;
    std::vector<int> rev;
};

struct BVHNode {
    std::array<double, 6> box;
    std::vector<int> indices; // leaf
    BVHNode* left = nullptr;
    BVHNode* right = nullptr;
    ~BVHNode() { delete left; delete right; }
};

struct BVH {
    BVHNode* root = nullptr;
    std::vector<std::array<double, 6>> triBoxes;
    ~BVH() { delete root; }
};

struct NearestResult {
    double distance = 1e30;
    int triIdx = -1;
    Vec3 closest = {0, 0, 0};
};

struct SDFGrid {
    std::vector<float> data;
    int resX = 0, resY = 0, resZ = 0;
    Vec3 origin = {0, 0, 0};
    double voxelSize = 0;
};

using ProgressCallback = std::function<void(const char*)>;

class MeshFix {
public:
    // Public API
    DiagnoseResult diagnose(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;
    DiagnoseResult quickDiagnose(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;
    RepairResult repairObject(std::vector<Vec3> V, std::vector<Tri> T,
                              ProgressCallback progress, RepairOptions opts = {});
    void dispose();

    // Utilities
    static inline int64_t undirEdgeKey(int a, int b) {
        return a < b ? (int64_t)a * 1000000 + b : (int64_t)b * 1000000 + a;
    }
    static inline int64_t dirEdgeKey(int a, int b) {
        return (int64_t)a * 1000000 + b;
    }
    static inline int64_t windingKey(int a, int b, int c) {
        if (a <= b && a <= c) return (int64_t)a * 1000000000000LL + (int64_t)b * 1000000 + c;
        if (b <= a && b <= c) return (int64_t)b * 1000000000000LL + (int64_t)c * 1000000 + a;
        return (int64_t)c * 1000000000000LL + (int64_t)a * 1000000 + b;
    }
    static inline int64_t unorderedKey(int a, int b, int c) {
        int x = a, y = b, z = c;
        if (x > y) std::swap(x, y);
        if (y > z) std::swap(y, z);
        if (x > y) std::swap(x, y);
        return (int64_t)x * 1000000000000LL + (int64_t)y * 1000000 + z;
    }
    static Vec3 computeNormal(const std::vector<Vec3>& V, int a, int b, int c);
    static double triangleArea3D(const std::vector<Vec3>& V, int a, int b, int c);
    static std::string vKey(const Vec3& v, int precision = 6);

    // Diagnose (diagnose.cpp)
    int countWindingInconsistencies(const std::vector<Tri>& T) const;
    std::vector<std::vector<int>> findConnectedComponents(const std::vector<Tri>& T) const;

    // Stages (stages.cpp)
    ScaleInfo analyzeScale(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;
    RepairOptions resolveOptions(const std::vector<Vec3>& V, const std::vector<Tri>& T,
                                 const RepairOptions& userOpts = {}) const;

    struct MergeResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; int merged; };
    MergeResult mergeVertices(const std::vector<Vec3>& V, const std::vector<Tri>& T, int precision) const;

    struct RemoveResult { std::vector<Tri> triangles; int removed; };
    RemoveResult removeDegenerateTriangles(const std::vector<Vec3>& V, const std::vector<Tri>& T, double threshold) const;

    struct OWPResult { std::vector<Tri> triangles; int pairsRemoved; };
    OWPResult removeOppositeWindingDuplicates(const std::vector<Tri>& T) const;

    struct DupResult { std::vector<Tri> triangles; int removed; };
    DupResult removeExactDuplicates(const std::vector<Tri>& T) const;

    struct NormResult { std::vector<Tri> triangles; int flipped; };
    NormResult enforceNormalConsistency(const std::vector<Vec3>& V, const std::vector<Tri>& T, bool skipVolumeTest) const;

    struct NMResult { std::vector<Tri> triangles; int fixed; };
    NMResult resolveNonManifoldEdges(const std::vector<Vec3>& V, const std::vector<Tri>& T, int maxIter) const;

    struct NMVResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; int split; };
    NMVResult resolveNonManifoldVertices(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;

    struct ShellResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; int removed; };
    ShellResult removeSmallShells(const std::vector<Vec3>& V, const std::vector<Tri>& T, double threshold) const;

    struct CleanResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; };
    CleanResult removeUnusedVertices(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;

    // Hole filling (hole_fill.cpp)
    struct BoundaryEdge { int from, to; };
    std::vector<BoundaryEdge> getBoundaryEdges(const std::vector<Tri>& T) const;
    std::vector<std::vector<int>> findAllLoops(const std::vector<BoundaryEdge>& edges) const;
    std::vector<Tri> fillHoleEarClipping(std::vector<Vec3>& V, const std::vector<int>& loop, double maxTriArea) const;
    Tri fillTJunction(const std::vector<BoundaryEdge>& boundary) const;
    std::vector<int> findOpenChain(const std::vector<BoundaryEdge>& boundary) const;
    struct FillResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; int filled; };
    FillResult fillHoles(const std::vector<Vec3>& V, const std::vector<Tri>& T, double maxTriArea) const;
    struct SubdivResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; int splitCount; };
    SubdivResult subdivideLongBoundaryEdges(const std::vector<Vec3>& V, const std::vector<Tri>& T, double maxTriArea) const;
    struct OversizeResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; };
    OversizeResult subdivideOversizedTriangles(const std::vector<Vec3>& V, const std::vector<Tri>& T, double maxTriArea) const;

    bool pointInTriangle2D(const std::array<double,2>& p, const std::array<double,2>& a,
                           const std::array<double,2>& b, const std::array<double,2>& c) const;

    std::unordered_map<int64_t, int> buildEdgeCounts(const std::vector<Tri>& T) const;

    // Winding (winding.cpp)
    struct WindingResult { std::vector<Tri> triangles; int fixed; };
    WindingResult fixWindingByEdgeFlip(const std::vector<Vec3>& V, const std::vector<Tri>& T, int maxPasses) const;
    struct RemeshResult { std::vector<Tri> triangles; };
    RemeshResult fixWindingByLocalRemesh(const std::vector<Vec3>& V, const std::vector<Tri>& T, double fractionLimit) const;

    // Self-intersection (self_intersect.cpp)
    BVH* buildBVH(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;
    std::vector<int> bvhQuery(const BVHNode* node, const std::array<double,6>& queryBox) const;
    bool trianglesIntersect(const std::vector<Vec3>& V, const std::vector<Tri>& T, int t1, int t2) const;
    NearestResult pointToTriangleDistance(const Vec3& p, const std::vector<Vec3>& V, int a, int b, int c) const;
    NearestResult findNearestTriangle(const Vec3& point, const std::vector<Vec3>& V, const std::vector<Tri>& T, const BVH* bvh) const;
    std::vector<std::pair<int,int>> detectSelfIntersections(const std::vector<Vec3>& V, const std::vector<Tri>& T) const;
    struct SIResult { std::vector<Vec3> vertices; std::vector<Tri> triangles; int repaired; };
    SIResult repairSelfIntersections(std::vector<Vec3> V, std::vector<Tri> T, double holeMaxTriArea) const;

    // Voxel (voxel.cpp)
    SDFGrid voxelizeToSDF(const std::vector<Vec3>& V, const std::vector<Tri>& T, int resolution, ProgressCallback progress) const;
    struct MCResult { std::vector<Vec3> V; std::vector<Tri> T; };
    MCResult marchingCubes(const SDFGrid& sdf) const;
    std::vector<Vec3> laplacianSmooth(const std::vector<Vec3>& V, const std::vector<Tri>& T, int iterations, double lambda) const;
    std::vector<Vec3> projectToOriginalSurface(const std::vector<Vec3>& newV, const std::vector<Vec3>& origV,
                                                const std::vector<Tri>& origT, const BVH* bvh, double maxDist) const;
    struct VoxelResult { std::vector<Vec3> V; std::vector<Tri> T; int resolution; };
    VoxelResult repairViaVoxelReconstruction(const std::vector<Vec3>& V, const std::vector<Tri>& T,
                                              ProgressCallback progress, const RepairOptions& opts) const;
    int selectVoxelResolution(const ScaleInfo& scale, int userResolution) const;

    struct RayHit { double t; bool hit; };
    RayHit rayTriangleIntersect(const Vec3& origin, const Vec3& dir, const std::vector<Vec3>& V, int a, int b, int c) const;
};

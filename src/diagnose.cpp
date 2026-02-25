#include "mesh_fix.h"
#include <sstream>
#include <iomanip>
#include <cmath>

// ===== Static utility functions =====

Vec3 MeshFix::computeNormal(const std::vector<Vec3>& V, int a, int b, int c) {
    const auto& v0 = V[a];
    const auto& v1 = V[b];
    const auto& v2 = V[c];
    double e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
    double e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x;
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-15) return {0.0, 0.0, 0.0};
    return {nx / len, ny / len, nz / len};
}

double MeshFix::triangleArea3D(const std::vector<Vec3>& V, int a, int b, int c) {
    const auto& v0 = V[a];
    const auto& v1 = V[b];
    const auto& v2 = V[c];
    double e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
    double e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
    double cx = e1y * e2z - e1z * e2y;
    double cy = e1z * e2x - e1x * e2z;
    double cz = e1x * e2y - e1y * e2x;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

std::string MeshFix::vKey(const Vec3& v, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision)
        << v[0] << "_" << v[1] << "_" << v[2];
    return oss.str();
}

// ===== buildEdgeCounts =====
std::unordered_map<int64_t, int> MeshFix::buildEdgeCounts(const std::vector<Tri>& T) const {
    std::unordered_map<int64_t, int> ec;
    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int64_t e1 = undirEdgeKey(a, b);
        int64_t e2 = undirEdgeKey(b, c);
        int64_t e3 = undirEdgeKey(c, a);
        ec[e1]++;
        ec[e2]++;
        ec[e3]++;
    }
    return ec;
}

// ===== quickDiagnose =====
// Fast diagnosis: NM and B only (skips WI, components, OWP)
// Used where only isWatertight check is needed

DiagnoseResult MeshFix::quickDiagnose(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    std::unordered_map<int64_t, int> undirEdges;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        if (a == b || b == c || c == a) continue;

        int64_t e1 = undirEdgeKey(a, b);
        int64_t e2 = undirEdgeKey(b, c);
        int64_t e3 = undirEdgeKey(c, a);
        undirEdges[e1]++;
        undirEdges[e2]++;
        undirEdges[e3]++;
    }

    int boundary = 0, nonManifold = 0;
    for (const auto& [key, cnt] : undirEdges) {
        if (cnt == 1) boundary++;
        else if (cnt > 2) nonManifold++;
    }

    DiagnoseResult result;
    result.vertices = static_cast<int>(V.size());
    result.triangles = static_cast<int>(T.size());
    result.boundary = boundary;
    result.nonManifold = nonManifold;
    result.isWatertight = (boundary == 0 && nonManifold == 0);
    return result;
}

// ===== diagnose =====
// Full diagnosis including WI, OWP, and connected components

DiagnoseResult MeshFix::diagnose(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    std::unordered_map<int64_t, int> undirEdges;
    std::unordered_map<int64_t, int> dirEdges;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        if (a == b || b == c || c == a) continue;

        // Undirected counts
        int64_t e1 = undirEdgeKey(a, b);
        int64_t e2 = undirEdgeKey(b, c);
        int64_t e3 = undirEdgeKey(c, a);
        undirEdges[e1]++;
        undirEdges[e2]++;
        undirEdges[e3]++;

        // Directed counts
        int64_t d1 = dirEdgeKey(a, b);
        int64_t d2 = dirEdgeKey(b, c);
        int64_t d3 = dirEdgeKey(c, a);
        dirEdges[d1]++;
        dirEdges[d2]++;
        dirEdges[d3]++;
    }

    int boundary = 0;
    int nonManifold = 0;

    for (const auto& [key, cnt] : undirEdges) {
        if (cnt == 1) boundary++;
        else if (cnt > 2) nonManifold++;
    }

    // Opposite winding pair detection
    std::unordered_map<int64_t, std::vector<int>> triByUKey;
    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        if (a == b || b == c || c == a) continue;
        int64_t uk = unorderedKey(a, b, c);
        triByUKey[uk].push_back(static_cast<int>(i));
    }

    int oppositeWindingPairs = 0;
    for (const auto& [uk, tris] : triByUKey) {
        if (tris.size() < 2) continue;
        std::unordered_set<int64_t> windings;
        for (int ti : tris) {
            windings.insert(windingKey(T[ti][0], T[ti][1], T[ti][2]));
        }
        if (windings.size() >= 2) oppositeWindingPairs++;
    }

    // Winding inconsistency edges (manifold edges traversed in same direction)
    int windingInconsistencies = 0;
    {
        // wdInfo: undirEdgeKey -> [fwdCount, revCount]
        std::unordered_map<int64_t, std::array<int, 2>> wdInfo;
        for (size_t i = 0; i < T.size(); i++) {
            int a = T[i][0], b = T[i][1], c = T[i][2];
            if (a == b || b == c || c == a) continue;
            int edges[3][2] = {{a, b}, {b, c}, {c, a}};
            for (int e = 0; e < 3; e++) {
                int u = edges[e][0], v = edges[e][1];
                int64_t wk = undirEdgeKey(u, v);
                auto& wd = wdInfo[wk];
                if (u < v) wd[0]++;
                else wd[1]++;
            }
        }
        for (const auto& [key, wd] : wdInfo) {
            if (wd[0] + wd[1] != 2) continue;
            if (wd[0] == 2 || wd[1] == 2) windingInconsistencies++;
        }
    }

    // Connected component count
    int components = static_cast<int>(findConnectedComponents(T).size());

    DiagnoseResult result;
    result.vertices = static_cast<int>(V.size());
    result.triangles = static_cast<int>(T.size());
    result.boundary = boundary;
    result.nonManifold = nonManifold;
    result.oppositeWindingPairs = oppositeWindingPairs;
    result.windingInconsistencies = windingInconsistencies;
    result.components = components;
    result.isWatertight = (boundary == 0 && nonManifold == 0);
    return result;
}

// ===== countWindingInconsistencies =====
// Count winding inconsistency edges

int MeshFix::countWindingInconsistencies(const std::vector<Tri>& T) const {
    // edgeInfo: undirEdgeKey -> EdgeInfo { fwd[], rev[] }
    std::unordered_map<int64_t, EdgeInfo> edgeInfo;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int edges[3][2] = {{a, b}, {b, c}, {c, a}};
        for (int e = 0; e < 3; e++) {
            int u = edges[e][0], v = edges[e][1];
            int64_t k = undirEdgeKey(u, v);
            auto& entry = edgeInfo[k];
            if (u < v) entry.fwd.push_back(static_cast<int>(i));
            else entry.rev.push_back(static_cast<int>(i));
        }
    }

    int count = 0;
    for (const auto& [k, info] : edgeInfo) {
        int total = static_cast<int>(info.fwd.size() + info.rev.size());
        if (total != 2) continue;
        if (info.fwd.size() == 2 || info.rev.size() == 2) count++;
    }
    return count;
}

// ===== findConnectedComponents =====
// Find connected components via BFS over shared edges

std::vector<std::vector<int>> MeshFix::findConnectedComponents(const std::vector<Tri>& T) const {
    int n = static_cast<int>(T.size());
    if (n == 0) return {};

    // Build adjacency: edge -> list of triangle indices
    std::unordered_map<int64_t, std::vector<int>> edgeMap;
    for (int i = 0; i < n; i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int edges[3][2] = {{a, b}, {b, c}, {c, a}};
        for (int e = 0; e < 3; e++) {
            int u = edges[e][0], v = edges[e][1];
            int64_t key = undirEdgeKey(u, v);
            edgeMap[key].push_back(i);
        }
    }

    // BFS to find connected components
    std::vector<uint8_t> visited(n, 0);
    std::vector<std::vector<int>> components;

    for (int i = 0; i < n; i++) {
        if (visited[i]) continue;
        std::vector<int> component;
        std::vector<int> queue;
        queue.push_back(i);
        visited[i] = 1;

        while (!queue.empty()) {
            int ti = queue.back();
            queue.pop_back();
            component.push_back(ti);

            int a = T[ti][0], b = T[ti][1], c = T[ti][2];
            int edges[3][2] = {{a, b}, {b, c}, {c, a}};
            for (int e = 0; e < 3; e++) {
                int u = edges[e][0], v = edges[e][1];
                int64_t key = undirEdgeKey(u, v);
                auto it = edgeMap.find(key);
                if (it == edgeMap.end()) continue;
                for (int ni : it->second) {
                    if (!visited[ni]) {
                        visited[ni] = 1;
                        queue.push_back(ni);
                    }
                }
            }
        }
        components.push_back(std::move(component));
    }

    return components;
}

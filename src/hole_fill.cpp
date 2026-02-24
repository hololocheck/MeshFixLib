#include "mesh_fix.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>

// (buildEdgeCounts is defined in diagnose.cpp)

// ===== _countEdge helper (inlined into getBoundaryEdges) =====
// Helper: increment edge count and record direction

static void countEdge(std::unordered_map<int64_t, int>& cntMap,
                      std::unordered_map<int64_t, int>& dirMap,
                      int a, int b) {
    int64_t k = MeshFix::undirEdgeKey(a, b);
    cntMap[k]++;
    dirMap[k] = (a < b) ? 1 : 0;
}

// ===== getBoundaryEdges =====
// Find boundary edges (edges with count == 1)

std::vector<MeshFix::BoundaryEdge> MeshFix::getBoundaryEdges(const std::vector<Tri>& T) const {
    std::unordered_map<int64_t, int> edgeCount;
    std::unordered_map<int64_t, int> edgeDir;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        countEdge(edgeCount, edgeDir, a, b);
        countEdge(edgeCount, edgeDir, b, c);
        countEdge(edgeCount, edgeDir, c, a);
    }

    std::vector<BoundaryEdge> boundary;
    for (const auto& [k, cnt] : edgeCount) {
        if (cnt == 1) {
            int a = static_cast<int>(k / 1000000);
            int b = static_cast<int>(k % 1000000);
            if (edgeDir.at(k) == 1) {
                boundary.push_back({b, a});
            } else {
                boundary.push_back({a, b});
            }
        }
    }

    return boundary;
}

// ===== findAllLoops =====
// Find directed loops in boundary edges

std::vector<std::vector<int>> MeshFix::findAllLoops(const std::vector<BoundaryEdge>& edges) const {
    if (edges.empty()) return {};

    std::vector<std::vector<int>> loops;
    std::vector<uint8_t> edgeUsed(edges.size(), 0);

    // Build outgoing edge map: vertex -> list of {to, index}
    struct OutEdge { int to; int idx; };
    std::unordered_map<int, std::vector<OutEdge>> outMap;
    for (size_t i = 0; i < edges.size(); i++) {
        outMap[edges[i].from].push_back({edges[i].to, static_cast<int>(i)});
    }

    for (size_t startIdx = 0; startIdx < edges.size(); startIdx++) {
        if (edgeUsed[startIdx]) continue;

        int start = edges[startIdx].from;
        int next = edges[startIdx].to;
        edgeUsed[startIdx] = 1;

        // Chain tracing with self-intersection detection
        std::vector<int> path;
        path.push_back(start);
        int current = next;
        bool found = false;

        for (int step = 0; step < 300; step++) {
            // Reached start vertex -> loop complete
            if (current == start) {
                if (path.size() >= 3) found = true;
                break;
            }

            // Self-intersection: current already in path (not start) -> extract inner loop
            int existingIdx = -1;
            for (size_t pi = 0; pi < path.size(); pi++) {
                if (path[pi] == current) {
                    existingIdx = static_cast<int>(pi);
                    break;
                }
            }

            if (existingIdx >= 0) {
                // Extract inner loop from existingIdx to end of path
                std::vector<int> innerLoop(path.begin() + existingIdx, path.end());
                if (innerLoop.size() >= 3) loops.push_back(std::move(innerLoop));
                // Trim path back to intersection point (keep the vertex at existingIdx)
                path.resize(existingIdx + 1);
                // current == path back, don't push, just advance to next edge
            } else {
                path.push_back(current);
            }

            // Find unused outgoing edge from current
            auto it = outMap.find(current);
            bool advanced = false;
            if (it != outMap.end()) {
                for (auto& oe : it->second) {
                    if (!edgeUsed[oe.idx]) {
                        edgeUsed[oe.idx] = 1;
                        current = oe.to;
                        advanced = true;
                        break;
                    }
                }
            }
            if (!advanced) break;
        }

        if (found) loops.push_back(std::move(path));
    }

    return loops;
}

// ===== pointInTriangle2D =====
// 2D point-in-triangle test using sign of cross products

bool MeshFix::pointInTriangle2D(const std::array<double, 2>& p,
                                 const std::array<double, 2>& a,
                                 const std::array<double, 2>& b,
                                 const std::array<double, 2>& c) const {
    double d1 = (p[0] - c[0]) * (a[1] - c[1]) - (a[0] - c[0]) * (p[1] - c[1]);
    double d2 = (p[0] - a[0]) * (b[1] - a[1]) - (b[0] - a[0]) * (p[1] - a[1]);
    double d3 = (p[0] - b[0]) * (c[1] - b[1]) - (c[0] - b[0]) * (p[1] - b[1]);
    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

// ===== _fillHoleFan (private helper) =====
// Fan triangulation fallback: create centroid vertex and fan triangles around loop

static std::vector<Tri> fillHoleFan(std::vector<Vec3>& V, const std::vector<int>& loop, double maxTriArea) {
    int n = static_cast<int>(loop.size());
    if (n < 3) return {};
    if (n == 3) {
        if (maxTriArea > 0 && MeshFix::triangleArea3D(V, loop[0], loop[1], loop[2]) > maxTriArea) return {};
        return {{loop[0], loop[1], loop[2]}};
    }

    // Compute centroid
    double cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) {
        int idx = loop[i];
        cx += V[idx][0];
        cy += V[idx][1];
        cz += V[idx][2];
    }
    V.push_back({cx / n, cy / n, cz / n});
    int ci = static_cast<int>(V.size()) - 1;

    std::vector<Tri> tris;
    for (int i = 0; i < n; i++) {
        Tri tri = {loop[i], loop[(i + 1) % n], ci};
        if (maxTriArea > 0 && MeshFix::triangleArea3D(V, tri[0], tri[1], tri[2]) > maxTriArea) continue;
        tris.push_back(tri);
    }

    // If all fan triangles were skipped, remove the centroid vertex
    if (tris.empty()) {
        V.pop_back();
    }
    return tris;
}

// ===== fillHoleEarClipping =====
// Ear clipping triangulation for a loop
// Projects 3D loop to 2D using dominant normal axis, then does ear clipping

std::vector<Tri> MeshFix::fillHoleEarClipping(std::vector<Vec3>& V,
                                               const std::vector<int>& loop,
                                               double maxTriArea) const {
    int n = static_cast<int>(loop.size());
    if (n < 3) return {};
    if (n == 3) {
        if (maxTriArea > 0 && triangleArea3D(V, loop[0], loop[1], loop[2]) > maxTriArea) return {};
        return {{loop[0], loop[1], loop[2]}};
    }

    // Compute loop normal using Newell's method
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; i++) {
        const auto& curr = V[loop[i]];
        const auto& next = V[loop[(i + 1) % n]];
        nx += (curr[1] - next[1]) * (curr[2] + next[2]);
        ny += (curr[2] - next[2]) * (curr[0] + next[0]);
        nz += (curr[0] - next[0]) * (curr[1] + next[1]);
    }
    double nLen = std::sqrt(nx * nx + ny * ny + nz * nz);

    // Normal is near-zero -> fan fallback (matches JS _fillHoleFan)
    if (nLen < 1e-15) {
        return fillHoleFan(V, loop, maxTriArea);
    }

    nx /= nLen; ny /= nLen; nz /= nLen;

    // Projection plane basis vectors
    double ux, uy, uz;
    if (std::abs(nz) < 0.9) {
        ux = ny; uy = -nx; uz = 0;
    } else {
        ux = 0; uy = nz; uz = -ny;
    }
    double uLen = std::sqrt(ux * ux + uy * uy + uz * uz);
    ux /= uLen; uy /= uLen; uz /= uLen;

    double vx = ny * uz - nz * uy;
    double vy = nz * ux - nx * uz;
    double vz = nx * uy - ny * ux;

    // Project to 2D
    std::vector<std::array<double, 2>> pts(n);
    for (int i = 0; i < n; i++) {
        const auto& p = V[loop[i]];
        pts[i] = {p[0] * ux + p[1] * uy + p[2] * uz,
                  p[0] * vx + p[1] * vy + p[2] * vz};
    }

    // Ear clipping
    std::vector<Tri> result;
    std::vector<int> indices;
    for (int i = 0; i < n; i++) indices.push_back(i);

    int maxIter = n * n;
    int iterCount = 0;

    while (static_cast<int>(indices.size()) > 3 && iterCount < maxIter) {
        iterCount++;
        int len = static_cast<int>(indices.size());
        bool earFound = false;

        for (int i = 0; i < len; i++) {
            int prevI = (i - 1 + len) % len;
            int nextI = (i + 1) % len;
            int pi = indices[prevI], ci = indices[i], ni = indices[nextI];

            // Convexity check
            double cross = (pts[ci][0] - pts[pi][0]) * (pts[ni][1] - pts[pi][1])
                         - (pts[ci][1] - pts[pi][1]) * (pts[ni][0] - pts[pi][0]);
            if (cross <= 0) continue;

            // Area check: skip giant ears
            if (maxTriArea > 0 && triangleArea3D(V, loop[pi], loop[ci], loop[ni]) > maxTriArea) continue;

            // Check no other vertex is inside the triangle
            bool isEar = true;
            for (int j = 0; j < len; j++) {
                if (j == prevI || j == i || j == nextI) continue;
                int pj = indices[j];
                if (pointInTriangle2D(pts[pj], pts[pi], pts[ci], pts[ni])) {
                    isEar = false;
                    break;
                }
            }

            if (isEar) {
                result.push_back({loop[pi], loop[ci], loop[ni]});
                indices.erase(indices.begin() + i);
                earFound = true;
                break;
            }
        }

        if (!earFound) break;
    }

    // Remaining 3 vertices - with area check
    if (static_cast<int>(indices.size()) == 3) {
        if (maxTriArea <= 0 || triangleArea3D(V, loop[indices[0]], loop[indices[1]], loop[indices[2]]) <= maxTriArea) {
            result.push_back({loop[indices[0]], loop[indices[1]], loop[indices[2]]});
        }
    } else if (static_cast<int>(indices.size()) > 3) {
        // Fallback: fill remaining with fan (with area check)
        for (int i = 1; i < static_cast<int>(indices.size()) - 1; i++) {
            if (maxTriArea > 0 && triangleArea3D(V, loop[indices[0]], loop[indices[i]], loop[indices[i + 1]]) > maxTriArea) continue;
            result.push_back({loop[indices[0]], loop[indices[i]], loop[indices[i + 1]]});
        }
    }

    return result;
}

// ===== fillTJunction =====
// Fill T-junction with single triangle

Tri MeshFix::fillTJunction(const std::vector<BoundaryEdge>& boundary) const {
    // Build outgoing and incoming edge maps
    std::unordered_map<int, std::vector<int>> outEdges;
    std::unordered_map<int, std::vector<int>> inEdges;

    for (size_t i = 0; i < boundary.size(); i++) {
        int a = boundary[i].from;
        int b = boundary[i].to;
        outEdges[a].push_back(b);
        inEdges[b].push_back(a);
    }

    // Find vertex with 2+ outgoing edges
    for (const auto& [v, outs] : outEdges) {
        if (static_cast<int>(outs.size()) >= 2) {
            return {v, outs[1], outs[0]};
        }
    }

    // Find vertex with 2+ incoming edges
    for (const auto& [v, ins] : inEdges) {
        if (static_cast<int>(ins.size()) >= 2) {
            return {v, ins[0], ins[1]};
        }
    }

    // No T-junction found - return invalid sentinel
    return {-1, -1, -1};
}

// ===== findOpenChain =====
// Find open chain in boundary (edges that don't form a loop)

std::vector<int> MeshFix::findOpenChain(const std::vector<BoundaryEdge>& boundary) const {
    if (boundary.empty()) return {};

    std::unordered_map<int, int> outMap;
    std::unordered_map<int, int> inMap;
    for (size_t i = 0; i < boundary.size(); i++) {
        int a = boundary[i].from;
        int b = boundary[i].to;
        outMap[a] = b;
        inMap[b] = a;
    }

    // Find chain start: vertex with no incoming edge
    int start = -1;
    for (size_t i = 0; i < boundary.size(); i++) {
        int a = boundary[i].from;
        if (inMap.find(a) == inMap.end()) {
            start = a;
            break;
        }
    }
    if (start == -1) return {}; // All edges form loops

    // Trace the chain
    std::vector<int> chain;
    chain.push_back(start);
    int current = start;
    for (size_t step = 0; step < boundary.size() + 1; step++) {
        auto it = outMap.find(current);
        if (it == outMap.end()) break;
        current = it->second;
        if (current == start) break; // Would be a loop (shouldn't happen)
        chain.push_back(current);
    }

    if (static_cast<int>(chain.size()) >= 3) return chain;
    return {};
}

// ===== fillHoles =====
// Main hole filling loop (synchronous)

MeshFix::FillResult MeshFix::fillHoles(const std::vector<Vec3>& V_in,
                                        const std::vector<Tri>& T_in,
                                        double maxTriAreaOverride) const {
    std::vector<Vec3> V = V_in;
    std::vector<Tri> T = T_in;
    int filled = 0;
    int lastBoundary = INT_MAX;
    int stuckCount = 0;

    // Triangle area limit: skip ears exceeding 100x average
    double maxTriArea = maxTriAreaOverride;
    if (maxTriArea <= 0) {
        double totalArea = 0;
        for (size_t i = 0; i < T.size(); i++) {
            totalArea += triangleArea3D(V, T[i][0], T[i][1], T[i][2]);
        }
        double avgTriArea = T.size() > 0 ? totalArea / T.size() : 1.0;
        maxTriArea = avgTriArea * 100.0;
    }

    for (int iter = 0; iter < 10000; iter++) {
        auto boundary = getBoundaryEdges(T);
        if (boundary.empty()) break;

        int boundarySize = static_cast<int>(boundary.size());
        if (boundarySize >= lastBoundary) {
            stuckCount++;
            if (stuckCount > 10) break;
        } else {
            stuckCount = 0;
        }
        lastBoundary = boundarySize;

        auto loops = findAllLoops(boundary);
        bool didFill = false;

        if (!loops.empty()) {
            // Sort by loop length (smallest first)
            std::sort(loops.begin(), loops.end(),
                      [](const std::vector<int>& a, const std::vector<int>& b) {
                          return a.size() < b.size();
                      });

            for (size_t li = 0; li < loops.size(); li++) {
                const auto& loop = loops[li];
                if (static_cast<int>(loop.size()) < 3) continue;

                auto newTris = fillHoleEarClipping(V, loop, maxTriArea);
                if (newTris.empty()) continue;
                for (size_t ti = 0; ti < newTris.size(); ti++) {
                    T.push_back(newTris[ti]);
                }
                filled++;
                didFill = true;
                break;
            }
        }

        if (!didFill) {
            auto forced = fillTJunction(boundary);
            if (forced[0] != -1) {
                if (triangleArea3D(V, forced[0], forced[1], forced[2]) <= maxTriArea) {
                    T.push_back(forced);
                    filled++;
                    didFill = true;
                }
            }
        }

        if (!didFill) {
            auto chain = findOpenChain(boundary);
            if (static_cast<int>(chain.size()) >= 3) {
                auto newTris = fillHoleEarClipping(V, chain, maxTriArea);
                if (!newTris.empty()) {
                    for (size_t ti = 0; ti < newTris.size(); ti++) {
                        T.push_back(newTris[ti]);
                    }
                    filled++;
                    didFill = true;
                }
            }
        }

        if (!didFill) break;
    }

    return {std::move(V), std::move(T), filled};
}

// ===== subdivideLongBoundaryEdges =====
// Subdivide long boundary edges for better hole filling

MeshFix::SubdivResult MeshFix::subdivideLongBoundaryEdges(const std::vector<Vec3>& V_in,
                                                           const std::vector<Tri>& T_in,
                                                           double maxTriArea) const {
    std::vector<Vec3> V = V_in;
    std::vector<Tri> T = T_in;
    int splitCount = 0;

    for (int pass = 0; pass < 500; pass++) {
        // Build edge map: undirEdgeKey -> list of triangle indices
        std::unordered_map<int64_t, std::vector<int>> edgeMap;
        for (size_t i = 0; i < T.size(); i++) {
            int a = T[i][0], b = T[i][1], c = T[i][2];
            int edges[3][2] = {{a, b}, {b, c}, {c, a}};
            for (int e = 0; e < 3; e++) {
                int64_t k = undirEdgeKey(edges[e][0], edges[e][1]);
                edgeMap[k].push_back(static_cast<int>(i));
            }
        }

        // Collect boundary edges
        auto boundary = getBoundaryEdges(T);
        if (boundary.empty()) break;

        // Detect loops (directed loops only)
        auto loops = findAllLoops(boundary);

        // For each loop, estimate area and decide if subdivision is needed
        int64_t splitEdgeKey = -1;
        int splitU = -1, splitV = -1;

        for (const auto& loop : loops) {
            if (static_cast<int>(loop.size()) < 3) continue;

            // Estimate loop area (fan approximation)
            double loopArea = 0;
            for (size_t j = 1; j < loop.size() - 1; j++) {
                loopArea += triangleArea3D(V, loop[0], loop[j], loop[j + 1]);
            }

            // Estimate max ear area
            int numEars = static_cast<int>(loop.size()) - 2;
            double estimatedMaxEarArea = numEars > 0 ? loopArea / numEars * 3.0 : 0;

            // If estimated max ear area exceeds limit, subdivide longest edge in this loop
            if (estimatedMaxEarArea > maxTriArea) {
                double bestLen = 0;
                for (size_t j = 0; j < loop.size(); j++) {
                    int u = loop[j], v = loop[(j + 1) % loop.size()];
                    double dx = V[u][0] - V[v][0], dy = V[u][1] - V[v][1], dz = V[u][2] - V[v][2];
                    double len = dx * dx + dy * dy + dz * dz;
                    if (len > bestLen) {
                        bestLen = len;
                        splitU = u;
                        splitV = v;
                        splitEdgeKey = undirEdgeKey(u, v);
                    }
                }
                break; // Subdivide one edge per pass then rescan
            }
        }

        // Also check open chains
        if (splitEdgeKey == -1) {
            auto chain = findOpenChain(boundary);
            if (static_cast<int>(chain.size()) >= 3) {
                double chainArea = 0;
                for (size_t j = 1; j < chain.size() - 1; j++) {
                    chainArea += triangleArea3D(V, chain[0], chain[j], chain[j + 1]);
                }
                int numEars = static_cast<int>(chain.size()) - 2;
                double estimatedMaxEarArea = numEars > 0 ? chainArea / numEars * 3.0 : 0;
                if (estimatedMaxEarArea > maxTriArea) {
                    double bestLen = 0;
                    for (size_t j = 0; j < chain.size() - 1; j++) {
                        int u = chain[j], v = chain[j + 1];
                        double dx = V[u][0] - V[v][0], dy = V[u][1] - V[v][1], dz = V[u][2] - V[v][2];
                        double len = dx * dx + dy * dy + dz * dz;
                        if (len > bestLen) {
                            bestLen = len;
                            splitU = u;
                            splitV = v;
                            splitEdgeKey = undirEdgeKey(u, v);
                        }
                    }
                }
            }
        }

        if (splitEdgeKey == -1) break; // No subdivision needed

        // Create midpoint vertex
        int mi = static_cast<int>(V.size());
        V.push_back({(V[splitU][0] + V[splitV][0]) / 2.0,
                     (V[splitU][1] + V[splitV][1]) / 2.0,
                     (V[splitU][2] + V[splitV][2]) / 2.0});

        // Split adjacent triangles
        auto it = edgeMap.find(splitEdgeKey);
        if (it != edgeMap.end()) {
            for (int triIdx : it->second) {
                int ta = T[triIdx][0], tb = T[triIdx][1], tc = T[triIdx][2];
                int perms[3][3] = {{ta, tb, tc}, {tb, tc, ta}, {tc, ta, tb}};
                for (int pi = 0; pi < 3; pi++) {
                    int p0 = perms[pi][0], p1 = perms[pi][1], p2 = perms[pi][2];
                    if ((p0 == splitU && p1 == splitV) || (p0 == splitV && p1 == splitU)) {
                        T[triIdx] = {p0, mi, p2};
                        T.push_back({mi, p1, p2});
                        break;
                    }
                }
            }
        }

        splitCount++;
    }

    return {std::move(V), std::move(T), splitCount};
}

// ===== subdivideOversizedTriangles =====
// Subdivide oversized triangles by splitting longest non-boundary edge

MeshFix::OversizeResult MeshFix::subdivideOversizedTriangles(const std::vector<Vec3>& V_in,
                                                              const std::vector<Tri>& T_in,
                                                              double maxTriArea) const {
    std::vector<Vec3> V = V_in;
    std::vector<Tri> T = T_in;

    if (maxTriArea <= 0) {
        double totalArea = 0;
        for (size_t i = 0; i < T.size(); i++) {
            totalArea += triangleArea3D(V, T[i][0], T[i][1], T[i][2]);
        }
        maxTriArea = T.size() > 0 ? (totalArea / T.size()) * 20.0 : 1.0;
    }

    // Build edge map helper lambda
    auto buildEdgeMap = [&]() {
        std::unordered_map<int64_t, std::vector<int>> em;
        for (size_t i = 0; i < T.size(); i++) {
            int a = T[i][0], b = T[i][1], c = T[i][2];
            int edges[3][2] = {{a, b}, {b, c}, {c, a}};
            for (int e = 0; e < 3; e++) {
                int64_t k = undirEdgeKey(edges[e][0], edges[e][1]);
                em[k].push_back(static_cast<int>(i));
            }
        }
        return em;
    };

    auto edgeMap = buildEdgeMap();
    int maxIter = static_cast<int>(T.size()) * 3;

    for (int iter = 0; iter < maxIter; iter++) {
        // Find triangle with largest area exceeding maxTriArea
        int worstIdx = -1;
        double worstArea = maxTriArea;
        for (size_t i = 0; i < T.size(); i++) {
            double area = triangleArea3D(V, T[i][0], T[i][1], T[i][2]);
            if (area > worstArea) {
                worstArea = area;
                worstIdx = static_cast<int>(i);
            }
        }
        if (worstIdx == -1) break;

        int a = T[worstIdx][0], b = T[worstIdx][1], c = T[worstIdx][2];

        // Distance squared helper
        auto d2 = [&](int u, int v) -> double {
            double dx = V[u][0] - V[v][0], dy = V[u][1] - V[v][1], dz = V[u][2] - V[v][2];
            return dx * dx + dy * dy + dz * dz;
        };

        // Boundary edge check
        auto isBoundaryEdge = [&](int u, int v) -> bool {
            int64_t k = undirEdgeKey(u, v);
            auto it = edgeMap.find(k);
            if (it == edgeMap.end()) return true;
            return it->second.size() == 1;
        };

        // Candidate edges: [eu, ev, ew] where ew is opposite vertex
        struct EdgeCandidate { int eu, ev, ew; };
        std::vector<EdgeCandidate> candidates;
        int allEdges[3][3] = {{a, b, c}, {b, c, a}, {c, a, b}};
        for (int e = 0; e < 3; e++) {
            if (!isBoundaryEdge(allEdges[e][0], allEdges[e][1])) {
                candidates.push_back({allEdges[e][0], allEdges[e][1], allEdges[e][2]});
            }
        }
        if (candidates.empty()) break; // All edges are boundary -> cannot split

        // Find longest non-boundary edge
        int eu = -1, ev = -1, ew = -1;
        double bestDist = -1;
        for (const auto& cand : candidates) {
            double d = d2(cand.eu, cand.ev);
            if (d > bestDist) {
                bestDist = d;
                eu = cand.eu;
                ev = cand.ev;
                ew = cand.ew;
            }
        }

        // Create midpoint
        int mi = static_cast<int>(V.size());
        V.push_back({(V[eu][0] + V[ev][0]) / 2.0,
                     (V[eu][1] + V[ev][1]) / 2.0,
                     (V[eu][2] + V[ev][2]) / 2.0});

        // Split original triangle: [eu, ev, ew] -> [eu, mi, ew] + [mi, ev, ew]
        T[worstIdx] = {eu, mi, ew};
        T.push_back({mi, ev, ew});

        // Split neighbor triangle sharing the edge
        int64_t ek = undirEdgeKey(eu, ev);
        auto it = edgeMap.find(ek);
        if (it != edgeMap.end()) {
            for (int ni : it->second) {
                if (ni == worstIdx) continue;
                int na = T[ni][0], nb = T[ni][1], nc = T[ni][2];
                int perms[3][3] = {{na, nb, nc}, {nb, nc, na}, {nc, na, nb}};
                for (int pi = 0; pi < 3; pi++) {
                    int p0 = perms[pi][0], p1 = perms[pi][1], p2 = perms[pi][2];
                    if (p0 == ev && p1 == eu) {
                        // [ev, eu, p2] -> [ev, mi, p2] + [mi, eu, p2]
                        T[ni] = {ev, mi, p2};
                        T.push_back({mi, eu, p2});
                        break;
                    }
                    if (p0 == eu && p1 == ev) {
                        // Same direction (non-manifold case): [eu, ev, p2] -> [eu, mi, p2] + [mi, ev, p2]
                        T[ni] = {eu, mi, p2};
                        T.push_back({mi, ev, p2});
                        break;
                    }
                }
            }
        }

        // Rebuild edge map after split
        edgeMap = buildEdgeMap();
    }

    return {std::move(V), std::move(T)};
}

#include "mesh_fix.h"
#include <set>

// ============================================================
// Stage 0-6 functions ported from mesh-fix-lib.js
// (Static utility functions: computeNormal, triangleArea3D, vKey,
//  buildEdgeCounts are defined in diagnose.cpp)
// ============================================================

// --- analyzeScale (JS _analyzeScale, line ~72) ---
ScaleInfo MeshFix::analyzeScale(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    double minX =  1e30, maxX = -1e30;
    double minY =  1e30, maxY = -1e30;
    double minZ =  1e30, maxZ = -1e30;

    for (size_t i = 0; i < V.size(); i++) {
        const auto& v = V[i];
        if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
    }

    double dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    double bboxDiag = std::sqrt(dx * dx + dy * dy + dz * dz);

    double totalArea = 0;
    std::vector<double> edgeLens;
    std::unordered_set<int64_t> seen;

    for (size_t i = 0; i < T.size(); i++) {
        totalArea += MeshFix::triangleArea3D(V, T[i][0], T[i][1], T[i][2]);
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int edges[3][2] = { {a, b}, {b, c}, {c, a} };
        for (int e = 0; e < 3; e++) {
            int u = edges[e][0], v = edges[e][1];
            int64_t k = MeshFix::undirEdgeKey(u, v);
            if (seen.count(k)) continue;
            seen.insert(k);
            double ex = V[u][0] - V[v][0], ey = V[u][1] - V[v][1], ez = V[u][2] - V[v][2];
            edgeLens.push_back(std::sqrt(ex * ex + ey * ey + ez * ez));
        }
    }

    std::sort(edgeLens.begin(), edgeLens.end());
    double medianEdge = edgeLens.size() > 0 ? edgeLens[edgeLens.size() / 2] : 1.0;
    double avgArea = T.size() > 0 ? totalArea / (double)T.size() : 1.0;

    ScaleInfo info;
    info.bboxDiag = bboxDiag;
    info.medianEdge = medianEdge;
    info.avgArea = avgArea;
    info.triCount = (int)T.size();
    return info;
}

// --- resolveOptions (JS _resolveOptions + _resolveScaleDependentOptions, line ~106) ---
// Resolves all auto options. In JS, the scale from _resolveOptions is cached and
// reused by _resolveScaleDependentOptions. We replicate this by computing scale once.
RepairOptions MeshFix::resolveOptions(const std::vector<Vec3>& V, const std::vector<Tri>& T,
                                       const RepairOptions& userOpts) const {
    RepairOptions opts = userOpts;

    ScaleInfo scale;
    bool hasScale = false;

    // Phase 1: resolve mergePrecision (JS _resolveOptions)
    if (opts.mergePrecision < 0) {
        // 'auto'
        scale = analyzeScale(V, T);
        hasScale = true;
        double targetRes = scale.medianEdge / 1000.0;
        double raw = -std::log10(std::max(targetRes, 1e-15));
        opts.mergePrecision = (int)std::max(3.0, std::min(10.0, std::round(raw)));
    }

    // Phase 2: resolve scale-dependent options (JS _resolveScaleDependentOptions)
    // JS uses cached scale from phase 1 (pre-merge mesh) if available
    if (!hasScale) {
        scale = analyzeScale(V, T);
    }

    if (opts.degenerateThreshold < 0) {
        // 'auto'
        double eps = scale.medianEdge * scale.medianEdge * 1e-6;
        opts.degenerateThreshold = eps * eps;
    }

    if (opts.holeAreaMultiplier < 0) {
        // 'auto'
        opts.holeAreaMultiplier = std::max(10.0, std::min(500.0,
            100.0 * std::sqrt((double)scale.triCount / 5000.0)));
    }

    // Note: assumeClosedSolid must be computed AFTER merge in repairObject,
    // using diagnose on the post-merge mesh (matches JS _resolveScaleDependentOptions).

    return opts;
}

// --- mergeVertices (JS _mergeVertices, line ~404) ---
MeshFix::MergeResult MeshFix::mergeVertices(const std::vector<Vec3>& V, const std::vector<Tri>& T, int precision) const {
    std::unordered_map<std::string, int> vMap;
    std::vector<Vec3> newV;
    std::vector<int> indexMap(V.size());
    int merged = 0;

    for (size_t i = 0; i < V.size(); i++) {
        std::string k = MeshFix::vKey(V[i], precision);
        auto it = vMap.find(k);
        if (it != vMap.end()) {
            indexMap[i] = it->second;
            merged++;
        } else {
            indexMap[i] = (int)newV.size();
            vMap[k] = (int)newV.size();
            newV.push_back(V[i]);
        }
    }

    std::vector<Tri> newT;
    newT.reserve(T.size());
    for (size_t i = 0; i < T.size(); i++) {
        newT.push_back({ indexMap[T[i][0]], indexMap[T[i][1]], indexMap[T[i][2]] });
    }

    return { std::move(newV), std::move(newT), merged };
}

// --- removeDegenerateTriangles (JS _removeDegenerateTriangles, line ~432) ---
MeshFix::RemoveResult MeshFix::removeDegenerateTriangles(const std::vector<Vec3>& V, const std::vector<Tri>& T, double threshold) const {
    std::vector<Tri> result;
    int removed = 0;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];

        // Duplicate vertex check
        if (a == b || b == c || c == a) {
            removed++;
            continue;
        }

        // Zero-area check (collinear vertices)
        const auto& v0 = V[a];
        const auto& v1 = V[b];
        const auto& v2 = V[c];
        double e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
        double e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
        double cx = e1y * e2z - e1z * e2y;
        double cy = e1z * e2x - e1x * e2z;
        double cz = e1x * e2y - e1y * e2x;
        if (cx * cx + cy * cy + cz * cz < threshold) {
            removed++;
            continue;
        }

        result.push_back({ a, b, c });
    }

    return { std::move(result), removed };
}

// --- removeSmallShells (JS _removeSmallShells, line ~508) ---
MeshFix::ShellResult MeshFix::removeSmallShells(const std::vector<Vec3>& V, const std::vector<Tri>& T, double threshold) const {
    auto components = findConnectedComponents(T);
    if (components.size() <= 1) {
        return { V, T, 0 };
    }

    // Calculate area of each component
    double totalArea = 0;
    std::vector<double> compAreas(components.size());
    for (size_t ci = 0; ci < components.size(); ci++) {
        double area = 0;
        for (int ti : components[ci]) {
            int a = T[ti][0], b = T[ti][1], c = T[ti][2];
            double ax = V[a][0], ay = V[a][1], az = V[a][2];
            double bx = V[b][0], by = V[b][1], bz = V[b][2];
            double cx = V[c][0], cy = V[c][1], cz = V[c][2];
            double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
            double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
            double nx = e1y * e2z - e1z * e2y;
            double ny = e1z * e2x - e1x * e2z;
            double nz = e1x * e2y - e1y * e2x;
            area += std::sqrt(nx * nx + ny * ny + nz * nz) * 0.5;
        }
        compAreas[ci] = area;
        totalArea += area;
    }

    // Mark small components for removal
    std::unordered_set<int> removeTris;
    int removed = 0;
    for (size_t ci = 0; ci < components.size(); ci++) {
        if (compAreas[ci] < totalArea * threshold) {
            for (int ti : components[ci]) {
                removeTris.insert(ti);
            }
            removed++;
        }
    }
    if (removeTris.empty()) {
        return { V, T, 0 };
    }

    // Filter triangles
    std::vector<Tri> newTris;
    for (size_t i = 0; i < T.size(); i++) {
        if (!removeTris.count((int)i)) {
            newTris.push_back(T[i]);
        }
    }

    // Compact vertices
    std::set<int> usedVerts;
    for (const auto& t : newTris) {
        usedVerts.insert(t[0]);
        usedVerts.insert(t[1]);
        usedVerts.insert(t[2]);
    }
    std::unordered_map<int, int> vertMap;
    std::vector<Vec3> newVerts;
    for (int vi : usedVerts) {
        vertMap[vi] = (int)newVerts.size();
        newVerts.push_back(V[vi]);
    }
    std::vector<Tri> remappedTris;
    remappedTris.reserve(newTris.size());
    for (const auto& t : newTris) {
        remappedTris.push_back({ vertMap[t[0]], vertMap[t[1]], vertMap[t[2]] });
    }

    return { std::move(newVerts), std::move(remappedTris), removed };
}

// --- removeOppositeWindingDuplicates (JS _removeOppositeWindingDuplicates, line ~565) ---
MeshFix::OWPResult MeshFix::removeOppositeWindingDuplicates(const std::vector<Tri>& T) const {
    // Group by unorderedKey
    std::unordered_map<int64_t, std::vector<int>> groups;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int64_t uk = MeshFix::unorderedKey(a, b, c);
        groups[uk].push_back((int)i);
    }

    std::unordered_set<int> toRemove;
    int pairsRemoved = 0;

    for (auto& [uk, indices] : groups) {
        if ((int)indices.size() < 2) continue;

        // Sub-group by winding
        std::unordered_map<int64_t, std::vector<int>> byWinding;
        for (int idx : indices) {
            int a = T[idx][0], b = T[idx][1], c = T[idx][2];
            int64_t wk = MeshFix::windingKey(a, b, c);
            byWinding[wk].push_back(idx);
        }

        if ((int)byWinding.size() >= 2) {
            // Different winding directions exist -> internal wall
            auto it = byWinding.begin();
            const auto& classA = it->second;
            ++it;
            const auto& classB = it->second;
            int pairs = std::min((int)classA.size(), (int)classB.size());

            for (int j = 0; j < pairs; j++) {
                toRemove.insert(classA[j]);
                toRemove.insert(classB[j]);
                pairsRemoved++;
            }
        }
    }

    if (toRemove.empty()) {
        return { T, 0 };
    }

    std::vector<Tri> result;
    for (size_t i = 0; i < T.size(); i++) {
        if (!toRemove.count((int)i)) {
            result.push_back(T[i]);
        }
    }

    return { std::move(result), pairsRemoved };
}

// --- removeExactDuplicates (JS _removeExactDuplicates, line ~618) ---
MeshFix::DupResult MeshFix::removeExactDuplicates(const std::vector<Tri>& T) const {
    std::unordered_set<int64_t> seen;
    std::vector<Tri> result;
    int removed = 0;

    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int64_t wk = MeshFix::windingKey(a, b, c);
        if (seen.count(wk)) {
            removed++;
            continue;
        }
        seen.insert(wk);
        result.push_back({ a, b, c });
    }

    return { std::move(result), removed };
}

// --- enforceNormalConsistency (JS _enforceNormalConsistency, line ~639) ---
MeshFix::NormResult MeshFix::enforceNormalConsistency(const std::vector<Vec3>& V, const std::vector<Tri>& T, bool skipVolumeTest) const {
    int n = (int)T.size();
    if (n == 0) {
        return { T, 0 };
    }

    // Build undirected edge -> adjacency map (with direction info)
    // edgeMap: undirKey -> { fwd: [triIdx,...], rev: [triIdx,...] }
    std::unordered_map<int64_t, EdgeInfo> edgeMap;

    for (int i = 0; i < n; i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        int edges[3][2] = { {a, b}, {b, c}, {c, a} };
        for (int e = 0; e < 3; e++) {
            int u = edges[e][0], v = edges[e][1];
            int64_t k = MeshFix::undirEdgeKey(u, v);
            auto& entry = edgeMap[k];
            if (u < v) entry.fwd.push_back(i);
            else entry.rev.push_back(i);
        }
    }

    // Build adjacency relationships
    // consistent = edge crossed in opposite direction (correct orientation relationship)
    // inconsistent = edge crossed in same direction (one needs to be flipped)
    struct AdjEntry { int neighbor; bool consistent; };
    std::vector<std::vector<AdjEntry>> adj(n);

    for (auto& [k, info] : edgeMap) {
        const auto& fwd = info.fwd;
        const auto& rev = info.rev;
        int edgeTotal = (int)(fwd.size() + rev.size());
        // Skip count=4 edges in BFS adjacency to prevent cross-shell normal propagation.
        // These NM edges are resolved separately in Stage 5.
        if (edgeTotal == 4) continue;
        // fwd x rev: consistent
        for (size_t fi = 0; fi < fwd.size(); fi++) {
            for (size_t ri = 0; ri < rev.size(); ri++) {
                if (fwd[fi] != rev[ri]) {
                    adj[fwd[fi]].push_back({ rev[ri], true });
                    adj[rev[ri]].push_back({ fwd[fi], true });
                }
            }
        }
        // fwd x fwd: inconsistent
        for (size_t i = 0; i < fwd.size(); i++) {
            for (size_t j = i + 1; j < fwd.size(); j++) {
                adj[fwd[i]].push_back({ (int)fwd[j], false });
                adj[fwd[j]].push_back({ (int)fwd[i], false });
            }
        }
        // rev x rev: inconsistent
        for (size_t i = 0; i < rev.size(); i++) {
            for (size_t j = i + 1; j < rev.size(); j++) {
                adj[rev[i]].push_back({ (int)rev[j], false });
                adj[rev[j]].push_back({ (int)rev[i], false });
            }
        }
    }

    // BFS to determine orientation for each connected component
    std::vector<int8_t> orientation(n, 0); // 0=unvisited, 1=keep, 2=flip
    int totalFlipped = 0;

    for (int seed = 0; seed < n; seed++) {
        if (orientation[seed] != 0) continue;

        // Seed keeps its orientation
        orientation[seed] = 1;
        std::vector<int> componentTris;
        componentTris.push_back(seed);
        std::queue<int> queue;
        queue.push(seed);

        while (!queue.empty()) {
            int current = queue.front();
            queue.pop();
            bool curFlip = orientation[current] == 2;

            for (size_t ai = 0; ai < adj[current].size(); ai++) {
                int neighbor = adj[current][ai].neighbor;
                bool consistent = adj[current][ai].consistent;
                if (orientation[neighbor] != 0) continue;

                // curFlip XOR !consistent -> neighbor should flip
                if (curFlip != !consistent) {
                    orientation[neighbor] = 2; // flip
                } else {
                    orientation[neighbor] = 1; // keep
                }

                componentTris.push_back(neighbor);
                queue.push(neighbor);
            }
        }

        // Signed volume test to determine correct orientation for component
        double volume = 0;
        for (size_t ci = 0; ci < componentTris.size(); ci++) {
            int ti = componentTris[ci];
            int a = T[ti][0], b = T[ti][1], c = T[ti][2];
            // Apply current orientation decision for calculation
            if (orientation[ti] == 2) {
                std::swap(b, c);
            }
            const auto& v0 = V[a];
            const auto& v1 = V[b];
            const auto& v2 = V[c];
            volume += v0[0] * (v1[1] * v2[2] - v1[2] * v2[1])
                    + v0[1] * (v1[2] * v2[0] - v1[0] * v2[2])
                    + v0[2] * (v1[0] * v2[1] - v1[1] * v2[0]);
        }

        // Negative volume -> flip entire component orientation (skip if skipVolumeTest)
        if (!skipVolumeTest && volume < 0) {
            for (size_t ci = 0; ci < componentTris.size(); ci++) {
                int ti = componentTris[ci];
                orientation[ti] = orientation[ti] == 1 ? 2 : 1;
            }
        }
    }

    // Apply flips
    std::vector<Tri> result;
    result.reserve(n);
    for (int i = 0; i < n; i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        if (orientation[i] == 2) {
            result.push_back({ a, c, b });
            totalFlipped++;
        } else {
            result.push_back({ a, b, c });
        }
    }

    return { std::move(result), totalFlipped };
}

// --- separateOverlayShells ---
// For count=4 edges (overlapping shells), separate shells by duplicating shared vertices
// instead of removing triangles. This preserves all geometry.
MeshFix::SeparateResult MeshFix::separateOverlayShells(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    // Build undirected edge count map
    std::unordered_map<int64_t, int> edgeCounts;
    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        if (a == b || b == c || c == a) continue;
        edgeCounts[undirEdgeKey(a, b)]++;
        edgeCounts[undirEdgeKey(b, c)]++;
        edgeCounts[undirEdgeKey(c, a)]++;
    }

    // Check if there are any count>2 edges (overlay/non-manifold)
    bool hasOverlay = false;
    for (const auto& [k, cnt] : edgeCounts) {
        if (cnt > 2) { hasOverlay = true; break; }
    }
    if (!hasOverlay) {
        return { std::vector<Vec3>(V), std::vector<Tri>(T), 0 };
    }

    // Build connected components using ONLY manifold (count=2) edges
    int n = (int)T.size();
    std::unordered_map<int64_t, std::vector<int>> edgeToTris;
    for (int i = 0; i < n; i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        if (a == b || b == c || c == a) continue;
        int64_t e1 = undirEdgeKey(a, b);
        int64_t e2 = undirEdgeKey(b, c);
        int64_t e3 = undirEdgeKey(c, a);
        edgeToTris[e1].push_back(i);
        edgeToTris[e2].push_back(i);
        edgeToTris[e3].push_back(i);
    }

    // BFS using only count=2 edges
    std::vector<int> compId(n, -1);
    int numComps = 0;
    for (int i = 0; i < n; i++) {
        if (compId[i] >= 0) continue;
        int cid = numComps++;
        std::vector<int> queue;
        queue.push_back(i);
        compId[i] = cid;
        int head = 0;
        while (head < (int)queue.size()) {
            int ti = queue[head++];
            int a = T[ti][0], b = T[ti][1], c = T[ti][2];
            int64_t edges[3] = { undirEdgeKey(a, b), undirEdgeKey(b, c), undirEdgeKey(c, a) };
            for (int e = 0; e < 3; e++) {
                // Only traverse through count=2 (manifold) edges
                if (edgeCounts[edges[e]] != 2) continue;
                auto it = edgeToTris.find(edges[e]);
                if (it == edgeToTris.end()) continue;
                for (int ni : it->second) {
                    if (compId[ni] < 0) {
                        compId[ni] = cid;
                        queue.push_back(ni);
                    }
                }
            }
        }
    }

    if (numComps <= 1) {
        return { std::vector<Vec3>(V), std::vector<Tri>(T), 0 };
    }

    // Find vertices shared by multiple components
    // vertexComps[v] = set of component IDs that use vertex v
    std::unordered_map<int, std::unordered_set<int>> vertexComps;
    for (int i = 0; i < n; i++) {
        if (compId[i] < 0) continue;
        for (int j = 0; j < 3; j++) {
            vertexComps[T[i][j]].insert(compId[i]);
        }
    }

    // For shared vertices, determine the "primary" component (the one with most triangles)
    std::vector<int> compTriCount(numComps, 0);
    for (int i = 0; i < n; i++) {
        if (compId[i] >= 0) compTriCount[compId[i]]++;
    }

    // Create vertex mapping: for each (vertex, component) pair that isn't primary,
    // create a new duplicate vertex
    std::vector<Vec3> newV(V);
    std::vector<Tri> newT(T);

    // vertexRemap[original_vertex][component] = new_vertex_index
    std::unordered_map<int, std::unordered_map<int, int>> vertexRemap;
    int separated = 0;

    for (auto& [vi, comps] : vertexComps) {
        if ((int)comps.size() <= 1) continue;

        // Find primary component (largest)
        int primaryComp = -1;
        int maxTri = -1;
        for (int cid : comps) {
            if (compTriCount[cid] > maxTri) {
                maxTri = compTriCount[cid];
                primaryComp = cid;
            }
        }

        // For non-primary components, create duplicate vertices
        for (int cid : comps) {
            if (cid == primaryComp) continue;
            int newIdx = (int)newV.size();
            newV.push_back(V[vi]); // duplicate at same position
            vertexRemap[vi][cid] = newIdx;
            separated++;
        }
    }

    if (separated == 0) {
        return { std::vector<Vec3>(V), std::vector<Tri>(T), 0 };
    }

    // Update triangle indices for non-primary components
    for (int i = 0; i < n; i++) {
        int cid = compId[i];
        if (cid < 0) continue;
        for (int j = 0; j < 3; j++) {
            int vi = newT[i][j];
            auto it = vertexRemap.find(vi);
            if (it != vertexRemap.end()) {
                auto it2 = it->second.find(cid);
                if (it2 != it->second.end()) {
                    newT[i][j] = it2->second;
                }
            }
        }
    }

    return { std::move(newV), std::move(newT), separated };
}

// --- resolveNonManifoldEdges (JS _resolveNonManifoldEdges, line ~1043) ---
MeshFix::NMResult MeshFix::resolveNonManifoldEdges(const std::vector<Vec3>& V, const std::vector<Tri>& T, int maxIter) const {
    std::vector<Tri> tris(T);
    int totalFixed = 0;

    for (int iter = 0; iter < maxIter; iter++) {
        // Build undirected edge map
        std::unordered_map<int64_t, std::vector<int>> undirMap;
        for (size_t i = 0; i < tris.size(); i++) {
            int a = tris[i][0], b = tris[i][1], c = tris[i][2];
            int edges[3][2] = { {a, b}, {b, c}, {c, a} };
            for (int e = 0; e < 3; e++) {
                int64_t k = MeshFix::undirEdgeKey(edges[e][0], edges[e][1]);
                undirMap[k].push_back((int)i);
            }
        }

        // Identify non-manifold edges (count > 2)
        struct NMEdge { int64_t key; std::vector<int> tris; };
        std::vector<NMEdge> nmEdges;
        for (auto& [k, triList] : undirMap) {
            if ((int)triList.size() > 2) {
                nmEdges.push_back({ k, triList });
            }
        }

        if (nmEdges.empty()) break;

        // Sort NM edges by key for deterministic processing order
        // (JS Map uses insertion order; we need consistent order for reproducibility)
        std::sort(nmEdges.begin(), nmEdges.end(), [](const NMEdge& a, const NMEdge& b) {
            return a.key < b.key;
        });

        // Calculate per-triangle NM edge count
        std::unordered_map<int, int> triNMCount;
        for (const auto& nme : nmEdges) {
            for (int ti : nme.tris) {
                triNMCount[ti]++;
            }
        }

        // Calculate per-triangle scores
        std::unordered_map<int, double> triScores;
        for (const auto& nme : nmEdges) {
            for (int ti : nme.tris) {
                if (triScores.count(ti)) continue;

                int a = tris[ti][0], b = tris[ti][1], c = tris[ti][2];
                int64_t triEdges[3] = {
                    MeshFix::undirEdgeKey(a, b),
                    MeshFix::undirEdgeKey(b, c),
                    MeshFix::undirEdgeKey(c, a)
                };

                // Count manifold edges
                int manifoldEdges = 0;
                for (int e = 0; e < 3; e++) {
                    auto it = undirMap.find(triEdges[e]);
                    if (it != undirMap.end() && (int)it->second.size() == 2) {
                        manifoldEdges++;
                    }
                }

                int nmCount = triNMCount.count(ti) ? triNMCount[ti] : 0;
                double score = (double)(manifoldEdges * 10 - nmCount * 5);

                // Normal consistency with adjacent manifold faces
                Vec3 n1 = MeshFix::computeNormal(V, a, b, c);
                for (int e = 0; e < 3; e++) {
                    auto it = undirMap.find(triEdges[e]);
                    if (it == undirMap.end() || (int)it->second.size() != 2) continue;
                    int otherIdx = it->second[0] == ti ? it->second[1] : it->second[0];
                    int oa = tris[otherIdx][0], ob = tris[otherIdx][1], oc = tris[otherIdx][2];
                    Vec3 n2 = MeshFix::computeNormal(V, oa, ob, oc);
                    double dot = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2];
                    score += dot * 3.0;
                }

                triScores[ti] = score;
            }
        }

        // For each NM edge, keep the best 2 triangles
        std::unordered_set<int> toRemove;
        for (const auto& nme : nmEdges) {
            // Build scored list and sort descending by score
            std::vector<std::pair<double, int>> scored;
            for (int ti : nme.tris) {
                double s = triScores.count(ti) ? triScores[ti] : 0.0;
                scored.push_back({ s, ti });
            }
            std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
                return a.first > b.first;
            });
            for (size_t i = 2; i < scored.size(); i++) {
                toRemove.insert(scored[i].second);
            }
        }

        if (toRemove.empty()) break;
        totalFixed += (int)toRemove.size();

        std::vector<Tri> newTris;
        for (size_t i = 0; i < tris.size(); i++) {
            if (!toRemove.count((int)i)) {
                newTris.push_back(tris[i]);
            }
        }
        tris = std::move(newTris);
    }

    return { std::move(tris), totalFixed };
}

// --- resolveNonManifoldVertices (JS _resolveNonManifoldVertices, line ~1887) ---
MeshFix::NMVResult MeshFix::resolveNonManifoldVertices(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    // Work on mutable copies since vertex splitting mutates both arrays
    std::vector<Vec3> vertices(V);
    std::vector<Tri> triangles(T);
    int totalSplit = 0;

    // vertex -> adjacent triangle indices
    std::unordered_map<int, std::vector<int>> vertexTris;
    for (size_t i = 0; i < triangles.size(); i++) {
        int a = triangles[i][0], b = triangles[i][1], c = triangles[i][2];
        vertexTris[a].push_back((int)i);
        vertexTris[b].push_back((int)i);
        vertexTris[c].push_back((int)i);
    }

    for (auto& [v, triIndices] : vertexTris) {
        if ((int)triIndices.size() < 2) continue;

        // Local adjacency via edges sharing vertex v
        std::unordered_map<int64_t, std::vector<int>> edgeToTris;
        for (int ti : triIndices) {
            int a = triangles[ti][0], b = triangles[ti][1], c = triangles[ti][2];
            // Collect the other vertices of the triangle
            std::vector<int> others;
            if (a != v) others.push_back(a);
            if (b != v) others.push_back(b);
            if (c != v) others.push_back(c);
            for (int w : others) {
                int64_t ek = MeshFix::undirEdgeKey(v, w);
                edgeToTris[ek].push_back(ti);
            }
        }

        // Local adjacency graph
        std::unordered_map<int, std::unordered_set<int>> localAdj;
        for (int ti : triIndices) {
            localAdj[ti]; // ensure entry exists
        }

        for (auto& [ek, etris] : edgeToTris) {
            for (size_t i = 0; i < etris.size(); i++) {
                for (size_t j = i + 1; j < etris.size(); j++) {
                    localAdj[etris[i]].insert(etris[j]);
                    localAdj[etris[j]].insert(etris[i]);
                }
            }
        }

        // BFS to find connected components
        std::unordered_set<int> visited;
        std::vector<std::vector<int>> components;
        for (int ti : triIndices) {
            if (visited.count(ti)) continue;
            std::vector<int> comp;
            std::queue<int> queue;
            queue.push(ti);
            visited.insert(ti);
            while (!queue.empty()) {
                int cur = queue.front();
                queue.pop();
                comp.push_back(cur);
                for (int nb : localAdj[cur]) {
                    if (!visited.count(nb)) {
                        visited.insert(nb);
                        queue.push(nb);
                    }
                }
            }
            components.push_back(std::move(comp));
        }

        if ((int)components.size() <= 1) continue;

        // Assign new vertices to components 2nd and beyond
        for (size_t ci = 1; ci < components.size(); ci++) {
            int newIdx = (int)vertices.size();
            vertices.push_back({ vertices[v][0], vertices[v][1], vertices[v][2] });
            totalSplit++;

            for (int ti : components[ci]) {
                auto& tri = triangles[ti];
                if (tri[0] == v) tri[0] = newIdx;
                if (tri[1] == v) tri[1] = newIdx;
                if (tri[2] == v) tri[2] = newIdx;
            }
        }
    }

    return { std::move(vertices), std::move(triangles), totalSplit };
}

// --- removeUnusedVertices (JS _removeUnusedVertices, line ~2631) ---
MeshFix::CleanResult MeshFix::removeUnusedVertices(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    std::unordered_set<int> used;
    for (size_t i = 0; i < T.size(); i++) {
        used.insert(T[i][0]);
        used.insert(T[i][1]);
        used.insert(T[i][2]);
    }

    std::unordered_map<int, int> newIdx;
    std::vector<Vec3> newV;

    for (size_t i = 0; i < V.size(); i++) {
        if (used.count((int)i)) {
            newIdx[(int)i] = (int)newV.size();
            newV.push_back(V[i]);
        }
    }

    std::vector<Tri> newT;
    newT.reserve(T.size());
    for (size_t i = 0; i < T.size(); i++) {
        newT.push_back({ newIdx[T[i][0]], newIdx[T[i][1]], newIdx[T[i][2]] });
    }

    return { std::move(newV), std::move(newT) };
}

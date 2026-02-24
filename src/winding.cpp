#include "mesh_fix.h"

// ============================================================
// Winding repair functions ported from mesh-fix-lib.js
// ============================================================

// --- fixWindingByEdgeFlip (JS _fixWindingByEdgeFlip, line ~861) ---
// Batch version: flips ALL winding-inconsistent triangles per pass.
// Uses local WI improvement check before flipping.
// Falls back to diagonal edge flip if batch flip makes no progress.
// Returns WindingResult { triangles, fixed }.

MeshFix::WindingResult MeshFix::fixWindingByEdgeFlip(const std::vector<Vec3>& V,
                                                      const std::vector<Tri>& T,
                                                      int maxPasses) const {
    std::vector<Tri> tris(T.begin(), T.end());
    int totalFixed = 0;
    int prevWI = INT_MAX;
    int stagnantCount = 0;

    for (int pass = 0; pass < maxPasses; pass++) {
        // Build edge info: for each undirected edge, track fwd/rev triangle indices
        std::unordered_map<int64_t, EdgeInfo> edgeInfo;
        for (size_t i = 0; i < tris.size(); i++) {
            int a = tris[i][0], b = tris[i][1], c = tris[i][2];
            int edges[3][2] = { {a, b}, {b, c}, {c, a} };
            for (int e = 0; e < 3; e++) {
                int u = edges[e][0], v = edges[e][1];
                int64_t k = undirEdgeKey(u, v);
                auto& entry = edgeInfo[k];
                if (u < v) entry.fwd.push_back(static_cast<int>(i));
                else       entry.rev.push_back(static_cast<int>(i));
            }
        }

        // Collect inconsistent edges and triangles
        struct InconEdge { int64_t key; int tri1, tri2; };
        std::vector<InconEdge> inconEdges;
        std::unordered_set<int> inconTris;

        for (const auto& [k, info] : edgeInfo) {
            int total = static_cast<int>(info.fwd.size() + info.rev.size());
            if (total != 2) continue;
            if (info.fwd.size() == 2 || info.rev.size() == 2) {
                const auto& pair = (info.fwd.size() == 2) ? info.fwd : info.rev;
                inconEdges.push_back({k, pair[0], pair[1]});
                inconTris.insert(pair[0]);
                inconTris.insert(pair[1]);
            }
        }

        if (inconEdges.empty()) break;

        // Stagnation detection: if WI count doesn't decrease for 3 consecutive passes, break
        int curWI = static_cast<int>(inconEdges.size());
        if (curWI >= prevWI) {
            stagnantCount++;
            if (stagnantCount >= 3) break;
        } else {
            stagnantCount = 0;
        }
        prevWI = curWI;

        bool improved = false;

        // --- Batch triangle flip ---
        // Count how many inconsistent edges each triangle is involved in
        {
            std::unordered_map<int, int> triInconCount;
            for (const auto& ie : inconEdges) {
                triInconCount[ie.tri1]++;
                triInconCount[ie.tri2]++;
            }

            // Sort candidates by descending inconsistency count
            std::vector<int> candidates(inconTris.begin(), inconTris.end());
            std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
                return triInconCount[a] > triInconCount[b];
            });

            // Batch flip: for each candidate, check if flipping locally improves WI
            std::unordered_set<int> flippedInPass;
            for (int ti : candidates) {
                if (flippedInPass.count(ti)) continue;

                int a = tris[ti][0], b = tris[ti][1], c = tris[ti][2];
                int myEdges[3][2] = { {a, b}, {b, c}, {c, a} };
                int wiBefore = 0, wiAfter = 0;

                for (int e = 0; e < 3; e++) {
                    int u = myEdges[e][0], v = myEdges[e][1];
                    int64_t k = undirEdgeKey(u, v);
                    auto it = edgeInfo.find(k);
                    if (it == edgeInfo.end()) continue;
                    const auto& info = it->second;
                    int fwdLen = static_cast<int>(info.fwd.size());
                    int revLen = static_cast<int>(info.rev.size());
                    if (fwdLen + revLen != 2) continue;

                    // Current state
                    if (fwdLen == 2 || revLen == 2) wiBefore++;

                    // After flip: this triangle's edge direction reverses
                    bool isFwd = (u < v);
                    int newFwd = fwdLen, newRev = revLen;
                    if (isFwd) { newFwd--; newRev++; }
                    else       { newRev--; newFwd++; }
                    if (newFwd + newRev == 2 && (newFwd == 2 || newRev == 2)) wiAfter++;
                }

                if (wiAfter < wiBefore) {
                    // Execute flip: swap indices 1 and 2
                    std::swap(tris[ti][1], tris[ti][2]);
                    flippedInPass.insert(ti);
                    totalFixed++;
                    improved = true;

                    // Update edgeInfo for the flipped triangle's edges
                    // (original edge directions before flip)
                    for (int e = 0; e < 3; e++) {
                        int u = myEdges[e][0], v = myEdges[e][1];
                        int64_t k = undirEdgeKey(u, v);
                        auto it = edgeInfo.find(k);
                        if (it == edgeInfo.end()) continue;
                        auto& info = it->second;
                        bool isFwd = (u < v);
                        if (isFwd) {
                            // Remove from fwd, add to rev
                            auto pos = std::find(info.fwd.begin(), info.fwd.end(), ti);
                            if (pos != info.fwd.end()) info.fwd.erase(pos);
                            info.rev.push_back(ti);
                        } else {
                            // Remove from rev, add to fwd
                            auto pos = std::find(info.rev.begin(), info.rev.end(), ti);
                            if (pos != info.rev.end()) info.rev.erase(pos);
                            info.fwd.push_back(ti);
                        }
                    }
                }
            }
        }

        // --- Diagonal edge flip fallback ---
        // If batch flip made no progress, try edge flips on up to 50 inconsistent edges
        if (!improved) {
            int limit = std::min(static_cast<int>(inconEdges.size()), 50);
            for (int ei = 0; ei < limit; ei++) {
                int tri1 = inconEdges[ei].tri1;
                int tri2 = inconEdges[ei].tri2;
                const Tri& t1 = tris[tri1];
                const Tri& t2 = tris[tri2];

                // Find shared vertices
                std::vector<int> shared;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        if (t1[i] == t2[j]) shared.push_back(t1[i]);
                    }
                }
                if (shared.size() != 2) continue;

                int a = shared[0], b = shared[1];
                int c = -1, d = -1;
                for (int i = 0; i < 3; i++) {
                    if (t1[i] != a && t1[i] != b) c = t1[i];
                    if (t2[i] != a && t2[i] != b) d = t2[i];
                }
                if (c == d || c == -1 || d == -1) continue;

                // Check if new diagonal edge already exists
                int64_t cdKey = undirEdgeKey(c, d);
                if (edgeInfo.count(cdKey)) continue;

                // Try two diagonal flip options
                Tri options[2][2] = {
                    { {a, c, d}, {b, d, c} },
                    { {a, d, c}, {b, c, d} }
                };

                for (int opt = 0; opt < 2; opt++) {
                    std::vector<Tri> test(tris.begin(), tris.end());
                    test[tri1] = options[opt][0];
                    test[tri2] = options[opt][1];
                    int newCount = countWindingInconsistencies(test);
                    if (newCount < static_cast<int>(inconEdges.size())) {
                        tris = std::move(test);
                        totalFixed++;
                        improved = true;
                        break;
                    }
                }
                if (improved) break;
            }
        }

        if (!improved) break;
    }

    return { tris, totalFixed };
}


// --- fixWindingByLocalRemesh (JS _fixWindingByLocalRemesh, line ~770) ---
// Identifies WI edges, collects affected triangles + 1-ring,
// removes them (respecting boundary), then runs enforceNormalConsistency
// to re-orient the remaining mesh.
// Returns RemeshResult { triangles }.

MeshFix::RemeshResult MeshFix::fixWindingByLocalRemesh(const std::vector<Vec3>& V,
                                                        const std::vector<Tri>& T,
                                                        double fractionLimit) const {
    std::vector<Tri> tris(T.begin(), T.end());

    for (int iter = 0; iter < 3; iter++) {
        int wi = countWindingInconsistencies(tris);
        if (wi == 0) break;

        // Build edge info to detect inconsistent edges
        std::unordered_map<int64_t, EdgeInfo> edgeInfo;
        for (size_t i = 0; i < tris.size(); i++) {
            int a = tris[i][0], b = tris[i][1], c = tris[i][2];
            int edges[3][2] = { {a, b}, {b, c}, {c, a} };
            for (int e = 0; e < 3; e++) {
                int u = edges[e][0], v = edges[e][1];
                int64_t k = undirEdgeKey(u, v);
                auto& entry = edgeInfo[k];
                if (u < v) entry.fwd.push_back(static_cast<int>(i));
                else       entry.rev.push_back(static_cast<int>(i));
            }
        }

        // Collect triangles involved in inconsistent edges
        std::unordered_set<int> inconTriSet;
        for (const auto& [k, info] : edgeInfo) {
            int total = static_cast<int>(info.fwd.size() + info.rev.size());
            if (total != 2) continue;
            if (info.fwd.size() == 2 || info.rev.size() == 2) {
                const auto& pair = (info.fwd.size() == 2) ? info.fwd : info.rev;
                inconTriSet.insert(pair[0]);
                inconTriSet.insert(pair[1]);
            }
        }

        if (inconTriSet.empty()) break;

        // Build edge counts to identify boundary edges
        std::unordered_map<int64_t, int> edgeCount;
        for (size_t i = 0; i < tris.size(); i++) {
            int a = tris[i][0], b = tris[i][1], c = tris[i][2];
            int edges[3][2] = { {a, b}, {b, c}, {c, a} };
            for (int e = 0; e < 3; e++) {
                int64_t k = undirEdgeKey(edges[e][0], edges[e][1]);
                edgeCount[k]++;
            }
        }

        // Protect triangles adjacent to boundary edges
        std::unordered_set<int> boundaryTris;
        for (size_t i = 0; i < tris.size(); i++) {
            int a = tris[i][0], b = tris[i][1], c = tris[i][2];
            int edges[3][2] = { {a, b}, {b, c}, {c, a} };
            for (int e = 0; e < 3; e++) {
                int64_t k = undirEdgeKey(edges[e][0], edges[e][1]);
                if (edgeCount[k] == 1) {
                    boundaryTris.insert(static_cast<int>(i));
                    break;
                }
            }
        }

        // Collect vertices from inconsistent triangles
        std::unordered_set<int> inconVerts;
        for (int ti : inconTriSet) {
            inconVerts.insert(tris[ti][0]);
            inconVerts.insert(tris[ti][1]);
            inconVerts.insert(tris[ti][2]);
        }

        // Build 1-ring: all triangles touching any inconVert, excluding boundary tris
        std::unordered_set<int> ring1;
        for (size_t i = 0; i < tris.size(); i++) {
            if (boundaryTris.count(static_cast<int>(i))) continue;
            if (inconVerts.count(tris[i][0]) ||
                inconVerts.count(tris[i][1]) ||
                inconVerts.count(tris[i][2])) {
                ring1.insert(static_cast<int>(i));
            }
        }

        // If removal exceeds fraction limit, skip (not localized enough)
        if (ring1.size() > tris.size() * fractionLimit) break;

        // Remove 1-ring triangles
        std::vector<Tri> newT;
        newT.reserve(tris.size() - ring1.size());
        for (size_t i = 0; i < tris.size(); i++) {
            if (!ring1.count(static_cast<int>(i))) {
                newT.push_back(tris[i]);
            }
        }

        // Re-run BFS normal consistency on the reduced mesh
        NormResult bfsResult = enforceNormalConsistency(V, newT, true);

        int newWI = countWindingInconsistencies(bfsResult.triangles);
        if (newWI >= wi) break; // No improvement, abort

        tris = std::move(bfsResult.triangles);
    }

    return { tris };
}

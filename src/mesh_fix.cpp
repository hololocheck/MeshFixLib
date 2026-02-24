#include "mesh_fix.h"
#include <cmath>
#include <algorithm>

// (buildEdgeCounts is defined in diagnose.cpp)

// ===== dispose =====
void MeshFix::dispose() {
    // No-op in C++ (RAII handles cleanup)
}

// ===== repairObject =====
RepairResult MeshFix::repairObject(std::vector<Vec3> V, std::vector<Tri> T,
                                    ProgressCallback progress, RepairOptions opts) {
    // Deep copy input
    std::vector<Vec3> vertices(V);
    std::vector<Tri> triangles(T);

    // Resolve options (auto values)
    opts = resolveOptions(vertices, triangles, opts);

    RepairReport report;

    // Early skip: if mesh only has overlay NM edges (count=4) with B=0 and WI=0,
    // it's a valid overlapping-shell model that doesn't need repair
    if (!opts._wiRecoveryRetry) {
        std::unordered_map<int64_t, int> preEdgeCounts;
        for (size_t i = 0; i < triangles.size(); i++) {
            int a = triangles[i][0], b = triangles[i][1], c = triangles[i][2];
            if (a == b || b == c || c == a) continue;
            preEdgeCounts[undirEdgeKey(a, b)]++;
            preEdgeCounts[undirEdgeKey(b, c)]++;
            preEdgeCounts[undirEdgeKey(c, a)]++;
        }
        int preBoundary = 0, preNonManifoldReal = 0;
        bool hasOverlay = false;
        for (const auto& [k, cnt] : preEdgeCounts) {
            if (cnt == 1) preBoundary++;
            else if (cnt > 2) {
                if (cnt == 4) hasOverlay = true;
                else preNonManifoldReal++;
            }
        }
        if (preBoundary == 0 && preNonManifoldReal == 0 && hasOverlay) {
            int preWI = countWindingInconsistencies(triangles);
            if (preWI == 0) {
                progress("Mesh is valid (overlay shells only) - skipping repair");
                auto cleaned = removeUnusedVertices(vertices, triangles);
                return { std::move(cleaned.vertices), std::move(cleaned.triangles), report };
            }
        }
    }

    // Stage 0: Vertex merge
    progress("Stage 0/8: Merging vertices...");
    {
        auto mr = mergeVertices(vertices, triangles, opts.mergePrecision);
        vertices = std::move(mr.vertices);
        triangles = std::move(mr.triangles);
        report.merged = mr.merged;
    }

    // Determine assumeClosedSolid from POST-MERGE diagnose
    // (matches JS _resolveScaleDependentOptions called after merge)
    bool assumeClosedSolid;
    {
        DiagnoseResult postMergeDiag = diagnose(vertices, triangles);
        assumeClosedSolid = (postMergeDiag.boundary == 0);
    }

    // Stage 1: Degenerate triangle removal
    progress("Stage 1/8: Removing degenerate triangles...");
    {
        auto dr = removeDegenerateTriangles(vertices, triangles, opts.degenerateThreshold);
        triangles = std::move(dr.triangles);
        report.degenerateRemoved = dr.removed;
    }

    // Small shell removal (opt-in)
    if (opts.removeSmallShells) {
        progress("Removing small shells...");
        auto sr = removeSmallShells(vertices, triangles, opts.smallShellThreshold);
        vertices = std::move(sr.vertices);
        triangles = std::move(sr.triangles);
        report.smallShellsRemoved = sr.removed;
    }

    // Stage 2: Opposite winding duplicate removal
    progress("Stage 2/8: Removing opposite winding duplicates...");
    {
        auto or_ = removeOppositeWindingDuplicates(triangles);
        triangles = std::move(or_.triangles);
        report.oppositeWindingRemoved = or_.pairsRemoved;
    }

    // Stage 3: Exact duplicate removal
    progress("Stage 3/8: Removing exact duplicates...");
    {
        auto dr = removeExactDuplicates(triangles);
        triangles = std::move(dr.triangles);
        report.exactDuplicatesRemoved = dr.removed;
    }

    // Stage 4: Normal consistency
    progress("Stage 4/8: Enforcing normal consistency...");
    {
        bool skipVolume = !assumeClosedSolid;
        auto nr = enforceNormalConsistency(vertices, triangles, skipVolume);
        triangles = std::move(nr.triangles);
        report.normalsFlipped = nr.flipped;
    }

    // Stage 5: Non-manifold edge resolution
    progress("Stage 5/8: Resolving non-manifold edges...");
    {
        auto nmr = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations);
        triangles = std::move(nmr.triangles);
        report.nmFixed = nmr.fixed;
    }

    // Stage 5b: Normal re-consistency after NM edge removal (skip volume test)
    {
        auto nr = enforceNormalConsistency(vertices, triangles, true);
        triangles = std::move(nr.triangles);
        report.normalsFlipped += nr.flipped;
    }

    // Stage 6: Non-manifold vertex resolution
    progress("Stage 6/8: Resolving non-manifold vertices...");
    {
        auto nmvr = resolveNonManifoldVertices(vertices, triangles);
        vertices = std::move(nmvr.vertices);
        triangles = std::move(nmvr.triangles);
        report.nmVerticesSplit = nmvr.split;
    }

    // Compute hole max tri area
    double holeMaxTriArea;
    {
        double totalArea = 0;
        for (size_t i = 0; i < triangles.size(); i++) {
            totalArea += triangleArea3D(vertices, triangles[i][0], triangles[i][1], triangles[i][2]);
        }
        holeMaxTriArea = triangles.size() > 0
            ? (totalArea / triangles.size()) * opts.holeAreaMultiplier
            : 100.0;
    }

    // Self-intersection repair (opt-in)
    if (opts.repairSelfIntersections) {
        progress("Repairing self-intersections...");
        auto sir = repairSelfIntersections(vertices, triangles, holeMaxTriArea);
        vertices = std::move(sir.vertices);
        triangles = std::move(sir.triangles);
        report.selfIntersectionsRepaired = sir.repaired;
    }

    // Stage 7: Hole filling
    progress("Stage 7/8: Filling holes...");
    {
        auto hr = fillHoles(vertices, triangles, holeMaxTriArea);
        vertices = std::move(hr.vertices);
        triangles = std::move(hr.triangles);
        report.holesFilled = hr.filled;
    }

    // Stage 8: Final cleanup (iterative repair + hole fill)
    progress("Stage 8/8: Final cleanup...");
    for (int finalPass = 0; finalPass < 10; finalPass++) {
        auto diag = quickDiagnose(vertices, triangles);
        if (diag.isWatertight) break;

        if (diag.nonManifold > 0) {
            auto nmr = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations);
            triangles = std::move(nmr.triangles);
            auto nmvr = resolveNonManifoldVertices(vertices, triangles);
            vertices = std::move(nmvr.vertices);
            triangles = std::move(nmvr.triangles);
        }

        auto diagAfterNM = quickDiagnose(vertices, triangles);
        if (diagAfterNM.isWatertight) break;

        if (diagAfterNM.boundary > 0) {
            auto hr = fillHoles(vertices, triangles, holeMaxTriArea);
            vertices = std::move(hr.vertices);
            triangles = std::move(hr.triangles);
            report.holesFilled += hr.filled;
        }
    }

    // Stage 8b: Final NM resolution - remove triangles around NM edges, re-fill
    {
        auto diagFinal = quickDiagnose(vertices, triangles);
        if (diagFinal.nonManifold > 0) {
            progress("Resolving remaining NM edges...");
            // Build edge -> triangle index
            std::unordered_map<int64_t, std::vector<int>> edgeToTris;
            for (size_t i = 0; i < triangles.size(); i++) {
                int a = triangles[i][0], b = triangles[i][1], c = triangles[i][2];
                int pairs[3][2] = {{a,b},{b,c},{c,a}};
                for (int e = 0; e < 3; e++) {
                    int64_t k = undirEdgeKey(pairs[e][0], pairs[e][1]);
                    edgeToTris[k].push_back((int)i);
                }
            }

            // Collect NM edges and sort for deterministic processing
            std::vector<int64_t> nmEdgeKeys;
            for (auto& [k, tris] : edgeToTris) {
                if ((int)tris.size() > 2) nmEdgeKeys.push_back(k);
            }
            std::sort(nmEdgeKeys.begin(), nmEdgeKeys.end());

            // Score triangles by NM edge count
            std::unordered_map<int, int> triNMEdgeCount;
            for (int64_t k : nmEdgeKeys) {
                for (int ti : edgeToTris[k]) {
                    triNMEdgeCount[ti]++;
                }
            }

            // Remove excess triangles (keep 2 per NM edge)
            std::unordered_set<int> toRemove;
            for (int64_t k : nmEdgeKeys) {
                auto& tris = edgeToTris[k];
                if ((int)tris.size() <= 2) continue;
                std::vector<int> trisOnEdge;
                for (int ti : tris) {
                    if (toRemove.find(ti) == toRemove.end()) {
                        trisOnEdge.push_back(ti);
                    }
                }
                if ((int)trisOnEdge.size() <= 2) continue;
                // Sort by NM edge count descending
                std::sort(trisOnEdge.begin(), trisOnEdge.end(), [&](int a, int b) {
                    return triNMEdgeCount[a] > triNMEdgeCount[b];
                });
                for (int i = 0; i < (int)trisOnEdge.size() - 2; i++) {
                    toRemove.insert(trisOnEdge[i]);
                }
            }

            if (!toRemove.empty()) {
                std::vector<Tri> newT;
                for (int i = 0; i < (int)triangles.size(); i++) {
                    if (toRemove.find(i) == toRemove.end()) {
                        newT.push_back(triangles[i]);
                    }
                }
                triangles = std::move(newT);

                auto nmvr2 = resolveNonManifoldVertices(vertices, triangles);
                vertices = std::move(nmvr2.vertices);
                triangles = std::move(nmvr2.triangles);

                auto hr2 = fillHoles(vertices, triangles, holeMaxTriArea);
                vertices = std::move(hr2.vertices);
                triangles = std::move(hr2.triangles);
                report.holesFilled += hr2.filled;

                auto nmr2 = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations);
                triangles = std::move(nmr2.triangles);

                auto nmvr3 = resolveNonManifoldVertices(vertices, triangles);
                vertices = std::move(nmvr3.vertices);
                triangles = std::move(nmvr3.triangles);
            }
        }
    }

    // Final pass: Normal re-consistency (use volume test if mesh is closed)
    progress("Final pass: Normal re-consistency...");
    {
        bool skipVol = quickDiagnose(vertices, triangles).boundary > 0;
        auto nr2 = enforceNormalConsistency(vertices, triangles, skipVol);
        triangles = std::move(nr2.triangles);
        report.normalsFlipped += nr2.flipped;
    }
    // Fix winding by edge flip
    {
        auto efr = fixWindingByEdgeFlip(vertices, triangles, opts.edgeFlipPasses);
        triangles = std::move(efr.triangles);
    }

    // Local remesh for remaining WI (only if NM=0)
    {
        auto preDiag = diagnose(vertices, triangles);
        if (preDiag.nonManifold == 0 && preDiag.windingInconsistencies > 0) {
            progress("Local remesh for winding inconsistencies...");
            double wiRatio1 = (double)preDiag.windingInconsistencies / triangles.size();
            double adaptiveLimit1 = wiRatio1 > 0.05 ? 0.6 : opts.localRemeshLimit;
            auto remeshResult = fixWindingByLocalRemesh(vertices, triangles, adaptiveLimit1);
            std::vector<Tri> remeshedT = std::move(remeshResult.triangles);

            // Post-remesh repair pipeline
            auto nmvr = resolveNonManifoldVertices(vertices, remeshedT);
            std::vector<Vec3> remeshedV = std::move(nmvr.vertices);
            remeshedT = std::move(nmvr.triangles);

            auto hr = fillHoles(remeshedV, remeshedT, holeMaxTriArea);
            remeshedV = std::move(hr.vertices);
            remeshedT = std::move(hr.triangles);
            report.holesFilled += hr.filled;

            for (int rp = 0; rp < 5; rp++) {
                auto rd = diagnose(remeshedV, remeshedT);
                if (rd.nonManifold == 0 && rd.boundary <= preDiag.boundary) break;
                if (rd.nonManifold > 0) {
                    remeshedT = resolveNonManifoldEdges(remeshedV, remeshedT, opts.nmEdgeIterations).triangles;
                }
                auto rd2 = diagnose(remeshedV, remeshedT);
                if (rd2.boundary > 0) {
                    auto rh = fillHoles(remeshedV, remeshedT, holeMaxTriArea);
                    remeshedV = std::move(rh.vertices);
                    remeshedT = std::move(rh.triangles);
                    report.holesFilled += rh.filled;
                }
                if (rd2.nonManifold == 0 && rd2.boundary <= preDiag.boundary) break;
            }

            // Remove duplicates created by remesh
            remeshedT = removeDegenerateTriangles(remeshedV, remeshedT, opts.degenerateThreshold).triangles;
            remeshedT = removeOppositeWindingDuplicates(remeshedT).triangles;
            remeshedT = removeExactDuplicates(remeshedT).triangles;

            // Post-duplicate-removal repair
            for (int rp2 = 0; rp2 < 5; rp2++) {
                auto rd3 = diagnose(remeshedV, remeshedT);
                if (rd3.nonManifold == 0 && rd3.boundary <= preDiag.boundary) break;
                if (rd3.nonManifold > 0) {
                    remeshedT = resolveNonManifoldEdges(remeshedV, remeshedT, opts.nmEdgeIterations).triangles;
                }
                auto rd4 = diagnose(remeshedV, remeshedT);
                if (rd4.boundary > 0) {
                    auto rh2 = fillHoles(remeshedV, remeshedT, holeMaxTriArea);
                    remeshedV = std::move(rh2.vertices);
                    remeshedT = std::move(rh2.triangles);
                }
                if (rd4.nonManifold == 0 && rd4.boundary <= preDiag.boundary) break;
            }

            remeshedT = enforceNormalConsistency(remeshedV, remeshedT, true).triangles;
            auto ef2 = fixWindingByEdgeFlip(remeshedV, remeshedT, opts.edgeFlipPasses);
            remeshedT = std::move(ef2.triangles);

            // Post-BFS repair: fix NM/boundary introduced by remesh
            for (int postBfsPass = 0; postBfsPass < 5; postBfsPass++) {
                auto pbDiag = quickDiagnose(remeshedV, remeshedT);
                if (pbDiag.nonManifold == 0 && pbDiag.boundary == 0) break;
                if (pbDiag.nonManifold > 0) {
                    remeshedT = resolveNonManifoldEdges(remeshedV, remeshedT, opts.nmEdgeIterations).triangles;
                    auto nmvr3 = resolveNonManifoldVertices(remeshedV, remeshedT);
                    remeshedV = std::move(nmvr3.vertices); remeshedT = std::move(nmvr3.triangles);
                }
                auto pbDiag2 = quickDiagnose(remeshedV, remeshedT);
                if (pbDiag2.boundary > 0) {
                    auto pbHr = fillHoles(remeshedV, remeshedT, holeMaxTriArea);
                    remeshedV = std::move(pbHr.vertices); remeshedT = std::move(pbHr.triangles);
                }
            }
            // Re-run BFS after NM/boundary fix
            {
                auto pbCheck = quickDiagnose(remeshedV, remeshedT);
                if (pbCheck.nonManifold == 0) {
                    remeshedT = enforceNormalConsistency(remeshedV, remeshedT, true).triangles;
                    remeshedT = fixWindingByEdgeFlip(remeshedV, remeshedT, opts.edgeFlipPasses).triangles;
                }
            }

            // Giant triangle check
            double preMaxArea = 0;
            for (size_t i = 0; i < triangles.size(); i++) {
                int a = triangles[i][0], b = triangles[i][1], c = triangles[i][2];
                double e1x = vertices[b][0]-vertices[a][0], e1y = vertices[b][1]-vertices[a][1], e1z = vertices[b][2]-vertices[a][2];
                double e2x = vertices[c][0]-vertices[a][0], e2y = vertices[c][1]-vertices[a][1], e2z = vertices[c][2]-vertices[a][2];
                double cx = e1y*e2z-e1z*e2y, cy = e1z*e2x-e1x*e2z, cz = e1x*e2y-e1y*e2x;
                double area = cx*cx + cy*cy + cz*cz;
                if (area > preMaxArea) preMaxArea = area;
            }
            double postMaxArea = 0;
            for (size_t i = 0; i < remeshedT.size(); i++) {
                int a = remeshedT[i][0], b = remeshedT[i][1], c = remeshedT[i][2];
                double e1x = remeshedV[b][0]-remeshedV[a][0], e1y = remeshedV[b][1]-remeshedV[a][1], e1z = remeshedV[b][2]-remeshedV[a][2];
                double e2x = remeshedV[c][0]-remeshedV[a][0], e2y = remeshedV[c][1]-remeshedV[a][1], e2z = remeshedV[c][2]-remeshedV[a][2];
                double cx = e1y*e2z-e1z*e2y, cy = e1z*e2x-e1x*e2z, cz = e1x*e2y-e1y*e2x;
                double area = cx*cx + cy*cy + cz*cz;
                if (area > postMaxArea) postMaxArea = area;
            }

            // Accept if improved
            auto postDiag = diagnose(remeshedV, remeshedT);
            if (postDiag.nonManifold <= preDiag.nonManifold &&
                postDiag.boundary <= preDiag.boundary &&
                postDiag.windingInconsistencies < preDiag.windingInconsistencies &&
                postDiag.oppositeWindingPairs <= preDiag.oppositeWindingPairs &&
                postMaxArea <= preMaxArea * 4) {
                vertices = std::move(remeshedV);
                triangles = std::move(remeshedT);
            }
        }
    }

    // Fill remaining holes with boundary edge subdivision
    {
        auto preBDiag = diagnose(vertices, triangles);
        if (preBDiag.boundary > 0 && preBDiag.nonManifold == 0) {
            progress("Subdividing boundary edges and filling...");

            for (int bcPass = 0; bcPass < 10; bcPass++) {
                auto bcDiag = diagnose(vertices, triangles);
                if (bcDiag.boundary == 0) break;

                // 1. Fill small directed loops directly
                auto bcBoundary = getBoundaryEdges(triangles);
                auto bcLoops = findAllLoops(bcBoundary);
                int bcLoopFilled = 0;
                for (auto& loop : bcLoops) {
                    if ((int)loop.size() < 3) continue;
                    auto newTris = fillHoleEarClipping(vertices, loop, holeMaxTriArea);
                    if (!newTris.empty()) {
                        for (auto& t : newTris) triangles.push_back(t);
                        bcLoopFilled++;
                    }
                }
                report.holesFilled += bcLoopFilled;

                // 2. Subdivide long boundary edges (directed loops only)
                auto bSubdiv = subdivideLongBoundaryEdges(vertices, triangles, holeMaxTriArea);
                vertices = std::move(bSubdiv.vertices);
                triangles = std::move(bSubdiv.triangles);

                if (bSubdiv.splitCount > 0) {
                    // 3. Fill loops after subdivision
                    bcBoundary = getBoundaryEdges(triangles);
                    bcLoops = findAllLoops(bcBoundary);
                    for (auto& loop : bcLoops) {
                        if ((int)loop.size() < 3) continue;
                        auto newTris = fillHoleEarClipping(vertices, loop, holeMaxTriArea);
                        if (!newTris.empty()) {
                            for (auto& t : newTris) triangles.push_back(t);
                            bcLoopFilled++;
                        }
                    }
                    report.holesFilled += bcLoopFilled;
                }

                if (bcLoopFilled == 0 && bSubdiv.splitCount == 0) break;
            }

            // Post-fill normal consistency
            auto bcDiagAfter = diagnose(vertices, triangles);
            if (bcDiagAfter.windingInconsistencies > 0 && bcDiagAfter.nonManifold == 0) {
                triangles = enforceNormalConsistency(vertices, triangles, true).triangles;
                triangles = fixWindingByEdgeFlip(vertices, triangles, opts.edgeFlipPasses).triangles;

                // Local remesh if WI remains
                auto bcDiag2 = diagnose(vertices, triangles);
                if (bcDiag2.windingInconsistencies > 0 && bcDiag2.nonManifold == 0) {
                    double bcWiRatio = (double)bcDiag2.windingInconsistencies / triangles.size();
                    double bcAdaptiveLimit = bcWiRatio > 0.05 ? 0.6 : opts.localRemeshLimit;
                    auto bcRemesh = fixWindingByLocalRemesh(vertices, triangles, bcAdaptiveLimit);
                    std::vector<Tri> rT = std::move(bcRemesh.triangles);
                    std::vector<Vec3> rV = vertices;

                    auto nmvr = resolveNonManifoldVertices(rV, rT);
                    rV = std::move(nmvr.vertices); rT = std::move(nmvr.triangles);
                    auto rh = fillHoles(rV, rT, holeMaxTriArea);
                    rV = std::move(rh.vertices); rT = std::move(rh.triangles);

                    for (int rp = 0; rp < 3; rp++) {
                        auto rd = diagnose(rV, rT);
                        if (rd.nonManifold == 0 && rd.boundary <= bcDiag2.boundary) break;
                        if (rd.nonManifold > 0) rT = resolveNonManifoldEdges(rV, rT, opts.nmEdgeIterations).triangles;
                        auto rd2 = diagnose(rV, rT);
                        if (rd2.boundary > 0) {
                            auto rh2 = fillHoles(rV, rT, holeMaxTriArea);
                            rV = std::move(rh2.vertices); rT = std::move(rh2.triangles);
                        }
                    }
                    rT = removeDegenerateTriangles(rV, rT, opts.degenerateThreshold).triangles;
                    rT = removeOppositeWindingDuplicates(rT).triangles;
                    rT = removeExactDuplicates(rT).triangles;
                    rT = enforceNormalConsistency(rV, rT, true).triangles;
                    rT = fixWindingByEdgeFlip(rV, rT, opts.edgeFlipPasses).triangles;

                    // Post-BFS repair: fix NM/boundary introduced by remesh
                    for (int postBfsPass = 0; postBfsPass < 5; postBfsPass++) {
                        auto pbDiag = quickDiagnose(rV, rT);
                        if (pbDiag.nonManifold == 0 && pbDiag.boundary == 0) break;
                        if (pbDiag.nonManifold > 0) {
                            rT = resolveNonManifoldEdges(rV, rT, opts.nmEdgeIterations).triangles;
                            auto nmvr2 = resolveNonManifoldVertices(rV, rT);
                            rV = std::move(nmvr2.vertices); rT = std::move(nmvr2.triangles);
                        }
                        auto pbDiag2 = quickDiagnose(rV, rT);
                        if (pbDiag2.boundary > 0) {
                            auto pbHr = fillHoles(rV, rT, holeMaxTriArea);
                            rV = std::move(pbHr.vertices); rT = std::move(pbHr.triangles);
                        }
                    }
                    {
                        auto pbCheck = quickDiagnose(rV, rT);
                        if (pbCheck.nonManifold == 0) {
                            rT = enforceNormalConsistency(rV, rT, true).triangles;
                            rT = fixWindingByEdgeFlip(rV, rT, opts.edgeFlipPasses).triangles;
                        }
                    }

                    auto postD = diagnose(rV, rT);
                    if (postD.nonManifold <= bcDiag2.nonManifold &&
                        postD.boundary <= bcDiag2.boundary &&
                        postD.windingInconsistencies < bcDiag2.windingInconsistencies) {
                        vertices = std::move(rV);
                        triangles = std::move(rT);
                    }
                }
            }
        }
    }

    // Final NM cleanup
    {
        auto finalDiag = quickDiagnose(vertices, triangles);
        if (finalDiag.nonManifold > 0) {
            progress("Final NM cleanup...");
            for (int fc = 0; fc < 5; fc++) {
                auto fd = quickDiagnose(vertices, triangles);
                if (fd.nonManifold == 0) break;
                triangles = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations).triangles;
                auto nmvr = resolveNonManifoldVertices(vertices, triangles);
                vertices = std::move(nmvr.vertices); triangles = std::move(nmvr.triangles);
                auto fd2 = quickDiagnose(vertices, triangles);
                if (fd2.boundary > 0) {
                    auto hr = fillHoles(vertices, triangles, holeMaxTriArea);
                    vertices = std::move(hr.vertices); triangles = std::move(hr.triangles);
                    report.holesFilled += hr.filled;
                }
            }

            auto fd3 = quickDiagnose(vertices, triangles);
            if (fd3.nonManifold > 0) {
                auto ec = buildEdgeCounts(triangles);
                // Collect and sort NM edge keys for deterministic processing (skip count=4 overlay)
                std::vector<int64_t> nmKeys;
                for (auto& [k, cnt] : ec) {
                    if (cnt > 2 && cnt != 4) nmKeys.push_back(k);
                }
                std::sort(nmKeys.begin(), nmKeys.end());

                std::unordered_set<int> toRemove;
                for (int64_t k : nmKeys) {
                    int cnt = ec[k];
                    if (cnt <= 2 || cnt == 4) continue;
                    int a = (int)(k / 1000000), b = (int)(k % 1000000);
                    std::vector<int> trisOnEdge;
                    for (int i = 0; i < (int)triangles.size(); i++) {
                        if (toRemove.find(i) != toRemove.end()) continue;
                        int x = triangles[i][0], y = triangles[i][1], z = triangles[i][2];
                        if ((x == a || y == a || z == a) && (x == b || y == b || z == b)) {
                            trisOnEdge.push_back(i);
                        }
                    }
                    // Score by NM involvement
                    std::vector<std::pair<int, int>> scores; // (nmCount, triIdx)
                    for (int ti : trisOnEdge) {
                        int nmCnt = 0;
                        int x = triangles[ti][0], y = triangles[ti][1], z = triangles[ti][2];
                        int edges[3][2] = {{x,y},{y,z},{z,x}};
                        for (int e = 0; e < 3; e++) {
                            int64_t ek = undirEdgeKey(edges[e][0], edges[e][1]);
                            auto it = ec.find(ek);
                            if (it != ec.end() && it->second > 2 && it->second != 4) nmCnt++;
                        }
                        scores.push_back({nmCnt, ti});
                    }
                    std::sort(scores.begin(), scores.end(), [](auto& a, auto& b) {
                        return a.first > b.first;
                    });
                    for (int i = 0; i < (int)scores.size() - 2; i++) {
                        toRemove.insert(scores[i].second);
                    }
                }

                if (!toRemove.empty()) {
                    std::vector<Tri> newT;
                    for (int i = 0; i < (int)triangles.size(); i++) {
                        if (toRemove.find(i) == toRemove.end()) newT.push_back(triangles[i]);
                    }
                    triangles = std::move(newT);
                    auto nmvr2 = resolveNonManifoldVertices(vertices, triangles);
                    vertices = std::move(nmvr2.vertices); triangles = std::move(nmvr2.triangles);
                    auto hr2 = fillHoles(vertices, triangles, holeMaxTriArea);
                    vertices = std::move(hr2.vertices); triangles = std::move(hr2.triangles);
                    report.holesFilled += hr2.filled;
                }
            }
        }
    }

    // Aggressive NM cleanup: if 1-3 NM edges remain, remove ALL adjacent triangles and re-fill
    {
        auto aggDiag = quickDiagnose(vertices, triangles);
        if (aggDiag.nonManifold > 0 && aggDiag.nonManifold <= 3) {
            auto ec = buildEdgeCounts(triangles);
            std::unordered_set<int> nmVerts;
            for (auto& [k, cnt] : ec) {
                if (cnt <= 2 || cnt == 4) continue;
                int a = (int)(k / 1000000), b = (int)(k % 1000000);
                nmVerts.insert(a);
                nmVerts.insert(b);
            }
            // Remove all triangles touching any NM vertex (1-ring removal)
            std::unordered_set<int> toRemove;
            for (int i = 0; i < (int)triangles.size(); i++) {
                int x = triangles[i][0], y = triangles[i][1], z = triangles[i][2];
                if (nmVerts.count(x) || nmVerts.count(y) || nmVerts.count(z)) {
                    toRemove.insert(i);
                }
            }
            if (!toRemove.empty()) {
                std::vector<Tri> newT;
                for (int i = 0; i < (int)triangles.size(); i++) {
                    if (!toRemove.count(i)) newT.push_back(triangles[i]);
                }
                triangles = std::move(newT);
                auto nmvr = resolveNonManifoldVertices(vertices, triangles);
                vertices = std::move(nmvr.vertices); triangles = std::move(nmvr.triangles);
                auto hr = fillHoles(vertices, triangles, holeMaxTriArea);
                vertices = std::move(hr.vertices); triangles = std::move(hr.triangles);
                report.holesFilled += hr.filled;
                // Final NM edge resolution
                triangles = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations).triangles;
                auto nmvr2 = resolveNonManifoldVertices(vertices, triangles);
                vertices = std::move(nmvr2.vertices); triangles = std::move(nmvr2.triangles);
            }
        }
    }

    // Post-NM-cleanup hole filling (boundary edge subdivision + fill)
    {
        auto postNMDiag = diagnose(vertices, triangles);
        if (postNMDiag.boundary > 0 && postNMDiag.nonManifold == 0) {
            progress("Final hole filling (boundary subdivision)...");
            auto bSubdiv = subdivideLongBoundaryEdges(vertices, triangles, holeMaxTriArea);
            vertices = std::move(bSubdiv.vertices);
            triangles = std::move(bSubdiv.triangles);
            auto hr = fillHoles(vertices, triangles, holeMaxTriArea);
            vertices = std::move(hr.vertices);
            triangles = std::move(hr.triangles);
            report.holesFilled += hr.filled;
        }
    }

    // Post-fill WI fix
    {
        auto postFillDiag = diagnose(vertices, triangles);
        if (postFillDiag.windingInconsistencies > 0 && postFillDiag.nonManifold == 0) {
            progress("Final normal consistency...");
            triangles = enforceNormalConsistency(vertices, triangles, true).triangles;
            int maxFlipPasses = std::max(opts.edgeFlipPasses, postFillDiag.windingInconsistencies);
            triangles = fixWindingByEdgeFlip(vertices, triangles, maxFlipPasses).triangles;

            // Local remesh if WI remains
            auto postFixDiag = diagnose(vertices, triangles);
            if (postFixDiag.windingInconsistencies > 0 && postFixDiag.nonManifold == 0) {
                double wiRatio = (double)postFixDiag.windingInconsistencies / triangles.size();
                double adaptiveLimit = wiRatio > 0.05 ? 0.6 : opts.localRemeshLimit;
                auto remesh = fixWindingByLocalRemesh(vertices, triangles, adaptiveLimit);
                std::vector<Tri> rT = std::move(remesh.triangles);
                std::vector<Vec3> rV = vertices;

                auto nmvr = resolveNonManifoldVertices(rV, rT);
                rV = std::move(nmvr.vertices); rT = std::move(nmvr.triangles);
                auto rh = fillHoles(rV, rT, holeMaxTriArea);
                rV = std::move(rh.vertices); rT = std::move(rh.triangles);

                for (int rp = 0; rp < 3; rp++) {
                    auto rd = diagnose(rV, rT);
                    if (rd.nonManifold == 0 && rd.boundary == 0) break;
                    if (rd.nonManifold > 0) rT = resolveNonManifoldEdges(rV, rT, opts.nmEdgeIterations).triangles;
                    auto rd2 = diagnose(rV, rT);
                    if (rd2.boundary > 0) {
                        auto rh2 = fillHoles(rV, rT, holeMaxTriArea);
                        rV = std::move(rh2.vertices); rT = std::move(rh2.triangles);
                    }
                }
                rT = removeDegenerateTriangles(rV, rT, opts.degenerateThreshold).triangles;
                rT = removeOppositeWindingDuplicates(rT).triangles;
                rT = removeExactDuplicates(rT).triangles;
                rT = enforceNormalConsistency(rV, rT, true).triangles;
                int maxFlipPasses2 = std::max(opts.edgeFlipPasses, postFixDiag.windingInconsistencies);
                rT = fixWindingByEdgeFlip(rV, rT, maxFlipPasses2).triangles;

                // Post-BFS repair: fix NM/boundary introduced by remesh
                for (int postBfsPass = 0; postBfsPass < 5; postBfsPass++) {
                    auto pbDiag = quickDiagnose(rV, rT);
                    if (pbDiag.nonManifold == 0 && pbDiag.boundary == 0) break;
                    if (pbDiag.nonManifold > 0) {
                        rT = resolveNonManifoldEdges(rV, rT, opts.nmEdgeIterations).triangles;
                        auto nmvr3 = resolveNonManifoldVertices(rV, rT);
                        rV = std::move(nmvr3.vertices); rT = std::move(nmvr3.triangles);
                    }
                    auto pbDiag2 = quickDiagnose(rV, rT);
                    if (pbDiag2.boundary > 0) {
                        auto pbHr = fillHoles(rV, rT, holeMaxTriArea);
                        rV = std::move(pbHr.vertices); rT = std::move(pbHr.triangles);
                    }
                }
                // Aggressive NM fix: remove 1-ring around remaining NM edges
                {
                    auto pbNmDiag = quickDiagnose(rV, rT);
                    if (pbNmDiag.nonManifold > 0 && pbNmDiag.nonManifold <= 3) {
                        auto ec = buildEdgeCounts(rT);
                        std::unordered_set<int> nmVerts;
                        for (auto& [k, cnt] : ec) {
                            if (cnt <= 2 || cnt == 4) continue;
                            nmVerts.insert((int)(k / 1000000));
                            nmVerts.insert((int)(k % 1000000));
                        }
                        std::unordered_set<int> toRemove;
                        for (int i = 0; i < (int)rT.size(); i++) {
                            if (nmVerts.count(rT[i][0]) || nmVerts.count(rT[i][1]) || nmVerts.count(rT[i][2])) {
                                toRemove.insert(i);
                            }
                        }
                        if (!toRemove.empty()) {
                            std::vector<Tri> newT;
                            for (int i = 0; i < (int)rT.size(); i++) {
                                if (!toRemove.count(i)) newT.push_back(rT[i]);
                            }
                            rT = std::move(newT);
                            auto nmvr4 = resolveNonManifoldVertices(rV, rT);
                            rV = std::move(nmvr4.vertices); rT = std::move(nmvr4.triangles);
                            auto pbHr2 = fillHoles(rV, rT, holeMaxTriArea);
                            rV = std::move(pbHr2.vertices); rT = std::move(pbHr2.triangles);
                            rT = resolveNonManifoldEdges(rV, rT, opts.nmEdgeIterations).triangles;
                            auto nmvr5 = resolveNonManifoldVertices(rV, rT);
                            rV = std::move(nmvr5.vertices); rT = std::move(nmvr5.triangles);
                        }
                    }
                }
                // Re-run BFS after NM/boundary fix to ensure WI stays 0
                {
                    auto pbCheck = quickDiagnose(rV, rT);
                    if (pbCheck.nonManifold == 0) {
                        rT = enforceNormalConsistency(rV, rT, true).triangles;
                        rT = fixWindingByEdgeFlip(rV, rT, maxFlipPasses2).triangles;
                    }
                }

                auto postRemesh = diagnose(rV, rT);
                if (postRemesh.nonManifold <= postFixDiag.nonManifold &&
                    postRemesh.boundary <= postFixDiag.boundary &&
                    postRemesh.windingInconsistencies < postFixDiag.windingInconsistencies) {
                    vertices = std::move(rV);
                    triangles = std::move(rT);
                }
            }
        }
    }

    // === Deep Repair (voxel reconstruction) ===
    if (opts.deepRepair == 2 ||
        (opts.deepRepair == 1 && !quickDiagnose(vertices, triangles).isWatertight)) {
        progress("Deep Repair: Starting voxel reconstruction...");
        auto drResult = repairViaVoxelReconstruction(vertices, triangles, progress, opts);
        if (drResult.resolution > 0) {
            vertices = std::move(drResult.V);
            triangles = std::move(drResult.T);
            report.deepRepairApplied = true;
            report.deepRepairResolution = drResult.resolution;
        }
    }

    // Final orientation verification: ensure outward-facing normals for closed meshes
    {
        auto orientDiag = quickDiagnose(vertices, triangles);
        if (orientDiag.isWatertight) {
            // Compute signed volume - negative means inside-out
            double volume = 0;
            for (size_t i = 0; i < triangles.size(); i++) {
                const auto& v0 = vertices[triangles[i][0]];
                const auto& v1 = vertices[triangles[i][1]];
                const auto& v2 = vertices[triangles[i][2]];
                volume += v0[0] * (v1[1] * v2[2] - v1[2] * v2[1])
                        + v0[1] * (v1[2] * v2[0] - v1[0] * v2[2])
                        + v0[2] * (v1[0] * v2[1] - v1[1] * v2[0]);
            }
            if (volume < 0) {
                for (auto& t : triangles) {
                    std::swap(t[1], t[2]);
                }
                report.normalsFlipped += (int)triangles.size();
            }
        }
    }

    // WI Recovery: retry with self-intersection repair
    {
        auto wiPreCheck = diagnose(vertices, triangles);
        if (wiPreCheck.windingInconsistencies > 0 && wiPreCheck.nonManifold <= 3 &&
            !opts.repairSelfIntersections && !opts._wiRecoveryRetry) {
            progress("WI Recovery: Retrying with self-intersection repair...");
            RepairOptions retryOpts = opts;
            retryOpts.repairSelfIntersections = true;
            retryOpts._wiRecoveryRetry = true;
            auto retryResult = repairObject(V, T, progress, retryOpts);
            auto retryDiag = diagnose(retryResult.V, retryResult.T);

            // Check self-intersections in retry result before accepting
            int retrySICount = 0;
            if (retryDiag.windingInconsistencies < wiPreCheck.windingInconsistencies &&
                retryDiag.boundary <= wiPreCheck.boundary + 2) {
                auto siPairs = detectSelfIntersections(retryResult.V, retryResult.T);
                retrySICount = (int)siPairs.size();
            }

            // Triangle preservation check: reject retry if it loses >40% of pre-recovery triangles
            int preRecoveryTriCount = (int)triangles.size();
            bool triCountOk = (int)retryResult.T.size() >= (int)(preRecoveryTriCount * 0.6);

            if (retryDiag.windingInconsistencies < wiPreCheck.windingInconsistencies &&
                retryDiag.boundary <= wiPreCheck.boundary + 2 &&
                retrySICount <= (int)(retryResult.T.size() * 0.05) &&
                triCountOk) {
                // Final NM resolution for retry result
                if (retryDiag.nonManifold > 0 && retryDiag.nonManifold <= 5) {
                    std::vector<Vec3> rV = retryResult.V;
                    std::vector<Tri> rT = retryResult.T;

                    for (int nmPass = 0; nmPass < 3; nmPass++) {
                        auto nmDiag = diagnose(rV, rT);
                        if (nmDiag.nonManifold == 0) break;
                        auto ec = buildEdgeCounts(rT);
                        // Collect and sort NM edge keys for deterministic processing (skip count=4 overlay)
                        std::vector<int64_t> nmEcKeys;
                        for (auto& [k, cnt] : ec) {
                            if (cnt > 2 && cnt != 4) nmEcKeys.push_back(k);
                        }
                        std::sort(nmEcKeys.begin(), nmEcKeys.end());

                        std::unordered_set<int> nmTriSet;
                        for (int64_t k : nmEcKeys) {
                            int cnt = ec[k];
                            if (cnt <= 2 || cnt == 4) continue;
                            int a = (int)(k / 1000000), b = (int)(k % 1000000);
                            for (int i = 0; i < (int)rT.size(); i++) {
                                int x = rT[i][0], y = rT[i][1], z = rT[i][2];
                                if ((x == a || y == a || z == a) && (x == b || y == b || z == b)) {
                                    nmTriSet.insert(i);
                                }
                            }
                        }
                        if (nmTriSet.empty()) break;
                        std::vector<Tri> newT;
                        for (int i = 0; i < (int)rT.size(); i++) {
                            if (nmTriSet.find(i) == nmTriSet.end()) newT.push_back(rT[i]);
                        }
                        rT = std::move(newT);
                        auto nmvr = resolveNonManifoldVertices(rV, rT);
                        rV = std::move(nmvr.vertices); rT = std::move(nmvr.triangles);
                        auto hr = fillHoles(rV, rT, holeMaxTriArea);
                        rV = std::move(hr.vertices); rT = std::move(hr.triangles);
                        rT = resolveNonManifoldEdges(rV, rT, opts.nmEdgeIterations).triangles;
                        auto nmvr2 = resolveNonManifoldVertices(rV, rT);
                        rV = std::move(nmvr2.vertices); rT = std::move(nmvr2.triangles);
                    }
                    // BFS consistency
                    rT = enforceNormalConsistency(rV, rT, true).triangles;
                    auto finalRetryDiag = diagnose(rV, rT);
                    if (finalRetryDiag.windingInconsistencies <= retryDiag.windingInconsistencies) {
                        retryResult.V = std::move(rV);
                        retryResult.T = std::move(rT);
                    }
                }
                // Volume check on retry result before returning
                {
                    auto retDiag2 = quickDiagnose(retryResult.V, retryResult.T);
                    if (retDiag2.isWatertight) {
                        double vol = 0;
                        for (size_t i = 0; i < retryResult.T.size(); i++) {
                            const auto& v0 = retryResult.V[retryResult.T[i][0]];
                            const auto& v1 = retryResult.V[retryResult.T[i][1]];
                            const auto& v2 = retryResult.V[retryResult.T[i][2]];
                            vol += v0[0] * (v1[1] * v2[2] - v1[2] * v2[1])
                                 + v0[1] * (v1[2] * v2[0] - v1[0] * v2[2])
                                 + v0[2] * (v1[0] * v2[1] - v1[1] * v2[0]);
                        }
                        if (vol < 0) {
                            for (auto& t : retryResult.T) std::swap(t[1], t[2]);
                        }
                    }
                }
                return retryResult;
            }

            // Full retry rejected (triangle loss) — targeted WI fix:
            // Remove triangles on WI edges, re-fill holes, repeat until WI=0 or no improvement
            if (!triCountOk && wiPreCheck.windingInconsistencies > 0 &&
                wiPreCheck.windingInconsistencies <= 500) {
                progress("WI Recovery: Targeted WI edge repair...");
                int prevWI = wiPreCheck.windingInconsistencies;
                for (int wiFixPass = 0; wiFixPass < 5; wiFixPass++) {
                    // Count directed edges to find WI edges
                    std::unordered_map<int64_t, int> dirCounts;
                    for (size_t i = 0; i < triangles.size(); i++) {
                        const auto& t = triangles[i];
                        dirCounts[(int64_t)t[0] * 1000000 + t[1]]++;
                        dirCounts[(int64_t)t[1] * 1000000 + t[2]]++;
                        dirCounts[(int64_t)t[2] * 1000000 + t[0]]++;
                    }

                    std::unordered_set<int64_t> wiUndirEdges;
                    for (const auto& [key, cnt] : dirCounts) {
                        int a = (int)(key / 1000000), b = (int)(key % 1000000);
                        int64_t revKey = (int64_t)b * 1000000 + a;
                        auto revIt = dirCounts.find(revKey);
                        int revCnt = (revIt != dirCounts.end()) ? revIt->second : 0;
                        if (cnt != 1 || revCnt != 1) {
                            wiUndirEdges.insert(undirEdgeKey(a, b));
                        }
                    }

                    if (wiUndirEdges.empty()) break;

                    // Find triangles incident on WI edges
                    std::unordered_set<int> wiTriIndices;
                    for (size_t i = 0; i < triangles.size(); i++) {
                        const auto& t = triangles[i];
                        if (wiUndirEdges.count(undirEdgeKey(t[0], t[1])) ||
                            wiUndirEdges.count(undirEdgeKey(t[1], t[2])) ||
                            wiUndirEdges.count(undirEdgeKey(t[2], t[0]))) {
                            wiTriIndices.insert((int)i);
                        }
                    }

                    if (wiTriIndices.empty()) break;

                    // Save state for rollback
                    std::vector<Vec3> savedV = vertices;
                    std::vector<Tri> savedT = triangles;

                    // Remove WI triangles
                    std::vector<Tri> newT;
                    newT.reserve(triangles.size() - wiTriIndices.size());
                    for (size_t i = 0; i < triangles.size(); i++) {
                        if (wiTriIndices.find((int)i) == wiTriIndices.end()) {
                            newT.push_back(triangles[i]);
                        }
                    }
                    triangles = std::move(newT);

                    // Fill holes created by removal
                    auto hr = fillHoles(vertices, triangles, holeMaxTriArea);
                    vertices = std::move(hr.vertices);
                    triangles = std::move(hr.triangles);

                    // Cleanup: NM resolution, normal consistency, edge flip
                    triangles = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations).triangles;
                    auto nmvr = resolveNonManifoldVertices(vertices, triangles);
                    vertices = std::move(nmvr.vertices);
                    triangles = std::move(nmvr.triangles);

                    triangles = enforceNormalConsistency(vertices, triangles, true).triangles;
                    triangles = fixWindingByEdgeFlip(vertices, triangles,
                        std::max(opts.edgeFlipPasses, prevWI)).triangles;

                    auto postDiag = diagnose(vertices, triangles);
                    if (postDiag.windingInconsistencies >= prevWI ||
                        postDiag.nonManifold > 0 ||
                        (int)triangles.size() < (int)(savedT.size() * 0.8)) {
                        // No improvement or degraded — rollback
                        vertices = std::move(savedV);
                        triangles = std::move(savedT);
                        break;
                    }
                    prevWI = postDiag.windingInconsistencies;
                    if (prevWI == 0) break;
                }

                // Final boundary cleanup: fill any remaining holes from WI triangle removal
                auto postWIFixDiag = diagnose(vertices, triangles);
                if (postWIFixDiag.boundary > 0) {
                    auto hr2 = fillHoles(vertices, triangles, holeMaxTriArea);
                    vertices = std::move(hr2.vertices);
                    triangles = std::move(hr2.triangles);
                    triangles = resolveNonManifoldEdges(vertices, triangles, opts.nmEdgeIterations).triangles;
                    auto nmvr2 = resolveNonManifoldVertices(vertices, triangles);
                    vertices = std::move(nmvr2.vertices);
                    triangles = std::move(nmvr2.triangles);
                    triangles = enforceNormalConsistency(vertices, triangles, true).triangles;
                    triangles = fixWindingByEdgeFlip(vertices, triangles, opts.edgeFlipPasses).triangles;
                }
            }
        }
    }

    // Remove spurious floating shells created during repair (opt-out via removeSpuriousShells=false)
    // Criterion: component has < 2% of total triangles AND < 1% of largest component volume
    if (opts.removeSpuriousShells) {
        auto comps = findConnectedComponents(triangles);
        if ((int)comps.size() > 1) {
            // Compute per-component signed volume
            std::vector<double> compVols(comps.size(), 0);
            for (size_t ci = 0; ci < comps.size(); ci++) {
                double vol = 0;
                for (int ti : comps[ci]) {
                    const auto& v0 = vertices[triangles[ti][0]];
                    const auto& v1 = vertices[triangles[ti][1]];
                    const auto& v2 = vertices[triangles[ti][2]];
                    vol += v0[0] * (v1[1] * v2[2] - v1[2] * v2[1])
                         + v0[1] * (v1[2] * v2[0] - v1[0] * v2[2])
                         + v0[2] * (v1[0] * v2[1] - v1[1] * v2[0]);
                }
                compVols[ci] = std::abs(vol / 6.0);
            }
            // Find largest component volume
            double maxVol = 0;
            for (double v : compVols) {
                if (v > maxVol) maxVol = v;
            }
            // Mark spurious shells for removal
            int totalT = (int)triangles.size();
            int threshold = std::max(10, (int)(totalT * 0.02));
            std::unordered_set<int> removeSet;
            for (size_t ci = 0; ci < comps.size(); ci++) {
                if ((int)comps[ci].size() < threshold && compVols[ci] < maxVol * 0.01) {
                    for (int ti : comps[ci]) removeSet.insert(ti);
                }
            }
            if (!removeSet.empty()) {
                std::vector<Tri> newT;
                newT.reserve(totalT - removeSet.size());
                for (int i = 0; i < totalT; i++) {
                    if (!removeSet.count(i)) newT.push_back(triangles[i]);
                }
                triangles = std::move(newT);
            }
        }
    }

    // Subdivide oversized triangles
    progress("Subdividing oversized triangles...");
    {
        auto subdivResult = subdivideOversizedTriangles(vertices, triangles, holeMaxTriArea / 5.0);
        vertices = std::move(subdivResult.vertices);
        triangles = std::move(subdivResult.triangles);
    }

    // Final cleanup: remove OWP safely (only if it doesn't break watertightness)
    {
        auto preOwpDiag = quickDiagnose(vertices, triangles);
        auto owpResult = removeOppositeWindingDuplicates(triangles);
        if (owpResult.pairsRemoved > 0) {
            auto postOwpDiag = quickDiagnose(vertices, owpResult.triangles);
            if (postOwpDiag.boundary == 0 || postOwpDiag.boundary <= preOwpDiag.boundary) {
                // Safe: didn't create new boundaries
                triangles = std::move(owpResult.triangles);
                report.oppositeWindingRemoved += owpResult.pairsRemoved;
            } else {
                // OWP removal created boundaries — try to repair
                std::vector<Tri> owpT = std::move(owpResult.triangles);
                auto owpHr = fillHoles(vertices, owpT, holeMaxTriArea);
                vertices = std::move(owpHr.vertices);
                owpT = std::move(owpHr.triangles);
                auto owpFinalDiag = quickDiagnose(vertices, owpT);
                if (owpFinalDiag.boundary <= preOwpDiag.boundary &&
                    owpFinalDiag.nonManifold <= preOwpDiag.nonManifold) {
                    triangles = std::move(owpT);
                    report.oppositeWindingRemoved += owpResult.pairsRemoved;
                    report.holesFilled += owpHr.filled;
                }
                // else: revert — keep original triangles
            }
        }
    }

    // Remove unused vertices
    {
        auto cleanResult = removeUnusedVertices(vertices, triangles);
        vertices = std::move(cleanResult.vertices);
        triangles = std::move(cleanResult.triangles);
    }

    return {std::move(vertices), std::move(triangles), report};
}

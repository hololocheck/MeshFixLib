#include "mesh_fix.h"
#include <stack>
#include <limits>

// ============================================================
// Helper: merge bounding boxes for a set of triangle indices
// ============================================================

static std::array<double, 6> mergeBoxes(
    const std::vector<std::array<double, 6>>& triBoxes,
    const std::vector<int>& indices)
{
    double x0 = 1e30, y0 = 1e30, z0 = 1e30;
    double x1 = -1e30, y1 = -1e30, z1 = -1e30;
    for (int i : indices) {
        const auto& b = triBoxes[i];
        if (b[0] < x0) x0 = b[0];
        if (b[1] < y0) y0 = b[1];
        if (b[2] < z0) z0 = b[2];
        if (b[3] > x1) x1 = b[3];
        if (b[4] > y1) y1 = b[4];
        if (b[5] > z1) z1 = b[5];
    }
    return { x0, y0, z0, x1, y1, z1 };
}

// ============================================================
// Helper: recursive BVH build
// ============================================================

static BVHNode* buildBVHRec(
    const std::vector<std::array<double, 6>>& triBoxes,
    std::vector<int>& indices)
{
    BVHNode* node = new BVHNode();

    if ((int)indices.size() <= 4) {
        // Leaf node
        node->box = mergeBoxes(triBoxes, indices);
        node->indices = indices;
        return node;
    }

    // Compute bounding box of all indices
    node->box = mergeBoxes(triBoxes, indices);

    // Split along longest axis
    double dx = node->box[3] - node->box[0];
    double dy = node->box[4] - node->box[1];
    double dz = node->box[5] - node->box[2];
    int axis = (dx >= dy && dx >= dz) ? 0 : (dy >= dz ? 1 : 2);

    // Sort by centroid along axis
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        double ca = (triBoxes[a][axis] + triBoxes[a][axis + 3]) * 0.5;
        double cb = (triBoxes[b][axis] + triBoxes[b][axis + 3]) * 0.5;
        return ca < cb;
    });

    int mid = (int)indices.size() >> 1;
    std::vector<int> leftIndices(indices.begin(), indices.begin() + mid);
    std::vector<int> rightIndices(indices.begin() + mid, indices.end());

    node->left = buildBVHRec(triBoxes, leftIndices);
    node->right = buildBVHRec(triBoxes, rightIndices);

    return node;
}

// ============================================================
// buildBVH
// ============================================================

BVH* MeshFix::buildBVH(const std::vector<Vec3>& V, const std::vector<Tri>& T) const {
    BVH* bvh = new BVH();
    int n = (int)T.size();
    bvh->triBoxes.resize(n);

    for (int i = 0; i < n; i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        double ax = V[a][0], ay = V[a][1], az = V[a][2];
        double bx = V[b][0], by = V[b][1], bz = V[b][2];
        double cx = V[c][0], cy = V[c][1], cz = V[c][2];
        bvh->triBoxes[i] = {
            std::min({ax, bx, cx}), std::min({ay, by, cy}), std::min({az, bz, cz}),
            std::max({ax, bx, cx}), std::max({ay, by, cy}), std::max({az, bz, cz})
        };
    }

    std::vector<int> allIndices(n);
    for (int i = 0; i < n; i++) allIndices[i] = i;

    bvh->root = buildBVHRec(bvh->triBoxes, allIndices);
    return bvh;
}

// ============================================================
// bvhQuery
// ============================================================

std::vector<int> MeshFix::bvhQuery(const BVHNode* node, const std::array<double, 6>& queryBox) const {
    std::vector<int> results;
    if (!node) return results;

    std::stack<const BVHNode*> stk;
    stk.push(node);

    while (!stk.empty()) {
        const BVHNode* n = stk.top();
        stk.pop();

        // AABB overlap test
        if (n->box[0] > queryBox[3] || n->box[3] < queryBox[0] ||
            n->box[1] > queryBox[4] || n->box[4] < queryBox[1] ||
            n->box[2] > queryBox[5] || n->box[5] < queryBox[2]) {
            continue;
        }

        if (!n->indices.empty()) {
            // Leaf node
            for (int i : n->indices) {
                results.push_back(i);
            }
        } else {
            if (n->left) stk.push(n->left);
            if (n->right) stk.push(n->right);
        }
    }

    return results;
}

// ============================================================
// Helper: computeInterval for triangle-triangle intersection
// ============================================================

static bool computeInterval(double p0, double p1, double p2,
                             double d0, double d1, double d2,
                             double& lo, double& hi)
{
    const double eps = 1e-12;
    double vals[6];
    int count = 0;

    // Interpolate to find crossing points
    if (std::abs(d0 - d1) > eps && d0 * d1 < 0) {
        vals[count++] = p0 + (p1 - p0) * d0 / (d0 - d1);
    }
    if (std::abs(d1 - d2) > eps && d1 * d2 < 0) {
        vals[count++] = p1 + (p2 - p1) * d1 / (d1 - d2);
    }
    if (std::abs(d2 - d0) > eps && d2 * d0 < 0) {
        vals[count++] = p2 + (p0 - p2) * d2 / (d2 - d0);
    }
    // Include vertices on the plane
    if (std::abs(d0) <= eps) vals[count++] = p0;
    if (std::abs(d1) <= eps) vals[count++] = p1;
    if (std::abs(d2) <= eps) vals[count++] = p2;

    if (count < 2) return false;

    lo = vals[0];
    hi = vals[0];
    for (int i = 1; i < count; i++) {
        if (vals[i] < lo) lo = vals[i];
        if (vals[i] > hi) hi = vals[i];
    }
    return true;
}

// ============================================================
// trianglesIntersect  (Moller's triangle-triangle test)
// ============================================================

bool MeshFix::trianglesIntersect(const std::vector<Vec3>& V, const std::vector<Tri>& T,
                                  int t1, int t2) const
{
    const Tri& tri1 = T[t1];
    const Tri& tri2 = T[t2];

    // Skip if sharing any vertex (adjacent triangles)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (tri1[i] == tri2[j]) return false;
        }
    }

    const Vec3& p0 = V[tri1[0]];
    const Vec3& p1 = V[tri1[1]];
    const Vec3& p2 = V[tri1[2]];
    const Vec3& q0 = V[tri2[0]];
    const Vec3& q1 = V[tri2[1]];
    const Vec3& q2 = V[tri2[2]];

    // Compute plane of triangle 2
    double e1x = q1[0] - q0[0], e1y = q1[1] - q0[1], e1z = q1[2] - q0[2];
    double e2x = q2[0] - q0[0], e2y = q2[1] - q0[1], e2z = q2[2] - q0[2];
    double n2x = e1y * e2z - e1z * e2y;
    double n2y = e1z * e2x - e1x * e2z;
    double n2z = e1x * e2y - e1y * e2x;
    double d2 = -(n2x * q0[0] + n2y * q0[1] + n2z * q0[2]);

    // Signed distances of tri1 vertices to plane of tri2
    double dp0 = n2x * p0[0] + n2y * p0[1] + n2z * p0[2] + d2;
    double dp1 = n2x * p1[0] + n2y * p1[1] + n2z * p1[2] + d2;
    double dp2 = n2x * p2[0] + n2y * p2[1] + n2z * p2[2] + d2;

    // All on same side -> no intersection
    if (dp0 > 0 && dp1 > 0 && dp2 > 0) return false;
    if (dp0 < 0 && dp1 < 0 && dp2 < 0) return false;

    // Compute plane of triangle 1
    double f1x = p1[0] - p0[0], f1y = p1[1] - p0[1], f1z = p1[2] - p0[2];
    double f2x = p2[0] - p0[0], f2y = p2[1] - p0[1], f2z = p2[2] - p0[2];
    double n1x = f1y * f2z - f1z * f2y;
    double n1y = f1z * f2x - f1x * f2z;
    double n1z = f1x * f2y - f1y * f2x;
    double d1 = -(n1x * p0[0] + n1y * p0[1] + n1z * p0[2]);

    // Signed distances of tri2 vertices to plane of tri1
    double dq0 = n1x * q0[0] + n1y * q0[1] + n1z * q0[2] + d1;
    double dq1 = n1x * q1[0] + n1y * q1[1] + n1z * q1[2] + d1;
    double dq2 = n1x * q2[0] + n1y * q2[1] + n1z * q2[2] + d1;

    if (dq0 > 0 && dq1 > 0 && dq2 > 0) return false;
    if (dq0 < 0 && dq1 < 0 && dq2 < 0) return false;

    // Intersection line direction
    double lx = n1y * n2z - n1z * n2y;
    double ly = n1z * n2x - n1x * n2z;
    double lz = n1x * n2y - n1y * n2x;

    // Project vertices onto intersection line
    double projP0 = lx * p0[0] + ly * p0[1] + lz * p0[2];
    double projP1 = lx * p1[0] + ly * p1[1] + lz * p1[2];
    double projP2 = lx * p2[0] + ly * p2[1] + lz * p2[2];
    double projQ0 = lx * q0[0] + ly * q0[1] + lz * q0[2];
    double projQ1 = lx * q1[0] + ly * q1[1] + lz * q1[2];
    double projQ2 = lx * q2[0] + ly * q2[1] + lz * q2[2];

    // Compute intervals on the intersection line
    double lo1, hi1, lo2, hi2;
    bool ok1 = computeInterval(projP0, projP1, projP2, dp0, dp1, dp2, lo1, hi1);
    bool ok2 = computeInterval(projQ0, projQ1, projQ2, dq0, dq1, dq2, lo2, hi2);

    if (!ok1 || !ok2) return false;

    // Check overlap
    return lo1 < hi2 && lo2 < hi1;
}

// ============================================================
// pointToTriangleDistance  (barycentric coordinate method)
// ============================================================

NearestResult MeshFix::pointToTriangleDistance(const Vec3& p, const std::vector<Vec3>& V,
                                                int a, int b, int c) const
{
    const Vec3& v0 = V[a];
    const Vec3& v1 = V[b];
    const Vec3& v2 = V[c];

    double e0x = v1[0] - v0[0], e0y = v1[1] - v0[1], e0z = v1[2] - v0[2];
    double e1x = v2[0] - v0[0], e1y = v2[1] - v0[1], e1z = v2[2] - v0[2];
    double vx = v0[0] - p[0], vy = v0[1] - p[1], vz = v0[2] - p[2];

    double a00 = e0x * e0x + e0y * e0y + e0z * e0z;
    double a01 = e0x * e1x + e0y * e1y + e0z * e1z;
    double a11 = e1x * e1x + e1y * e1y + e1z * e1z;
    double b0 = e0x * vx + e0y * vy + e0z * vz;
    double b1 = e1x * vx + e1y * vy + e1z * vz;

    double det = a00 * a11 - a01 * a01;
    double s = a01 * b1 - a11 * b0;
    double t = a01 * b0 - a00 * b1;

    if (s + t <= det) {
        if (s < 0) {
            if (t < 0) {
                // region 4
                if (b0 < 0) {
                    t = 0;
                    s = (-b0 >= a00) ? 1 : -b0 / a00;
                } else {
                    s = 0;
                    t = (b1 >= 0) ? 0 : ((-b1 >= a11) ? 1 : -b1 / a11);
                }
            } else {
                // region 3
                s = 0;
                t = (b1 >= 0) ? 0 : ((-b1 >= a11) ? 1 : -b1 / a11);
            }
        } else if (t < 0) {
            // region 5
            t = 0;
            s = (b0 >= 0) ? 0 : ((-b0 >= a00) ? 1 : -b0 / a00);
        } else {
            // region 0 (interior)
            double invDet = (det > 1e-30) ? 1.0 / det : 0.0;
            s *= invDet;
            t *= invDet;
        }
    } else {
        if (s < 0) {
            // region 2
            double tmp0 = a01 + b0, tmp1 = a11 + b1;
            if (tmp1 > tmp0) {
                double numer = tmp1 - tmp0;
                double denom = a00 - 2.0 * a01 + a11;
                s = (numer >= denom) ? 1.0 : numer / denom;
                t = 1.0 - s;
            } else {
                s = 0;
                t = (tmp1 <= 0) ? 1.0 : ((b1 >= 0) ? 0.0 : -b1 / a11);
            }
        } else if (t < 0) {
            // region 6
            double tmp0 = a01 + b1, tmp1 = a00 + b0;
            if (tmp1 > tmp0) {
                double numer = tmp1 - tmp0;
                double denom = a00 - 2.0 * a01 + a11;
                t = (numer >= denom) ? 1.0 : numer / denom;
                s = 1.0 - t;
            } else {
                t = 0;
                s = (tmp1 <= 0) ? 1.0 : ((b0 >= 0) ? 0.0 : -b0 / a00);
            }
        } else {
            // region 1
            double numer = (a11 + b1) - (a01 + b0);
            if (numer <= 0) {
                s = 0;
                t = 1;
            } else {
                double denom = a00 - 2.0 * a01 + a11;
                s = (numer >= denom) ? 1.0 : numer / denom;
                t = 1.0 - s;
            }
        }
    }

    double cx = v0[0] + s * e0x + t * e1x;
    double cy = v0[1] + s * e0y + t * e1y;
    double cz = v0[2] + s * e0z + t * e1z;
    double dx = p[0] - cx, dy = p[1] - cy, dz = p[2] - cz;

    NearestResult result;
    result.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    result.triIdx = -1; // caller sets this
    result.closest = { cx, cy, cz };
    return result;
}

// ============================================================
// rayTriangleIntersect  (Moller-Trumbore)
// ============================================================

MeshFix::RayHit MeshFix::rayTriangleIntersect(const Vec3& origin, const Vec3& dir,
                                                const std::vector<Vec3>& V,
                                                int a, int b, int c) const
{
    const Vec3& v0 = V[a];
    const Vec3& v1 = V[b];
    const Vec3& v2 = V[c];

    double e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
    double e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];

    double hx = dir[1] * e2z - dir[2] * e2y;
    double hy = dir[2] * e2x - dir[0] * e2z;
    double hz = dir[0] * e2y - dir[1] * e2x;

    double det = e1x * hx + e1y * hy + e1z * hz;
    if (det > -1e-12 && det < 1e-12) return { 0.0, false };

    double invDet = 1.0 / det;
    double sx = origin[0] - v0[0], sy = origin[1] - v0[1], sz = origin[2] - v0[2];
    double u = invDet * (sx * hx + sy * hy + sz * hz);
    if (u < -1e-8 || u > 1.0 + 1e-8) return { 0.0, false };

    double qx = sy * e1z - sz * e1y;
    double qy = sz * e1x - sx * e1z;
    double qz = sx * e1y - sy * e1x;
    double v = invDet * (dir[0] * qx + dir[1] * qy + dir[2] * qz);
    if (v < -1e-8 || u + v > 1.0 + 1e-8) return { 0.0, false };

    double t = invDet * (e2x * qx + e2y * qy + e2z * qz);
    return { t, true };
}

// ============================================================
// findNearestTriangle
// ============================================================

NearestResult MeshFix::findNearestTriangle(const Vec3& point, const std::vector<Vec3>& V,
                                            const std::vector<Tri>& T, const BVH* bvh) const
{
    double bestDist = 1e30;
    int bestTri = -1;
    Vec3 bestClosest = { 0, 0, 0 };

    // Expanding radius search
    double radius = (bvh && bvh->root) ? (bvh->root->box[3] - bvh->root->box[0]) * 0.01 : 1.0;

    for (int attempt = 0; attempt < 20; attempt++) {
        std::array<double, 6> qbox = {
            point[0] - radius, point[1] - radius, point[2] - radius,
            point[0] + radius, point[1] + radius, point[2] + radius
        };
        std::vector<int> candidates = bvhQuery(bvh->root, qbox);

        for (int ci = 0; ci < (int)candidates.size(); ci++) {
            int ti = candidates[ci];
            NearestResult d = pointToTriangleDistance(point, V, T[ti][0], T[ti][1], T[ti][2]);
            if (d.distance < bestDist) {
                bestDist = d.distance;
                bestTri = ti;
                bestClosest = d.closest;
            }
        }
        if (bestTri >= 0) break;
        radius *= 4.0;
    }

    // Fallback: brute-force search if BVH search found nothing
    if (bestTri < 0) {
        for (int i = 0; i < (int)T.size(); i++) {
            NearestResult d = pointToTriangleDistance(point, V, T[i][0], T[i][1], T[i][2]);
            if (d.distance < bestDist) {
                bestDist = d.distance;
                bestTri = i;
                bestClosest = d.closest;
            }
        }
    }

    NearestResult result;
    result.triIdx = bestTri;
    result.distance = bestDist;
    result.closest = bestClosest;
    return result;
}

// ============================================================
// detectSelfIntersections
// ============================================================

std::vector<std::pair<int, int>> MeshFix::detectSelfIntersections(
    const std::vector<Vec3>& V, const std::vector<Tri>& T) const
{
    BVH* bvh = buildBVH(V, T);
    std::vector<std::pair<int, int>> pairs;
    std::unordered_set<int64_t> seen;

    for (int i = 0; i < (int)T.size(); i++) {
        std::vector<int> candidates = bvhQuery(bvh->root, bvh->triBoxes[i]);
        for (int ci = 0; ci < (int)candidates.size(); ci++) {
            int j = candidates[ci];
            if (j <= i) continue;
            // Use undirEdgeKey as a pair key (works since i < j)
            int64_t key = (int64_t)i * 1000000 + j;
            if (seen.count(key)) continue;
            seen.insert(key);
            if (trianglesIntersect(V, T, i, j)) {
                pairs.push_back({ i, j });
            }
        }
    }

    delete bvh;
    return pairs;
}

// ============================================================
// repairSelfIntersections
// ============================================================

MeshFix::SIResult MeshFix::repairSelfIntersections(
    std::vector<Vec3> V, std::vector<Tri> T, double holeMaxTriArea) const
{
    std::vector<Vec3> vertices = std::move(V);
    std::vector<Tri> triangles = std::move(T);
    int totalRepaired = 0;

    for (int iter = 0; iter < 100; iter++) {
        std::vector<std::pair<int, int>> pairs = detectSelfIntersections(vertices, triangles);
        if (pairs.empty()) break;

        // Collect intersecting triangles + 1-ring neighbors
        std::unordered_set<int> toRemove;
        for (const auto& pr : pairs) {
            toRemove.insert(pr.first);
            toRemove.insert(pr.second);
        }

        // Build 1-ring: triangles sharing a vertex with intersecting triangles
        std::unordered_set<int> intersectingVerts;
        for (int ti : toRemove) {
            int a = triangles[ti][0], b = triangles[ti][1], c = triangles[ti][2];
            intersectingVerts.insert(a);
            intersectingVerts.insert(b);
            intersectingVerts.insert(c);
        }
        for (int i = 0; i < (int)triangles.size(); i++) {
            if (toRemove.count(i)) continue;
            int a = triangles[i][0], b = triangles[i][1], c = triangles[i][2];
            if (intersectingVerts.count(a) || intersectingVerts.count(b) || intersectingVerts.count(c)) {
                toRemove.insert(i);
            }
        }

        // Remove intersecting triangles
        int prevCount = (int)triangles.size();
        std::vector<Tri> newTris;
        newTris.reserve(prevCount);
        for (int i = 0; i < prevCount; i++) {
            if (!toRemove.count(i)) {
                newTris.push_back(triangles[i]);
            }
        }
        triangles = std::move(newTris);
        int removed = prevCount - (int)triangles.size();
        if (removed == 0) break;

        // Fill holes created by removal
        FillResult fillResult = fillHoles(vertices, triangles, holeMaxTriArea);
        vertices = std::move(fillResult.vertices);
        triangles = std::move(fillResult.triangles);
        totalRepaired += (int)pairs.size();

        // Safety: if we didn't improve, stop
        std::vector<std::pair<int, int>> newPairs = detectSelfIntersections(vertices, triangles);
        if ((int)newPairs.size() >= (int)pairs.size()) break;
    }

    SIResult result;
    result.vertices = std::move(vertices);
    result.triangles = std::move(triangles);
    result.repaired = totalRepaired;
    return result;
}

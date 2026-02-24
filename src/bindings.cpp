#include "mesh_fix.h"
#include <emscripten/bind.h>
#include <emscripten/val.h>

using namespace emscripten;

// Convert JS array of [x,y,z] arrays to C++ vector<Vec3>
static std::vector<Vec3> jsToVertices(const val& jsV) {
    int len = jsV["length"].as<int>();
    std::vector<Vec3> V(len);
    for (int i = 0; i < len; i++) {
        val v = jsV[i];
        V[i] = {v[0].as<double>(), v[1].as<double>(), v[2].as<double>()};
    }
    return V;
}

// Convert JS array of [a,b,c] arrays to C++ vector<Tri>
static std::vector<Tri> jsToTriangles(const val& jsT) {
    int len = jsT["length"].as<int>();
    std::vector<Tri> T(len);
    for (int i = 0; i < len; i++) {
        val t = jsT[i];
        T[i] = {t[0].as<int>(), t[1].as<int>(), t[2].as<int>()};
    }
    return T;
}

// Convert C++ vector<Vec3> to JS array
static val verticesToJs(const std::vector<Vec3>& V) {
    val result = val::array();
    for (size_t i = 0; i < V.size(); i++) {
        val v = val::array();
        v.call<void>("push", V[i][0]);
        v.call<void>("push", V[i][1]);
        v.call<void>("push", V[i][2]);
        result.call<void>("push", v);
    }
    return result;
}

// Convert C++ vector<Tri> to JS array
static val trianglesToJs(const std::vector<Tri>& T) {
    val result = val::array();
    for (size_t i = 0; i < T.size(); i++) {
        val t = val::array();
        t.call<void>("push", T[i][0]);
        t.call<void>("push", T[i][1]);
        t.call<void>("push", T[i][2]);
        result.call<void>("push", t);
    }
    return result;
}

// ===== Diagnose wrapper =====
static val jsDiagnose(const val& jsV, const val& jsT) {
    MeshFix fixer;
    auto V = jsToVertices(jsV);
    auto T = jsToTriangles(jsT);
    auto result = fixer.diagnose(V, T);

    val obj = val::object();
    obj.set("v", result.vertices);
    obj.set("t", result.triangles);
    obj.set("boundary", result.boundary);
    obj.set("nonManifold", result.nonManifold);
    obj.set("windingInconsistencies", result.windingInconsistencies);
    obj.set("oppositeWindingPairs", result.oppositeWindingPairs);
    obj.set("components", result.components);
    obj.set("isWatertight", result.isWatertight);
    return obj;
}

// ===== RepairObject wrapper =====
static val jsRepairObject(const val& jsV, const val& jsT, val jsProgress, val jsOpts) {
    MeshFix fixer;
    auto V = jsToVertices(jsV);
    auto T = jsToTriangles(jsT);

    // Build RepairOptions from JS object
    RepairOptions opts;
    if (!jsOpts.isUndefined() && !jsOpts.isNull()) {
        if (jsOpts["mergePrecision"].typeOf().as<std::string>() == "number")
            opts.mergePrecision = jsOpts["mergePrecision"].as<int>();
        if (jsOpts["degenerateThreshold"].typeOf().as<std::string>() == "number")
            opts.degenerateThreshold = jsOpts["degenerateThreshold"].as<double>();
        if (jsOpts["holeAreaMultiplier"].typeOf().as<std::string>() == "number")
            opts.holeAreaMultiplier = jsOpts["holeAreaMultiplier"].as<double>();
        if (jsOpts["localRemeshLimit"].typeOf().as<std::string>() == "number")
            opts.localRemeshLimit = jsOpts["localRemeshLimit"].as<double>();
        if (jsOpts["edgeFlipPasses"].typeOf().as<std::string>() == "number")
            opts.edgeFlipPasses = jsOpts["edgeFlipPasses"].as<int>();
        if (jsOpts["nmEdgeIterations"].typeOf().as<std::string>() == "number")
            opts.nmEdgeIterations = jsOpts["nmEdgeIterations"].as<int>();
        if (jsOpts["oversizeMultiplier"].typeOf().as<std::string>() == "number")
            opts.oversizeMultiplier = jsOpts["oversizeMultiplier"].as<int>();
        if (jsOpts["removeSmallShells"].typeOf().as<std::string>() == "boolean")
            opts.removeSmallShells = jsOpts["removeSmallShells"].as<bool>();
        if (jsOpts["smallShellThreshold"].typeOf().as<std::string>() == "number")
            opts.smallShellThreshold = jsOpts["smallShellThreshold"].as<double>();
        if (jsOpts["removeSpuriousShells"].typeOf().as<std::string>() == "boolean")
            opts.removeSpuriousShells = jsOpts["removeSpuriousShells"].as<bool>();
        if (jsOpts["repairSelfIntersections"].typeOf().as<std::string>() == "boolean")
            opts.repairSelfIntersections = jsOpts["repairSelfIntersections"].as<bool>();

        // deepRepair: false=0, 'auto'=1, 'always'=2, true=1
        val dr = jsOpts["deepRepair"];
        if (!dr.isUndefined() && !dr.isNull()) {
            std::string drType = dr.typeOf().as<std::string>();
            if (drType == "boolean") {
                opts.deepRepair = dr.as<bool>() ? 1 : 0;
            } else if (drType == "string") {
                std::string drStr = dr.as<std::string>();
                if (drStr == "auto") opts.deepRepair = 1;
                else if (drStr == "always") opts.deepRepair = 2;
            } else if (drType == "number") {
                opts.deepRepair = dr.as<int>();
            }
        }

        if (jsOpts["voxelResolution"].typeOf().as<std::string>() == "number")
            opts.voxelResolution = jsOpts["voxelResolution"].as<int>();
        if (jsOpts["voxelSmoothing"].typeOf().as<std::string>() == "boolean")
            opts.voxelSmoothing = jsOpts["voxelSmoothing"].as<bool>();
        if (jsOpts["voxelSmoothIterations"].typeOf().as<std::string>() == "number")
            opts.voxelSmoothIterations = jsOpts["voxelSmoothIterations"].as<int>();
        if (jsOpts["voxelSmoothLambda"].typeOf().as<std::string>() == "number")
            opts.voxelSmoothLambda = jsOpts["voxelSmoothLambda"].as<double>();
        if (jsOpts["voxelProjectToSurface"].typeOf().as<std::string>() == "boolean")
            opts.voxelProjectToSurface = jsOpts["voxelProjectToSurface"].as<bool>();
        if (jsOpts["voxelProjectMaxDist"].typeOf().as<std::string>() == "number")
            opts.voxelProjectMaxDist = jsOpts["voxelProjectMaxDist"].as<double>();
        if (jsOpts["_wiRecoveryRetry"].typeOf().as<std::string>() == "boolean")
            opts._wiRecoveryRetry = jsOpts["_wiRecoveryRetry"].as<bool>();
    }

    // Progress callback: call JS function
    ProgressCallback progressCb = [&](const char* msg) {
        if (!jsProgress.isUndefined() && !jsProgress.isNull()) {
            jsProgress(val(std::string(msg)));
        }
    };

    auto result = fixer.repairObject(std::move(V), std::move(T), progressCb, opts);

    // Build result object
    val obj = val::object();
    obj.set("V", verticesToJs(result.V));
    obj.set("T", trianglesToJs(result.T));

    val rep = val::object();
    rep.set("merged", result.report.merged);
    rep.set("degenerateRemoved", result.report.degenerateRemoved);
    rep.set("oppositeWindingRemoved", result.report.oppositeWindingRemoved);
    rep.set("exactDuplicatesRemoved", result.report.exactDuplicatesRemoved);
    rep.set("normalsFlipped", result.report.normalsFlipped);
    rep.set("nmFixed", result.report.nmFixed);
    rep.set("nmVerticesSplit", result.report.nmVerticesSplit);
    rep.set("holesFilled", result.report.holesFilled);
    rep.set("smallShellsRemoved", result.report.smallShellsRemoved);
    rep.set("selfIntersectionsRepaired", result.report.selfIntersectionsRepaired);
    rep.set("deepRepairApplied", result.report.deepRepairApplied);
    rep.set("deepRepairResolution", result.report.deepRepairResolution);
    obj.set("report", rep);

    return obj;
}

// ===== Embind module =====
EMSCRIPTEN_BINDINGS(mesh_fix_core) {
    function("diagnose", &jsDiagnose);
    function("repairObject", &jsRepairObject);
}

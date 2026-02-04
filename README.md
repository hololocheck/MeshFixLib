# MeshFixLib
Non-manifold Edge Repair Library

#  MeshFixLib v1.0

**Version:** 1.0
**Date:** February 4, 2026
**Type:** Major Update

## 🚀 Overview
MeshFixLib v1.0 is a comprehensive update designed to ensure 3D mesh integrity for procedural generation tools. This release introduces a robust repair pipeline capable of making non-watertight meshes printable by automatically resolving holes, non-manifold edges, and degenerate geometry within `.3mf` archives.

## ✨ Key Features & Improvements

### 🛠 Automated Mesh Repair Pipeline
The core `repairObject` method now performs a multi-stage cleaning process:
1.  **Vertex Merging:** Consolidates duplicate vertices based on spatial proximity to stitch disconnected faces.
2.  **Degenerate Removal:** Filters out zero-area triangles and duplicate faces that cause slicing errors.
3.  **Non-Manifold Correction:** Iteratively detects and removes edges shared by more than two faces.
4.  **Advanced Hole Filling:**
    * Detects boundary edge loops recursively.
    * Fills holes using a centroid-fan triangulation strategy.
    * Includes a fallback for resolving T-Junctions.

### 📂 Enhanced 3MF Support
* **Parsing:** Seamless extraction of mesh data from `.3mf` files (requires `JSZip`).
* **Editing:** The `write3MF` method updates geometry within an existing 3MF archive while preserving original metadata.
* **Creation:** The new `create3MF` method allows generating valid 3MF files from scratch using raw vertex/triangle data.

### 📊 Diagnostics
* **`diagnose(V, T)`:** Returns instant health metrics for any mesh, including:
    * Boundary count (open edges).
    * Non-manifold edge count.
    * Watertight status boolean.
* **Progress Monitoring:** Both `repairAll` and `repairObject` now support real-time callbacks to track the repair status of heavy meshes.

---

## 💻 Usage Examples

### 1. Standard Workflow: Repairing a 3MF File
This example demonstrates loading a file, repairing it, and saving the result.

```javascript
const meshFix = new MeshFixLib();

// A. Parse the Input (ArrayBuffer)
// 'buffer' usually comes from a File input or fetch request
const parsed = await meshFix.parse3MF(buffer);

// B. Execute Repair
// The callback is optional but recommended for UI feedback
const repaired = await meshFix.repairAll(parsed.objects, (progress) => {
    if (progress.type === 'progress') {
        console.log(`[Object ${progress.objectId}] ${progress.status}`);
    }
});

// Check the report
console.log(`Holes Filled: ${repaired.totalReport.holesFilled}`);

// C. Export
// Updates the original zip structure with the new geometry
const finalBlob = await meshFix.write3MF(
    repaired.objects, 
    parsed.originalXml, 
    parsed.zip, 
    parsed.modelPath
);

// D. Download (Browser example)
const url = URL.createObjectURL(finalBlob);
const a = document.createElement('a');
a.href = url;
a.download = "repaired_model.3mf";
a.click();

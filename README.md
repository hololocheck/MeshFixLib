# MeshFixLib

**High-performance WebAssembly mesh repair library for 3D printing**

MeshFixLib v3.2 — C++ WASM engine with automatic 8-stage repair pipeline

[日本語版は下にあります](#meshfixlib-日本語)

---

## Overview

MeshFixLib is a standalone mesh repair library that fixes non-manifold, non-watertight 3D meshes for 3D printing. It runs entirely in the browser via WebAssembly (compiled from C++17), with no server required.

**Input:** Broken mesh (holes, non-manifold edges, winding inconsistencies, self-intersections)
**Output:** Clean, watertight, manifold mesh ready for slicing

### Key Capabilities

- 8-stage automated repair pipeline (vertex merge → hole fill → final cleanup)
- Non-manifold edge/vertex resolution
- Winding inconsistency repair (edge flip + local remesh + targeted WI fix)
- Self-intersection detection and repair
- Deep Repair mode (SDF + Marching Cubes voxel reconstruction)
- 3MF file parsing and writing (via JSZip)
- Real-time progress callbacks
- Web Worker support for non-blocking UI

---

## Architecture

```
MeshFixLib (JavaScript class)
  └─ mesh-fix-core.wasm (C++17, compiled via Emscripten)
      ├─ diagnose()      — Mesh quality analysis
      └─ repairObject()  — Full repair pipeline
```

| File | Size | Description |
|------|------|-------------|
| `mesh-fix-lib.js` | ~15 KB | MeshFixLib class (WASM wrapper + 3MF I/O) |
| `build/mesh-fix-core.js` | ~32 KB | Emscripten WASM loader |
| `build/mesh-fix-core.wasm` | ~345 KB | Compiled C++ repair engine |

**Dependencies:** JSZip (for 3MF file handling only)

---

## Installation

```bash
npm install jszip
```

Copy `mesh-fix-lib.js`, `build/mesh-fix-core.js`, and `build/mesh-fix-core.wasm` to your project.

---

## Quick Start

### Browser (ES Module)

```javascript
import MeshFixLib from './mesh-fix-lib.js';

const meshFix = new MeshFixLib();
await meshFix.init();

// Diagnose a mesh
const diagnosis = meshFix.diagnose(vertices, triangles);
console.log(`Boundary: ${diagnosis.boundary}, NM: ${diagnosis.nonManifold}, WI: ${diagnosis.windingInconsistencies}`);

// Repair a mesh
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);  // "Stage 0/8: Merging vertices..."
});

console.log(`Repaired: ${result.V.length} vertices, ${result.T.length} triangles`);
```

### Web Worker

```javascript
// mesh-repair-worker.js
importScripts('mesh-fix-core.js');
importScripts('mesh-fix-lib.js');

const meshFix = new MeshFixLib();
await meshFix.init();

self.onmessage = async (e) => {
  const { vertices, triangles } = e.data;
  const result = await meshFix.repairMesh(vertices, triangles, (status) => {
    self.postMessage({ type: 'progress', status });
  });
  self.postMessage({ type: 'result', ...result });
};
```

### Node.js

```javascript
const MeshFixLib = require('./mesh-fix-lib.js');

const meshFix = new MeshFixLib();
await meshFix.init();

const result = await meshFix.repairObject(vertices, triangles);
// result.V = repaired vertices, result.T = repaired triangles
```

### 3MF File Workflow

```javascript
const meshFix = new MeshFixLib();
await meshFix.init();

// 1. Parse
const parsed = await meshFix.parse3MF(arrayBuffer);

// 2. Repair all objects
const repaired = await meshFix.repairAll(parsed.objects, (event) => {
  if (event.type === 'progress') {
    console.log(`[Object ${event.objectId}] ${event.status}`);
  }
});

// 3. Export
const blob = await meshFix.write3MF(
  repaired.objects, parsed.originalXml, parsed.zip, parsed.modelPath
);
```

---

## API Reference

### `new MeshFixLib()`

Creates a new instance.

### `async init(wasmDir?: string): void`

Loads the WASM module. Must be called before any other method.

| Parameter | Type | Description |
|-----------|------|-------------|
| `wasmDir` | string (optional) | Directory path to `.wasm` file (trailing `/` required) |

### `diagnose(V, T): DiagnoseResult`

Analyzes mesh quality without modifying it.

**Returns:**

| Field | Type | Description |
|-------|------|-------------|
| `v` | number | Vertex count |
| `t` | number | Triangle count |
| `boundary` | number | Boundary edge count (edges shared by only 1 face) |
| `nonManifold` | number | Non-manifold edge count (edges shared by 3+ faces) |
| `windingInconsistencies` | number | Winding inconsistency count |
| `oppositeWindingPairs` | number | Opposite-winding duplicate pairs |
| `components` | number | Connected component count |
| `isWatertight` | boolean | `boundary === 0 && nonManifold === 0` |

### `async repairObject(V, T, onProgress?, options?): RepairResult`

Repairs a single mesh object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `V` | `Array<[x, y, z]>` | Vertex positions |
| `T` | `Array<[i, j, k]>` | Triangle indices |
| `onProgress` | `(status: string) => void` | Progress callback (optional) |
| `options` | `RepairOptions` | Repair options (optional) |

**Returns:** `{ V, T, report }`

### `async repairMesh(vertices, triangles, onProgress?, options?): RepairMeshResult`

Repairs and diagnoses. Convenience wrapper used by Web Workers.

**Returns:** `{ vertices, triangles, report, diagnosis }`

### `async repairAll(objects, onProgress?, options?): RepairAllResult`

Repairs multiple objects sequentially.

**Returns:** `{ objects: [{id, V, T, report, diagnosis}], totalReport }`

### `async parse3MF(buffer): ParseResult`

Parses a 3MF file (ArrayBuffer). Requires JSZip.

### `async write3MF(objects, originalXml, originalZip, modelPath): Blob`

Writes repaired objects back to a 3MF file.

### `async create3MF(objects): Blob`

Creates a new 3MF file from scratch.

---

## Repair Options

| Option | Default | Description |
|--------|---------|-------------|
| `mergePrecision` | auto | Vertex merge decimal precision (3–10) |
| `degenerateThreshold` | auto | Degenerate triangle area threshold |
| `holeAreaMultiplier` | auto | Hole-fill area limit multiplier (10–500) |
| `localRemeshLimit` | 0.3 | Local remesh triangle change ratio limit |
| `edgeFlipPasses` | 20 | Edge flip passes for winding fix |
| `nmEdgeIterations` | 50 | Non-manifold edge resolution iterations |
| `removeSmallShells` | false | Remove floating shell components |
| `removeSpuriousShells` | true | Remove spurious shells (< 2% triangles + < 1% volume) |
| `repairSelfIntersections` | false | Enable self-intersection repair |
| `deepRepair` | false | Voxel reconstruction: `false`, `'auto'`, `'always'` |
| `voxelResolution` | 'auto' | Voxel grid resolution |
| `voxelSmoothing` | true | Laplacian smoothing after Marching Cubes |
| `voxelSmoothIterations` | 3 | Smoothing iterations |
| `voxelSmoothLambda` | 0.5 | Smoothing damping factor |
| `voxelProjectToSurface` | true | Project vertices back to original surface |

---

## Repair Pipeline

The 8-stage repair pipeline processes meshes in this order:

```
Stage 0: Vertex Merging         — Consolidate nearby duplicate vertices
Stage 1: Degenerate Removal     — Remove zero-area / collapsed triangles
Stage 2: Opposite Winding Removal — Remove internal wall duplicates
Stage 3: Exact Duplicate Removal — Remove identical triangles
Stage 4: Normal Consistency     — BFS propagation + signed volume test
Stage 5: Non-Manifold Edges    — Score-based edge resolution
Stage 6: Non-Manifold Vertices — Fan separation
Stage 7: Hole Filling          — Pinch-point split + ear-clipping triangulation
Stage 8: Final Cleanup         — Iterative NM/boundary resolution
```

### Additional Repair Passes

| Pass | Trigger | Description |
|------|---------|-------------|
| Edge Flip WI Fix | WI > 0 after Stage 8 | Flip edge winding directions |
| Local Remesh WI Fix | WI > 0, NM = 0 | Re-triangulate WI regions |
| WI Recovery (Full Retry) | WI > 0, NM ≤ 3 | Re-run pipeline with self-intersection repair |
| Targeted WI Fix | Full retry rejected (triangle loss) | Remove only WI-edge triangles, re-fill holes (up to 5 passes) |
| Oversize Subdivision | Large triangles detected | Subdivide oversized triangles |
| Deep Repair | `deepRepair = 'auto'/'always'` | SDF + Marching Cubes voxel reconstruction |

### Safety Mechanisms

| Mechanism | Description |
|-----------|-------------|
| Triangle Preservation Check | Reject WI Recovery if result has < 60% of pre-recovery triangles |
| Targeted WI Fix | Surgically remove only WI-edge triangles instead of destroying geometry |
| Pinch-Point Split | Detect self-intersecting boundary loops (duplicate vertex positions) and split into simple sub-loops before ear-clipping, preventing interior membrane formation |
| Overlay Skip | Skip repair for valid overlay-shell meshes (count=4 edges, B=0, WI=0) |
| Thin Geometry Skip | Skip Deep Repair if any grid axis < 8 voxels |
| Zero MC Protection | Return original mesh if Marching Cubes produces 0 triangles |
| Volume Sign Check | Flip all normals if signed volume is negative |

---

## Repair Report

The `report` object returned by repair methods contains:

| Field | Description |
|-------|-------------|
| `merged` | Duplicate vertices merged |
| `degenerateRemoved` | Degenerate triangles removed |
| `oppositeWindingRemoved` | Opposite-winding duplicate pairs removed |
| `exactDuplicatesRemoved` | Exact duplicate triangles removed |
| `normalsFlipped` | Normals flipped for consistency |
| `nmFixed` | Non-manifold edges fixed |
| `nmVerticesSplit` | Non-manifold vertices split |
| `holesFilled` | Holes filled |
| `smallShellsRemoved` | Floating shells removed |
| `selfIntersectionsRepaired` | Self-intersections repaired |
| `deepRepairApplied` | Whether voxel reconstruction was used |
| `deepRepairResolution` | Voxel grid resolution used (0 = not applied) |

---

## Data Format

### Vertices

```javascript
// Array of [x, y, z] positions
const V = [
  [0.0, 1.0, 2.0],
  [3.0, 4.0, 5.0],
  // ...
];
```

### Triangles

```javascript
// Array of [vertexIndex0, vertexIndex1, vertexIndex2]
// Counter-clockwise winding = outward-facing normal
const T = [
  [0, 1, 2],
  [2, 1, 3],
  // ...
];
```

---

## Building from Source

Requirements: Emscripten SDK, CMake, Ninja

```bash
# Install Emscripten
# https://emscripten.org/docs/getting_started/downloads.html

# Build
bash build.sh
```

Output: `build/mesh-fix-core.js` + `build/mesh-fix-core.wasm`

---

## License

MIT

---
---

# MeshFixLib (日本語)

**高性能 WebAssembly メッシュ修復ライブラリ — 3Dプリント向け**

MeshFixLib v3.2 — C++ WASMエンジンによる自動8段階修復パイプライン

---

## 概要

MeshFixLibは、非多様体・非水密な3Dメッシュを3Dプリント用に修復するスタンドアロンライブラリです。WebAssembly（C++17からコンパイル）によりブラウザ上で完全に動作し、サーバー不要です。

**入力:** 破損メッシュ（穴、非多様体エッジ、巻き方向不整合、自己交差）
**出力:** スライサーに直接投入可能なクリーンで水密な多様体メッシュ

### 主な機能

- 8段階自動修復パイプライン（頂点マージ → 穴埋め → 最終クリーンアップ）
- 非多様体エッジ/頂点の解決
- 巻き方向不整合修復（エッジフリップ + 局所リメッシュ + Targeted WI修復）
- 自己交差検出・修復
- Deep Repairモード（SDF + Marching Cubesボクセル再構築）
- 3MFファイルのパース・書き出し（JSZip経由）
- リアルタイム進捗コールバック
- Web Workerによる非ブロッキングUI対応

---

## アーキテクチャ

```
MeshFixLib (JavaScriptクラス)
  └─ mesh-fix-core.wasm (C++17, Emscriptenでコンパイル)
      ├─ diagnose()      — メッシュ品質分析
      └─ repairObject()  — 完全修復パイプライン
```

| ファイル | サイズ | 説明 |
|----------|--------|------|
| `mesh-fix-lib.js` | ~15 KB | MeshFixLibクラス（WASMラッパー + 3MF I/O） |
| `build/mesh-fix-core.js` | ~32 KB | Emscripten WASMローダー |
| `build/mesh-fix-core.wasm` | ~345 KB | コンパイル済みC++修復エンジン |

**依存:** JSZip（3MFファイル処理のみ）

---

## インストール

```bash
npm install jszip
```

`mesh-fix-lib.js`、`build/mesh-fix-core.js`、`build/mesh-fix-core.wasm` をプロジェクトにコピーしてください。

---

## クイックスタート

### ブラウザ (ESモジュール)

```javascript
import MeshFixLib from './mesh-fix-lib.js';

const meshFix = new MeshFixLib();
await meshFix.init();

// メッシュ診断
const diagnosis = meshFix.diagnose(vertices, triangles);
console.log(`境界: ${diagnosis.boundary}, NM: ${diagnosis.nonManifold}, WI: ${diagnosis.windingInconsistencies}`);

// メッシュ修復
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);  // "Stage 0/8: Merging vertices..."
});

console.log(`修復完了: ${result.V.length}頂点, ${result.T.length}三角形`);
```

### Web Worker

```javascript
// mesh-repair-worker.js
importScripts('mesh-fix-core.js');
importScripts('mesh-fix-lib.js');

const meshFix = new MeshFixLib();
await meshFix.init();

self.onmessage = async (e) => {
  const { vertices, triangles } = e.data;
  const result = await meshFix.repairMesh(vertices, triangles, (status) => {
    self.postMessage({ type: 'progress', status });
  });
  self.postMessage({ type: 'result', ...result });
};
```

### Node.js

```javascript
const MeshFixLib = require('./mesh-fix-lib.js');

const meshFix = new MeshFixLib();
await meshFix.init();

const result = await meshFix.repairObject(vertices, triangles);
// result.V = 修復済み頂点, result.T = 修復済み三角形
```

### 3MFファイルワークフロー

```javascript
const meshFix = new MeshFixLib();
await meshFix.init();

// 1. 解析
const parsed = await meshFix.parse3MF(arrayBuffer);

// 2. 全オブジェクトを修復
const repaired = await meshFix.repairAll(parsed.objects, (event) => {
  if (event.type === 'progress') {
    console.log(`[Object ${event.objectId}] ${event.status}`);
  }
});

// 3. 書き出し
const blob = await meshFix.write3MF(
  repaired.objects, parsed.originalXml, parsed.zip, parsed.modelPath
);
```

---

## APIリファレンス

### `new MeshFixLib()`

新しいインスタンスを作成。

### `async init(wasmDir?: string): void`

WASMモジュールをロード。他のメソッドの前に必ず呼び出す。

| パラメータ | 型 | 説明 |
|------------|-----|------|
| `wasmDir` | string (省略可) | `.wasm`ファイルのディレクトリパス（末尾`/`必須） |

### `diagnose(V, T): DiagnoseResult`

メッシュの品質を分析（修復しない）。

**戻り値:**

| フィールド | 型 | 説明 |
|------------|-----|------|
| `v` | number | 頂点数 |
| `t` | number | 三角形数 |
| `boundary` | number | 境界エッジ数（1面のみが共有するエッジ） |
| `nonManifold` | number | 非多様体エッジ数（3面以上が共有するエッジ） |
| `windingInconsistencies` | number | 巻き方向不整合エッジ数 |
| `oppositeWindingPairs` | number | 反対巻き重複面ペア数 |
| `components` | number | 連結成分数 |
| `isWatertight` | boolean | 水密性（`boundary === 0 && nonManifold === 0`） |

### `async repairObject(V, T, onProgress?, options?): RepairResult`

単一メッシュオブジェクトを修復。

| パラメータ | 型 | 説明 |
|------------|-----|------|
| `V` | `Array<[x, y, z]>` | 頂点座標配列 |
| `T` | `Array<[i, j, k]>` | 三角形インデックス配列 |
| `onProgress` | `(status: string) => void` | 進捗コールバック（省略可） |
| `options` | `RepairOptions` | 修復オプション（省略可） |

**戻り値:** `{ V, T, report }`

### `async repairMesh(vertices, triangles, onProgress?, options?): RepairMeshResult`

修復 + 診断のラッパー。Web Workerで使用。

**戻り値:** `{ vertices, triangles, report, diagnosis }`

### `async repairAll(objects, onProgress?, options?): RepairAllResult`

複数オブジェクトを順次修復。

**戻り値:** `{ objects: [{id, V, T, report, diagnosis}], totalReport }`

### `async parse3MF(buffer): ParseResult`

3MFファイル（ArrayBuffer）を解析。JSZip必須。

### `async write3MF(objects, originalXml, originalZip, modelPath): Blob`

修復済みオブジェクトを3MFファイルに書き出し。

### `async create3MF(objects): Blob`

新規3MFファイルを作成。

---

## 修復オプション

| オプション | デフォルト | 説明 |
|------------|------------|------|
| `mergePrecision` | auto | 頂点マージの小数桁精度（3〜10） |
| `degenerateThreshold` | auto | 縮退三角形の面積閾値 |
| `holeAreaMultiplier` | auto | 穴埋め面積上限の倍率（10〜500） |
| `localRemeshLimit` | 0.3 | 局所リメッシュの三角形変更比率上限 |
| `edgeFlipPasses` | 20 | エッジフリップによる巻き方向修正パス数 |
| `nmEdgeIterations` | 50 | 非多様体エッジ解決の最大反復数 |
| `removeSmallShells` | false | 浮遊シェル除去（opt-in） |
| `removeSpuriousShells` | true | スプリアスシェル除去（三角形数2%未満 + 体積1%未満） |
| `repairSelfIntersections` | false | 自己交差修復の有効化 |
| `deepRepair` | false | ボクセル再構築: `false`, `'auto'`, `'always'` |
| `voxelResolution` | 'auto' | ボクセルグリッド解像度 |
| `voxelSmoothing` | true | MC後のLaplacian平滑化 |
| `voxelSmoothIterations` | 3 | 平滑化反復数 |
| `voxelSmoothLambda` | 0.5 | 平滑化減衰係数 |
| `voxelProjectToSurface` | true | 元サーフェスへの頂点投影 |

---

## 修復パイプライン

8段階の修復パイプラインでメッシュを処理します:

```
Stage 0: 頂点マージ         ─── 空間的に近い重複頂点を統合
Stage 1: 縮退三角形除去     ─── ゼロ面積・潰れた三角形を除去
Stage 2: 反対巻き重複除去   ─── 内部壁の重複面ペアを除去
Stage 3: 完全重複除去       ─── 同一三角形を除去
Stage 4: 法線整合性         ─── BFS伝播 + 符号付き体積テスト
Stage 5: 非多様体エッジ解決 ─── スコアベースのエッジ解決
Stage 6: 非多様体頂点分離   ─── ファン分離
Stage 7: 穴埋め             ─── ピンチポイント分割 + イヤークリッピング三角形分割
Stage 8: 最終クリーンアップ ─── NM/境界の反復解決
```

### 追加修復パス

| パス | トリガー | 説明 |
|------|----------|------|
| エッジフリップWI修復 | Stage 8後にWI > 0 | エッジの巻き方向を反転 |
| 局所リメッシュWI修復 | WI > 0、NM = 0 | WI領域を局所的に再メッシュ |
| WI Recovery（フルリトライ） | WI > 0、NM ≤ 3 | SI修復付きでパイプライン再実行 |
| Targeted WI修復 | フルリトライが三角形損失で拒否 | WIエッジの三角形のみ除去→穴埋め（最大5パス） |
| 巨大三角形分割 | 巨大三角形検出時 | 大きすぎる三角形をサブディバイド |
| Deep Repair | `deepRepair = 'auto'/'always'` | SDF + Marching Cubesボクセル再構築 |

### 安全機構

| 機構 | 説明 |
|------|------|
| 三角形保存チェック | WI Recoveryリトライが60%未満の三角形になる場合は拒否 |
| Targeted WI修復 | ジオメトリ破壊を防止し、WIエッジのみを外科的に除去 |
| ピンチポイント分割 | 穴埋め前に境界ループの自己交差（同一座標の頂点ペア）を検出し、単純サブループに分割。内部膜の生成を防止 |
| オーバーレイスキップ | 有効なオーバーレイシェルメッシュは修復をスキップ |
| 薄ジオメトリスキップ | グリッド軸が8ボクセル未満ならDeep Repairをスキップ |
| MC出力ゼロ保護 | Marching Cubesが0三角形の場合は元メッシュを返却 |
| 体積符号検証 | 符号付き体積が負なら全面の法線を反転 |

---

## 修復レポート

修復メソッドが返す `report` オブジェクト:

| フィールド | 説明 |
|------------|------|
| `merged` | マージされた重複頂点数 |
| `degenerateRemoved` | 除去された縮退三角形数 |
| `oppositeWindingRemoved` | 除去された反対巻き重複ペア数 |
| `exactDuplicatesRemoved` | 除去された完全重複三角形数 |
| `normalsFlipped` | 反転された法線数 |
| `nmFixed` | 修正された非多様体エッジ数 |
| `nmVerticesSplit` | 分離された非多様体頂点数 |
| `holesFilled` | 埋められた穴の数 |
| `smallShellsRemoved` | 除去された浮遊シェル数 |
| `selfIntersectionsRepaired` | 修復された自己交差数 |
| `deepRepairApplied` | ボクセル再構築が実行されたか |
| `deepRepairResolution` | 使用されたボクセル解像度（0 = 未実行） |

---

## ソースからのビルド

必要環境: Emscripten SDK, CMake, Ninja

```bash
# Emscriptenのインストール
# https://emscripten.org/docs/getting_started/downloads.html

# ビルド
bash build.sh
```

出力: `build/mesh-fix-core.js` + `build/mesh-fix-core.wasm`

---

## ライセンス

MIT

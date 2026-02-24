# MeshFixLib

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Dependencies](https://img.shields.io/badge/dependencies-JSZip_only-brightgreen.svg)
![Size](https://img.shields.io/badge/WASM-345KB-blue.svg)
![Version](https://img.shields.io/badge/version-3.2-orange.svg)

[日本語](#japanese) | [English](#english)

---

<a id="english"></a>
## 🇺🇸 English

A high-performance WebAssembly mesh repair library that fixes non-manifold, non-watertight 3D meshes for 3D printing — entirely in the browser. C++17 engine compiled via Emscripten with an automatic 8-stage repair pipeline, no server required.

### ✨ Why This Library?

Preparing 3D models for printing often means dealing with broken meshes — holes, flipped normals, non-manifold edges, and self-intersections. Existing repair tools are typically desktop-only or server-dependent. This creates friction:

- Users must install heavy desktop software or upload models to remote servers.
- Many tools fail on severely damaged meshes with complex non-manifold topology.
- Real-time feedback and browser-native workflows are unavailable.

**MeshFixLib** solves all of these problems:

| Problem | Solution |
|---|---|
| Desktop or server-only repair tools | Runs entirely in-browser via WebAssembly |
| Broken meshes with holes and bad topology | 8-stage automated repair pipeline |
| Non-manifold edges/vertices remain after repair | Score-based edge resolution + fan separation |
| Winding direction inconsistencies | Edge flip + local remesh + targeted WI fix |
| Self-intersections in complex models | Optional self-intersection detection and repair |
| Severely damaged meshes beyond standard repair | Deep Repair mode (SDF + Marching Cubes voxel reconstruction) |
| No 3MF file support | Full 3MF parse / repair / write workflow |

### 📦 File Structure

| File | Size | Description |
|---|---|---|
| `mesh-fix-lib.js` | ~15 KB | MeshFixLib class (WASM wrapper + 3MF I/O) |
| `build/mesh-fix-core.js` | ~32 KB | Emscripten WASM loader |
| `build/mesh-fix-core.wasm` | ~345 KB | Compiled C++ repair engine |

### 🔧 Features

**Repair Pipeline**
- 8-stage automated repair (vertex merge → hole fill → final cleanup)
- Non-manifold edge/vertex resolution
- Winding inconsistency repair (edge flip + local remesh + targeted WI fix)
- Self-intersection detection and repair
- Deep Repair mode (SDF + Marching Cubes voxel reconstruction)
- Safety mechanisms: triangle preservation check, zero MC protection, volume sign check

**3MF Support**
- 3MF file parsing and writing (via JSZip)
- Multi-object batch repair with `repairAll()`
- Create new 3MF files from scratch

**Integration**
- Real-time progress callbacks
- Web Worker support for non-blocking UI
- Browser (ES Module), CommonJS (Node.js), and Web Worker compatible

### 🚀 Quick Start

```bash
npm install jszip
```

Copy `mesh-fix-lib.js`, `build/mesh-fix-core.js`, and `build/mesh-fix-core.wasm` to your project.

```javascript
import MeshFixLib from './mesh-fix-lib.js';

const meshFix = new MeshFixLib();
await meshFix.init();

// 1. Diagnose a mesh
const diagnosis = meshFix.diagnose(vertices, triangles);
console.log(`Boundary: ${diagnosis.boundary}, NM: ${diagnosis.nonManifold}, WI: ${diagnosis.windingInconsistencies}`);

// 2. Repair a mesh
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);  // "Stage 0/8: Merging vertices..."
});

console.log(`Repaired: ${result.V.length} vertices, ${result.T.length} triangles`);
```

#### 3MF File Workflow

```javascript
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

### 📖 API Reference

#### `new MeshFixLib()`

Creates a new instance.

#### `async init(wasmDir?: string): void`

Loads the WASM module. Must be called before any other method.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `wasmDir` | string | `undefined` | Directory path to `.wasm` file (trailing `/` required) |

#### `diagnose(V, T): DiagnoseResult`

Analyzes mesh quality without modifying it.

```javascript
const diagnosis = meshFix.diagnose(vertices, triangles);
// diagnosis.v                     — Vertex count
// diagnosis.t                     — Triangle count
// diagnosis.boundary              — Boundary edge count (1-face edges)
// diagnosis.nonManifold           — Non-manifold edge count (3+ face edges)
// diagnosis.windingInconsistencies — Winding inconsistency count
// diagnosis.oppositeWindingPairs  — Opposite-winding duplicate pairs
// diagnosis.components            — Connected component count
// diagnosis.isWatertight          — boundary === 0 && nonManifold === 0
```

#### `async repairObject(V, T, onProgress?, options?): RepairResult`

Repairs a single mesh object.

```javascript
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);
});
// result.V      — Repaired vertex positions [x, y, z][]
// result.T      — Repaired triangle indices [i, j, k][]
// result.report — Detailed repair report
```

**Options:**
| Key | Type | Default | Description |
|---|---|---|---|
| `mergePrecision` | number | auto | Vertex merge decimal precision (3–10) |
| `degenerateThreshold` | number | auto | Degenerate triangle area threshold |
| `holeAreaMultiplier` | number | auto | Hole-fill area limit multiplier (10–500) |
| `localRemeshLimit` | number | `0.3` | Local remesh triangle change ratio limit |
| `edgeFlipPasses` | number | `20` | Edge flip passes for winding fix |
| `nmEdgeIterations` | number | `50` | Non-manifold edge resolution iterations |
| `removeSmallShells` | boolean | `false` | Remove floating shell components |
| `removeSpuriousShells` | boolean | `true` | Remove spurious shells (< 2% triangles + < 1% volume) |
| `repairSelfIntersections` | boolean | `false` | Enable self-intersection repair |
| `deepRepair` | boolean/string | `false` | Voxel reconstruction: `false`, `'auto'`, `'always'` |
| `voxelResolution` | string/number | `'auto'` | Voxel grid resolution |
| `voxelSmoothing` | boolean | `true` | Laplacian smoothing after Marching Cubes |
| `voxelSmoothIterations` | number | `3` | Smoothing iterations |
| `voxelSmoothLambda` | number | `0.5` | Smoothing damping factor |
| `voxelProjectToSurface` | boolean | `true` | Project vertices back to original surface |

#### `async repairMesh(vertices, triangles, onProgress?, options?): RepairMeshResult`

Repairs and diagnoses. Convenience wrapper used by Web Workers.

**Returns:** `{ vertices, triangles, report, diagnosis }`

#### `async repairAll(objects, onProgress?, options?): RepairAllResult`

Repairs multiple objects sequentially.

**Returns:** `{ objects: [{id, V, T, report, diagnosis}], totalReport }`

#### `async parse3MF(buffer): ParseResult`

Parses a 3MF file (ArrayBuffer). Requires JSZip.

#### `async write3MF(objects, originalXml, originalZip, modelPath): Blob`

Writes repaired objects back to a 3MF file.

#### `async create3MF(objects): Blob`

Creates a new 3MF file from scratch.

### 🏗️ Architecture

```
MeshFixLib
│
├─ mesh-fix-lib.js (~15KB)
│  ├─ MeshFixLib class    — WASM wrapper + public API
│  ├─ 3MF I/O             — parse3MF / write3MF / create3MF (via JSZip)
│  └─ Progress callbacks  — Real-time status reporting
│
└─ mesh-fix-core.wasm (~345KB, C++17 via Emscripten)
   │
   ├─ diagnose()          — Mesh quality analysis
   └─ repairObject()      — 8-stage repair pipeline
      │
      ├─ Stage 0: Vertex Merging
      ├─ Stage 1: Degenerate Removal
      ├─ Stage 2: Opposite Winding Removal
      ├─ Stage 3: Exact Duplicate Removal
      ├─ Stage 4: Normal Consistency (BFS + signed volume)
      ├─ Stage 5: Non-Manifold Edges (score-based)
      ├─ Stage 6: Non-Manifold Vertices (fan separation)
      ├─ Stage 7: Hole Filling (ear-clipping)
      └─ Stage 8: Final Cleanup
      │
      ├─ Edge Flip WI Fix
      ├─ Local Remesh WI Fix
      ├─ WI Recovery (full retry)
      ├─ Targeted WI Fix
      ├─ Oversize Subdivision
      └─ Deep Repair (SDF + Marching Cubes)
```

### 🛡️ Safety Mechanisms

| Mechanism | Description |
|---|---|
| Triangle Preservation Check | Reject WI Recovery if result has < 60% of pre-recovery triangles |
| Targeted WI Fix | Surgically remove only WI-edge triangles instead of destroying geometry |
| Overlay Skip | Skip repair for valid overlay-shell meshes (count=4 edges, B=0, WI=0) |
| Thin Geometry Skip | Skip Deep Repair if any grid axis < 8 voxels |
| Zero MC Protection | Return original mesh if Marching Cubes produces 0 triangles |
| Volume Sign Check | Flip all normals if signed volume is negative |

### 📊 Repair Report

The `report` object returned by repair methods:

| Field | Description |
|---|---|
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

### 📐 Data Format

```javascript
// Vertices: Array of [x, y, z] positions
const V = [
  [0.0, 1.0, 2.0],
  [3.0, 4.0, 5.0],
];

// Triangles: Array of [i, j, k] indices
// Counter-clockwise winding = outward-facing normal
const T = [
  [0, 1, 2],
  [2, 1, 3],
];
```

### ⚙️ Environment Support

| Environment | Status |
|---|---|
| Browser (ES Module) | ✅ `import MeshFixLib` |
| CommonJS (Node.js) | ✅ `require()` |
| Web Worker | ✅ `importScripts()` |

### 🔨 Building from Source

Requirements: Emscripten SDK, CMake, Ninja

```bash
bash build.sh
```

Output: `build/mesh-fix-core.js` + `build/mesh-fix-core.wasm`

### 📄 License

MIT License.

---

<a id="japanese"></a>
## 🇯🇵 日本語 (Japanese)

非多様体・非水密な3Dメッシュを3Dプリント用に修復する、高性能WebAssemblyメッシュ修復ライブラリです。C++17エンジンをEmscriptenでコンパイルし、ブラウザ上で完全動作。自動8段階修復パイプラインを備え、サーバー不要です。

### ✨ このライブラリを作った理由

3Dプリント用モデルの準備では、穴・法線反転・非多様体エッジ・自己交差といった破損メッシュへの対処が頻繁に必要です。既存の修復ツールはデスクトップ専用やサーバー依存がほとんどで、いくつかの問題があります：

- 重いデスクトップソフトのインストールやサーバーへのアップロードが必要
- 複雑な非多様体トポロジーを持つ重度の破損メッシュで失敗しがち
- リアルタイムフィードバックやブラウザネイティブなワークフローが利用不可

**MeshFixLib** はこれらの問題をすべて解決します：

| 問題 | 解決策 |
|---|---|
| デスクトップ/サーバー専用の修復ツール | WebAssemblyによりブラウザ上で完全動作 |
| 穴やトポロジー異常のある破損メッシュ | 8段階自動修復パイプライン |
| 修復後も非多様体エッジ/頂点が残る | スコアベースエッジ解決 + ファン分離 |
| 巻き方向の不整合 | エッジフリップ + 局所リメッシュ + Targeted WI修復 |
| 複雑モデルの自己交差 | オプションの自己交差検出・修復 |
| 通常修復では対応不能な重度の破損メッシュ | Deep Repairモード（SDF + Marching Cubesボクセル再構築） |
| 3MFファイル非対応 | 3MF パース / 修復 / 書き出しの完全ワークフロー |

### 📦 ファイル構成

| ファイル | サイズ | 説明 |
|---|---|---|
| `mesh-fix-lib.js` | ~15 KB | MeshFixLibクラス（WASMラッパー + 3MF I/O） |
| `build/mesh-fix-core.js` | ~32 KB | Emscripten WASMローダー |
| `build/mesh-fix-core.wasm` | ~345 KB | コンパイル済みC++修復エンジン |

### 🔧 機能

**修復パイプライン**
- 8段階自動修復（頂点マージ → 穴埋め → 最終クリーンアップ）
- 非多様体エッジ/頂点の解決
- 巻き方向不整合修復（エッジフリップ + 局所リメッシュ + Targeted WI修復）
- 自己交差検出・修復
- Deep Repairモード（SDF + Marching Cubesボクセル再構築）
- 安全機構：三角形保存チェック、MC出力ゼロ保護、体積符号検証

**3MFサポート**
- 3MFファイルのパース・書き出し（JSZip経由）
- `repairAll()` による複数オブジェクト一括修復
- 新規3MFファイルの作成

**統合**
- リアルタイム進捗コールバック
- Web Workerによる非ブロッキングUI対応
- ブラウザ（ESモジュール）、CommonJS（Node.js）、Web Worker対応

### 🚀 クイックスタート

```bash
npm install jszip
```

`mesh-fix-lib.js`、`build/mesh-fix-core.js`、`build/mesh-fix-core.wasm` をプロジェクトにコピーしてください。

```javascript
import MeshFixLib from './mesh-fix-lib.js';

const meshFix = new MeshFixLib();
await meshFix.init();

// 1. メッシュ診断
const diagnosis = meshFix.diagnose(vertices, triangles);
console.log(`境界: ${diagnosis.boundary}, NM: ${diagnosis.nonManifold}, WI: ${diagnosis.windingInconsistencies}`);

// 2. メッシュ修復
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);  // "Stage 0/8: Merging vertices..."
});

console.log(`修復完了: ${result.V.length}頂点, ${result.T.length}三角形`);
```

#### 3MFファイルワークフロー

```javascript
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

### 📖 APIリファレンス

#### `new MeshFixLib()`

新しいインスタンスを作成。

#### `async init(wasmDir?: string): void`

WASMモジュールをロード。他のメソッドの前に必ず呼び出す。

| パラメータ | 型 | デフォルト | 説明 |
|---|---|---|---|
| `wasmDir` | string | `undefined` | `.wasm`ファイルのディレクトリパス（末尾`/`必須） |

#### `diagnose(V, T): DiagnoseResult`

メッシュの品質を分析（修復しない）。

```javascript
const diagnosis = meshFix.diagnose(vertices, triangles);
// diagnosis.v                     — 頂点数
// diagnosis.t                     — 三角形数
// diagnosis.boundary              — 境界エッジ数（1面のみが共有）
// diagnosis.nonManifold           — 非多様体エッジ数（3面以上が共有）
// diagnosis.windingInconsistencies — 巻き方向不整合エッジ数
// diagnosis.oppositeWindingPairs  — 反対巻き重複面ペア数
// diagnosis.components            — 連結成分数
// diagnosis.isWatertight          — 水密性（boundary === 0 && nonManifold === 0）
```

#### `async repairObject(V, T, onProgress?, options?): RepairResult`

単一メッシュオブジェクトを修復。

```javascript
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);
});
// result.V      — 修復済み頂点座標 [x, y, z][]
// result.T      — 修復済み三角形インデックス [i, j, k][]
// result.report — 詳細修復レポート
```

**オプション:**
| キー | 型 | デフォルト | 説明 |
|---|---|---|---|
| `mergePrecision` | number | auto | 頂点マージの小数桁精度（3〜10） |
| `degenerateThreshold` | number | auto | 縮退三角形の面積閾値 |
| `holeAreaMultiplier` | number | auto | 穴埋め面積上限の倍率（10〜500） |
| `localRemeshLimit` | number | `0.3` | 局所リメッシュの三角形変更比率上限 |
| `edgeFlipPasses` | number | `20` | エッジフリップパス数 |
| `nmEdgeIterations` | number | `50` | 非多様体エッジ解決の最大反復数 |
| `removeSmallShells` | boolean | `false` | 浮遊シェル除去 |
| `removeSpuriousShells` | boolean | `true` | スプリアスシェル除去（三角形数2%未満 + 体積1%未満） |
| `repairSelfIntersections` | boolean | `false` | 自己交差修復の有効化 |
| `deepRepair` | boolean/string | `false` | ボクセル再構築: `false`, `'auto'`, `'always'` |
| `voxelResolution` | string/number | `'auto'` | ボクセルグリッド解像度 |
| `voxelSmoothing` | boolean | `true` | MC後のLaplacian平滑化 |
| `voxelSmoothIterations` | number | `3` | 平滑化反復数 |
| `voxelSmoothLambda` | number | `0.5` | 平滑化減衰係数 |
| `voxelProjectToSurface` | boolean | `true` | 元サーフェスへの頂点投影 |

#### `async repairMesh(vertices, triangles, onProgress?, options?): RepairMeshResult`

修復 + 診断のラッパー。Web Workerで使用。

**戻り値:** `{ vertices, triangles, report, diagnosis }`

#### `async repairAll(objects, onProgress?, options?): RepairAllResult`

複数オブジェクトを順次修復。

**戻り値:** `{ objects: [{id, V, T, report, diagnosis}], totalReport }`

#### `async parse3MF(buffer): ParseResult`

3MFファイル（ArrayBuffer）を解析。JSZip必須。

#### `async write3MF(objects, originalXml, originalZip, modelPath): Blob`

修復済みオブジェクトを3MFファイルに書き出し。

#### `async create3MF(objects): Blob`

新規3MFファイルを作成。

### 🏗️ アーキテクチャ

```
MeshFixLib
│
├─ mesh-fix-lib.js (~15KB)
│  ├─ MeshFixLib class    — WASMラッパー + 公開API
│  ├─ 3MF I/O             — parse3MF / write3MF / create3MF（JSZip経由）
│  └─ 進捗コールバック     — リアルタイムステータス通知
│
└─ mesh-fix-core.wasm (~345KB, C++17 via Emscripten)
   │
   ├─ diagnose()          — メッシュ品質分析
   └─ repairObject()      — 8段階修復パイプライン
      │
      ├─ Stage 0: 頂点マージ
      ├─ Stage 1: 縮退三角形除去
      ├─ Stage 2: 反対巻き重複除去
      ├─ Stage 3: 完全重複除去
      ├─ Stage 4: 法線整合性（BFS + 符号付き体積）
      ├─ Stage 5: 非多様体エッジ解決（スコアベース）
      ├─ Stage 6: 非多様体頂点分離（ファン分離）
      ├─ Stage 7: 穴埋め（イヤークリッピング）
      └─ Stage 8: 最終クリーンアップ
      │
      ├─ エッジフリップWI修復
      ├─ 局所リメッシュWI修復
      ├─ WI Recovery（フルリトライ）
      ├─ Targeted WI修復
      ├─ 巨大三角形分割
      └─ Deep Repair（SDF + Marching Cubes）
```

### 🛡️ 安全機構

| 機構 | 説明 |
|---|---|
| 三角形保存チェック | WI Recoveryリトライが60%未満の三角形になる場合は拒否 |
| Targeted WI修復 | ジオメトリ破壊を防止し、WIエッジのみを外科的に除去 |
| オーバーレイスキップ | 有効なオーバーレイシェルメッシュは修復をスキップ |
| 薄ジオメトリスキップ | グリッド軸が8ボクセル未満ならDeep Repairをスキップ |
| MC出力ゼロ保護 | Marching Cubesが0三角形の場合は元メッシュを返却 |
| 体積符号検証 | 符号付き体積が負なら全面の法線を反転 |

### 📊 修復レポート

修復メソッドが返す `report` オブジェクト:

| フィールド | 説明 |
|---|---|
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

### 📐 データフォーマット

```javascript
// 頂点: [x, y, z] 座標の配列
const V = [
  [0.0, 1.0, 2.0],
  [3.0, 4.0, 5.0],
];

// 三角形: [i, j, k] インデックスの配列
// 反時計回り = 外向き法線
const T = [
  [0, 1, 2],
  [2, 1, 3],
];
```

### ⚙️ 動作環境

| 環境 | 状態 |
|---|---|
| ブラウザ (ESモジュール) | ✅ `import MeshFixLib` |
| CommonJS (Node.js) | ✅ `require()` |
| Web Worker | ✅ `importScripts()` |

### 🔨 ソースからのビルド

必要環境: Emscripten SDK, CMake, Ninja

```bash
bash build.sh
```

出力: `build/mesh-fix-core.js` + `build/mesh-fix-core.wasm`

### 📄 ライセンス

MIT License.

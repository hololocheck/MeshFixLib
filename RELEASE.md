# MeshFixLib v3.2.0 — WASM Release

High-performance WebAssembly mesh repair library for 3D printing.
Fixes non-manifold, non-watertight meshes entirely in the browser — no server required.

[日本語は下にあります](#meshfixlib-v320--wasm-リリース)

---

## Highlights

- **C++ WASM Engine** — Core repair logic compiled from C++17 via Emscripten for maximum performance
- **8-Stage Automated Pipeline** — Vertex merge, degenerate removal, normal consistency, non-manifold resolution, hole filling, and more
- **Targeted WI Repair** — Surgically fixes winding inconsistencies without destroying geometry (96%+ triangle preservation)
- **Deep Repair Mode** — SDF + Marching Cubes voxel reconstruction for severely broken meshes
- **Self-Intersection Repair** — Detects and resolves self-intersecting triangles
- **3MF Support** — Parse and write 3MF files with multi-object handling
- **Web Worker Ready** — Non-blocking UI with real-time progress callbacks
- **JS Fallback** — Automatic fallback to pure JavaScript if WASM fails to load

## Release Files

| File | Description |
|------|-------------|
| `mesh-fix-lib.js` | MeshFixLib class — WASM wrapper + 3MF I/O (~15 KB) |
| `build/mesh-fix-core.js` | Emscripten WASM loader (~32 KB) |
| `build/mesh-fix-core.wasm` | Compiled C++ repair engine (~345 KB) |
| `mesh-repair-3mf-v37.html` | Standalone HTML tool with embedded Worker (WASM + JS fallback) |

**Total size: ~400 KB** (excluding HTML tool)

**Dependency:** [JSZip](https://stuk.github.io/jszip/) (required only for 3MF file handling)

## Quick Start

```javascript
import MeshFixLib from './mesh-fix-lib.js';

const meshFix = new MeshFixLib();
await meshFix.init();

// Diagnose
const d = meshFix.diagnose(vertices, triangles);
console.log(`NM: ${d.nonManifold}, B: ${d.boundary}, WI: ${d.windingInconsistencies}`);

// Repair
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);
});
// result.V = repaired vertices, result.T = repaired triangles
```

## Repair Pipeline

```
Stage 0: Vertex Merging          — Consolidate nearby duplicate vertices
Stage 1: Degenerate Removal      — Remove zero-area / collapsed triangles
Stage 2: Opposite Winding Removal — Remove internal wall duplicates
Stage 3: Exact Duplicate Removal  — Remove identical triangles
Stage 4: Normal Consistency       — BFS propagation + signed volume test
Stage 5: Non-Manifold Edges      — Score-based edge resolution
Stage 6: Non-Manifold Vertices   — Fan separation
Stage 7: Hole Filling            — Pinch-point split + ear-clipping triangulation
Stage 8: Final Cleanup           — Iterative NM/boundary resolution
```

**Additional passes:** Edge flip WI fix, Local remesh WI fix, WI Recovery (full retry with SI repair), Targeted WI fix (surgical WI-edge removal), Oversize subdivision, Deep Repair (SDF + MC)

## Safety Mechanisms

- **Triangle Preservation Check** — Rejects WI Recovery retry if < 60% triangles would remain
- **Targeted WI Fix** — Removes only WI-edge triangles instead of full geometry rebuild
- **Pinch-Point Split** — Detects self-intersecting boundary loops (duplicate vertex positions) and splits into simple sub-loops before ear-clipping, preventing interior membrane formation
- **Overlay Skip** — Skips repair for valid overlay-shell meshes
- **Thin Geometry Skip** — Skips Deep Repair if grid axis < 8 voxels
- **Zero MC Protection** — Returns original mesh if Marching Cubes produces 0 triangles
- **Volume Sign Check** — Flips all normals if signed volume is negative

## Platforms

| Platform | Support |
|----------|---------|
| Browser (ES Module) | Full WASM support |
| Web Worker | Full WASM support |
| Node.js | Full WASM support |
| Standalone HTML | WASM with JS fallback |

## Build from Source

Requires: Emscripten SDK, CMake, Ninja

```bash
bash build.sh
```

## License

MIT

---
---

# MeshFixLib v3.2.0 — WASM リリース

高性能 WebAssembly メッシュ修復ライブラリ — 3Dプリント向け
非多様体・非水密なメッシュをブラウザ上で完全に修復。サーバー不要。

---

## 主な特徴

- **C++ WASMエンジン** — C++17からEmscriptenでコンパイル、最高のパフォーマンス
- **8段階自動パイプライン** — 頂点マージ、縮退除去、法線整合性、非多様体解決、穴埋めなど
- **Targeted WI修復** — ジオメトリを破壊せず巻き方向不整合を外科的に修復（三角形96%以上保存）
- **Deep Repairモード** — SDF + Marching Cubesによるボクセル再構築（重度に破損したメッシュ向け）
- **自己交差修復** — 自己交差三角形の検出と解決
- **3MFサポート** — マルチオブジェクト対応の3MFファイル読み書き
- **Web Worker対応** — リアルタイム進捗コールバック付きの非ブロッキングUI
- **JSフォールバック** — WASMロード失敗時に自動的に純JavaScript版にフォールバック

## リリースファイル

| ファイル | 説明 |
|----------|------|
| `mesh-fix-lib.js` | MeshFixLibクラス — WASMラッパー + 3MF I/O (~15 KB) |
| `build/mesh-fix-core.js` | Emscripten WASMローダー (~32 KB) |
| `build/mesh-fix-core.wasm` | コンパイル済みC++修復エンジン (~345 KB) |
| `mesh-repair-3mf-v37.html` | スタンドアロンHTMLツール（WASM + JSフォールバック内蔵） |

**合計サイズ: ~400 KB**（HTMLツール除く）

**依存:** [JSZip](https://stuk.github.io/jszip/)（3MFファイル処理のみ必要）

## クイックスタート

```javascript
import MeshFixLib from './mesh-fix-lib.js';

const meshFix = new MeshFixLib();
await meshFix.init();

// 診断
const d = meshFix.diagnose(vertices, triangles);
console.log(`NM: ${d.nonManifold}, B: ${d.boundary}, WI: ${d.windingInconsistencies}`);

// 修復
const result = await meshFix.repairObject(vertices, triangles, (status) => {
  console.log(status);
});
// result.V = 修復済み頂点, result.T = 修復済み三角形
```

## 修復パイプライン

```
Stage 0: 頂点マージ          ─── 空間的に近い重複頂点を統合
Stage 1: 縮退三角形除去      ─── ゼロ面積・潰れた三角形を除去
Stage 2: 反対巻き重複除去    ─── 内部壁の重複面ペアを除去
Stage 3: 完全重複除去        ─── 同一三角形を除去
Stage 4: 法線整合性          ─── BFS伝播 + 符号付き体積テスト
Stage 5: 非多様体エッジ解決  ─── スコアベースのエッジ解決
Stage 6: 非多様体頂点分離    ─── ファン分離
Stage 7: 穴埋め              ─── ピンチポイント分割 + イヤークリッピング三角形分割
Stage 8: 最終クリーンアップ  ─── NM/境界の反復解決
```

**追加パス:** エッジフリップWI修復、局所リメッシュWI修復、WI Recovery（SI修復付きフルリトライ）、Targeted WI修復（WIエッジのみ外科的除去）、巨大三角形分割、Deep Repair（SDF + MC）

## 安全機構

- **三角形保存チェック** — WI Recoveryリトライで三角形が60%未満になる場合は拒否
- **Targeted WI修復** — 全体再構築ではなくWIエッジの三角形のみを除去
- **ピンチポイント分割** — 穴埋め前に境界ループの自己交差（同一座標の頂点ペア）を検出し、単純サブループに分割。内部膜の生成を防止
- **オーバーレイスキップ** — 有効なオーバーレイシェルメッシュは修復をスキップ
- **薄ジオメトリスキップ** — グリッド軸が8ボクセル未満ならDeep Repairをスキップ
- **MC出力ゼロ保護** — Marching Cubesが0三角形の場合は元メッシュを返却
- **体積符号検証** — 符号付き体積が負なら全面の法線を反転

## 対応プラットフォーム

| プラットフォーム | サポート |
|------------------|----------|
| ブラウザ (ESモジュール) | WASM完全対応 |
| Web Worker | WASM完全対応 |
| Node.js | WASM完全対応 |
| スタンドアロンHTML | WASM + JSフォールバック |

## ソースからのビルド

必要環境: Emscripten SDK, CMake, Ninja

```bash
bash build.sh
```

## ライセンス

MIT

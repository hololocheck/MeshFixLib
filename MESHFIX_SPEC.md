# MeshFixLib v3.2 WASM — 完全仕様書

> KeyboardStudio v1 導入用
> 最終更新: 2026-02-25

---

## 1. システム概要

### 1.1 アーキテクチャ

```
KeyboardStudio (index.html)
  ├─ MeshRepairService        ← メインスレッド（Worker管理）
  │    └─ mesh-repair-worker.js  ← Web Worker（非同期修復実行）
  │         ├─ mesh-fix-lib.js      ← MeshFixLib クラス（WASM薄ラッパー）
  │         └─ mesh-fix-core.js     ← Emscripten WASM ローダー
  │              └─ mesh-fix-core.wasm  ← C++ コンパイル済みバイナリ
  └─ repairGeoWithMeshFix()   ← THREE.js BufferGeometry 変換ヘルパー
```

### 1.2 必要ファイル一覧

| ファイル | 配置先（KS基準） | サイズ | 説明 |
|----------|-------------------|--------|------|
| `mesh-fix-lib.js` | `lib/mesh-fix-lib.js` | ~15KB | MeshFixLib クラス（WASM薄ラッパー + 3MFパース/書き出し） |
| `mesh-fix-core.js` | `lib/mesh-fix/mesh-fix-core.js` | ~32KB | Emscripten WASM ローダースクリプト |
| `mesh-fix-core.wasm` | `lib/mesh-fix/mesh-fix-core.wasm` | ~345KB | C++ コンパイル済みバイナリ |
| `mesh-repair-worker.js` | `workers/mesh-repair-worker.js` | ~2KB | Web Worker エントリポイント |

### 1.3 バージョン情報

| コンポーネント | バージョン |
|----------------|------------|
| MeshFixLib | v3.2.0-wasm |
| HTML Worker (standalone) | v38 |
| WASM コア | C++17 + Emscripten |
| 依存ライブラリ | JSZip 3.10.1（3MF処理のみ） |

---

## 2. API リファレンス

### 2.1 MeshFixLib クラス

```javascript
class MeshFixLib {
  VERSION: string;  // '3.2.0-wasm'
}
```

#### `async init(wasmDir?: string): void`

WASMモジュールをロード。修復・診断の前に必ず呼び出す。

| パラメータ | 型 | 説明 |
|------------|-----|------|
| `wasmDir` | string (省略可) | `.wasm` ファイルの配置ディレクトリ（末尾`/`付き）。省略時はEmscriptenデフォルトパス |

**Worker内での使用例:**
```javascript
importScripts('../lib/mesh-fix/mesh-fix-core.js');
importScripts('../lib/mesh-fix-lib.js');
const meshFix = new MeshFixLib();
await meshFix.init('../lib/mesh-fix/');
```

#### `diagnose(V, T): DiagnoseResult`

メッシュの品質を診断（修復しない）。

| パラメータ | 型 | 説明 |
|------------|-----|------|
| `V` | `Array<[x, y, z]>` | 頂点座標配列 |
| `T` | `Array<[i, j, k]>` | 三角形インデックス配列 |

**戻り値: DiagnoseResult**

| フィールド | 型 | 説明 |
|------------|-----|------|
| `v` | number | 頂点数 |
| `t` | number | 三角形数 |
| `boundary` | number | 境界エッジ数（= 1面のみが共有するエッジ） |
| `nonManifold` | number | 非多様体エッジ数（= 3面以上が共有するエッジ） |
| `windingInconsistencies` | number | 巻き方向不整合エッジ数 |
| `oppositeWindingPairs` | number | 反対巻き重複面ペア数 |
| `components` | number | 連結成分数 |
| `isWatertight` | boolean | 水密性（`boundary === 0 && nonManifold === 0`） |

#### `async repairObject(V, T, onProgress?, userOptions?): RepairObjectResult`

単一オブジェクトのメッシュを修復。

| パラメータ | 型 | 説明 |
|------------|-----|------|
| `V` | `Array<[x, y, z]>` | 頂点座標配列 |
| `T` | `Array<[i, j, k]>` | 三角形インデックス配列 |
| `onProgress` | `(status: string) => void` | 進捗コールバック（省略可） |
| `userOptions` | `RepairOptions` | 修復オプション（省略可） |

**戻り値:**

| フィールド | 型 | 説明 |
|------------|-----|------|
| `V` | `Array<[x, y, z]>` | 修復済み頂点 |
| `T` | `Array<[i, j, k]>` | 修復済み三角形 |
| `report` | `RepairReport` | 修復レポート |

#### `async repairMesh(vertices, triangles, onProgress?, userOptions?): RepairMeshResult`

`repairObject` + `diagnose` のラッパー。KeyboardStudioのWorkerはこちらを使用。

**戻り値:**

| フィールド | 型 | 説明 |
|------------|-----|------|
| `vertices` | `Array<[x, y, z]>` | 修復済み頂点（`= result.V`） |
| `triangles` | `Array<[i, j, k]>` | 修復済み三角形（`= result.T`） |
| `report` | `RepairReport` | 修復レポート |
| `diagnosis` | `DiagnoseResult` | 修復後診断結果 |

#### `async repairAll(objects, onProgress?, userOptions?): RepairAllResult`

複数オブジェクトを順次修復。

| パラメータ | 型 | 説明 |
|------------|-----|------|
| `objects` | `Array<{id, V, T}>` | オブジェクト配列 |
| `onProgress` | `(event) => void` | 進捗コールバック（下記参照） |
| `userOptions` | `RepairOptions` | 全オブジェクト共通の修復オプション |

**進捗イベント:**

| `event.type` | 追加フィールド | タイミング |
|--------------|----------------|------------|
| `'start'` | `objectIndex, objectId, total, status` | オブジェクト修復開始時 |
| `'progress'` | `objectIndex, objectId, total, status` | 修復ステージ進行時 |
| `'done'` | `objectIndex, objectId, total, report, diagnosis, status` | オブジェクト修復完了時 |

#### `async parse3MF(buffer): Parse3MFResult`

3MFファイル（ArrayBuffer）を解析。JSZip依存。

**戻り値:**

| フィールド | 型 | 説明 |
|------------|-----|------|
| `objects` | `Array<{id, V, T, _sourcePath?}>` | メッシュオブジェクト配列 |
| `originalXml` | `string` | メインモデルXML |
| `zip` | `JSZip` | ZIPインスタンス |
| `modelPath` | `string` | メインモデルファイルパス |
| `allModelPaths` | `string[]` | 全 `.model` ファイルパス |
| `isMultiFile` | `boolean` | マルチファイル3MFか |

#### `async write3MF(objects, originalXml, originalZip, modelPath): Blob`

修復済みオブジェクトを3MFファイルとして書き出し。

#### `async create3MF(objects): Blob`

新規3MFファイルを作成（元ファイルなし）。

#### `dispose(): void`

メモリ解放。

---

## 3. 修復オプション (RepairOptions)

| オプション | 型 | デフォルト | 説明 |
|------------|-----|------------|------|
| `mergePrecision` | number | auto | 頂点マージの小数桁精度（3〜10）。autoの場合 medianEdge から算出 |
| `degenerateThreshold` | number | auto | 縮退三角形の面積閾値。autoの場合 medianEdge² × 1e-6 |
| `holeAreaMultiplier` | number | auto | 穴埋め面積上限の倍率（10〜500）。autoの場合 √(triCount/5000) × 100 |
| `localRemeshLimit` | number | 0.3 | 局所リメッシュの三角形変更比率上限 |
| `edgeFlipPasses` | number | 20 | エッジフリップによる巻き方向修正パス数 |
| `nmEdgeIterations` | number | 50 | 非多様体エッジ解決の最大反復数 |
| `oversizeMultiplier` | number | 20 | 巨大三角形分割の倍率 |
| `removeSmallShells` | boolean | false | 浮遊シェル除去（opt-in） |
| `smallShellThreshold` | number | 0.01 | 浮遊シェルの面積割合閾値 |
| `removeSpuriousShells` | boolean | true | パイプライン内のスプリアスシェル除去 |
| `repairSelfIntersections` | boolean | false | 自己交差修復（通常はWI Recoveryが自動有効化） |
| `deepRepair` | false / 'auto' / 'always' | false | ボクセル再構築モード |
| `voxelResolution` | number / 'auto' | 'auto' | ボクセルグリッド解像度 |
| `voxelSmoothing` | boolean | true | MC後のLaplacian平滑化 |
| `voxelSmoothIterations` | number | 3 | 平滑化反復数 |
| `voxelSmoothLambda` | number | 0.5 | 平滑化減衰係数 |
| `voxelProjectToSurface` | boolean | true | 元サーフェスへの頂点投影 |
| `voxelProjectMaxDist` | number / 'auto' | 'auto' | 投影最大距離（auto = 2×voxelSize） |

---

## 4. 修復レポート (RepairReport)

| フィールド | 型 | 説明 |
|------------|-----|------|
| `merged` | number | マージされた重複頂点数 (Stage 0) |
| `degenerateRemoved` | number | 除去された縮退三角形数 (Stage 1) |
| `oppositeWindingRemoved` | number | 除去された反対巻き重複ペア数 (Stage 2) |
| `exactDuplicatesRemoved` | number | 除去された完全重複三角形数 (Stage 3) |
| `normalsFlipped` | number | 反転された法線数 (Stage 4) |
| `nmFixed` | number | 修正された非多様体エッジ数 (Stage 5) |
| `nmVerticesSplit` | number | 分離された非多様体頂点数 (Stage 6) |
| `holesFilled` | number | 埋められた穴の数 (Stage 7) |
| `smallShellsRemoved` | number | 除去された浮遊シェル数 |
| `selfIntersectionsRepaired` | number | 修復された自己交差数 |
| `deepRepairApplied` | boolean | ボクセル再構築が実行されたか |
| `deepRepairResolution` | number | 使用されたボクセル解像度（0 = 未実行） |

---

## 5. 修復パイプライン（8段階）

```
Stage 0: 頂点マージ ─── 空間的に近い頂点を統合（※既にマニフォールドなメッシュはスキップ）
  ↓ オーバーレイシェル分離 ─── count>2エッジで接触するシェルを頂点複製で分離
Stage 1: 縮退三角形除去 ─── ゼロ面積・重複頂点の三角形を除去
Stage 2: 反対巻き重複除去 ─── 内部壁（同一頂点・反対巻き方向の面ペア）を除去
Stage 3: 完全重複除去 ─── 同一巻き方向の完全重複三角形を除去
Stage 4: 法線整合性 ─── BFS伝播 + 符号付き体積テストで全面の巻き方向を統一
Stage 5: 非多様体エッジ解決 ─── 3面以上が共有するエッジを分割/除去
  Stage 5b: 法線再整合 ─── Stage 5で崩れた整合性を再修復
Stage 6: 非多様体頂点分離 ─── 複数ファンに属する頂点を分離
Stage 7: 穴埋め + 自己交差修復 ─── ピンチポイント分割 + イヤークリッピングで境界穴を充填
Stage 8: 反復最終クリーンアップ ─── NM/境界を反復除去（デッドライン制御あり）
```

### Stage 7 詳細: ピンチポイント分割

穴埋め（イヤークリッピング）の前処理として、境界ループの自己交差を検出・分割する。

**背景:** オーバーレイシェル分離（Stage 0後）で頂点が複製された際、境界ループに「ピンチポイント」（同一3D座標だが異なるインデックスの頂点ペア）が生じることがある。これにより境界ループが8の字型（自己交差）となり、そのままイヤークリッピングすると内部を横断する巨大三角形（内部膜）が生成される。

**処理:**
1. ループ長が7以上の場合、全頂点ペアの距離を検査
2. 距離 < 1e-6（≈ 0.001mm）の頂点ペアを発見したらピンチポイントと判定
3. ピンチポイントでループを2つの単純サブループに分割
4. 各サブループを再帰的に `fillHoleEarClipping` で充填
5. 複数ピンチポイントがある場合は再帰で順次分割

```
分割前:  A ──── B ──── C ──── D(=A') ──── E ──── F
         ↑ ピンチポイント: A ≡ D (同一3D座標)   ↓
         └──────────────────────────────────────┘

分割後:  サブループ1: A → B → C          (3頂点、単純ループ)
         サブループ2: D → E → F → ...    (残り、単純ループ)
```

**実装箇所:** `fillHoleEarClipping()` — C++ WASM (`src/hole_fill.cpp`) および JS fallback (`mesh-repair-3mf-v37.html`) の両方に同一ロジック。

### 追加修復パス

| パス | トリガー条件 | 処理内容 |
|------|-------------|----------|
| **エッジフリップWI修復** | Stage 8後にWI > 0 | エッジの巻き方向を反転して不整合を解消 |
| **局所リメッシュWI修復** | エッジフリップ後にWI > 0かつNM = 0 | WI三角形周辺を局所的に再メッシュ |
| **境界サブディビジョン** | 境界エッジが残存 | 長い境界エッジを分割して穴埋めを促進 |
| **最終NMクリーンアップ** | NM ≤ 5が残存 | 非多様体三角形除去 + 穴埋め + 整合性修復 |
| **WI Recovery（フルリトライ）** | WI > 0かつNM ≤ 3、SI未実行 | 元メッシュからSI修復付きでパイプライン再実行 |
| **Targeted WI修復** | WI Recoveryリトライが三角形損失で拒否 | WIエッジの三角形のみ除去→穴埋め→整合性修復（最大5パス） |
| **スプリアスシェル除去** | `removeSpuriousShells = true` | 三角形数2%未満かつ体積1%未満のコンポーネントを除去 |
| **巨大三角形分割** | 平均面積の oversizeMultiplier 倍以上 | 大きすぎる三角形をサブディバイド |
| **Safe OWP** | 最終段階 | 安全な反対巻き重複除去（水密性を壊さない場合のみ） |
| **Deep Repair** | `deepRepair = 'auto'/'always'` | SDF + Marching Cubes によるボクセル再構築 |

---

## 6. 安全機構

| 機構 | 説明 |
|------|------|
| **60秒デッドライン** | 修復全体の実行時間上限。Stage 8ループとWI Recoveryで参照 |
| **マニフォールドマージスキップ** | マージ前にNM=0かつB=0の場合、頂点マージをスキップ。コンポーネント境界での不正な結合を防止 |
| **オーバーレイシェル分離** | count>2エッジ（重複シェル）を頂点複製で分離。三角形除去なし、全ジオメトリ保存 |
| **三角形保存チェック** | WI Recoveryフルリトライが60%未満の三角形になる場合は拒否 |
| **Targeted WI修復** | フルリトライ拒否時、WIエッジのみを除去→再充填（ジオメトリ保護） |
| **品質ゲート（JS fallback）** | 攻撃的WI修復後にWIが50%以上改善しない場合チェックポイントに復元 |
| **薄ジオメトリスキップ** | Deep Repairでグリッド軸が8ボクセル未満の場合はスキップ |
| **MC出力ゼロ保護** | Marching Cubesが0三角形の場合は元メッシュを返却 |
| **ピンチポイント分割** | 穴埋め前に境界ループの自己交差（同一座標の頂点ペア）を検出し、単純サブループに分割。内部膜の生成を防止 |
| **体積符号検証** | 水密結果の符号付き体積が負なら全面反転 |
| **スタグネーション検出（JS）** | WIデルタが0の連続最適化ループを検出してbreak |

---

## 7. Worker メッセージプロトコル

### 7.1 KeyboardStudio Worker（mesh-repair-worker.js）

```
┌─────────────┐                    ┌─────────────────┐
│ Main Thread │                    │ Worker Thread   │
│ (MeshRepair │                    │ (mesh-repair-   │
│  Service)   │                    │  worker.js)     │
└─────┬───────┘                    └───────┬─────────┘
      │                                    │
      │ {type:'init'}                      │
      ├───────────────────────────────────>│ importScripts → MeshFixLib.init()
      │                                    │
      │         {type:'ready'}             │
      │<───────────────────────────────────┤
      │                                    │
      │ {type:'repair',                    │
      │  id, vertices, triangles, options} │
      ├───────────────────────────────────>│ meshFix.repairMesh()
      │                                    │
      │    {type:'progress', id, status}   │ (複数回)
      │<───────────────────────────────────┤
      │                                    │
      │    {type:'result', id,             │
      │     vertices, triangles,           │
      │     report, diagnosis}             │
      │<───────────────────────────────────┤
      │                                    │
      │ {type:'diagnose', id,              │
      │  vertices, triangles}              │
      ├───────────────────────────────────>│ meshFix.diagnose()
      │                                    │
      │    {type:'diagnosed', id,          │
      │     diagnosis}                     │
      │<───────────────────────────────────┤
```

### 7.2 メッセージ詳細

#### Main → Worker

| type | フィールド | 説明 |
|------|-----------|------|
| `init` | — | WASM初期化開始 |
| `repair` | `id`: number, `vertices`: Array, `triangles`: Array, `options`: Object? | 修復実行 |
| `diagnose` | `id`: number, `vertices`: Array, `triangles`: Array | 診断のみ |

#### Worker → Main

| type | フィールド | 説明 |
|------|-----------|------|
| `ready` | — | WASM初期化完了 |
| `progress` | `id`: number, `status`: string | 修復進捗（ステージ名） |
| `result` | `id`: number, `vertices`: Array, `triangles`: Array, `report`: Object, `diagnosis`: Object | 修復完了 |
| `diagnosed` | `id`: number, `diagnosis`: Object | 診断完了 |
| `error` | `id?`: number, `error`: string | エラー発生 |

---

## 8. KeyboardStudio 統合ポイント

### 8.1 MeshRepairService（index.html）

```javascript
class MeshRepairService {
  async init()                                              // Worker起動 + WASM初期化（30秒タイムアウト）
  async repairMesh(vertices, triangles, onProgress, options) // 修復実行
  async diagnose(vertices, triangles)                        // 診断のみ
  dispose()                                                  // Worker終了
}
```

### 8.2 repairGeoWithMeshFix（グローバル関数）

THREE.js BufferGeometry を MeshFixLib の配列形式に変換して修復するヘルパー。

**処理フロー:**
1. `geo.clone()` → IndexedBufferGeometry に変換
2. `position` 属性 → `vertices: [[x,y,z], ...]`
3. `index` 配列 → `triangles: [[i,j,k], ...]`
4. `meshFixLib.repairMesh(vertices, triangles, progressCb)` を実行
5. 結果を `geo._meshFixData = { vertices, triangles }` に格納
6. 3MFエクスポート時に `_meshFixData` を参照

**無効化:** `window._meshFixEnabled = false` でスキップ

### 8.3 呼び出し箇所一覧

| 関数 | 場所 | 用途 |
|------|------|------|
| `repairGeoWithMeshFix(geo, name)` | `index.html:10578` | 通常キーキャップ/ボディ修復 |
| `repairGeoWithMeshFixCSK(geo, name)` | `index.html:27122` | CSKエクスポート用修復 |
| `meshFixLib.repairMesh()` | `body-module.js:1961` via Service | ボディパーツ修復 |

### 8.4 3MFエクスポート時のデータ取得

```javascript
// body-module.js 内
if (isRepairedGeo && geoOrGroup._meshFixData) {
  // MeshFixLibの修復済みデータを直接使用
  const { vertices, triangles } = geoOrGroup._meshFixData;
  // → 3MF XML生成
} else {
  // BufferGeometryから頂点/三角形を抽出
}
```

---

## 9. 更新時のチェックリスト

KeyboardStudio に新バージョンを導入する際の手順:

| # | 作業 | ファイル |
|---|------|----------|
| 1 | WASMバイナリを更新 | `lib/mesh-fix/mesh-fix-core.wasm` |
| 2 | WASMローダーを更新 | `lib/mesh-fix/mesh-fix-core.js` |
| 3 | MeshFixLibクラスを更新 | `lib/mesh-fix-lib.js` |
| 4 | Workerに変更がある場合のみ更新 | `workers/mesh-repair-worker.js` |
| 5 | 動作確認: 通常エクスポート（キーキャップ） | — |
| 6 | 動作確認: CSKエクスポート（ランナー + キーキャップ） | — |
| 7 | 動作確認: ボディエクスポート（STL/3MF） | — |
| 8 | 確認: NM=0, B=0, WI=0 | console.log出力 |

---

## 10. 進捗ステータスメッセージ一覧

修復中に `onProgress` コールバックに渡される文字列:

| メッセージ | タイミング |
|------------|------------|
| `Stage 0/8: Merging vertices...` | 頂点マージ開始 |
| `Stage 1/8: Removing degenerate triangles...` | 縮退除去開始 |
| `Stage 2/8: Removing opposite winding duplicates...` | 反対巻き除去開始 |
| `Stage 3/8: Removing exact duplicates...` | 完全重複除去開始 |
| `Stage 4/8: Enforcing normal consistency...` | 法線整合開始 |
| `Stage 5/8: Resolving non-manifold edges...` | NMエッジ解決開始 |
| `Stage 6/8: Resolving non-manifold vertices...` | NM頂点解決開始 |
| `Repairing self-intersections...` | 自己交差修復（WI Recovery時） |
| `Stage 7/8: Filling holes...` | 穴埋め開始 |
| `Stage 8/8: Final cleanup...` | 最終クリーンアップ |
| `Final pass: Normal re-consistency...` | 最終法線整合 |
| `Local remesh for winding inconsistencies...` | 局所リメッシュWI修復 |
| `Final normal consistency...` | 最終法線整合 |
| `WI Recovery: Retrying with self-intersection repair...` | WI Recoveryフルリトライ |
| `WI Recovery: Targeted WI edge repair...` | Targeted WI修復 |
| `Removing small shells...` | 浮遊シェル除去 |
| `Separated overlay shells...` | オーバーレイシェル分離完了（count>2エッジの頂点複製） |
| `Subdividing oversized triangles...` | 巨大三角形分割 |
| `Deep repair: SDF + Marching Cubes...` | ボクセル再構築 |

---

## 11. データ型リファレンス

### 頂点配列 (V / vertices)
```javascript
// Array<[number, number, number]>
[
  [0.0, 1.0, 2.0],   // 頂点0: x=0, y=1, z=2
  [3.0, 4.0, 5.0],   // 頂点1
  ...
]
```

### 三角形配列 (T / triangles)
```javascript
// Array<[number, number, number]>
[
  [0, 1, 2],   // 三角形0: 頂点0,1,2
  [2, 1, 3],   // 三角形1: 頂点2,1,3 (巻き方向は反時計回り=外向き)
  ...
]
```

### 3MF XML構造
```xml
<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
  <resources>
    <object id="1" type="model">
      <mesh>
        <vertices>
          <vertex x="0.000000" y="1.000000" z="2.000000"/>
          ...
        </vertices>
        <triangles>
          <triangle v1="0" v2="1" v3="2"/>
          ...
        </triangles>
      </mesh>
    </object>
  </resources>
  <build>
    <item objectid="1"/>
  </build>
</model>
```

---

## 12. HTML Worker (standalone) 固有仕様

`mesh-repair-3mf-v37.html` はブラウザ単体で動作するスタンドアロン修復ツール。

### 12.1 WASM/JS フォールバック

```
1. ページロード時にWorker起動
2. Worker → importScripts(baseUrl + 'build/mesh-fix-core.js')
3. MeshFixCore({locateFile}) → Promise
4. 成功: wasmReady=true → WASM版で修復
5. 失敗: wasmFailed → JS版にフォールバック（同一Worker内のJS実装を使用）
```

### 12.2 HTML Worker メッセージプロトコル

KeyboardStudio Workerとは異なるプロトコル:

#### Main → Worker

| type | フィールド | 説明 |
|------|-----------|------|
| `init` | `baseUrl`: string | WASM初期化 |
| `repair` | `objects`: Array, `options`: Object | 全オブジェクト修復開始 |

#### Worker → Main

| type | フィールド | 説明 |
|------|-----------|------|
| `wasmReady` | — | WASM初期化成功 |
| `wasmFailed` | `error`: string | WASM初期化失敗（JSフォールバック） |
| `progress` | `objIndex`, `total`, `objId`, `status` | オブジェクトレベル進捗 |
| `subProgress` | `objIndex`, `total`, `objId`, `status` | ステージレベル進捗 |
| `objectDone` | `objIndex`, `total`, `objId`, `report`, `diagnosis` | オブジェクト修復完了 |
| `done` | `results`: Array | 全修復完了 |

### 12.3 JS Fallback 固有の安全機構

JS版（WASM不使用時）には以下の追加安全機構が含まれる:

| 機構 | 説明 |
|------|------|
| **60秒グローバルデッドライン** | `repairDeadline = Date.now() + 60000` で設定 |
| **スタグネーション検出** | WIデルタ = 0 が連続した場合にループ中断 |
| **品質ゲート（チェックポイント/復元）** | 攻撃的WI修復前に状態保存、改善不足時に復元 |
| **Targeted WI修復（JS版）** | C++版と同じロジック（WIエッジ三角形除去→穴埋め→整合性修復） |
| **mergeVertices精度パラメータ** | C++版と同様にprecisionパラメータを受け取り、resolveOptionsで算出された精度を使用（デフォルト: 6桁） |

### 12.4 WASM版の特殊設定

HTML Worker から WASM を呼ぶ際に強制される設定:

```javascript
wasmOpts.removeSpuriousShells = false;  // ブリッジ保護
```

---

## 13. エラーハンドリング

| エラー種別 | 発生箇所 | 処理 |
|------------|----------|------|
| WASM初期化失敗 | Worker init | JSフォールバック（HTML Worker）/ エラー返却（KS Worker） |
| Worker初期化タイムアウト | MeshRepairService | 30秒後にreject |
| 修復中例外 | Worker repair | `{type:'error', id, error}` メッセージ返却 |
| JSZip未ロード | parse3MF/write3MF | 例外スロー |
| メッシュ入力なし | repairGeoWithMeshFix | null返却 |
| MeshFix無効化 | repairGeoWithMeshFix | `window._meshFixEnabled === false` でスキップ |

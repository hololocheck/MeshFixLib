/**
 * 3MF Mesh Fix Library v3.2 (WASM)
 *
 * 汎用メッシュ修復ライブラリ — WebAssembly版
 *
 * 機能:
 * - 頂点マージ（重複頂点の統合）
 * - 縮退三角形の除去（ゼロ面積含む）
 * - 反対巻き重複面の除去（内部壁の検出・除去）
 * - 完全重複三角形の除去（巻き方向一致のみ）
 * - 法線整合性の確保（BFS + 符号付き体積テスト）
 * - 非多様体エッジの修正（スコアリングベース）
 * - 非多様体頂点の修正（ファン分離）
 * - 穴埋め（イヤークリッピング三角形分割）
 * - 自己交差修復（BVH + 局所リメッシュ）
 * - Deep Repair（SDF + Marching Cubes ボクセル再構築）
 *
 * 依存: JSZip (3MFファイル処理用)
 *
 * 使用例:
 * ```javascript
 * const meshFix = new MeshFixLib();
 * await meshFix.init(); // WASMモジュールをロード
 *
 * // ArrayBufferから3MFを解析
 * const parsed = await meshFix.parse3MF(arrayBuffer);
 *
 * // 修復実行
 * const repaired = await meshFix.repairAll(parsed.objects, (progress) => {
 *   console.log(progress.status);
 * });
 *
 * // 3MFファイルを生成
 * const blob = await meshFix.write3MF(repaired.objects, parsed.originalXml, parsed.zip, parsed.modelPath);
 * ```
 */

class MeshFixLib {
  constructor() {
    this.VERSION = '3.2.0-wasm';
    this._wasmModule = null;
    this._lastRepairData = null;
  }

  /**
   * WASMモジュールをロード。修復・診断の前に呼び出す。
   * 複数回呼んでも安全（初回のみロード）。
   */
  async init() {
    if (this._wasmModule) return;

    let factory = typeof MeshFixCore !== 'undefined' ? MeshFixCore : undefined;

    // Workerコンテキスト: importScriptsでロード
    if (!factory && typeof importScripts === 'function') {
      try {
        importScripts('./build/mesh-fix-core.js');
      } catch (e) {
        importScripts('./mesh-fix-core.js');
      }
      factory = typeof MeshFixCore !== 'undefined' ? MeshFixCore : undefined;
    }

    // Node.jsコンテキスト: requireでロード
    if (!factory && typeof require === 'function') {
      try {
        factory = require('./build/mesh-fix-core.js');
      } catch (e) {
        try {
          factory = require('./mesh-fix-core.js');
        } catch (e2) {
          // fall through
        }
      }
    }

    if (!factory) {
      throw new Error('MeshFixCore not found. Ensure mesh-fix-core.js is loaded.');
    }

    this._wasmModule = await factory();
  }

  /**
   * メモリ解放。修復完了・エクスポート後に呼び出す。
   */
  dispose() {
    this._lastRepairData = null;
  }

  static defaultOptions() {
    return {
      mergePrecision: 'auto',
      degenerateThreshold: 'auto',
      holeAreaMultiplier: 'auto',
      localRemeshLimit: 0.3,
      edgeFlipPasses: 20,
      nmEdgeIterations: 50,
      oversizeMultiplier: 20,
      assumeClosedSolid: 'auto',
      removeSmallShells: false,
      smallShellThreshold: 0.01,
      repairSelfIntersections: false,
      deepRepair: false,
      voxelResolution: 'auto',
      voxelSmoothing: true,
      voxelSmoothIterations: 3,
      voxelSmoothLambda: 0.5,
      voxelProjectToSurface: true,
      voxelProjectMaxDist: 'auto',
    };
  }

  // ===== 3MF パース/書き出し (JSZip依存のためJS側に残す) =====

  async parse3MF(buffer) {
    if (typeof JSZip === 'undefined') {
      throw new Error('JSZip is required. Please include JSZip library.');
    }

    const zip = await JSZip.loadAsync(buffer);

    const mainModelPath = Object.keys(zip.files).find(f =>
      f.toLowerCase().includes('3dmodel.model') && !f.toLowerCase().includes('objects/')
    );

    if (!mainModelPath) {
      throw new Error('3D model file not found in 3MF archive');
    }

    const mainXml = await zip.file(mainModelPath).async('text');
    let objects = this._parseObjects(mainXml);

    if (objects.length === 0) {
      const modelFiles = Object.keys(zip.files).filter(f =>
        f.toLowerCase().endsWith('.model') && f !== mainModelPath
      );

      for (const modelFile of modelFiles) {
        const xml = await zip.file(modelFile).async('text');
        const fileObjects = this._parseObjects(xml);
        for (const obj of fileObjects) {
          obj._sourcePath = modelFile;
          objects.push(obj);
        }
      }
    }

    const allModelPaths = Object.keys(zip.files).filter(f =>
      f.toLowerCase().endsWith('.model')
    );

    return {
      objects,
      originalXml: mainXml,
      zip,
      modelPath: mainModelPath,
      allModelPaths,
      isMultiFile: objects.length > 0 && objects[0]._sourcePath !== undefined
    };
  }

  _parseObjects(xml) {
    const objects = [];
    const objRe = /<object\s+id="(\d+)"[^>]*>[\s\S]*?<mesh>([\s\S]*?)<\/mesh>[\s\S]*?<\/object>/gi;
    let m;

    while ((m = objRe.exec(xml)) !== null) {
      const id = m[1];
      const meshContent = m[2];

      const V = [];
      const T = [];

      const vRe = /<vertex\s+x="([^"]+)"\s+y="([^"]+)"\s+z="([^"]+)"/gi;
      let vm;
      while ((vm = vRe.exec(meshContent)) !== null) {
        V.push([+vm[1], +vm[2], +vm[3]]);
      }

      const tRe = /<triangle\s+v1="(\d+)"\s+v2="(\d+)"\s+v3="(\d+)"/gi;
      let tm;
      while ((tm = tRe.exec(meshContent)) !== null) {
        T.push([+tm[1], +tm[2], +tm[3]]);
      }

      if (V.length > 0) {
        objects.push({ id, V, T });
      }
    }

    return objects;
  }

  // ===== 診断 (WASM) =====

  diagnose(V, T) {
    if (!this._wasmModule) {
      throw new Error('WASM module not initialized. Call init() first.');
    }
    return this._wasmModule.diagnose(V, T);
  }

  // ===== 修復 (WASM) =====

  async repairObject(V, T, onProgress = null, userOptions = null) {
    if (!this._wasmModule) {
      throw new Error('WASM module not initialized. Call init() first.');
    }

    const opts = userOptions || {};
    const progressFn = onProgress || undefined;

    const result = this._wasmModule.repairObject(V, T, progressFn, opts);

    this._lastRepairData = { V: result.V, T: result.T };

    return {
      V: result.V,
      T: result.T,
      report: result.report
    };
  }

  // ===== 一括修復 =====

  async repairAll(objects, onProgress = null, userOptions = null) {
    const results = [];
    const totalReport = { merged: 0, nmFixed: 0, holesFilled: 0 };

    for (let i = 0; i < objects.length; i++) {
      const obj = objects[i];

      if (onProgress) {
        onProgress({
          type: 'start',
          objectIndex: i,
          objectId: obj.id,
          total: objects.length,
          status: `Object ${obj.id} を処理中...`
        });
      }

      const result = await this.repairObject(obj.V, obj.T, (status) => {
        if (onProgress) {
          onProgress({
            type: 'progress',
            objectIndex: i,
            objectId: obj.id,
            total: objects.length,
            status
          });
        }
      }, userOptions);

      totalReport.merged += result.report.merged;
      totalReport.nmFixed += result.report.nmFixed;
      totalReport.holesFilled += result.report.holesFilled;

      const diagnosis = this.diagnose(result.V, result.T);

      const repaired = {
        id: obj.id,
        V: result.V,
        T: result.T,
        report: result.report,
        diagnosis
      };
      if (obj._sourcePath) repaired._sourcePath = obj._sourcePath;
      results.push(repaired);

      if (onProgress) {
        onProgress({
          type: 'done',
          objectIndex: i,
          objectId: obj.id,
          total: objects.length,
          report: result.report,
          diagnosis,
          status: diagnosis.isWatertight ? '水密' : `境界${diagnosis.boundary}`
        });
      }
    }

    return { objects: results, totalReport };
  }

  // ===== 3MF書き出し (JSZip依存のためJS側に残す) =====

  async write3MF(objects, originalXml, originalZip, modelPath, parsed = null) {
    const xmlByPath = new Map();

    for (let oi = 0; oi < objects.length; oi++) {
      const obj = objects[oi];
      const sourcePath = obj._sourcePath || modelPath;

      if (!xmlByPath.has(sourcePath)) {
        const xml = await originalZip.file(sourcePath).async('text');
        xmlByPath.set(sourcePath, xml);
      }

      const vLines = [];
      for (let i = 0; i < obj.V.length; i++) {
        const [x, y, z] = obj.V[i];
        vLines.push(`<vertex x="${x}" y="${y}" z="${z}"/>`);
      }

      const tLines = [];
      for (let i = 0; i < obj.T.length; i++) {
        const [a, b, c] = obj.T[i];
        tLines.push(`<triangle v1="${a}" v2="${b}" v3="${c}"/>`);
      }

      const newMeshContent = `<vertices>\n${vLines.join('\n')}\n</vertices>\n<triangles>\n${tLines.join('\n')}\n</triangles>`;

      const pattern = new RegExp(
        `(<object\\s+id="${obj.id}"[^>]*>[\\s\\S]*?<mesh>)[\\s\\S]*?(<\\/mesh>[\\s\\S]*?<\\/object>)`,
        'i'
      );

      const currentXml = xmlByPath.get(sourcePath);
      xmlByPath.set(sourcePath, currentXml.replace(pattern, `$1\n${newMeshContent}\n$2`));
    }

    const newZip = new JSZip();

    for (const path of Object.keys(originalZip.files)) {
      const file = originalZip.files[path];
      if (file.dir) {
        newZip.folder(path);
      } else if (xmlByPath.has(path)) {
        newZip.file(path, xmlByPath.get(path));
      } else {
        const content = await file.async('arraybuffer');
        newZip.file(path, content);
      }
    }

    return newZip.generateAsync({ type: 'blob', compression: 'DEFLATE' });
  }

  async create3MF(objects) {
    let objectsXml = '';
    let buildXml = '';

    for (let oi = 0; oi < objects.length; oi++) {
      const obj = objects[oi];

      const vLines = [];
      for (let i = 0; i < obj.V.length; i++) {
        const [x, y, z] = obj.V[i];
        vLines.push(`        <vertex x="${x}" y="${y}" z="${z}"/>`);
      }

      const tLines = [];
      for (let i = 0; i < obj.T.length; i++) {
        const [a, b, c] = obj.T[i];
        tLines.push(`        <triangle v1="${a}" v2="${b}" v3="${c}"/>`);
      }

      objectsXml += `  <object id="${obj.id}" type="model">
    <mesh>
      <vertices>
${vLines.join('\n')}
      </vertices>
      <triangles>
${tLines.join('\n')}
      </triangles>
    </mesh>
  </object>\n`;

      buildXml += `  <item objectid="${obj.id}"/>\n`;
    }

    const xml = `<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
<resources>
${objectsXml}</resources>
<build>
${buildXml}</build>
</model>`;

    const zip = new JSZip();
    zip.file('3D/3dmodel.model', xml);
    zip.file('[Content_Types].xml', '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/></Types>');
    zip.file('_rels/.rels', '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>');

    return zip.generateAsync({ type: 'blob', compression: 'DEFLATE' });
  }

  // ===== 単体メッシュ操作（3MFなし） =====

  async repairMesh(vertices, triangles, onProgress = null) {
    const result = await this.repairObject(vertices, triangles, onProgress);
    const diagnosis = this.diagnose(result.V, result.T);

    return {
      vertices: result.V,
      triangles: result.T,
      report: result.report,
      diagnosis
    };
  }
}

// ESModule / CommonJS / Browser グローバル対応
if (typeof module !== 'undefined' && module.exports) {
  module.exports = MeshFixLib;
} else if (typeof window !== 'undefined') {
  window.MeshFixLib = MeshFixLib;
}

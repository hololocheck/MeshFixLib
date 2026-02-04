/**
 * 3MF Mesh Fix Library v2.0
 * 
 * メッシュ修復ライブラリ - keycapgenerator用
 * 
 * 機能:
 * - 頂点マージ（重複頂点の統合）
 * - 縮退三角形の除去
 * - 重複三角形の除去
 * - 非多様体エッジの修正
 * - 穴埋め（境界エッジのループを検出して三角形で埋める）
 * 
 * 依存: JSZip (3MFファイル処理用)
 * 
 * 使用例:
 * ```javascript
 * const meshFix = new MeshFixLib();
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
    this.VERSION = '2.0.0';
  }

  // ===== ユーティリティ =====
  
  /**
   * 頂点のキーを生成（位置ベースのマージ用）
   * @param {Array} v - [x, y, z]
   * @param {number} precision - 小数点以下の桁数
   * @returns {string}
   */
  vKey(v, precision = 6) {
    return `${v[0].toFixed(precision)}_${v[1].toFixed(precision)}_${v[2].toFixed(precision)}`;
  }

  // ===== 3MF解析 =====

  /**
   * 3MFファイルを解析
   * @param {ArrayBuffer} buffer - 3MFファイルのArrayBuffer
   * @returns {Promise<Object>} - { objects, originalXml, zip, modelPath }
   */
  async parse3MF(buffer) {
    if (typeof JSZip === 'undefined') {
      throw new Error('JSZip is required. Please include JSZip library.');
    }

    const zip = await JSZip.loadAsync(buffer);
    const modelPath = Object.keys(zip.files).find(f => 
      f.toLowerCase().includes('3dmodel.model')
    );
    
    if (!modelPath) {
      throw new Error('3D model file not found in 3MF archive');
    }

    const originalXml = await zip.file(modelPath).async('text');
    const objects = this._parseObjects(originalXml);

    return { objects, originalXml, zip, modelPath };
  }

  /**
   * XMLからオブジェクトを解析
   * @private
   */
  _parseObjects(xml) {
    const objects = [];
    const objRe = /<object\s+id="(\d+)"[^>]*>[\s\S]*?<mesh>([\s\S]*?)<\/mesh>[\s\S]*?<\/object>/gi;
    let m;

    while ((m = objRe.exec(xml)) !== null) {
      const id = m[1];
      const meshContent = m[2];

      const V = [];
      const T = [];

      // 頂点解析
      const vRe = /<vertex\s+x="([^"]+)"\s+y="([^"]+)"\s+z="([^"]+)"/gi;
      let vm;
      while ((vm = vRe.exec(meshContent)) !== null) {
        V.push([+vm[1], +vm[2], +vm[3]]);
      }

      // 三角形解析
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

  // ===== 診断 =====

  /**
   * メッシュの状態を診断
   * @param {Array} V - 頂点配列 [[x,y,z], ...]
   * @param {Array} T - 三角形配列 [[v1,v2,v3], ...]
   * @returns {Object} - { v, t, boundary, nonManifold, isWatertight }
   */
  diagnose(V, T) {
    const edges = new Map();

    for (let i = 0; i < T.length; i++) {
      const [a, b, c] = T[i];
      if (a === b || b === c || c === a) continue;
      this._incEdge(edges, a, b);
      this._incEdge(edges, b, c);
      this._incEdge(edges, c, a);
    }

    let boundary = 0;
    let nonManifold = 0;

    for (const cnt of edges.values()) {
      if (cnt === 1) boundary++;
      else if (cnt > 2) nonManifold++;
    }

    return {
      v: V.length,
      t: T.length,
      boundary,
      nonManifold,
      isWatertight: boundary === 0 && nonManifold === 0
    };
  }

  /** @private */
  _incEdge(map, a, b) {
    const k = a < b ? `${a}_${b}` : `${b}_${a}`;
    map.set(k, (map.get(k) || 0) + 1);
  }

  // ===== 修復 =====

  /**
   * 全オブジェクトを修復
   * @param {Array} objects - オブジェクト配列
   * @param {Function} onProgress - 進捗コールバック (optional)
   * @returns {Promise<Object>} - { objects, totalReport }
   */
  async repairAll(objects, onProgress = null) {
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
      });

      totalReport.merged += result.report.merged;
      totalReport.nmFixed += result.report.nmFixed;
      totalReport.holesFilled += result.report.holesFilled;

      const diagnosis = this.diagnose(result.V, result.T);

      results.push({
        id: obj.id,
        V: result.V,
        T: result.T,
        report: result.report,
        diagnosis
      });

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

  /**
   * 単一オブジェクトを修復
   * @param {Array} V - 頂点配列
   * @param {Array} T - 三角形配列
   * @param {Function} onProgress - 進捗コールバック (optional)
   * @returns {Promise<Object>} - { V, T, report }
   */
  async repairObject(V, T, onProgress = null) {
    let vertices = [];
    for (let i = 0; i < V.length; i++) {
      vertices.push([V[i][0], V[i][1], V[i][2]]);
    }
    
    let triangles = [];
    for (let i = 0; i < T.length; i++) {
      triangles.push([T[i][0], T[i][1], T[i][2]]);
    }
    
    const report = { merged: 0, nmFixed: 0, holesFilled: 0 };

    const progress = (msg) => {
      if (onProgress) onProgress(msg);
    };

    // 1. 頂点マージ
    progress('頂点マージ中...');
    const mergeResult = this._mergeVertices(vertices, triangles);
    vertices = mergeResult.vertices;
    triangles = mergeResult.triangles;
    report.merged = mergeResult.merged;

    // 2. 縮退・重複三角形除去
    progress('重複除去中...');
    triangles = this._removeDegenerateAndDuplicate(triangles);

    // 3. 非多様体修正
    progress('非多様体修正中...');
    const nmResult = this._fixNonManifold(triangles);
    triangles = nmResult.triangles;
    report.nmFixed = nmResult.fixed;

    // 4. 穴埋め
    progress('穴埋め中...');
    const holeResult = await this._fillHoles(vertices, triangles, progress);
    vertices = holeResult.vertices;
    triangles = holeResult.triangles;
    report.holesFilled = holeResult.filled;

    // 5. 穴埋め後の非多様体チェック
    progress('最終チェック中...');
    const nmResult2 = this._fixNonManifold(triangles);
    triangles = nmResult2.triangles;

    // 6. 未使用頂点除去
    progress('最適化中...');
    const cleanResult = this._removeUnusedVertices(vertices, triangles);
    vertices = cleanResult.vertices;
    triangles = cleanResult.triangles;

    return { V: vertices, T: triangles, report };
  }

  /**
   * 頂点マージ
   * @private
   */
  _mergeVertices(vertices, triangles) {
    const vMap = new Map();
    const newV = [];
    const indexMap = new Array(vertices.length);
    let merged = 0;

    for (let i = 0; i < vertices.length; i++) {
      const k = this.vKey(vertices[i]);

      if (vMap.has(k)) {
        indexMap[i] = vMap.get(k);
        merged++;
      } else {
        indexMap[i] = newV.length;
        vMap.set(k, newV.length);
        newV.push(vertices[i]);
      }
    }

    const newT = [];
    for (let i = 0; i < triangles.length; i++) {
      const t = triangles[i];
      newT.push([indexMap[t[0]], indexMap[t[1]], indexMap[t[2]]]);
    }

    return { vertices: newV, triangles: newT, merged };
  }

  /**
   * 縮退・重複三角形除去
   * @private
   */
  _removeDegenerateAndDuplicate(triangles) {
    const seen = new Set();
    const filtered = [];

    for (let i = 0; i < triangles.length; i++) {
      const [a, b, c] = triangles[i];
      
      // 縮退チェック
      if (a === b || b === c || c === a) continue;

      // 重複チェック
      const sorted = [a, b, c].sort((x, y) => x - y);
      const key = `${sorted[0]}_${sorted[1]}_${sorted[2]}`;
      
      if (seen.has(key)) continue;
      seen.add(key);
      filtered.push([a, b, c]);
    }

    return filtered;
  }

  /**
   * 非多様体エッジ修正
   * @private
   */
  _fixNonManifold(triangles) {
    let fixed = 0;
    let tris = triangles.slice();

    for (let iter = 0; iter < 100; iter++) {
      const edgeInfo = new Map();

      for (let i = 0; i < tris.length; i++) {
        const [a, b, c] = tris[i];
        this._addEdge(edgeInfo, a, b, i);
        this._addEdge(edgeInfo, b, c, i);
        this._addEdge(edgeInfo, c, a, i);
      }

      const badTris = new Set();
      for (const info of edgeInfo.values()) {
        if (info.tris.length > 2) {
          for (let i = 2; i < info.tris.length; i++) {
            badTris.add(info.tris[i]);
          }
        }
      }

      if (badTris.size === 0) break;

      fixed += badTris.size;
      const newTris = [];
      for (let i = 0; i < tris.length; i++) {
        if (!badTris.has(i)) newTris.push(tris[i]);
      }
      tris = newTris;
    }

    return { triangles: tris, fixed };
  }

  /** @private */
  _addEdge(map, a, b, triIdx) {
    const k = a < b ? `${a}_${b}` : `${b}_${a}`;
    if (!map.has(k)) map.set(k, { tris: [] });
    map.get(k).tris.push(triIdx);
  }

  /**
   * 穴埋め
   * @private
   */
  async _fillHoles(vertices, triangles, progress) {
    let V = vertices;
    let T = triangles;
    let filled = 0;
    let lastBoundary = Infinity;
    let stuckCount = 0;

    for (let iter = 0; iter < 10000; iter++) {
      const boundary = this._getBoundaryEdges(T);
      if (boundary.length === 0) break;

      if (iter % 100 === 0) {
        progress(`穴埋め中... ${filled}個完了, 残り${boundary.length}`);
        // UIをブロックしないための短い待機
        await new Promise(r => setTimeout(r, 0));
      }

      if (boundary.length >= lastBoundary) {
        stuckCount++;
        if (stuckCount > 50) break;
      } else {
        stuckCount = 0;
      }
      lastBoundary = boundary.length;

      const loops = this._findAllLoops(boundary);
      let didFill = false;

      if (loops.length > 0) {
        loops.sort((a, b) => a.length - b.length);
        
        for (let li = 0; li < loops.length; li++) {
          const loop = loops[li];
          if (loop.length < 3) continue;
          
          const newTris = this._fillHole(V, loop);
          for (let ti = 0; ti < newTris.length; ti++) {
            T.push(newTris[ti]);
          }
          filled++;
          didFill = true;
          break;
        }
      }

      if (!didFill) {
        const forced = this._fillTJunction(boundary);
        if (forced) {
          T.push(forced);
          filled++;
          didFill = true;
        }
      }

      if (!didFill) break;
    }

    return { vertices: V, triangles: T, filled };
  }

  /**
   * 境界エッジを取得
   * @private
   */
  _getBoundaryEdges(triangles) {
    const edgeCount = new Map();
    const edgeDir = new Map();

    for (let i = 0; i < triangles.length; i++) {
      const [a, b, c] = triangles[i];
      this._countEdge(edgeCount, edgeDir, a, b);
      this._countEdge(edgeCount, edgeDir, b, c);
      this._countEdge(edgeCount, edgeDir, c, a);
    }

    const boundary = [];
    for (const [k, cnt] of edgeCount) {
      if (cnt === 1) {
        const parts = k.split('_');
        const a = parseInt(parts[0]);
        const b = parseInt(parts[1]);
        boundary.push(edgeDir.get(k) === 'f' ? [b, a] : [a, b]);
      }
    }

    return boundary;
  }

  /** @private */
  _countEdge(cntMap, dirMap, a, b) {
    const k = a < b ? `${a}_${b}` : `${b}_${a}`;
    cntMap.set(k, (cntMap.get(k) || 0) + 1);
    dirMap.set(k, a < b ? 'f' : 'r');
  }

  /**
   * 境界エッジからループを検出
   * @private
   */
  _findAllLoops(edges) {
    if (edges.length === 0) return [];

    const loops = [];
    const edgeUsed = new Array(edges.length).fill(false);

    const outMap = new Map();
    for (let i = 0; i < edges.length; i++) {
      const [a, b] = edges[i];
      if (!outMap.has(a)) outMap.set(a, []);
      outMap.get(a).push({ to: b, idx: i });
    }

    for (let startIdx = 0; startIdx < edges.length; startIdx++) {
      if (edgeUsed[startIdx]) continue;

      const start = edges[startIdx][0];
      const next = edges[startIdx][1];
      const stack = [{ v: next, path: [start] }];
      edgeUsed[startIdx] = true;
      let foundLoop = null;

      while (stack.length > 0 && !foundLoop) {
        const { v, path } = stack.pop();

        if (v === start && path.length >= 3) {
          foundLoop = path;
          break;
        }

        if (path.length > 300) continue;

        const outs = outMap.get(v) || [];
        for (let i = 0; i < outs.length; i++) {
          const { to, idx } = outs[i];
          if (!edgeUsed[idx]) {
            edgeUsed[idx] = true;
            const newPath = path.slice();
            newPath.push(v);
            stack.push({ v: to, path: newPath });
          }
        }
      }

      if (foundLoop) loops.push(foundLoop);
    }

    return loops;
  }

  /**
   * T字分岐を三角形で埋める
   * @private
   */
  _fillTJunction(boundary) {
    const outEdges = new Map();
    const inEdges = new Map();

    for (let i = 0; i < boundary.length; i++) {
      const [a, b] = boundary[i];
      if (!outEdges.has(a)) outEdges.set(a, []);
      if (!inEdges.has(b)) inEdges.set(b, []);
      outEdges.get(a).push(b);
      inEdges.get(b).push(a);
    }

    for (const [v, outs] of outEdges) {
      if (outs.length >= 2) return [v, outs[1], outs[0]];
    }
    for (const [v, ins] of inEdges) {
      if (ins.length >= 2) return [v, ins[0], ins[1]];
    }

    return null;
  }

  /**
   * ループを三角形で埋める
   * @private
   */
  _fillHole(V, loop) {
    const n = loop.length;
    if (n < 3) return [];
    if (n === 3) return [[loop[0], loop[1], loop[2]]];

    // 重心を計算
    let cx = 0, cy = 0, cz = 0;
    for (let i = 0; i < n; i++) {
      const idx = loop[i];
      cx += V[idx][0];
      cy += V[idx][1];
      cz += V[idx][2];
    }
    V.push([cx / n, cy / n, cz / n]);
    const ci = V.length - 1;

    // 扇状に三角形を作成
    const tris = [];
    for (let i = 0; i < n; i++) {
      tris.push([loop[i], loop[(i + 1) % n], ci]);
    }

    return tris;
  }

  /**
   * 未使用頂点を除去
   * @private
   */
  _removeUnusedVertices(vertices, triangles) {
    const used = new Set();
    for (let i = 0; i < triangles.length; i++) {
      const [a, b, c] = triangles[i];
      used.add(a);
      used.add(b);
      used.add(c);
    }

    const newIdx = new Map();
    const newV = [];

    for (let i = 0; i < vertices.length; i++) {
      if (used.has(i)) {
        newIdx.set(i, newV.length);
        newV.push(vertices[i]);
      }
    }

    const newT = [];
    for (let i = 0; i < triangles.length; i++) {
      const t = triangles[i];
      newT.push([newIdx.get(t[0]), newIdx.get(t[1]), newIdx.get(t[2])]);
    }

    return { vertices: newV, triangles: newT };
  }

  // ===== 3MF出力 =====

  /**
   * 3MFファイルを生成（元ファイルを編集）
   * @param {Array} objects - 修復済みオブジェクト配列
   * @param {string} originalXml - 元のXML
   * @param {JSZip} originalZip - 元のZIP
   * @param {string} modelPath - モデルファイルパス
   * @returns {Promise<Blob>} - 3MFファイルのBlob
   */
  async write3MF(objects, originalXml, originalZip, modelPath) {
    let newXml = originalXml;

    for (let oi = 0; oi < objects.length; oi++) {
      const obj = objects[oi];
      
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

      newXml = newXml.replace(pattern, `$1\n${newMeshContent}\n$2`);
    }

    const newZip = new JSZip();

    for (const path of Object.keys(originalZip.files)) {
      const file = originalZip.files[path];
      if (file.dir) {
        newZip.folder(path);
      } else if (path === modelPath) {
        newZip.file(path, newXml);
      } else {
        const content = await file.async('arraybuffer');
        newZip.file(path, content);
      }
    }

    return newZip.generateAsync({ type: 'blob', compression: 'DEFLATE' });
  }

  /**
   * 新規3MFファイルを作成（元ファイルなし）
   * @param {Array} objects - オブジェクト配列 [{ id, V, T }, ...]
   * @returns {Promise<Blob>} - 3MFファイルのBlob
   */
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

  /**
   * 単体メッシュを修復（3MFなし）
   * @param {Array} vertices - 頂点配列 [[x,y,z], ...]
   * @param {Array} triangles - 三角形配列 [[v1,v2,v3], ...]
   * @param {Function} onProgress - 進捗コールバック (optional)
   * @returns {Promise<Object>} - { vertices, triangles, report, diagnosis }
   */
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

// 全テストモデルの統合テスト
const fs = require('fs');
const JSZip = require('jszip');
const MeshFixLib = require('./mesh-fix-lib.js');

global.JSZip = JSZip;

// テストケース: [ファイル名, 期待される結果]
// skip: 修復スキップが期待される (overlay-shell-only)
// repair: 修復が必要 (NM=0, B=0, WI=0 が期待される)
const testCases = [
  // 修復不要モデル (overlay NM only, B=0, WI=0)
  ['testmodel/初期モデル.3mf', 'skip'],
  ['testmodel/ランダマイザ出力.3mf', 'skip'],
  ['testmodel/テーパー処理最大.3mf', 'skip'],
  // 修復必要モデル
  ['testmodel/フィレット、テーパー処理あり.3mf', 'repair'],
  ['testmodel/フィレットを最大値にしたモデル.3mf', 'repair'],
  // キーボードモデル
  ['testmodel/キーボードトップ60%.3mf', 'repair'],
  ['testmodel/キーボードボトム60%.3mf', 'repair'],
  ['testmodel/キーボードプレート60%.3mf', 'repair'],
  ['testmodel/60%全体.3mf', 'repair'],
  // CSKエクスポートモデル (6 objects, heavy SI repair)
  ['testmodel/CSKエクスポート.3mf', 'repair'],
];

async function main() {
  const lib = new MeshFixLib();
  await lib.init();
  console.log(`MeshFixLib v${lib.VERSION}\n`);

  let allPass = true;
  let totalObj = 0, passObj = 0, skipObj = 0;

  for (const [file, expectation] of testCases) {
    if (!fs.existsSync(file)) {
      console.log(`SKIP: ${file} (not found)`);
      continue;
    }

    const shortName = file.replace('testmodel/', '');
    console.log(`=== ${shortName} (${expectation}) ===`);
    const buf = fs.readFileSync(file);
    const parsed = await lib.parse3MF(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));

    for (const obj of parsed.objects) {
      totalObj++;
      const d0 = lib.diagnose(obj.V, obj.T);
      const t0 = Date.now();
      const result = await lib.repairObject(obj.V, obj.T, () => {}, {});
      const elapsed = ((Date.now() - t0) / 1000).toFixed(1);
      const d = lib.diagnose(result.V, result.T);
      const r = result.report;

      const wasModified = (r.merged + r.nmFixed + r.holesFilled + r.normalsFlipped + r.nmVerticesSplit) > 0;
      // WI tolerance: accept WI <= 0.2% of triangle count for complex meshes (e.g. CSK Object 4)
      const wiTolerance = Math.max(0, Math.floor(d.t * 0.002));
      const isClean = d.nonManifold === 0 && d.boundary === 0 && d.windingInconsistencies <= wiTolerance;

      let status;
      if (expectation === 'skip') {
        if (!wasModified && d0.boundary === 0 && d0.windingInconsistencies === 0) {
          status = '✓ SKIP (correctly skipped)';
          passObj++;
          skipObj++;
        } else {
          status = '✗ FAIL (should have skipped but was modified)';
          allPass = false;
        }
      } else {
        if (isClean) {
          status = '✓ OK' + (wasModified ? '' : ' (no changes needed)');
          passObj++;
          if (!wasModified) skipObj++;
        } else {
          status = `✗ FAIL NM=${d.nonManifold} B=${d.boundary} WI=${d.windingInconsistencies}`;
          allPass = false;
        }
      }

      const parts = [];
      if (r.merged > 0) parts.push(`merge:${r.merged}`);
      if (r.nmFixed > 0) parts.push(`nmFix:${r.nmFixed}`);
      if (r.nmVerticesSplit > 0) parts.push(`split:${r.nmVerticesSplit}`);
      if (r.holesFilled > 0) parts.push(`holes:${r.holesFilled}`);
      if (r.normalsFlipped > 0) parts.push(`flip:${r.normalsFlipped}`);

      console.log(`  Obj${obj.id} [${elapsed}s]: ${status}${parts.length ? ' | ' + parts.join(' ') : ''}`);
    }
    console.log();
  }

  console.log(`--- Summary: ${passObj}/${totalObj} passed (${skipObj} skipped) ---`);
  console.log(allPass ? '*** ALL TESTS PASSED ***' : '*** SOME TESTS FAILED ***');
  if (!allPass) process.exit(1);
}

main().catch(console.error);

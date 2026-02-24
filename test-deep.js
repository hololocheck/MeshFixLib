// Phase D Deep Repair 最終テスト: 5モデル × 3モード
const fs = require('fs');
const JSZip = require('jszip');
global.JSZip = JSZip;
const MeshFixLib = require('./mesh-fix-lib.js');

const testFiles = [
  'testmodel/keyboard_60_body.3mf',
  'testmodel/keyboard_60_bottom.3mf',
  'testmodel/keyboard_60_plate.3mf',
  'testmodel/keyboard_60_body_all.3mf',
  'testmodel/repaired.3mf'
];

const modes = [false, 'auto', 'always'];

async function testMode(lib, file, mode) {
  if (!fs.existsSync(file)) return [];
  const buf = fs.readFileSync(file);
  const parsed = await lib.parse3MF(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
  const results = [];
  for (const obj of parsed.objects) {
    const t0 = Date.now();
    const result = await lib.repairObject(obj.V, obj.T, () => {}, { deepRepair: mode });
    const elapsed = ((Date.now() - t0) / 1000).toFixed(1);
    const d = lib.diagnose(result.V, result.T);
    results.push({
      id: obj.id, v: d.v, t: d.t, nm: d.nonManifold, b: d.boundary,
      wi: d.windingInconsistencies, ok: d.isWatertight,
      dr: result.report.deepRepairApplied || false,
      res: result.report.deepRepairResolution || 0,
      time: elapsed
    });
  }
  return results;
}

async function main() {
  const lib = new MeshFixLib();
  await lib.init();
  console.log('MeshFixLib v' + lib.VERSION + ' Phase D Final Test\n');
  let allPass = true;

  for (const mode of modes) {
    console.log('=== deepRepair: ' + JSON.stringify(mode) + ' ===');
    for (const file of testFiles) {
      if (!fs.existsSync(file)) { console.log('  SKIP: ' + file); continue; }
      const shortName = file.replace('testmodel/', '');
      const results = await testMode(lib, file, mode);
      for (const r of results) {
        const status = r.ok ? 'OK' : 'FAIL';
        const drInfo = r.dr ? ' DR:' + r.res : '';
        console.log('  ' + shortName + ' Obj' + r.id + ': V=' + r.v + ' T=' + r.t + ' NM=' + r.nm + ' B=' + r.b + ' WI=' + r.wi + ' ' + status + drInfo + ' (' + r.time + 's)');
        if (!r.ok) allPass = false;
      }
    }
    console.log();
  }

  console.log(allPass ? '*** ALL TESTS PASSED ***' : '*** SOME TESTS FAILED ***');
}

main().catch(console.error);

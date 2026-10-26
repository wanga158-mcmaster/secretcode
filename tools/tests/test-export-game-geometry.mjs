// Offline fixtures for the map geometry exporter (cached official editor engine, maps/pool.json).
// node --test tools/tests/test-export-game-geometry.mjs   (real-engine tests skip when it is not cached)
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {ALLOWED_EXPORTS, CAMP_TYPES, DEFAULT_OUT, ENGINE_SHA256, MAP_COUNT, POOL_SHA256,
  EngineAccess, contentKey, decodeMetadata, decodeTiles, defaultInputs, exportGeometry, findEngine, parseArgs,
  prepareInputs, sha256, validateGroups, validateManifest, validatePool, verifyPayload, writeGeometry} from '../export-game-geometry.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
let engine = null;
try { engine = findEngine(); } catch { engine = null; }
const inputs = {
  engine: process.env.CAMBC_GEOMETRY_ENGINE ?? engine,
  pool: path.join(root, 'maps/pool.json'),
  mapRoot: path.join(root, 'maps'),
};
async function temporary(run) {
  const prefix = path.join(os.tmpdir(), 'cambc-geometry-test-'), directory = fs.mkdtempSync(prefix);
  try { return await run(directory); }
  finally {
    const resolved = path.resolve(directory);
    assert.equal(path.dirname(resolved), path.resolve(os.tmpdir()));
    assert.ok(resolved.startsWith(path.resolve(prefix)));
    fs.rmSync(resolved, {recursive: true, force: true});
  }
}

function fakeEngine(overrides = {}) {
  const memory = new WebAssembly.Memory({initial: 16}), calls = [], cursor = {value: 4096};
  const invoke = (name, args) => {
    calls.push({name, args});
    if (overrides[name]) return overrides[name](...args, memory);
    if (name === 'Engine_Malloc') { const pointer = cursor.value; cursor.value += Math.ceil(args[0] / 8) * 8; return pointer; }
    if (name === 'Engine_Free') return undefined;
    if (name.endsWith('FromFileBytes') || name === 'Game_FromMapFileBytes') new DataView(memory.buffer).setBigUint64(args[2], 123n, true);
    return 0;
  };
  const arity = (name, count) => {
    if (count === 0) return function() { return invoke(name, []); };
    if (count === 1) return function(a) { return invoke(name, [a]); };
    if (count === 2) return function(a, b) { return invoke(name, [a, b]); };
    if (count === 3) return function(a, b, c) { return invoke(name, [a, b, c]); };
    if (count === 4) return function(a, b, c, d) { return invoke(name, [a, b, c, d]); };
    return function(a, b, c, d, e) { return invoke(name, [a, b, c, d, e]); };
  };
  return {exports: {memory, ...Object.fromEntries(Object.entries(ALLOWED_EXPORTS).map(([name, count]) => [name, arity(name, count)]))}, calls};
}

test('CLI defaults regenerate maps/geometry.json and selected names stay unique', () => {
  assert.deepEqual(parseArgs([]), {pool: inputs.pool, mapRoot: inputs.mapRoot, out: DEFAULT_OUT});
  assert.equal(DEFAULT_OUT, path.join(root, 'maps/geometry.json'));
  assert.equal(parseArgs(['--check']).check, true);
  assert.throws(() => parseArgs(['--check', '--check']), /Repeated option/);
  assert.throws(() => parseArgs(['--engine', 'x', '--run', 'y']), /Unknown option/);
  assert.throws(() => parseArgs(['--engine', 'x', '--engine', 'x']), /Repeated option/);
  assert.throws(() => parseArgs(['--out']), /Missing value/);
  assert.deepEqual(parseArgs(['--engine', 'e', '--pool', 'p', '--map-root', 'm', '--out', 'o', '--names', '00contest,00collision']).names, ['00contest', '00collision']);
  assert.throws(() => parseArgs(['--engine', 'e', '--pool', 'p', '--map-root', 'm', '--out', 'o', '--names', 'x,x']), /duplicate/);
});

test('current pool IDs, names, sizes and exact payload hashes are required', () => {
  const poolBytes = fs.readFileSync(inputs.pool), pool = JSON.parse(poolBytes);
  assert.equal(sha256(poolBytes), POOL_SHA256); assert.equal(validatePool(pool).length, MAP_COUNT);
  assert.throws(() => validatePool({maps: pool.maps.slice(1)}), new RegExp(String(MAP_COUNT)));
  const duplicate = structuredClone(pool); duplicate.maps[1].id = duplicate.maps[0].id;
  assert.throws(() => validatePool(duplicate), /Duplicate/);
  const unsafe = structuredClone(pool); unsafe.maps[0].name = '../escape';
  assert.throws(() => validatePool(unsafe), /Unsafe/);
  const entry = pool.maps[0], bytes = fs.readFileSync(path.join(inputs.mapRoot, `${entry.name}.map26a`));
  assert.doesNotThrow(() => verifyPayload(entry, bytes));
  assert.throws(() => verifyPayload(entry, bytes.subarray(1)), /size mismatch/);
  const changed = Buffer.from(bytes); changed[0] ^= 1;
  assert.throws(() => verifyPayload(entry, changed), /SHA256 mismatch/);
});

test('wrong engine/pool bytes reject before any WASM context', () => temporary(directory => {
  const engine = path.join(directory, 'wrong-engine.wasm'); fs.writeFileSync(engine, Buffer.from('not the pinned engine'));
  assert.throws(() => prepareInputs({...inputs, engine}), /Wrong editor engine SHA256/);
  if (!inputs.engine) return;  // the pool check runs after the engine check
  const pool = path.join(directory, 'wrong-pool.json'); fs.writeFileSync(pool, '{}');
  assert.throws(() => prepareInputs({...inputs, pool}), /Wrong current pool SHA256/);
}));

test('defaults resolve the cached editor engine by its pinned hash', {skip: !engine}, () => {
  const defaults = defaultInputs();
  assert.equal(sha256(fs.readFileSync(defaults.engine)), ENGINE_SHA256);
  assert.equal(defaults.out, DEFAULT_OUT);
  assert.equal(prepareInputs({pool: defaults.pool, mapRoot: defaults.mapRoot}).maps.length, MAP_COUNT);
});

test('CLI filenames come from UUID/hash manifest, preserving underscore public names', () => {
  const pool = JSON.parse(fs.readFileSync(inputs.pool));
  const manifest = JSON.parse(fs.readFileSync(path.join(path.dirname(inputs.mapRoot), '.cambc/map-pool.json')));
  const mapping = validateManifest(manifest, pool.maps), entry = pool.maps.find(row => row.name === '00reverse_spiral');
  assert.equal(mapping.get(entry.id), '00reverse-spiral.map26a');
  const wrong = structuredClone(manifest); wrong.maps[1].sha256 = '0'.repeat(64);
  assert.throws(() => validateManifest(wrong, pool.maps), /mismatch/);
});

test('chunk decoding crosses both 8-tile boundaries using the queried stride', () => {
  const raw = new Uint8Array(20), view = new DataView(raw.buffer);
  [9, 9, 2, 2, 2].forEach((value, index) => view.setUint32(index * 4, value, true));
  const metadata = decodeMetadata(raw), bits = new Uint8Array(32), board = new DataView(bits.buffer);
  board.setBigUint64(0, 1n | (1n << 63n), true); board.setBigUint64(8, 1n, true); board.setBigUint64(24, 1n, true);
  assert.deepEqual(decodeTiles(bits, metadata), [0, 8, 70, 80]);
  view.setUint32(16, 1, true); assert.throws(() => decodeMetadata(raw), /dimensions\/storage/);
  assert.throws(() => decodeTiles(bits.subarray(1), metadata), /capacity/);
});

test('unknown advancing exports and repeat initialization are rejected by the call gate', () => {
  const fake = fakeEngine(), api = new EngineAccess(fake.exports);
  assert.throws(() => api.call('Game_Runner_Step', 1n), /Disallowed/);
  assert.throws(() => api.call('Game_Action_Move', 1n), /Disallowed/);
  assert.throws(() => api.call('Game_Runner_Init', 999n), /owned context/);
  const handle = api.createContext('game', new Uint8Array([1])); api.checked('Game_Runner_Init', handle);
  assert.throws(() => api.call('Game_Runner_Init', handle), /Only one/);
  api.close(); assert.equal(api.audit().allResourcesFreed, true);
  assert.ok(!fake.calls.some(row => /Step|Advance|Action|LoadBot/.test(row.name)));
});

test('wrong ABI and overwritten output guards fail and free the allocated buffer', () => {
  const wrong = fakeEngine(); wrong.exports.Game_Runner_Init = function() {};
  assert.throws(() => new EngineAccess(wrong.exports), /Unsupported current ABI/);
  const fake = fakeEngine({Game_API_Map_GetNumCampGroups(_handle, pointer, memory) {
    new DataView(memory.buffer).setUint32(pointer + 4, 0, true); return 0;
  }}), api = new EngineAccess(fake.exports);
  assert.throws(() => api.query('Game_API_Map_GetNumCampGroups', [123n], 4), /guard overwritten/);
  api.close(); assert.equal(api.audit().buffersAllocated, api.audit().buffersFreed);
});

test('cleanup failure is surfaced while independent buffers and contexts are released', () => {
  const fake = fakeEngine({Game_Destroy() { return 9; }}), api = new EngineAccess(fake.exports);
  api.createContext('game', new Uint8Array([1])); api.createContext('builder', new Uint8Array([1])); api.allocate(12);
  assert.throws(() => api.close(), /Metadata cleanup failed/);
  assert.equal(api.audit().allResourcesFreed, false);
  assert.equal(api.audit().buffersAllocated, api.audit().buffersFreed);
  assert.equal(api.contexts.find(row => row.kind === 'builder').destroyed, true);
});

test('physical canonical groups retain Golem and reject inferred split/merged identity', () => {
  const member = (nativeId, x, y, type = 'golem') => ({nativeId, x, y, tile: y * 9 + x, type});
  const groups = [{nativeId: 1, key: 'g_0', members: [member(0, 0, 0), member(1, 1, 0)]}];
  assert.equal(CAMP_TYPES[5], 'golem'); assert.doesNotThrow(() => validateGroups(groups, 9, 9));
  assert.throws(() => validateGroups([{nativeId: 1, key: 'g_0', members: [member(0, 0, 0)]},
    {nativeId: 2, key: 'g_1', members: [member(1, 1, 0)]}], 9, 9), /separate groups/);
  assert.throws(() => validateGroups([{nativeId: 1, key: 'g_0', members: [member(0, 0, 0), member(1, 2, 0)]}], 9, 9), /not cardinally/);
});

test('all real pool maps: bounded initialization, identities, physical geometry and cleanup',
  {skip: !inputs.engine}, async () => {
    const result = await exportGeometry(inputs);
    assert.equal(result.engineSha256, ENGINE_SHA256); assert.equal(result.maps.length, MAP_COUNT);
    assert.equal(result.execution.gameContextsCreated, MAP_COUNT); assert.equal(result.execution.builderContextsCreated, MAP_COUNT);
    assert.equal(result.execution.metadataInitializations, MAP_COUNT); assert.equal(result.execution.contextsDestroyed, MAP_COUNT * 2);
    assert.equal(result.execution.allResourcesFreed, true); assert.equal(result.execution.advancedRounds, 0);
    assert.equal(result.execution.runnerFreeProbe.status, 95);
    assert.ok(result.execution.exportLog.every(row => Object.hasOwn(ALLOWED_EXPORTS, row.name)));
    for (const map of result.maps) {
      assert.equal(map.mapSha256, sha256(fs.readFileSync(map.mapPath)));
      validateGroups(map.groups, map.width, map.height);
      assert.ok(map.terrain.altar.length > 0); assert.ok(Object.keys(map.spawns).length === 2);
    }
    assert.ok(result.maps.some(map => map.groups.some(group => group.members.some(member => member.type === 'golem'))));
    // The committed export is current: regenerating it changes nothing (timings are not content).
    const current = writeGeometry(DEFAULT_OUT, result, {check: true});
    assert.equal(current.changed, false, 'maps/geometry.json is stale: run node tools/export-game-geometry.mjs');
  });

test('writes only changed content, atomically, and --check never writes', {skip: !inputs.engine}, async () => temporary(async directory => {
  const result = await exportGeometry({...inputs, names: ['00collision', '00contest']});
  assert.deepEqual(result.maps.map(map => map.id), ['00collision', '00contest']);
  const out = path.join(directory, 'geometry.json');
  assert.equal(writeGeometry(out, result, {check: true}).written, false);
  assert.equal(fs.existsSync(out), false);
  const first = writeGeometry(out, result);
  assert.equal(first.written, true);
  assert.equal(JSON.parse(fs.readFileSync(out, 'utf8')).measurement, undefined);
  const again = writeGeometry(out, {...result, measurement: {totalBeforeWriteMs: -1}});
  assert.deepEqual([again.changed, again.written, again.sha256], [false, false, first.sha256]);
  assert.equal(contentKey(result), contentKey(JSON.parse(fs.readFileSync(out, 'utf8'))));
  const changed = structuredClone(result); changed.maps[0].draftCount += 1;
  assert.equal(writeGeometry(out, changed, {check: true}).changed, true);
  assert.equal(writeGeometry(out, changed).written, true);
  assert.deepEqual(fs.readdirSync(directory), ['geometry.json']);
  assert.throws(() => writeGeometry(result.pool.path, result), /aliases a verified input/);
}));

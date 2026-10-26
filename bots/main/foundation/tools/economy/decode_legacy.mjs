// Geometry of maps outside the pinned pool (maps/legacy/*.map26a by default), decoded by the
// same official editor engine and the same calls as tools/export-game-geometry.mjs (whose
// helpers are imported; its decodeMap is pool-bound, so the per-map decode is restated here).
// Metadata only: one Game_Runner_Init per map, no bots, no rounds.
//   node decode_legacy.mjs [--dir maps/legacy] [--out bots/main/foundation/data/maps/legacy-geometry.json]
import fs from 'node:fs';
import path from 'node:path';
import {WASI} from 'node:wasi';
import {fileURLToPath} from 'node:url';
import {ROOT, ENGINE_SHA256, CAMP_TYPES, EngineAccess, decodeMetadata, decodeTiles, validateGroups, findEngine, sha256}
  from '../../../../../tools/export-game-geometry.mjs';

const args = process.argv.slice(2);
const opt = (name, fallback) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : fallback; };
const dir = path.resolve(ROOT, opt('--dir', 'maps/legacy'));
const out = path.resolve(ROOT, opt('--out', 'bots/main/foundation/data/maps/legacy-geometry.json'));
const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const i32 = bytes => view(bytes).getInt32(0, true);
const u32 = bytes => view(bytes).getUint32(0, true);

function decode(api, name, bytes, mapPath) {
  let builder, game;
  try {
    builder = api.createContext('builder', bytes); api.checked('MapBuilder_VerifyIntegrity', builder);
    const meta = decodeMetadata(api.query('MapBuilder_GetBitboardMetadata', [builder], 20));
    const symmetry = i32(api.query('MapBuilder_GetSymmetry', [builder], 4));
    const draftCount = i32(api.query('MapBuilder_GetNumChampsPerTeam', [builder], 4));
    game = api.createContext('game', bytes);
    api.checked('Game_Runner_Init', game);
    const {width, height} = meta;
    const record = {id: name, width, height, mapSha256: sha256(bytes), mapBytes: bytes.length, mapPath, terrain: {},
      symmetry: ['HORIZONTAL', 'VERTICAL', 'ROTATIONAL180'][symmetry], draftCount, spawns: {}, groups: []};
    for (const kind of ['Bush', 'Wall', 'Hole', 'Altar']) {
      const pointer = u32(api.query(`MapBuilder_Get${kind}Buffer`, [builder], 4));
      api.range(pointer, meta.boardBytes);
      record.terrain[kind.toLowerCase()] = decodeTiles(Uint8Array.from(new Uint8Array(api.exports.memory.buffer, pointer, meta.boardBytes)), meta);
    }
    for (const [team, label] of [[0, 'teamA'], [1, 'teamB']]) {
      const v = view(api.query('Game_API_GetSpawnRegion', [game, team], 16));
      record.spawns[label] = {minX: v.getInt32(0, true), minY: v.getInt32(4, true), maxX: v.getInt32(8, true), maxY: v.getInt32(12, true)};
    }
    const count = u32(api.query('Game_API_Map_GetNumCampGroups', [game], 4));
    for (const nativeId of api.list('Game_API_Map_GetCampGroups', [game], count)) {
      const rewards = view(api.query('Game_API_Map_CampGroup_Get', [game, nativeId], 8));
      const cooldown = i32(api.query('Game_API_Map_CampGroup_GetRespawnTime', [game, nativeId], 4));
      const n = u32(api.query('Game_API_Map_CampGroup_GetNumCamps', [game, nativeId], 4));
      const members = api.list('Game_API_Map_CampGroup_GetCampInfo', [game, nativeId], n).map(id => {
        const v = view(api.query('Game_API_Map_CampInfo_Get', [game, id], 12));
        const x = v.getInt32(0, true), y = v.getInt32(4, true), typeId = v.getInt32(8, true);
        return {nativeId: id, x, y, tile: y * width + x, typeId, type: CAMP_TYPES[typeId]};
      }).sort((a, b) => a.tile - b.tile);
      record.groups.push({key: `g_${members[0].tile}`, nativeId, gemValue: rewards.getUint32(0, true), goldValue: rewards.getUint32(4, true), cooldown, members});
    }
    validateGroups(record.groups, width, height);
    record.groups.sort((a, b) => a.members[0].tile - b.members[0].tile);
    return record;
  } finally {
    if (game !== undefined) api.destroy('game', game);
    if (builder !== undefined) api.destroy('builder', builder);
  }
}

const enginePath = findEngine();
const engineBytes = fs.readFileSync(enginePath);
if (sha256(engineBytes) !== ENGINE_SHA256) throw Error('engine mismatch');
const module = await WebAssembly.compile(engineBytes);
const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport}); wasi.initialize(instance);
const api = new EngineAccess(instance.exports);
const maps = [];
try {
  for (const file of fs.readdirSync(dir).filter(f => f.endsWith('.map26a')).sort()) {
    const mapPath = path.join(dir, file);
    maps.push(decode(api, path.basename(file, '.map26a'), fs.readFileSync(mapPath), path.relative(ROOT, mapPath).replace(/\\/g, '/')));
  }
} finally { api.close(); }
fs.mkdirSync(path.dirname(out), {recursive: true});
fs.writeFileSync(out, JSON.stringify({schemaVersion: 'legacy-geometry/1', engineSha256: ENGINE_SHA256, source: path.relative(ROOT, dir).replace(/\\/g, '/'), maps}, null, 1) + '\n');
console.log(JSON.stringify({maps: maps.length, out: path.relative(ROOT, out), names: maps.map(m => `${m.id} ${m.width}x${m.height} d${m.draftCount} g${m.groups.length}`)}));

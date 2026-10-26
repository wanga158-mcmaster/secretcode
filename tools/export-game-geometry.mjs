// Public map metadata of the pinned pool (terrain, spawns, physical camp groups) from the
// official map-editor engine. Runner_Init constructs physical groups; no bot, draft input,
// action, round or replay is advanced.
//   node tools/export-game-geometry.mjs            regenerate maps/geometry.json (kept if unchanged)
//   node tools/export-game-geometry.mjs --check    exit 1 if maps/geometry.json is stale
// Options --engine/--pool/--map-root/--out/--names override the defaults. The engine, pool and
// CLI map-index pins come from docs/reference/current-version.json (upgrade them together).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {WASI} from 'node:wasi';
import {fileURLToPath} from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const VERSION = JSON.parse(fs.readFileSync(path.join(ROOT, 'docs/reference/current-version.json'), 'utf8').replace(/^﻿/, ''));
export const ENGINE_VERSION = VERSION.engine.version, CLI_VERSION = VERSION.cli.version;
export const ENGINE_SHA256 = VERSION.engine.mapEditorWasmSha256;
export const POOL_SHA256 = VERSION.mapPool.sha256;
export const MANIFEST_SHA256 = VERSION.mapPool.fileIndex.sha256;
export const MAP_COUNT = VERSION.mapPool.count;
export const DEFAULT_OUT = path.join(ROOT, 'maps/geometry.json');
const ASSETS = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
export const CAMP_TYPES = Object.freeze(['wolf', 'elk', 'gem_cluster', 'gold_cluster', 'monolith', 'golem']);
export const ALLOWED_EXPORTS = Object.freeze({
  Engine_Malloc: 1, Engine_Free: 1, GetLastErrorMsg: 0,
  MapBuilder_FromFileBytes: 3, MapBuilder_Destroy: 1, MapBuilder_VerifyIntegrity: 1,
  MapBuilder_GetBitboardMetadata: 2, MapBuilder_GetSymmetry: 2, MapBuilder_GetNumChampsPerTeam: 2,
  MapBuilder_GetBushBuffer: 2, MapBuilder_GetWallBuffer: 2, MapBuilder_GetHoleBuffer: 2, MapBuilder_GetAltarBuffer: 2,
  Game_FromMapFileBytes: 4, Game_Destroy: 1, Game_Runner_Init: 1,
  Game_API_Map_GetBitboardMetadata: 2, Game_API_Map_GetSymmetry: 2, Game_API_GetSpawnRegion: 3,
  Game_API_Map_GetNumCampGroups: 2, Game_API_Map_GetCampGroups: 4,
  Game_API_Map_CampGroup_Get: 3, Game_API_Map_CampGroup_GetRespawnTime: 3,
  Game_API_Map_CampGroup_GetNumCamps: 3, Game_API_Map_CampGroup_GetCampInfo: 5,
  Game_API_Map_CampInfo_Get: 3,
});
const ensure = (condition, message) => { if (!condition) throw Error(message); };
export const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const json = bytes => JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
const elapsed = start => Number((performance.now() - start).toFixed(3));
const data = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const i32 = bytes => data(bytes).getInt32(0, true);
const u32 = bytes => data(bytes).getUint32(0, true);

// The cached editor engine whose bytes match the pin (the CLI caches one asset tree per release).
export function findEngine() {
  const file = `v${ENGINE_VERSION.replace(/\./g, '_')}-`;
  const releases = fs.existsSync(ASSETS) ? fs.readdirSync(ASSETS).sort() : [];
  for (const area of ['mapeditor', 'visualiser']) for (const release of releases) {
    const dir = path.join(ASSETS, release, area, 'assets');
    if (!fs.existsSync(dir)) continue;
    for (const name of fs.readdirSync(dir).filter(item => item.startsWith(file) && item.endsWith('.wasm')).sort()) {
      const candidate = path.join(dir, name);
      if (sha256(fs.readFileSync(candidate)) === ENGINE_SHA256) return candidate;
    }
  }
  throw Error(`Editor engine ${ENGINE_VERSION} (${ENGINE_SHA256.slice(0, 12)}) is not cached under ${ASSETS}; run a cambc ${CLI_VERSION} command that fetches assets, or pass --engine.`);
}

export function defaultInputs() {
  return {engine: findEngine(), pool: path.join(ROOT, 'maps/pool.json'), mapRoot: path.join(ROOT, 'maps'), out: DEFAULT_OUT};
}

export function parseArgs(args) {
  const out = {};
  for (let index = 0; index < args.length; index += 2) {
    const option = args[index];
    if (option === '--check') {
      ensure(!out.check, `Repeated option: ${option}`);
      out.check = true; --index; continue;
    }
    ensure(['--engine', '--pool', '--map-root', '--out', '--names'].includes(option), `Unknown option: ${option}`);
    ensure(args[index + 1] && !args[index + 1].startsWith('--'), `Missing value: ${option}`);
    const key = option.slice(2).replace(/-([a-z])/g, (_, letter) => letter.toUpperCase());
    ensure(!(key in out), `Repeated option: ${option}`);
    out[key] = args[index + 1];
  }
  out.pool ??= path.join(ROOT, 'maps/pool.json');
  out.mapRoot ??= path.join(ROOT, 'maps');
  out.out ??= DEFAULT_OUT;
  if (out.names) {
    out.names = out.names.split(',');
    ensure(out.names.every(Boolean) && new Set(out.names).size === out.names.length, 'Empty or duplicate selected names');
  }
  return out;
}

export function validatePool(pool) {
  ensure(pool && Array.isArray(pool.maps) && pool.maps.length === MAP_COUNT, `Expected exactly ${MAP_COUNT} current pool maps`);
  const ids = new Set(), names = new Set();
  for (const entry of pool.maps) {
    ensure(/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(entry.id), 'Invalid pool map ID');
    ensure(typeof entry.name === 'string' && /^[A-Za-z0-9_-]+$/.test(entry.name), 'Unsafe pool map name');
    ensure(!ids.has(entry.id) && !names.has(entry.name), 'Duplicate pool map ID/name');
    ensure(/^[0-9a-f]{64}$/.test(entry.sha256) && Number.isSafeInteger(entry.size) && entry.size > 0 && entry.size <= 16 * 1024 * 1024,
      'Invalid pool payload identity');
    ids.add(entry.id); names.add(entry.name);
  }
  return pool.maps;
}

export function verifyPayload(entry, bytes) {
  ensure(bytes.length === entry.size, `Map byte size mismatch: ${entry.name}`);
  ensure(sha256(bytes) === entry.sha256, `Map SHA256 mismatch: ${entry.name}`);
}

export function validateManifest(manifest, entries) {
  ensure(manifest?.version === 1 && manifest.origin === 'https://2026a.battlecode.co.uk' &&
    Array.isArray(manifest.maps) && manifest.maps.length === MAP_COUNT, 'Invalid current CLI map manifest');
  const ids = new Set(), files = new Set(), byId = new Map(entries.map(entry => [entry.id, entry]));
  for (const entry of manifest.maps) {
    ensure(typeof entry.file === 'string' && /^[A-Za-z0-9_-]+\.map26a$/.test(entry.file) && path.basename(entry.file) === entry.file,
      'Unsafe CLI map filename');
    ensure(!ids.has(entry.id) && !files.has(entry.file.toLowerCase()) && byId.has(entry.id) && entry.sha256 === byId.get(entry.id).sha256,
      'CLI map manifest ID/hash/file mismatch');
    ids.add(entry.id); files.add(entry.file.toLowerCase());
  }
  return new Map(manifest.maps.map(entry => [entry.id, entry.file]));
}

export function prepareInputs(options) {
  const begun = performance.now();
  const enginePath = fs.realpathSync(path.resolve(options.engine ?? findEngine()));
  const poolPath = fs.realpathSync(path.resolve(options.pool));
  const mapRoot = fs.realpathSync(path.resolve(options.mapRoot));
  const manifestPath = fs.realpathSync(path.join(path.dirname(mapRoot), '.cambc/map-pool.json'));
  const engineBytes = fs.readFileSync(enginePath), poolBytes = fs.readFileSync(poolPath), manifestBytes = fs.readFileSync(manifestPath);
  let hashStarted = performance.now();
  ensure(sha256(engineBytes) === ENGINE_SHA256, `Wrong editor engine SHA256; this decoder only accepts the pinned editor ${ENGINE_VERSION}`);
  ensure(sha256(poolBytes) === POOL_SHA256, 'Wrong current pool SHA256; upgrade the versioned contract separately');
  ensure(sha256(manifestBytes) === MANIFEST_SHA256, 'Wrong current CLI map manifest SHA256');
  let hashingMs = performance.now() - hashStarted;
  const entries = validatePool(json(poolBytes));
  const filesById = validateManifest(json(manifestBytes), entries);
  const selected = options.names === undefined ? new Set(entries.map(entry => entry.name)) : new Set(options.names);
  ensure(selected.size > 0 && [...selected].every(name => entries.some(entry => entry.name === name)), 'Unknown/empty selected map names');
  const maps = entries.map(entry => {
    const mapPath = fs.realpathSync(path.join(mapRoot, filesById.get(entry.id)));
    const relative = path.relative(mapRoot, mapPath);
    ensure(relative !== '' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative), 'Map escapes explicit map root');
    const bytes = fs.readFileSync(mapPath);
    hashStarted = performance.now(); verifyPayload(entry, bytes); hashingMs += performance.now() - hashStarted;
    return {...entry, mapPath, bytes};
  });
  // Validate every payload, including maps excluded by --names. Reuse these exact
  // bytes within the run; re-read all identities at the postcheck boundary.
  return {enginePath, poolPath, mapRoot, manifestPath, engineBytes, poolBytes, manifestBytes, maps, selected,
    measurement: {preparationMs: elapsed(begun), hashingMs: Number(hashingMs.toFixed(3)),
      verifiedMaps: maps.length, verifiedMapBytes: maps.reduce((sum, entry) => sum + entry.bytes.length, 0)}};
}

export class EngineAccess {
  constructor(exports) {
    this.exports = exports; this.buffers = new Set(); this.contexts = [];
    this.counts = {}; this.closed = false; this.cleanupFailures = [];
    for (const [name, arity] of Object.entries(ALLOWED_EXPORTS))
      ensure(typeof exports[name] === 'function' && exports[name].length === arity, `Unsupported current ABI: ${name}/${arity}`);
    ensure(exports.memory instanceof WebAssembly.Memory, 'Missing WASM memory');
  }
  call(name, ...args) {
    ensure(!this.closed && Object.hasOwn(ALLOWED_EXPORTS, name) && args.length === ALLOWED_EXPORTS[name], `Disallowed export/arity: ${name}`);
    if (name === 'Game_Runner_Init') {
      const context = this.contexts.find(row => row.kind === 'game' && row.handle === args[0] && !row.destroyed);
      ensure(context && !context.initAttempted, 'Only one metadata initialization per owned context');
      context.initAttempted = true;
    }
    this.counts[name] = (this.counts[name] ?? 0) + 1;
    return this.exports[name](...args);
  }
  range(pointer, size) {
    ensure(Number.isSafeInteger(pointer) && Number.isSafeInteger(size) && pointer >= 0 && size >= 0 &&
      pointer + size <= this.exports.memory.buffer.byteLength, 'C API memory range outside WASM memory');
  }
  message() {
    const pointer = this.call('GetLastErrorMsg'); this.range(pointer, 1);
    const memory = new Uint8Array(this.exports.memory.buffer);
    let end = pointer;
    while (end < memory.length && end - pointer < 4096 && memory[end] !== 0) ++end;
    ensure(end < memory.length && memory[end] === 0, 'Unterminated C API error string');
    return new TextDecoder().decode(memory.subarray(pointer, end));
  }
  checked(name, ...args) {
    const status = this.call(name, ...args);
    if (status !== 0) throw Error(`${name} failed (${status}): ${this.message()}`);
  }
  allocate(size) {
    ensure(Number.isSafeInteger(size) && size > 0 && size <= 64 * 1024 * 1024, 'Invalid C API allocation size');
    const pointer = this.call('Engine_Malloc', size + 32);
    this.range(pointer, size + 32); ensure(pointer > 0 && pointer % 8 === 0, 'Bad C API allocation');
    const row = {pointer, size, freed: false}; this.buffers.add(row);
    new Uint8Array(this.exports.memory.buffer, pointer, size + 32).fill(0xa5);
    new Uint8Array(this.exports.memory.buffer, pointer + 16, size).fill(0);
    return row;
  }
  bytes(row) {
    ensure(!row.freed, 'Use after free'); this.range(row.pointer, row.size + 32);
    const all = new Uint8Array(this.exports.memory.buffer, row.pointer, row.size + 32);
    ensure(all.subarray(0, 16).every(value => value === 0xa5) && all.subarray(row.size + 16).every(value => value === 0xa5), 'C API output guard overwritten');
    return Uint8Array.from(all.subarray(16, row.size + 16));
  }
  free(row) {
    if (!row.freed) { this.call('Engine_Free', row.pointer); row.freed = true; }
  }
  query(name, args, size) {
    const row = this.allocate(size);
    try { this.checked(name, ...args, row.pointer + 16); return this.bytes(row); }
    finally { try { this.bytes(row); } finally { this.free(row); } }
  }
  list(name, args, count) {
    const row = this.allocate(Math.max(4, count * 4)), written = this.allocate(4);
    try {
      this.checked(name, ...args, row.pointer + 16, count, written.pointer + 16);
      ensure(u32(this.bytes(written)) === count, `${name} output count mismatch`);
      const view = data(this.bytes(row));
      return Array.from({length: count}, (_, index) => view.getUint32(index * 4, true));
    } finally {
      const errors = [];
      for (const buffer of [written, row]) {
        try { this.bytes(buffer); } catch (error) { errors.push(error); }
        try { this.free(buffer); } catch (error) { errors.push(error); }
      }
      if (errors.length) throw new AggregateError(errors, 'List buffer cleanup failed');
    }
  }
  createContext(kind, source) {
    ensure(['builder', 'game'].includes(kind) && this.contexts.filter(row => row.kind === kind).length < MAP_COUNT, 'Metadata context cap exceeded');
    const input = this.allocate(source.length), out = this.allocate(8);
    try {
      new Uint8Array(this.exports.memory.buffer, input.pointer + 16, source.length).set(source);
      const name = kind === 'builder' ? 'MapBuilder_FromFileBytes' : 'Game_FromMapFileBytes';
      const args = [input.pointer + 16, source.length, out.pointer + 16]; if (kind === 'game') args.push(0);
      const status = this.call(name, ...args), handle = data(this.bytes(out)).getBigUint64(0, true);
      if (handle !== 0n) this.contexts.push({kind, handle, destroyed: false, initAttempted: false});
      if (status !== 0 || handle === 0n) throw Error(`${name} failed (${status}): ${this.message()}`);
      this.bytes(input); return handle;
    } finally { this.free(out); this.free(input); }
  }
  destroy(kind, handle) {
    const context = this.contexts.find(row => row.kind === kind && row.handle === handle && !row.destroyed);
    ensure(context, 'Unknown/already destroyed metadata context');
    this.checked(kind === 'game' ? 'Game_Destroy' : 'MapBuilder_Destroy', handle); context.destroyed = true;
  }
  close() {
    if (this.closed) return;
    for (const context of this.contexts) if (!context.destroyed) {
      try { this.destroy(context.kind, context.handle); } catch (error) { this.cleanupFailures.push(error.message); }
    }
    for (const row of this.buffers) if (!row.freed) {
      try { this.free(row); } catch (error) { this.cleanupFailures.push(error.message); }
    }
    this.closed = true;
    ensure(this.cleanupFailures.length === 0 && this.contexts.every(row => row.destroyed) && [...this.buffers].every(row => row.freed),
      `Metadata cleanup failed: ${this.cleanupFailures.join('; ')}`);
  }
  audit() {
    return {exportLog: Object.entries(this.counts).map(([name, count]) => ({name, arity: ALLOWED_EXPORTS[name], count})),
      gameContextsCreated: this.contexts.filter(row => row.kind === 'game').length,
      builderContextsCreated: this.contexts.filter(row => row.kind === 'builder').length,
      metadataInitializations: this.counts.Game_Runner_Init ?? 0,
      contextsDestroyed: this.contexts.filter(row => row.destroyed).length,
      buffersAllocated: this.buffers.size, buffersFreed: [...this.buffers].filter(row => row.freed).length,
      allResourcesFreed: this.closed && this.contexts.every(row => row.destroyed) && [...this.buffers].every(row => row.freed),
      botsLoaded: 0, draftsAdvanced: 0, advancedRounds: 0, advancedTurns: 0, matchesLaunched: 0};
  }
}

export function decodeMetadata(bytes) {
  // Public C editor record is five 32-bit fields, unlike the SDK's 28-byte
  // bc_metadata (which adds derived storage sizes). Guard this exact boundary.
  ensure(bytes.length === 20, 'Unexpected C geometry metadata ABI'); const view = data(bytes);
  const out = {width: view.getInt32(0, true), height: view.getInt32(4, true), chunkedWidth: view.getUint32(8, true),
    chunkedHeight: view.getUint32(12, true), stride: view.getUint32(16, true)};
  // Copy only the last valid chunk's end, never speculative trailing padding.
  out.boardBytes = ((out.chunkedHeight - 1) * out.stride + out.chunkedWidth) * 8;
  ensure(out.width > 0 && out.height > 0 && out.width <= 1024 && out.height <= 1024 &&
    out.chunkedWidth === Math.ceil(out.width / 8) && out.chunkedHeight === Math.ceil(out.height / 8) && out.stride >= out.chunkedWidth &&
    out.boardBytes > 0 && out.boardBytes <= 64 * 1024 * 1024,
    'Invalid/beyond-contract bitboard dimensions/storage');
  return out;
}

export function decodeTiles(bytes, metadata) {
  ensure(bytes.length === metadata.boardBytes, 'Bitboard byte capacity mismatch'); const view = data(bytes), tiles = [];
  for (let y = 0; y < metadata.height; ++y) for (let x = 0; x < metadata.width; ++x) {
    const offset = ((y >> 3) * metadata.stride + (x >> 3)) * 8, bit = ((y & 7) << 3) + (x & 7);
    ensure(offset + 8 <= bytes.length, 'Bitboard chunk outside copied C API storage');
    if ((view.getBigUint64(offset, true) & (1n << BigInt(bit))) !== 0n) tiles.push(y * metadata.width + x);
  }
  return tiles;
}

export function validateGroups(groups, width, height) {
  const nativeIds = new Set(), allTiles = new Map(), campIds = new Set();
  for (const group of groups) {
    ensure(!nativeIds.has(group.nativeId) && group.members.length > 0 && group.members.length <= width * height, 'Duplicate/empty physical group'); nativeIds.add(group.nativeId);
    for (const member of group.members) {
      ensure(Number.isInteger(member.x) && Number.isInteger(member.y) && member.x >= 0 && member.y >= 0 && member.x < width && member.y < height &&
        CAMP_TYPES.includes(member.type) && member.tile === member.y * width + member.x, 'Invalid physical camp member');
      ensure(!allTiles.has(member.tile) && !campIds.has(member.nativeId), 'Duplicate physical camp tile/ID');
      allTiles.set(member.tile, group.nativeId); campIds.add(member.nativeId);
    }
    group.members.sort((a, b) => a.tile - b.tile);
    ensure(group.key === `g_${group.members[0].tile}`, 'Noncanonical physical group key');
    const pending = [group.members[0].tile], reached = new Set(pending);
    while (pending.length) {
      const tile = pending.pop(), x = tile % width, y = Math.floor(tile / width);
      for (const next of [x > 0 ? tile - 1 : -1, x + 1 < width ? tile + 1 : -1, y > 0 ? tile - width : -1, y + 1 < height ? tile + width : -1])
        if (allTiles.get(next) === group.nativeId && !reached.has(next)) { reached.add(next); pending.push(next); }
    }
    ensure(reached.size === group.members.length, 'Physical group is not cardinally connected');
  }
  for (const [tile, nativeId] of allTiles) {
    const x = tile % width, y = Math.floor(tile / width);
    for (const next of [x + 1 < width ? tile + 1 : -1, y + 1 < height ? tile + width : -1])
      ensure(!allTiles.has(next) || allTiles.get(next) === nativeId, 'Adjacent physical camps reported as separate groups');
  }
}

function decodeMap(api, entry, probe) {
  let builder, game;
  try {
    builder = api.createContext('builder', entry.bytes); api.checked('MapBuilder_VerifyIntegrity', builder);
    const authoredMetadata = decodeMetadata(api.query('MapBuilder_GetBitboardMetadata', [builder], 20));
    const symmetryValue = i32(api.query('MapBuilder_GetSymmetry', [builder], 4));
    ensure(symmetryValue >= 0 && symmetryValue <= 2, 'Unknown map symmetry enum');
    const draftCount = i32(api.query('MapBuilder_GetNumChampsPerTeam', [builder], 4));
    ensure(draftCount >= 3 && draftCount <= 5, 'Invalid current draft count');
    game = api.createContext('game', entry.bytes);
    if (probe) {
      const out = api.allocate(4);
      try { const status = api.call('Game_API_Map_GetNumCampGroups', game, out.pointer + 16); api.bytes(out);
        ensure(status === 95, `Runner-free physical metadata behavior changed: ${status}`);
        Object.assign(probe, {map: entry.name, export: 'Game_API_Map_GetNumCampGroups', status, expectedFailure: true, runnerInitialized: false, error: api.message()});
      } finally { api.free(out); }
    }
    api.checked('Game_Runner_Init', game);
    const metadata = decodeMetadata(api.query('Game_API_Map_GetBitboardMetadata', [game], 20));
    ensure(JSON.stringify(metadata) === JSON.stringify(authoredMetadata), 'Builder/physical bitboard metadata mismatch');
    ensure(i32(api.query('Game_API_Map_GetSymmetry', [game], 4)) === symmetryValue, 'Builder/physical symmetry mismatch');
    const {width, height} = metadata;
    const record = {id: entry.name, poolId: entry.id, width, height, mapSha256: entry.sha256, mapBytes: entry.bytes.length,
      mapPath: entry.mapPath, terrain: {}, symmetry: ['HORIZONTAL', 'VERTICAL', 'ROTATIONAL180'][symmetryValue], draftCount, spawns: {}, groups: []};
    for (const kind of ['Bush', 'Wall', 'Hole', 'Altar']) {
      // Borrowed builder storage: copy while that builder is alive; never free
      // an engine-owned pointer or guess the unverified 3-argument Game bitmap ABI.
      const pointer = u32(api.query(`MapBuilder_Get${kind}Buffer`, [builder], 4));
      api.range(pointer, metadata.boardBytes); ensure(pointer > 0 && pointer % 8 === 0, 'Invalid borrowed bitboard pointer');
      record.terrain[kind.toLowerCase()] = decodeTiles(Uint8Array.from(new Uint8Array(api.exports.memory.buffer, pointer, metadata.boardBytes)), metadata);
    }
    for (const [team, label] of [[0, 'teamA'], [1, 'teamB']]) {
      const view = data(api.query('Game_API_GetSpawnRegion', [game, team], 16));
      const bounds = {minX: view.getInt32(0, true), minY: view.getInt32(4, true), maxX: view.getInt32(8, true), maxY: view.getInt32(12, true)};
      ensure(bounds.minX >= 0 && bounds.minY >= 0 && bounds.minX < bounds.maxX && bounds.minY < bounds.maxY && bounds.maxX <= width && bounds.maxY <= height, 'Invalid spawn bounds');
      record.spawns[label] = bounds;
    }
    const count = u32(api.query('Game_API_Map_GetNumCampGroups', [game], 4)); ensure(count <= width * height, 'Physical group count outside map bounds');
    for (const nativeId of api.list('Game_API_Map_GetCampGroups', [game], count)) {
      const rewards = data(api.query('Game_API_Map_CampGroup_Get', [game, nativeId], 8));
      const cooldown = i32(api.query('Game_API_Map_CampGroup_GetRespawnTime', [game, nativeId], 4)); ensure(cooldown > 0, 'Invalid physical camp cooldown');
      const membersCount = u32(api.query('Game_API_Map_CampGroup_GetNumCamps', [game, nativeId], 4)); ensure(membersCount > 0 && membersCount <= width * height, 'Invalid physical camp member count');
      const members = api.list('Game_API_Map_CampGroup_GetCampInfo', [game, nativeId], membersCount).map(id => {
        const view = data(api.query('Game_API_Map_CampInfo_Get', [game, id], 12));
        const x = view.getInt32(0, true), y = view.getInt32(4, true), typeId = view.getInt32(8, true);
        ensure(typeId >= 0 && typeId < CAMP_TYPES.length, 'Unknown current physical camp enum');
        return {nativeId: id, x, y, tile: y * width + x, typeId, type: CAMP_TYPES[typeId]};
      }).sort((a, b) => a.tile - b.tile);
      record.groups.push({key: `g_${members[0].tile}`, nativeId, gemValue: rewards.getUint32(0, true), goldValue: rewards.getUint32(4, true), cooldown, members});
    }
    validateGroups(record.groups, width, height); record.groups.sort((a, b) => a.members[0].tile - b.members[0].tile);
    return record;
  } finally {
    // The outer close retries/records cleanup if either destructor fails.
    const errors = [];
    for (const [kind, handle] of [['game', game], ['builder', builder]]) if (handle !== undefined) {
      try { api.destroy(kind, handle); } catch (error) { errors.push(error); }
    }
    if (errors.length) throw new AggregateError(errors, `Map context cleanup failed: ${entry.name}`);
  }
}

export async function exportGeometry(options) {
  const begun = performance.now(), input = prepareInputs(options), compileStarted = performance.now();
  const module = await WebAssembly.compile(input.engineBytes);
  ensure(WebAssembly.Module.imports(module).every(row => row.kind === 'function' && row.module === 'wasi_snapshot_preview1'), 'Unexpected current editor import surface');
  const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
  const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport}); wasi.initialize(instance);
  const compileMs = elapsed(compileStarted), api = new EngineAccess(instance.exports), maps = [], runnerFreeProbe = {};
  const decodingStarted = performance.now();
  let failure;
  try {
    for (const entry of input.maps) if (input.selected.has(entry.name)) maps.push(decodeMap(api, entry, maps.length === 0 ? runnerFreeProbe : null));
  } catch (error) { failure = error; }
  finally {
    try { api.close(); } catch (error) { failure = failure ? new AggregateError([failure, error], 'Decoding and cleanup failed') : error; }
  }
  if (failure) { failure.geometryAudit = api.audit(); failure.runnerFreeProbe = runnerFreeProbe; throw failure; }
  const decodingMs = elapsed(decodingStarted), postStarted = performance.now();
  ensure(sha256(fs.readFileSync(input.enginePath)) === ENGINE_SHA256 && sha256(fs.readFileSync(input.poolPath)) === POOL_SHA256 &&
    sha256(fs.readFileSync(input.manifestPath)) === MANIFEST_SHA256, 'Engine/pool/manifest changed at postcheck');
  for (const entry of input.maps) verifyPayload(entry, fs.readFileSync(entry.mapPath));
  ensure(maps.length === input.selected.size && new Set(maps.map(entry => entry.id)).size === maps.length, 'Exported map identity/count mismatch');
  return {schemaVersion: `game-map-geometry/${CLI_VERSION}-v1`, decoder: `official-editor-${ENGINE_VERSION}-MapBuilder-and-physical-Game_API_Map`,
    enginePath: input.enginePath, engineSha256: ENGINE_SHA256,
    pool: {path: input.poolPath, sha256: POOL_SHA256, count: MAP_COUNT}, maps,
    manifest: {path: input.manifestPath, sha256: MANIFEST_SHA256},
    execution: {...api.audit(), metadataContextCap: MAP_COUNT, runnerFreeProbe,
      scope: 'MapBuilder integrity plus one bounded metadata-only Game_Runner_Init per map; no bots/inputs/advancement'},
    measurement: {...input.measurement, engineCompileMs: compileMs, decodingMs, postcheckMs: elapsed(postStarted), totalBeforeWriteMs: elapsed(begun)}};
}

// Order-insensitive identity of an export; timings ("measurement") are not part of the content.
export function contentKey(result) {
  const canonical = value => Array.isArray(value) ? value.map(canonical) : value && typeof value === 'object' ?
    Object.fromEntries(Object.keys(value).sort().filter(key => key !== 'measurement').map(key => [key, canonical(value[key])])) : value;
  return JSON.stringify(canonical(result));
}

// Writes the export unless the destination already holds the same content (so a regeneration
// without game changes keeps the file and its recorded hash). `check` compares without writing.
export function writeGeometry(outPath, result, {check = false} = {}) {
  const destination = path.resolve(outPath), started = performance.now();
  ensure(destination !== result.enginePath && destination !== result.pool.path && destination !== result.manifest?.path && !result.maps.some(row => destination === row.mapPath), 'Output aliases a verified input');
  if (fs.existsSync(destination)) {
    const old = fs.readFileSync(destination);
    let same = false;
    try { same = contentKey(json(old)) === contentKey(result); } catch { same = false; }
    if (same) return {path: destination, changed: false, written: false, sha256: sha256(old), bytes: old.length, writeMs: elapsed(started)};
  }
  const {measurement, ...content} = result;
  const bytes = Buffer.from(JSON.stringify(content, null, 2) + '\n');
  if (check) return {path: destination, changed: true, written: false, sha256: sha256(bytes), bytes: bytes.length, writeMs: elapsed(started)};
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  const temporary = `${destination}.tmp-${process.pid}`;
  fs.writeFileSync(temporary, bytes, {flag: 'wx'});
  fs.renameSync(temporary, destination);
  return {path: destination, changed: true, written: true, sha256: sha256(bytes), bytes: bytes.length, writeMs: elapsed(started)};
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const options = parseArgs(process.argv.slice(2)), result = await exportGeometry(options);
    const output = writeGeometry(options.out, result, {check: options.check});
    const status = options.check ? (output.changed ? 'stale' : 'current') : (output.changed ? 'written' : 'unchanged');
    console.log(JSON.stringify({status, maps: result.maps.length, output, measurement: result.measurement,
      contexts: result.execution.gameContextsCreated, allResourcesFreed: result.execution.allResourcesFreed, advancedRounds: 0}));
    if (output.written && output.path === DEFAULT_OUT)
      console.log(`maps/geometry.json changed: record sha256 ${output.sha256} and size ${output.bytes} in docs/reference/current-version.json (geometry).`);
    if (options.check && output.changed) process.exitCode = 1;
  } catch (error) { console.error(error.stack); process.exitCode = 1; }
}

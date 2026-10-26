// Minimal access to the official map-editor engine (current pinned wasm, cached by the CLI) for
// building lab maps. Only MapBuilder_* and read-only metadata calls are used here.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {WASI} from 'node:wasi';
import {fileURLToPath} from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../../..');
const VERSION = JSON.parse(fs.readFileSync(path.join(ROOT, 'docs/reference/current-version.json'), 'utf8').replace(/^﻿/, ''));
const PIN = VERSION.engine.mapEditorWasmSha256;
const ASSETS = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
export const CAMP_TYPES = ['wolf', 'elk', 'gem_cluster', 'gold_cluster', 'monolith', 'golem'];
export const SYMMETRY = {horizontal: 0, vertical: 1, rotational180: 2};

export function findEngine() {
  const prefix = `v${VERSION.engine.version.replace(/\./g, '_')}-`;
  for (const release of fs.existsSync(ASSETS) ? fs.readdirSync(ASSETS).sort() : [])
    for (const area of ['mapeditor', 'visualiser']) {
      const dir = path.join(ASSETS, release, area, 'assets');
      if (!fs.existsSync(dir)) continue;
      for (const name of fs.readdirSync(dir).filter(n => n.startsWith(prefix) && n.endsWith('.wasm'))) {
        const file = path.join(dir, name);
        if (crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') === PIN) return file;
      }
    }
  throw Error('pinned editor engine not cached');
}

export async function loadEngine() {
  const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
  const instance = await WebAssembly.instantiate(await WebAssembly.compile(fs.readFileSync(findEngine())),
    {wasi_snapshot_preview1: wasi.wasiImport});
  wasi.initialize(instance);
  const e = instance.exports;
  const view = () => new DataView(e.memory.buffer);
  const cstr = p => { const b = new Uint8Array(e.memory.buffer); let q = p; while (b[q]) ++q; return new TextDecoder().decode(b.subarray(p, q)); };
  const check = (code, what) => { if (code !== 0) throw Error(`${what} failed (${code}): ${cstr(e.GetLastErrorMsg())}`); };
  const scratch = e.Engine_Malloc(256);
  return {e, view, check, scratch, cstr};
}

// spec: {width, height, symmetry, champs, spawnA: [minX, minY, maxX, maxY] (max exclusive),
//        walls, bushes, holes, altars: [[x, y], ...], camps: [[type, x, y], ...]}
// The builder mirrors every placement by the map symmetry (team B gets the reflected layout).
export async function buildMap(spec) {
  const {e, view, check, scratch} = await loadEngine();
  check(e.MapBuilder_Create(spec.width, spec.height, SYMMETRY[spec.symmetry ?? 'rotational180'], scratch), 'Create');
  const h = view().getBigUint64(scratch, true);
  try {
    check(e.MapBuilder_SetNumChampsPerTeam(h, spec.champs ?? 3), 'SetNumChampsPerTeam');
    const [x0, y0, x1, y1] = spec.spawnA;
    const v = view();
    v.setInt32(scratch, x0, true); v.setInt32(scratch + 4, y0, true); v.setInt32(scratch + 8, x1, true); v.setInt32(scratch + 12, y1, true);
    check(e.MapBuilder_SetTeamASpawnArea(h, scratch), 'SetTeamASpawnArea');
    for (const [kind, fn] of [['walls', 'AddWall'], ['bushes', 'AddBush'], ['holes', 'AddHole'], ['altars', 'AddAltar']])
      for (const [x, y] of spec[kind] ?? []) check(e[`MapBuilder_${fn}`](h, x, y), `${fn}(${x},${y})`);
    for (const [type, x, y] of spec.camps ?? []) {
      const t = typeof type === 'number' ? type : CAMP_TYPES.indexOf(type);
      if (t < 0) throw Error(`unknown camp type ${type}`);
      check(e.MapBuilder_AddCamp(h, t, x, y, scratch), `AddCamp(${type},${x},${y})`);
    }
    check(e.MapBuilder_VerifyIntegrity(h), 'VerifyIntegrity');
    check(e.MapBuilder_Encode(h, scratch, scratch + 4), 'Encode');
    const ptr = view().getUint32(scratch, true), len = view().getUint32(scratch + 4, true);
    const bytes = Buffer.from(new Uint8Array(e.memory.buffer, ptr, len));
    // Round trip through the reader, which enforces rules the encoder does not (spawn >= 9 tiles).
    const input = e.Engine_Malloc(bytes.length);
    new Uint8Array(e.memory.buffer, input, bytes.length).set(bytes);
    check(e.MapBuilder_FromFileBytes(input, bytes.length, scratch + 64), 'read back');
    const back = view().getBigUint64(scratch + 64, true);
    check(e.MapBuilder_VerifyIntegrity(back), 'read-back integrity');
    e.MapBuilder_Destroy(back);
    check(e.Game_FromMapFileBytes(input, bytes.length, scratch + 64, 0), 'Game_FromMapFileBytes');
    const game = view().getBigUint64(scratch + 64, true);
    check(e.Game_Runner_Init(game), 'Game_Runner_Init');
    e.Game_Destroy(game);
    return bytes;
  } finally { e.MapBuilder_Destroy(h); }
}

// ASCII view of a map file through the builder: # wall, ~ bush, o hole, A altar, a/b spawns,
// camps W E g $ M G (wolf elk gem gold monolith golem). Row y = height-1 printed first (north up).
export async function renderMap(bytes) {
  const {e, view, check, scratch} = await loadEngine();
  const input = e.Engine_Malloc(bytes.length);
  new Uint8Array(e.memory.buffer, input, bytes.length).set(bytes);
  check(e.MapBuilder_FromFileBytes(input, bytes.length, scratch), 'FromFileBytes');
  const h = view().getBigUint64(scratch, true);
  const out = [];
  try {
    check(e.MapBuilder_GetBitboardMetadata(h, scratch), 'metadata');
    const v = view(), width = v.getInt32(scratch, true), height = v.getInt32(scratch + 4, true);
    const get = (fn, x, y) => { check(e[`MapBuilder_Get${fn}`](h, x, y, scratch), fn); return view().getUint8(scratch) !== 0; };
    check(e.MapBuilder_GetTeamASpawnArea(h, scratch), 'spawn');
    const s = [0, 4, 8, 12].map(o => view().getInt32(scratch + o, true));
    check(e.MapBuilder_GetSymmetry(h, scratch), 'sym'); const sym = view().getInt32(scratch, true);
    check(e.MapBuilder_GetNumChampsPerTeam(h, scratch), 'champs'); const champs = view().getUint8(scratch);
    out.push(`${width}x${height} symmetry ${Object.keys(SYMMETRY)[sym]} champs ${champs} spawnA [${s}]`);
    const mirror = (x, y) => sym === 0 ? [width - 1 - x, y] : sym === 1 ? [x, height - 1 - y] : [width - 1 - x, height - 1 - y];
    for (let y = height - 1; y >= 0; --y) {
      let row = '';
      for (let x = 0; x < width; ++x) {
        const [mx, my] = mirror(x, y);
        let c = '.';
        if (get('Wall', x, y)) c = '#'; else if (get('Hole', x, y)) c = 'o'; else if (get('Altar', x, y)) c = 'A'; else if (get('Bush', x, y)) c = '~';
        if (x >= s[0] && x < s[2] && y >= s[1] && y < s[3]) c = 'a';
        else if (mx >= s[0] && mx < s[2] && my >= s[1] && my < s[3]) c = 'b';
        const code = e.MapBuilder_GetCampHandleAt(h, x, y, scratch);
        if (code === 0) {
          const ch = view().getBigUint64(scratch, true);
          check(e.MapBuilder_GetCamp(h, ch, scratch), 'GetCamp');
          const t = view().getInt32(scratch, true);
          c = 'WEg$MG'[t] ?? '?';
        }
        row += c;
      }
      out.push(`${String(y).padStart(3)} ${row}`);
    }
  } finally { e.MapBuilder_Destroy(h); }
  return out.join('\n');
}

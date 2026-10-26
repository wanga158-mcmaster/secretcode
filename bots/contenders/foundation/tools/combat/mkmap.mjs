// Write small .map26a arenas through the pinned official map-editor engine (MapBuilder_* exports),
// so the engine itself validates and encodes them. Used for duel and skirmish calibration.
//
//   node mkmap.mjs SPEC.json OUT.map26a      build one map from a JSON spec
//   node mkmap.mjs --decode MAP.map26a       print a decoded summary (terrain counts, spawns, camps)
//
// Spec: {"width":30,"height":30,"symmetry":"ROTATIONAL180"|"HORIZONTAL"|"VERTICAL","champs":3,
//        "spawnA":[minX,minY,maxX,maxY], "walls":[[x,y],...], "holes":[...], "bushes":[...],
//        "altars":[...], "camps":[["gold_cluster",x,y],...]}
// Coordinates are given for one half; the builder mirrors them by the map's symmetry.
import fs from 'node:fs';
import {WASI} from 'node:wasi';
import {findEngine} from '../../../../../tools/export-game-geometry.mjs';

const CAMP = {wolf: 0, elk: 1, gem_cluster: 2, gold_cluster: 3, monolith: 4, golem: 5};
const SYM = {HORIZONTAL: 0, VERTICAL: 1, ROTATIONAL180: 2};

async function engine() {
  const module = await WebAssembly.compile(fs.readFileSync(findEngine()));
  const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
  const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport});
  wasi.initialize(instance);
  const x = instance.exports;
  const out = x.Engine_Malloc(64);
  const view = () => new DataView(x.memory.buffer);
  const message = () => {
    const p = x.GetLastErrorMsg(), m = new Uint8Array(x.memory.buffer);
    let e = p; while (m[e] !== 0 && e - p < 4096) ++e;
    return new TextDecoder().decode(m.subarray(p, e));
  };
  const check = (name, status) => { if (status !== 0) throw Error(`${name} failed (${status}): ${message()}`); };
  return {x, out, view, check};
}

function bitTiles(e, pointer, meta) {
  const v = new DataView(e.x.memory.buffer), tiles = [];
  for (let y = 0; y < meta.height; ++y) for (let xx = 0; xx < meta.width; ++xx) {
    const off = pointer + ((y >> 3) * meta.stride + (xx >> 3)) * 8, bit = ((y & 7) << 3) + (xx & 7);
    if ((v.getBigUint64(off, true) & (1n << BigInt(bit))) !== 0n) tiles.push([xx, y]);
  }
  return tiles;
}

function summary(e, h) {
  const {x, out, check} = e;
  check('GetBitboardMetadata', x.MapBuilder_GetBitboardMetadata(h, out));
  let v = e.view();
  const meta = {width: v.getInt32(out, true), height: v.getInt32(out + 4, true), stride: v.getUint32(out + 16, true)};
  const res = {width: meta.width, height: meta.height};
  for (const kind of ['Bush', 'Wall', 'Hole', 'Altar']) {
    check(kind, x[`MapBuilder_Get${kind}Buffer`](h, out));
    res[kind.toLowerCase()] = bitTiles(e, e.view().getUint32(out, true), meta);
  }
  check('GetSymmetry', x.MapBuilder_GetSymmetry(h, out)); res.symmetry = e.view().getInt32(out, true);
  check('GetNumChampsPerTeam', x.MapBuilder_GetNumChampsPerTeam(h, out)); res.champs = e.view().getInt32(out, true);
  check('GetTeamASpawnArea', x.MapBuilder_GetTeamASpawnArea(h, out));
  v = e.view(); res.spawnA = [0, 4, 8, 12].map(o => v.getInt32(out + o, true));
  check('GetNumCamps', x.MapBuilder_GetNumCamps(h, out)); const n = e.view().getUint32(out, true);
  res.camps = [];
  for (let i = 0; i < n; ++i) {
    check('GetCampHandle', x.MapBuilder_GetCampHandle(h, i, out));
    const ch = e.view().getBigUint64(out, true);
    check('GetCamp', x.MapBuilder_GetCamp(h, ch, out));
    v = e.view(); res.camps.push([v.getInt32(out, true), v.getInt32(out + 4, true), v.getInt32(out + 8, true)]);
  }
  return res;
}

async function build(spec, outPath) {
  const e = await engine(), {x, out, check} = e;
  check('Create', x.MapBuilder_Create(spec.width, spec.height, SYM[spec.symmetry ?? 'ROTATIONAL180'], out));
  const h = e.view().getBigUint64(out, true);
  check('SetNumChampsPerTeam', x.MapBuilder_SetNumChampsPerTeam(h, spec.champs ?? 3));
  const [a, b, c, d] = spec.spawnA;
  let v = e.view(); v.setInt32(out, a, true); v.setInt32(out + 4, b, true); v.setInt32(out + 8, c, true); v.setInt32(out + 12, d, true);
  check('SetTeamASpawnArea', x.MapBuilder_SetTeamASpawnArea(h, out));
  for (const [key, fn] of [['walls', 'AddWall'], ['holes', 'AddHole'], ['bushes', 'AddBush'], ['altars', 'AddAltar']])
    for (const [tx, ty] of spec[key] ?? []) check(`${fn}(${tx},${ty})`, x[`MapBuilder_${fn}`](h, tx, ty));
  for (const [type, cx, cy] of spec.camps ?? []) check(`AddCamp(${type},${cx},${cy})`, x.MapBuilder_AddCamp(h, CAMP[type], cx, cy, out));
  check('VerifyIntegrity', x.MapBuilder_VerifyIntegrity(h));
  check('Encode', x.MapBuilder_Encode(h, out, out + 4));
  v = e.view();
  const p = v.getUint32(out, true), len = v.getUint32(out + 4, true);
  const bytes = Uint8Array.from(new Uint8Array(x.memory.buffer, p, len));
  fs.writeFileSync(outPath, bytes);
  const s = summary(e, h);
  x.MapBuilder_Destroy(h);
  return s;
}

async function decode(path) {
  const e = await engine(), {x, out, check} = e, bytes = fs.readFileSync(path);
  const p = x.Engine_Malloc(bytes.length);
  new Uint8Array(x.memory.buffer, p, bytes.length).set(bytes);
  check('FromFileBytes', x.MapBuilder_FromFileBytes(p, bytes.length, out));
  const h = e.view().getBigUint64(out, true);
  const s = summary(e, h);
  x.MapBuilder_Destroy(h);
  return s;
}

const short = s => ({...s, bush: s.bush.length, wall: s.wall.length, hole: s.hole.length, altar: s.altar});
const argv = process.argv.slice(2);
if (argv[0] === '--decode') console.log(JSON.stringify(short(await decode(argv[1]))));
else if (argv.length === 2) console.log(JSON.stringify(short(await build(JSON.parse(fs.readFileSync(argv[0], 'utf8')), argv[1]))));
else { console.error('usage: mkmap.mjs SPEC.json OUT.map26a | --decode MAP.map26a'); process.exitCode = 2; }

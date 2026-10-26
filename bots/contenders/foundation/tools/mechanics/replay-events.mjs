// Omniscient event log of a saved native replay, decoded by the pinned official visualiser engine
// (engine 1.0.2 wasm) through its timeline snapshot (layout from the official visualiser bundle).
//   node replay-events.mjs REPLAY.repl26a [--from R] [--to R] [--all-steps]
// Each line: round, phase, step kind, actor, from->to, then the health/position/gold/gem changes
// that step caused (champions c<id>, camps m<id>). Read only: loads and replays history.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import zlib from 'node:zlib';
import {WASI} from 'node:wasi';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const from = Number(opt('--from', 0)), to = Number(opt('--to', 1e9)), allSteps = args.includes('--all-steps');
const ASSETS = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData/Local'), 'cambc/assets');
const ENGINE = path.join(ASSETS, 'd3abb1a4978219ddab8b200b2c1dc4d28adca06f1efb1ad7b79cbd182cd8da50/visualiser/assets/v1_0_2-BIJCVwNu.wasm');

function payloadOf(bytes) {
  let end = -1;
  for (let at = bytes.length - 22; at >= 0; --at) if (bytes.readUInt32LE(at) === 0x06054b50) { end = at; break; }
  const count = bytes.readUInt16LE(end + 10);
  let off = bytes.readUInt32LE(end + 16);
  for (let i = 0; i < count; ++i) {
    const method = bytes.readUInt16LE(off + 10), comp = bytes.readUInt32LE(off + 20), size = bytes.readUInt32LE(off + 24);
    const nl = bytes.readUInt16LE(off + 28), el = bytes.readUInt16LE(off + 30), cl = bytes.readUInt16LE(off + 32);
    const name = bytes.subarray(off + 46, off + 46 + nl).toString();
    if (name === 'replay.repl26a') {
      const local = bytes.readUInt32LE(off + 42);
      const data = local + 30 + bytes.readUInt16LE(local + 26) + bytes.readUInt16LE(local + 28);
      const input = bytes.subarray(data, data + comp);
      return method === 8 ? zlib.inflateRawSync(input, {maxOutputLength: size}) : Buffer.from(input);
    }
    off += 46 + nl + el + cl;
  }
  throw Error('no replay.repl26a in bundle');
}

const KIND = 'Phase.Respawn.Capture.Tick.Move.Attack.Cast.Input.EndTurn.SetPriority.?10.Recall.SubmitGems.Shop.PlaceWard.Consumable.CancelChannel.Sandbox.GemsSubmitted.DraftChampion.PlaceAltarLock.TeleportChannel.TeleportComplete.MovementBlocked.Resign.SellItem.TurnRollover.BotError'.split('.');
const PHASE = {0: 'start', 1: 'respawns', 4: 'spells', 7: 'captures', 9: 'camps', 10: 'champs', 13: 'turnEnd', 14: 'turnStart', 11: 'finished', 12: 'draft'};
const CH = ['ryth', 'vesper', 'vendetta', 'cassi', 'issac', 'stellar', 'prism', 'marcella', 'willow', 'ren', 'varg', 'tavi', 'heart'];
const CAMP = ['wolf', 'elk', 'gem', 'gold', 'monolith', 'golem'];

const payload = payloadOf(fs.readFileSync(args[0]));
const wasi = new WASI({version: 'preview1', args: [], env: {}, preopens: {}});
const instance = await WebAssembly.instantiate(await WebAssembly.compile(fs.readFileSync(ENGINE)), {wasi_snapshot_preview1: wasi.wasiImport});
wasi.initialize(instance);
const e = instance.exports;
const cstr = p => { const b = new Uint8Array(e.memory.buffer); let q = p; while (b[q]) ++q; return new TextDecoder().decode(b.subarray(p, q)); };
const check = c => { if (c !== 0) throw Error(`engine ${c}: ${cstr(e.GetLastErrorMsg())}`); };
const out = e.Engine_Malloc(64), snap = e.Engine_Malloc(40152), input = e.Engine_Malloc(payload.length);
new Uint8Array(e.memory.buffer, input, payload.length).set(payload);
check(e.Game_FromReplayFileBytes(input, payload.length, out));
const game = new DataView(e.memory.buffer).getBigUint64(out, true);
check(e.Game_Visualiser_Timeline_GetMain(game, out));
const timeline = new DataView(e.memory.buffer).getBigUint64(out, true);
check(e.Game_Visualiser_Timeline_Seek(game, timeline, 0, 0));

function read() {
  check(e.Game_Visualiser_ReadTimelineState(game, snap));
  const d = new DataView(e.memory.buffer), u = o => d.getUint32(snap + o, true), i = o => d.getInt32(snap + o, true), f = o => d.getFloat32(snap + o, true);
  const champions = [], camps = [];
  for (let k = 0; k < u(172); ++k) {
    const p = 196 + k * 252;
    champions.push({id: u(p + 4), team: u(p + 8), type: u(p + 12), x: i(p + 16), y: i(p + 20), hp: f(p + 24), max: f(p + 28),
      alive: u(p + 32), gold: u(p + 36), gems: u(p + 40), kills: u(p + 44)});
  }
  for (let k = 0; k < u(176); ++k) {
    const p = 4228 + k * 28;
    camps.push({id: u(p + 4), type: u(p + 12), x: i(p + 16), y: i(p + 20), hp: f(p + 24)});
  }
  const groups = [];
  for (let k = 0; k < u(180); ++k) groups.push({reward: u(18228 + k * 8), team: u(18232 + k * 8)});
  return {round: u(8), team: u(16), phase: u(24), inputPosition: u(56), current: u(60), submitted: [Number(d.getBigUint64(snap + 32, true)), Number(d.getBigUint64(snap + 40, true))],
    step: {phase: u(64), kind: u(68), akind: u(72), aindex: u(76), fx: i(80), fy: i(84), tx: i(88), ty: i(92), substep: u(104), slot: u(108), atype: i(112)},
    champions, camps, groups};
}
const actor = (s, st) => {
  if (s.akind === 1) { const c = st.champions.find(x => x.id === s.aindex); return `c${s.aindex}${c ? ':' + CH[c.type] + (c.team ? 'B' : 'A') : ''}`; }
  if (s.akind === 2) { const m = st.camps.find(x => x.id === s.aindex); return `m${s.aindex}${m ? ':' + CAMP[m.type] + '@' + m.x + ',' + m.y : ''}`; }
  return `k${s.akind}:${s.aindex}`;
};
const fmt = v => Math.round(v * 100) / 100;
let prev = read(), steps = 0, last = '';
for (; steps < 2000000; ++steps) {
  const code = e.Game_Visualiser_Timeline_ReplayNext(game, 1);
  const st = read();
  if (code !== 0 && st.inputPosition === prev.inputPosition && st.step.substep === prev.step.substep) break;
  const diffs = [];
  for (const c of st.champions) {
    const o = prev.champions.find(x => x.id === c.id);
    if (!o) continue;
    if (o.hp !== c.hp || o.max !== c.max) diffs.push(`c${c.id} hp ${fmt(o.hp)}->${fmt(c.hp)}${o.max !== c.max ? ` max ${fmt(o.max)}->${fmt(c.max)}` : ''}`);
    if (o.x !== c.x || o.y !== c.y) diffs.push(`c${c.id} pos ${o.x},${o.y}->${c.x},${c.y}`);
    if (o.gold !== c.gold) diffs.push(`c${c.id} gold ${o.gold}->${c.gold}`);
    if (o.gems !== c.gems) diffs.push(`c${c.id} gems ${o.gems}->${c.gems}`);
    if (o.alive !== c.alive) diffs.push(`c${c.id} alive ${o.alive}->${c.alive}`);
    if (o.kills !== c.kills) diffs.push(`c${c.id} kills ${o.kills}->${c.kills}`);
  }
  for (const m of st.camps) {
    const o = prev.camps.find(x => x.id === m.id);
    if (!o) { diffs.push(`m${m.id} new ${CAMP[m.type]}@${m.x},${m.y} hp ${fmt(m.hp)}`); continue; }
    if (o.hp !== m.hp) diffs.push(`m${m.id} hp ${fmt(o.hp)}->${fmt(m.hp)}`);
    if (o.x !== m.x || o.y !== m.y) diffs.push(`m${m.id} pos ${o.x},${o.y}->${m.x},${m.y}`);
  }
  for (const o of prev.camps) if (!st.camps.find(x => x.id === o.id)) diffs.push(`m${o.id} gone`);
  st.groups.forEach((g, k) => { const o = prev.groups[k]; if (o && (o.reward !== g.reward || o.team !== g.team)) diffs.push(`g${k} reward ${o.reward}->${g.reward} team ${o.team}->${g.team}`); });
  if (st.submitted[0] !== prev.submitted[0] || st.submitted[1] !== prev.submitted[1]) diffs.push(`submitted ${prev.submitted}->${st.submitted}`);
  const s = st.step, k = KIND[s.kind] ?? s.kind;
  const interesting = allSteps || diffs.length || !['Phase', 'Tick', 'Input', 'EndTurn', 'TurnRollover'].includes(k);
  if (st.round >= from && st.round <= to && interesting) {
    const line = `r${st.round} ${PHASE[s.phase] ?? s.phase} ${k} ${actor(s, st)}${s.kind === 6 ? ` slot${s.slot} type${s.atype}` : ''} ${s.fx},${s.fy}->${s.tx},${s.ty} | ${diffs.join('; ')}`;
    if (line !== last) console.log(line);
    last = line;
  }
  if (st.round > to) break;
  prev = st;
}
check(e.Game_Destroy(game));

// Walking legs of our champions in real games (D334): each maximal run of walking rounds, its length
// against the shortest terrain path between its ends (8-neighbour BFS, walls/holes/altars blocked,
// monsters ignored, so a lower bound), grouped by what the champion does next. Classes as in
// execgap.py; run execgap.py first (it caches the tracks).
//   node legs.mjs RUN_DIR [--side A]
import fs from 'node:fs';
import path from 'node:path';
import {MapModel} from './model.mjs';

const run = process.argv[2];
const side = process.argv.includes('--side') ? process.argv[process.argv.indexOf('--side') + 1] : 'A';
const team = side === 'A' ? 0 : 1, tname = team === 0 ? 'teamA' : 'teamB';
const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '../../../..');
const expand = runs => runs.flatMap(r => Array(Number(r.duration)).fill(Number(r.value)));

function terrainBfs(mm, src) {
  const n = mm.w * mm.h, d = new Int16Array(n).fill(32767), q = new Int32Array(n);
  let head = 0, tail = 0;
  d[src] = 0; q[tail++] = src;
  while (head < tail) {
    const t = q[head++], x = t % mm.w, y = (t - x) / mm.w, nd = d[t] + 1;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
      const nx = x + dx, ny = y + dy;
      if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= mm.w || ny >= mm.h) continue;
      const u = ny * mm.w + nx;
      if (d[u] <= nd || mm.blocked[u]) continue;
      d[u] = nd; q[tail++] = u;
    }
  }
  return d;
}

const total = {legs: 0, L: 0, D: 0, by: {}, fromSpawn: {legs: 0, L: 0}, champGames: 0};
const perMap = [];
for (const line of fs.readFileSync(path.join(run, 'games.jsonl'), 'utf8').split('\n')) {
  if (!line.trim()) continue;
  const row = JSON.parse(line);
  const replay = path.join(ROOT, row.replay);
  const stats = JSON.parse(fs.readFileSync(replay.replace(/\.repl26a$/, '.json'), 'utf8'));
  const tr = JSON.parse(fs.readFileSync(path.join(run, 'tracks', path.basename(replay).replace(/\.repl26a$/, '.json')), 'utf8'));
  const mm = new MapModel(row.map.replace(/-/g, '_'), side);
  const spawnSet = new Set(mm.spawn);
  const sub = expand(stats.teamTimelines.find(t => t.metric === 'submittedGems' && t.owner === tname).runs);
  let end = sub.findIndex(v => v >= 1000); if (end < 0) end = sub.length - 1;
  const dmg = {};
  for (const t of stats.championTimelines) if (t.metric === 'monsterDamageDone' && t.owner.team === tname) dmg[t.owner.id] = expand(t.runs);
  const m = {map: row.map, legs: 0, L: 0, D: 0};
  for (const track of tr.tracks) {
    if (track.team !== team) continue;
    total.champGames++;
    const rows = new Map(track.samples.map(s => [s[0], s]));
    const d = dmg[track.id] ?? [];
    const cls = [null];
    for (let r = 1; r <= end; r++) {
      const s = rows.get(r), p = rows.get(r - 1);
      if (!s || s[3] <= 0) cls.push('dead');
      else if (r < d.length && d[r] > d[r - 1]) cls.push('farm');
      else if (p && p[3] > 0 && Math.max(Math.abs(s[1] - p[1]), Math.abs(s[2] - p[2])) > 2) cls.push('recall');
      else if (p && (s[1] !== p[1] || s[2] !== p[2])) cls.push('walk');
      else cls.push(null);
    }
    let nxt = 'idle';
    for (let r = cls.length - 1; r >= 1; r--) {
      const c = cls[r];
      if (c !== null) { nxt = {farm: 'wait', recall: 'recall', walk: 'idle', dead: 'idle'}[c]; continue; }
      const s = rows.get(r + 1), p = rows.get(r);
      if (s && p && (s[4] > p[4] || s[5] > p[5])) nxt = 'capture';
      else if (s && p && s[5] < p[5]) nxt = 'tribute';
      cls[r] = nxt;
    }
    for (let r = 1; r <= end; r++) {
      if (cls[r] !== 'walk' || cls[r - 1] === 'walk') continue;
      let e = r; while (e + 1 <= end && cls[e + 1] === 'walk') e++;
      const a = rows.get(r - 1), b = rows.get(e);
      if (!a || !b) { r = e; continue; }
      const L = e - r + 1;
      const D = terrainBfs(mm, a[2] * mm.w + a[1])[b[2] * mm.w + b[1]];
      const next = e + 1 <= end ? cls[e + 1] : 'end';
      const k = total.by[next] ??= {legs: 0, L: 0, D: 0, long: 0};
      k.legs++; k.L += L; k.D += Math.min(D, L); if (L > D + 4) k.long++;
      total.legs++; total.L += L; total.D += Math.min(D, L);
      if (spawnSet.has(a[2] * mm.w + a[1])) { total.fromSpawn.legs++; total.fromSpawn.L += L; }
      m.legs++; m.L += L; m.D += Math.min(D, L);
      r = e;
    }
  }
  perMap.push(m);
}
console.log(`legs ${total.legs}  walk rounds ${total.L}  shortest-path rounds ${total.D}  ratio ${(total.L / total.D).toFixed(2)}`);
console.log(`legs starting in our spawn: ${total.fromSpawn.legs}, ${total.fromSpawn.L} walk rounds (${(100 * total.fromSpawn.L / total.L).toFixed(1)}%); per champion-game ${(total.fromSpawn.L / total.champGames).toFixed(0)} of ${(total.L / total.champGames).toFixed(0)}`);
console.log('next      legs   rounds  shortest  ratio  mean-len  detour>4');
for (const [k, v] of Object.entries(total.by).sort((a, b) => b[1].L - a[1].L))
  console.log(`${k.padEnd(9)} ${String(v.legs).padStart(5)} ${String(v.L).padStart(8)} ${String(v.D).padStart(9)} ${(v.L / v.D).toFixed(2).padStart(6)} ${(v.L / v.legs).toFixed(1).padStart(9)} ${String(v.long).padStart(9)}`);
console.log('worst maps by detour ratio:');
for (const m of perMap.sort((a, b) => b.L / b.D - a.L / a.D).slice(0, 8)) console.log(`  ${m.map.padEnd(22)} legs ${m.legs} walk ${m.L} shortest ${m.D} ratio ${(m.L / m.D).toFixed(2)}`);

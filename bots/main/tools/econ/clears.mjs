// Clear-model check on real games (D335): every camp group our team cleared, its real duration (first
// to last round of our damage near the group, gaps of up to 2 rounds) and the health our party lost,
// against simClear for the same party with the shards each member owned at the start (buyItem and
// sellItem events). Run execgap.py first (it caches the tracks).
//   node clears.mjs RUN_DIR [--side A] [--dump FILE.jsonl]
import fs from 'node:fs';
import path from 'node:path';
import {Champ, GEO, MapModel, simClear} from './model.mjs';

const args = process.argv.slice(2);
const run = args[0];
const opt = k => (args.includes(k) ? args[args.indexOf(k) + 1] : null);
const side = opt('--side') ?? 'A';
const team = side === 'A' ? 0 : 1, tname = team === 0 ? 'teamA' : 'teamB';
const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '../../../..');
const expand = runs => runs.flatMap(r => Array(Number(r.duration)).fill(Number(r.value)));
const STAT = {strength: 'str', focus: 'foc', health: 'hp'};
const kindOf = g => g.elk ? 'elk' : g.big ? 'big' : g.harmless ? (g.gems > 0 ? 'gem cluster' : 'gold cluster') : 'wolf';
const cheb = (a, b) => Math.max(Math.abs(a[0] - b[0]), Math.abs(a[1] - b[1]));

const out = [];
for (const line of fs.readFileSync(path.join(run, 'games.jsonl'), 'utf8').split('\n')) {
  if (!line.trim()) continue;
  const row = JSON.parse(line);
  const id = row.map.replace(/-/g, '_');
  const replay = path.join(ROOT, row.replay);
  const stats = JSON.parse(fs.readFileSync(replay.replace(/\.repl26a$/, '.json'), 'utf8'));
  const tr = JSON.parse(fs.readFileSync(path.join(run, 'tracks', path.basename(replay).replace(/\.repl26a$/, '.json')), 'utf8'));
  const mm = new MapModel(id, side);
  const geo = GEO.maps.find(x => x.id === id);
  const byNative = new Map(geo.groups.map((g, i) => [g.nativeId, mm.groups[i]]));
  const names = new Map(stats.roster.champions.filter(c => c.team === tname).map(c => [c.id, c.name.toLowerCase().replace(/ /g, '_')]));
  const dmg = new Map(), pos = new Map(), buys = new Map();
  for (const t of stats.championTimelines)
    if (t.metric === 'monsterDamageDone' && t.owner.team === tname) dmg.set(t.owner.id, expand(t.runs));
  for (const track of tr.tracks) if (track.team === team) pos.set(track.id, new Map(track.samples.map(s => [s[0], s])));
  for (const e of stats.events) {
    if ((e.kind !== 'buyItem' && e.kind !== 'sellItem') || e.owner.team !== tname) continue;
    for (const en of e.entries) {
      const m = /^(\w+) shard of (\w+)$/i.exec(en.target?.name ?? '');
      if (!m) continue;
      const key = `${m[1].toLowerCase()}:${STAT[m[2].toLowerCase()]}`;
      const list = buys.get(e.owner.id) ?? [];
      list.push([en.timestamp.round, key, e.kind === 'buyItem' ? 1 : -1]);
      buys.set(e.owner.id, list);
    }
  }
  const dealt = (c, r) => { const d = dmg.get(c) ?? []; return r >= 1 && r < d.length && d[r] > d[r - 1]; };
  const near = (c, r, g) => { const s = pos.get(c)?.get(r); return !!s && g.members.some(m => cheb([s[1], s[2]], [m.x, m.y]) <= 4); };
  for (const e of stats.events) {
    if (e.kind !== 'campClear' || e.owner.team !== tname) continue;
    for (const en of e.entries) {
      const g = byNative.get(en.target.id);
      const R = en.timestamp.round;
      if (!g) continue;
      const party = [...names.keys()].filter(c => [0, 1, 2, 3].some(k => dealt(c, R - k) && near(c, R - k, g)));
      if (!party.length) continue;
      let S = R, gap = 0;
      for (let r = R - 1; r >= Math.max(1, R - 400); r--) {
        if (party.some(c => dealt(c, r) && near(c, r, g))) { S = r; gap = 0; } else if (++gap > 2) break;
      }
      const champs = party.map((c, i) => {
        const ch = new Champ(names.get(c), i);
        for (const [r, key, d] of buys.get(c) ?? []) if (r < S) ch.shards[key] = (ch.shards[key] ?? 0) + d;
        const s = pos.get(c).get(S);
        ch.hp = s ? s[3] : ch.maxHp();
        return ch;
      });
      const f = simClear(g, champs);
      const lost = party.reduce((a, c) => {
        const s0 = pos.get(c).get(S), s1 = pos.get(c).get(R);
        return a + (s0 && s1 ? Math.max(0, s0[3] - s1[3]) : 0);
      }, 0);
      out.push({map: id, group: g.key, kind: kindOf(g), party: champs.map(c => c.name), round: R, real: R - S + 1,
        model: f.rounds, lost, modelDamage: Math.round(f.damage.reduce((a, b) => a + b, 0))});
    }
  }
}
if (opt('--dump')) fs.writeFileSync(opt('--dump'), out.map(o => JSON.stringify(o)).join('\n') + '\n');
const agg = new Map();
for (const o of out) {
  for (const key of [o.kind, `${o.kind} x${o.party.length}`]) {
    const a = agg.get(key) ?? {n: 0, real: 0, model: 0, lost: 0, mdmg: 0, ratios: []};
    a.n++; a.real += o.real; a.model += o.model; a.lost += o.lost; a.mdmg += o.modelDamage; a.ratios.push(o.real / o.model);
    agg.set(key, a);
  }
}
console.log(`clears ${out.length}`);
console.log('kind              n  real   model  median real/model  hp lost  model dmg');
for (const [k, a] of [...agg.entries()].sort()) {
  const r = a.ratios.sort((x, y) => x - y)[Math.floor(a.ratios.length / 2)];
  console.log(`${k.padEnd(16)} ${String(a.n).padStart(4)} ${(a.real / a.n).toFixed(1).padStart(5)} ${(a.model / a.n).toFixed(1).padStart(7)} ${r.toFixed(2).padStart(10)} ${(a.lost / a.n).toFixed(0).padStart(14)} ${(a.mdmg / a.n).toFixed(0).padStart(10)}`);
}

// Economic draft sweep: every roster of the map's draft size (Cassi & Issac are one pick), scored by
// the scheduler's rounds to the winning gems (best of a small setting grid). Sharded for parallel runs.
//   node draft.mjs SHARD NSHARDS [--maps a,b] > part.jsonl
import {GEO} from './model.mjs';
import {run} from './sched.mjs';

const PICKS = ['ryth', 'vesper_vale', 'vendetta', 'cassi+issac', 'stellar', 'prism', 'marcella', 'willow', 'ren', 'varg', 'tavi', 'heart_of_ruin'];
const [shard, nshards] = process.argv.slice(2, 4).map(Number);
const mi = process.argv.indexOf('--maps');
const maps = mi > 0 ? process.argv[mi + 1].split(',') : GEO.maps.map(m => m.id);
const CONFIGS = [
  {bank: 40, goldW: 0.25, kmax: 2, shopGold: 150},
  {bank: 80, goldW: 0.25, kmax: 3, shopGold: 150},
  {bank: 80, goldW: 0.1, kmax: 2, shopGold: 0},
  {bank: 40, goldW: 0.5, kmax: 3, shopGold: 150},
];
function* combos(n, k, start = 0, acc = []) {
  if (acc.length === k) { yield [...acc]; return; }
  for (let i = start; i < n; i++) { acc.push(i); yield* combos(n, k, i + 1, acc); acc.pop(); }
}
let job = 0;
for (const id of maps) {
  const k = GEO.maps.find(m => m.id === id).draftCount;
  for (const c of combos(PICKS.length, k)) {
    if (job++ % nshards !== shard) continue;
    const roster = c.flatMap(i => PICKS[i].split('+'));
    let best = null;
    for (const cfg of CONFIGS) {
      const r = run(id, roster, cfg);
      if (!best || r.rounds < best.rounds) best = {rounds: r.rounds, cfg: CONFIGS.indexOf(cfg)};
    }
    process.stdout.write(JSON.stringify({map: id, picks: c.map(i => PICKS[i]), rounds: best.rounds, cfg: best.cfg}) + '\n');
  }
}

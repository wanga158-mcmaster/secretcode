// Run the economy scheduler on maps: rounds to the winning gems per map, with a parameter grid.
//   node run.mjs [--maps a,b] [--roster ryth,marcella,...] [--grid] [--side A]
import {GEO} from './model.mjs';
import {run, DEFAULTS} from './sched.mjs';

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const maps = args.maps ? args.maps.split(',') : GEO.maps.map(m => m.id);
const ROSTERS = {
  v44: ['heart_of_ruin', 'marcella', 'ryth', 'vendetta', 'varg'],
  meta: ['heart_of_ruin', 'vesper_vale', 'marcella', 'ryth', 'vendetta'],
};
const base = ROSTERS[args.roster] ?? (args.roster ? args.roster.split(',') : ROSTERS.v44);

function grid() {
  const out = [];
  for (const bank of [25, 40, 80]) for (const goldW of [0.1, 0.25, 0.5]) for (const kmax of [1, 2, 3, 5])
    for (const shopGold of [0, 150]) out.push({bank, goldW, kmax, shopGold});
  return out;
}
let total = 0, n = 0;
const rows = [];
for (const id of maps) {
  const m = GEO.maps.find(x => x.id === id);
  const roster = base.slice(0, m.draftCount);
  const configs = args.grid ? grid() : [{}];
  let best = null;
  for (const cfg of configs) {
    const r = run(id, roster, {...cfg, side: args.side ?? 'A'});
    if (!best || r.rounds < best.r.rounds) best = {r, cfg};
  }
  total += best.r.rounds; n++;
  rows.push([id, best.r.rounds, best.r.finished, JSON.stringify(best.cfg), best.r.stats.captures, best.r.stats.recalls,
    best.r.stats.parties.slice(1).join('/'), JSON.stringify(best.r.stats.clears)]);
}
for (const r of rows) console.log(r.map(String).join('\t'));
console.log(`mean rounds ${(total / n).toFixed(1)} over ${n} maps`);

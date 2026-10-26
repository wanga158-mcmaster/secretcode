// Surrogate time use per champion (D334): greedy scheduler with each map's best grid setting, the
// same classes as execgap.py (real games), averaged over the roster.
//   node timeuse.mjs --from grid.tsv [--roster v44] [--maps a,b] > surrogate.tsv
import fs from 'node:fs';
import {GEO} from './model.mjs';
import {Sim} from './sched.mjs';

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const ROSTERS = {v44: ['heart_of_ruin', 'marcella', 'ryth', 'vendetta', 'varg'], meta: ['heart_of_ruin', 'vesper_vale', 'marcella', 'ryth', 'vendetta']};
const base = ROSTERS[args.roster ?? 'v44'] ?? args.roster.split(',');
const best = {};
if (args.from) for (const line of fs.readFileSync(args.from, 'utf8').split('\n')) {
  const p = line.split('\t');
  if (p.length > 3 && p[3].startsWith('{')) best[p[0]] = JSON.parse(p[3]);
}
const CLASSES = ['farm', 'walk', 'capture', 'tribute', 'recall', 'wait', 'idle', 'walkSpawn', 'departures'];
const maps = args.maps ? args.maps.split(',') : GEO.maps.map(x => x.id);
console.log(['map', 'end', 'captures', ...CLASSES].join('\t'));
for (const id of maps) {
  const k = GEO.maps.find(x => x.id === id).draftCount;
  const s = new Sim(id, base.slice(0, k), best[id] ?? {});
  const end = s.playout();
  const per = CLASSES.map(c => s.champs.reduce((a, x) => a + (x.time[c] ?? 0), 0) / s.champs.length);
  console.log([id, end, s.stats.captures, ...per.map(v => v.toFixed(0))].join('\t'));
}

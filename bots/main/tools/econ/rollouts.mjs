// Rollout policy improvement for the economy scheduler (D332): at every decision, score the top-M greedy
// candidates (and waiting) by cloning the simulation, applying the candidate and playing the rest of the
// game out with the greedy policy; take the fastest. One step of policy iteration over the greedy rule.
//   node rollouts.mjs --maps a,b [--roster v44] [--m 4] [--cfg '{"bank":40}'] [--from grid.tsv]
import fs from 'node:fs';
import {GEO, SET} from './model.mjs';
import {Sim} from './sched.mjs';

function remap(task, sim) {
  if (!task || task.special) return task;
  return {...task, party: task.party.map(x => sim.champs[x.slot]), cap: sim.champs[task.cap.slot]};
}
const label = x => x.special ? x.special : x.g.key + '/' + x.party.length;

export function rolloutRun(mapId, roster, opts = {}, m = 4) {
  const s = new Sim(mapId, roster, opts);
  const log = [];
  for (let c = s.next(); c; c = s.next()) {
    const cands = [...s.candidates(c).slice(0, m), ...(opts.specials ? s.specials(c) : [])];
    if (cands.length === 0) { s.apply(c, null); continue; }
    if (cands.length === 1) { s.apply(c, cands[0]); continue; }
    let best = cands[0], bestRounds = Infinity, bestBanked = -1;
    for (const cand of cands) {
      const s2 = s.clone();
      s2.apply(s2.champs[c.slot], remap(cand, s2));
      const r = s2.playout();
      if (r < bestRounds || (r === bestRounds && s2.banked > bestBanked)) { bestRounds = r; bestBanked = s2.banked; best = cand; }
    }
    if (best !== cands[0]) log.push({t: s.t, champ: c.name, greedy: label(cands[0]), chose: label(best)});
    s.apply(c, best);
  }
  return {rounds: s.t, banked: s.banked, stats: s.stats, finished: s.banked >= SET.winning_gems, changes: log};
}

if (import.meta.url === `file://${process.argv[1].replace(/\\/g, '/')}` || process.argv[1]?.endsWith('rollouts.mjs')) {
  const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
    if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
    return acc;
  }, []));
  const ROSTERS = {v44: ['heart_of_ruin', 'marcella', 'ryth', 'vendetta', 'varg'], meta: ['heart_of_ruin', 'vesper_vale', 'marcella', 'ryth', 'vendetta']};
  const base = ROSTERS[args.roster ?? 'v44'] ?? args.roster.split(',');
  const maps = args.maps ? args.maps.split(',') : GEO.maps.map(x => x.id);
  const best = {};
  if (args.from) for (const line of fs.readFileSync(args.from, 'utf8').split('\n')) {
    const p = line.split('\t');
    if (p.length > 3 && p[3].startsWith('{')) best[p[0]] = JSON.parse(p[3]);
  }
  for (const id of maps) {
    const k = GEO.maps.find(x => x.id === id).draftCount;
    const cfg = {...(best[id] ?? {}), ...(args.cfg ? JSON.parse(args.cfg) : {})};
    const t0 = Date.now();
    const r = rolloutRun(id, base.slice(0, k), cfg, Number(args.m ?? 4));
    console.log([id, r.rounds, r.finished, JSON.stringify(cfg), r.stats.captures, r.stats.parties.slice(1, 6).join('/'), r.changes.length, `${Date.now() - t0}ms`].join('\t'));
  }
}

// Opening plans from the economy solver (bots/main/tools/econ, D331-D334) for the S1 plan executor
// (cfg::plan in main/long): plays the team scheduler (greedy, or rollouts with --m) on a map and side and records,
// per champion, the ordered camp groups it clears until round --horizon (party size, capturer flag, start round).
//   node plan.mjs --maps a,b [--sides A,B] [--roster heart_of_ruin,marcella,ryth,vendetta,varg] [--horizon 400]
//                 [--m 0] [--cfg '{"bank":40}'] > plans.json
// Output: {"MAP:SIDE": {"champs": {name: [{x, y, start, party, cap}, ...]}, "banked": n, "t": round}}
import {GEO, SET} from '../../tools/econ/model.mjs';
import {Sim} from '../../tools/econ/sched.mjs';

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const roster = (args.roster ?? 'heart_of_ruin,marcella,ryth,vendetta,varg').split(',');
const horizon = Number(args.horizon ?? 400);
const M = Number(args.m ?? 0);
const cfg = args.cfg ? JSON.parse(args.cfg) : {};

function remap(task, sim) {
  if (!task || task.special) return task;
  return {...task, party: task.party.map(x => sim.champs[x.slot]), cap: sim.champs[task.cap.slot]};
}

function plan(mapId, side) {
  const geo = GEO.maps.find(x => x.id === mapId);
  const s = new Sim(mapId, roster.slice(0, geo.draftCount), {...cfg, side, maxRounds: horizon});
  const out = Object.fromEntries(s.champs.map(c => [c.name, []]));
  for (let c = s.next(); c; c = s.next()) {
    let cands = s.candidates(c);
    let task = cands[0] ?? null;
    if (M > 0 && cands.length > 1) {
      let bestR = Infinity, bestB = -1;
      for (const cand of cands.slice(0, M)) {
        const s2 = s.clone();
        s2.o = {...s2.o, maxRounds: 2000};
        s2.apply(s2.champs[c.slot], remap(cand, s2));
        const r = s2.playout();
        if (r < bestR || (r === bestR && s2.banked > bestB)) { bestR = r; bestB = s2.banked; task = cand; }
      }
    }
    if (task && !task.special) {
      const g = task.g;
      const cx = Math.round(g.members.reduce((a, m) => a + m.x, 0) / g.members.length);
      const cy = Math.round(g.members.reduce((a, m) => a + m.y, 0) / g.members.length);
      for (const x of task.party) out[x.name].push({x: cx, y: cy, start: task.start, party: task.party.length, cap: x === task.cap ? 1 : 0});
    }
    s.apply(c, task);
  }
  return {champs: out, banked: s.banked, t: s.t};
}

const maps = args.maps ? args.maps.split(',') : GEO.maps.map(x => x.id);
const sides = (args.sides ?? 'A,B').split(',');
const result = {};
for (const id of maps) for (const side of sides) {
  try { result[`${id}:${side}`] = plan(id, side); } catch (e) { result[`${id}:${side}`] = {error: String(e)}; }
}
process.stdout.write(JSON.stringify(result));

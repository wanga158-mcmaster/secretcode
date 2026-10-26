// Event-driven team economy scheduler for bots/main (D331-D332). Passive opponent. Every time a
// champion is free it takes a task for the team: clear a group with a party formed on the spot from
// champions free soon (any size up to kmax; parties form and dissolve per target), bank, or recall to
// heal and shop. One designated capturer holds the cleared group (best gold multiplier for gold,
// Marcella/Prism for gem-only groups); the rest move on. `Sim` is cloneable so a search can score a
// candidate decision by playing the rest of the game out (rollouts.mjs).
import {Champ, MapModel, simClear, SET} from './model.mjs';

export const DEFAULTS = {
  bank: 40,          // bag size that triggers a trip to the nearest altar
  goldW: 0.25,       // gem-equivalents per gold in target value
  kmax: 3,           // largest party
  recruit: 12,       // allies free within this many rounds may be recruited
  waitMax: 20,       // longest wait for a respawning group after arrival
  recallFrac: 0.35,  // recall below this fraction of max health when no task is feasible
  shopGold: 150,     // recall to shop with this much gold (0 = only when hurt or dead)
  shopFrac: 0.8,     // ... and only below this health fraction
  captureRecall: 1,  // a capturer that will recall starts it on the tile (overlaps the wait)
  reserveRounds: 2,  // strikes an uncovered tank keeps in reserve to walk out (x group's max strength)
  plan: null,        // shard plan per champion name (default: by main stat)
  alternatives: 0,   // also try the next N earliest allies as a lone partner (search breadth)
  safety: 1,         // a party member needs safety x its expected damage in health (v44: 1.6)
  maxRounds: 2000,
};

const CAPTURE = SET.capture_time + 1;
const mapCache = new Map();
export function mapModel(id, side = 'A') {
  const key = `${id}:${side}`;
  if (!mapCache.has(key)) mapCache.set(key, new MapModel(id, side));
  return mapCache.get(key);
}

function mainStat(c) {
  const base = c.campRate(1);
  const gain = st => { const k = `power:${st}`; c.shards[k] = 1; const r = c.campRate(1); delete c.shards[k]; return r - base; };
  return gain('foc') > gain('str') ? 'foc' : 'str';
}

export function defaultPlan(c) {
  const main = mainStat(c);
  if (c.passive?.type === 1) return [['treasure', main], ['jungle', main], ['monster', main], ['bruiser', main]];  // Vesper
  if (!c.melee) return [['jungle', main], ['treasure', main], ['monster', main], ['bruiser', main]];
  return [['jungle', main], ['treasure', main], ['bruiser', 'hp'], ['monster', main], ['bruiser', main]];
}

function cloneChamp(c) {
  const d = Object.create(Object.getPrototypeOf(c));
  Object.assign(d, c);
  d.shards = {...c.shards};
  d.pendingBank = c.pendingBank ? {...c.pendingBank} : null;
  d.time = {...c.time};
  return d;
}

export class Sim {
  constructor(mapId, roster, opts = {}) {
    this.o = {...DEFAULTS, ...opts};
    this.mm = mapModel(mapId, this.o.side ?? 'A');
    this.champs = roster.map((n, i) => new Champ(n, i));
    for (const c of this.champs) {
      c.pt = 0; c.free = 1; c.pendingBank = null; c.plan = this.o.plan?.[c.name] ?? defaultPlan(c); c.shop(c.plan);
      c.time = {farm: 0, walk: 0, capture: 0, tribute: 0, recall: 0, wait: 0, idle: 0};
    }
    const G = this.mm.groups.length;
    this.avail = new Int32Array(G).fill(1);
    this.claimed = new Int32Array(G).fill(-1);
    this.banked = 0; this.t = 0; this.done = false;
    this.stats = {captures: 0, tributes: 0, recalls: 0, parties: [0, 0, 0, 0, 0, 0, 0], gold: 0, clears: {}};
  }
  clone() {
    const s = Object.create(Sim.prototype);
    s.o = this.o; s.mm = this.mm;
    s.champs = this.champs.map(cloneChamp);
    s.avail = this.avail.slice(); s.claimed = this.claimed.slice();
    s.banked = this.banked; s.t = this.t; s.done = this.done;
    s.stats = {...this.stats, parties: [...this.stats.parties], clears: {...this.stats.clears}};
    return s;
  }
  nearestAltar(pt) {
    let best = -1, bd = 32767;
    for (const a of this.mm.altarPoints) if (this.mm.dist[pt][a] < bd) { bd = this.mm.dist[pt][a]; best = a; }
    return [best, bd];
  }
  recall(c, at) {
    c.time.idle += Math.max(0, at - c.free); c.time.recall += c.recallTurns() + 1;
    c.free = at + c.recallTurns() + 1; c.pt = 0; c.hp = c.maxHp(); this.stats.recalls++; c.shop(c.plan);
  }
  bank(c) {
    const [a, d] = this.nearestAltar(c.pt);
    const ch = Math.max(1, Math.ceil(c.bag / 10));
    c.time.walk += d; c.time.tribute += ch;
    c.pendingBank = {at: this.t + d + ch, gems: c.bag}; c.bag = 0; c.pt = a; c.free = this.t + d + ch; this.stats.tributes++;
  }
  /** Advance to the next free champion, applying any automatic action (bank, endgame, shop recall).
   * Returns the champion that needs a task decision, or null when the game is over. */
  next() {
    const o = this.o;
    for (;;) {
      let c = this.champs[0];
      for (const x of this.champs) if (x.free < c.free || (x.free === c.free && x.slot < c.slot)) c = x;
      this.t = c.free;
      for (const x of this.champs) if (x.pendingBank && x.pendingBank.at <= this.t) { this.banked += x.pendingBank.gems; x.pendingBank = null; }
      if (this.banked >= SET.winning_gems) { this.done = true; return null; }
      if (this.t > o.maxRounds) { this.done = true; this.t = o.maxRounds; return null; }
      if (c.pt === 0) c.shop(c.plan);
      const bankable = x => x.bag > 0 && this.nearestAltar(x.pt)[1] < 32767;
      const pendingGems = this.champs.reduce((s, x) => s + (x.pendingBank ? x.pendingBank.gems : 0), 0);
      const endgame = this.banked + pendingGems + this.champs.reduce((s, x) => s + (bankable(x) || x.pt !== 0 ? x.bag : 0), 0) >= SET.winning_gems;
      if (c.bag > 0 && (c.bag >= o.bank || endgame)) {
        const [a, d] = this.nearestAltar(c.pt);
        if (a >= 0 && d < 32767) { this.bank(c); continue; }
        if (c.pt !== 0) { this.recall(c, this.t); continue; }
      }
      if (endgame && this.champs.some(x => x !== c && (x.bag > 0 || x.pendingBank))) { c.time.idle += 1; c.free = this.t + 1; continue; }
      if (o.shopGold > 0 && c.gold >= o.shopGold && c.pt !== 0 && c.hp < o.shopFrac * c.maxHp()) { this.recall(c, this.t); continue; }
      return c;
    }
  }
  /** Feasible clear tasks for champion `c` now, best greedy score first. */
  candidates(c) {
    const o = this.o, t = this.t, mm = this.mm, out = [];
    for (const g of mm.groups) {
      if (this.claimed[g.i] > t) continue;
      const gp = mm.groupPoint(g.i);
      const reach = mm.dist[c.pt][gp];
      if (reach >= 32767 || this.avail[g.i] > t + reach + o.waitMax) continue;
      const others = this.champs.filter(x => x !== c && x.free <= t + o.recruit)
        .map(x => ({x, arrive: Math.max(x.free, t) + mm.dist[x.pt][gp]})).filter(e => e.arrive < 32767)
        .sort((a, b) => a.arrive - b.arrive);
      // Parties: c alone, then c plus the earliest arrivals (prefixes); with o.alternatives, also c
      // with each of the next earliest allies as the single partner.
      const teams = [];
      for (let k = 1; k <= Math.min(o.kmax, this.champs.length); k++) {
        if (k > 1 && !others[k - 2]) break;
        teams.push(others.slice(0, k - 1));
      }
      if (o.alternatives && o.kmax >= 2) for (const e of others.slice(1, 1 + o.alternatives)) teams.push([e]);
      for (const team of teams) {
        const party = [c, ...team.map(e => e.x)], arrivals = [t + reach, ...team.map(e => e.arrive)];
        const start = Math.max(Math.max(...arrivals), this.avail[g.i]);
        const f = simClear(g, party);
        if (!Number.isFinite(f.rounds)) continue;
        const covered = party.length >= 2;
        let ok = true;
        for (let i = 0; i < party.length; i++) {
          const reserve = covered ? 0 : Math.max(0, g.maxRange - 1) * g.maxStr * party[i].campTakenMult() * (o.reserveRounds / 2);
          if (f.damage[i] * o.safety > party[i].hp - reserve) ok = false;
        }
        if (!ok) continue;
        let cap = party[0];
        const capScore = y => g.gold > 0 ? y.goldMult() : (y.passive?.type === 8 ? 2 : y.passive?.type === 3 ? 1.5 : 1);
        for (const x of party) if (capScore(x) > capScore(cap)) cap = x;
        const busy = party.reduce((s, x) => s + (start - Math.max(x.free, t)) + f.rounds, 0) + CAPTURE;
        const value = g.gems + o.goldW * g.gold * cap.goldMult();
        out.push({score: value / busy, g, party: [...party], start, f, cap});
      }
    }
    out.sort((a, b) => b.score - a.score);
    return out;
  }
  /** Extra non-clear options a search may weigh: bank the bag now, recall now. */
  specials(c) {
    const out = [];
    if (c.bag > 0 && this.nearestAltar(c.pt)[1] < 32767) out.push({special: 'bank'});
    if (c.pt !== 0 && (c.gold >= 30 || c.hp < c.maxHp())) out.push({special: 'recall'});
    return out;
  }
  /** Apply a decision for champion `c`: a candidate from candidates(c), or null (recall or wait). */
  apply(c, task) {
    const o = this.o;
    if (!task) {
      if (c.hp < c.maxHp() && c.pt !== 0) this.recall(c, this.t);
      else { c.time.idle += 5; c.free = this.t + 5; }
      return;
    }
    if (task.special === 'recall') { this.recall(c, this.t); return; }
    if (task.special === 'bank') { this.bank(c); return; }
    const {g, party, start, f, cap} = task;
    this.stats.parties[party.length]++;
    const kind = g.types.includes('elk') ? 'elk' : g.big ? 'big' : g.harmless ? 'cluster' : 'wolf';
    this.stats.clears[kind] = (this.stats.clears[kind] ?? 0) + 1;
    const end = start + f.rounds;
    const gp = this.mm.groupPoint(g.i);
    party.forEach((x, i) => {
      const from = Math.max(x.free, this.t), walk = this.mm.dist[x.pt][gp];
      x.time.walk += walk; x.time.wait += Math.max(0, start - from - walk); x.time.farm += f.rounds;
      if (x.pt === 0) { x.time.walkSpawn = (x.time.walkSpawn ?? 0) + walk; x.time.departures = (x.time.departures ?? 0) + 1; }
      x.hp -= f.damage[i]; x.pt = gp; x.free = end;
    });
    cap.time.capture += CAPTURE;
    const capEnd = end + CAPTURE;
    cap.free = capEnd;
    const gold = g.gold * cap.goldMult();
    cap.gold += gold; cap.earned += gold; this.stats.gold += gold;
    cap.bag += g.gems; this.stats.captures++;
    this.claimed[g.i] = capEnd;
    this.avail[g.i] = capEnd + g.respawn;
    if (o.captureRecall && ((o.shopGold > 0 && cap.gold >= o.shopGold) || cap.hp < o.recallFrac * cap.maxHp())) {
      cap.free = capEnd; cap.pt = 0; cap.hp = cap.maxHp(); this.stats.recalls++; cap.shop(cap.plan);
    }
    for (const x of party) if (x !== cap && x.hp < o.recallFrac * x.maxHp()) this.recall(x, end);
  }
  /** Greedy play to the end; returns the finishing round. */
  playout() {
    for (let c = this.next(); c; c = this.next()) this.apply(c, this.candidates(c)[0] ?? null);
    return this.t;
  }
}

export function run(mapId, roster, opts = {}) {
  const s = new Sim(mapId, roster, opts);
  const rounds = s.playout();
  return {rounds: Math.min(rounds, s.o.maxRounds), banked: s.banked, stats: s.stats, finished: s.banked >= SET.winning_gems};
}

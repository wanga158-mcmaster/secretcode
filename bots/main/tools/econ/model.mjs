// Economy surrogate for bots/main (D331): champion camp-damage rates, Smith-ordered clear arithmetic,
// walking distances and item effects from the pinned runtime catalog and map geometry. It forecasts
// a passive-opponent economy fast enough for search; the exact engine (tools/engine) is the referee.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export const CAT = JSON.parse(fs.readFileSync(path.join(ROOT, 'docs/reference/runtime-catalog.json'), 'utf8')).result;
export const GEO = JSON.parse(fs.readFileSync(path.join(ROOT, 'maps/geometry.json'), 'utf8'));
export const SET = CAT.settings;

// SDK ability_kind: 1 attack reset, 2 damage, 3 area damage, 4 crowd control, 5 heal, 6 dash, 7 shield.
const K = {reset: 1, damage: 2, aoe: 3, cc: 4, heal: 5, dash: 6, shield: 7};
// SDK passive_type ids as the catalog reports them (passives[].type).
const P = {bonusGold: 1, remoteShop: 2, gemFocus: 3, splash: 4, healthStrength: 5, killReset: 6, retaliate: 7, tributeGold: 8, missingHealth: 10};

export const CAMP = Object.fromEntries(CAT.camps.map(c => [c.name, {hp: c.stats.base_health, str: c.stats.base_strength, range: c.stats.base_attack_range}]));
export const DESIGN = Object.fromEntries(CAT.champions.map(c => [c.name, c]));
const PASSIVE = Object.fromEntries(CAT.passives.map(p => [p.name, p]));

// Item families: emblem passive ids in the catalog (items[].passive on emblems): 1 monster (+10% camp
// damage), 2 durability (-10% damage), 3 treasure (+20% gold), 4 jungle (-30% camp damage), 5 home
// (recall 3), 0 power/bruiser (stats only). Read names to stay robust to id changes.
export const FAMILIES = ['monster', 'power', 'durability', 'treasure', 'jungle', 'home', 'bruiser'];
const SHARD_COST = CAT.items.find(i => i.name === 'monster_shard_of_strength').cost;
const EMBLEM_MAG = {};
for (const item of CAT.items) {
  const m = /^([a-z]+)_emblem_of_([a-z]+)$/.exec(item.name);
  if (m) EMBLEM_MAG[m[1]] = item.passive_magnitude;
}
export const SHARD = SHARD_COST;

/** A champion instance with live stats from shards bought (counts per family x stat). */
export class Champ {
  constructor(name, slot) {
    const d = DESIGN[name];
    this.name = name; this.slot = slot;
    this.melee = d.stats.base_attack_range <= 1;
    this.range = d.stats.base_attack_range;
    this.base = {hp: d.stats.base_health, str: d.stats.base_strength, foc: d.stats.base_focus};
    this.grow = {hp: d.stats.health_growth, str: d.stats.strength_growth, foc: d.stats.focus_growth};
    this.abilities = d.abilities;
    this.passive = CAT.passives.find(p => p.name === name) ?? PASSIVE[name];
    this.shards = {};  // "family:stat" -> count
    this.gold = 0; this.earned = 0; this.bag = 0;
    this.hp = this.maxHp();
  }
  count(stat) { let n = 0; for (const [k, v] of Object.entries(this.shards)) if (k.endsWith(':' + stat)) n += v; return n; }
  emblem(family) { return FAMILIES.includes(family) && Object.entries(this.shards).some(([k, v]) => k.startsWith(family + ':') && v >= 5); }
  stat(stat) {
    let n = 0;
    for (const [k, v] of Object.entries(this.shards)) {
      const [fam, st] = k.split(':');
      const full = Math.floor(v / 5), rest = v % 5;
      if (fam === 'power') n += (st === stat ? full * 6 + rest : 0);
      else if (fam === 'bruiser') n += (st === stat ? full * 5 + rest : full);
      else n += (st === stat ? v : 0);
    }
    const g = this.grow[stat];
    let base = this.base[stat] + n * g;
    if (stat === 'str' && this.passive?.type === P.healthStrength) base += this.passive.magnitude * (this.stat('hp') - this.base.hp);
    return base;
  }
  maxHp() { return this.stat('hp'); }
  goldMult() {
    let m = 1;
    if (this.passive?.type === P.bonusGold) m += this.passive.magnitude;
    if (this.emblem('treasure')) m += EMBLEM_MAG.treasure ?? 0.2;
    return m;
  }
  campTakenMult() {
    let m = 1;
    if (this.emblem('jungle')) m *= 1 - (EMBLEM_MAG.jungle ?? 0.3);
    if (this.emblem('durability')) m *= 1 - (EMBLEM_MAG.durability ?? 0.1);
    return m;
  }
  recallTurns() { return this.emblem('home') ? Math.ceil(SET.recall_time * (1 - (EMBLEM_MAG.home ?? 0.5)) - 1e-6) : SET.recall_time; }
  /** Sustained camp damage per round, standing, against a group whose members have `nb` neighbours
   * within radius 1 on average (economy E2): basic x (1 + resets / 8) + damaging casts / 8. */
  campRate(nb) {
    const S = this.stat('str'), F = this.stat('foc');
    let basic = S, casts = 0, resetShare = 0;
    for (const a of this.abilities) {
      const cd = Math.max(1, a.cooldown);
      const p = a.payload ?? {};
      if (a.kind === K.reset) resetShare += 1 / cd;
      else if (a.kind === K.damage) casts += (p.modifier ?? 0) * F / cd;
      else if (a.kind === K.aoe) casts += (p.modifier ?? 0) * F * (1 + nb) / cd;
      else if (a.kind === K.cc) casts += (p.modifier ?? 0) * F / cd;
      else if (a.kind === K.dash && (p.modifier ?? 0) > 0) casts += p.modifier * F * (1 + nb) / cd;
    }
    let rate = basic * (1 + resetShare);
    if (this.passive?.type === P.splash) rate *= 1 + nb;
    if (this.passive?.type === P.missingHealth) rate *= 1.15;  // hurt in long clears (up to +50%)
    rate += casts;
    if (this.emblem('monster')) rate *= 1 + (EMBLEM_MAG.monster ?? 0.1);
    return rate;
  }
  /** Shield health per round from self-shields (Varg, Heart): absorbs camp strikes. */
  shieldRate() {
    let s = 0;
    for (const a of this.abilities) if (a.kind === K.shield) s += (a.payload.modifier ?? 0) * this.stat('foc') / Math.max(1, a.cooldown);
    return s;
  }
  /** Buy shards along `plan` (array of [family, stat]) with the gold carried, five per step. */
  shop(plan) {
    let bought = 0;
    for (const [fam, st] of plan) {
      const key = `${fam}:${st}`;
      const want = fam === 'power' || fam === 'bruiser' ? Infinity : 5;
      while ((this.shards[key] ?? 0) < want && this.gold >= SHARD) {
        const before = this.maxHp();
        this.shards[key] = (this.shards[key] ?? 0) + 1;
        this.gold -= SHARD; bought++;
        this.hp += this.maxHp() - before;  // spawn purchases heal the health they add
      }
      if ((this.shards[key] ?? 0) < want) break;  // finish a block before the next
    }
    return bought;
  }
}

/** Static map model: walkable grid, camp groups with clear constants, key-point distances. */
export class MapModel {
  constructor(id, side = 'A') {
    const m = GEO.maps.find(x => x.id === id);
    if (!m) throw Error(`unknown map ${id}`);
    this.id = id; this.w = m.width; this.h = m.height; this.draft = m.draftCount; this.symmetry = m.symmetry;
    const n = this.w * this.h;
    this.blocked = new Uint8Array(n);
    for (const t of m.terrain.wall) this.blocked[t] = 1;
    for (const t of m.terrain.hole) this.blocked[t] = 1;
    for (const t of m.terrain.altar) this.blocked[t] = 1;
    this.altars = m.terrain.altar.map(t => ({x: t % this.w, y: Math.floor(t / this.w), t}));
    const rect = r => { const out = []; for (let y = r.minY; y <= r.maxY; y++) for (let x = r.minX; x <= r.maxX; x++) out.push(y * this.w + x); return out; };
    const mine = side === 'A' ? m.spawns.teamA : m.spawns.teamB, theirs = side === 'A' ? m.spawns.teamB : m.spawns.teamA;
    this.spawn = rect(mine); this.enemySpawn = rect(theirs);
    for (const t of this.enemySpawn) this.blocked[t] = 1;
    this.campTile = new Int16Array(n).fill(-1);
    this.groups = m.groups.map((g, i) => {
      const members = g.members.map(mm => ({x: mm.x, y: mm.y, t: mm.tile, type: mm.type, ...CAMP[mm.type]}));
      for (const mm of members) this.campTile[mm.t] = i;
      const H = members.reduce((s, mm) => s + mm.hp, 0);
      const order = [...members].sort((a, b) => b.str / b.hp - a.str / a.hp);
      let cum = 0, W = 0;
      for (const mm of order) { cum += mm.hp; W += mm.str * cum; }
      let nb = 0;
      for (const a of members) for (const b of members) if (a !== b && Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y)) <= 1) nb++;
      nb = members.length ? nb / members.length : 0;
      const types = members.map(mm => mm.type);
      return {i, key: g.key, members, gold: g.goldValue, gems: g.gemValue, respawn: g.cooldown, H, W, nb,
        maxRange: Math.max(0, ...members.map(mm => mm.str > 0 ? mm.range : 0)), maxStr: Math.max(0, ...members.map(mm => mm.str)),
        harmless: members.every(mm => mm.str <= 0), types,
        elk: types.includes('elk'), big: types.includes('golem') || types.includes('monolith')};
    });
    this.computeDistances();
  }
  bfs(sources, through) {
    const n = this.w * this.h, d = new Int16Array(n).fill(32767), q = new Int32Array(n);
    let head = 0, tail = 0;
    for (const s of sources) { if (d[s] === 32767) { d[s] = 0; q[tail++] = s; } }
    while (head < tail) {
      const t = q[head++], x = t % this.w, y = (t - x) / this.w, nd = d[t] + 1;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        if (!dx && !dy) continue;
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= this.w || ny >= this.h) continue;
        const u = ny * this.w + nx;
        if (d[u] <= nd || this.blocked[u]) continue;
        const g = this.campTile[u];
        // Living monsters block, except the group we walk to; harmless clusters die in a hit or two
        // and are cleared on the way (00quadrants is ringed by 81 of them).
        if (g >= 0 && g !== through && !this.groups[g].harmless) continue;
        d[u] = nd; q[tail++] = u;
      }
    }
    return d;
  }
  computeDistances() {
    // Key points: 0 spawn, 1..G groups, G+1.. altars. dist[a][b] = walking rounds to stand in reach.
    const G = this.groups.length, A = this.altars.length;
    this.nPoints = 1 + G + A;
    const dist = Array.from({length: this.nPoints}, () => new Int16Array(this.nPoints).fill(32767));
    const fields = [];
    fields.push(this.bfs(this.spawn, -1));
    for (const g of this.groups) fields.push(this.bfs(g.members.map(m => m.t), g.i));
    for (const a of this.altars) {
      const near = [];
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        const x = a.x + dx, y = a.y + dy;
        if (x >= 0 && y >= 0 && x < this.w && y < this.h && !this.blocked[y * this.w + x]) near.push(y * this.w + x);
      }
      fields.push(this.bfs(near, -1));
    }
    // Arrival tiles: spawn tiles; free tiles beside a group's members (melee reach) or an altar.
    const beside = tiles => {
      const out = new Set();
      for (const t0 of tiles) {
        const x0 = t0 % this.w, y0 = (t0 - x0) / this.w;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
          const x = x0 + dx, y = y0 + dy;
          if (x < 0 || y < 0 || x >= this.w || y >= this.h) continue;
          const u = y * this.w + x;
          if (!this.blocked[u] && (this.campTile[u] < 0 || this.groups[this.campTile[u]].harmless)) out.add(u);
        }
      }
      return [...out];
    };
    const targets = [this.spawn, ...this.groups.map(g => [...beside(g.members.map(m => m.t)), ...g.members.map(m => m.t)]),
      ...this.altars.map(a => beside([a.t]))];
    for (let a = 0; a < this.nPoints; a++) for (let b = 0; b < this.nPoints; b++) {
      let best = 32767;
      for (const t of targets[b]) best = Math.min(best, fields[a][t]);
      dist[a][b] = best;
    }
    this.dist = dist;
    this.altarPoints = this.altars.map((_, k) => 1 + G + k);
  }
  groupPoint(i) { return 1 + i; }
}

/** Camp clear of group `g` by champions `party` (all present from the start): rounds of fighting and
 * damage per member. Strikes hit the nearest body; with covered rotation the melee members share
 * them (ranged ones only without a melee). Economy E1: damage = W / d. */
export function clear(g, party) {
  const d = party.reduce((s, c) => s + c.campRate(g.nb), 0);
  if (d <= 0) return {rounds: Infinity, damage: party.map(() => Infinity)};
  const rounds = Math.ceil(g.H / d) + 1;
  const total = g.W / d;
  const tanks = party.filter(c => c.melee);
  const share = tanks.length ? tanks : party;
  const per = party.map(c => share.includes(c) ? Math.max(0, total / share.length * c.campTakenMult() - c.shieldRate() * rounds) : 0);
  return {rounds, damage: per, rate: d};
}

/** Per-round damage components of a champion against camps: single-target, area (target and its
 * living neighbours), splash flag (Varg: basic also hits neighbours). Casts are averaged per round. */
Champ.prototype.components = function () {
  const S = this.stat('str'), F = this.stat('foc');
  let reset = 0, single = 0, aoe = 0;
  for (const a of this.abilities) {
    const cd = Math.max(1, a.cooldown), p = a.payload ?? {};
    if (a.kind === 1) reset += 1 / cd;
    else if (a.kind === 2 || a.kind === 4) single += (p.modifier ?? 0) * F / cd;
    else if (a.kind === 3) aoe += (p.modifier ?? 0) * F / cd;
    else if (a.kind === 6 && (p.modifier ?? 0) > 0) aoe += p.modifier * F / cd;
  }
  let mult = this.emblem('monster') ? 1.1 : 1;
  if (this.passive?.type === 10) mult *= 1.15;
  return {basic: S * (1 + reset) * mult, single: single * mult, aoe: aoe * mult, splash: this.passive?.type === 4, shield: this.shieldRate()};
};

const cheb = (a, b) => Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y));

/** Round-by-round clear of group `g` by `party` (present from the start): Smith kill order, splash and
 * area damage on living neighbours of the target, every living member striking the tanks from the
 * second round. Returns rounds and damage per party member (melee share strikes; shields absorb). */
export function simClear(g, party) {
  const members = g.members.map(m => ({x: m.x, y: m.y, str: m.str, hp: m.hp, alive: true}));
  const order = [...members].sort((a, b) => b.str / b.hp - a.str / a.hp);
  const comp = party.map(c => c.components());
  let rounds = 0, strikes = 0;
  while (rounds < 600) {
    const target = order.find(m => m.alive);
    if (!target) break;
    rounds++;
    const nbrs = members.filter(m => m.alive && m !== target && cheb(m, target) <= 1);
    for (const cp of comp) {
      target.hp -= cp.basic + cp.single + cp.aoe;
      for (const nb of nbrs) nb.hp -= (cp.splash ? cp.basic : 0) + cp.aoe;
    }
    for (const m of members) if (m.alive && m.hp <= 0) m.alive = false;
    if (rounds > 1) strikes += members.reduce((s, m) => s + (m.alive ? m.str : 0), 0);
  }
  if (rounds >= 600) return {rounds: Infinity, damage: party.map(() => Infinity)};
  const tanks = party.filter(c => c.melee);
  const share = tanks.length ? tanks : party;
  // A self-shield (amount A, D rounds, cooldown C) absorbs at most A per cast and only what lands in
  // its D rounds: per cast min(A, D x incoming per round), ceil(rounds / C) casts.
  const damage = party.map(c => {
    if (!share.includes(c)) return 0;
    const taken = strikes / share.length * c.campTakenMult();
    const sh = c.shield();
    if (!sh) return taken;
    const perRound = rounds > 0 ? taken / rounds : 0;
    const absorbed = Math.ceil(rounds / sh.cooldown) * Math.min(sh.amount, sh.duration * perRound);
    return Math.max(0, taken - absorbed);
  });
  return {rounds: rounds + 1, damage};
}

Champ.prototype.shield = function () {
  for (const a of this.abilities)
    if (a.kind === 7) return {amount: (a.payload.modifier ?? 0) * this.stat('foc'), duration: Math.max(1, a.payload.duration ?? 5), cooldown: Math.max(1, a.cooldown)};
  return null;
};

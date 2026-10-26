# Combat: the arithmetic of fights between champions (engine 1.0.2)

The tables below are historical engine 1.0.2 measurements, not blanket current-bundle guarantees.
The 24 October independent renewal corrects the escape interpretation in C8/C20 and derives the
ordered-prefix counterexample C22 from the pinned mechanics/catalog. C19's main-source comparisons
remain inherited historical analyses; they are not independent premises for the contenders.
Slot/cadence consequences later in this ledger inherit C2's fixed-relative-rank and recorded-policy
premises. In particular, sampled chase frequencies or two-turn reach formulas do not supply universal
opportunity bounds under dynamic priorities; use the explicit current/next schedule instead.

What decides a fight, measured in about 4,700 engine games of a scripted duel bot and reproduced by a
turn-exact model that reads only runtime values. Entries state structural laws first (turn tempo,
contact, numbers, stuns, escape) and the tables they produce at today's balance second; every number
is named by the runtime value that drives it, so the laws survive a patch and the tables are rerun.

**How it was measured.** The scratch bot `bots/contenders/_duel-lab` (one build for every scenario)
reads its scenario from wall bits on the map's bottom row: both teams' picks, builds (bought at spawn
after farming a 25-cluster gold block), policies and placement. Maps are 41x30 rotational arenas on open
ground, written through the official editor engine by `tools/combat/mkmap.mjs`; fillers rest on spawn,
out of vision. `tools/combat/duel.py run|parse|show` plays a scenario set through arena's release builds
(one game per key; games are deterministic) and reads the bots' per-turn logs; `analyze.py` and
`export.py` build the tables in `data/combat/`; `duelsim.py` is the model (C1). Runs (regenerable) are in
`.cache/contenders/combat/runs/`, scenario sets in `.cache/contenders/combat/s_*.json`.

**Policies** (the "good ability use" behind every number): FIGHT approaches (melee to 1, ranged to 3),
opens with Heart's dash on the landing tile next to the most enemies, shields when an enemy is within
2, casts stuns, then damage spells, then area spells when the target is within 4 (melee only when
adjacent, so the walking-exclusive cast costs no step), dashes in from 2-4 (melee), attacks, then
"attack, reset, attack"; Willow heals the most injured ally within 4 when 60% of the heal lands.
Targets: the least-health enemy within 4, else the nearest. Variants: BASIC (no abilities), SPREAD
(fixed different targets), STAGGER (hold a stun while the target's stun duration is above 0), KITE,
FLEE, FLEEHIT (strike, then step), FLEESTUN, HOLD, STANDOFF (C16), BAIT (C18).

**Conventions.** Picks: Ryth, Vesper, Vendetta, Cassi & Issac (C&I), Stellar, Prism, Marcella, Willow,
Ren, Varg, Tavi, Heart. A cell's *margin* is the winner's surviving share of total health, signed from
the row champion (+0.30: the row champion won with 30% of its health left), averaged over both sides
and both round parities (4 games). Builds are shard counts: H10 S6 F1 = ten Health, six Strength, one
Focus shard (catalog growth per shard, M39). Turn *slot* = position in a team's turn order (M2).

**How to rerun** (pinned Python, `arena env`): `tools/combat/scenarios.py` writes the scenario sets
(`s_base`, `s_b11`, `s_stat`, `s_online`, `s_metab`, `s_kite2`, `s_chase`, `s_fleehit`, `s_slot`,
`s_stun`, `s_camp`, `s_standoff`, `s_standoff3`, `s_g1`); `tools/combat/duel.py run SET RUNS/<name>`
plays one; `tools/combat/export.py` rebuilds `data/combat/*.json` from the run directories, including
the model check (`duelsim.py compare RUN` does it for one run). A full rerun is about 4,700 games, about
half an hour on the shared slots. The final bot reproduced the base set game for game (288/288).

## Index
| Id | Law |
| --- | --- |
| C1 | Fights are turn-exact arithmetic; the model reproduces engine duels exactly |
| C2 | Fixed ranks give pair double turns or alternation; dynamic priorities require explicit schedules |
| C3 | One-on-one at base stats: Heart, Marcella, Tavi, Vendetta and Ryth win most duels; casters lose |
| C4 | Why: effective health times sustained damage, corrected by first strike, stuns and burst |
| C5 | At equal gold Health and Strength tie for fighting and Focus loses, except Heart, Varg and C&I |
| C6 | At r300-400 builds the Focus Heart is the strongest duelist; C&I falls apart |
| C7 | Ranged champions cannot kite at equal speed; range buys about two free hits against dash-less melee |
| C8 | Escape: an equal-speed chaser keeps contact and lands full or half damage by slot; dashes decide |
| C9 | A retreating melee champion should keep hitting: it costs nothing |
| C10 | Stuns cost one turn and never stack; k staggered stunners lock k turns in 8; no perma-lock |
| C11 | Numbers: the square law holds in the engine; even fights are coin flips that wreck both sides |
| C12 | Focus fire is worth about one champion |
| C13 | Group fights: damage per round, time to kill an isolated champion, who dies first |
| C14 | Area damage scales with clumping; two tiles of spacing defeat it |
| C15 | The lowest-health support is the natural focus: Willow lost every 2v2 |
| C16 | First-strike discipline: hold outside the enemy's two-turn reach; two holders never fight |
| C17 | The value of a kill and of a death |
| C18 | Monsters in fights: the nearer champion pays, so waking a camp the enemy is nearer to wins |
| C19 | Where main's fight model and the strategy hypotheses are wrong |
| C20 | Contact safeguards remain conditional hypotheses |
| C21 | Online, the death hazard is a function of contact, health and allies, not of the bag |
| C22 | Ordered burst invalidates average-rate escape guarantees |
| C23 | Admit the complete word after its effects change hazards and legal continuation |

### C1. A fight is turn-exact arithmetic of runtime values; the model reproduces engine duels exactly
Claim: Given kits, builds, policies and the turn order, a fight's outcome is fixed and computable.
  `tools/combat/duelsim.py` reads only `docs/reference/runtime-catalog.json` (stats, growth, ability
  kinds and payloads, passives) plus the rules of C2 and M2-M3, M22-M36, M39-M40, M43, applies health
  loss in float32, and places champions on a line (Chebyshev distance = the gap along it). Against the
  engine: base duels 288/288 winners, 270 with the exact surviving health (all 18 misses are C&I's
  two-body geometry); equal-gold stat duels 424/432 (misses: C&I, and two Varg fights longer than the
  72-round window); duels at H10 S6 F1 270/288 and at online builds 152/162 (every miss involves C&I,
  whose two bodies split one gold block in the engine but not in the model). Group fights (19 table
  rows x 2 parities): 36/38 engine winners (the misses are a 3v3 mirror decided by 1% health); surviving
  health is off by up to 0.2 of a team's total in full-kit melees, where 2D reach matters.
Evidence: runs base, stat, b11, online, metab, g1 (`duelsim.py compare RUN`;
  `data/combat/model_calibration.json`, whose single-body diff lists are empty but for the two Varg
  window cases). Group check: `duelsim.py groups RUN` on the s_g1 run (the rows of
  `_duel-lab/src/table.h` on a line, columns 8 tiles apart).
Rests on: M2, M3, M22-M36, M39, M40, M43.
Status: verified (engine 1.0.2) for one-on-one between single bodies; model (95% of group winners).
Recheck: any catalog change: rerun `s_base.json` and `compare`. Number changes need no code edit; a
  new ability kind or passive type does.
Consequence: the exchange inside a fight is not uncertain, and a bot can compute it (a few hundred
  simulated steps) instead of approximating. What is uncertain in real fights is who takes part, who
  moves, and the turn slots: C2, C8, C11, C13. Structure: fight risk lives in geometry and timing, not in
  the damage arithmetic.

### C2. Fixed ranks give pair double turns or alternation; dynamic priorities require explicit schedules
Claim: With M2's interleaving (round r: X1 Y1 X2 Y2 ...; the other team starts round r+1), a champion
  in fixed rank i and an enemy in fixed rank j have one of two projected pair-input patterns. Both
  ranks must remain fixed across the rounds being compared; all other actors are omitted from this
  projection, not assumed physically absent:
  - **i = j:** X Y | Y X | X Y ... Each side gets two turns in a row (the end of one round and the start
    of the next) every second round, and the round's parity decides who acts first in the pair.
  - **i ≠ j:** strict alternation, and the lower slot acts first in every round, whatever the parity.

  This is not a global team-input alternation theorem. M2's equal-body evidence includes consecutive
  team inputs across the boundary, `...A2 B5 | B3 A0...`. With equal fixed pair ranks, one champion's
  later pair input in r and earlier pair input in r+1 have no opposing pair input between them,
  although other actors and camp phases may intervene. Measured fixed-schedule consequences:
  - Mirror duels between first picks (same slot) are won by the side whose turn comes first at contact,
    so they flip with the parity of the contact round (C3). With the duelists in slots 1 and 3 the same 12
    mirrors give one winner in both parities: the lower slot wins Ryth, Vesper, Vendetta, C&I, Stellar and
    Varg mirrors, the later actor wins Prism, Marcella, Willow, Ren, Tavi and Heart mirrors.
  - A melee chaser hits a fleeing champion every second round from the same slot and every round from a
    different slot (C8): Varg's fleer loses 26-33 health per round from the same slot and 45-51 from a
    different one (Varg's Strength 40 plus a reset every 8 rounds).
  - An approacher that ends its turn just outside the enemy's reach and acts again next closes the last
    tile and strikes first (C16).
Evidence: `data/combat/slot_mirrors.json` (24 games; team B's fillers at priority 5 put its duelist in
  slot 3), chase runs (`data/combat/chase.json`: e.g. Vendetta chasing Marcella hits in rounds 52, 54,
  56, ... from the same slot), naive-standoff mirrors (runs/standoff).
Rests on: M2.
Status: projected fixed-rank scheduling pattern derived from M2; listed finite duel/chase consequences
  measured on engine 1.0.2. These are not universal contact or latency bounds under changing ranks.
Recheck: scheduling changes (M2).
Consequence: Resolve actual actor inputs rather than simultaneous volleys or an assumed fixed cadence.
  Current rank uses committed current-round priority; observed next-round priority can still change
  until its owner's current input completes, and pending peers can change another body's rank. The
  sampled fixed-rank chase frequencies are descriptive for those keys, not guarantees of future hits.

  A bounded scheduled-opportunity producer can make the premise explicit. At the observing owner's
  current input let `C` be 1 if a fresh visible foe has not completed its current-round input, else 0
  (M45's pinned native round-start reset). Suppose the owner's next-round rank `a` is committed and
  the foe's next-round rank lies in `[bmin,bmax]` under every still-possible priority change. A foe
  input next round is retained as possible before that owner's next input when `bmin<a`, or
  `bmin==a` and the foe's team starts next round. It is guaranteed to precede when the same comparison
  holds for `bmax`. Thus an upper scheduled-input count is `C + possible`, in 0..2; this counts owner
  slots, not usable attacks. An enclosing rank interval can be conservative; these comparisons are
  exact at the extremes only when its endpoints are the actual feasible minimum and maximum ranks.
  If the owner's rank is also uncertain, range over its feasible ranks too. A hidden foe's current
  completion is unknown and requires the conservative current-input possibility. Future inputs get
  their future refreshed allowances; spent current M/A/C cannot erase them. Pending allies/foes may
  mutate next priorities; even a completed foe's committed priority does not fix its rank while its
  peers can change theirs. This is a finite schedule deduction, not a new native combat outcome.

  Opposing input opportunities and camp batches are different clocks. Through the next scheduled
  owner input in R+1, M2's pinned phase contract gives one camp batch even when `C + possible` is 2.
  Do not turn that two-input enemy envelope into two camp volleys. Longer paid exits, delayed next
  usable actions and suspension require their actual phase count; persistent effects are separate.

### C3. One-on-one at base stats: Heart, Marcella, Tavi, Vendetta and Ryth win most duels; casters lose
Claim: Base-stat duels, both champions using their full kits (288 engine games; margins as defined
  above, row against column):

  | | Ryt | Ves | Vnd | C&I | Ste | Pri | Mar | Wil | Ren | Var | Tav | Hea |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | Ryth | | +0.17 | +0.10 | +0.10 | +0.35 | +0.33 | -0.14 | +0.45 | +0.18 | -0.09 | -0.03 | -0.18 |
  | Vesper | -0.17 | | -0.23 | -0.65 | +0.22 | +0.14 | -0.31 | +0.28 | -0.05 | -0.32 | -0.24 | -0.31 |
  | Vendetta | -0.10 | +0.23 | | +0.24 | +0.45 | +0.25 | -0.10 | +0.48 | +0.15 | +0.01 | +0.06 | -0.03 |
  | C&I | -0.10 | +0.65 | -0.24 | | +0.66 | +0.61 | -0.26 | +0.74 | +0.46 | -0.07 | -0.21 | -0.30 |
  | Stellar | -0.35 | -0.22 | -0.45 | -0.66 | | -0.09 | -0.36 | +0.11 | -0.26 | -0.27 | -0.44 | -0.47 |
  | Prism | -0.33 | -0.14 | -0.25 | -0.61 | +0.09 | | -0.39 | +0.29 | -0.21 | -0.09 | -0.33 | -0.41 |
  | Marcella | +0.14 | +0.31 | +0.10 | +0.26 | +0.36 | +0.39 | | +0.50 | +0.26 | -0.00 | +0.22 | +0.01 |
  | Willow | -0.45 | -0.28 | -0.48 | -0.74 | -0.11 | -0.29 | -0.50 | | -0.36 | -0.40 | -0.51 | -0.60 |
  | Ren | -0.18 | +0.05 | -0.15 | -0.46 | +0.26 | +0.21 | -0.26 | +0.36 | | -0.24 | -0.03 | -0.23 |
  | Varg | +0.09 | +0.32 | -0.01 | +0.07 | +0.27 | +0.09 | +0.00 | +0.40 | +0.24 | | -0.18 | -0.29 |
  | Tavi | +0.03 | +0.24 | -0.06 | +0.21 | +0.44 | +0.33 | -0.22 | +0.51 | +0.03 | +0.18 | | +0.00 |
  | Heart | +0.18 | +0.31 | +0.03 | +0.30 | +0.47 | +0.41 | -0.01 | +0.60 | +0.23 | +0.29 | -0.00 | |

  - **Ranking** (mean margin, wins out of 11): Heart +0.26 (9), Marcella +0.23 (10), C&I +0.18 (5),
    Tavi +0.15 (9), Vendetta +0.15 (8), Ryth +0.11 (7), Varg +0.09 (8), Ren -0.06 (4), Vesper -0.15 (3),
    Prism -0.22 (2), Stellar -0.32 (1), Willow -0.43 (0).
  - **C&I is bimodal:** +0.6 to +0.7 against Vesper, Stellar, Prism and Willow, +0.46 against Ren;
    negative against the other melee kits (Ryth, Vendetta, Marcella, Varg, Heart) and Tavi, which reach
    Cassi's 600 health (M36: both bodies die with her).
  - **Mirrors and pairs within ±0.05** (Marcella-Heart, Marcella-Varg, Vendetta-Varg, Vendetta-Heart,
    Tavi-Heart, Tavi-Ryth, Tavi-Ren) are razor-thin: the winner keeps 0-11% of its health. In mirrors
    and in four of these pairs the winner flips with the round parity, i.e. with the first strike.
  - **Length:** first damage to first death takes a median 13 rounds (5-48; 90% within 21).
Evidence: run base (`data/combat/duels_base.json`, per-game margins included); policy FIGHT both;
  start 6 tiles apart on open ground, same turn slot.
Rests on: C1, C2, M23-M36.
Status: verified (engine 1.0.2), under these policies and open ground.
Recheck: any patch to champion stats or abilities (the 1.0.2 patch moved Stellar, Heart, Ren, Vesper,
  Vendetta, Tavi and Prism).
Consequence: the online meta five (Heart, Vesper, Marcella, Ryth, Vendetta) are four of the six best
  duelists plus Vesper, the weakest non-caster: Vesper is drafted for gold (+33%), not for fights, and
  must not be the champion left alone in contact. Stellar, Prism and Willow should never take an even
  one-on-one.

### C4. Why: effective health times sustained damage, corrected by first strike, stuns and front-loaded burst
Claim: Damage into a passive target per 8-round cycle in contact (model, equal to the engine): at base
  C&I 740 (two bodies), Vendetta 630, Tavi 559, Marcella 525, Ryth 505, Vesper 495, Willow 495, Prism 480,
  Stellar 478, Heart 390 (it rises with Heart's own missing health), Varg 360 (plus splash), Ren 355
  (plus retaliation); at H10 S6 F1 Vendetta 954, Ryth 875, Tavi 843, Marcella 825, Vesper 765, Heart (F11
  H5) 588 with a 408-damage opening pair of rounds. To first order X beats Y when X's effective health
  times sustained damage exceeds Y's, where effective health = max health + shield absorbed per cycle x
  cycles, divided by 0.9 with Durability (M40), and sustained damage is reduced by 1/8 per stun the
  opponent lands each cycle (none against Marcella, M35). The orders where this product misleads are the
  mechanisms that matter:
  - Heart's damage grows by up to 50% as its health falls (M29), and its dash stuns on landing;
  - C&I's effective health is Cassi's (M36);
  - shields (M30: absorb first, five owner turns) are worth more against low hits: Varg's 2.5xF = 250
    absorbs eight of Heart's 30-damage hits but under four of Vendetta's 70; Heart's 1.5xF = 150;
  - front-loading: every ability is ready at contact, so the first 1-2 rounds carry a cycle's burst
    (Tavi 289, Stellar 298, C&I 320 in the first two rounds at base);
  - in an even pair, one extra hit (3-7% of health) decides.
Evidence: `duelsim.py` dummy profiles (24 rounds into a passive 10^7-health target); C1 calibration.
Rests on: C1, M24-M36.
Status: model (the exact computation is C1; the product is a reading aid, not a predictor).
Recheck: as C1.
Consequence: to compare fighters cheaply, use (health + shields) x damage per round, with stun and
  first-strike corrections. To decide a real fight, simulate it.

### C5. At equal gold Health and Strength tie for fighting and Focus loses, except on Heart, Varg and C&I
Claim: Same champion, same gold, different stat (engine; mean margin of the first build over the
  second, both sides and parities):

  | champion | H5/S5 | H10/S10 | H15/S15 | H15/F15 | S15/F15 |
  | --- | --- | --- | --- | --- | --- |
  | Ryth | +0.09 | +0.09 | +0.17 | +0.53 | +0.45 |
  | Vesper | +0.01 | +0.03 | +0.04 | +0.59 | +0.56 |
  | Vendetta | +0.02 | +0.06 | +0.07 | +0.59 | +0.54 |
  | C&I | +0.64 | +0.66 | +0.66 | +0.70 | -0.03 |
  | Stellar | +0.22 | +0.32 | +0.38 | +0.21 | -0.51 |
  | Prism | +0.22 | +0.32 | +0.39 | +0.20 | -0.11 |
  | Marcella | -0.01 | -0.01 | -0.01 | +0.13 | +0.21 |
  | Willow | +0.03 | +0.08 | +0.10 | +0.51 | +0.53 |
  | Ren | +0.12 | +0.18 | +0.22 | +0.37 | +0.13 |
  | Varg | -0.43 | -0.56 | -0.57 | +0.37 | +0.71 |
  | Tavi | +0.02 | +0.03 | +0.03 | +0.33 | +0.30 |
  | Heart | -0.02 | -0.02 | -0.03 | -0.23 | -0.35 |

  Online-style 450-gold builds (Treasure/Health > Jungle/Strength > Bruiser/Health against Treasure/
  Strength > Jungle/Strength > Durability/Health, main's) tie within ±0.01 for Ryth, Marcella, Vendetta,
  Vesper, Willow and Tavi; Heart's Focus build (Treasure/Focus > Power/Focus > Durability/Health) beats its
  Strength build 4/4 (+0.17); C&I's Health build +0.67; Varg's Strength build +0.22.
  - **Law:** the product of health and damage grows fastest by buying the stat whose current value is
    the smaller multiple of its per-shard gain. Example: Vendetta's 1000 health is 10 Health shards'
    worth; its ~79 damage per round is 11.7 Strength shards' worth (6 + 6/8 per shard). So Health comes
    first, then the two alternate.
  - **Focus** scales only spell, shield and heal payloads (`ability.info()` modifier x Focus). That is
    15-35% of a cycle on most kits and nothing on Vesper, Vendetta and Varg; on Heart it scales the
    dash, the shield and its missing-health amplified hits.
  - **C&I** gains most from Health because Cassi is the body enemies kill.
Evidence: runs stat (432 games, builds 1-9 = 5/10/15 Monster shards of one stat, no fight passive) and
  online (48 games); `data/combat/stat_duels.json`, `builds_health_vs_strength.json`.
Rests on: C1, C4, M39, M40, M43 (Ryth gains 1% of bonus health as Strength).
Status: verified (engine 1.0.2), for fights only.
Recheck: shard growth or ability modifiers in a patch.
Consequence: for fighting, Health-first is not wrong and not a cure. Main's Strength-first builds duel
  as well at equal gold (online O4 head-to-head: 53%), so the online gap is gold, not build. The two
  costly fight builds are Heart on Strength (main buys 79-84% Strength on Heart) and Focus on a kit with
  no Focus-scaled damage. Health also absorbs camp damage and survives chases (C8); Strength also farms
  (economy).

### C6. At r300-400 builds the Focus Heart is the strongest duelist; C&I falls apart
Claim: Online builds (Heart F11 H5 with Durability, the others H10 S6 F1; 162 engine games, 9 kits):

  | | Ryt | Ves | Vnd | C&I | Pri | Mar | Ren | Tav | Hea |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | Ryth | | +0.23 | +0.13 | +0.46 | +0.53 | -0.06 | +0.22 | +0.09 | -0.22 |
  | Vesper | -0.23 | | -0.21 | +0.36 | +0.35 | -0.27 | -0.10 | -0.12 | -0.45 |
  | Vendetta | -0.13 | +0.21 | | +0.55 | +0.48 | -0.13 | +0.13 | +0.20 | -0.24 |
  | C&I | -0.46 | -0.36 | -0.55 | | -0.06 | -0.52 | -0.38 | -0.51 | -0.62 |
  | Prism | -0.53 | -0.35 | -0.48 | +0.06 | | -0.55 | -0.45 | -0.40 | -0.47 |
  | Marcella | +0.06 | +0.27 | +0.13 | +0.52 | +0.55 | | +0.22 | +0.17 | -0.07 |
  | Ren | -0.22 | +0.10 | -0.13 | +0.38 | +0.45 | -0.22 | | -0.02 | -0.38 |
  | Tavi | -0.09 | +0.12 | -0.20 | +0.51 | +0.40 | -0.17 | +0.02 | | -0.26 |
  | Heart | +0.22 | +0.45 | +0.24 | +0.62 | +0.47 | +0.07 | +0.38 | +0.26 | |

  - **Ranking:** Heart +0.34 (8/8), Marcella +0.23 (7/8), Ryth +0.17, Vendetta +0.13, Tavi +0.04, Ren
    0.00, Vesper -0.08, Prism -0.40, C&I -0.43.
  - **With H10 S6 F1 on every kit** (288 games, `duels_b11.json`): Marcella wins all 11 (+0.29), Ryth
    +0.26, Heart +0.26, Vendetta +0.22.
  - **C&I** drops from +0.18 at base to -0.30, because one champion's gold buys each body half a build
    and Cassi stays the weak link.
Evidence: runs metab and b11; `data/combat/duels_online.json`, `duels_b11.json`.
Rests on: C5, M29, M36, M39.
Status: verified (engine 1.0.2); C&I's cells assume the team's gold is split evenly between its bodies.
Recheck: as C3.
Consequence: Heart and Marcella are the duel picks whose strength grows with gold. Heart must be built
  on Focus. C&I is an early-game duelist (and a two-body farmer) whose fight value decays as gold
  accumulates.

### C7. Ranged champions cannot kite at equal speed; range buys about two free hits against dash-less melee
Claim:
  - **Free hits on approach:** against melee without a dash (Ryth, Marcella, Varg) a ranged champion
    lands its first damage 1-3 rounds before the melee's first hit (mean 1.9; 60 games). Against
    Vendetta, Ren and Heart (dash 2 + step 1 + hit from 4 tiles, M25, M28) the lead is -1 to +1 (mean
    0.3).
  - **Kiting loses:** stepping away whenever a melee is within 2, on open ground, does worse than
    standing in 24 of 36 ranged-melee pairings and makes fights 1.5-2x longer. Examples: Vesper vs
    Vendetta -0.23 → -0.71, vs Heart -0.31 → -0.72; Tavi vs Heart 0.00 → -0.37; Willow loses everything.
  - **Where it helps:** C&I (Issac keeps fighting while Cassi kites) and Tavi against Ren (+0.14).
  - **The exception:** Vesper's dash against Varg, who has neither a dash nor a stun, kites to a
    flawless win (+1.00 against -0.32 standing).
  - **Mechanism:** a ranged champion's step costs its attack (M23), while a same-slot melee chaser still
    hits every second round and a different-slot one every round (C2, C8).
Evidence: base matrix against run kite2 (144 games, kiting towards a neutral corner; an earlier run
  that kited towards the kiter's own spawn was discarded: spawn healing and resting fillers decided it).
  `data/combat/kite.json`.
Rests on: C2, C8, M23, M25, M28.
Status: verified (engine 1.0.2), open ground, same turn slot (the case most favourable to the kiter).
Recheck: movement, dash or attack-allowance changes.
Consequence: ranged champions take position before contact and stand. They break contact only with a
  dash against a chaser that has neither a dash nor a stun in range 4. They never "kite" Vendetta, Ren,
  Heart, Marcella or a stunner. Their real defence is not being reached: allies in front, distance
  before contact.

### C8. Escape: an equal-speed chaser keeps contact and lands full or half damage by slot; dashes decide
Claim: In engine chases on open ground, the fleer starts adjacent to its chaser, L diagonal tiles from
  its own spawn (spawn tiles heal fully each turn and are closed to enemies, M18). The health a fleer
  loses per round of retreat (the *chase rate*, base stats):

  | chaser | kit that keeps contact | same slot | different slots |
  | --- | --- | --- | --- |
  | Vendetta | dash, reset | 52-65 (mean 59) | 75-90 (82) |
  | Marcella | stunning reset, 2xF spell | 35-60 (50) | 51-75 (67) |
  | Ryth | area stun, reset | 34-46 (40) | 51-66 (57) |
  | Heart | stunning dash | 35-51 (44) | 48-60 (53) |
  | Ren | stun, dash | 33-41 (38) | - |
  | Varg | reset only | 26-33 (29) | 45-51 (48) |
  | Tavi (ranged) | stun spell | 21-44 (39) | - |
  | Vesper (ranged) | dash, reset | 14-25 (20) | - |

  Rates are against dash-less fleers (Ryth, Marcella, Tavi, Stellar) over L = 12 and 24, both parities.
  A different-slot melee chaser deals about its full sustained damage per round (C4); a same-slot one
  60-85% of it (half its hits, plus the dash, stun and reset extras).

  - **A fleer with a ready dash** breaks contact for good with any chaser that has none: Vesper,
    Vendetta and Heart lose 0-8% of their health fleeing Ryth, Marcella or Varg over 24 rounds. Against
    a dash chaser they fare like dash-less fleers.
  - **The recorded retreat averages describe these sampled complete chases.** They are not upper
    bounds on damage in a shorter prefix or a different equipped state (C22).
    - Same slot, L = 24: Stellar dies to every melee chaser but Varg; Tavi to Vendetta, Marcella,
      Heart, Ren and Tavi; Ryth to Vendetta, Marcella and (once in two) Heart; Marcella to Vendetta.
    - Same slot, L = 12: only Stellar dies (to Vendetta and Marcella).
    - Different slots, L = 12: Tavi dies to Vendetta and Marcella. With the fleer in the lower slot,
      Ryth and Marcella die to Vendetta too.
  - **Mechanism:** at equal walking speed distance never grows by walking (G3). A same-slot pair's double
    turns let the fleer take two steps between the chaser's hits; strict alternation does not. Dashes
    (2 tiles), stuns (a lost step) and resets (a second hit) are the asymmetries.
Evidence: runs chase (528 games: 8 chasers x 7 fleers x L 3/6/12/24 x 2 parities, plus FLEESTUN) and slot
  (160 chases with team B's champion in slot 3, fleeing from either side); `data/combat/chase.json`
  (escaped, rounds, health taken, rate per row).
Rests on: C2, M18, M23, M25, M26, M28.
Status: verified (engine 1.0.2) on open ground. Terrain is unmeasured here: walls that funnel, holes a
  dash crosses (M28), and bushes, which hide from champions but not from monsters (M46, M12).
Recheck: movement, dash, stun or spawn-healing changes.
Consequence: These averages estimate the cost of comparable long chases; multiplying them by travel
  time is not a sufficient escape condition. C22 gives a counterexample even at base stats. A proposed
  escape must survive every actual ordered damage prefix until its physical endpoint, with actual
  abilities, actor slots, movement, shields and timely support. A ready dash is useful only if its
  actual route and later continuation establish separation. Online (O13): T1 fights end in separation 75% of the time and in death 13%, and victims die
  after a median 18 rounds of contact from full health with 1.5-1.7 enemies within 3 (O14). Two chasers
  at 50-75 each take 100-150 health a round, so 12-18 rounds of contact kill a 1,500-1,800-health
  champion: the online deaths are retreats that started too late, against chasers that keep contact.

### C9. A retreating melee champion should keep hitting: it costs nothing
Claim: Melee fleers that strike their adjacent chaser before stepping (FLEEHIT) lose the same health as
  pure fleers (32 of 32 scenario pairs, within 3%) and deal 500-700 to a melee chaser over a 24-round
  retreat: Ryth fleeing Ryth 585-630, Marcella fleeing Marcella 585, Vendetta fleeing Vendetta 700, Ryth
  fleeing Vendetta 495-540. Melee champions step and attack in one turn (M23). A ranged fleer's shot
  costs its step.
Evidence: run fleehit (80 games) against the same keys in run chase; chaser health from the bots' logs.
Rests on: M23, C8.
Status: verified (engine 1.0.2).
Recheck: attack-allowance rules.
Consequence: every melee retreat attacks whenever it starts a turn adjacent. A retreat that ends in
  death has then at least traded, and one that survives leaves the chaser weaker for the next contact.

### C10. Stuns cost one turn and never stack; k staggered stunners lock k turns in 8; no perma-lock
Claim:
  - **One turn per stun:** any number of stuns landing between two turns of a victim cost it one turn
    (M26).
  - **Engine, a 2,700-health passive victim:**
    - three stunners (Tavi, Prism, Cassi) casting freely cost it 1-2 turns per 8-round cycle;
    - holding a stun while the victim's `buff_stun.duration` is above 0 (STAGGER) costs it 3
      consecutive turns per cycle (S S S . . . . .);
    - five staggered stunners (adding Ren and Ryth) cost 5 consecutive turns; uncoordinated, 3-4.
  - **No perma-lock:** each stunning ability recharges in 8 owner turn ends (M24) and a team has at most
    5 picks (Tavi, Prism, C&I, Ren, Ryth, Heart and Marcella stun). A victim can be locked for at most 5
    of every 8 turns; a permanent lock would need 8 stunners.
  - **Escapes:** a walking-exclusive stun on the chaser (Tavi, Ryth, Prism, Ren) did not help, because
    the cast costs the fleer's step (M25). Tavi fleeing Vendetta at L = 12: escaped 2/2 with 78% of its
    health lost, against 1/2 with 93% when stunning. Cassi's walk-compatible stun let C&I escape every
    chaser at L = 12 with 0-8% lost.
Evidence: run stun (tables 16-19 of `_duel-lab/src/table.h`, 8 games, stunned turns in
  `data/combat/groups.json`); run chase (FLEESTUN rows).
Rests on: M2, M24, M25, M26, M35.
Status: verified (engine 1.0.2).
Recheck: stun durations or cooldowns in a patch.
Consequence:
  - **Stagger:** read the target's stun duration (visible, M45) before casting; a stun on a target that
    is already stunned is wasted. Order stunners by priority a round ahead (M2).
  - **Value:** in an exchange a stun is worth one victim turn, about its damage per round plus a delay of
    its casts. In a kill it is a window of k turns with no damage and no escape. In an escape only
    walk-compatible or allied stuns help.
  - **Channels:** any stun also breaks a recall, tribute or crystal channel (M27).

### C11. Numbers: the square law holds in the engine; even fights are coin flips that wreck both sides
Claim: Identical units (Ryth and Marcella have identical basic stats; basic attacks, focus fire, full
  contact; engine up to 3 per side, model beyond). Cell: survivors of the winning side and their share
  of its health.

  | N \ M | 1 | 2 | 3 | 4 | 5 |
  | --- | --- | --- | --- | --- | --- |
  | 1 | coin flip, 3% | lose | lose | lose | lose |
  | 2 | 2 alive, 74-76% | coin flip, 3% | lose | lose | lose |
  | 3 | 3 alive, 87-89% | 2 alive, 65-67% | coin flip, 1-2% | lose | lose |
  | 4 | 4 alive, 93% | 4 alive, 80% | 3 alive, 57% | coin flip, 1% | lose |
  | 5 | 5 alive, 95% | 5 alive, 87% | 4 alive, 74% | 3 alive, 52% | coin flip, 1% |

  - **Survivors** ≈ sqrt(N² - M²), rounded up (Lanchester's square law): 3v2 2.2 → 2, 4v3 2.6 → 3,
    5v4 3 → 3, 5v3 4 → 4.
  - **Full kits in 2D** (engine): the meta five mirror leaves the winner 2 of 5 (30-32% health) and the
    winner flips with parity; 5 against 4 leaves 3 (35-50%), 5 against 3 leaves 4 (80%); a 3v3 meta mirror
    leaves 2 (42-46%), 3 against 2 leaves 2 (54-56%); Marcella + Vendetta beat Heart with 72-74% left.
Evidence: run g1 tables 0-5 and 9-15 (`data/combat/groups.json`); model table from
  `duelsim.Fight` with identical Ryth units.
Rests on: C1, C2, C12.
Status: verified (engine 1.0.2) to 3v3 and for the full-kit rows; model beyond.
Recheck: any change to health or damage scaling (laws), or to kits (full-kit rows).
Consequence: G4 holds in the arithmetic. One extra champion turns a coin flip into a win that keeps most
  of the team. An even fight's expected result is losing most of both sides, which is worse than not
  fighting for the side that is ahead. Count every enemy that can arrive within the fight's length
  (contact fights last 6-20 rounds: anyone within about 6-15 tiles).

### C12. Focus fire is worth about one champion
Claim: Focus against spread (engine, basic attacks, identical units):
  - **2v2:** the focusing side wins with 27-29% of its health, against 3% in a focus mirror.
  - **3v3:** it wins with 45-51% and loses nobody.
  - **3v2:** when the larger side spreads it still wins, with 56% instead of 65-67%.
  - **Model, 5v4:** the five win with one survivor (13%) instead of three (52%) when they spread.
Evidence: run g1 tables 6-8; model rows from `duelsim.Fight` (policies BASICSPREAD/BASIC).
Rests on: C11.
Status: verified (engine 1.0.2) to 3v3; model beyond.
Recheck: as C11.
Consequence: all damage goes on one target, the one that dies soonest among those in reach; main
  already does this (D10). Defensively, the champion everyone can reach first is the one that dies:
  rotate the front.

### C13. Group fights: damage per round, time to kill an isolated champion, who dies first
Claim:
  - **5v5, full contact** (model on a line): team damage is 250-370 per round at base and 390-630 at
    online builds over the first 10 contact rounds. A 1,000-1,500-health champion focused by five dies in
    2-3 rounds.
  - **Meta five mirror (engine):** the first death comes 5-7 rounds after the start, and it is Heart,
    whose dash puts it in front, in both teams. Then one champion dies every 2-4 rounds.
  - **Isolated champion** (model, online builds, attackers starting in contact; both parities):

    | attackers | rounds to kill a lone Ryth / Vesper / Marcella / Heart / Tavi / Stellar |
    | --- | --- |
    | Vendetta alone | survives / 17 / survives / survives / 16 / 11 |
    | Marcella + Ryth | 11 / 9 / 10 / 10-12 / 8 / 6 |
    | Marcella + Ryth + Vendetta | 8 / 6 / 7 / 7 / 5 / 3 |
    | meta five | 4-5 / 3 / 4 / 2-3 / 2 / 2 |
Evidence: `duelsim.py` (5v5 per-round damage, isolation table); run g1 tables 9-11 (engine 5v5, 5v4,
  5v3).
Rests on: C1, C11, C12.
Status: model; the 5v5 death order and timing are verified (engine 1.0.2).
Recheck: as C3.
Consequence: online kills are slower than this (O14: 100-200 damage per round over the last 6 rounds,
  1.5-1.7 enemies within 3), because real fights move (C7, C8) and rarely bring everyone into contact.
  The isolated champion is the natural victim: two enemies in contact kill it in 6-12 rounds, faster
  than it can walk home (C8).

### C14. Area damage scales with clumping; two tiles of spacing defeat it
Claim: On three adjacent passive champions (engine):
  - Heart's dash hit all three (150 each plus a stun), and the next dash two (the third tile was not
    reachable);
  - Stellar's area spell hit all three for 98 each, Prism's for 80;
  - Varg's splash hit only enemies adjacent to its target (40).

  Payloads at base (at 15 Focus shards), per target: Heart dash 1.5xF = 150 (420) plus a stun, Stellar
  1.4xF = 98 (245), Prism 1xF = 80 (200), Ryth 1xF = 100 (250) plus a stun; all radius 1 (Chebyshev),
  and none hits allies (M25). Against a clump of three, a Focus Heart's opening dash deals about 1,000
  and stuns all three: a 3-turn lock of a whole front line.
Evidence: run g1 tables 20-23 (`data/combat/groups.json`), catalog payloads.
Rests on: M25, M29, M33.
Status: verified (engine 1.0.2).
Recheck: area radius or modifiers in a patch.
Consequence: against Heart, Stellar, Prism, Ryth or Varg, allies in contact keep at least 2 tiles
  between them; with those kits, fight where the enemy is forced together (chokes, around a camp they
  are clearing).

### C15. The lowest-health support is the natural focus: Willow lost every 2v2
Claim: In engine 2v2s, a side with Willow lost all six games: Vendetta + Willow against Vendetta +
  Vesper, Marcella + Willow against Marcella + Tavi, Varg + Willow against Varg + Vesper. Willow (700
  health) died first, 7-10 rounds in, each time. Her heal is min(3xF, missing health) = 360 per 8 rounds
  on an ally (M31): about 45 health per round of sustain, against the ~60 per round a damage dealer adds
  (and this bot heals only when 60% of the heal would land).
Evidence: run g1 tables 24-26.
Rests on: M31, C12.
Status: verified (engine 1.0.2), in 2v2 on open ground.
Recheck: heal modifier or Willow's stats.
Consequence: a healer pays only behind a front line that keeps it out of the enemy's reach (more than 4
  tiles from every enemy), which needs numbers. In small fights, a damage dealer is worth more.

### C16. First-strike discipline: hold outside the enemy's two-turn reach; two holders never fight
Claim:
  - **Reach:** a champion's strike reach is the distance from which it can damage this turn: walk 1 +
    melee hit = 2; with a ready dash 4 (dash 2 + step 1 + hit); ranged 3, or 4 with a ready spell or
    reset (walk, reset, attack).
  - **Naive holding fails:** holding at the enemy's reach + 1 loses mirrors to plain approach (Ryth,
    Vendetta and Marcella 0/4). The approacher crosses the last tile at the end of its double turn and
    strikes on the next one (C2).
  - **Parity-aware holding** (STANDOFF): hold at the enemy's reach + 2 when the enemy acts twice before
    your next turn (same slot, you first in this round), + 1 otherwise; strike when the enemy is inside
    your reach. Against plain approach this improved 63 of 132 non-mirror pairings (5 worse, mean
    +0.04), flipped 6 results in its favour (Ryth over Varg and Tavi, Vendetta over Heart, C&I over
    Vendetta and Tavi, Varg over Tavi), and won mirrors 4/4 for Ryth, Marcella, Stellar, Willow and
    Heart. Vesper, Vendetta, C&I, Prism, Ren, Varg and Tavi gained nothing in mirrors: 2/4, a coin flip.
  - **Two holders never fight:** 24 of 24 mirrors between two holders ended with no death.
Evidence: runs standoff (600 games, naive) and standoff3 (576, parity-aware);
  `data/combat/standoff.json`.
Rests on: C2, M2, M23, M25, M28.
Status: verified (engine 1.0.2) for the listed kits; the rule as coded misjudges some dash and
  stunner timings.
Recheck: as C2.
Consequence (safeguard): never end a turn inside an enemy's two-turn reach unless you want that fight.
  The side that wants a fight has to give the first strike or win the slot timing; the side that does
  not can always refuse, so an even fight happens only if someone errs.

### C17. The value of a kill and of a death
Claim: A death costs its team about 80 champion-rounds of production: 60 rounds dead (M22) plus the
  walk back, since contact is mid-map (O12), 10-35 rounds. At T1 farm rates (O2, O8: earned gold per
  alive champion-round 0.72 over r1-100, 1.1 over r200-400, 1.64 over r400-1000; collected gems 0.126 per
  alive champion-round over r1-400) that is:

  | death at | gold lost | gems lost | plus |
  | --- | --- | --- | --- |
  | r100 | ~60 | ~10 | the bag to the killer, 100 gold to the killer |
  | r300 | ~90 | ~10 | T1 carries a median 30 gems at r300 (O11) |
  | r600 | ~130 | 10+ | |

  - **Killer's gain:** `constants::kill_gold` (100) scaled by its gold bonuses (Vesper x1.33, Treasure
    +0.2; M37), plus the whole bag (both bags for C&I, M36). Assists get nothing (M22); a camp kill
    destroys the bag.
  - **Swing at r300 on a 30-gem carrier:** about 190 gold and 70 gems between the teams, 7% of
    `winning_gems`. The bag dominates: 30 gems are about 240 champion-rounds of a T1 champion's gem
    collection.
  - **Cost of a fight:** a median T1 fight lasts 22 rounds (O13). With two champions that is about 44
    champion-rounds of farming, half a death.
  - **In gems** (economy E25): about 35-45 gems plus twice the bag. The lost production is 14-27 gems at
    banked rates, and the kill gold is about 20 gems as two thirds of an emblem. This derivation is the
    same order: 10 gems of collection plus 190-230 gold of swing (lost production and kill gold) at
    E25's ~0.2 gems per gold is 45-55 gems. E26: a 10% income edge is worth 2-3 such kills over a game.
Evidence: derivation from M22, M36, M37 and online O2, O8, O11, O12, O13 (rates of T1-T1 games);
  economy E18, E25, E26.
Rests on: M22, M36, M37, E18, E25, C21 (who dies).
Status: model (rates are online medians; outcome-laden, O2); consistent with E25.
Recheck: kill_gold, respawn_time or bag-transfer changes.
Consequence:
  - **Fight value** ≈ P(kill) x kill value − P(death) x death value − participants x rounds x farm rate.
    P(death) per contact round comes from C21: about 0.6% with one enemy within 3, 1.8% with two or more.
  - **Carriers:** a carrier is the prize and should not be in contact (O11). Banking before a fight
    removes most of a death's swing.
  - **Empty bags:** a kill on an empty-bag champion late in the game is worth about its 130 gold of lost
    production plus 100 gold, which is why gold beats kills when they disagree (O9).

### C18. Monsters in fights: the nearer champion pays, so waking a camp the enemy is nearer to wins
Claim:
  - **Rules:** camps act at round start, before champions (M1). Each awake monster hits the nearest
    champion of either team in its range, re-chosen every round, ties to the earliest actor (M5, M6),
    once per round (M8). Damage per round = `camp_stats` Strength: wolf 20, elk 60, golem 120, monolith
    200. An awake elk is an extra attacker the size of a base Ryth (63 per round, C4); two elks out-damage
    any champion.
  - **Engine** (an elk pair on the centre line, the duel held 6 tiles from it):
    - Vesper woke the pair from range 3 while Vendetta, approaching, was nearer to it. A 23%-health loss
      became a win with 37-44%: Vendetta lost 600 health to the elks in five rounds, Vesper none.
    - Tavi against Ryth went from a 3% win to a flawless one.
    - Marcella, who had to walk adjacent to wake the elks, died with her opponent.
  - **Side effects:** Ren retaliates against monsters (M34); a stunned monster skips its next attack
    (M26); bushes do not hide from monsters (M12).
Evidence: run camp (12 games, tables 27-32, map terrain "elk"); `data/combat/groups.json`.
Rests on: M1, M5, M6, M8, M26, M34.
Status: verified (engine 1.0.2), one geometry.
Recheck: monster targeting rules or camp stats.
Consequence:
  - **Accounting:** count each awake monster in range as a combatant against whoever will be nearest
    to it.
  - **Pulling:** fight with the enemy between you and an awake camp; wake a camp only when the enemy is
    nearer to it than you.
  - **Never** fight inside a camp's range while nearer than the enemy (main's collapse mechanism 3).

### C19. Where main's fight model and the strategy hypotheses are wrong
Claim: Read against the laws above:
  1. **Every fighter's damage every round.** `model::simulate_skirmish` applies each fighter's damage
     every round. In the engine the hit rate depends on movement and turn slots: a moving target takes
     half from a same-slot melee chaser (C8); a ranged champion deals nothing on a moving turn (C7); an
     approach costs 1-3 rounds before contact. This is the structural reason main measured fights
     resolving about 3x slower than predicted and deaths predicted 3.6x too often
     (`.cache/main/analysis/fights/report.md` §1).
  2. **Retreat and escape.** Main's retreat picks the nearest tile out of reach and has no escape
     condition. At equal speed the gap never grows except through a dash against a dash-less chaser
     (C8), so its "retreats that never end" (47% of trap-map deaths) are the expected result. C8's
     condition and C9's free hits answer them. D92's finding, that adjacency matters and destination
     does not, is C8's rate x rounds.
  3. **Stuns.** Main's model counts casts as damage. A stun is a lost victim turn, wasted if it overlaps
     another (C10). The fight report's "stuns matter for escape and burst" holds for burst; for escape
     only walk-compatible or allied stuns help, as main's rejected stun-the-chaser retreat (D47) found.
  4. **Even trades.** Main's "better-than-even trades only" (D21) is right by C11 and C16: even fights
     are coin flips decided by first strike, which a volley model cannot see.
  5. **One arrival flips a fight** into a rout (C11). Main's commit-then-flip deaths (collapse
     mechanism 2) are this; the fight must count enemies that can arrive within its 6-20 rounds.
  6. **Camps.** The model has no monster term (collapse mechanism 3); C18 supplies it.
  7. **Heart's build.** Main's Heart buys 79-84% Strength. C5 and C6 measure the cost: the Strength
     Heart loses its Focus mirror 4/4 at 450 gold.

  Strategy corrections:
  - **G3's mechanism:** "an adjacent melee chaser strikes a retreating target every turn" holds for
    different-slot pairs; same-slot pairs strike every second round (C2, C8). The conclusion holds:
    walking never opens distance.
  - **G4:** confirmed and quantified. In the engine: survivors ≈ sqrt(N² − M²), focus worth a champion
    (C11, C12). Online: a second enemy within 3 triples the death hazard, and allies within 6 cut it
    2.4-fold (C21).
    - Cohesion as such does not win (O15): grouping costs farming throughput (E8) and top teams rarely
      fight at full contact (O13: 75% of fights end in separation).
    - The law that completes G4: numbers decide fights that happen, so a team needs its numbers only
      where contact can happen, i.e. inside the enemies' two-turn reach plus the arrival time (C11,
      C16). Everywhere else grouping is pure throughput cost.
  - **E22 (optimal bag):** the hazard rises with contact, not with the bag (C21).
Evidence: entries cited; main's `bots/main/main/src/model.h`, DECISIONS D10, D21, D47, D92-D94;
  `.cache/main/analysis/fights/report.md`, `collapse/report.md`; economy E8, E22.
Rests on: C1-C18, C21.
Status: derived from verified entries; main's numbers as reported by main.
Recheck: when main's model changes.
Consequence: a contender's fight model needs per-pair turn order (C2), movement-dependent hit rates (C7,
  C8), stuns as turn denial (C10), arrivals within the fight's length (C11) and monsters (C18). The exact
  exchange arithmetic is the easy part (C1).

### C20. Contact safeguards remain conditional hypotheses
Claim: The following are historical candidate contact heuristics. They are not guarantees against
  every opponent: C22 refutes the former average-rate escape test, and each action still needs its
  current information, geometry, equipment, actor order and executable continuation established.
  1. **No contact without a way out.** Before entering any enemy's two-turn reach (C16) one of these
     holds:
     - a ready dash whose actual route, landing and later continuation establish separation under
       the admitted chaser actions and occupancy/refusal assumptions (C8);
     - positive health at every ordered damage prefix under the admitted chaser continuations,
       including double turns before the next usable owner input (C22); C8 averages cannot prove it;
     - a fight the exact model (C1) calls won, counting every enemy that can arrive within its length
       (C11).
  2. **Never the lone champion within reach of two enemies.** It dies in 6-12 rounds and cannot walk
     home (C13, C8).
  3. **Melee retreats hit back; ranged champions stand** (C9, C7). Kite only a chaser with neither a
     dash nor a stun, and only with a dash.
  4. **Focus one target and stagger stuns** by reading the target's stun duration (C12, C10).
  5. **Keep allies 2 tiles apart against area kits** (C14).
  6. **Never be nearer than the enemy to an awake camp in a fight** (C18).
  7. **Carriers stay out of contact; contact, not carrying, is the risk.** Bank or break off when an
     enemy enters the two-turn reach, and carry freely otherwise (C17, C21).
  8. **Don't take even fights.** Hold outside the enemy's two-turn reach and let it choose; an even
     fight then happens only if it errs (C11, C16).

  Online, 91% of champion kills happen with an enemy within 3 and 1% with none within 6 (C21).
  This descriptive concentration does not establish a bound on an individual continuation's risk.
Evidence: the entries cited. The rules have not yet been tested together in full games.
Rests on: C7-C18, C21.
Status: conditional hypotheses; former average-rate sufficiency claim refuted by C22's mechanics
  counterexample. Their combined effect on win rate remains untested.
Recheck: any change to the entries they rest on.
Consequence: each contender should state which of these it enforces as invariants, unit-test them and
  witness them in games (charter, "Safeguards").

### C21. Online, the death hazard is a function of contact, health and allies, not of the bag
Claim: The hazard is the chance that a champion alive at its turn start dies to a champion in that
  round. Data: 1,000 online games (all tiers), 9.8 million champion-rounds, 5,960 champion kills.

  | state at the turn start | share of rounds | share of deaths | deaths per 1,000 rounds |
  | --- | --- | --- | --- |
  | no enemy within 6 | 77.5% | 0.9% | 0.007 |
  | enemies at 4-6 only | 15.7% | 8.3% | 0.32 |
  | one enemy within 3 | 5.5% | 50.7% | 5.7 |
  | two or more within 3 | 1.3% | 40.1% | 18.2 |

  - **The bag does not raise the hazard.** Given the contact state, the hazard is flat in the bag:
    5.8 / 5.3 / 5.6 / 5.3 / 4.6 / 6.1 per 1,000 rounds with one enemy within 3, for bags 0, 1-9, 10-19,
    20-39, 40-79 and 80+. With two or more within 3: 18.5 / 18.5 / 17.1 / 17.5 / 15.5 / 17.9.
    Carriers are within 3 of an enemy less often (5-6% of rounds, against 7.6% with an empty bag), so
    their unconditional hazard is lower (0.36-0.46 against 0.71).
  - **Health and allies** multiply it. One enemy within 3: 1.2 at 70%+ health, 13.2 below 40%. Two or
    more within 3: 2.3 at 70%+, 35 below 40%. Two or more within 3 with no ally within 6: 29; with one
    ally 17; with two or more allies 12.
  - **By tier**, in contact (one / two or more within 3): T1 3.6 / 14.4, T2 4.4 / 14.5, T3 5.8 / 18.8,
    us 6.5 / 22.0.
Evidence: `tools/combat/hazard.py` over the online agent's replay tracks (`.cache/contenders/online/
  tracks/`, turn-start samples) and the match summaries' death events with context "champion";
  `data/combat/hazard.json`.
Rests on: O13, O14 (the same games), C2 (turn-start samples).
Status: measured (association: health below 40% in contact is mostly the last rounds before a death,
  not a cause).
Recheck: new online data, or any change to movement, stun or respawn rules.
Consequence:
  - **For economy E22:** the death rate in the optimal-bag formula is not constant, but what raises it
    is contact, not the bag. Online, carriers are not hunted measurably more. So carry while no enemy
    is within 6, where the hazard is 0.007 per 1,000 rounds and b* is effectively unbounded. Bank, or
    break contact, as soon as an enemy is within its two-turn reach (C16), where the hazard is about
    1,000 times higher. A bot that reads visible bags (M45) and hunts carriers would change this; the
    online field does not do it yet.
  - **For fights:** the second enemy within 3 triples the hazard, and allies within 6 cut it 2.4-fold.
    This is C11's square law seen from the victim's side, and G4 measured online.
  - **For E4's cover:** the cover argument is exact against monsters (they hit the nearest champion,
    M5). Against enemy champions it holds only when the enemy prefers the nearer target; what the data
    shows is that allies nearby lower the hazard in contact.

### C22. Ordered burst invalidates average-rate escape guarantees
Claim: An empirical chase rate times travel rounds is not a sufficient escape bound, even when the
rate is the upper endpoint in C8 and the actors have base equipment. Damage must be affordable at
every ordered primitive prefix before the endpoint is actually reached.

Counterexample model: a legal floor corridor has the fleeing team's spawn at x=0, an injured target
at x=2 with 110 HP, and an enemy base Varg at x=3. The target needs two ordinary steps to its spawn.
They have the same team slot; the target acts first this round and Varg acts first next round. Varg
has Strength40, range1, movement1 and a ready ordinary reset. There is no other damage, mitigation,
healing, stun, dash, blocking body or relevant intervention; the target follows its two-step route.

| Ordered boundary | Action | Target HP |
| --- | --- | --- |
| Target input r | Walk x=2 -> x=1 | 110 |
| Varg input r | Walk x=3 -> x=2; basic; reset; basic | 70, then 30 |
| Varg input r+1, before target's second input | Basic | 0 (10 overkill): death |

The target dies before its second step and spawn healing. It passes both the same-slot upper average
test `110 > 33*2` and the more conservative different-slot upper average test `110 > 51*2`.
The average is diluted over longer chases and does not cap this first two-input burst.

Evidence: deduction from M2, M18, M23-M24 and the pinned `docs/reference/runtime-catalog.json`,
champion `varg` (id10): Strength40, melee range1, movement1, slot0 ordinary attack reset with cooldown8.
This is an explicit model counterexample, not a new engine 1.0.3 native measurement. The old chase
table remains descriptive evidence on its recorded keys, with its original version and coverage.
Rests on: actor order, melee walk/basic compatibility, ordinary reset, delayed spawn recovery.
Status: derived counterexample. Native refinement remains open: seed this injured corridor state,
retain the 40/40/40 damage and owner-input boundaries, then compare opposite parity and reset-disabled
controls. Do not relabel the arithmetic as a completed native experiment.
Recheck: scheduling, movement/basic interaction, reset guard or current equipped Varg stats change.
Consequence: Evaluate actual HP/shield/heal state in engine order at every admitted enemy prefix.
A sufficient scalar bound needs a certified prefix-damage envelope, with only timely guaranteed
support credited. A proposed escape also needs executable movement under its occupancy/refusal and
bounded-computation assumptions. Nearby allies, time averages and the existence of a geometric path
cannot supply those missing premises.

### C23. Admit the complete word after its effects change hazards and legal continuation
Claim: A pre-word survival or routing filter using unchanged hazards can reject a feasible word
whose own effects remove those hazards or free its next destination. Admission must evaluate the
ordered work effects and its actual continuation together, while retaining every immediate and
external damage prefix. This is a completeness issue in the supported operator family, not a reason
to ignore legality or call an unmodeled word safe.

For one body in one fresh owner input, the restricted successful-primitives family has a small
resource automaton: actual M/A/C, remaining cooldowns/restrictions, successful ordinary-walk
provenance, and any movement-exclusive-cast lock not already reflected in effective M. Geometry,
target life, buffs and effect state remain additional inputs. It permits at most one ordinary move,
one successful cast and two basics if that cast
resets attack: at most four successful primitives. Failed/refused attempts, wards, purchases,
consumables, channels and priority actions are outside this bound and need their own bounded work.
Vendetta's cooldown refresh does not grant a second cast in the same input. A reset restores only A,
not M or the fact that ordinary walking occurred (M24, M53). Suspension invalidates the input frame.

| Supported word | Actual allowance condition |
| --- | --- |
| Melee Move -> Basic -> Reset -> Basic | Movement preserves A; the single reset restores the spent A. |
| Ranged Move -> Reset -> Basic | Move spends M+A; reset restores A and consumes C. |
| Basic -> Dash, or Dash -> Basic | The actual dash must be compatible, ready and use the one C; query live attack/landing permissions. |
| Compatible melee Dash + ordinary Move + Basic | Any admitted order must preserve actual per-primitive legality; dash consumes C, not ordinary M. |
| Vendetta Move -> lethal Basic -> Dash | A credited kill refreshes Dash cooldown; C must still be unused and the actual post-kill landing legal. |

The last family is effect-dependent even when root resource checks reject Dash. Let Vendetta have
ready M/A/C, an initially cooling Dash, and a visible camp outside basic range but reachable by one
ordinary move followed by a lethal Basic. Move -> Basic kill -> Dash can then use the refreshed
cooldown for exit. Move -> Dash -> Basic cannot cast the still-cooling Dash before the kill, and
Basic -> Dash -> Move cannot begin with the out-of-range Basic. A legally used Reset before the
killing Basic consumes C; the kill's cooldown refresh does not permit a subsequent Dash. This class
follows from pinned `GameRules.md:613`, M23-M25/M32 and the compatible dash kit. It is not a new native
witness. Qualification must join the actual credited kill, refreshed cooldown, remaining M/A/C and
accepted exit; predicted lethal damage alone grants none of those receipts.

Minimal effect-conditioned counterexample: a melee body has HP10 and ready M/A. Its only potential
attacker is one visible, isolated wolf with exact HP35, Strength20; the body's actual basic damage is
40. The wolf is the sole living member of its group, and a finite short delivery route reaches an
unlocked altar before the horizon. There are no other hazards, retaliation, hidden bodies or active
effects during the uninterrupted owner input or its declared continuation. A gate requiring
affordability of the unchanged20-damage next camp phase rejects before work search. Actual lethal
Basic removes the only attacker immediately; M remains available and the former camp cell is now a
free floor destination. Under the stated model, Basic -> ordinary move onto that cell -> uncontested
capture -> quiet delivery/bank is feasible. This is a model counterexample, not a new seeded native
game or proof that this state occurs in a retained contender replay.

The effect transition must include the entire relevant footprint. Varg splash can wake other groups;
a nonlethal hit can add rather than remove incoming damage. A stun can remove a priced action only
when actual applied duration and camp/owner phase ordering cover that action. Shields need their
actual value and expiry. Killing an occupying unit can free a later landing, whereas a movement-
exclusive cast can forbid the ordinary step even after its target dies. Sequential float32 damage
and target survival must be established rather than guessed from `hits*damage`. Price immediate
effects such as retaliation before using the new state for external responses.
Camp phases do not occur between uninterrupted successful primitives in one owner input (M1).
A one-command-then-yield/reobserve service class can legitimately charge that later camp interval,
but that is a different operator from a complete same-input word. It cannot establish that a
Cast -> Basic or Basic -> Reset -> Basic word needs a camp volley between its own commands.
Charge any real refusal, deliberate deferral or computation-suspension seam that can insert a phase;
ordinary same-input observation alone does not insert one.

Evidence: M9, M23-M26, M28, M30, M33-M34, M48 and M53; the source-specific contender composition audit
identifies missing own-word families and an unchanged-support gate before Mosaic word search.
G11's exact R9 witness establishes Basic -> same-input move -> full egress/recovery on one ordinary
trajectory; it does not establish this lethal-hazard-removal family or complete productive conversion.
Another bounded native word is Warband R6 `8921f2841dbf`, RAW
`fff955efa1ed319ab2bfb68cf6591ea0b55eae0554408d10164956891254dd98`, Packs A/passive, retained in
`results/contenders/warband/word-service/first-camp-word-witness.json`. At R121 actor2 actually Basics
camp36 at (10,16), HP120->50, then Dashes from (9,15) to (7,15), retaining HP1000 through its next owner
start. The actual root packet has Reset cooldown 0, C available, restrictions 0 and Strength70, with
no enhanced/charged reset effect. Under that same single-target damage/reset model, Basic -> Reset
-> Basic could instead remove the 120HP target at the root. That word was not executed; it spends C
and cannot borrow the actual Dash exit afterward. Target removal, group wake/capture, alternative
exit and economic continuation still need comparison. The observed Basic -> Dash changes no team
gem score and proves neither that counterfactual nor general dominance, renewal or strength.
Status: derived bounded grammar and effect-conditioned admission obligation, with that separate
scoped native compatibility evidence. New families still require exact-executable native refinement.
Recheck: allowance/reset/cast rules, passives, action effects, refusal behavior or dispatcher changes.
Consequence: Keep pre-action legality filters, but do not prune a complete candidate solely because
its no-action or unchanged-effect continuation fails. Generate compatible words, propagate their
effects, then price each resulting exit/service suffix. After every accepted primitive refresh the
actual body, targets, occupancy and epoch required by the next primitive. A failed or truncated word
needs a continuation from the state actually reached; it cannot borrow the unexecuted kill, stun,
shield, cleared tile or remaining permission. Models with incomplete effect coverage may offer an
explicitly limited risk class, not fabricate certification or globally exclude useful work.

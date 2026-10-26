# 04 splash-harvester

This controller treats combat as a bounded set of overlapping hit regions. Varg,
Stellar and Ryth form the three-pick prefix, with Tavi and Marcella filling larger
drafts. Varg buys monster Strength and jungle Health, Stellar buys monster and
jungle Focus, and Ryth buys monster and jungle Health before power Strength.
The live shop calls the same pure purchase hook exercised by profiles, queries
item costs and legality, and counts every stat toward its affiliation emblem.

The local `flow.h` is used by production and mutable fixtures. A menu contains
at most six observed living camps. Varg compares primary targets by clipped
damage to their radius-one hit sets; Stellar and Ryth compare nine visible,
walkable ground centers near the current camp using the typed AOE radius and
queried damage. A direct spell remains a competing option. The criterion adds
coarse saved attacks and avoided incoming damage to clipped HP loss. Damage
beyond death has no value. Ground-only Ryth area stun is handled as AOE damage,
as reported by the SDK, rather than being mistaken for a CC-target spell.

Whole-group public health and incoming damage conservatively admit contact with
another source. Every basic/direct primary and every splash/area footprint is
checked against nearby unaffordable other groups, including members omitted
from the six-target damage menu. This can refuse a safe depleted source because
its full public group was dangerous; safety admission is deliberately coarse.
At most one basic and one successful cast execute in two transitions. There is
no reset or dash chain. Actual HP, death, visibility, geometry and allowances are
read again after actions; range masks alone do not establish legality. Camp
death is queried through a visible public spawn tile, never corpse live info.
Sparse compact logs report two actual observed HP drops from the same action.

The source selector removes Pareto dominated personal-return/cycle/risk jobs,
then compares injury-adjusted payback among survivors. It divides gems and
useful equipment credit by visible sharing, prices cash arrival at an altar and
loses gem value beyond the horizon while retaining productive fallback work.
A six-member public geometry sample provides a coarse splash-speed prior;
the combat menu performs the actual geometry decision. A progress lease lasts
one queried capture window and uses historical best distance and HP, preventing
two-tile reversals from renewing the lease. The executor gives a valid ongoing
source its own route/contact phase before global rescoring. Actual HP loss or
new best route distance renews that phase; expiry and source respawn invalidate
it. This avoids paying global observation and geometry costs on every fighting
or walking turn. Banking uses smaller lots for fragile
Stellar, larger lots for Ryth, early close-altar opportunities, deadline pressure
and injured cargo recovery. No equipment funding phase delays gem banking.

Routing, capture, witnessed-death expiry and harmless-blocker rescue are local
copies of completed 03's mechanics. The disabled branch includes the frozen
shared control directly, without production includes from another bot.

## Evidence and limitations

Focused fixtures call the real selector and executor. They demonstrate a ground
center killing two weak neighbours while a weakest-first basic kills one,
Varg choosing a central primary, refusals for dangerous neighbouring sources,
fresh actual death/HP and allowances, at most one cast plus one basic, and at
most 252 target-slot evaluations over two maximum menus. Policy fixtures show
a reachable personal-sharing choice different from 03, bounded lease expiry,
invalidations, banking, risk and physical champion ID purchase mapping.

The fixed v3 production profile is `cb2fe24735f8`. Versus 03 it disagrees on
47.4% of source selections, 9.3% banking, 1.1% risk, 100% roster entries and
12.3% actual equipment plans. Coarse three-stage build profiles are identical;
the actual role-specific purchase hook supplies equipment differences. Profiles
omit combat and movement, so native action witnesses remain necessary.

Development smoke exposed post-move combat menus entering effect observation
with too little fuel. Admission now reserves 40k before world construction and
each flow transition; impossible follow-up menus are skipped. A cheap current
primary/ground-center legality check precedes world construction, so an out of
range basic allowance cannot consume the walking window. Logging uses one
formatted call for two actual drops, admitted only with 25k fuel remaining.
Global source detail reserves 65k after the first useful candidate, with a 55k
floor for that candidate. Early global observation on large maps can otherwise
starve every candidate; the local execution phase gives selected work a fresh
action window on its next turn. These repairs removed measured zero-clear
stalls and ordinary combat suspension without tuning losing score.

The first full-pool attempt also found one round-one setup exhaustion per actor
on four long-group maps. An accidental all-pairs public density prior was
replaced by a six-by-six sample. Metadata itself now spans setup turns: at most
six completed groups and 24 members per turn, with a retained partial group and
fuel admission before each member. Map handle acquisition and topology each
have separate turns. The 32 discovered large-map regression keys are all clean
in `h12-04-action-admission`, maximum ordinary fuel 90,100.

The final active default is enabled as a healthy, distinct experimental losing
controller. This is not a strength improvement or a release candidate.

- `.cache/arena/sweeps/h12-04-final`: 40 pool maps, both sides, four opponents,
  320 paired keys per arm. Active 126-194 versus frozen 162-158, flips +11/-47,
  McNemar p<.001, paired gem-margin difference -165.7 +/-19.8 SE. Active wins:
  Fundamentals 14/80, Tempo 35/80, main 0/80, Concentric 77/80. Main was exactly
  one quarter of the panel, snapshot `dce540466d11`; it changed externally from
  earlier development runs, so comparisons use the same-run control only.
- `.cache/arena/runs/h12-04-final-confirm`: all 17 different legacy maps, both
  sides, Starter, Banked Vanguard and Bargainer: 101-1, respectively 34/34,
  34/34 and 33/34. This transfers against those weaker opponents; it does not
  override the losing paired pool result.
- All 422 final active games have zero faults, fuel exhaustion, logged errors,
  zero-clear stalls or suspension. Maximum ordinary-turn fuel: pool 92,328,
  legacy 87,534. This states tested coverage, not a universal fuel proof. The
  frozen historical control retains nine known stalls.
- Focused tests pass 2/2 (`arena test heuristics/splash-harvester`), including
  the production execution-phase lease and pre-menu allowance guard. Final
  strict v3 profile report:
  `.cache/arena/heuristics-profiles/20261021-225608-73412-491704900/report.json`.

Measured active source `4e9fb84ec7d5` has WASM SHA256
`a64ad1073363642db3710ab44943a1f8cf5d8092819f8fed8807a5362179a19e`.
Promoted default source `af3d1a8787de` builds to exactly those measured bytes.
Explicit disabled source `9437a15c9632` preserves frozen fundamentals byte for
byte, SHA256
`ed121f9767fca75e4bf5d346683c3ee4f89b918a79194338b1721fbbd4913d29`.

Exact-source native continuation: final pool replay
`00elkring_splash-harvester_fundamentals_184b8674.repl26a`, side A, Stellar
physical actor 1 (champion identity 5), round 6, input 107. Slot 1 AOE casts at
ground center (3,5), reducing camp (3,6) from 2400 to 2302 HP and camp (4,6)
from 120 to 22 HP in the same action. The diagonal hit verifies the actual
Chebyshev radius. Tavi supplies source 0 / `g_183`'s final clear at round 23.
Shared capture at round 32 gives Stellar and Tavi 55 gold and 11 gems each.
Stellar personally banks her 11 at round 53, raising team score from 0 to 11.
This proves a multi-target contribution followed by shared capture and personal
banking; it does not claim the AOE supplied the whole clear. Complete native
receipts: `.cache/splash-review/final-elkring-6-55-receipts.json`.

Next-bot lesson: bounded combat geometry still needs bounded setup and earlier
budget admission for its upstream selector. Sampling static geometry is enough
for a source prior; fresh clipped sets and observed effects should govern the
action. Preserve productive movement and a capture/bank continuation before
adding another optimization.

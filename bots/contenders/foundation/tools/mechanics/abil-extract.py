"""Regenerate bots/contenders/foundation/data/mechanics/abil-evidence.txt from the abilities probe runs.

    python abil-extract.py

Each section names a claim, the run and the replay/log lines that establish it (replay lines come from
replay-events.mjs: round, phase, step, actor, from->to | changes). Rerun the probes first if .cache was
pruned: run_probe.py contenders/_probe-abilities <map> --out .cache/contenders/mechanics/runs/abil-N."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
RUNS = ROOT / ".cache/contenders/mechanics/runs"
NODE = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"
EVENTS = Path(__file__).resolve().parent / "replay-events.mjs"
OUT = ROOT / "bots/contenders/foundation/data/mechanics/abil-evidence.txt"

# (claim, run, source "replay" or "a"/"b" log, first round, last round, regex)
SECTIONS = [
    ("Ranged allowance, attack resets, excludes-walking casts, area damage spares allies (abil-basic)",
     "abil-2", "a", 20, 24, r"atk |step|cast|after|qdmg|op "),
    ("Same, replay view: who lost health (aoe hits only enemies; no self-hit)", "abil-2", "replay:abil-basic", 20, 24,
     r"Attack|Cast"),
    ("Cooldown ticks: Tavi reset cast r20, ready r28", "abil-2", "a", 20, 29, r"(start|end) tavi"),
    ("Stun before/after the target's turn, stunned actions, recall/tribute/teleport cancelled, no stacking, chain (abil-stun)",
     "abil-3", "b", 20, 73, r"stun1|op 8|osc .* error|set_priority|shared|tribute|consumable|recall"),
    ("Stun scenario replay: casts, camp stun skips one attack, channels", "abil-3", "replay:abil-stun", 19, 76,
     r"Cast|Recall|Submit|Teleport|Gems|camps Attack m0"),
    ("Heart of Ruin dash, passive scaling, shield, dash rules (abil-heart)", "abil-5", "replay:abil-heart", 29, 40,
     r"Cast|Attack"),
    ("Heart dash/shield queries and dash rule errors", "abil-5", "a", 29, 38, r"cast|qdmg|atk "),
    ("Stun from Heart's dash on Varg, Marcella immune", "abil-5", "b", 30, 32, r"start (varg|marcella)"),
    ("Durability emblem purchase, Varg shield with Durability, splash, Ren retaliation, Vendetta champion kill (abil-varg)",
     "abil-6", "replay:abil-varg", 20, 58, r"Capture|Shop|Cast|Attack c|kills"),
    ("Varg shield buff per turn", "abil-6", "a", 39, 46, r"(start|end) varg"),
    ("Vendetta cooldowns around the champion kill", "abil-6", "a", 52, 58, r"vendetta cd|vreset|vkill"),
    ("Ren vs wolf retaliation, Willow heals, Vendetta kills one wolf of a group, Marcella enhanced attack, C&I camp death (abil-misc)",
     "abil-7", "replay:abil-misc", 20, 45, r"m0:wolf@8,4|Cast|c4 hp|c5 hp|Submit|c3:marcellaA 5,22"),
    ("Vendetta cooldowns around the camp kill", "abil-7", "a", 29, 32, r"vendetta cd|vreset|vdash|w2a"),
    ("Prism focus per gem, Marcella's enhanced stun, 1-tick tribute cancelled by a stun after the turn", "abil-7", "b", 17, 45,
     r"start prism|start ryth|end prism"),
    ("Cassi & Issac killed by a champion: one reward, both bags (abil-candi)", "abil-8", "replay:abil-candi", 10, 62,
     r"Capture|alive|kills"),
    ("Stun buff seen right after application", "abil-8", "a", 44, 44, r"target |cast"),
]


def replay_lines(run: str, stem: str, lo: int, hi: int) -> list[str]:
    reps = sorted((RUNS / run / "games").glob(f"{stem}_*.repl26a"))
    if not reps:
        return [f"(missing replay {run}/{stem})"]
    out = subprocess.run([str(NODE), "--no-warnings", str(EVENTS), str(reps[-1]), "--from", str(lo), "--to", str(hi)],
                         capture_output=True, text=True, check=True).stdout
    return [line for line in out.splitlines() if " Move " not in line]


def log_lines(run: str, team: str, lo: int, hi: int) -> list[str]:
    logs = sorted((RUNS / run).glob(f"*.{team}.log"))
    if not logs:
        return [f"(missing log {run} {team})"]
    keep = []
    for line in logs[-1].read_text(encoding="utf-8").splitlines():
        m = re.match(r"P [AB] r(\d+) ", line)
        if m and lo <= int(m.group(1)) <= hi:
            keep.append(line)
    return keep


def main() -> int:
    parts = []
    for claim, run, src, lo, hi, rx in SECTIONS:
        lines = replay_lines(run, src.split(":", 1)[1], lo, hi) if src.startswith("replay:") else log_lines(run, src, lo, hi)
        pat = re.compile(rx)
        lines = [l for l in lines if pat.search(l)]
        parts.append(f"## {claim}\n# run {run}, {src}, rounds {lo}-{hi}\n" + "\n".join(l[:240] for l in lines[:120]) + "\n")
    OUT.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)} ({sum(p.count(chr(10)) for p in parts)} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

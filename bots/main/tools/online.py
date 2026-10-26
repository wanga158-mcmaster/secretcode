"""Online match groups of bots/main: registry, opponent panels and paired version comparisons.

  python -B bots/main/tools/online.py status [--version V]      # local: recorded groups
  python -B bots/main/tools/online.py register --version V --group ID --opponent TEAM \
         --maps m1,m2,m3,m4,m5 --sides a,b,a,b,a                # local: record a group created by hand
  python -B bots/main/tools/online.py panel --version V [--panel FILE] [--only K]
         # server: one group per panel entry against the live submission, recorded under V
  python -B bots/main/tools/online.py fetch                     # server, read-only: results, zips, summaries
  python -B bots/main/tools/online.py compare VA VB [--panel FILE]
         # local: pair the games of two versions on identical (opponent, map, side) keys
  python -B bots/main/tools/online.py keep [--hours H] [--every S]
         # server: keep the hourly quota full with survey groups against the live version (diverse
         # opponents across rank tiers, random maps and sides) and fetch them; pause by creating
         # .cache/main/keeper.pause (do that, and let pending groups finish, before a new submit)

Server commands are allowed to the main bot's release agent only (granted in its kickoff). Every group plays
the current submission: register it under the version that was live when it was created.
Panel file (default bots/main/tools/panel.json): [{"name", "opponent", "maps": [5], "sides": [5]}].
Registry results/online/groups.json; downloads results/online/<version>/<group-id>/ (group.zip with
our logs and replays, <match-id>-summary.json.gz); one result row per group in results/online/online.csv.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import json
import math
import random
import re
import subprocess
import sys
import time
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scout  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
CAMBC = ROOT / ".tools" / "cambc.exe"
ONLINE = ROOT / "results" / "online"
REGISTRY = ONLINE / "groups.json"
CSV = ONLINE / "online.csv"
PANEL = Path(__file__).resolve().parent / "panel.json"
LIVE = ROOT / "results" / "releases" / "LIVE.json"
LIVE_OVERRIDE = ROOT / ".cache" / "main" / "live-override.json"
PAUSE = ROOT / ".cache" / "main" / "keeper.pause"
STAGING = ROOT / ".cache" / "main" / "online-repair"
FETCH_SECONDS = 60
POOL = ["00ambush", "00collision", "00contest", "00corners", "00eight", "00elkring", "00expanse", "00fragment",
        "00gem_fields", "00gemfight", "00great_wall", "00interlock", "00lanes", "00lost_world", "00monolith", "00packs",
        "00pilgrimage", "00quadrants", "00reverse_spiral", "00road_of_riches", "00rows", "00rushbait_v2", "00split",
        "00stampede", "00swirl", "01big_world", "01boss_battle", "01broken_curve", "01concentrated_wealth", "01corridors",
        "01elk_arena", "01fracture", "01gold_rush", "01hooks", "01islands", "01shortcut", "01starfish", "01starvation",
        "01the_farm", "01yellow_brick_road"]
TIERS = [(1, 20, 0.3), (21, 60, 0.3), (61, 200, 0.25), (201, 10000, 0.15)]  # survey opponents: rank range, share
OUR_TEAM = "34b36bab-66bb-4506-b032-2df6a081758d"  # platform team "ha2t"


class ReadDeferred(RuntimeError):
    """A collection read should be retried in a later keeper cycle, without sleeping here."""


class IneligibleOpponent(RuntimeError):
    """The server explicitly rejected creation because the opponent has no eligible bot."""


def cli(*args: str, timeout: float = 300, retry: bool = True) -> str:
    # The server answers HTTP 429 ("queue is busy") under load: retry a few times with backoff. The
    # group quota is a rolling hour; its 429 names the wait ("retry after N seconds"): sleep it out.
    message = ""
    for attempt in range(6):
        try:
            proc = subprocess.run([str(CAMBC), *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
                                  errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            # A create may have reached the server: do not retry an unknown outcome here.
            error = RuntimeError if retry else ReadDeferred
            raise error(f"cambc {' '.join(args[:2])} timed out after {timeout:g}s; outcome unknown") from exc
        if proc.returncode == 0:
            return proc.stdout
        message = proc.stderr.strip() or proc.stdout.strip()
        creating = args[:2] == ("match-groups", "create")
        if creating and re.search(r"\bHTTP\s+409\b", message, re.IGNORECASE):
            if re.search(r"\b(?:doesn.t|does not) have an eligible bot\b", message, re.IGNORECASE):
                raise IneligibleOpponent(message)
            break
        retryable = bool(re.search(r"\bHTTP\s+429\b", message, re.IGNORECASE)) if creating else \
            "429" in message or "busy" in message.lower()
        if not retryable:
            break
        if not retry:
            raise ReadDeferred(f"cambc {' '.join(args[:2])} failed: {message}")
        wait = re.search(r"retry after (\d+) seconds", message)
        if wait and int(wait.group(1)) <= 3600:
            print(f"{time.strftime('%H:%M:%S')} cambc {' '.join(args[:2])}: waiting {int(wait.group(1)) + 5}s", flush=True)
            time.sleep(int(wait.group(1)) + 5)
            continue
        time.sleep(5 * (attempt + 1))
    raise RuntimeError(f"cambc {' '.join(args)} failed: {message}")


def quota_available() -> int:
    text = cli("match-groups", "quota", "--json", timeout=30, retry=False)
    try:
        reply = json.loads(text)
    except ValueError as e:
        raise RuntimeError("cambc match-groups quota returned invalid JSON") from e
    quota = reply.get("quota") if isinstance(reply, dict) else None
    if not isinstance(quota, dict) or any(type(quota.get(k)) is not int or quota[k] < 0 for k in ("limit", "used")):
        raise RuntimeError("cambc match-groups quota returned unexpected JSON; expected quota.limit/used nonnegative integers")
    return max(0, quota["limit"] - quota["used"])


def create_group(opponent: str, maps: str, sides: str) -> dict:
    text = cli("match-groups", "create", opponent, "--maps", maps, "--sides", sides, "--json")
    try:
        reply = json.loads(text)
    except ValueError as e:
        raise RuntimeError("cambc match-groups create returned invalid JSON; outcome unknown") from e
    if not isinstance(reply, dict) or not isinstance(reply.get("groupId"), str) or not reply["groupId"].strip():
        raise RuntimeError("cambc match-groups create returned unexpected JSON; expected groupId; outcome unknown")
    return reply


def load() -> list:
    return json.loads(REGISTRY.read_text(encoding="utf-8")) if REGISTRY.exists() else []


def save(groups: list) -> None:
    """Merge into the registry on disk: groups written by another process since our load are kept
    (two writers once dropped ten freshly created groups). Our rows replace theirs by group id."""
    REGISTRY.parent.mkdir(parents=True, exist_ok=True)
    try:
        on_disk = load()
    except (OSError, ValueError):
        on_disk = []
    mine = {g["group"] for g in groups}
    merged = list(groups) + [g for g in on_disk if g["group"] not in mine]
    groups[:] = merged
    staged = REGISTRY.with_name(REGISTRY.name + f".{time.time_ns()}.tmp")
    staged.write_text(json.dumps(merged, indent=1), encoding="utf-8")
    staged.replace(REGISTRY)


def write_csv(groups: list) -> None:
    """The registry is authoritative; rebuilding also repairs a crash between the two saves."""
    CSV.parent.mkdir(parents=True, exist_ok=True)
    staged = CSV.with_name(CSV.name + ".tmp")
    rows = {g["group"]: g for g in groups if g.get("results")}
    with staged.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["version", "group", "opponent", "maps", "results"])
        for g in rows.values():
            results = g["results"]
            wins = sum(r.endswith(":win") for r in results)
            writer.writerow([g["version"], g["group"], g["opponent"], g["maps"],
                             f"W{wins}/{len(results)} " + " ".join(results)])
    staged.replace(CSV)


def record(groups: list, version: str, group: str, opponent: str, maps: str, sides: str, name: str = "",
           kind: str = "", rank: int = 0) -> None:
    created = {"teamName": name} if name else {}
    if rank:
        created["rank"] = rank
    row = {"version": version, "group": group, "opponent": opponent, "maps": maps, "sides": sides,
           "state": "pending", "created": created}
    if kind:
        row["kind"] = kind
    groups.append(row)


def register(args) -> int:
    groups = load()
    if any(g["group"] == args.group for g in groups):
        print(f"group {args.group} is already recorded")
        return 1
    record(groups, args.version, args.group, args.opponent, args.maps, args.sides)
    save(groups)
    print(f"recorded group {args.group} (v{args.version} vs {args.opponent[:8]})")
    return 0


def panel(args) -> int:
    entries = json.loads(Path(args.panel).read_text(encoding="utf-8"))
    if args.only:
        entries = entries[:args.only]
    groups = load()
    for e in entries:
        out = create_group(e["opponent"], ",".join(e["maps"]), ",".join(e["sides"]))
        record(groups, args.version, out["groupId"], e["opponent"], ",".join(e["maps"]), ",".join(e["sides"]),
               out.get("teamName", e.get("name", "")))
        save(groups)
        print(f"created {out['groupId']} v{args.version} vs {e.get('name', e['opponent'][:8])}")
    return 0


def summary_bytes(path: Path, packed: bool = False) -> bytes | None:
    """Reuse only complete summaries; retain damaged downloads for diagnosis."""
    if not path.exists():
        return None
    try:
        payload = gzip.decompress(path.read_bytes()) if packed else path.read_bytes()
        summary = json.loads(payload)
        if not isinstance(summary, dict) or not isinstance(summary.get("match"), dict) or \
                "roundsCompleted" not in summary["match"]:
            raise ValueError("summary has no completed-match metadata")
        return payload
    except (OSError, EOFError, ValueError, zlib.error):
        path.replace(path.with_name(f"{path.name}.invalid-{time.time_ns()}"))
        return None


def collect_artifacts(g: dict, read) -> None:
    dest = ONLINE / str(g["version"]) / g["group"]
    dest.mkdir(parents=True, exist_ok=True)
    STAGING.mkdir(parents=True, exist_ok=True)
    pending = g["collection_pending"]
    # Summaries are useful even when a large archive needs a later attempt.
    for item in sorted(pending[:], key=lambda name: name == "group.zip"):
        if item == "group.zip":
            target = dest / item
            if target.exists() and not zipfile.is_zipfile(target):
                target.replace(target.with_name(f"{target.name}.invalid-{time.time_ns()}"))
            if not target.exists():
                staged = STAGING / f"{g['group']}-{time.time_ns()}.zip"
                read("match-groups", "download", g["group"], "--output", str(staged), timeout=900)
                if not zipfile.is_zipfile(staged):
                    raise ValueError("downloaded group archive is not a complete ZIP")
                staged.replace(target)
        else:
            packed = dest / f"{item}-summary.json.gz"
            if summary_bytes(packed, packed=True) is None:
                payload = summary_bytes(dest / f"{item}-summary.json")
                if payload is None:
                    staged = STAGING / f"{item}-{time.time_ns()}.json"
                    read("matches", "summary", item, "--json", "--output", str(staged), timeout=300)
                    payload = summary_bytes(staged)
                    if payload is None:
                        raise ValueError(f"summary {item} is incomplete")
                    staged.unlink()  # Regenerable staging lives under .cache/.
                compressed = STAGING / f"{item}-{time.time_ns()}.json.gz"
                compressed.write_bytes(gzip.compress(payload))
                compressed.replace(packed)
        pending.remove(item)


def fetch(args, max_seconds: float = 0) -> int:
    groups = load()
    write_csv(groups)  # Repair a prior interruption after a registry checkpoint.
    deadline = time.monotonic() + max_seconds if max_seconds else None
    discovery_deadline = time.monotonic() + max_seconds / 2 if max_seconds else None

    def read(*argv, timeout=30, until=None):
        limit = deadline
        if until is not None:
            limit = min(limit, until) if limit is not None else until
        if limit is not None:
            remaining = limit - time.monotonic()
            if remaining <= 0:
                raise ReadDeferred("collection time slice used")
            timeout = min(timeout, remaining)
        return cli(*argv, timeout=timeout, retry=False)

    # Discover remote completion before attempting slower artifact downloads. "done" means
    # remote work ended, including timestamped errors; artifact retries remain separate.
    discovering = sorted((g for g in groups if g["state"] != "done"),
                         key=lambda g: g.get("show_attempted", 0))
    for g in discovering:
        if discovery_deadline is not None and time.monotonic() >= discovery_deadline:
            break
        g["show_attempted"] = time.time()
        try:
            show = json.loads(read("match-groups", "show", g["group"], "--json", until=discovery_deadline))
        except ReadDeferred as e:
            save(groups)
            write_csv(groups)
            print(f"{time.strftime('%H:%M:%S')} collection deferred: {brief_error(e)}", flush=True)
            break
        except (RuntimeError, OSError, ValueError, KeyError) as e:
            save(groups)
            write_csv(groups)
            print(f"{time.strftime('%H:%M:%S')} group status error {g['group']}: {brief_error(e)}", flush=True)
            continue
        g["show"] = show
        matches = show.get("matches") or []
        finished = [m for m in matches if m.get("status") == "finished"]
        # An error without finishedAt may still be in transition. Preserve the entire response
        # and each terminal diagnostic, but never invent a result or request an error summary.
        errors = [m for m in matches if m.get("status") == "error" and
                  isinstance(m.get("finishedAt"), str) and m["finishedAt"].strip()]
        if errors:
            g["terminal_errors"] = errors
        else:
            g.pop("terminal_errors", None)
        complete = bool(matches) and len(finished) + len(errors) == len(matches)
        if complete:
            results = []
            for m in finished:
                ours = next((t for t in m.get("teams", []) if t.get("teamId") == OUR_TEAM), {})
                results.append(f"{m.get('map')}:{ours.get('side', '?')}:{ours.get('result', '?')}")
            g["state"] = "done"
            g["results"] = results
            g["collection_pending"] = [m["matchId"] for m in finished] + ["group.zip"]
        save(groups)
        write_csv(groups)
        error_count = f" errors={len(errors)}" if errors else ""
        state = "complete" if complete and errors else "finished" if complete else "pending"
        print(f"{g['group']}: {state} {len(finished)}/{len(matches)}{error_count}", flush=True)

    # Old done rows without collection metadata retain their existing meaning. Failed groups
    # rotate behind other collection work, so one slow archive cannot monopolise every slice.
    collecting = sorted((g for g in groups if g.get("collection_pending")),
                        key=lambda g: g.get("collection_attempted", 0))
    for g in collecting:
        if deadline is not None and time.monotonic() >= deadline:
            break
        g["collection_attempted"] = time.time()
        deferred = False
        try:
            collect_artifacts(g, read)
        except ReadDeferred as e:
            deferred = True
            print(f"{time.strftime('%H:%M:%S')} collection deferred {g['group']}: {brief_error(e)}", flush=True)
        except (RuntimeError, OSError, ValueError, KeyError) as e:
            print(f"{time.strftime('%H:%M:%S')} collection error {g['group']}: {brief_error(e)}", flush=True)
        finally:
            save(groups)
            write_csv(groups)
        print(f"{g['group']}: artifacts pending {len(g['collection_pending'])}", flush=True)
        if deferred:
            break
    return 0


def status(args) -> int:
    for g in load():
        if args.version and str(g["version"]) != args.version:
            continue
        name = (g.get("created") or {}).get("teamName", g["opponent"][:8])
        errors = f" errors={len(g['terminal_errors'])}" if g.get("terminal_errors") else ""
        collecting = f" artifacts-pending={len(g['collection_pending'])}" if g.get("collection_pending") else ""
        print(g["version"], g["group"][:8], name[:20], g["state"] + errors + collecting, " ".join(g.get("results", [])))
    return 0


def games(version: str) -> dict:
    """(opponent, map, side) -> metrics of our games in this version's finished groups (latest wins)."""
    out = {}
    for g in load():
        if str(g["version"]) != version or g["state"] != "done":
            continue
        dest = ONLINE / str(g["version"]) / g["group"]
        for m in (g.get("show") or {}).get("matches", []):
            if m.get("status") != "finished":
                continue
            ours = next((t for t in m["teams"] if t["teamId"] == OUR_TEAM), None)
            if not ours:
                continue
            row = {"win": ours["result"] == "win", "name": (g.get("created") or {}).get("teamName", g["opponent"][:8])}
            s = scout.load_summary(dest, m["matchId"])
            if s:
                us, them = ("teamA", "teamB") if ours["side"] == "a" else ("teamB", "teamA")
                a, b = scout.team_stats(s, us), scout.team_stats(s, them)
                row.update(gems=a["gems"], ogems=b["gems"], g300=a["g300"], og300=b["g300"], c300=a["c300"],
                           oc300=b["c300"], k=a["kills"], d=a["deaths"], rounds=s["match"]["roundsCompleted"])
            out[(g["opponent"], m["map"], ours["side"])] = row
    return out


def compare(args) -> int:
    a, b = games(args.a), games(args.b)
    keys = sorted(set(a) & set(b), key=lambda k: (a[k]["name"], k[1], k[2]))
    if not keys:
        print(f"no common games (v{args.a}: {len(a)}, v{args.b}: {len(b)})")
        return 1
    gained = [k for k in keys if b[k]["win"] and not a[k]["win"]]
    lost = [k for k in keys if a[k]["win"] and not b[k]["win"]]
    print(f"v{args.a} -> v{args.b}: {len(keys)} paired games (only v{args.a}: {len(set(a) - set(b))}, only v{args.b}: {len(set(b) - set(a))})")
    for k in keys:
        x, y = a[k], b[k]
        mark = "+" if k in gained else "-" if k in lost else " "
        metr = ""
        if "gems" in x and "gems" in y:
            metr = (f"gems {x['gems']:4.0f}-{x['ogems']:<4.0f} -> {y['gems']:4.0f}-{y['ogems']:<4.0f} "
                    f"gold300 {x['g300']:4.0f}/{x['og300']:<4.0f} -> {y['g300']:4.0f}/{y['og300']:<4.0f} "
                    f"k/d {x['k']:.0f}/{x['d']:.0f} -> {y['k']:.0f}/{y['d']:.0f}")
        print(f" {mark} {x['name'][:16]:16s} {k[1][:18]:18s} {k[2]} {'W' if x['win'] else 'L'}->{'W' if y['win'] else 'L'}  {metr}")
    n = len(gained) + len(lost)
    p = min(1.0, 2 * sum(math.comb(n, i) for i in range(0, min(len(gained), len(lost)) + 1)) / 2 ** n) if n else 1.0
    wa, wb = sum(a[k]["win"] for k in keys), sum(b[k]["win"] for k in keys)
    both = [k for k in keys if "g300" in a[k] and "g300" in b[k]]
    line = f"wins {wa} -> {wb} of {len(keys)}; flips +{len(gained)} -{len(lost)} (sign p {p:.2f})"
    if both:
        mean = lambda f: sum(f(k) for k in both) / len(both)
        line += (f"; mean diff (n={len(both)}): gold300 {mean(lambda k: b[k]['g300'] - a[k]['g300']):+.0f}, "
                 f"gem margin {mean(lambda k: (b[k]['gems'] - b[k]['ogems']) - (a[k]['gems'] - a[k]['ogems'])):+.0f}, "
                 f"deaths {mean(lambda k: b[k]['d'] - a[k]['d']):+.1f}, kills {mean(lambda k: b[k]['k'] - a[k]['k']):+.1f}")
    print(line)
    return 0


def live_version() -> str:
    """The live release's number (results/releases/LIVE.json name main-vN -> "N"). An online experiment
    submitted without `arena release` records its platform version in .cache/main/live-override.json."""
    try:
        return str(json.loads(LIVE_OVERRIDE.read_text(encoding="utf-8"))["version"])
    except (OSError, ValueError, KeyError):
        pass
    try:
        name = json.loads(LIVE.read_text(encoding="utf-8"))["name"]
        return name.rsplit("-v", 1)[-1]
    except (OSError, ValueError, KeyError):
        return "?"


def ladder_teams() -> list[dict]:
    teams, page = [], 1
    deadline = time.monotonic() + FETCH_SECONDS
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ReadDeferred("ladder refresh time slice used")
        out = json.loads(cli("ladder", "--page", str(page), "--limit", "50", "--json",
                             timeout=min(30, remaining), retry=False))
        rows = out.get("teams") or []
        teams += rows
        if len(rows) < 50 or page >= 40:
            return teams
        page += 1


def survey_pick(teams: list[dict], recent: list[str], rng: random.Random) -> dict:
    """An opponent from a rank tier drawn by TIERS' shares, avoiding the last ~40 picks."""
    candidates = [t for t in teams if t["teamId"] != OUR_TEAM and any(lo <= t["rank"] <= hi for lo, hi, _ in TIERS)]
    if not candidates:
        raise ValueError("no eligible survey opponents available")
    fresh = [t for t in candidates if t["teamId"] not in recent]
    candidates = fresh or candidates
    tiers = [([t for t in candidates if lo <= t["rank"] <= hi], share) for lo, hi, share in TIERS]
    tiers = [(pool, share) for pool, share in tiers if pool]
    r = rng.random() * sum(share for _, share in tiers)
    for pool, share in tiers:
        if r < share:
            return rng.choice(pool)
        r -= share
    return rng.choice(tiers[-1][0])


def keep(args) -> int:
    rng = random.Random()
    deadline = time.time() + args.hours * 3600
    teams, teams_at, recent = [], 0.0, []
    ineligible = set()
    while time.time() < deadline:
        # Collection must not depend on a successful creation. Continue draining already queued
        # groups while paused so the submit protocol can reach a stable empty queue.
        try:
            fetch(args, max_seconds=FETCH_SECONDS)
        except (RuntimeError, OSError, ValueError, KeyError) as e:
            print(f"{time.strftime('%H:%M:%S')} keeper fetch error: {brief_error(e)}", flush=True)
        if PAUSE.exists():
            print(f"{time.strftime('%H:%M:%S')} paused ({PAUSE})", flush=True)
        else:
            try:
                free = quota_available()
                if free > 0 and time.time() - teams_at > 3600:
                    try:
                        teams, teams_at = ladder_teams(), time.time()
                        ineligible.clear()
                    except (RuntimeError, OSError, ValueError, KeyError) as e:
                        if not teams:
                            raise
                        print(f"{time.strftime('%H:%M:%S')} ladder refresh deferred; using cached teams: {brief_error(e)}", flush=True)
                version = live_version()
                groups = load()
                made = 0
                while made < free:
                    if PAUSE.exists():
                        break
                    t = survey_pick([t for t in teams if t["teamId"] not in ineligible], recent, rng)
                    maps = rng.sample(POOL, 5)
                    sides = [rng.choice("ab") for _ in maps]
                    try:
                        out = create_group(t["teamId"], ",".join(maps), ",".join(sides))
                    except IneligibleOpponent as e:
                        ineligible.add(t["teamId"])
                        print(f"{time.strftime('%H:%M:%S')} skipped ineligible opponent {t['name']}: {brief_error(e)}", flush=True)
                        continue
                    record(groups, version, out["groupId"], t["teamId"], ",".join(maps), ",".join(sides),
                           out.get("teamName", t["name"]), kind="survey", rank=int(t["rank"]))
                    save(groups)
                    recent = (recent + [t["teamId"]])[-40:]
                    made += 1
                if made:
                    print(f"{time.strftime('%H:%M:%S')} created {made} survey group(s) for v{version}", flush=True)
            except (RuntimeError, OSError, ValueError, KeyError) as e:
                print(f"{time.strftime('%H:%M:%S')} keeper error: {brief_error(e)}", flush=True)
        time.sleep(args.every)
    return 0


def brief_error(error: Exception) -> str:
    # A long five-map command used to consume the whole log line before the server's reason.
    message = str(error)
    return message.split(" failed: ", 1)[-1][-320:]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("status", help="list the recorded groups (local)")
    s.add_argument("--version")
    r = sub.add_parser("register", help="record a group created by hand (local)")
    for name in ("--version", "--group", "--opponent", "--maps", "--sides"):
        r.add_argument(name, required=True)
    pn = sub.add_parser("panel", help="create one group per panel entry (server)")
    pn.add_argument("--version", required=True)
    pn.add_argument("--panel", default=str(PANEL))
    pn.add_argument("--only", type=int, default=0, help="first K entries only")
    sub.add_parser("fetch", help="download finished groups and their summaries (server, read-only)")
    k = sub.add_parser("keep", help="keep the hourly quota full with survey groups and fetch them (server)")
    k.add_argument("--hours", type=float, default=24.0)
    k.add_argument("--every", type=int, default=180, help="seconds between rounds")
    c = sub.add_parser("compare", help="paired comparison of two versions' games (local)")
    c.add_argument("a")
    c.add_argument("b")
    args = p.parse_args()
    return {"status": status, "register": register, "panel": panel, "fetch": fetch, "compare": compare,
            "keep": keep}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

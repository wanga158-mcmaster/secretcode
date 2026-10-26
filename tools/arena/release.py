"""Releases: turn a gated version into the next online submission, one release at a time.

  arena release                         show the live version and prepared releases
  arena release BOT --name NAME --gate DIR
        under a lock: check that DIR's gate passed for BOT's current version against the live
        anchor on every pool map, bundle the sources to results/releases/NAME/main.zip, freeze
        them as bots/anchors/NAME, and print the submit command (submit the frozen anchor, so later
        edits to the working line cannot slip in)
  arena release --live NAME             after the submission succeeded: record NAME as live

The live version is results/releases/LIVE.json; its anchor is the reference every candidate must
beat (`arena gate BOT --ref <anchor>`). Submitting is an online action: only with the explicit
authority of the user's instructions.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import ArenaError, BOTS, CACHE, CAMBC, RESULTS, ROOT, FileLock, pool_maps, read_closure, version_of  # noqa: E402
from core import bot as find_bot  # noqa: E402

RELEASES = RESULTS / "releases"
LIVE = RELEASES / "LIVE.json"
DEFAULT_LIVE = {"name": "main-v28", "anchor": "anchors/main-v28", "bot": "main/main", "key": "",
                "live_since": "2026-10-21", "note": "platform bot version 29"}
NAME = re.compile(r"^[a-z0-9][a-z0-9.-]*$")
OWNER_GATE_SCHEMA = "arena.owner-cli-gate.v1"


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def live() -> dict:
    return json.loads(LIVE.read_text(encoding="utf-8")) if LIVE.is_file() else dict(DEFAULT_LIVE)


def _write_live(rec: dict) -> None:
    history = live().get("history", []) if LIVE.is_file() else []
    prev = live()
    if prev.get("name") != rec.get("name"):
        history = history + [{k: prev.get(k) for k in ("name", "anchor", "bot", "key", "live_since")}]
    rec["history"] = history
    RELEASES.mkdir(parents=True, exist_ok=True)
    LIVE.write_text(json.dumps(rec, indent=1) + "\n", encoding="utf-8")


def _status() -> int:
    cur = live()
    print(f"live: {cur['name']} ({cur['anchor']}, from {cur.get('bot') or '?'}) since {cur.get('live_since')}")
    for d in sorted(p for p in RELEASES.iterdir() if p.is_dir()) if RELEASES.is_dir() else []:
        rec = d / "release.json"
        if rec.is_file():
            r = json.loads(rec.read_text(encoding="utf-8"))
            state = "live" if r["name"] == cur["name"] else ("submitted" if r.get("live_since") else "prepared")
            print(f"  {r['name']:16} {state:9} {r['key']}  gate {r.get('gate', '-')}")
        else:
            print(f"  {d.name:16} (no release.json)")
    return 0


def prove_reference(owner: str, reference_key: str, *, out, roots, timeout: int = 1800) -> dict:
    """Produce independent two-cold-build evidence for a prepared LIVE reference.

    This local producer builds released source bytes under their original bot ID;
    it does not submit, freeze a new anchor or alter LIVE/release records.
    """
    from released_proof import produce
    return produce(owner, reference_key, out=out, roots=roots, timeout=timeout)


def verify_gate_for_release(gate_dir, subject_key: str, current_live: dict) -> dict:
    """Verify a gate before release writes; owner-bound gates never use legacy checks."""
    gate_file = Path(gate_dir) / "gate.json" if gate_dir else None
    if not gate_file or not gate_file.is_file():
        raise ArenaError(f"--gate DIR must hold gate.json (run: arena gate {subject_key.split('@')[0]} --ref {current_live['anchor']})")
    try:
        g = json.loads(gate_file.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ArenaError(f"cannot read gate.json: {exc}") from exc
    if not isinstance(g, dict):
        raise ArenaError("gate.json must contain an object")
    for field in ("diagnostic_only", "private_snapshots"):
        if field in g and type(g[field]) is not bool:
            raise ArenaError("invalid diagnostic experiment marker")
        if g.get(field) is True:
            raise ArenaError("diagnostic experiments cannot qualify a release")
    # These fields are exclusive to the bound format. Removing its four headline
    # markers must not turn its remaining input/evidence records into a legacy pass.
    bound_fields = {"schema", "owner", "input_fingerprint", "evidence_fingerprint", "inputs", "rows", "tests",
                    "fixtures", "released_proof"}
    if bound_fields.intersection(g):
        if g.get("schema") != OWNER_GATE_SCHEMA:
            raise ArenaError("unsupported owner-bound gate schema")
        from gate import verify_owner_gate
        return verify_owner_gate(Path(gate_dir), subject_key=subject_key, live_record=current_live)
    problems = []
    if not g.get("pass"):
        problems.append("the gate did not pass")
    if g.get("key") != subject_key:
        problems.append(f"the gate tested {g.get('key')}, but the working tree is {subject_key} (re-run the gate)")
    if g.get("ref") != current_live["anchor"]:
        problems.append(f"the gate's reference is {g.get('ref')}, not the live anchor {current_live['anchor']}")
    missing = sorted(set(pool_maps()) - set(g.get("maps") or []))
    if missing:
        problems.append(f"the gate skipped {len(missing)} pool map(s), e.g. {missing[0]}")
    if g.get("rounds"):
        problems.append("the gate used --rounds")
    if problems:
        raise ArenaError("not releasable: " + "; ".join(problems))
    return g


def _prepare(args) -> int:
    b = find_bot(args.bot)
    if b.frozen:
        raise ArenaError("release a developing bot, not an anchor")
    name = args.name
    if not name or not NAME.match(name):
        raise ArenaError("--name must be lower-case letters, digits, '.' or '-' (e.g. skirmish-1)")
    with FileLock(CACHE / "release.lock", timeout=600):
        cur = live()
        own = (b.path / "src").resolve()
        contents = read_closure(b)  # the one read: the gate key is checked against, and the anchor frozen from, these bytes
        outside = [p for p in contents if not p.is_relative_to(own)]
        if outside:
            raise ArenaError(f"{b.id} includes files outside its src/ ({outside[0]}); a release must be self-contained")
        key = f"{b.id}@{version_of(contents)}"
        g = verify_gate_for_release(args.gate, key, cur)
        target, anchor = RELEASES / name, BOTS / "anchors" / name
        if target.exists() or anchor.exists():
            raise ArenaError(f"{name} already exists (results/releases/{name} or bots/anchors/{name})")
        target.mkdir(parents=True)
        for f, data in contents.items():
            dst = anchor / "src" / f.relative_to(own)
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
        bundle = target / "main.zip"
        proc = subprocess.run([str(CAMBC), "bundle", f"anchors/{name}", "--output", str(bundle)], cwd=ROOT,
                              capture_output=True, text=True, errors="replace")
        if proc.returncode != 0 or not bundle.is_file():
            raise ArenaError(f"cambc bundle failed: {(proc.stdout + proc.stderr)[-800:]}")
        with zipfile.ZipFile(bundle) as z:
            names = [n for n in z.namelist() if not n.endswith("/")]
            bad = [n for n in names if not (anchor / n).is_file() or (anchor / n).read_bytes() != z.read(n)]
        if bad:
            raise ArenaError(f"bundle does not match the frozen sources: {bad[:3]}")
        ref = next((c for c in g.get("checks", []) if c.get("step") == "reference"), {})
        rec = {"name": name, "bot": b.id, "key": key, "anchor": f"anchors/{name}", "prepared": _now(),
               "gate": Path(args.gate).resolve().relative_to(ROOT).as_posix() if Path(args.gate).resolve().is_relative_to(ROOT) else str(args.gate),
               "vs_live": ref.get("note", ""), "replaces": cur["name"], "files": len(names)}
        if g.get("schema") == OWNER_GATE_SCHEMA:
            rec.update(gate_schema=g["schema"], gate_input_fingerprint=g["input_fingerprint"],
                       gate_evidence_fingerprint=g["evidence_fingerprint"])
        (target / "release.json").write_text(json.dumps(rec, indent=1) + "\n", encoding="utf-8")
        (target / "NOTE.md").write_text(
            f"# {name}\n\n- source: {key} (frozen as bots/anchors/{name})\n- gate: {rec['gate']} (pass)\n"
            f"- against the live {cur['name']}: {rec['vs_live'] or 'see gate.json'}\n- prepared: {rec['prepared']}\n"
            f"- changes since {cur['name']}: see bots/{b.suite}/DECISIONS.md or the suite README\n", encoding="utf-8")
        table = BOTS / "anchors" / "README.md"
        if table.is_file():  # release is the sanctioned writer of new anchor rows
            lines = table.read_text(encoding="utf-8").splitlines()
            last = max(i for i, l in enumerate(lines) if l.startswith("|"))
            lines.insert(last + 1, f"| {name} | Release of {key} ({rec['prepared'][:10]}); against {cur['name']}: "
                                   f"{rec['vs_live'] or 'see its gate'} | Release reference once live |")
            table.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"prepared {name}: results/releases/{name}/main.zip ({len(names)} files), anchor bots/anchors/{name}")
    print(f"next (online, only with the user's authority): .tools/cambc.exe submit anchors/{name}")
    print(f"then: arena release --live {name}")
    return 0


def _mark_live(name: str) -> int:
    with FileLock(CACHE / "release.lock", timeout=600):
        rec_file = RELEASES / name / "release.json"
        if not rec_file.is_file():
            raise ArenaError(f"no prepared release {name}")
        rec = json.loads(rec_file.read_text(encoding="utf-8"))
        rec["live_since"] = _now()
        rec_file.write_text(json.dumps(rec, indent=1) + "\n", encoding="utf-8")
        _write_live({"name": name, "anchor": rec["anchor"], "bot": rec["bot"], "key": rec["key"],
                     "live_since": rec["live_since"]})
    print(f"live: {name} ({rec['anchor']}); candidates now gate against --ref {rec['anchor']}")
    return 0


def _cmd(args) -> int:
    proof_args = [getattr(args, name, None) for name in ("prove_reference", "owner", "proof_out", "cold_root")]
    if any(proof_args):
        if (not all(proof_args) or len(args.cold_root) != 2 or args.bot or args.name or args.gate or args.live):
            raise ArenaError("reference reproduction needs --prove-reference KEY --owner OWNER --proof-out FILE and two --cold-root ROOT values")
        result = prove_reference(args.owner, args.prove_reference, out=args.proof_out, roots=tuple(map(Path, args.cold_root)),
                                 timeout=args.build_timeout)
        print(json.dumps(result, sort_keys=True))
        return 0
    if args.live:
        return _mark_live(args.live)
    if not args.bot:
        return _status()
    return _prepare(args)


def register(sub) -> None:
    p = sub.add_parser("release", help="prepare a gated version for submission, or record it as live")
    p.add_argument("bot", nargs="?", default="", help="the developing bot to release (e.g. main/skirmish)")
    p.add_argument("--name", default="", help="release name, also the new anchor's name (e.g. skirmish-1)")
    p.add_argument("--gate", default="", help="the out dir of a passing `arena gate BOT --ref <live anchor>`")
    p.add_argument("--live", default="", metavar="NAME", help="record a submitted release as the live version")
    p.add_argument("--prove-reference", default="", metavar="KEY", help="locally reproduce a prepared LIVE reference from two cold builds")
    p.add_argument("--owner", default="", help="private owner namespace for reference reproduction")
    p.add_argument("--proof-out", default="", metavar="FILE", help="new owner-local reproduction proof file")
    p.add_argument("--cold-root", action="append", default=[], metavar="ROOT", help="fresh owner-local build root (repeat twice)")
    p.add_argument("--build-timeout", type=int, default=1800, help="per-cold-build wall timeout in seconds")
    p.set_defaults(func=_cmd)

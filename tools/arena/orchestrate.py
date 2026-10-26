"""Derived local work offers and exclusive, explicitly reconciled ownership claims.

This module never dispatches agents. A claim survives a lost worker or old heartbeat: only
an explicit coordinator reconciliation releases its paths. Source and dependency hashes make
an offer obsolete when its read inputs change. Claims are disposable coordination state, not
acceptance evidence; population.assess remains the admission authority.
"""
from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path

import core
from core import ArenaError, FileLock

ROOT = core.ROOT
CACHE = core.CACHE / "orchestration"
EXCLUDED = {"main", "anchors", "surprise", "contenders"}
EXTERNALLY_OWNED = {"contenders"}  # informational audit rows only; user excludes ongoing work
ACTIVE = {"claimed", "running", "reported", "interrupted"}
KINDS = {"repair", "activation", "differentiation", "evidence", "exploration"}


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _digest(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _path(raw: str | Path) -> Path:
    p = (ROOT / raw).resolve() if not Path(raw).is_absolute() else Path(raw).resolve()
    if not p.is_relative_to(ROOT):
        raise ArenaError(f"scope is outside repository: {raw}")
    return p


def _rel(p: Path) -> str:
    # ROOT and closure paths are canonical already; scope entry goes through _path.
    return p.relative_to(ROOT).as_posix()


def _overlap(a: str | Path, b: str | Path) -> bool:
    x, y = _path(a), _path(b)
    return x == y or x.is_relative_to(y) or y.is_relative_to(x)


def _lexical_overlap(a: str, b: str) -> bool:
    a, b = a.rstrip("/"), b.rstrip("/")
    if os.name == "nt":
        a, b = a.casefold(), b.casefold()
    return a == b or a.startswith(b + "/") or b.startswith(a + "/")


def _allowed(p: Path) -> None:
    rel = p.relative_to(ROOT)
    if (len(rel.parts) < 3 or rel.parts[0].casefold() != "bots" or
            rel.parts[1].casefold() in EXCLUDED | EXTERNALLY_OWNED):
        raise ArenaError(f"development claims must stay in a nonexcluded suite: {rel.as_posix()}")
    if any(part in {".git", ".codex", ".agents"} for part in rel.parts):
        raise ArenaError(f"protected scope: {rel.as_posix()}")


def _hash_inputs(paths) -> dict[str, str]:
    result = {}
    for raw in sorted(set(str(p) for p in paths)):
        p = _path(raw)
        if p.is_dir():
            files = sorted(f for f in p.rglob("*") if f.is_file() and ".cambc" not in f.parts and
                           "__pycache__" not in f.parts and f.suffix != ".pyc")
        else:
            files = [p]
        for f in files:
            result[_rel(f)] = hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else "missing"
    return result


def dependency_graph() -> dict[str, list[str]]:
    """Actual include closure, including shared headers housed beneath bot directories."""
    return {b.id: [_rel(p) for p in core.closure(b)] for b in core.all_bots()
            if b.suite.casefold() not in EXCLUDED and not b.name.startswith("_")}


def affected_bots(paths, graph=None) -> list[str]:
    graph = dependency_graph() if graph is None else graph
    canonical = [_rel(_path(p)) for p in paths]
    return sorted(bot for bot, files in graph.items() if any(_lexical_overlap(p, f) for p in canonical for f in files))


def _read_inputs(bot: str, graph: dict) -> list[str]:
    suite = bot.split("/", 1)[0]
    inputs = list(graph.get(bot, []))
    for raw in ("AGENTS.md", "README.md", "bots/README.md", "docs/workflow.md",
                "docs/rules-notes.md", "docs/reference/GameRules.md", "sdk/cpp/cambc.h",
                "docs/reference/current-version.json", f"bots/{suite}/README.md"):
        if (ROOT / raw).is_file():
            inputs.append(raw)
    tests = ROOT / "bots" / suite / "tests"
    inputs.append(f"bots/{suite}/tests")  # pins additions/removals as well as existing file bytes
    if tests.is_dir():
        inputs += [_rel(p) for p in tests.rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    return sorted(set(inputs))


def _current_source(bot: str) -> dict[str, str]:
    b = next((b for b in core.all_bots() if b.id == bot), None)
    if b is None:
        raise ArenaError(f"bot disappeared: {bot}")
    return _hash_inputs(core.closure(b))


def _validate_assessment(row: dict, graph: dict) -> None:
    identity = row.get("identity")
    if not isinstance(identity, dict):
        return
    source = identity.get("working_source", identity.get("source"))
    if source is not None and source != _hash_inputs(graph[row["bot"]]):
        raise ArenaError(f"cached assessment source changed for {row['bot']}; refresh population inventory")
    if identity.get("contract") is not None and identity["contract"] != _digest(row.get("contract", {})):
        raise ArenaError(f"cached assessment contract changed for {row['bot']}; refresh population inventory")
    if identity.get("tests") is not None:
        suite = row["bot"].split("/", 1)[0]
        current = _hash_inputs([f"bots/{suite}/tests"] + list((row.get("contract") or {}).get("test_paths", [])) +
                               list(identity["tests"]))
        current = {p: None if h == "missing" else h for p, h in current.items()}
        if current != identity["tests"]:
            raise ArenaError(f"cached assessment tests changed for {row['bot']}; refresh population inventory")
    if identity.get("observers") is not None:
        current = _hash_inputs(identity["observers"].keys())
        current = {p: None if h == "missing" else h for p, h in current.items()}
        if current != identity["observers"]:
            raise ArenaError(f"cached assessment observers changed for {row['bot']}; refresh population inventory")


def _offer(row: dict, kind: str, graph: dict, reverse: dict | None = None) -> dict:
    bot = row["bot"]
    suite, name = bot.split("/", 1)
    own = ROOT / "bots" / suite / name / "src"
    # A bot directory can house shared headers. Ordinary bot work excludes those files.
    own_relative = own.relative_to(ROOT).as_posix()
    reverse = reverse if reverse is not None else {f: {b for b, files in graph.items() if f in files} for files in graph.values() for f in files}
    unique = [f for f in graph.get(bot, []) if f.startswith(own_relative + "/") and len(reverse.get(f, set())) == 1]
    contract = row.get("contract") or {}
    gaps = list(row.get("gaps") or [])
    questions = {
        "repair": "Which observed health defect prevents this interaction from being reliable?",
        "activation": "Does the declared mechanism execute physically and continue when its trigger exists?",
        "differentiation": "Which matched physical interaction would disappear if this bot were replaced by its neighbor?",
        "evidence": "Which missing exact-version observation would settle admission or identify the next obstruction?",
        "exploration": "Can this doctrine produce an uncovered interaction, including an unusual one, without converging on another policy?",
    }
    roots = _read_inputs(bot, graph)
    roots += list(contract.get("test_paths", [])) + list(contract.get("observer_paths", []))
    identity = row.get("identity")
    if isinstance(identity, dict):
        roots += list(identity.get("tests", {})) + list(identity.get("observers", {}))
    roots = sorted(set(roots))
    inputs = _hash_inputs(roots)
    offer = {
        "bot": bot, "kind": kind, "question": questions[kind], "owned_paths": sorted(unique),
        "affected_bots": [bot] if unique else [], "read_inputs": inputs, "read_input_roots": roots,
        "source_identity": {p: inputs[p] for p in graph.get(bot, [])},
        "preserve": contract.get("interaction") or f"The commitment in bots/{suite}/README.md; establish its contract first.",
        "evidence": gaps or ["Complete native activation, continuation/refusal, neighbor/control comparison and health evidence."],
        "next_check": contract.get("next_check") or row.get("next_check") or "Obtain the missing evidence without changing the doctrine.",
        "stop": "Report the decisive observation or a precise obstruction; do not manufacture admission or optimize away the commitment.",
        "state": row.get("state", "unassessed"), "contract": contract.get("id"),
        "shared_scope_excluded": sorted(set(graph.get(bot, [])) - set(unique)),
    }
    offer["revision"] = _digest(inputs)
    offer["id"] = f"{kind}-{suite}-{name}-{_digest(offer)[:12]}"
    return offer


def derive_offers(rows=None) -> list[dict]:
    if rows is None:
        import population
        rows = population.inventory()
    graph = dependency_graph()
    reverse = {}
    for bot, files in graph.items():
        for f in files:
            reverse.setdefault(f, set()).add(bot)
    offers = []
    for row in rows:
        if row.get("bot") not in graph or row.get("state") in {"ready", "parked"} or row.get("externally_owned") or row["bot"].split("/", 1)[0] in EXTERNALLY_OWNED:
            continue
        _validate_assessment(row, graph)
        kind = {"repair": "repair", "activate": "activation", "differentiate": "differentiation"}.get(row.get("state"), "evidence")
        offers.append(_offer(row, kind, graph, reverse))
    # Exploration remains visible during repair. A separate offer cannot silently expand
    # an existing worker's scope and is subject to exactly the same conflict checks.
    exploratory = next((r for r in rows if r.get("bot") in graph and r.get("state") != "parked" and not r.get("externally_owned") and r["bot"].split("/", 1)[0] not in EXTERNALLY_OWNED), None)
    if exploratory:
        offers.append(_offer(exploratory, "exploration", graph, reverse))
    priority = {"repair": 0, "activation": 2, "differentiation": 3, "evidence": 3, "exploration": 4}
    return sorted(offers, key=lambda o: (0 if o["state"] == "stale" else priority[o["kind"]], o["bot"], o["id"]))


def _state() -> dict:
    path = CACHE / "claims.json"
    if not path.is_file():
        return {"schema": 1, "reconciled": False, "claims": []}
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
        if state.get("schema") != 1 or not isinstance(state.get("claims"), list):
            raise ValueError("unknown claim state")
        return state
    except (OSError, ValueError, TypeError) as exc:
        raise ArenaError(f"cannot read ownership state; reconcile without overwriting it: {exc}") from exc


def _save(state: dict) -> None:
    CACHE.mkdir(parents=True, exist_ok=True)
    tmp = CACHE / "claims.json.tmp"
    tmp.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8", newline="\n")
    tmp.replace(CACHE / "claims.json")


def status() -> dict:
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        state["notice"] = "Cache state does not discover live workers. Reconcile known workers before new assignments. No claim expires."
        return state


def reconcile_workers(note: str) -> dict:
    """Coordinator acknowledgement after inspecting existing workers, also after cache loss."""
    if not note.strip():
        raise ArenaError("record how known workers and live scopes were reconciled")
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        state.update(reconciled=True, reconciliation={"at": _now(), "note": note})
        _save(state)
        return state["reconciliation"]


def _find(state: dict, ident: str) -> dict:
    found = next((c for c in state["claims"] if c["id"] == ident), None)
    if found is None:
        raise ArenaError(f"unknown claim {ident}")
    return found


def _validate_offer(offer: dict) -> None:
    if offer.get("kind") not in KINDS or not offer.get("owned_paths"):
        raise ArenaError("offer needs a supported kind and explicit owned paths; shared-only bots need a module offer")
    if not isinstance(offer.get("bot"), str) or offer["bot"].split("/", 1)[0].casefold() in EXCLUDED | EXTERNALLY_OWNED:
        raise ArenaError("offer bot is excluded or externally owned; explicit ownership handoff required")
    for raw in offer["owned_paths"]:
        _allowed(_path(raw))
    current = _hash_inputs(offer.get("read_input_roots", offer.get("read_inputs", {}).keys()))
    if current != offer.get("read_inputs"):
        raise ArenaError("offer inputs changed; derive a fresh offer before claiming")
    if offer.get("source_identity") != _current_source(offer["bot"]):
        raise ArenaError("offer source closure changed; derive a fresh offer before claiming")


def claim(offer: dict, worker: str) -> dict:
    _validate_offer(offer)
    if not worker.strip():
        raise ArenaError("worker must be named")
    graph = dependency_graph()
    affected = affected_bots(offer["owned_paths"], graph)
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        if not state.get("reconciled"):
            raise ArenaError("reconcile known workers first (orchestrate reconcile-workers --note ...); absent cache does not prove no owners")
        for old in state["claims"]:
            if old.get("state") not in ACTIVE:
                continue
            direct = any(_overlap(a, b) for a in offer["owned_paths"] for b in old["owned_paths"])
            shared = set(affected) & set(old.get("affected_bots", []))
            if direct or shared:
                raise ArenaError(f"scope conflicts with {old['id']} held by {old['worker']} ({old['state']}); explicit handoff required")
        # Check again under the ownership lock: two claims cannot pass on incompatible revisions.
        _validate_offer(offer)
        ident = f"claim-{len(state['claims'])+1:04d}-{_digest([offer['id'],worker,_now()])[:8]}"
        rec = {**offer, "offer": offer["id"], "id": ident, "worker": worker, "state": "claimed",
               "affected_bots": affected, "claimed_at": _now(), "boundary": None,
               "initial_owned": _hash_inputs(offer["owned_paths"])}
        state["claims"].append(rec)
        _save(state)
        return rec


def start(ident: str, worker: str) -> dict:
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        c = _find(state, ident)
        if c["worker"] != worker or c["state"] != "claimed":
            raise ArenaError("only the named claimant may start a claimed assignment")
        c.update(state="running", started_at=_now())
        _save(state)
        return c


def checkpoint(ident: str, worker: str) -> dict:
    """Record a coherent source boundary before preparing immutable games.

    The coordinator/worker supplies the boundary only after finishing edits. Other claims that
    could change any affected bot are blocked for the claim's whole lifetime.
    """
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        c = _find(state, ident)
        if c["worker"] != worker or c["state"] not in {"claimed", "running"}:
            raise ArenaError("only an active claimant can declare its source boundary")
        graph = dependency_graph()
        affected = affected_bots(c["owned_paths"], graph)
        for old in state["claims"]:
            if old["id"] == ident or old.get("state") not in ACTIVE:
                continue
            if set(affected) & set(affected_bots(old["owned_paths"], graph)):
                raise ArenaError(f"expanded source closure conflicts with {old['id']} held by {old['worker']}; explicit handoff required")
        inputs = _hash_inputs(set(c["read_inputs"]) | set(_read_inputs(c["bot"], graph)))
        source = _hash_inputs(graph.get(c["bot"], []))
        c["affected_bots"] = affected
        c["boundary"] = {"at": _now(), "inputs": inputs, "source": source, "revision": _digest(inputs)}
        _save(state)
        return c["boundary"]


def _validate_result(c: dict, result: dict) -> None:
    required = {"changed_paths", "evidence_paths", "observed_contribution", "limitations", "next_obstruction", "coherent_boundary", "relinquished"}
    if not required.issubset(result):
        raise ArenaError("result missing: " + ", ".join(sorted(required - set(result))))
    if result["coherent_boundary"] is not True or result["relinquished"] is not True:
        raise ArenaError("finish requires a coherent source boundary and relinquished ownership")
    if not isinstance(result["changed_paths"], dict) or not isinstance(result["evidence_paths"], list):
        raise ArenaError("changed_paths must map repository paths to SHA-256 hashes; evidence_paths must be a list")
    if not isinstance(result["observed_contribution"], str) or not isinstance(result["next_obstruction"], str) or not isinstance(result["limitations"], list):
        raise ArenaError("observed_contribution/next_obstruction must be text and limitations must be a list")
    current_owned = _hash_inputs(c["owned_paths"])
    initial_owned = c.get("initial_owned", {})
    changed = {p for p in set(initial_owned) | set(current_owned) if initial_owned.get(p) != current_owned.get(p)}
    missing = changed - set(result["changed_paths"])
    if missing:
        raise ArenaError("result omits changed owned paths: " + ", ".join(sorted(missing)))
    for raw, digest in result["changed_paths"].items():
        p = _path(raw)
        if not any(_overlap(p, scope) and (p == _path(scope) or p.is_relative_to(_path(scope))) for scope in c["owned_paths"]):
            raise ArenaError(f"result writes outside claim: {raw}")
        if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            raise ArenaError(f"changed file missing or hash mismatch: {raw}")
    for raw in result["evidence_paths"]:
        if not _path(raw).is_file():
            raise ArenaError(f"evidence unavailable: {raw}")


def finish(ident: str, worker: str, result: dict) -> dict:
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        c = _find(state, ident)
        if c["worker"] != worker or c["state"] not in {"claimed", "running", "interrupted"}:
            raise ArenaError("only the active claimant can report its result")
        _validate_result(c, result)
        c.update(state="reported", result=result, reported_at=_now(),
                 final_read_inputs=_hash_inputs(c["read_inputs"].keys()))
        _save(state)
        return c


def reconcile(ident: str, disposition: str, note: str, worker_stopped: bool = False) -> dict:
    if disposition not in {"verified", "returned", "interrupted"} or not note.strip():
        raise ArenaError("use verified, returned or interrupted and record the evidence/reason")
    with FileLock(CACHE / "claims.lock", timeout=30):
        state = _state()
        c = _find(state, ident)
        if c["state"] not in ACTIVE:
            raise ArenaError("claim is already released")
        if disposition != "interrupted":
            if not worker_stopped:
                raise ArenaError("coordinator must verify the worker stopped writing; timeouts never release ownership")
            if c["state"] != "reported" and disposition == "verified":
                raise ArenaError("verification requires a structured worker result")
            if c["state"] == "reported":
                _validate_result(c, c["result"])
        c.update(state=disposition, reconciled_at=_now(), reconciliation_note=note)
        _save(state)
        return c


def _emit(value) -> None:
    print(json.dumps(value, indent=2))


def _cmd_next(args) -> int:
    offers = derive_offers()
    state = status()
    active = [c for c in state["claims"] if c["state"] in ACTIVE]
    for offer in offers:
        offer["blocked_by"] = [c["id"] for c in active if set(c.get("affected_bots", [])) & set(offer["affected_bots"])]
    if args.json:
        _emit({"reconciled": state.get("reconciled", False), "offers": offers})
    else:
        for offer in offers:
            print(f"{offer['id']}  {offer['bot']}  {offer['kind']}" + (f" [blocked by {','.join(offer['blocked_by'])}]" if offer["blocked_by"] else ""))
        print(f"{len(offers)} offers; coordinator chooses; no automatic dispatch or expiration")
    return 0


def _cmd_claim(args) -> int:
    if args.offer_file:
        offer = json.loads(_path(args.offer_file).read_text(encoding="utf-8"))
    else:
        offer = next((o for o in derive_offers() if o["id"] == args.offer), None)
        if offer is None:
            raise ArenaError("offer no longer current; run orchestrate next --json")
    if args.path:
        # The coordinator may claim a shared adapter/README or a shared runtime module instead
        # of the ordinary bot-only scope. This never widens an existing live claim.
        offer = dict(offer)
        offer["owned_paths"] = sorted({_rel(_path(p)) for p in args.path})
        offer["read_input_roots"] = sorted(set(offer.get("read_input_roots", offer["read_inputs"].keys())) | set(offer["owned_paths"]))
        offer["read_inputs"] = _hash_inputs(offer["read_input_roots"])
        offer["affected_bots"] = affected_bots(offer["owned_paths"])
        offer["revision"] = _digest(offer["read_inputs"])
    _emit(claim(offer, args.worker))
    return 0


def register(sub) -> None:
    p = sub.add_parser("orchestrate", help="derive local work offers and reconcile exclusive development ownership")
    commands = p.add_subparsers(dest="operation", required=True)
    nxt = commands.add_parser("next", help="derive offers; never dispatch workers")
    nxt.add_argument("--json", action="store_true")
    nxt.set_defaults(func=_cmd_next)
    st = commands.add_parser("status", help="show claims, including interrupted ownership")
    st.set_defaults(func=lambda a: _emit(status()))
    rw = commands.add_parser("reconcile-workers", help="record inspection of known live workers before first claims")
    rw.add_argument("--note", required=True)
    rw.set_defaults(func=lambda a: _emit(reconcile_workers(a.note)))
    cl = commands.add_parser("claim", help="claim a current offer or an explicitly scoped coordinator offer JSON")
    cl.add_argument("offer", nargs="?", default="")
    cl.add_argument("--offer-file", default="")
    cl.add_argument("--path", action="append", help="explicit coordinator write scope; repeat for adapter/README or shared module handoff")
    cl.add_argument("--worker", required=True)
    cl.set_defaults(func=_cmd_claim)
    for name, fn in (("start", start), ("checkpoint", checkpoint)):
        cp = commands.add_parser(name)
        cp.add_argument("claim")
        cp.add_argument("--worker", required=True)
        cp.set_defaults(func=lambda a, fn=fn: _emit(fn(a.claim, a.worker)))
    fi = commands.add_parser("finish", help="report a structured result; ownership remains held until reconciliation")
    fi.add_argument("claim")
    fi.add_argument("--worker", required=True)
    fi.add_argument("--result", required=True)
    fi.set_defaults(func=lambda a: _emit(finish(a.claim, a.worker, json.loads(_path(a.result).read_text(encoding="utf-8")))))
    re = commands.add_parser("reconcile", help="coordinator records verification or a coherent handoff")
    re.add_argument("claim")
    re.add_argument("--disposition", choices=("verified", "returned", "interrupted"), required=True)
    re.add_argument("--note", required=True)
    re.add_argument("--worker-stopped", action="store_true")
    re.set_defaults(func=lambda a: _emit(reconcile(a.claim, a.disposition, a.note, a.worker_stopped)))

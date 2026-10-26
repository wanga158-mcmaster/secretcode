"""Read-only per-game diagnostics for frontier bots on one standard `cambc run` output directory.

    python bots/frontier/tools/analyze_native.py OUTDIR --candidate-side A|B --bot NAME [--output FILE]

OUTDIR holds statistics.json (from `cambc run ... --statistics`) and one replay bundle
(*.repl26a in OUTDIR/replay/ or OUTDIR). The engine identity must be registered in
tools/replay-profiles.json. Reports per-actor purchases, camp payouts, gem submissions,
fuel exhaustion, native faults, `p10 ...` runtime log records (decisions, phases, receipts,
validation errors) and prediction-timing calibration for the candidate side.

This does not run an engine, decode omniscient snapshots, or establish strength.
Native event timestamps are authoritative; stdout framing and RLE indices are
not round clocks. Missing evidence stays unknown rather than becoming zero.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[3]
PROFILE_PATH = ROOT / "tools/replay-profiles.json"
SIDES = ("teamA", "teamB")
LIMIT = 64 * 1024 * 1024
SAMPLE_LIMIT = 12
TASKS = ("independent", "farm", "capture", "bank", "recover", "move", "inspect", "attack",
         "heal", "shield", "cast", "wait", "buy", "lock", "priority", "release")
RECEIPTS = ("none", "attack", "cast", "payout", "bank", "purchase", "death", "blocked", "heal", "priority")
SIDE_ALIASES = {"a": "teamA", "b": "teamB", "teama": "teamA", "teamb": "teamB", "0": "teamA", "1": "teamB"}
LOG = re.compile(rb"p10 (?:decision|prediction|phase|witnessed|event|error|invalidate|blocked|pulse-veto)[^\r\n\x00]*")
GUEST_ERROR = re.compile(rb"(?:guest[_ ]?error|wasm[_ ]?trap|RuntimeError:|guest panicked|unreachable instruction|bot fault)[^\r\n\x00]*", re.I)
KV = re.compile(r"\b(actor|round|r|source|group|task|template|owner|kind|gemsLower|gems|predictionEvent|predicted_event|event|condition|phase|fuel|complete|groups)=([^ ]+)")


def _integer(value):
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str) and re.fullmatch(r"-?\d+", value):
        return int(value)
    return None


def _side(value):
    return SIDE_ALIASES.get(str(value).lower())


def _round(event):
    return _integer(event.get("timestamp", {}).get("round"))


def _order(event):
    t = event.get("timestamp", {})
    ordinal = _integer(t.get("ordinal"))
    return (ordinal if ordinal is not None else -1, _round(event) or 0,
            _integer(t.get("input")) or 0, _integer(t.get("substep")) or 0)


def _row(event):
    owner = event.get("owner", {})
    result = {"actor": owner.get("id"), "team": owner.get("team"), "kind": event.get("kind"),
              "timestamp": event.get("timestamp"), "value": _integer(event.get("value")),
              "target": event.get("target"), "context": event.get("context")}
    if "amount" in event:
        result["amount"] = event["amount"]
    return result


def _ranges(counts):
    """Losslessly encode every observed round as [start,end,eventsPerRound]."""
    result = []
    for r, count in sorted(counts.items()):
        if result and result[-1][1] + 1 == r and result[-1][2] == count:
            result[-1][1] = r
        else:
            result.append([r, r, count])
    return result


class _Inputs:
    """Reads evidence files from disk only and records their size and hash."""

    def __init__(self):
        self.evidence = []

    def read(self, path):
        path = Path(path)
        if not path.is_file():
            return None
        if path.stat().st_size > LIMIT:
            raise ValueError(f"Input exceeds64MiB: {path}")
        data = path.read_bytes()
        if len(data) > LIMIT:
            raise ValueError(f"Input exceeds64MiB: {path}")
        self.evidence.append({"path": str(path), "storage": "file", "bytes": len(data), "sha256": sha256(data).hexdigest()})
        return data

    def replays(self, directory):
        return sorted(Path(directory).glob("*.repl26a"))


def _logs(data, side, member):
    rows = []
    for match in LOG.finditer(data):
        text = match.group().decode("utf-8", errors="replace")
        fields = dict(KV.findall(text))
        row = {"side": side, "member": member, "offset": match.start(), "text": text[:800]}
        for key in ("actor", "round", "r", "source", "group", "task", "template", "owner", "kind", "gemsLower", "gems"):
            if key in fields:
                row[key] = _integer(fields[key])
        if "round" not in row and "r" in row:
            row["round"] = row["r"]
        for key in ("condition", "phase"):
            if key in fields:
                row[key] = fields[key]
        for key in ("complete", "groups"):
            if key in fields:
                row[key] = _integer(fields[key])
        if "fuel" in fields:
            row["fuel"] = [_integer(v) for v in fields["fuel"].split("/")]
        predicted = re.search(r"\bpredicted=\[(-?\d+),(-?\d+)\]", text)
        if predicted and int(predicted[1]) >= 0 and int(predicted[2]) >= int(predicted[1]):
            row["prediction"] = {"minimum": int(predicted[1]), "maximum": int(predicted[2]),
                                 "event": fields.get("predictionEvent", fields.get("predicted_event"))}
        reason = re.search(r"\breason=(.*?)(?: fuel=|$)", text)
        if reason:
            row["reason"] = reason[1][:300]
        if text.startswith("p10 decision"):
            row["type"] = "decision"
        elif text.startswith("p10 prediction"):
            row["type"] = "prediction"
        elif text.startswith("p10 phase"):
            row["type"] = "phase"
        elif text.startswith("p10 error"):
            row["type"] = "validation_error"
        elif text.startswith(("p10 witnessed", "p10 event")):
            row["type"] = "witnessed"
            kind = row.get("kind")
            row["receipt"] = RECEIPTS[kind] if kind is not None and 0 <= kind < len(RECEIPTS) else "unknown"
        elif text.startswith("p10 invalidate"):
            row["type"] = "invalidation"
            row["invalidation"] = text.split()[2] if len(text.split()) > 2 else "unknown"
        else:
            row["type"] = "invalidation_or_fallback"
        rows.append(row)
    return rows


def _fuel(events, roster, side, complete, issues):
    actors = []
    total = 0
    covered = True
    for actor in roster:
        if actor.get("team") != side:
            continue
        groups = [g for g in events if g.get("kind") == "fuelExhausted" and g.get("owner", {}).get("team") == side and g.get("owner", {}).get("id") == actor.get("id")]
        if len(groups) != 1:
            covered = False
            issues.append({"kind": "fuel_owner_coverage", "actor": actor.get("id"), "groups": len(groups)})
        entries = [e for g in groups for e in g.get("entries", [])]
        counts = Counter(_round(e) for e in entries if _round(e) is not None)
        total += len(entries)
        actors.append({"actor": actor.get("id"), "name": actor.get("name"), "observedCount": len(entries),
                       "roundRanges": _ranges(counts), "unknownRoundCount": sum(_round(e) is None for e in entries),
                       "first": min(entries, key=_order).get("timestamp") if entries else None})
    return {"observedCount": total, "totalCount": total if complete and covered else None,
            "completeOwnerCoverage": covered, "roundRangeEncoding": "[inclusiveStart,inclusiveEnd,eventsPerRound]",
            "byActor": actors}


def _economy(rows, actors, teams, side, complete):
    mine = [e for e in rows if e.get("owner", {}).get("team") == side]
    bank = sorted((e for e in mine if e["kind"] == "gemSubmission"), key=_order)
    clear = [e for e in mine if e["kind"] == "campClear"]
    payout = sorted((e for e in mine if e["kind"] == "gemCollection" and isinstance(e.get("target"), dict) and e["target"].get("kind") == "campGroup" and e.get("timestamp", {}).get("phase") == "campCaptures"), key=_order)
    purchases = sorted((e for e in mine if e["kind"] in ("buyItem", "buyConsumable")), key=_order)
    occurrences = defaultdict(list)
    for e in payout:
        occurrences[(e["target"].get("id"), _round(e))].append(e)
    capture_rows = [{"group": group, "round": r, "actors": sorted({e["owner"]["id"] for e in entries}),
                     "observedGems": sum(_integer(e.get("value")) or 0 for e in entries),
                     "timestamp": min(entries, key=_order).get("timestamp")}
                    for (group, r), entries in occurrences.items()]
    capture_rows.sort(key=lambda e: (e["round"] if e["round"] is not None else -1, e["group"] or 0))
    renewal = []
    previous = {}
    for e in capture_rows:
        if e["group"] in previous and e["round"] != previous[e["group"]]:
            renewal.append({"group": e["group"], "previousCaptureRound": previous[e["group"]],
                            "renewedCaptureRound": e["round"], "actors": e["actors"]})
        previous[e["group"]] = e["round"]
    loops = []
    by_actor_group = defaultdict(list)
    for e in payout:
        by_actor_group[(e["owner"]["id"], e["target"]["id"])].append(e)
    for (actor, group), receipts in by_actor_group.items():
        for first, following in zip(receipts, receipts[1:]):
            banks = [e for e in bank if e["owner"]["id"] == actor and _order(first) < _order(e) < _order(following)]
            bought = [e for e in purchases if e["owner"]["id"] == actor and _order(first) < _order(e) < _order(following)]
            if banks:
                loops.append({"actor": actor, "group": group, "capture": first["timestamp"], "bank": banks[0]["timestamp"],
                              "renewedCapture": following["timestamp"], "purchase": bought[0]["timestamp"] if bought else None})
    submitted = next((_integer(t.get("submittedGems")) for t in teams if t.get("team") == side), None)
    event_score = sum(_integer(e.get("value")) or 0 for e in bank)
    metrics = {}
    for name in ("earnedGold", "spentGold", "collectedGems", "submittedGems", "gems", "gold", "monsterDamageDone", "monsterDamageTaken", "deaths", "kills"):
        values = [a.get("metrics", {}).get(name) for a in actors if a.get("team") == side]
        metrics[name] = sum(float(v) if isinstance(v, float) else int(v) for v in values) if values and all(v is not None for v in values) else None
    return {"evidenceState": "observed", "submittedGems": submitted, "submissionEventGems": event_score,
            "submissionEvents": len(bank), "submissionSumMatches": event_score == submitted if complete and submitted is not None else None,
            "firstBank": _row(bank[0]) if bank else None, "campClearCredits": len(clear),
            "captureOccurrencesObserved": len(capture_rows), "captureRecipientsObserved": len(payout),
            "firstCapture": capture_rows[0] if capture_rows else None, "captures": capture_rows[:SAMPLE_LIMIT],
            "capturesTruncated": len(capture_rows) > SAMPLE_LIMIT, "personalPurchaseEvents": len(purchases),
            "firstPurchase": _row(purchases[0]) if purchases else None,
            "purchasesByActor": dict(Counter(str(e["owner"]["id"]) for e in purchases)),
            "renewedCapturesObserved": len(renewal), "renewalExamples": renewal[:SAMPLE_LIMIT],
            "captureBankRenewalLoops": len(loops), "capturePurchaseBankRenewalLoops": sum(e["purchase"] is not None for e in loops),
            "loopExamples": loops[:SAMPLE_LIMIT], "actorMetricTotals": metrics,
            "limits": ["campClear is kill credit, not payout ownership", "capture occurrences use campCaptures gemCollection, so gold-only captures may be absent",
                       "repeated capture proves renewed productive supply; exact respawn/occupation interval is unknown",
                       "itemCombination is not a paid purchase", "terminal totals are omniscient observations; no competitive inference"]}


def _predictions(logs, rows, side):
    predicted = [d for d in logs if "prediction" in d]
    tagged = [d for d in predicted if d["prediction"].get("event") in ("bank", "payout", "capture", "renewal")]
    matches = []
    def source(d):
        return d.get("source", d.get("group"))

    def after(other, d):
        r, issued = other.get("round"), d.get("round")
        if r is None or issued is None:
            return False
        return r > issued or (r == issued and other.get("member") == d.get("member") and other.get("offset", -1) > d.get("offset", -1))

    def barrier(d, event):
        """Stop one intent at its first known lifecycle/owner discontinuity.

        Native capture/channel completions precede the owner's stdout that
        round, so a same-round later log cannot cancel a witnessed completion.
        A native death/fuel event can be ordered exactly against that event.
        """
        stops = []
        final = _round(event) if event else None
        capture = d["prediction"]["event"] != "bank"
        for other in logs:
            if not after(other, d) or (final is not None and other["round"] >= final):
                continue
            mine = other.get("actor") == d.get("actor")
            state = reason = None
            if other["type"] == "prediction" and "prediction" in other and mine and source(other) == source(d):
                state, reason = "superseded", "newer_same_actor_source_prediction"
            elif other["type"] == "decision" and mine:
                state, reason = "superseded", "newer_actor_decision"
            elif other["type"] == "witnessed" and other.get("receipt") == "death" and mine:
                state, reason = "invalidated", "witnessed_death"
            elif other["type"] == "invalidation":
                invalidation = other.get("invalidation")
                relevant = invalidation == "suspended" or (capture and invalidation == "capture" and source(other) == source(d))
                if relevant and mine:
                    state, reason = "invalidated", "actor_" + invalidation
                elif relevant and other.get("actor") is None:
                    state, reason = "conditional_unknown", "unattributed_legacy_" + invalidation
            if state:
                stops.append({"state": state, "reason": reason, "round": other["round"], "actor": other.get("actor"),
                              "source": source(other), "member": other.get("member"), "offset": other.get("offset")})
        for native in rows:
            r = _round(native)
            if r is None or d.get("round") is None or r < d["round"] or (event and _order(native) >= _order(event)):
                continue
            owner = native.get("owner", {})
            mine = owner.get("team") == side and owner.get("id") == d.get("actor")
            if mine and native["kind"] in ("death", "fuelExhausted"):
                stops.append({"state": "invalidated", "reason": "native_" + native["kind"], "round": r,
                              "actor": d.get("actor"), "timestamp": native.get("timestamp")})
            # A witnessed intervening payout closes the old source lifecycle.
            # It cannot be credited to an absent promised recipient hundreds of
            # rounds later. Same-round party recipients belong to one payout.
            target = native.get("target")
            if capture and r > d["round"] and (final is None or r < final) and native["kind"] == "gemCollection" and isinstance(target, dict) and target.get("kind") == "campGroup" and target.get("id") == source(d) and native.get("timestamp", {}).get("phase") == "campCaptures":
                stops.append({"state": "invalidated", "reason": "intervening_source_capture", "round": r,
                              "actor": owner.get("id"), "source": source(d), "timestamp": native.get("timestamp")})
        return min(stops, key=lambda stop: stop["round"]) if stops else None

    for d in tagged:
        kind = d["prediction"]["event"]
        native_kind = "gemSubmission" if kind == "bank" else "gemCollection"
        candidates = [e for e in rows if e.get("owner", {}).get("team") == side and e["kind"] == native_kind and
                      e["owner"].get("id") == d.get("actor") and _round(e) is not None and d.get("round") is not None and _round(e) > d["round"]]
        if kind != "bank":
            candidates = [e for e in candidates if isinstance(e.get("target"), dict) and e["target"].get("kind") == "campGroup" and e["target"].get("id") == d.get("source", d.get("group")) and e.get("timestamp", {}).get("phase") == "campCaptures"]
        if kind == "renewal" and not any(e["kind"] == "gemCollection" and e.get("owner", {}).get("team") == side and isinstance(e.get("target"), dict) and e["target"].get("kind") == "campGroup" and e["target"].get("id") == d.get("source", d.get("group")) and _round(e) is not None and _round(e) <= d.get("round", -1) for e in rows):
            candidates = []
        event = min(candidates, key=_order) if candidates else None
        stopped = barrier(d, event)
        unknown = not d.get("condition")
        result = {"actor": d.get("actor"), "source": d.get("source"), "issuedRound": d.get("round"), "prediction": d["prediction"],
                  "condition": d.get("condition", "unspecified"),
                  "observedTimestamp": event.get("timestamp") if event and not stopped and not unknown else None,
                  "state": stopped["state"] if stopped else "conditional_unknown" if unknown else "matched" if event else "unobserved_or_censored"}
        if stopped:
            result["endedBy"] = stopped
        elif unknown:
            result["unknownReason"] = "condition_not_logged"
        if event and not stopped and not unknown:
            result["insideInterval"] = d["prediction"]["minimum"] <= _round(event) <= d["prediction"]["maximum"]
        matches.append(result)
    return {"evidenceState": "predicted", "intervalsLogged": len(predicted), "explicitEventIntervals": len(tagged),
            "unboundIntervals": len(predicted) - len(tagged), "comparisons": matches[:SAMPLE_LIMIT],
            "comparisonCount": len(matches), "outsideInterval": sum(m.get("insideInterval") is False for m in matches),
            "states": dict(Counter(m["state"] for m in matches)),
            "limits": ["unlabelled predicted intervals cannot be assigned to capture versus bank", "a matching event is conditional calibration, not proof of the entire script",
                       "capture invalidation, owner death/suspension, intervening source capture or a newer intent ends matching",
                       "missing conditions and unattributed legacy invalidations remain unknown; no actor identity is invented",
                       "superseded or unobserved intervals remain unknown; no missed event is invented"]}


def analyze(outdir, candidateSide, bot):
    """Analyze one retained attempt. candidateSide is absolute A/B or teamA/teamB."""
    side = _side(candidateSide)
    if side is None:
        raise ValueError("candidateSide must be A, B, teamA or teamB")
    directory = Path(outdir).resolve()
    inputs = _Inputs()
    issues = []
    failures = []
    coverage = {"nativeStatistics": False, "completeNativeEventCapture": None,
                "candidateStdout": False, "candidateDebug": False, "opaqueStdoutFramingDecoded": False,
                "candidateStdoutCaptureComplete": None,
                "nativeSnapshotsDecoded": False, "strengthEvaluated": False}
    report = {"schemaVersion": 1, "candidate": {"bot": bot, "side": side}, "outdir": str(directory),
              "evidence": inputs.evidence, "coverage": coverage, "issues": issues, "firstFailures": failures}
    stats = None
    raw = inputs.read(directory / "statistics.json")
    if raw is not None:
        try:
            stats = json.loads(raw.decode("utf-8-sig"))
            if stats.get("formatVersion") != "1.0" or stats.get("jsonFormatVersion") != "1.0" or stats.get("match", {}).get("metricSemanticsVersion") != 1:
                raise ValueError("Unsupported native statistics schema")
            if not all(isinstance(stats.get(k), list) for k in ("teams", "champions", "events")):
                raise ValueError("Incomplete native statistics arrays")
        except (ValueError, TypeError) as error:
            issues.append({"kind": "statistics_unreadable", "message": str(error)})
            stats = None
    profiles = json.loads(PROFILE_PATH.read_text(encoding="utf-8-sig"))["profiles"]
    rows = []
    if stats is not None:
        coverage["nativeStatistics"] = True
        match = stats["match"]
        matching_profiles = [p for p in profiles if p["engineVersion"] == match.get("engineVersion") and p["engineBuildHash"] == match.get("engineBuildHash")]
        profile = matching_profiles[0] if len(matching_profiles) == 1 else None
        coverage["engineProfile"] = profile.get("id") if profile else None
        if not profile or not re.fullmatch(r"3\.\d+\.\d+", str(profile.get("cliVersion", ""))) or not re.fullmatch(r"[0-9a-f]{64}", str(profile.get("engineBuildHash", ""))):
            issues.append({"kind": "unsupported_engine_identity", "engineVersion": match.get("engineVersion"), "engineBuildHash": match.get("engineBuildHash")})
        coverage["completeNativeEventCapture"] = stats.get("diagnostics", {}).get("detailedCaptureComplete")
        roster = stats.get("roster", {}).get("champions", stats["champions"])
        actor_teams = {a.get("id"): a.get("team") for a in roster}
        valid_groups = []
        for g in stats["events"]:
            if not isinstance(g.get("entries"), list):
                issues.append({"kind": "malformed_event_group", "eventKind": g.get("kind")})
                continue
            owner = g.get("owner", {})
            if owner.get("team") not in SIDES or actor_teams.get(owner.get("id")) != owner.get("team"):
                issues.append({"kind": "invalid_event_owner", "owner": owner})
                continue
            valid_groups.append(g)
            rows.extend(dict(e, kind=g.get("kind"), owner=owner) for e in g["entries"])
        complete = coverage["completeNativeEventCapture"] is True
        diagnostic = stats.get("diagnostics", {})
        report["match"] = {k: match.get(k) for k in ("seed", "outcome", "roundsStarted", "roundsCompleted", "finalRoundPartial", "engineVersion", "engineBuildHash", "mapHash")}
        report["diagnostics"] = {"nativeBotFaults": diagnostic.get("botFaults"), "nativeCodes": diagnostic.get("codes"),
                                 "codesAttribution": "codes without an explicit team remain unattributed"}
        report["fuel"] = {s: _fuel(valid_groups, roster, s, complete, issues) for s in SIDES}
        report["economy"] = _economy(rows, stats["champions"], stats["teams"], side, complete)
        for s in SIDES:
            fault_count = diagnostic.get("botFaults", {}).get(s)
            events = sorted((e for e in rows if e["kind"] == "botFault" and e["owner"]["team"] == s), key=_order)
            report["diagnostics"].setdefault("observedBotFaultEvents", {})[s] = len(events)
            if _integer(fault_count) is None:
                issues.append({"kind": "missing_bot_fault_count", "team": s})
            elif complete and int(fault_count) != len(events):
                issues.append({"kind": "bot_fault_count_mismatch", "team": s, "diagnosticCount": int(fault_count), "observedEvents": len(events)})
            if (_integer(fault_count) or 0) > 0 or events:
                failures.append({"kind": "bot_fault", "team": s, "candidate": s == side, "count": max(_integer(fault_count) or 0, len(events)),
                                 "first": _row(events[0]) if events else None, "nextAction": "Inspect the native fault context and exact owner turn before another launch"})
            if report["fuel"][s]["observedCount"]:
                first = min((e for e in rows if e["kind"] == "fuelExhausted" and e["owner"]["team"] == s), key=_order)
                failures.append({"kind": "fuel_exhaustion", "team": s, "candidate": s == side, "count": report["fuel"][s]["observedCount"],
                                 "first": _row(first), "nextAction": "Bound work before this actor/round; inspect suspended continuation and reserve"})
    else:
        issues.append({"kind": "missing_native_statistics", "message": "Incomplete/interrupted attempt; no zero outcome inferred"})
        report["match"] = None
        report["diagnostics"] = {"nativeBotFaults": None, "nativeCodes": None}
        report["fuel"] = None
        report["economy"] = {"evidenceState": "unknown", "submittedGems": None}
    logs = []
    guest = []
    bundles = inputs.replays(directory / "replay")
    if not bundles:
        bundles = inputs.replays(directory)
    if len(bundles) == 1:
        try:
            with zipfile.ZipFile(BytesIO(inputs.read(bundles[0]))) as archive:
                names = archive.namelist()
                if len(names) != len(set(names)):
                    raise ValueError("Duplicate replay archive members")
                for s, prefix in (("teamA", "team-a"), ("teamB", "team-b")):
                    for suffix in ("stdout", "debug", "stderr"):
                        name = f"{prefix}.{suffix}"
                        if name not in names:
                            continue
                        if archive.getinfo(name).file_size > LIMIT:
                            raise ValueError(f"Replay member exceeds64MiB: {name}")
                        data = archive.read(name)
                        if s == side and suffix in ("stdout", "debug"):
                            coverage["candidate" + suffix.capitalize()] = True
                        logs.extend(_logs(data, s, name))
                        guest.extend({"team": s, "member": name, "offset": m.start(), "text": m.group().decode("utf-8", errors="replace")[:500]} for m in GUEST_ERROR.finditer(data))
        except (zipfile.BadZipFile, ValueError, RuntimeError) as error:
            issues.append({"kind": "replay_unreadable", "message": str(error)})
    else:
        issues.append({"kind": "missing_or_ambiguous_replay", "count": len(bundles)})
    errors = [row for row in logs if row["type"] == "validation_error"]
    report["runtimeValidationErrors"] = {s: {"count": sum(e["side"] == s for e in errors), "examples": [e for e in errors if e["side"] == s][:SAMPLE_LIMIT]} for s in SIDES}
    report["guestErrorMarkers"] = {s: {"count": sum(e["team"] == s for e in guest), "examples": [e for e in guest if e["team"] == s][:SAMPLE_LIMIT]} for s in SIDES}
    for s in SIDES:
        for kind, values in (("runtime_validation", [e for e in errors if e["side"] == s]), ("guest_error_marker", [e for e in guest if e["team"] == s])):
            if values:
                failures.append({"kind": kind, "team": s, "candidate": s == side, "count": len(values), "first": values[0],
                                 "nextAction": "Resolve the exact error before treating this arm as calibrated"})
    decisions = [e for e in logs if e["type"] == "decision" and e["side"] == side]
    activations = Counter((e.get("actor"), e.get("task"), e.get("template")) for e in decisions)
    report["decisions"] = {"evidenceState": "observed_log_records", "count": len(decisions),
                           "activations": [{"actor": actor, "task": TASKS[task] if task is not None and 0 <= task < len(TASKS) else task,
                                            "template": template, "loggedChanges": count} for (actor, task, template), count in activations.items()],
                           "first": decisions[0] if decisions else None, "last": decisions[-1] if decisions else None,
                           "limits": "Logs emit on stage/template changes, so counts are not actor-turn utilization or mechanism success"}
    witnessed = [e for e in logs if e["type"] == "witnessed" and e["side"] == side]
    report["runtimeReceipts"] = {"evidenceState": "bot_observation_log", "counts": dict(Counter(e["receipt"] for e in witnessed)), "examples": witnessed[:SAMPLE_LIMIT]}
    mine_logs = [e for e in logs if e["side"] == side]
    report["timing"] = _predictions(mine_logs, rows, side)
    phases = [e for e in mine_logs if e["type"] == "phase"]
    phase_groups = defaultdict(list)
    for phase in phases:
        phase_groups[phase.get("phase", "unknown")].append(phase)
    report["runtimePhases"] = {"count": len(phases), "byPhase": {name: {"count": len(values), "incomplete": sum(v.get("complete") == 0 for v in values),
        "minimumFuelAfter": min((v["fuel"][-1] for v in values if v.get("fuel") and v["fuel"][-1] is not None), default=None),
        "first": values[0], "last": values[-1]} for name, values in phase_groups.items()},
        "limits": "Only logged owner turns; no allocation or exhaustion inferred from fuel sample indices"}
    economy = report["economy"]
    if economy.get("submittedGems") == 0:
        failures.append({"kind": "zero_secured_output", "team": side, "candidate": True,
                         "captureOccurrencesObserved": economy.get("captureOccurrencesObserved"),
                         "purchaseEventsObserved": economy.get("personalPurchaseEvents"),
                         "nextAction": "Inspect first-income/custody/conversion stages; zero score alone does not identify the cause"})
    if report["timing"]["outsideInterval"]:
        failures.append({"kind": "prediction_outside_interval", "team": side, "candidate": True,
                         "count": report["timing"]["outsideInterval"], "nextAction": "Inspect explicit event premise and observed timestamp; retain interference as a separate branch"})
    report["candidateTechnicalFailuresObserved"] = sum(f.get("count", 1) for f in failures if f.get("candidate") and f["kind"] in ("bot_fault", "fuel_exhaustion", "runtime_validation", "guest_error_marker"))
    report["candidateHasTechnicalFailure"] = report["candidateTechnicalFailuresObserved"] > 0
    def first_round(failure):
        first = failure.get("first") or {}
        return first.get("round", first.get("timestamp", {}).get("round", 1000000))
    failures.sort(key=lambda f: (not f.get("candidate", False), first_round(f)))
    report["status"] = "technical_failure_observed" if report["candidateTechnicalFailuresObserved"] else "evidence_incomplete" if issues or not coverage["candidateStdout"] or coverage["completeNativeEventCapture"] is not True else "diagnostic_complete"
    report["limits"] = ["No build or engine was launched by this analyzer", "No winner/strength/promotion inference is made",
                        "Technical failure categories may overlap; their sum is an indicator count, not unique failed turns",
                        "Missing or truncated telemetry cannot establish absence of errors or successful mechanisms", "No general native attack/cooldown/ward/channel-cancellation stream is available"]
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("outdir")
    parser.add_argument("--candidate-side", required=True)
    parser.add_argument("--bot", required=True)
    parser.add_argument("--output", type=Path, help="Save JSON; default prints it")
    args = parser.parse_args(argv)
    result = analyze(args.outdir, args.candidate_side, args.bot)
    text = json.dumps(result, indent=2, ensure_ascii=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

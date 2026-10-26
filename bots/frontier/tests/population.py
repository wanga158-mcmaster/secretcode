"""Frontier behavior contracts and conservative native triage.

`observe` counts production log proposals; it never calls a proposal an action.
No admission evaluator is exported: current evidence does not reconstruct
priority adoption, physical departure, and continued custody in one replay.
Card06 additionally measures actual native bag/visibility/cooldown prerequisites;
that observer also exports no admission evaluator.
"""
from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[3]
LOG = re.compile(rb"p10 decision[^\r\n\x00]*")
FIELD = re.compile(r"\b(actor|round|source|task|template)=(-?\d+)")
LIMIT = 16 * 1024 * 1024


def caller_packets(replay: Path, side: str) -> dict:
    """Read bounded C06 diagnostic receipts, never health or native activation.

    A receipt describes that diagnostic executable's actual policy call. Match
    its physical input to an untraced native frame before comparing behavior.
    No receipt may mean disabled tracing, an earlier runtime return, no matching
    caller, or the bounded sample being full; absence is not a refusal witness.
    """
    if side not in {"A", "B"}:
        raise ValueError("caller side must be A or B")
    with zipfile.ZipFile(replay) as bundle:
        member = "team-" + side.lower() + ".stdout"
        if bundle.getinfo(member).file_size > LIMIT:
            raise ValueError("stdout exceeds observer limit")
        data = bundle.read(member)
    packets = []
    for match in re.finditer(rb"p10_caller (\{[^\r\n\x00]*\})", data):
        packet = json.loads(match.group(1))
        if packet.get("v") != 1:
            raise ValueError("unknown caller packet schema")
        packets.append(packet)
    compact_fields = ("v round actor arm x y hp_low hp_high gold bag_low bag_high channel capture "
                      "c_path b_entered b_bank cd1 can_cast can_attack can_move dash range_low range_high "
                      "world_fuel reserve entry_fuel safe geometry observation actors task template "
                      "directive_altar destination_x destination_y group monster loaded_seen emitted "
                      "skipped_low_fuel altars_total altars_written").split()
    compact = list(re.finditer(rb"p10_caller2 ([0-9a-f ]+)\r?\n", data))
    if len(compact) != data.count(b"p10_caller2 "):
        raise ValueError("incomplete compact caller packet")
    for match in compact:
        raw = match.group(1).split()
        if any(len(word) != 8 for word in raw) or len(raw) < len(compact_fields):
            raise ValueError("invalid compact caller word")
        unsigned = [int(word, 16) for word in raw]
        signed = [word if word < 0x80000000 else word - 0x100000000 for word in unsigned]
        packet = dict(zip(compact_fields, signed))
        if packet["v"] != 2 or len(raw) != len(compact_fields) + 10 * packet["altars_written"]:
            raise ValueError("invalid compact caller size/version")
        packet["hp"] = struct.unpack("<d", struct.pack("<II", unsigned[6], unsigned[7]))[0]
        packet["bag"] = unsigned[9] | unsigned[10] << 32
        packet["dash_range"] = struct.unpack("<d", struct.pack("<II", unsigned[21], unsigned[22]))[0]
        for field in ["hp_low", "hp_high", "bag_low", "bag_high", "range_low", "range_high"]:
            del packet[field]
        packet["destination"] = [packet.pop("destination_x"), packet.pop("destination_y")]
        packet["tribute_range"] = packet["executor_fuel"] = None
        packet["altars"] = [signed[i:i + 10] for i in range(len(compact_fields), len(raw), 10)]
        packets.append(packet)
    for packet in packets:
        if not isinstance(packet.get("altars"), list):
            raise ValueError("unknown caller packet schema")
        if (packet["altars_written"] != len(packet["altars"]) or
                not 0 <= packet["altars_written"] <= min(4, packet["altars_total"]) or
                any(len(altar) != 10 for altar in packet["altars"])):
            raise ValueError("invalid caller altar counts")
        if packet["tribute_range"] is not None or packet["executor_fuel"] is not None:
            raise ValueError("caller packet silently supplies unavailable executor data")
    for actor in {packet["actor"] for packet in packets}:
        actor_packets = [packet for packet in packets if packet["actor"] == actor]
        loaded_cap = 3 if any(packet["v"] == 2 for packet in actor_packets) else 16
        initial_cap = 0 if loaded_cap == 3 else 2
        if (sum(packet["bag"] > 0 for packet in actor_packets) > loaded_cap or
                sum(packet["bag"] <= 0 for packet in actor_packets) > initial_cap):
            raise ValueError("caller diagnostic bound exceeded")
        previous = {"loaded_seen": 0, "emitted": 0, "skipped_low_fuel": 0}
        for packet in actor_packets:
            if packet["v"] != 2:
                continue
            if (packet["emitted"] != previous["emitted"] + 1 or
                    packet["loaded_seen"] <= previous["loaded_seen"] or
                    packet["skipped_low_fuel"] < previous["skipped_low_fuel"] or
                    packet["skipped_low_fuel"] < 0 or packet["world_fuel"] < 65000 or
                    packet["loaded_seen"] < packet["emitted"] + packet["skipped_low_fuel"]):
                raise ValueError("invalid compact caller counters/fuel stamp")
            previous = packet
    return {"diagnostic_only": True, "admission": False, "health_qualified": False,
            "packets": packets, "packet_count": len(packets),
            "altar_fields": ["id", "x", "y", "approach_x", "approach_y", "route_known",
                             "travel", "visible", "enemy_lock", "friendly_lock"],
            "c_paths": {1: "protected", 2: "loaded-active-B", 3: "loaded-control-bank", 4: "other"},
            "limitation": "Diagnostic caller return and observed World fuel only. Executor selection, tribute range, post-log fuel, native ability effects and activation are not supplied."}


def runtime_receipts(replay: Path, side: str, header: Path) -> dict:
    """Decode the generic deferred observer with its exact snapshot header.

    The caller must bind replay/source identity. These are facts about the
    diagnostic executable; baseline transfer needs a native prefix comparison.
    No private C/B branch or B candidate is invented by this mechanical reader.
    """
    if side not in {"A", "B"}:
        raise ValueError("runtime receipt side must be A or B")
    source = Path(header).read_text(encoding="utf-8")
    body = re.search(r"enum Field \{(.*?)\};", source, re.S)
    if not body:
        raise ValueError("runtime receipt Field schema unavailable")
    names = [name.strip() for name in body.group(1).split(",")]
    if names.pop() != "count" or len(set(names)) != len(names):
        raise ValueError("unsupported runtime receipt enum")
    with zipfile.ZipFile(replay) as bundle:
        member = "team-" + side.lower() + ".stdout"
        if bundle.getinfo(member).file_size > LIMIT:
            raise ValueError("stdout exceeds observer limit")
        data = bundle.read(member)
    matches = list(re.finditer(rb"p10_runtime1 ([0-9a-f ]+)\r?\n", data))
    if len(matches) != data.count(b"p10_runtime1"):
        raise ValueError("incomplete runtime receipt line")
    records = []
    for match in matches:
        words = match.group(1).split()
        if any(len(word) != 8 for word in words):
            raise ValueError("invalid runtime receipt word")
        unsigned = [int(word, 16) for word in words]
        signed = [word if word < 0x80000000 else word - 0x100000000 for word in unsigned]
        if len(words) < 14 + len(names) or unsigned[0] != 1 or unsigned[13] != len(names):
            raise ValueError("runtime receipt size/version differs from snapshot header")
        meta = dict(zip(["version", "emission_round", "actor", "attempt", "index", "stored", "overflow",
                         "emission_entry_fuel", "emission_bank_pending", "emission_hold", "previous_last_round",
                         "previous_bag_low", "previous_bag_high", "field_count"], signed[:14]))
        if not 1 <= meta["attempt"] <= 24 or not 0 <= meta["index"] < meta["stored"] <= 24 or meta["overflow"] < 0:
            raise ValueError("invalid runtime receipt attempt/storage bounds")
        raw = dict(zip(names, signed[14:14 + len(names)]))
        raw_unsigned = dict(zip(names, unsigned[14:14 + len(names)]))
        if raw["version"] != 1 or raw["actor"] != meta["actor"] or raw["round"] >= meta["emission_round"]:
            raise ValueError("runtime receipt identity/freshness mismatch")
        total, written = raw["altar_total"], raw["altar_written"]
        lengths = [raw["policy_reason_length"], raw["final_reason_length"]]
        if not 0 <= written <= min(4, total) or any(not 0 <= length <= 96 for length in lengths):
            raise ValueError("invalid runtime altar/reason bounds")
        required = 14 + len(names) + written * 10 + sum((length + 3) // 4 for length in lengths)
        if required != len(words) or raw["selection_fuel"] != -1:
            raise ValueError("runtime receipt trailing words or invented selection-fuel sample")
        offset = 14 + len(names)
        altars = [signed[i:i + 10] for i in range(offset, offset + written * 10, 10)]
        offset += written * 10
        reasons = []
        for length in lengths:
            packed = b"".join(struct.pack("<I", value) for value in unsigned[offset:offset + (length + 3) // 4])
            reasons.append(packed[:length].decode("utf-8", errors="replace"))
            offset += (length + 3) // 4
        present = raw["presence"]
        world = None
        if present & 1:
            world = {"round": raw["world_round"], "self_complete": bool(present & 2),
                     "type": raw["type"], "x": raw["x"], "y": raw["y"], "gold": raw["gold"],
                     "hp": struct.unpack("<d", struct.pack("<II", raw_unsigned["hp_low"], raw_unsigned["hp_high"]))[0],
                     "bag": raw_unsigned["bag_low"] | raw_unsigned["bag_high"] << 32,
                     "channel": raw["channel"], "capture": raw["capture"], "readiness": raw["readiness"],
                     "cooldown1": raw["cooldown1"], "flags": raw["world_flags"],
                     "observation_end_fuel": raw["world_fuel"] if present & 4 else None,
                     "altar_total": total, "altars": altars, "altar_truncated": total != written}
        records.append({"emission": meta, "measured_round": raw["round"], "actor": raw["actor"],
                        "exit": raw["exit_code"], "complete": raw["exit_code"] != 0,
                        "world": world, "raw_fields": raw,
                        "policy_reason": reasons[0] if present & 32 else None,
                        "final_reason": reasons[1] if present & 64 else None,
                        "policy_reason_complete": bool(raw["policy_reason_complete"]),
                        "final_reason_complete": bool(raw["final_reason_complete"]),
                        "selection_fuel": None})
    for actor in {record["actor"] for record in records}:
        selected = [record for record in records if record["actor"] == actor]
        attempts = [record["emission"]["attempt"] for record in selected]
        indexes = [record["emission"]["index"] for record in selected]
        if attempts != sorted(set(attempts)) or indexes != sorted(set(indexes)):
            raise ValueError("runtime attempts/indexes are not strictly increasing")
    return {"diagnostic_only": True, "admission": False, "header_sha256": hashlib.sha256(Path(header).read_bytes()).hexdigest(),
            "records": records, "record_count": len(records),
            "first_emission_round": min((record["emission"]["emission_round"] for record in records), default=None),
            "limitation": "Pre-World bag/state are unknown. C/B attribution requires unique pinned returned reason bytes. Exact post-selection fuel is unavailable. Emission and all later play are observer-exposed; counters/pending rows can censor coverage."}


def _initiative_contracts() -> list[dict]:
    return [{
        "id": "frontier.c10.adopted-custody-release",
        "bot": "frontier/initiative-works",
        "interaction": "A worker leaves a safe Marcella holder only after both actual next-round priorities are observed, then works a separate source before the holder banks.",
        "preconditions": "Personally visible living Marcella on a cleared, uncontested appointed source; complete safe-custody observation; worker and holder priorities already adopted; a reachable independent source; no active personal capture/channel guard.",
        "activation": "Current CPolicy emits template 102 and Runtime executes a successful departure from the holder's source; issuing a priority or logging a release proposal alone does not count.",
        "continuation": "The same worker physically acts on its independent source (template 103) while Marcella retains custody through payout and banking; renewed or invalidated work is observed.",
        "refusal": "No release before actual priority adoption, with unsafe/hidden holder, incomplete custody observation, or active personal capture/channel; retain or recover safely.",
        "control": "CPolicy::step(World, Arm::control): same capital menu, independent fixed-owner mobile work, no active priority-release organization.",
        "neighbor": "frontier/two-spindle: fixed admitted stages over two sources; compare executed custody and departure timing rather than income totals.",
        "positive_fixture": "bots/frontier/tests/plan10-cards-c-06-10.cpp preserve_mechanisms invokes production CPolicy and requires Task::release; B caller custody_pipeline follows priority issue -> adoption -> release -> independent job.",
        "refusal_fixture": "bots/frontier/tests/plan10-cards-b-06-10.cpp custody_pipeline refuses release before adoption and for unsafe holder; C caller funded_recurrence preserves a tribute channel. CPolicy reachability and native execution remain unqualified.",
        "observer": "Decode actual owner-turn priority values/order, departure and source actions plus holder capture/bank transitions from the pinned native replay. observe() supplies proposal triage only; no admission evaluator yet.",
        "gap": "Current exact-version full games still trip the zero-clear stall proxy; existing caller fixtures do not prove native custody-release continuation or control/neighbor replacement coverage. Native opportunity counts are unknown.",
        "next_check": "Construct a reachable CPolicy/runtime custody fixture with actual observed priorities and both capture_active values, then locate a native physical departure and continued holder capture; first resolve exact-version health.",
        "scope": "Experimental only. Four full games on collision/monolith both sides vs fundamentals, not full-pool health or general activation coverage.",
        "test_paths": [
            "bots/frontier/tests/run.py",
            "bots/frontier/tests/plan10-cards-c-06-10.cpp",
            "bots/frontier/tests/plan10-cards-b-06-10.cpp",
            "bots/frontier/tests/plan10-c-capital-ownership.cpp",
            "bots/frontier/tests/plan10-native-executor.cpp",
            "bots/frontier/tests/plan10-native-executor.mjs",
        ],
        "observer_paths": ["bots/frontier/tests/population.py", "tools/inspect-strategy-replay.mjs"],
    }]


def _observe_initiative(contract: dict, rows: list[dict], root: Path = ROOT) -> dict:
    """Inspect exact-subject native logs without claiming a physical witness.

    Logs are conditional on spare fuel and task changes. Zero logged proposals
    therefore neither proves zero opportunities nor proves zero activations.
    """
    root = Path(root).resolve()
    counts: Counter = Counter()
    samples, missing, coverage = [], [], []
    for row in rows:
        sides = [s for s in "AB" if str(row.get(s.lower(), "")).split("@", 1)[0] == contract["bot"]]
        for side in sides:
            coverage.append({"map": row.get("map"), "side": side,
                             "key": row.get(side.lower()), "rounds": row.get("rounds"),
                             "truncated": bool(row.get("truncated"))})
            replay = row.get("replay")
            if not replay:
                missing.append({"map": row.get("map"), "side": side, "reason": "no native replay"})
                continue
            path = (root / replay).resolve()
            if not path.is_relative_to(root):
                missing.append({"map": row.get("map"), "side": side, "reason": "replay outside repository"})
                continue
            try:
                with zipfile.ZipFile(path) as bundle:
                    member = "team-" + side.lower() + ".stdout"
                    if bundle.getinfo(member).file_size > LIMIT:
                        raise ValueError("stdout exceeds observer limit")
                    data = bundle.read(member)
            except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
                missing.append({"map": row.get("map"), "side": side, "reason": str(exc)})
                continue
            for match in LOG.finditer(data):
                text = match.group().decode("utf-8", errors="replace")
                fields = {k: int(v) for k, v in FIELD.findall(text)}
                tag = fields.get("template")
                if tag is not None:
                    counts[str(tag)] += 1
                if tag in (102, 103) and len(samples) < 12:
                    samples.append({"map": row.get("map"), "side": side,
                                    "replay": replay, **fields, "log": text})
    return {"status": "unknown", "admission": False,
            "opportunities": None, "activations": None, "continuations": None, "refusals": None,
            "proposal_counts_by_template": dict(sorted(counts.items())),
            "release_proposals": counts["102"], "independent_work_proposals": counts["103"],
            "proposal_samples": samples, "coverage": coverage, "missing": missing,
            "limitation": "Conditional production logs expose proposals, not adopted priorities or physical action acknowledgments. Missing triggers and suppressed execution cannot yet be separated."}


def contracts() -> list[dict]:
    return _initiative_contracts() + [{
        "id": "frontier.c06.loaded-return-reset-stock",
        "bot": "frontier/catalyst-depots",
        "interaction": "A supplier leaves a damaged harmless camp survivor as physical stock for a loaded Vendetta's bank-route cooldown reset, keeps its own independent job, and later liquidates the depot.",
        "preconditions": "Actual supplier income; personally visible loaded Vendetta whose dash cooldown exceeds two for manufacture; harmless on-route stock; worthwhile fully priced independent production. Consumption requires an in-range damaged lethal survivor, actual loaded bag, dash cooldown above one, cast availability, distant altar and no ordinary-kill substitute.",
        "activation": "Supplier basic actually leaves a living lethal harmless survivor; Vendetta later kills that stock while dash is cooling and actually dashes using the refreshed slot before natural cooldown expiry.",
        "continuation": "Supplier physically works its named independent source; consumer banks its own loaded output and depot capture/liquidation or safe expiry/renewal follows.",
        "refusal": "Do not manufacture without personal income, valuable loaded customer, cooling dash, harmless observed stock or safe complete proof. Do not consume untouched stock, with ready dash, without cast, or when an ordinary kill substitutes. Preserve existing capture/channel.",
        "control": "Production CPolicy Arm::control: same roster and recurrent capital menu; independent liquidation and direct banking rather than manufactured loaded-return stock.",
        "neighbor": "specialists/domino uses slot-0 attack resets inside the current source; it has no independent supplier, reserved survivor or slot-1 loaded-return dash commitment.",
        "positive_fixture": "bots/frontier/tests/plan10-reset-stock.cpp production_sequence calls actual CPolicy for supplier/control, receipt and independent job, consumer/control, bank and liquidation. It injects cooling dash, not native bootstrap or dash execution. reset-stock-caller additionally exercises actual C/B entry tags, small-bag banking, bankPoint selection and bounded diagnostic emission/skip; it simulates no native ability effect.",
        "refusal_fixture": "bots/frontier/tests/plan10-reset-stock.cpp refusal_and_recovery checks actual CPolicy priced bag boundary, income, cooldown, visibility, harmlessness, observation, capture, missing receipt, ready dash, empty bag, cast availability, untouched stock and ordinary-kill substitute.",
        "observer": "Pinned official native timeline snapshots and read-only Game_API_Champion_GetCooldown queries count actual loaded/cooling consumer and visible-customer prerequisite turns. Full stock/action/continuation witnesses remain unknown; internal confirmedReset and log proposals are not accepted as dash proof.",
        "gap": "Current policy requires an already cooling slot-1 dash, but no non-stock path has been found that spends Vendetta's initial ready dash. Native prerequisite observation tests this bootstrap obstruction. Full health, stock-use continuation and replacement coverage remain unverified.",
        "next_check": "Common diagnostic8c6462dff4bd closes earliest loaded eligibility: actual B-bank reason and executor altar2(17,29) at A r24/r25, whose full native inputs match off through r25 actor0 start. Later diagnostic choices stay separate after r25actor4 divergence. The proposed four-step cardinal first-dash proof has no retained candidate; the production executor fixture shows that excluding a requested two-step landing permits an unproductive one-step substitute. Reopen with a concrete personally observed safe productive landing and supported return suffix, or a separately justified observation experiment. Then require exact refreshed landing refusal, identical first-dash support in active/control, native cooldown/stock use, and unchanged stock prices. No first-dash or common seam was implemented.",
        "scope": "Experimental loaded-return stock, not the older wave policy's outward-job stock. Necessary native gates only; no admission evaluator.",
        "test_paths": ["bots/frontier/tests/run.py", "bots/frontier/tests/plan10-reset-stock.cpp",
                       "bots/frontier/tests/plan10-runtime-receipt.cpp", "bots/frontier/tests/plan10-runtime-receipt.mjs",
                       "bots/frontier/tests/plan10-firstdash.cpp"],
        "observer_paths": ["bots/frontier/tests/population.py", "tools/inspect-strategy-replay.mjs",
                           "tools/replay-profiles.mjs", "tools/replay-profiles.json",
                           "docs/reference/runtime-catalog.json", "tools/compare-replay-prefix.mjs",
                           "bots/frontier/common/src/runtime-receipt.h", "bots/frontier/common/src/runtime.h"],
    }]


def _reset_decoder_source(root: Path) -> str:
    """Extend the existing pinned read-only decoder in disposable generated code.

    Exact seams fail closed when its source changes. No game/action export is
    added: the only extra engine query is a visible champion's live cooldown.
    """
    source = (root / "tools/inspect-strategy-replay.mjs").read_text(encoding="utf-8")
    catalog = json.loads((root / "docs/reference/runtime-catalog.json").read_text(encoding="utf-8"))
    vendetta = next(c["type"] for c in catalog["result"]["champions"] if c["name"] == "vendetta")
    changes = {
        "from './replay-profiles.mjs'": "from " + json.dumps((root / "tools/replay-profiles.mjs").as_uri()),
        "const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');": "const ROOT = " + json.dumps(str(root)) + ";",
        "const samples = [], captures = [], transitions = [], lastVisible = new Map();":
            "const resetStockGates = {ownerTurns:0,consumerTurns:0,consumerLoadedTurns:0,consumerLoadedCoolingTurns:0,supplierVisibleLoadedCoolingTurns:0,maxConsumerBag:0,cooldowns:{},queryErrors:0,examples:[]};\nconst samples = [], captures = [], transitions = [], lastVisible = new Map();",
        "const result = { schemaVersion:": "const result = { resetStockGates, schemaVersion:",
    }
    for old, new in changes.items():
        if source.count(old) != 1:
            raise ValueError("Pinned replay decoder seam changed: " + old[:60])
        source = source.replace(old, new, 1)
    seam = "  if (allActors && ownerStart && round >= from && lastTrackRound.get(u(60)) !== round) {"
    if source.count(seam) != 1:
        raise ValueError("Pinned owner-turn snapshot seam changed")
    injection = r'''
  if (allActors && ownerStart && round >= from && lastTrackRound.get(u(60)) !== round) {
    const states=[];
    for(let index=0;index<u(172);++index)states.push(champion(196+index*252));
    const owner=states.find(c=>c.id===u(60));
    if(owner && owner.team===team && owner.health>0) {
      resetStockGates.ownerTurns++;
      view().setUint32(perspective,owner.team,true);view().setUint32(perspective+4,owner.id,true);
      checked(e.Game_Visualiser_GetVisionBuffer(game,perspective,out));
      const vision=view().getUint32(out,true);
      const sees=c=>c.id===owner.id || ((view().getBigUint64(vision+((c.y>>3)*stride+(c.x>>3))*8,true)&(1n<<BigInt(((c.y&7)<<3)+(c.x&7))))!==0n);
      for(const customer of states.filter(c=>c.team===team&&c.type===VENDETTA&&c.health>0)) {
        resetStockGates.maxConsumerBag=Math.max(resetStockGates.maxConsumerBag,customer.gems);
        if(!sees(customer))continue;
        if(typeof e.Game_API_Champion_GetCooldown!=='function'||e.Game_API_Champion_GetCooldown.length!==5)throw Error('Unsupported read-only cooldown query');
        const code=e.Game_API_Champion_GetCooldown(game,perspective,customer.id,1,out);
        if(code!==0){resetStockGates.queryErrors++;continue;}
        const cooldown=view().getInt32(out,true);
        if(customer.id===owner.id) {
          resetStockGates.consumerTurns++;
          resetStockGates.cooldowns[cooldown]=(resetStockGates.cooldowns[cooldown]??0)+1;
          if(customer.gems>0)resetStockGates.consumerLoadedTurns++;
          if(customer.gems>0&&cooldown>1)resetStockGates.consumerLoadedCoolingTurns++;
        } else if(owner.gems===0&&customer.gems>0&&cooldown>2)resetStockGates.supplierVisibleLoadedCoolingTurns++;
        if((customer.gems>0||cooldown>0)&&resetStockGates.examples.length<16)resetStockGates.examples.push({round,owner:owner.id,customer:customer.id,bag:customer.gems,cooldown});
      }
    }
  }
'''.replace("VENDETTA", str(vendetta))
    return source.replace(seam, injection + seam, 1)


def _observe_reset(contract: dict, rows: list[dict], root: Path) -> dict:
    import sys
    sys.path.insert(0, str(root / "tools/arena"))
    from core import NODE, FileLock
    generated = root / ".cache/arena/orchestration/frontier-reset/native-opportunities.mjs"
    with FileLock(generated.with_suffix(".lock")):
        source = _reset_decoder_source(root)
        generated.parent.mkdir(parents=True, exist_ok=True)
        if not generated.is_file() or generated.read_text(encoding="utf-8") != source:
            generated.write_text(source, encoding="utf-8", newline="\n")
    observations, missing = [], []
    for row in rows:
        for side in "AB":
            if str(row.get(side.lower(), "")).split("@", 1)[0] != contract["bot"]:
                continue
            name = row.get("replay")
            replay = (root / name).resolve() if name else None
            if replay is None or not replay.is_relative_to(root) or not replay.is_file():
                missing.append({"map": row.get("map"), "side": side, "reason": "native replay unavailable"})
                continue
            try:
                proc = subprocess.run([str(NODE), "--disable-warning=ExperimentalWarning", str(generated),
                                       "--replay", str(replay), "--actor", "all", "--team", str("AB".index(side)),
                                       "--captures", "0"], cwd=root, capture_output=True, text=True, timeout=180)
                if proc.returncode:
                    raise ValueError(proc.stderr[-800:])
                decoded = json.loads(proc.stdout)
                if decoded["stopped"] != "timeline_end":
                    raise ValueError("incomplete native timeline: " + decoded["stopped"])
                observations.append({"map": row.get("map"), "side": side, "key": row[side.lower()],
                                     "rounds": row.get("rounds"), "truncated": bool(row.get("truncated")),
                                     "replay": name, "replaySha256": decoded["replaySha256"],
                                     "decoderSha256": decoded["decoderSha256"],
                                     "native_gates": decoded["resetStockGates"]})
            except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as exc:
                missing.append({"map": row.get("map"), "side": side, "reason": str(exc)})
    return {"status": "partial", "admission": False, "opportunities": None,
            "activations": None, "continuations": None, "refusals": None,
            "generatedObserverSha256": hashlib.sha256(source.encode()).hexdigest(),
            "native_prerequisites": observations, "missing": missing,
            "limitation": "Actual native bags, personal visibility and cooldowns bound opportunities; stock geometry, casts, continuation and policy hydration remain unverified. Zero prerequisite turns is trigger absence at callable owner-turn starts, not failed activation."}


def observe(contract: dict, rows: list[dict], root: Path = ROOT) -> dict:
    root = Path(root).resolve()
    if contract["id"] == "frontier.c06.loaded-return-reset-stock":
        return _observe_reset(contract, rows, root)
    return _observe_initiative(contract, rows, root)

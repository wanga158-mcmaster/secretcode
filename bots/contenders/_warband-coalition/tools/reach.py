"""Extract bounded party diagnostics from saved normal-planner native games."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[4]
PARTY = re.compile(r"party r(\d+) a(\d+) g(-?\d+) (\S+) (\S+) (\S+)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit("refusing existing output")
    source = args.run / "games.jsonl"
    reports = []
    total = Counter()
    for line in source.read_text(encoding="utf-8").splitlines():
        row = json.loads(line)
        side = row["tags"]["subject"]
        replay = ROOT / row["replay"]
        with zipfile.ZipFile(replay) as archive:
            stdout = archive.read(f"team-{side.lower()}.stdout").replace(b"\0", b"").decode("utf-8", "replace")
        events = [{"round": int(r), "actor": int(a), "group": int(g), "reason": reason,
                   "value_a": float(x), "value_b": float(y)}
                  for r, a, g, reason, x, y in PARTY.findall(stdout)]
        counts = Counter(event["reason"] for event in events)
        total.update(counts)
        reports.append({"row": row, "counts": dict(counts), "events": events})
    report = {"run": str(args.run), "rows_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "native_games": len(reports), "counts": dict(total), "games": reports,
              "limit": "bounded optional trace; counts may stop after diagnostic allowance or omit repeated same-round reasons; no strength attribution"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({key: value for key, value in report.items() if key != "games"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

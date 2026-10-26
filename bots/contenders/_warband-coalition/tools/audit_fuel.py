"""Read saved caller archives and preserve actual per-side native fuel header budgets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[4]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("reports", type=Path, nargs="+")
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit("refusing existing output")
    entries = []
    for report in args.reports:
        data = report.read_bytes()
        for game in json.loads(data)["games"]:
            replay = Path(game["replay"])
            if not replay.is_absolute():
                replay = ROOT / replay
            with zipfile.ZipFile(replay) as archive:
                budgets = {}
                for side, index in (("a", 3), ("b", 7)):
                    raw = archive.read(f"team-{side}.fuel")
                    if raw[:8] != b"a62leuf\0" or (len(raw) - 13) % 8:
                        raise ValueError("invalid fuel stream")
                    values = struct.unpack_from(f"<{(len(raw) - 13) // 8}Q", raw, 13)
                    budgets[side.upper()] = values[index]
            entries.append({"report": str(report), "report_sha256": hashlib.sha256(data).hexdigest(),
                            "replay": str(replay.relative_to(ROOT)), "ordinary_budgets": budgets})
    result = {"native_games": len(entries), "streams": 2 * len(entries), "entries": entries,
              "all_budgets_equal_100000": all(row["ordinary_budgets"] == {"A": 100000, "B": 100000} for row in entries),
              "scope": "metadata audit only; original reports and game identities remain unchanged"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({key: value for key, value in result.items() if key != "entries"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

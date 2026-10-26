"""Merge the fuel probe's "F" lines from several team-A logs into one table (net fuel per op).

    python fuel-table.py LABEL=LOG [LABEL=LOG ...] [--csv OUT]

Rows: operation; columns: one per log (net = median raw minus the median of the noop task;
actions report raw). Lines come from bots/contenders/_probe-fuel (release build)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

LINE = re.compile(r"^F A (\d+)x(\d+) (.+?) raw=(-?\d+)(?: net=(-?\d+))?")


def main() -> int:
    args = sys.argv[1:]
    csv = None
    if "--csv" in args:
        i = args.index("--csv")
        csv = Path(args[i + 1])
        del args[i:i + 2]
    cols, table, order = [], {}, []
    for a in args:
        label, _, path = a.partition("=")
        cols.append(label)
        for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
            m = LINE.match(line)
            if not m:
                continue
            op = m.group(3).strip()
            val = int(m.group(5) if m.group(5) is not None else m.group(4))
            if op not in table:
                table[op] = {}
                order.append(op)
            table[op].setdefault(label, val)
    width = max(len(op) for op in order)
    print(f"{'operation':<{width}} " + " ".join(f"{c:>9}" for c in cols))
    for op in order:
        print(f"{op:<{width}} " + " ".join(f"{table[op].get(c, ''):>9}" for c in cols))
    if csv:
        with csv.open("w", encoding="utf-8", newline="\n") as fh:
            fh.write("operation," + ",".join(cols) + "\n")
            for op in order:
                fh.write('"' + op.replace('"', "'") + '",' + ",".join(str(table[op].get(c, "")) for c in cols) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

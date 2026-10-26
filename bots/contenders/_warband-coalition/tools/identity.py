"""Build a captured coalition source and bind exact OFF modules to the frozen substrate."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools/arena"))
import core


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--set", action="append", default=[], metavar="KNOB=VALUE")
    parser.add_argument("--reference-key", help="Compare with an already built immutable BOT@SOURCE instead of default v40")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        raise SystemExit("refusing existing output: " + str(out))
    bot = core.bot("contenders/_warband-coalition")
    anchor = core.bot("anchors/main-v40")
    overrides = dict(value.split("=", 1) for value in args.set)
    contents = core.read_closure(bot)
    bad = [str(p.relative_to(ROOT)) for p, data in contents.items() if b"\r" in data]
    if bad:
        raise SystemExit("non-LF source: " + ", ".join(bad))
    items = [(bot, overrides, "current", contents)]
    if not args.reference_key:
        items.append((anchor, {}, "anchor"))
    entries = core.prepare_many(items)
    if not all(entry.ok for entry in entries):
        raise SystemExit("\n".join(entry.log[-4000:] for entry in entries if not entry.ok))
    if args.reference_key:
        reference_id, reference_version = args.reference_key.split("@", 1)
        reference_bot = core.bot(reference_id)
        reference_path = core.CACHE / "snap" / reference_version / reference_bot.path.relative_to(ROOT)
    else:
        reference_id, reference_version = anchor.id, entries[1].version
        reference_path = entries[1].path
    result = {"bot": bot.id, "default_source": entries[0].version, "overrides": overrides,
              "anchor": reference_id, "anchor_source": reference_version,
              "captured_source_files": len(contents), "source_lf": True, "modules": {}}
    for name in ("bot.wasm", "bot-injected.wasm"):
        modules = [path / ".cambc/build-release" / name for path in (entries[0].path, reference_path)]
        data = [path.read_bytes() for path in modules]
        result["modules"][name] = {"default_sha256": hashlib.sha256(data[0]).hexdigest(),
                                    "anchor_sha256": hashlib.sha256(data[1]).hexdigest(),
                                    "byte_identical": data[0] == data[1]}
    result["all_modules_byte_identical"] = all(value["byte_identical"] for value in result["modules"].values())
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(result))
    return 0 if result["all_modules_byte_identical"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

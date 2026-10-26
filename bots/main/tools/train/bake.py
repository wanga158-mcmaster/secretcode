"""Write a trained weight vector into bots/main/main/src/learn.cpp (between its "weights BEGIN/END" lines).

  python -B -X utf8 -I bots/main/tools/train/bake.py W.txt     bake the weights (shorter files are zero-padded)
  python -B -X utf8 -I bots/main/tools/train/bake.py --zero    restore {} (all zero = v44's rules)
  python -B -X utf8 -I bots/main/tools/train/bake.py --check W.txt   exit 1 unless learn.cpp holds exactly W

Values are written with nine significant digits, which round-trips float32 exactly, so the baked bot
reads the same weights the runner injected during training.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / 'bots/main/main/src/learn.cpp'
HEADER = ROOT / 'bots/main/main/src/learn.h'


def count() -> int:
    m = re.search(r'kCount\s*=\s*(\d+)', HEADER.read_text(encoding='utf-8'))
    return int(m.group(1))


def f32(x: float) -> float:
    return struct.unpack('<f', struct.pack('<f', x))[0]


def literal(x: float) -> str:
    """A C++ float literal (needs a point or an exponent: `0f` is not one)."""
    s = f'{x:.9g}'
    return (s if any(c in s for c in '.eE') else s + '.0') + 'f'


def block(text: str):
    m = re.search(r'(// weights BEGIN[^\n]*\n)(.*?)(\n\s*// weights END)', text, re.S)
    if not m:
        sys.exit(f'no weights BEGIN/END markers in {SRC}')
    return m


def current(text: str) -> list[float]:
    body = block(text).group(2).strip()
    nums = re.findall(r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?', body)
    return [float(x) for x in nums] + [0.0] * (count() - len(nums))


def main() -> None:
    args = sys.argv[1:]
    text = SRC.read_text(encoding='utf-8')
    n = count()
    if args and args[0] == '--check':
        want = [f32(float(x)) for x in Path(args[1]).read_text(encoding='utf-8').split()]
        want += [0.0] * (n - len(want))
        have = [f32(x) for x in current(text)]
        sys.exit(0 if have == want else f'learn.cpp weights differ from {args[1]}')
    if args and args[0] == '--zero':
        new = '               {}'
    else:
        w = [f32(float(x)) for x in Path(args[0]).read_text(encoding='utf-8').split()]
        if len(w) > n:
            sys.exit(f'{args[0]} has {len(w)} weights, the layer {n}')
        w += [0.0] * (n - len(w))
        rows = [', '.join(literal(x) for x in w[i:i + 8]) for i in range(0, n, 8)]
        new = '               { ' + (',\n                 '.join(rows)) + ' }'
    m = block(text)
    text = text[:m.start(2)] + new + text[m.end(2):]
    SRC.write_text(text, encoding='utf-8', newline='\n')
    print(f'{SRC.relative_to(ROOT)}: weights {"zero" if args and args[0] == "--zero" else "from " + args[0]}')


if __name__ == '__main__':
    main()

"""Build main/long with knobs and print the start of the build log on failure (quick check after edits).

  python -B -X utf8 bots/main/long/train/buildcheck.py [K=V ...]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import opt  # noqa: E402  (sets up tools/arena on the path)
import core  # noqa: E402

knobs = dict(kv.split('=', 1) for kv in sys.argv[1:])
e = core.prepare(core.bot('main/long'), knobs)
log = e.log if isinstance(e.log, str) else '\n'.join(map(str, e.log))
i = log.find('.cpp:') if log.find('.cpp:') >= 0 else log.find('.h:')
print('ok' if e.ok else log[max(0, i - 300):i + 1500])

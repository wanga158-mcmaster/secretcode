# Heavy turns in a replay fuel stream: python fuel.py team-a.fuel [bodies] [picks]
import struct, sys
def stream(path):
    b = open(path, 'rb').read()
    assert b[:8] == b'a62leuf\0', b[:8]
    body = b[13:]
    vals = struct.unpack('<%dQ' % (len(body) // 8), body[:len(body) // 8 * 8])
    return vals[:8], vals[8:]
budget, turns = stream(sys.argv[1])
nb = int(sys.argv[2]) if len(sys.argv) > 2 else 6
picks = int(sys.argv[3]) if len(sys.argv) > 3 else 5
print('budget', budget, 'turns', len(turns))
draft = turns[:picks]
print('draft', draft)
play = turns[picks:]
hi = [(i // nb + 1, i % nb, v) for i, v in enumerate(play) if v >= 90000]
print('turns >= 90K:', len(hi))
for r, s, v in hi[:40]: print('  r%d body%d %d' % (r, s, v))
import collections
by = collections.Counter(s for r, s, v in hi)
print(by)

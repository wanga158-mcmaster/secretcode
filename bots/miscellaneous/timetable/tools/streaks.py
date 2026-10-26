# Capture-mode streaks per body from a trace_slot=-2 run: python streaks.py RUN
import zipfile, glob, re, collections, os, sys
run = sys.argv[1]
for f in sorted(glob.glob(os.path.join(run, 'games', '*.repl26a'))):
    z = zipfile.ZipFile(f)
    for team in ('team-a', 'team-b'):
        out = z.read(team + '.stdout').replace(b'\0', b'').decode('utf-8', 'replace')
        if ' t r' not in out:
            continue
        streak = collections.defaultdict(list)
        cur = {}
        caps = collections.Counter()
        for line in out.splitlines():
            m = re.match(r'\[(\d+)\] t r(\d+) (\w+) pos (\d+),(\d+) hp \S+ mode (\d+) tgt (-?\d+) party (\d) mv (\d) at (\d) cs (\d) fd (-?\d+)', line)
            if m:
                s, r, path, x, y, mode, tgt, party, mv, at, cs, fd = m.groups()
                s = int(s)
                mode = int(mode)
                if mode == 1:
                    cur.setdefault(s, []).append((int(r), int(tgt), int(mv), int(fd), int(x), int(y)))
                elif s in cur:
                    streak[s].append(cur.pop(s))
            m2 = re.match(r'\[(\d+)\] cap r(\d+)', line)
            if m2:
                caps[int(m2.group(1))] += 1
        lens = [len(x) for v in streak.values() for x in v]
        print(os.path.basename(f)[:34], team, 'caps', sum(caps.values()), 'streaks', len(lens), 'total', sum(lens))
        print('  hist', sorted(collections.Counter(min(l, 20) for l in lens).items()))
        longest = sorted((len(x), s, x[0], x[-1]) for s, v in streak.items() for x in v)[-4:]
        for l, s, a, b in longest:
            print('   slot', s, 'len', l, 'from', a, 'to', b)

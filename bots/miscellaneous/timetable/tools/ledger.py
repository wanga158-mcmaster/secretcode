# Time ledger per map from kept replays (bodies log "ledger" lines every 400 rounds): python ledger.py RUN [max_round]
import sys, os, re, zipfile, glob, collections
run = sys.argv[1]
names = ['dead','rec','trib','flee','retr','bank','trav','appr','fight','stage','cap','wait','idle','spawn']
rows = []
for f in sorted(glob.glob(os.path.join(run, 'games', '*.repl26a'))):
    base = os.path.basename(f)
    m = base.split('_')[0]
    team = 'team-a' if '_timetable_' in base and base.index('timetable') < base.index('_', len(m)+1) + 2 else None
    z = zipfile.ZipFile(f)
    for t in ('team-a', 'team-b'):
        out = z.read(t + '.stdout').replace(b'\0', b'').decode('utf-8', 'replace')
        if 'ledger r' not in out: continue
        last = {}
        for line in out.splitlines():
            mm = re.match(r'\[(\d+)\] ledger r(\d+) (.*) caps (\d+)', line)
            if mm:
                slot, r = int(mm.group(1)), int(mm.group(2))
                vals = list(map(int, re.findall(r'\d+', mm.group(3))))
                if r <= int(sys.argv[2]) if len(sys.argv) > 2 else True:
                    last[slot] = (r, vals, int(mm.group(4)))
        tot = [0]*14; caps = 0
        for slot, (r, vals, c) in last.items():
            for i in range(14): tot[i] += vals[i]
            caps += c
        s = sum(tot) or 1
        rows.append((m, t, caps, [100.0*x/s for x in tot]))
print('%-22s %4s ' % ('map','caps') + ' '.join('%5s' % n for n in names))
agg = [0.0]*14
for m, t, caps, pct in rows:
    print('%-22s %4d ' % (m, caps) + ' '.join('%5.1f' % p for p in pct))
    for i in range(14): agg[i] += pct[i]/len(rows)
print('%-22s %4s ' % ('MEAN','') + ' '.join('%5.1f' % p for p in agg))

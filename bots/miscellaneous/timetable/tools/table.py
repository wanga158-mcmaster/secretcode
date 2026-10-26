# Per-map table of an arena run from the subject side: python table.py RUN/games.jsonl [subject]
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1], encoding='utf-8') if l.strip()]
subj = sys.argv[2] if len(sys.argv) > 2 else 'timetable'
out = []
for r in rows:
    side = 'A' if subj in r['a'] else 'B'
    me, op = r[side], r['B' if side == 'A' else 'A']
    won = r['winner'] == side
    out.append((r['map'], side, 'W' if won else ('D' if r['winner'] not in ('A','B') else 'L'), r['rounds'], me['gems'], op['gems'], me['clears'], me['deaths'], me['exhausted'], me['errors'], me['fuel_max'], me['gold']))
out.sort()
print(f"{'map':22s} s r     rnd  gems  opp  clr dth exh err fuelmax  gold")
for o in out:
    print(f"{o[0]:22s} {o[1]} {o[2]} {o[3]:5d} {o[4]:5d} {o[5]:4d} {o[6]:4d} {o[7]:3d} {o[8]:3d} {o[9]:3d} {o[10]:7d} {o[11]:5d}")
w = sum(1 for o in out if o[2]=='W'); print('wins', w, 'of', len(out), 'exh', sum(o[8] for o in out), 'err', sum(o[9] for o in out))

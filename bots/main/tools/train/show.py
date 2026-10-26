# Print a weight vector by learn.h block, largest first within each block.
#   python show.py W.txt [top]
import sys

BLOCKS = [('group', 16), ('fight', 16), ('bank', 12), ('recall', 12), ('draft', 12), ('shop', 12), ('spawn', 8), ('group2', 6)]
NAMES = {
    'group': ['travel', 'gem share', 'enemy near', 'respawn wait', 'committed', 'health left', 'territory x gap',
              'near x missing hp', 'travel x gems', 'log gems', 'log gold', 'territory', 'jackpot', 'harmless', 'log hp', 'altar dist'],
    'fight': ['bias', 'ours n', 'theirs n', 'hp share', 'my hp', 'my gems', 'prize gems', 'on spawn', 'nearest', 'rounds',
              'our losses', 'their losses', 'gap', 'round', 'allies engaged', 'spawn dist'],
    'bank': ['bias', 'altar dist', 'hunted', 'hp', 'round', 'banked', 'enemy banked', 'allies near', 'gold', 'enemies', 'gems', 'spawn dist'],
    'recall': ['bias', 'spawn dist', 'enemies near', 'gems', 'gold', 'round', 'engaged', 'allies near', 'incoming', 'hit unseen', 'hp', 'lead'],
    'draft': ['vendetta?' if False else f'c{i}' for i in range(12)],
    'shop': ['monster', 'power', 'durability', 'treasure', 'jungle', 'home', 'bruiser', 'strength', 'focus', 'health', 's10', 's11'],
    'spawn': ['bias', 'enemies near spawn', 'strongest', 'allies alive', 'allies near spawn', 'deaths', 'held', 'round'],
    'group2': ['near x jackpot', 'near x log gems', 'near x committed', 'near x gap', 'g4', 'g5'],
}
w = [float(x) for x in open(sys.argv[1], encoding='utf-8').read().split()]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 5
at = 0
for name, n in BLOCKS:
    part = w[at:at + n]
    at += n
    if not part:
        break
    ranked = sorted(range(len(part)), key=lambda i: -abs(part[i]))[:top]
    print(f'{name:7s} ' + ', '.join(f'{NAMES[name][i]} {part[i]:+.3f}' for i in ranked))

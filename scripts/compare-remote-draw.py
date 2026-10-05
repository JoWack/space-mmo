# What a walker's own client hands their animation, against what other clients hand it for their copy (task 182).
#
# Every client logs a DRAW: line a second for every character it draws: ground speed, travel direction, body
# facing, vertical speed, whether the feet are down, and the up the speeds are measured against. Another
# player's copy is named, as "<no view target: SpaceMMOCharacterPawn_1>". This pairs the walker's own lines
# with each other client's lines for their copy, second by second.
#
# On 5 October another player's copy kept the up it found at the system origin, 89 degrees from the ground at
# Borlash, and read a 6 m/s run as 0.92 m/s across the ground and 5.93 vertical: the glide Joe saw. The rig's
# logs showed it before he did, as a fall read as running; only the positions had been compared.
#
#   python scripts/compare-remote-draw.py                         the rig: RigA walks, RigB and RigC watch
#   python scripts/compare-remote-draw.py ClientB.log ClientA.log  Joe's two clients: B walked, A watched
#
# Logs are read from client/Saved/Logs. Clients log at their own moment within each second, so a second where
# the walker starts or stops can differ between them; steady running should not. On each watching client the
# walker's copy is taken to be the other character that moved most.
import math
import os
import re
import sys

LOGS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'client', 'Saved', 'Logs')

DRAW = re.compile(
    r'^\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d):\d+\]\[\s*\d+\]LogSpaceMMO: DRAW: speed ([-\d.]+) m/s, '
    r'direction ([-\d.]+) deg \(body facing ([-\d.]+), residual ([-\d.]+)\), vertical ([-\d.]+) m/s, (\w+), '
    r'up V\(([^)]*)\)(.*)$')


def vector(text):
    parts = {'X': 0.0, 'Y': 0.0, 'Z': 0.0}
    for part in text.split(','):
        if '=' in part:
            axis, value = part.strip().split('=')
            parts[axis] = float(value)
    return parts['X'], parts['Y'], parts['Z']


def read(name):
    """The pawn this client plays, by second; and every other character it draws, by name and second."""
    own, others = {}, {}
    with open(os.path.join(LOGS, name), encoding='utf-8', errors='replace') as log:
        for line in log:
            m = DRAW.match(line)
            if not m:
                continue
            second = m.group(1)[-8:]
            record = dict(speed=float(m.group(2)), direction=float(m.group(3)), facing=float(m.group(4)),
                          vertical=float(m.group(6)), state=m.group(7), up=vector(m.group(8)))
            other = re.search(r'<no view target: (\w+)>', m.group(9))
            if other:
                others.setdefault(other.group(1), {})[second] = record
            elif 'view target' in m.group(9):
                own[second] = record
    return own, others


def degrees_apart(a, b):
    length = math.sqrt(sum(x * x for x in a)) * math.sqrt(sum(x * x for x in b))
    if length == 0.0:
        return 0.0
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b)) / length))))


def main(names):
    walker_log, watchers = names[0], names[1:]
    walker, _ = read(walker_log)
    print(f'{walker_log}, the walker: {len(walker)} DRAW lines of their own, moving in '
          f'{sum(1 for r in walker.values() if r["speed"] > 1.0)}')
    if not walker:
        print('  No DRAW lines with an up in them: a log from before 5 October, or SpaceMMO.LogCharacterDraw was off.')
        return 1

    for name in watchers:
        _, others = read(name)
        if not others:
            print(f'{name}: draws nobody else, or its lines predate the names')
            continue

        copy_name = max(others, key=lambda n: sum(r['speed'] for r in others[n].values()))
        copy = others[copy_name]
        paired = sorted(set(copy) & set(walker))
        running = [s for s in paired
                   if copy[s]['state'] == walker[s]['state'] == 'GROUNDED'
                   and copy[s]['speed'] > 1.0 and walker[s]['speed'] > 1.0]
        apart = [degrees_apart(copy[s]['up'], walker[s]['up']) for s in paired]

        print(f'{name}: the walker is {copy_name}; {len(paired)} seconds paired, {len(running)} running on the ground in both')
        if apart:
            print(f'  up: {max(apart):.2f} deg from the walker\'s own at worst, {sum(apart) / len(apart):.2f} on average')
        if running:
            print(f'  running: vertical read {max(abs(copy[s]["vertical"]) for s in running):.2f} m/s at worst, the walker\'s '
                  f'own {max(abs(walker[s]["vertical"]) for s in running):.2f}; ground speed '
                  f'{min(copy[s]["speed"] for s in running):.2f}..{max(copy[s]["speed"] for s in running):.2f}, the walker\'s '
                  f'own {min(walker[s]["speed"] for s in running):.2f}..{max(walker[s]["speed"] for s in running):.2f}')

        print('  second    | walker: speed vert   direction facing state    | copy: speed vert   direction facing state    | up apart')
        for s in paired[::max(1, len(paired) // 12)]:
            w, c = walker[s], copy[s]
            print(f'  {s}  | {w["speed"]:12.2f} {w["vertical"]:6.2f} {w["direction"]:9.1f} {w["facing"]:6.1f} {w["state"]:8s} | '
                  f'{c["speed"]:10.2f} {c["vertical"]:6.2f} {c["direction"]:9.1f} {c["facing"]:6.1f} {c["state"]:8s} | '
                  f'{degrees_apart(w["up"], c["up"]):6.2f}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:] if len(sys.argv) > 2 else ['RigA.log', 'RigB.log', 'RigC.log']))

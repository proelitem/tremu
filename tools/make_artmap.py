"""Builds assets/arcade.tsv: one line per FBNeo arcade set,
    short name <TAB> full title <TAB> poster path on thumbnails.libretro.com (may be empty)

Tremu uses it to title arcade ROMs and to download a missing poster. The poster is the
libretro-thumbnails box art (else the title screen) whose name best matches the set; names
differ across DAT versions ("rev 2, set 1" vs "set 1"), hence the approximate match.

Inputs (download once, see README in this folder):
    python tools/make_artmap.py fbneo.dat FBNeo_-_Arcade_Games.json MAME.json
where the .json files are GitHub tree listings of the libretro-thumbnails repositories.
"""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# libretro-thumbnails replaces these characters with "_" in file names.
UNSAFE = re.compile(r'[&*/:`<>?\\|"]')
# Repository on GitHub → folder name on thumbnails.libretro.com.
SYSTEMS = {'FBNeo_-_Arcade_Games': 'FBNeo - Arcade Games', 'MAME': 'MAME'}
KINDS = ('Named_Boxarts', 'Named_Titles')


def words(text):
    return set(re.findall(r'[a-z0-9]+', text.lower()))


def base(title):
    return title.split(' (')[0].strip().lower()


def load_listing(path, system):
    """{kind: {lowercase file stem: (stem, system)}} and {kind: {base title: [stems]}}."""
    exact, by_base = {}, {}
    for entry in json.load(open(path, encoding='utf-8'))['tree']:
        parts = entry['path'].split('/')
        if entry['type'] != 'blob' or len(parts) != 2 or parts[0] not in KINDS or not parts[1].endswith('.png'):
            continue
        stem = parts[1][:-4]
        exact.setdefault(parts[0], {})[stem.lower()] = (stem, system)
        by_base.setdefault(parts[0], {}).setdefault(base(stem), []).append((stem, system))
    return exact, by_base


def main(dat, *listings):
    sources = [load_listing(p, SYSTEMS[Path(p).stem]) for p in listings]
    games = {}
    for game in ET.parse(dat).getroot().iter('game'):
        games[game.get('name')] = (game.findtext('description') or '', game.get('cloneof'))

    def find(title, kind):
        safe = UNSAFE.sub('_', title).lower()
        for exact, _ in sources:
            hit = exact.get(kind, {}).get(safe)
            if hit:
                return hit
        best, score = None, 0
        for _, by_base in sources:
            for stem, system in by_base.get(kind, {}).get(base(UNSAFE.sub('_', title)), []):
                s = len(words(stem) & words(title)) * 10 - len(words(stem) ^ words(title))
                if best is None or s > score:
                    best, score = (stem, system), s
        return best

    lines, found = [], 0
    for name, (title, parent) in sorted(games.items()):
        titles = [title] + ([games[parent][0]] if parent in games else [])
        hit = None
        for kind in KINDS:
            for t in titles:
                hit = find(t, kind)
                if hit:
                    hit = f'{hit[1]}/{kind}/{hit[0]}.png'
                    break
            if hit:
                break
        found += hit is not None
        lines.append(f'{name}\t{title}\t{hit or ""}')
    out = ROOT / 'assets' / 'arcade.tsv'
    out.parent.mkdir(exist_ok=True)
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{out}: {len(lines)} sets, {found} with a poster')


if __name__ == '__main__':
    main(*sys.argv[1:])

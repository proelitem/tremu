"""Build the friends' pack: Tremu's release APK, the companion apps and a guide, in one zip.

    python tools/friend_pack.py

Needs build/tremu-<VERSION>.apk (python build.py --release). The companion APKs are taken from
their Gradle outputs on this PC; the zip lands in ../_out/Tremu-pack-<VERSION>.zip. Time Crisis VR
stays out: its APK contains Namco's game.
"""
import hashlib
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WFVR = ROOT.parent
VERSION = (ROOT / 'VERSION').read_text().strip()

COMPANIONS = {
    'ps3-armsx3.apk': WFVR.parent / 'armsx3-src/android/armsx3-ui/app/build/outputs/apk/github/release/app-github-release.apk',
    'wiiu-cemu.apk': WFVR / '_src/cemu/src/android/app/build/outputs/apk/release/app-release.apk',
    'psvita-vita3k.apk': WFVR / '_src/vita3k/android/app/build/outputs/apk/release/app-release.apk',
}


def main():
    tremu = ROOT / f'build/tremu-{VERSION}.apk'
    files = {f'1-Tremu/tremu-{VERSION}.apk': tremu}
    files.update({f'2-Compagnes/{name}': path for name, path in COMPANIONS.items()})
    missing = [str(p) for p in files.values() if not p.is_file()]
    if missing:
        sys.exit('Missing: ' + ', '.join(missing))

    out = WFVR / '_out' / f'Tremu-pack-{VERSION}.zip'
    out.parent.mkdir(exist_ok=True)
    sums = []
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:  # APKs are already compressed
        for name, path in files.items():
            z.write(path, name)
            sums.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {name}')
            print(f'{name:36} {path.stat().st_size / 2**20:6.0f} MB  {path}')
        for guide in ('LISEZ-MOI.txt', 'README-EN.txt'):
            text = (ROOT / 'docs/pack' / guide).read_text(encoding='utf-8').replace('{VERSION}', VERSION)
            z.writestr(guide, '﻿' + text.replace('\n', '\r\n'))  # BOM: Notepad reads it as UTF-8
        z.writestr('SHA256SUMS.txt', '\r\n'.join(sums) + '\r\n')
    print(f'Built {out} ({out.stat().st_size / 2**20:.0f} MB)')


if __name__ == '__main__':
    main()

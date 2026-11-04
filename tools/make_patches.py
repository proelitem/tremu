"""Writes patches/<name>.diff: what we changed in each emulator, against its upstream commit.

The emulators are built from clones kept outside this repository, in ../_src. A diff holds our commits, the
changes not yet committed there, and the new source files that git does not track yet (build output, binary
files and files over 1 MB are skipped). Usage: python tools/make_patches.py [name ...]
The upstream commit of each clone is the merge base with its origin branch."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT.parent / '_src'
# name -> clone folder in _src
FORKS = {
    'fbneo': 'FBNeo',
    'fceumm': 'libretro-fceumm',
    'mame': 'mame-wfg',
    'mupen': 'mupen-wfg',
    'ps2': 'ps2',
    'play': 'Play-',
    'ppsspp': 'ppsspp',
    'xemu': 'x1box',
    'xendroid': 'xendroid',
    'vita3k': 'vita3k',
    'cemu': 'cemu',
}
MAX_NEW_FILE = 1_000_000
# Untracked files that are build output, not source.
BUILD_DIRS = {'obj', 'libs', 'objs', 'build', 'build_android', 'build_retro', 'build_cmake', 'build_arm64', '.cxx', '.gradle'}
BUILD_SUFFIXES = ('.o', '.a', '.so', '.d', '.tmp', '.log', '.list', '.apk', '.dex', '.class')
NL = '\n'


def git(folder, *args, binary=False):
    out = subprocess.run(['git', '-C', str(SRC / folder), *args], capture_output=True).stdout
    return out if binary else out.decode('utf-8', 'replace')


def base_commit(folder):
    for ref in ('origin/HEAD', 'origin/master', 'origin/main', 'upstream/master', 'upstream/main', 'origin/android-port'):
        base = git(folder, 'merge-base', 'HEAD', ref).strip()
        if base:
            return base
    return None


def new_file_diff(path, text):
    """The diff that creates a file git does not track yet."""
    ends_with_newline = text.endswith(NL)
    lines = text.split(NL)
    if ends_with_newline:
        lines.pop()
    body = ''.join('+' + line + NL for line in lines)
    if text and not ends_with_newline:
        body += '\\ No newline at end of file' + NL
    header = f'diff --git a/{path} b/{path}{NL}new file mode 100644{NL}--- /dev/null{NL}+++ b/{path}{NL}'
    return (header + f'@@ -0,0 +1,{len(lines)} @@{NL}' + body).encode('utf-8')


def main():
    wanted = sys.argv[1:] or list(FORKS)
    (ROOT / 'patches').mkdir(exist_ok=True)
    for name in wanted:
        folder = FORKS[name]
        base = base_commit(folder)
        if not base:
            print(f'{name}: no upstream branch found, skipped')
            continue
        diff = git(folder, 'diff', '--no-color', '--no-ext-diff', base, binary=True)
        extra = []
        for path in git(folder, 'ls-files', '-o', '--exclude-standard').splitlines():
            if BUILD_DIRS & set(path.split('/')) or path.endswith(BUILD_SUFFIXES):
                continue
            file = SRC / folder / path
            # libretro.h is a copy of the public libretro header, not our work.
            if not file.is_file() or file.stat().st_size > MAX_NEW_FILE or file.name == 'libretro.h':
                continue
            try:
                text = file.read_bytes().decode('utf-8')
            except UnicodeDecodeError:
                continue
            extra.append(new_file_diff(path, text))
        target = ROOT / 'patches' / f'{name}.diff'
        data = diff + b''.join(extra)
        if data.strip():
            target.write_bytes(data)
            print(f'{name}: {len(data) / 1e3:.0f} KB against {base[:7]} ({len(extra)} new files)')
        else:
            print(f'{name}: no change against {base[:7]}')


if __name__ == '__main__':
    main()

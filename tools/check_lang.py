"""Checks that every text the sources translate (T("...") in C, Lang.t/f/h/js("...") in Java, menu
labels and gun button names) has a row in tools/lang.py. Run: python tools/check_lang.py"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import lang  # noqa: E402

KEYS = {row[0] for row in lang.STRINGS}
LIT = r'"(?:[^"\\]|\\.)*"'


def c_decode(literals):
    out = b''
    for lit in re.findall(LIT, literals):
        body, i = lit[1:-1], 0
        while i < len(body):
            ch = body[i]
            if ch != '\\':
                out += ch.encode('utf-8'); i += 1; continue
            nxt = body[i + 1]
            if nxt == 'x':
                m = re.match(r'[0-9A-Fa-f]+', body[i + 2:]); out += bytes([int(m.group(0), 16)]); i += 2 + len(m.group(0))
            elif nxt in '01234567':
                m = re.match(r'[0-7]{1,3}', body[i + 1:]); out += bytes([int(m.group(0), 8)]); i += 1 + len(m.group(0))
            else:
                out += {'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\', "'": b"'"}[nxt]; i += 2
    return out.decode('utf-8')


def args_of(text, start):
    depth, i = 1, start
    while depth:
        c = text[i]
        if c == '"':
            i = re.match(LIT, text[i:]).end() + i; continue
        depth += c == '('
        depth -= c == ')'
        i += 1
    return text[start:i - 1]


missing = set()
for f in (ROOT / 'src').glob('*.c'):
    text = f.read_text(encoding='utf-8')
    for m in re.finditer(r'\bT\(', text):
        arg = args_of(text, m.end())
        # one key per run of adjacent literals ("a" "b" is one text)
        for run in re.findall(r'(?:' + LIT + r'\s*)+', arg):
            if run.strip():
                key = c_decode(run)
                if key not in KEYS: missing.add((f.name, key))
menu = (ROOT / 'src' / 'menu.c').read_text(encoding='utf-8')
for run in re.findall(r'\{((?:' + LIT + r'\s*)+), (?:NULL|gun_|screen_)', menu):
    key = c_decode(run)
    if key not in KEYS: missing.add(('menu.c items', key))
ext = (ROOT / 'src' / 'extgun.c').read_text(encoding='utf-8')
for run in re.findall(r'return ((?:' + LIT + r'\s*)+);', ext):
    key = c_decode(run)
    if key and key not in KEYS: missing.add(('extgun.c', key))
for f in (ROOT / 'java').rglob('*.java'):
    text = f.read_text(encoding='utf-8')
    for m in re.finditer(r'Lang\.[tfhj]s?\((' + LIT + ')', text):
        key = m.group(1)[1:-1].replace('\\"', '"').replace('\\\\', '\\')
        if key not in KEYS: missing.add((f.name, key))
# Each translation keeps the French text's printf specifiers, in order.
from lang_ja import JA  # noqa: E402
SPEC = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?[sdu])')
for row in lang.STRINGS:
    for code, text in zip(lang.CODES[1:], row[1:] + (JA[row[0]],)):
        if SPEC.findall(text) != SPEC.findall(row[0]): missing.add(('specifiers ' + code, row[0]))
for name, key in sorted(missing):
    print('missing in', name, ':', repr(key))
print('ok' if not missing else '%d missing' % len(missing))
sys.exit(1 if missing else 0)

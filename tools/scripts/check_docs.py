#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the documentation against the tree. Reads only; exit code 1 when something is wrong.

    python3 -I tools/scripts/check_docs.py            everything
    python3 -I tools/scripts/check_docs.py --links    the links only

For every Markdown file of the repository (third_party/ excluded):
  links     a relative link must lead to a file of the tree, and its #anchor to a heading of that file;
  paths     a path in backticks that starts with a directory of the repository (src/, tools/, tests/, gamedata/,
            examples/, docs/, assets/, cmake/, third_party/, .github/) must exist. Paths with a placeholder
            (<name>, *, ...) are checked up to the placeholder; what the import of a disc writes into gamedata/ is
            not in the repository and is skipped.
For the modding guide (docs/modding/) in addition:
  json      every ```json block must parse;
  lua api   every sh.<name> written in backticks or in a ```lua block must be a name of gamedata/tools/lua_api.json
            (or the table that holds such names);
  events    every event of the table in events.md must be in src/engine/hooks/catalog.cpp, and the other way round;
  manifest  every key of the table in mod-json.md must be read by src/engine/mods/manifest.cpp.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO_DIRS = ('src/', 'tools/', 'tests/', 'gamedata/', 'examples/', 'docs/', 'assets/', 'cmake/', 'third_party/',
             '.github/', 'licenses/')
# Written by the import of the player's disc image or by the game, never committed.
IMPORTED = re.compile(r'^gamedata/(raw|convert|tables|save|fmv|load|texturemods|index\.tsv|version\.cfg|'
                      r'cache|maps/areas|maps/[^/]+/(map|cameras|collision|events|header|points|spawns)\.json)(/|$)')
NOT_IN_GIT = re.compile(r'^(third_party/(deps|SDL3))(/|$)')
# Paths of the behaviour specification (the decompilation's tree), of a build's output, and of files the loaders
# still accept from old mods.
ELSEWHERE = re.compile(r'^(src/(bodyprog|maps|main)/|tests/sh_tests|tools/sh-import/sh-import|'
                       r'gamedata/items/(items|weapons)\.json$)')
PLACEHOLDER = re.compile(r'[<>*{}$…]|\.\.\.|\[')


def markdown_files():
    out = []
    for p in sorted(ROOT.rglob('*.md')):
        rel = p.relative_to(ROOT).as_posix()
        if rel.startswith(('third_party/', '.tmp/', 'build', '.claude/', 'references/', 'data/')):
            continue
        out.append(p)
    return out


def anchor_of(heading):
    """GitHub's anchor of a heading line."""
    text = re.sub(r'`([^`]*)`', r'\1', heading.strip())
    text = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = text.lower()
    text = re.sub(r'[^\w\- ]', '', text, flags=re.UNICODE)
    return text.replace(' ', '-')


def anchors(path, cache={}):
    if path not in cache:
        found = set()
        counts = {}
        in_code = False
        for line in path.read_text(encoding='utf-8').splitlines():
            if line.startswith('```'):
                in_code = not in_code
            if in_code:
                continue
            m = re.match(r'^(#{1,6})\s+(.*?)\s*#*\s*$', line)
            if m:
                a = anchor_of(m.group(2))
                n = counts.get(a, 0)
                counts[a] = n + 1
                found.add(a if n == 0 else f'{a}-{n}')
            for m in re.finditer(r'<a\s+(?:id|name)="([^"]+)"', line):
                found.add(m.group(1))
        cache[path] = found
    return cache[path]


def strip_code(text):
    """The text without fenced blocks and inline code (links inside code are examples, not links)."""
    out = []
    in_code = False
    for line in text.splitlines():
        if line.lstrip().startswith('```'):
            in_code = not in_code
            out.append('')
            continue
        out.append('' if in_code else re.sub(r'`[^`]*`', '', line))
    return out


def check_links(path, problems):
    rel = path.relative_to(ROOT).as_posix()
    for n, line in enumerate(strip_code(path.read_text(encoding='utf-8')), 1):
        for m in re.finditer(r'\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)', line):
            target = m.group(1)
            if re.match(r'^[a-z][a-z0-9+.-]*:', target):
                continue
            file_part, _, anchor = target.partition('#')
            dest = path if not file_part else (path.parent / file_part).resolve()
            if not dest.exists():
                problems.append(f'{rel}:{n}: broken link {target}')
                continue
            if anchor and dest.is_file() and dest.suffix == '.md' and anchor.lower() not in anchors(dest):
                problems.append(f'{rel}:{n}: no heading #{anchor} in {dest.relative_to(ROOT).as_posix()}')


def check_paths(path, problems):
    rel = path.relative_to(ROOT).as_posix()
    for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        for m in re.finditer(r'(?<![\w/.\-<>"=])([A-Za-z._][\w.\-]*/[^\s`"\',;()|]*)', ' '.join(re.findall(r'`([^`]+)`', line))):
            token = m.group(1).rstrip('.,;:)')
            if not token.startswith(REPO_DIRS):
                continue
            token = token.split('::')[0]
            token = re.sub(r':\d+(-\d+)?$', '', token)
            cut = PLACEHOLDER.search(token)
            if cut:
                token = token[:cut.start()]
                token = token[:token.rfind('/') + 1]
                if token.count('/') < 1 or token in REPO_DIRS:
                    continue
            if IMPORTED.match(token) or NOT_IN_GIT.match(token) or ELSEWHERE.match(token):
                continue
            # A file named without its extension (`src/x/file` for file.h and file.cpp) is found by its stem.
            target = ROOT / token
            if not target.exists() and not list(target.parent.glob(target.name + '.*')):
                problems.append(f'{rel}:{n}: no such path {m.group(1)}')


def api_names():
    names = set(json.loads((ROOT / 'gamedata/tools/lua_api.json').read_text(encoding='utf-8'))['functions'])
    tables = set()
    for name in names:
        parts = name.split('.')
        for i in range(1, len(parts)):
            tables.add('.'.join(parts[:i]))
    return names, tables


# Values and tables of the script API that are not functions (lua_api.json lists functions).
API_VALUES = {'sh.engine.version', 'sh.engine.major', 'sh.engine.minor', 'sh.engine.patch', 'sh.engine.build',
              'sh.vec'}


def check_modding(problems):
    guide = ROOT / 'docs/modding'
    if not guide.is_dir():
        return
    names, tables = api_names()
    for path in sorted(guide.glob('*.md')):
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding='utf-8')
        for m in re.finditer(r'```json\n(.*?)```', text, flags=re.S):
            try:
                json.loads(m.group(1))
            except ValueError as e:
                line = text[:m.start()].count('\n') + 1
                problems.append(f'{rel}:{line}: json block does not parse: {e}')
        if path.name == 'lua-api.md':
            continue  # generated from lua_api.json itself
        code = '\n'.join(m.group(1) for m in re.finditer(r'```lua\n(.*?)```', text, flags=re.S))
        code += '\n' + '\n'.join(m.group(1) for m in re.finditer(r'`([^`\n]+)`', text))
        for m in re.finditer(r'(?<![\w.])sh((?:\.[A-Za-z_][A-Za-z0-9_]*)+)', code):
            name = 'sh' + m.group(1)
            if name.endswith(('.json', '.lua', '.md')) or name in API_VALUES:
                continue
            if name not in names and name not in tables:
                problems.append(f'{rel}: {name} is not in gamedata/tools/lua_api.json')

    events = guide / 'events.md'
    if events.is_file():
        catalog = (ROOT / 'src/engine/hooks/catalog.cpp').read_text(encoding='utf-8')
        known = set(re.findall(r'\{"([a-z_]+\.[a-z_]+)",', catalog))
        written = set(re.findall(r'^\| `([a-z_]+\.[a-z_]+)` \|', events.read_text(encoding='utf-8'), flags=re.M))
        for e in sorted(written - known):
            problems.append(f'docs/modding/events.md: event {e} is not in src/engine/hooks/catalog.cpp')
        for e in sorted(known - written):
            problems.append(f'docs/modding/events.md: event {e} of the catalog is not described')

    manifest_doc = guide / 'mod-json.md'
    if manifest_doc.is_file():
        source = (ROOT / 'src/engine/mods/manifest.cpp').read_text(encoding='utf-8')
        in_table = False
        for line in manifest_doc.read_text(encoding='utf-8').splitlines():
            if line.startswith('| Key |'):
                in_table = True
                continue
            if in_table and not line.startswith('|'):
                break
            m = re.match(r'^\| `([a-z_]+)` \|', line) if in_table else None
            if m and f'"{m.group(1)}"' not in source:
                problems.append(f'docs/modding/mod-json.md: key {m.group(1)} is not read by manifest.cpp')


def main():
    problems = []
    files = markdown_files()
    for path in files:
        check_links(path, problems)
        if '--links' not in sys.argv:
            check_paths(path, problems)
    if '--links' not in sys.argv:
        check_modding(problems)
    for p in problems:
        print(p)
    print(f'check_docs: {len(files)} files, {len(problems)} problems')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

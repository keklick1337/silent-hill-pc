#!/usr/bin/env bash
# Packs the example mods (examples/mods) as one archive a player unpacks into the game folder:
#   <out-dir>/SilentHillPC-<version>-example-mods.zip  ->  mods/<id>/..., mods/README.md
# Every mod.json must be valid JSON whose "id" is its folder name; the archive is reproducible (sorted
# entries, fixed times). The examples that are broken on purpose (they show how the game reports a missing
# dependency or a too old engine) stay out of the players' archive.
#
# Usage: tools/ci/package_mods.sh <version> <out-dir>
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: $0 <version> <out-dir>" >&2; exit 2; }
VERSION="$1"
OUT="$2"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/examples/mods"
EXCLUDED=(broken_dependency needs_newer_engine)

python3 -I - "$SRC" <<'PY'
import json, pathlib, sys
src = pathlib.Path(sys.argv[1])
bad = 0
for d in sorted(p for p in src.iterdir() if p.is_dir()):
    for f in sorted(d.rglob("*.json")):
        try:
            data = json.loads(f.read_text(encoding="utf-8"))
        except (ValueError, UnicodeDecodeError) as e:
            print(f"{f.relative_to(src)}: {e}", file=sys.stderr)
            bad += 1
            continue
        if f.name == "mod.json" and f.parent == d and data.get("id") != d.name:
            print(f"{f.relative_to(src)}: id {data.get('id')!r} is not the folder name {d.name!r}", file=sys.stderr)
            bad += 1
sys.exit(1 if bad else 0)
PY

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/mods"
cp -R "$SRC/." "$STAGE/mods/"
for name in "${EXCLUDED[@]}"; do rm -rf "${STAGE:?}/mods/$name"; done
find "$STAGE" -exec touch -h -d '2000-01-01 00:00:00 UTC' {} +

mkdir -p "$OUT"
ARCHIVE="$(cd "$OUT" && pwd)/SilentHillPC-$VERSION-example-mods.zip"
rm -f "$ARCHIVE"
(cd "$STAGE" && find mods -print | LC_ALL=C sort | zip -q -X -D -@ "$ARCHIVE")
echo "$ARCHIVE"

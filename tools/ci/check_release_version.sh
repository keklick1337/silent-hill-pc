#!/usr/bin/env bash
# The engine version lives in the VERSION file at the repository root. A release
# tag must name it: "vX.Y.Z" (or a pre-release "vX.Y.Z-rc1") with X.Y.Z equal to
# VERSION. Fails otherwise, so a release never ships binaries that report another
# version than its name.
#
# Usage: tools/ci/check_release_version.sh <name>
#   <name> that is not a release tag (does not start with "v<digit>"; a
#   `git describe` name of a build between tags, "v1.0.0-12-gabc1234", is not one
#   either) is accepted without a check.
#   --require-tag as the second argument refuses such names too (release.yml).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NAME="${1:?version name or tag}"
REQUIRE_TAG="${2:-}"

ENGINE="$(head -n 1 "$ROOT/VERSION" | tr -d '[:space:]')"
if [[ ! "$ENGINE" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "version: VERSION must hold MAJOR.MINOR.PATCH, found '$ENGINE'" >&2
    exit 1
fi

# A build between tags, as `git describe` names it: not a release.
if [[ "$NAME" =~ -[0-9]+-g[0-9a-f]+(-dirty)?$ || "$NAME" == *-dirty ]]; then
    if [[ "$REQUIRE_TAG" == --require-tag ]]; then
        echo "version: '$NAME' is not a release tag (expected v$ENGINE)" >&2
        exit 1
    fi
    exit 0
fi

if [[ "$NAME" =~ ^v[0-9] ]]; then
    if [[ ! "$NAME" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.-]+)?$ ]]; then
        echo "version: tag '$NAME' is not vMAJOR.MINOR.PATCH (expected v$ENGINE)" >&2
        exit 1
    fi
    if [[ "${BASH_REMATCH[1]}" != "$ENGINE" ]]; then
        echo "version: tag '$NAME' does not match the engine version $ENGINE of the VERSION file;" >&2
        echo "version: set VERSION to ${BASH_REMATCH[1]} in the tagged commit, or tag v$ENGINE" >&2
        exit 1
    fi
    echo "version: $NAME matches the engine version $ENGINE"
    exit 0
fi

if [[ "$REQUIRE_TAG" == --require-tag ]]; then
    echo "version: '$NAME' is not a release tag (expected v$ENGINE)" >&2
    exit 1
fi

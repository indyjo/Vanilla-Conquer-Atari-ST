#!/bin/sh
# Fill gitinfo.cpp.in (CMake @VAR@ placeholders) from the git working tree.
# Usage: gen_gitinfo.sh <gitinfo.cpp.in> <gitinfo.cpp> <repo-root> [force-clean]
#
# force-clean=1 writes GitUncommittedChanges=false even if the tree is dirty.
# Official itch releases pass this so the in-game label never shows '~'.
#
# Tags named release/atarist/vX.Y.Z are stored as GitTag=vX.Y.Z so Version_Number()
# prints that label alone (no SHA) when the tree is clean.

set -e

IN=$1
OUT=$2
ROOT=$3
FORCE_CLEAN=${4:-0}

if [ -z "$IN" ] || [ -z "$OUT" ] || [ -z "$ROOT" ]; then
	echo "usage: $0 gitinfo.cpp.in gitinfo.cpp repo-root [force-clean]" >&2
	exit 1
fi

git_ok=true
git_in() {
	git -C "$ROOT" "$@"
}

GIT_HEAD_SHA1=$(git_in show -s --format=%H HEAD 2>/dev/null) || git_ok=false
GIT_HEAD_SHORT_SHA1=$(git_in show -s --format=%h HEAD 2>/dev/null) || true
GIT_COMMIT_DATE_ISO8601=$(git_in show -s --format=%ci HEAD 2>/dev/null) || true
GIT_AUTHOR_NAME=$(git_in show -s --format=%an HEAD 2>/dev/null) || true
GIT_COMMIT_TSTAMP=$(git_in show -s --format=%ct HEAD 2>/dev/null) || true
GIT_REV_LIST_COUNT=$(git_in rev-list --count HEAD 2>/dev/null) || true

if [ "$git_ok" != true ] || [ -z "$GIT_HEAD_SHA1" ]; then
	GIT_RETRIEVED_STATE=false
	GIT_HEAD_SHA1=unknown
	GIT_HEAD_SHORT_SHA1=unknown
	GIT_COMMIT_DATE_ISO8601=unknown
	GIT_AUTHOR_NAME=unknown
	GIT_COMMIT_TSTAMP=0
	GIT_REV_LIST_COUNT=0
	GIT_IS_DIRTY=false
	GIT_TAG=
else
	GIT_RETRIEVED_STATE=true
	[ -n "$GIT_HEAD_SHORT_SHA1" ] || GIT_HEAD_SHORT_SHA1=unknown
	[ -n "$GIT_COMMIT_DATE_ISO8601" ] || GIT_COMMIT_DATE_ISO8601=unknown
	[ -n "$GIT_AUTHOR_NAME" ] || GIT_AUTHOR_NAME=unknown
	[ -n "$GIT_COMMIT_TSTAMP" ] || GIT_COMMIT_TSTAMP=0
	[ -n "$GIT_REV_LIST_COUNT" ] || GIT_REV_LIST_COUNT=0

	if [ "$FORCE_CLEAN" = 1 ]; then
		GIT_IS_DIRTY=false
	elif [ -n "$(git_in status --porcelain 2>/dev/null)" ]; then
		GIT_IS_DIRTY=true
	else
		GIT_IS_DIRTY=false
	fi

	# Exact Atari ST release tag → vX.Y.Z for Version_Number().
	RELEASE_TAG=$(git_in describe --exact-match --tags --match 'release/atarist/v*' HEAD 2>/dev/null || true)
	case "$RELEASE_TAG" in
	release/atarist/v[0-9]*.[0-9]*.[0-9]*)
		GIT_TAG=${RELEASE_TAG#release/atarist/}
		;;
	*)
		GIT_TAG=$(git_in describe --tags HEAD 2>/dev/null || true)
		;;
	esac
fi

export GIT_HEAD_SHA1 GIT_HEAD_SHORT_SHA1 GIT_COMMIT_DATE_ISO8601 GIT_AUTHOR_NAME
export GIT_TAG GIT_COMMIT_TSTAMP GIT_IS_DIRTY GIT_RETRIEVED_STATE GIT_REV_LIST_COUNT

python3 - "$IN" "$OUT" <<'PY'
import os
import pathlib
import sys

src = pathlib.Path(sys.argv[1]).read_text()
dst = pathlib.Path(sys.argv[2])
keys = (
    "GIT_HEAD_SHA1",
    "GIT_HEAD_SHORT_SHA1",
    "GIT_COMMIT_DATE_ISO8601",
    "GIT_AUTHOR_NAME",
    "GIT_TAG",
    "GIT_COMMIT_TSTAMP",
    "GIT_IS_DIRTY",
    "GIT_RETRIEVED_STATE",
    "GIT_REV_LIST_COUNT",
)
quoted = {
    "GIT_HEAD_SHA1",
    "GIT_HEAD_SHORT_SHA1",
    "GIT_COMMIT_DATE_ISO8601",
    "GIT_AUTHOR_NAME",
    "GIT_TAG",
}


def c_escape(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
    )


text = src
for key in keys:
    raw = os.environ.get(key, "")
    text = text.replace("@" + key + "@", c_escape(raw) if key in quoted else raw)

tmp = dst.with_suffix(dst.suffix + ".tmp")
tmp.write_text(text)
if dst.exists() and dst.read_text() == text:
    tmp.unlink()
else:
    tmp.replace(dst)
PY

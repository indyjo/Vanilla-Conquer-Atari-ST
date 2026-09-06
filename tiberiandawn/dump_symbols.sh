#!/bin/sh
# Dump a TOS binary as Hatari ASCII symbols (addr T|D|B name).
#
# Names are left mangled. Hatari omits profile callee labels that contain ','
# (typical demangled C++ signatures) and cannot parse '()' in breakpoints.
# Demangle later, e.g.  c++filt < profile.txt  or pipe hatari_profile.py output.
#
# Usage:
#   dump_symbols.sh [OPTIONS] [TOOLDIR] TOSFILE
#
# TOOLDIR is optional. Pass the short-name triplet bin, e.g.
#   ~/opt/cross-mint/m68k-atari-mintelf/bin
# or the prefixed cross bin:
#   ~/opt/cross-mint/bin
# If omitted, nm (and c++filt if --demangle) are taken from PATH
# (m68k-atari-mintelf-* preferred).
#
# Options:
#   -o FILE         write FILE instead of stdout
#   -d, --demangle  run c++filt (Hatari profile/breakpoints will drop many names)
#   -h, --help

set -e

OUT=
DEMANGLE=0
TOOLDIR=
TOS=

usage() {
	sed -n '2,/^$/s/^# \{0,1\}//p' "$0" >&2
	exit "${1:-1}"
}

while [ $# -gt 0 ]; do
	case "$1" in
		-h|--help) usage 0 ;;
		-o)
			[ $# -ge 2 ] || usage
			OUT=$2
			shift 2
			;;
		-d|--demangle)
			DEMANGLE=1
			shift
			;;
		--)
			shift
			break
			;;
		-*)
			echo "unknown option: $1" >&2
			usage
			;;
		*)
			break
			;;
	esac
done

case $# in
	1)
		TOS=$1
		;;
	2)
		TOOLDIR=$1
		TOS=$2
		;;
	*)
		usage
		;;
esac

if [ -n "$TOOLDIR" ] && [ ! -d "$TOOLDIR" ]; then
	echo "not a directory: $TOOLDIR" >&2
	exit 1
fi
if [ ! -f "$TOS" ]; then
	echo "not a file: $TOS" >&2
	exit 1
fi

# Resolve nm / c++filt from TOOLDIR (short or prefixed names), then PATH.
find_tool() {
	name=$1
	if [ -n "$TOOLDIR" ]; then
		if [ -x "$TOOLDIR/$name" ]; then
			printf '%s\n' "$TOOLDIR/$name"
			return 0
		fi
		for pfx in m68k-atari-mintelf m68k-atari-mint; do
			if [ -x "$TOOLDIR/${pfx}-$name" ]; then
				printf '%s\n' "$TOOLDIR/${pfx}-$name"
				return 0
			fi
		done
		# ~/opt/cross-mint/m68k-atari-mintelf/bin → ~/opt/cross-mint/bin/m68k-atari-mintelf-c++filt
		parent=$(dirname "$TOOLDIR")
		triplet=$(basename "$parent")
		prefix=$(dirname "$parent")
		if [ -x "$prefix/bin/${triplet}-$name" ]; then
			printf '%s\n' "$prefix/bin/${triplet}-$name"
			return 0
		fi
	fi
	for cand in "m68k-atari-mintelf-$name" "m68k-atari-mint-$name" "$name"; do
		resolved=$(command -v "$cand" 2>/dev/null) || true
		if [ -n "$resolved" ]; then
			printf '%s\n' "$resolved"
			return 0
		fi
	done
	return 1
}

NM=$(find_tool nm) || {
	echo "nm not found (pass TOOLDIR, or put m68k-atari-mintelf-nm on PATH)" >&2
	exit 1
}
CXXFILT=
if [ "$DEMANGLE" = 1 ]; then
	CXXFILT=$(find_tool c++filt) || {
		echo "c++filt not found (expected next to nm, or m68k-atari-mintelf-c++filt on PATH)" >&2
		exit 1
	}
fi

# Keep text/data/bss/rodata with a name; map R→D. Drop N debug and empties.
filter_syms() {
	awk '
		$2 ~ /^[TtDdBbRr]$/ && $3 != "" {
			t = toupper($2)
			if (t == "R") t = "D"
			print $1, t, $3
		}
	'
}

dump() {
	printf '# Hatari ASCII symbols from %s\n' "$TOS"
	if [ "$DEMANGLE" = 1 ]; then
		printf '# nm=%s  c++filt=%s\n' "$NM" "$CXXFILT"
		"$NM" -n "$TOS" | filter_syms | "$CXXFILT"
	else
		printf '# nm=%s  (mangled; demangle profile output with c++filt)\n' "$NM"
		"$NM" -n "$TOS" | filter_syms
	fi
}

if [ -n "$OUT" ]; then
	dump >"$OUT"
else
	dump
fi

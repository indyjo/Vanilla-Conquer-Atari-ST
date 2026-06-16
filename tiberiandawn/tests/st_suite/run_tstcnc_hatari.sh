#!/bin/sh
# Run tstcnc.tos under Hatari with console on stderr.
# Writes TST_AUTO.CMD in the CNC folder so the test runs without keyboard input.
#
# Usage:
#   run_tstcnc_hatari.sh          # terrain left-clip test (t)
#   run_tstcnc_hatari.sh 1        # full automated bundle
#
# Env overrides: HATARI_BIN, EMU_HD, TOS_ROM, CNC_DIR, TEST_CH, HATARI_TIMEOUT

set -e

TEST_CH="${1:-t}"
HATARI_BIN="${HATARI_BIN:-/Users/jonas/Documents/devel/gcc/hatari/build/src/Hatari.app/Contents/MacOS/Hatari}"
EMU_HD="${EMU_HD:-$HOME/Documents/Emu/Atari/HD}"
CNC_DIR="${CNC_DIR:-$EMU_HD/CNC}"
TOS_ROM="${TOS_ROM:-$HOME/Documents/Emu/Atari/ROM/emutos-256k-1.3/etos256us.img}"
HATARI_TIMEOUT="${HATARI_TIMEOUT:-30}"
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
TSTCNC_SRC="${TSTCNC_SRC:-$SCRIPT_DIR/../../bin/AtariST/tstcnc.tos}"

if [ -f "$TSTCNC_SRC" ]; then
	cp "$TSTCNC_SRC" "$CNC_DIR/TSTCNC.TOS"
fi

printf '%s\n' "$TEST_CH" >"$CNC_DIR/TST_AUTO.CMD"

echo "Auto test: $TEST_CH (trigger $CNC_DIR/TST_AUTO.CMD)"
echo "Hatari timeout: ${HATARI_TIMEOUT}s"

perl -e '
	alarm shift;
	exec @ARGV;
' "$HATARI_TIMEOUT" "$HATARI_BIN" \
	--configfile "$HOME/Library/Application Support/Hatari/hatari.cfg" \
	--conout 2 \
	--confirm-quit false \
	--fast-boot true \
	--fast-forward true \
	--auto 'C:\CNC\TSTCNC.TOS'

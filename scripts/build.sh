#!/bin/bash
# Build SimpleDock with vbcc (m68k-amigaos target), vasm, vlink and the NDK 3.2 headers.
# Set VBCC_BIN, VBCC_TARGET and NDK_INCLUDE (and VASM, VLINK if not in VBCC_BIN),
# or put them in local.env in the repo (KEY=value lines, not in git).
set -e
cd "$(dirname "$0")/.."
if [ -f local.env ]; then set -a; . ./local.env; set +a; fi
: "${VBCC_BIN:?set VBCC_BIN to the directory with vbccm68k, vasmm68k_mot, vlink}"
: "${VBCC_TARGET:?set VBCC_TARGET to the vbcc targets/m68k-amigaos directory}"
: "${NDK_INCLUDE:?set NDK_INCLUDE to the NDK 3.2 Include_H directory}"
CC="$VBCC_BIN/vbccm68k"; AS="${VASM:-$VBCC_BIN/vasmm68k_mot}"; LD="${VLINK:-$VBCC_BIN/vlink}"
OPT="${OPT:--O=1}"
mkdir -p build/obj
OBJ=()
for f in src/*.c; do
  n=$(basename "${f%.c}")
  "$CC" -quiet -c99 -cpu=68020 -sd -dontwarn=153 $OPT -I"$VBCC_TARGET/include" -I"$NDK_INCLUDE" "$f" -o="build/obj/$n.s"
  "$AS" -quiet -Fhunk -m68020 -nowarn=62 "build/obj/$n.s" -o "build/obj/$n.o"
  OBJ+=("build/obj/$n.o")
done
for f in src/*.s; do
  n=$(basename "${f%.s}")
  "$AS" -quiet -Fhunk -m68020 "$f" -o "build/obj/$n.o"
  OBJ+=("build/obj/$n.o")
done
# small data (-sd): globals relative to A4 (_LinkerDB, set by startup.o; the input handler, the crash
# trap and the hooks other tasks call set it themselves); data and BSS merged, under 64 KB
"$LD" -bamigahunk -x -Bstatic -Cvbcc -nostdlib -mrel -sd -s "$VBCC_TARGET/lib/startup.o" "${OBJ[@]}" \
      -L"$VBCC_TARGET/lib" -lvcs -lamigas -Mbuild/SimpleDock.map -o build/SimpleDock
ls -l build/SimpleDock

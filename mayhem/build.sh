#!/usr/bin/env bash
#
# mayhem/build.sh — builds rAthena's fuzz harnesses (libconfig, strlib, YamlDatabase), their
# -standalone reproducers, and the known-answer probe used by mayhem/test.sh.
#
# rAthena's own build needs MySQL client headers and a configure/CMake run for the whole server. The
# fuzzed code lives in src/common (strlib, database, showmsg, malloc) plus the bundled 3rdparty
# libconfig and rapidyaml, so those sources are compiled directly here, exactly as upstream compiles
# them (src/common/CMakeLists.txt), with the HAVE_* feature macros that ./configure would set on Linux.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
M="$SRC/mayhem"
LC="$SRC/3rdparty/libconfig"
RY="$SRC/3rdparty/rapidyaml"

INC="-I$SRC/src -I$LC -I$RY/src -I$RY/ext/c4core/src"
DEFS="-DLIBCONFIG_STATIC -DHAVE_STRNLEN -DHAVE_STRTOK_R -DHAVE_MONOTONIC_CLOCK -DHAVE_SETRLIMIT -DHAVE_GETADDRINFO"
COMMON_SRCS="database malloc showmsg strlib"

# build_lib <outdir> <compile flags...>: libconfig, rapidyaml and the src/common objects into <outdir>/libfuzzed.a
build_lib() {
  local out="$1"; shift
  rm -rf "$out"; mkdir -p "$out"
  {
    for f in "$LC"/*.c; do
      echo "$CC $* $INC $DEFS -c $f -o $out/lc_$(basename "$f" .c).o"
    done
    for f in "$RY"/src/c4/yml/*.cpp "$RY"/ext/c4core/src/c4/*.cpp; do
      echo "$CXX -std=c++17 $* -fno-sanitize=nonnull-attribute $INC $DEFS -c $f -o $out/ry_$(basename "$f" .cpp).o"
    done
    for n in $COMMON_SRCS; do
      extra=""; [ "$n" = database ] && extra="-fno-sanitize=nonnull-attribute"
      echo "$CXX -std=c++17 $* $extra $INC $DEFS -c $SRC/src/common/$n.cpp -o $out/co_$n.o"
    done
    echo "$CXX -std=c++17 $* $INC $DEFS -c $M/server_stubs.cpp -o $out/stubs.o"
  } | xargs -P "$MAYHEM_JOBS" -I{} sh -c '{}'
  rm -f "$out/libfuzzed.a"
  ar rcs "$out/libfuzzed.a" "$out"/*.o
}

# 1) the project's libraries, instrumented, with DWARF <= 3 and SanCov (-fsanitize=fuzzer-no-link)
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
OBJ="$M/build-fuzz"
build_lib "$OBJ" $FUZZ_FLAGS

# 2) each harness twice: libFuzzer engine and the standalone run-once driver
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$M/lsan_off.cc" -o "$OBJ/lsan_off.o"

for h in libconfig strlib yamldb; do
  extra=""; [ "$h" = yamldb ] && extra="-fno-sanitize=nonnull-attribute"
  $CXX -std=c++17 $FUZZ_FLAGS $extra $INC $DEFS -c "$M/harness_$h.cpp" -o "$OBJ/harness_$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OBJ/harness_$h.o" "$OBJ/lsan_off.o" "$OBJ/libfuzzed.a" -o "/mayhem/fuzz_$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OBJ/harness_$h.o" "$OBJ/standalone_main.o" "$OBJ/lsan_off.o" "$OBJ/libfuzzed.a" -o "/mayhem/fuzz_$h-standalone"
done

# 3) the known-answer probe, with the project's normal flags (clean, unsanitized build)
TEST_FLAGS="-O2 $COVERAGE_FLAGS"
TOBJ="$M/build-test"
build_lib "$TOBJ" $TEST_FLAGS
$CXX -std=c++17 $TEST_FLAGS $INC $DEFS -c "$M/kat.cpp" -o "$TOBJ/kat.o"
$CXX $TEST_FLAGS "$TOBJ/kat.o" "$TOBJ/libfuzzed.a" -o /mayhem/rathena-kat

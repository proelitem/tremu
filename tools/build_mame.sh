#!/bin/bash
# Builds our libretro MAME for Quest with the ARM64 recompiler (the buildbot's core only has the slow
# C back end). Run from MSYS2 (MINGW64) through tools/build_mame.ps1 or: bash build_mame.sh [SOURCES]
# SOURCES limits the drivers, e.g. src/mame/midway/seattle.cpp; empty builds the arcade subtarget.
# NOASM=0: the makefile forces NOASM=1, which keeps the C back end even on ARM64.
# Windows long paths are off on this PC: sources are reached through the short junction G:\ms.
set -e
export NDK=/g/Projets/WFVR/_tools/android-sdk/ndk/29.0.14206865
export ANDROID_NDK_ROOT="$NDK" ANDROID_NDK_HOME="$NDK"
export TEMP=/g/xb_tmp TMP=/g/xb_tmp TMPDIR=/g/xb_tmp
export OS=Windows_NT  # MAME's makefile detects Windows from this, not from uname
cd /g/ms
# makefile hard-codes --NOASM=1 for android-arm64 (after our NOASM=0): removed for the retro OSD.
sed -i '/--gcc=android-arm64.*--osd=retro/s/ --NOASM=1//' makefile
SOURCES_ARG=()
if [ -n "$1" ]; then
    SOURCES_ARG=(SUBTARGET=wfg "SOURCES=$1")
else
    SOURCES_ARG=(SUBTARGET=arcade)
fi
make -f Makefile.libretro platform=android-arm64 -j"${JOBS:-6}" PYTHON_EXECUTABLE=python3 NOASM=0 "${SOURCES_ARG[@]}"

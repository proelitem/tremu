# Windows make cannot run this core's C++ lines (cmd.exe swallows them): take the commands from a
# dry run of ndk-build and run them from bash, in parallel. The compile lines come first, then asm, then link.
export TEMP='G:\Projets\WFVR\_tmp\n64' TMP='G:\Projets\WFVR\_tmp\n64'
NDK=/g/Projets/WFVR/time-crisis-vr/.tools/ndk/android-ndk-r27c
T=/g/Projets/WFVR/_tmp/n64
cd /g/Projets/WFVR/_src/mupen-wfg/libretro/jni
"$NDK/ndk-build.cmd" -n V=1 STRINGS=x GLES3=1 HAVE_PARALLEL_RDP=0 HAVE_PARALLEL_RSP=0 HAVE_THR_AL=0 LLE=0 APP_ABI=arm64-v8a APP_SHORT_COMMANDS=true > $T/dry.txt 2>&1
rm -rf $T/cmds; mkdir -p $T/cmds
grep -a 'bin/clang[+]*\.exe .* -c ' $T/dry.txt > $T/compile.txt
split -l 1 -a 4 $T/compile.txt $T/cmds/c
echo "compiling $(wc -l < $T/compile.txt) files"
ls $T/cmds/* | xargs -P 6 -n 1 bash > $T/compile.log 2>&1
grep -a "error:" $T/compile.log | cut -c1-300 | head -30
grep -a -n 'asm_defines\|gen_asm' $T/dry.txt | cut -c1-200 | head -5
L=$(grep -a -v ' -c ' $T/dry.txt | grep -a 'bin/clang[+]*\.exe .*-shared')
echo "$L" > $T/link.sh
echo "linking: $(echo "$L" | wc -c) chars"
bash $T/link.sh 2>&1 | cut -c1-300 | tail -15
ls -la ../obj/local/arm64-v8a/*.so ../libs/arm64-v8a/ 2>/dev/null

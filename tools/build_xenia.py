"""Builds our XenDroid (Xenia, Xbox 360) core for Tremu from ../_src/xendroid.

XenDroid normally builds through Gradle; we drive its native CMake project directly with the same
NDK toolchain as Tremu (arm64-v8a, android-29, -O2 -DNDEBUG -g -fno-strict-aliasing like its
emulator-core/build.gradle). Windows long paths are off on this PC, so the sources are reached
through a short junction (G:\\xs) and built in G:\\xb.

Outputs, used by build.py:
  third_party/xenia_tremu_libretro_android.so  (libe.so with tremu_libretro.cpp: libtremu_xenia.so)
  third_party/adrenotools/*.so               (libadrenotools hook libraries, next to it in lib/)
Usage: python tools/build_xenia.py [--clean]
"""
import os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WFVR = ROOT.parent
SOURCE = WFVR / '_src' / 'xendroid' / 'emulator-core' / 'src' / 'main' / 'cpp'
# XenDroid needs NDK 29's libc++ (std::atomic_ref); Tremu itself stays on r27c.
NDK = WFVR / '_tools' / 'android-sdk' / 'ndk' / '29.0.14206865'
CMAKE = WFVR / '_tools' / 'cmake' / 'cmake' / 'data' / 'bin' / 'cmake.exe'
NINJA = WFVR / 'time-crisis-vr' / '.tools' / 'ninja' / 'ninja.exe'
GLSLANG = WFVR / '_tools' / 'glslang' / 'bin'          # glslangValidator.exe (Khronos release)
SPIRV = NDK / 'shader-tools' / 'windows-x86_64'          # spirv-opt.exe, spirv-dis.exe
LINK = Path('G:/xs')
BUILD = Path('G:/xb')
HOOKS = ['main_hook', 'file_redirect_hook', 'gsl_alloc_hook', 'hook_impl']


def run(*args, **kw):
    print('> ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kw)


def main():
    if '--clean' in sys.argv:
        shutil.rmtree(BUILD, ignore_errors=True)
    if not LINK.exists():
        run('cmd', '/c', 'mklink', '/J', str(LINK), str(SOURCE))
    env = dict(os.environ)
    env['PATH'] = os.pathsep.join([str(GLSLANG), str(SPIRV), str(Path(sys.executable).parent), env['PATH']])
    # C: is nearly full: temporary files on G:, and few jobs (clang ran out of memory at 20 jobs).
    env['TEMP'] = env['TMP'] = 'G:\\xb_tmp'
    os.makedirs(env['TEMP'], exist_ok=True)
    flags = '-O2 -DNDEBUG -g -fno-strict-aliasing'
    if not (BUILD / 'build.ninja').exists():
        run(CMAKE, '-S', LINK, '-B', BUILD, '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={NINJA}',
            f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake', '-DANDROID_ABI=arm64-v8a',
            '-DANDROID_PLATFORM=android-29', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
            f'-DCMAKE_C_FLAGS={flags}', f'-DCMAKE_CXX_FLAGS={flags}', f'-DPython3_EXECUTABLE={sys.executable}',
            env=env)
    run(CMAKE, '--build', BUILD, '--parallel', '6', '--target', 'emu', *HOOKS, env=env)
    strip = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
    out = ROOT / 'third_party' / 'xenia_tremu_libretro_android.so'
    run(strip, '--strip-debug', '-o', out, BUILD / 'libe.so')
    hooks = ROOT / 'third_party' / 'adrenotools'
    hooks.mkdir(exist_ok=True)
    for hook in HOOKS:
        found = next(BUILD.rglob(f'lib{hook}.so'))
        run(strip, '--strip-unneeded', '-o', hooks / found.name, found)
    print(f'Built {out} ({out.stat().st_size / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()

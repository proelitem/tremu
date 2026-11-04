"""Builds our xemu (Xbox, from the X1 BOX fork in ../_src/x1box) for Tremu with the Android NDK.

X1 BOX normally builds through Gradle; this drives its native CMake project directly, like
build_xenia.py. Windows long paths are off on this PC: short junction G:\\xo, build folder G:\\xob.
Needs Rust with the aarch64-linux-android target in ../_tools/cargo and ../_tools/rustup.
Usage: python tools/build_xemu.py [--clean] [--jobs N]
"""
import os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WFVR = ROOT.parent
SOURCE = WFVR / '_src' / 'x1box'
NDK = WFVR / '_tools' / 'android-sdk' / 'ndk' / '29.0.14206865'
CMAKE = WFVR / '_tools' / 'cmake' / 'cmake' / 'data' / 'bin' / 'cmake.exe'
NINJA = WFVR / 'time-crisis-vr' / '.tools' / 'ninja' / 'ninja.exe'
LINK = Path('G:/xo')
BUILD = Path('G:/xob')


def run(*args, **kw):
    print('> ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kw)


def main():
    if '--clean' in sys.argv:
        shutil.rmtree(BUILD, ignore_errors=True)
    jobs = sys.argv[sys.argv.index('--jobs') + 1] if '--jobs' in sys.argv else '6'
    if not LINK.exists():
        run('cmd', '/c', 'mklink', '/J', str(LINK), str(SOURCE))
    env = dict(os.environ)
    env['RUSTUP_HOME'] = str(WFVR / '_tools' / 'rustup')
    env['CARGO_HOME'] = str(WFVR / '_tools' / 'cargo')
    env['TEMP'] = env['TMP'] = 'G:\\xb_tmp'  # C: is nearly full
    env['ANDROID_NDK_HOME'] = env['ANDROID_NDK_ROOT'] = str(NDK)
    env['PYTHONPATH'] = str(WFVR / '_tools' / 'pylib_meson')  # meson 1.5: 1.12 rejects G:/ prefixes for Android
    env['PATH'] = os.pathsep.join([str(WFVR / '_tools' / 'cargo' / 'bin'), str(WFVR / '_tools' / 'pylib_meson' / 'bin'),
                                   str(NINJA.parent), str(Path(sys.executable).parent), env['PATH'],
                                   str(WFVR / '_tools' / 'msys64' / 'mingw64' / 'bin')])  # dlltool, gcc for Rust's host crates
    if not (BUILD / 'build.ninja').exists():
        run(CMAKE, '-S', LINK / 'android' / 'app' / 'src' / 'main' / 'cpp', '-B', BUILD, '-G', 'Ninja',
            f'-DCMAKE_MAKE_PROGRAM={NINJA}', f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake',
            '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-26', '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DXEMU_ANDROID_BUILD_ID=3', '-DXEMU_ENABLE_XISO_CONVERTER=OFF',
            f'-DPYTHON_EXECUTABLE={sys.executable}', env=env)
    run(CMAKE, '--build', BUILD, '--parallel', jobs, env=env)
    strip = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
    run(strip, '--strip-debug', '-o', ROOT / 'third_party' / 'xemu_tremu_libretro_android.so', BUILD / 'libxemu.so')
    run(strip, '--strip-unneeded', '-o', ROOT / 'third_party' / 'libSDL2.so', BUILD / 'sdl2-local' / 'libSDL2.so')


if __name__ == '__main__':
    main()

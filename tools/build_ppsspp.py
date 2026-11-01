"""Builds our PPSSPP libretro core (../../_src/ppsspp) into third_party/ppsspp_libretro_android.so.

Our only change to PPSSPP: the core option ppsspp_adhoc_relay, which sends a game's ad hoc traffic
through the relay of the host's built-in server. Across the Internet, only the host's ports are
open; on a local network the players keep their direct links.
"""
import os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT = ROOT.parent
SOURCE = PROJECT / '_src' / 'ppsspp'
BUILD = Path('G:/pb')  # short: the object paths under the source tree are long
TOOLS = PROJECT / 'time-crisis-vr' / '.tools'
NDK = TOOLS / 'ndk' / 'android-ndk-r27c'
CMAKE = PROJECT / '_tools' / 'cmake' / 'cmake' / 'data' / 'bin' / 'cmake.exe'
NINJA = TOOLS / 'ninja' / 'ninja.exe'

OPTION = '''   {
      "ppsspp_adhoc_relay",
      "Send Ad Hoc Traffic Through the Server's Relay",
      NULL,
      NULL,
      NULL,
      "network",
      BOOL_OPTIONS,
      "disabled"
   },
'''
READ = '''   var.key = "ppsspp_adhoc_relay";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.iAdhocServerRelayMode = (int)(strcmp(var.value, "enabled") ? AdhocServerRelayMode::AlwaysOff : AdhocServerRelayMode::AlwaysOn);

'''


def patch():
    options = SOURCE / 'libretro' / 'libretro_core_options.h'
    text = options.read_text(encoding='utf-8')
    if 'ppsspp_adhoc_relay' not in text:
        at = text.index('   {\n      "ppsspp_enable_upnp",') if '   {\n      "ppsspp_enable_upnp",' in text else text.index('   {\r\n      "ppsspp_enable_upnp",')
        options.write_text(text[:at] + OPTION + text[at:], encoding='utf-8', newline='')
    core = SOURCE / 'libretro' / 'libretro.cpp'
    text = core.read_text(encoding='utf-8')
    if 'ppsspp_adhoc_relay' not in text:
        at = text.index('   var.key = "ppsspp_enable_builtin_pro_ad_hoc_server";')
        core.write_text(text[:at] + READ + text[at:], encoding='utf-8', newline='')


def main():
    patch()
    BUILD.mkdir(exist_ok=True)
    temp = BUILD / 'tmp'
    temp.mkdir(exist_ok=True)
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp), TMPDIR=str(temp))
    subprocess.run([CMAKE, '-S', SOURCE, '-B', BUILD, '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={NINJA}',
                    f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake', '-DANDROID_ABI=arm64-v8a',
                    '-DANDROID_PLATFORM=android-29', '-DANDROID_STL=c++_static', '-DCMAKE_BUILD_TYPE=Release',
                    '-DLIBRETRO=ON', '-DUSING_QT_UI=OFF', '-DUSE_DISCORD=OFF', '-DUSE_FFMPEG=ON'], check=True, env=env)
    subprocess.run([CMAKE, '--build', BUILD, '--target', 'ppsspp_libretro', '--parallel', '12'], check=True, env=env)
    built = next(BUILD.rglob('ppsspp_libretro*.so'))
    strip = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
    target = ROOT / 'third_party' / 'ppsspp_libretro_android.so'
    shutil.copy2(built, target)
    subprocess.run([strip, '--strip-unneeded', target], check=True)
    print(f'Built {target} ({target.stat().st_size / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()

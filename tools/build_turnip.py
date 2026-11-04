"""Builds our Turnip Vulkan driver for Quest (see turnip_quest_patch.py) from ../_src/mesa, with the
WinNative patch set (../_src/wn-drivers) so it matches a WN-Turnip balanced build.
Output: third_party/turnip/Tremu-Turnip.zip (libvulkan_freedreno.so + meta.json), an asset of the APK.
The patches are applied once to the Mesa checkout. Usage: python tools/build_turnip.py
"""
import json, os, subprocess, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WFVR = ROOT.parent
MESA = WFVR / '_src' / 'mesa'
WN = WFVR / '_src' / 'wn-drivers' / 'patches'
NDK = (WFVR / '_tools' / 'android-sdk' / 'ndk' / '29.0.14206865' / 'toolchains/llvm/prebuilt/windows-x86_64/bin').as_posix()
NINJA = WFVR / 'time-crisis-vr' / '.tools' / 'ninja' / 'ninja.exe'
BUILD = Path('G:/tb')
WN_SCRIPTS = ['fix_gralloc_flushall', 'fix_a8xx_dev_info', 'apply_a8xx_gpus', 'apply_a7xx_gen1_quirks',
              'apply_a7xx_gen2_ubwc_hint', 'add_aimapper_gralloc', 'add_ubwc_swapchain_usage', 'apply_balance_variant']


def run(*args, **kw):
    print('> ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, cwd=MESA, **kw)


def replace(path, old, new):
    p = MESA / path
    s = p.read_text(encoding='utf-8')
    if old in s:
        p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


def patch_once():
    stamp = MESA / '.tremu-patched'
    if stamp.exists():
        return
    for script in WN_SCRIPTS:
        run(sys.executable, WN / f'{script}.py')
    run(sys.executable, ROOT / 'tools' / 'turnip_quest_patch.py')
    # The same NDK 29 fixes as WinNative's build_turnip.sh.
    replace('include/android_stub/cutils/native_handle.h', 'typedef const native_handle_t* buffer_handle_t;', 'typedef void* buffer_handle_t;')
    replace('src/util/u_gralloc/u_gralloc_fallback.c', ', hnd->handle', ', (void *)hnd->handle')
    replace('src/vulkan/runtime/vk_android.c', 'native_buffer->handle->', '((const native_handle_t *)native_buffer->handle)->')
    replace('src/vulkan/runtime/vk_android.c', 'anb->handle->', '((const native_handle_t *)anb->handle)->')
    m = MESA / 'meson.build'
    m.write_text(''.join(l for l in m.read_text(encoding='utf-8').splitlines(True) if '-Werror=gnu-empty-initializer' not in l),
                 encoding='utf-8', newline='\n')
    stamp.write_text('ok')


def resolve_symlinks():
    """Git on Windows checks symlinks out as text files holding their target: copy the real file."""
    listing = subprocess.run(['git', 'ls-files', '-s'], cwd=MESA, check=True, capture_output=True, text=True).stdout
    for line in listing.splitlines():
        if not line.startswith('120000'):
            continue
        link = MESA / line.split(chr(9), 1)[1]
        if not link.is_file() or link.stat().st_size > 512:  # already a real file
            continue
        text = link.read_text(encoding='utf-8', errors='replace').strip()
        target = link.parent / text
        if chr(10) not in text and target.is_file():
            link.write_bytes(target.read_bytes())
            print('symlink resolved:', link.relative_to(MESA))


def main():
    patch_once()
    resolve_symlinks()
    # The lexers are compiled for Android, not Windows: no <io.h>.
    replace('meson.build', "prog_flex = [prog_flex, '--wincompat']", 'prog_flex = [prog_flex]')
    # This lint compares paths with '/', so on Windows it flags every wrapper header: warn only.
    replace('src/freedreno/meson.build', "error('XML include check found errors", "message('XML include check found errors")
    cross = WFVR / '_tmp' / 'turnip' / 'android-aarch64.txt'
    target = '--target=aarch64-linux-android29'
    cross.write_text(f"""[binaries]
ar = '{NDK}/llvm-ar.exe'
c = ['{NDK}/clang.exe', '{target}']
cpp = ['{NDK}/clang++.exe', '{target}', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
c_ld = 'lld'
cpp_ld = 'lld'
strip = '{NDK}/llvm-strip.exe'
nm = '{NDK}/llvm-nm.exe'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
""", encoding='utf-8')
    env = dict(os.environ)
    env['PYTHONPATH'] = str(WFVR / '_tools' / 'pylib')
    env['NINJA'] = str(NINJA)
    env['TEMP'] = env['TMP'] = 'G:\\xb_tmp'
    env['PATH'] = os.pathsep.join([str(WFVR / '_tools' / 'glslang' / 'bin'), str(WFVR / '_tools' / 'winflexbison'), str(NINJA.parent), env['PATH']])
    meson = [sys.executable, '-m', 'mesonbuild.mesonmain']
    if not (BUILD / 'build.ninja').exists():
        run(*meson, 'setup', BUILD, '--cross-file', cross, '-Dbuildtype=release', '-Dstrip=true', '-Dplatforms=android',
            '-Dvideo-codecs=', '-Dplatform-sdk-version=29', '-Dandroid-stub=true', '-Dgallium-drivers=',
            '-Dvulkan-drivers=freedreno', '-Dvulkan-beta=true', '-Dfreedreno-kmds=kgsl', '-Degl=disabled',
            '-Dandroid-libbacktrace=disabled', env=env)
    run(NINJA, '-C', BUILD, '-j', '6', 'src/freedreno/vulkan/libvulkan_freedreno.so', env=env)
    lib = BUILD / 'src' / 'freedreno' / 'vulkan' / 'libvulkan_freedreno.so'
    out = ROOT / 'third_party' / 'turnip' / 'Tremu-Turnip.zip'
    meta = {'schemaVersion': 1, 'name': 'Tremu-Turnip', 'description': 'Turnip for Quest (GPUOBJ_ALLOC fallback), WinNative patches',
            'author': 'Tremu', 'packageVersion': '1', 'vendor': 'Mesa', 'driverVersion': 'Tremu-1', 'minApi': 29,
            'libraryName': 'libvulkan_freedreno.so'}
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(lib, lib.name)
        z.writestr('meta.json', json.dumps(meta, indent=2))
    print(f'Built {out} ({out.stat().st_size / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()

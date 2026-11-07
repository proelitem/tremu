"""Build, sign and optionally install Tremu.

Reuses the Android toolchain that time-crisis-vr's bootstrap downloaded into its .tools folder.
Usage: python build.py [--install] [--sync] [--rom game.zip [--rom cchip.zip ...]]
       python build.py --release          (public build, see docs/release.md)
  --sync     copies this folder's Roms/, Banner/ and bios/ to Tremu/ on the Quest
  --release  not debuggable, signed with the release key: TREMU_KEYSTORE (path), TREMU_KEY_ALIAS
             (default "tremu") and TREMU_KEYSTORE_PASS (read by apksigner from the environment, never
             written anywhere); refuses uncommitted changes, so the APK matches a published commit.
The version is in the VERSION file (major.minor.patch); versionCode = major*10000 + minor*100 + patch.
"""
import argparse, os, shutil, subprocess, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TOOLS = ROOT.parent / 'time-crisis-vr' / '.tools'
NDK = TOOLS / 'ndk' / 'android-ndk-r27c'
BT = TOOLS / 'buildtools' / 'android-15'
ANDROID_JAR = TOOLS / 'platform' / 'android-34-ext12' / 'android.jar'
NINJA = TOOLS / 'ninja' / 'ninja.exe'
CMAKE = ROOT.parent / '_tools' / 'cmake' / 'cmake' / 'data' / 'bin' / 'cmake.exe'
JDK = next((ROOT.parent / '_tools').glob('jdk-17*'))
ADB = Path(r'C:\Program Files\Meta Quest Developer Hub\resources\bin\adb.exe')
BUILD = ROOT / 'build'
PACKAGE = 'com.tremu.app'
LIBS = {
    'libtremu.so': BUILD / 'native' / 'libtremu.so',
    'libopenxr_loader.so': TOOLS / 'openxr/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so',
    # FBNeo knows the user's opwolf chips (recent MAME wants extra C-Chip dumps, MAME 2003-Plus an older .29).
    # Our FBNeo build (../_src/FBNeo, ndk-build): reports layers for the 3D view (Operation Wolf...).
    'libtremu_core.so': ROOT / 'third_party' / 'fbneo_tremu_libretro_android.so',
    # Our MAME 0.289 (../_src/mame-wfg, tools/build_mame.sh, ARM64 recompiler) for the sets FBNeo lacks, in a "mame" folder: Area 51,
    # CarnEvil... (their hard disk .chd goes in the set's folder).
    'libtremu_mame.so': ROOT / 'third_party' / 'mame_tremu_libretro_android.so',
    # Console cores, picked by src/systems.c from the game file (libretro buildbot, arm64-v8a).
    # Our FCEUmm build (../_src/libretro-fceumm, ndk-build): also reports each pixel's layer for the 3D view.
    'libtremu_fceumm.so': ROOT / 'third_party' / 'fceumm_tremu_libretro_android.so',
    'libtremu_snes9x.so': ROOT / 'third_party' / 'snes9x_libretro_android.so',
    # Nintendo 64: our Mupen64Plus-Next GLES3 build (../_src/mupen-wfg), the scene drawn once per eye.
    'libtremu_n64.so': ROOT / 'third_party' / 'mupen_tremu_libretro_android.so',
    # Mega Drive / Genesis, Master System, Game Gear (libretro buildbot's Genesis Plus GX).
    'libtremu_genesis.so': ROOT / 'third_party' / 'genesis_plus_gx_libretro_android.so',
    # Saturn (libretro buildbot's Beetle Saturn: the only Saturn core with the Virtua Gun).
    'libtremu_saturn.so': ROOT / 'third_party' / 'mednafen_saturn_libretro_android.so',
    'libtremu_pcsx.so': ROOT / 'third_party' / 'pcsx_rearmed_libretro_android.so',
    # Dreamcast and Naomi, drawn with OpenGL ES (core.c reads each frame back).
    'libtremu_flycast.so': ROOT / 'third_party' / 'flycast_libretro_android.so',
    # Wii and GameCube; its Data/Sys folder goes in the assets (see dolphin_sys).
    'libtremu_dolphin.so': ROOT / 'third_party' / 'dolphin_libretro_android.so',
    # Namco System 246/256 arcade (Time Crisis 3/4...): our Play! build (../_src/Play-, arcade boot and light gun).
    'libtremu_play.so': ROOT / 'third_party' / 'play_tremu_libretro_android.so',
    # PS2 discs with the GunCon 2: our LRPS2 build (../_src/ps2, usb-guncon2.cpp).
    'libtremu_pcsx2.so': ROOT / 'third_party' / 'pcsx2_tremu_libretro_android.so',
    # Xbox 360: our XenDroid build (../_src/xendroid, Xenia + tremu_libretro.cpp), see tools/build_xenia.py.
    'libtremu_xenia.so': ROOT / 'third_party' / 'xenia_tremu_libretro_android.so',
    # Original Xbox: our xemu build (../_src/x1box + tremu_xemu.cpp), see tools/build_xemu.py; it needs SDL.
    'libtremu_xemu.so': ROOT / 'third_party' / 'xemu_tremu_libretro_android.so',
    'libSDL2.so': ROOT / 'third_party' / 'libSDL2.so',
    # PSP: our PPSSPP build (../_src/ppsspp, tools/build_ppsspp.py), with the relay Internet play needs; its data goes in the assets.
    'libtremu_ppsspp.so': ROOT / 'third_party' / 'ppsspp_libretro_android.so',
    # TensorFlow Lite 2.16.1 (Apache 2.0, official AARs): depth network of the 3D view, see src/depth.c.
    'libtensorflowlite_jni.so': ROOT / 'third_party' / 'libtensorflowlite_jni.so',
    'libtensorflowlite_gpu_jni.so': ROOT / 'third_party' / 'libtensorflowlite_gpu_jni.so',
}


def run(*args):
    print('> ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def native():
    sys.path.insert(0, str(ROOT / 'tools'))
    import lang
    lang.generate(ROOT)  # translations for the C side (and LangTable.java for dex())
    run(CMAKE, '-S', ROOT, '-B', BUILD / 'native', '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={NINJA}',
        f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake', '-DANDROID_ABI=arm64-v8a',
        '-DANDROID_PLATFORM=android-29', '-DCMAKE_BUILD_TYPE=Release', f'-DTOOLS={TOOLS}')
    run(CMAKE, '--build', BUILD / 'native', '--parallel', '16')


def dex():
    """Compiles the Java side (activity, menu text, WonderFitter Bluetooth) to classes.dex."""
    classes = BUILD / 'classes'
    shutil.rmtree(classes, ignore_errors=True)
    classes.mkdir(parents=True)
    sources = sorted((ROOT / 'java').rglob('*.java'))
    run(JDK / 'bin/javac.exe', '-encoding', 'UTF-8', '--release', '8', '-classpath', ANDROID_JAR, '-d', classes, *sources)
    run(JDK / 'bin/java.exe', '-cp', BT / 'lib/d8.jar', 'com.android.tools.r8.D8', '--min-api', '29',
        '--lib', ANDROID_JAR, '--output', BUILD, *sorted(classes.rglob('*.class')))
    return BUILD / 'classes.dex'


DOLPHIN_SYS = ROOT.parent / '_src' / 'dolphin' / 'Data' / 'Sys'
PLAY_ARCADEDEFS = ROOT.parent / '_src' / 'Play-' / 'arcadedefs'


def zip_folder(source, target):
    """Zips a folder into the APK assets once (Dolphin's Sys, Play!'s arcade definitions)."""
    if target.exists() or not source.is_dir():
        return
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(source.rglob('*')):
            if f.is_file():
                z.write(f, f.relative_to(source).as_posix())


def version():
    """VERSION file: "major.minor.patch" -> (versionName, versionCode)."""
    name = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
    major, minor, patch = (int(x) for x in name.split('.'))
    if not (0 <= minor < 100 and 0 <= patch < 100):
        sys.exit(f'VERSION {name}: minor and patch must stay below 100')
    return name, major * 10000 + minor * 100 + patch


def git_commit(release):
    """The commit the APK is built from; a release needs a clean tree (its source must be that commit)."""
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True).stdout.strip()
    commit = git('rev-parse', '--short', 'HEAD')
    dirty = git('status', '--porcelain', '--untracked-files=no')
    if release and dirty:
        sys.exit('Release: commit your changes first, the published source must match the APK:\n' + dirty)
    return commit + ('-dirty' if dirty else '')


def manifest(release):
    """The manifest as built: a release is not debuggable (tests use run-as on debug builds)."""
    text = (ROOT / 'AndroidManifest.xml').read_text(encoding='utf-8')
    if release:
        if 'android:debuggable="true"' not in text:
            sys.exit('AndroidManifest.xml: android:debuggable="true" not found')
        text = text.replace('android:debuggable="true"', 'android:debuggable="false"')
    out = BUILD / 'AndroidManifest.xml'
    out.write_text(text, encoding='utf-8')
    return out


def release_key():
    key = os.environ.get('TREMU_KEYSTORE')
    if not key or not Path(key).is_file() or not os.environ.get('TREMU_KEYSTORE_PASS'):
        sys.exit('Release: set TREMU_KEYSTORE (the release keystore) and TREMU_KEYSTORE_PASS (see docs/release.md)')
    return key, os.environ.get('TREMU_KEY_ALIAS', 'tremu')


def sign(aligned, signed, release):
    if not release:
        key = ROOT / 'debug.keystore'
        if not key.exists():
            run(JDK / 'bin/keytool.exe', '-genkeypair', '-keystore', key, '-storepass', 'android', '-alias', 'tremu',
                '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=Tremu')
        run(JDK / 'bin/java.exe', '-jar', BT / 'lib/apksigner.jar', 'sign', '--ks', key, '--ks-pass', 'pass:android',
            '--out', signed, aligned)
        return
    key, alias = release_key()
    # env: makes apksigner read the password itself; it never appears on a command line or in a log.
    run(JDK / 'bin/java.exe', '-jar', BT / 'lib/apksigner.jar', 'sign', '--ks', key, '--ks-key-alias', alias,
        '--ks-pass', 'env:TREMU_KEYSTORE_PASS', '--out', signed, aligned)
    run(JDK / 'bin/java.exe', '-jar', BT / 'lib/apksigner.jar', 'verify', '--print-certs', signed)


def package(release=False):
    name, code = version()
    commit = git_commit(release)
    base, aligned = BUILD / 'base.apk', BUILD / 'aligned.apk'
    signed = BUILD / (f'tremu-{name}.apk' if release else 'tremu.apk')
    print(f'Tremu {name} (versionCode {code}) from commit {commit}' + (', release' if release else ', debug'), flush=True)
    classes_dex = dex()
    # Launcher icon from the branding folder.
    res = BUILD / 'res' / 'mipmap'
    res.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'branding' / 'tremu-icon.png', res / 'ic_launcher.png')
    run(BT / 'aapt2.exe', 'compile', '--dir', BUILD / 'res', '-o', BUILD / 'res.zip')
    # Splash logo, read by TremuActivity.loadAsset.
    assets = BUILD / 'assets'
    assets.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'branding' / 'tremu-logo.png', assets / 'logo.png')
    # Carousel background and header logo, read by Carousel.java.
    shutil.copy2(ROOT / 'branding' / 'tremu-carousel-bg.jpg', assets / 'carousel_bg.jpg')
    shutil.copy2(ROOT / 'branding' / 'tremu-logo-plate.png', assets / 'carousel_logo.png')
    shutil.copy2(ROOT / 'branding' / 'tremu-logo-small.png', assets / 'menu_logo.png')
    shutil.copy2(ROOT / 'assets' / 'arcade.tsv', assets / 'arcade.tsv')  # tools/make_artmap.py
    zip_folder(DOLPHIN_SYS, assets / 'dolphin-sys.zip')
    zip_folder(PLAY_ARCADEDEFS, assets / 'play-arcadedefs.zip')
    zip_folder(ROOT / 'third_party' / 'ppsspp_assets', assets / 'ppsspp-assets.zip')
    # Our Turnip Vulkan driver for Xenia (Mesa, MIT; tools/build_turnip.py), unpacked by main.c into
    # files/drivers/<name>/ before an Xbox 360 game.
    for driver in sorted((ROOT / 'third_party' / 'turnip').glob('*.zip')):
        shutil.copy2(driver, assets / f'turnip-{driver.name}')
    # Build info for the curious and for bug reports (which commit an APK is).
    (assets / 'build.txt').write_text(f'{name}\n{code}\n{commit}\n{"release" if release else "debug"}\n', encoding='utf-8')
    run(BT / 'aapt2.exe', 'link', '--manifest', manifest(release), '-I', ANDROID_JAR, '-R', BUILD / 'res.zip',
        '-A', assets, '-o', base, '--version-code', str(code), '--version-name', name, '--replace-version')
    strip = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
    with zipfile.ZipFile(base, 'a', zipfile.ZIP_DEFLATED) as z:
        z.write(classes_dex, 'classes.dex')
        # libadrenotools loads its hooks from the app's lib folder (custom Turnip drivers for Xenia).
        libs = dict(LIBS, **{p.name: p for p in sorted((ROOT / 'third_party' / 'adrenotools').glob('*.so'))})
        for name, source in libs.items():
            staged = BUILD / name
            shutil.copy2(source, staged)
            if name == 'libtremu.so':
                run(strip, '--strip-unneeded', staged)
            z.write(staged, 'lib/arm64-v8a/' + name)
    run(BT / 'zipalign.exe', '-f', '-P', '16', '4', base, aligned)
    sign(aligned, signed, release)
    print(f'Built {signed} ({signed.stat().st_size / 1e6:.1f} MB)')
    return signed


def install(apk, roms):
    os.environ['MSYS_NO_PATHCONV'] = '1'
    run(ADB, 'shell', 'am', 'force-stop', PACKAGE)
    run(ADB, 'install', '-r', '-g', apk)  # -g grants the Bluetooth permissions up front
    # "All files access" for Tremu/Roms and Tremu/Banner (the Quest has no settings screen for it).
    run(ADB, 'shell', 'appops', 'set', '--uid', PACKAGE, 'MANAGE_EXTERNAL_STORAGE', 'allow')
    for rom in roms:
        # Stage in /data/local/tmp, then copy into the app's internal roms folder as the app user.
        tmp = f'/data/local/tmp/{rom.name}'
        run(ADB, 'push', rom, tmp)
        run(ADB, 'shell', 'chmod', '644', tmp)
        run(ADB, 'shell', f'run-as {PACKAGE} sh -c "mkdir -p files/roms && cp {tmp} files/roms/"')
        run(ADB, 'shell', 'rm', tmp)


def sync():
    """Mirrors Roms/, Banner/ and bios/ next to this script into the Quest's shared Tremu folder."""
    os.environ['MSYS_NO_PATHCONV'] = '1'
    for folder in ('Roms', 'Banner', 'bios'):
        source = ROOT / folder
        if not source.is_dir():
            continue
        dest = f'/storage/emulated/0/Tremu/{folder}'
        # Subfolders too: the core reads bios/fbneo/hiscore.dat.
        for d in [source] + sorted(x for x in source.rglob('*') if x.is_dir()):
            run(ADB, 'shell', 'mkdir', '-p', f'{dest}/{d.relative_to(source).as_posix()}'.rstrip('/.'))
        for f in sorted(source.rglob('*')):
            if f.is_file():
                run(ADB, 'push', f, f'{dest}/{f.relative_to(source).as_posix()}')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--install', action='store_true')
    # Repeat for a game and its support archives, e.g. --rom opwolf.zip --rom cchip.zip
    p.add_argument('--rom', type=Path, action='append', default=[])
    p.add_argument('--sync', action='store_true')
    p.add_argument('--release', action='store_true')
    args = p.parse_args()
    BUILD.mkdir(exist_ok=True)
    if args.release:  # before the long native build
        release_key()
        git_commit(True)
    native()
    apk = package(args.release)
    if args.install:
        install(apk, args.rom)
    if args.sync:
        sync()

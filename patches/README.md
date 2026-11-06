# Modifications des émulateurs

Les cœurs propres à Tremu sont compilés depuis les sources d'origine, gardées hors de ce dépôt dans `../_src`. Chaque fichier ci-dessous est ce que nous avons changé, pour que le travail et les sources correspondantes existent sans ces dossiers. Il s'applique avec `git apply` sur le commit d'origine indiqué.

| Fichier | Projet d'origine | Commit | Compilé par |
|---|---|---|---|
| `fbneo.diff` | https://github.com/libretro/FBNeo (arcade) | 9d82650 | ndk-build dans `src/burner/libretro/jni` |
| `fceumm.diff` | https://github.com/libretro/libretro-fceumm (NES) | ac0d00a | ndk-build dans `jni` |
| `mame.diff` | https://github.com/libretro/mame (arcade) | 5f5a077 | `tools/build_mame.sh` |
| `mupen.diff` | https://github.com/libretro/mupen64plus-libretro-nx (N64) | 15bada8 | `tools/build_mupen.sh` |
| `ps2.diff` | https://github.com/libretro/ps2 (LRPS2) | bd22f44 | ninja dans `_src/ps2/build_android` |
| `play.diff` | https://github.com/jpd002/Play- (Namco 246/256) | 1f27232 | ninja dans `build_retro/build_arm64`, cible `play_libretro` |
| `ppsspp.diff` | https://github.com/hrydgard/ppsspp (PSP) | 659e05c | `tools/build_ppsspp.py` |
| `xemu.diff` | https://github.com/WinDroidEmulation/x1-box (Xbox, d'après xemu) | 4eed5f7 | `tools/build_xemu.py` |
| `xendroid.diff` | https://github.com/rfandango/XenDroid (Xbox 360, d'après Xenia) | 779680a | `tools/build_xenia.py` |
| `vita3k.diff` | https://github.com/Vita3K/Vita3K (PS Vita, application séparée) | 66ddb35 | `_tools/vita3k_build.ps1` |
| `cemu.diff` | https://github.com/SSimco/Cemu, branche `android-port` (Wii U, application séparée) | 8759323 | `_tools/cemu_build.ps1` |

Les shaders de la vue stéréo de Xbox 360 sous `gpu/shaders/bytecode` sont générés : compiler les `tremu_stereo_*.cs.glsl` avec `tools/build/compile_shader_spirv.py` après avoir appliqué le diff (le build de `tools/build_xenia.py` le fait).

Le pilote Vulkan « Tremu-Turnip » est un fichier binaire, absent des diffs : `tools/build_turnip.py` le produit (`third_party/turnip/Tremu-Turnip.zip`) et il est copié dans `android/app/assets/drivers/` de Vita3K.

Les diffs sont produits par `tools/make_patches.py` (nos commits, les changements non commités et les nouveaux fichiers). À relancer après chaque modification d'un émulateur, avant de publier.

Les autres cœurs (SNES, Mega Drive, Saturn, PS1, Dreamcast, GameCube et Wii) ne sont pas modifiés : ils viennent du buildbot libretro.

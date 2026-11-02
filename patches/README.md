# Patches to the emulator cores

Tremu's own cores are built from upstream sources kept outside this repository, in `../_src`. These diffs are what we changed, so that the work survives without those folders. Each applies with `git apply` at the upstream commit named here.

| File | Upstream | Commit | Built by |
|---|---|---|---|
| `xendroid.diff` | https://github.com/rfandango/XenDroid (Xenia, Xbox 360) | 779680a | `tools/build_xenia.py` |
| `ps2.diff` | https://github.com/libretro/ps2 (LRPS2) | bd22f44 | ninja in `_src/ps2/build_android` |
| `mupen.diff` | https://github.com/libretro/mupen64plus-libretro-nx (N64) | 15bada8 | `tools/build_mupen.sh` |

The Xbox 360 stereo's shaders under `gpu/shaders/bytecode` are generated: compile `wfg_stereo_*.cs.glsl` with `tools/build/compile_shader_spirv.py` once after applying.

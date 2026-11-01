# Code tiers

## Dans ce dépôt

- `third_party/libretro.h` : API libretro, licence MIT (Libretro team), notice en tête du fichier.
- Parties reprises de [Time Crisis VR](https://github.com/DR-89/time-crisis-vr) (hôte OpenXR, visée, options, `WonderFitterGun.java`), licence MIT :

```
Copyright (c) 2026 DR-89 and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Hors du dépôt, intégré à l'APK

Ces binaires ne sont pas publiés ici. Toute distribution de l'APK doit respecter leurs licences.

| Composant | Rôle | Origine | Licence | Contrainte |
|---|---|---|---|---|
| FBNeo | Arcade | Modifié | Licence FBNeo, non commerciale | Publier nos changements, inclure le texte de la licence, pas de vente ni de dons |
| MAME | Arcade | Modifié | GPL-2.0 ou ultérieure | Publier les sources |
| FCEUmm | NES | Modifié | GPL-2.0 | Publier les sources |
| Snes9x | SNES | Buildbot libretro | Licence Snes9x, non commerciale | Pas d'usage commercial |
| Mupen64Plus-Next | N64 | Modifié | GPL-2.0 | Publier les sources |
| Genesis Plus GX | Mega Drive, Mega-CD | Buildbot libretro | Licence propre, non commerciale | Pas de vente ni d'usage commercial |
| Beetle Saturn | Saturn | Buildbot libretro | GPL-2.0 | Indiquer les sources |
| PCSX ReARMed | PS1 | Buildbot libretro | GPL-2.0 | Indiquer les sources |
| LRPS2 | PS2 | Modifié | GPL-3.0 | Publier les sources |
| Play! | Namco 246/256 | Modifié | BSD-2-Clause | Garder la notice |
| Flycast | Dreamcast, Naomi | Buildbot libretro | GPL-2.0 | Indiquer les sources |
| Dolphin | GameCube, Wii | Buildbot libretro | GPL-2.0 ou ultérieure | Indiquer les sources |
| PPSSPP | PSP | Modifié | GPL-2.0 ou ultérieure | Publier les sources |
| xemu (X1 BOX) | Xbox | Modifié | GPL-2.0 | Publier les sources |
| Xenia (XenDroid) | Xbox 360 | Modifié | BSD-3-Clause, selon les fichiers | Garder les notices |
| Mesa Turnip | Pilote Vulkan | Modifié | MIT | Garder la notice |
| libadrenotools | Chargement du pilote | Binaire | BSD-2-Clause | Garder la notice |
| TensorFlow Lite | Relief IA | AAR officiels | Apache-2.0 | Garder la notice |
| SDL2 | Xbox | Binaire | Zlib | Aucune notable |
| OpenXR Loader | Réalité virtuelle | Binaire | Apache-2.0 | Garder la notice |

Hors de l'APK, lancées comme applications séparées : Cemu (MPL-2.0), Vita3K (GPL-2.0).

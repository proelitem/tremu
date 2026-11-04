# Tremu

Émulateur pour Meta Quest, né comme émulateur de pistolets WonderFitter et étendu à des consoles Sony, Nintendo, Sega et Microsoft. Les jeux s'affichent sur un écran virtuel en réalité virtuelle (OpenXR) ; on vise avec le pistolet ou les manettes Touch.

Tremu ne fournit ni jeux, ni BIOS, ni clés, ni liens de téléchargement : le joueur apporte ses propres fichiers.

## Fichiers du joueur

- Jeux, mises à jour et DLC : `Download/Roms/<console>` sur le casque.
- BIOS et fichiers système : `Tremu/bios` et `Tremu/Xbox/system`.
- Affiches : `Tremu/Banner` (les affiches manquantes sont téléchargées).

## Compilation

```
python build.py --install
```

`build.py` attend, à côté de ce dossier, les outils `../time-crisis-vr/.tools` (NDK, OpenXR, outils Android) et `../_tools` (CMake, JDK), et les cœurs compilés dans `third_party/`. Ces cœurs ne sont pas dans le dépôt : ils viennent du buildbot libretro ou de nos versions, construites par `tools/build_*`. Voir [THIRD_PARTY.md](THIRD_PARTY.md).

Le plan de travail est dans [ROADMAP.md](ROADMAP.md).

## Licence et crédits

Tremu est publié sous la GNU GPL v3 ou ultérieure ([LICENSE](LICENSE)). Il charge des émulateurs écrits par d'autres, chacun sous sa licence : les auteurs, les licences et nos modifications sont dans [THIRD_PARTY.md](THIRD_PARTY.md), les textes de licence dans [licenses/](licenses/), et nos modifications des émulateurs dans [patches/](patches/). Trois émulateurs de l'APK (FBNeo, Snes9x, Genesis Plus GX) interdisent un usage commercial : voir les contraintes avant de distribuer un APK.

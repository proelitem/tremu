# Crédits et licences

Tremu est publié sous la **GNU GPL v3 ou ultérieure** (voir `LICENSE`). Copyright (C) 2026 proelitem et contributeurs.

Tremu ne fait que charger des émulateurs écrits par d'autres. Ce fichier nomme chaque projet repris, son auteur, sa licence et ce que nous y avons changé. Les textes de licence sont dans `licenses/`. Les modifications que nous avons apportées aux émulateurs sont dans `patches/`, sous forme de diffs à appliquer sur le commit d'origine indiqué ici.

Tremu ne contient ni jeu, ni BIOS, ni clé, et ne donne aucun lien pour en obtenir. Les marques citées appartiennent à leurs propriétaires ; Tremu n'est affilié à aucun d'eux.

## Pourquoi la GPL v3

- Les émulateurs liés à Tremu sont presque tous sous GPL (voir plus bas). La GPL v3 est la licence de la plupart des interfaces du même genre (RetroArch) et protège le travail de tous contre une reprise fermée.
- Tremu charge aussi le chargeur OpenXR et TensorFlow Lite, sous Apache 2.0. Cette licence est compatible avec la GPL v3 mais pas avec la GPL v2 seule : c'est pourquoi Tremu ne choisit pas la v2.
- Chaque émulateur est un programme séparé, chargé à l'exécution par l'interface libretro (`libretro.h`) ; il garde sa propre licence. Ils sont dans le même APK sans former un seul programme.

**Contraintes à connaître avant de distribuer un APK** (celles qui s'appliquent à un APK, pas au code source seul) :

- FBNeo, Snes9x et Genesis Plus GX interdisent toute vente et tout usage commercial ; FBNeo interdit aussi de demander des dons. Un APK qui les contient ne peut donc être ni vendu, ni financé par des dons.
- Les cœurs sous GPL obligent à fournir leurs sources à qui reçoit l'APK : les sources d'origine sont chez leurs auteurs (liens ci-dessous), nos modifications sont dans `patches/`.
- Les licences MIT, BSD et Apache obligent à garder les mentions de copyright ci-dessous avec toute copie.
- Cette note n'est pas un avis juridique.

## Tremu et ce qu'il reprend

| Composant | Auteur / projet | Licence | Détail |
|---|---|---|---|
| Hôte OpenXR, visée, réglages d'arme, protocole du pistolet WonderFitter | [Time Crisis VR](https://github.com/DR-89/time-crisis-vr), DR-89 et contributeurs | MIT | Parties reprises ; mention ci-dessous |
| `third_party/libretro.h` | Équipe RetroArch | MIT | Mention en tête du fichier |
| Chargeur OpenXR | [Khronos OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) | Apache 2.0 | Binaire inclus dans l'APK |
| TensorFlow Lite 2.16.1 | Google, [TensorFlow](https://github.com/tensorflow/tensorflow) | Apache 2.0 | Archives officielles, relief IA |
| Modèle de profondeur MiDaS | Intel ISL, [MiDaS](https://github.com/isl-org/MiDaS), conversion TFLite par Qualcomm AI Hub | MIT | Téléchargé par le casque quand il sert ; pas dans l'APK |
| SDL2 | [libsdl-org/SDL](https://github.com/libsdl-org/SDL) | zlib | Utilisé par xemu |
| libadrenotools | [bylaws/libadrenotools](https://github.com/bylaws/libadrenotools) | BSD 2 clauses | Chargement du pilote Vulkan |
| Pilote Vulkan Turnip | [Mesa](https://gitlab.freedesktop.org/mesa/mesa), correctifs de [WinNative Drivers](https://github.com/WinNative-Emu/Drivers) | MIT (Mesa), GPL v3 (correctifs) | Compilé par `tools/build_turnip.py` + `tools/turnip_quest_patch.py` (repli sur `GPUOBJ_ALLOC`) |

Mention de Time Crisis VR :

```
MIT License
Copyright (c) 2026 DR-89 and contributors
```

(Le texte complet est dans `licenses/mit.txt`.) Time Crisis VR s'appuie lui-même sur [namco22-decompile](https://github.com/spacestate1/namco22-decompile) (MIT, copyright (c) 2026 cmcrann), dont Tremu ne reprend rien.

## Émulateurs

Tous sont dans l'APK, sauf indication contraire. « Modifié » signifie que nous avons changé le code : le diff est dans `patches/` (ou le sera avant toute publication de l'APK).

| Système | Émulateur | Source | Commit | Licence | Modifié |
|---|---|---|---|---|---|
| Arcade | FBNeo | [libretro/FBNeo](https://github.com/libretro/FBNeo) | 9d82650 | Licence FBNeo, non commerciale (`licenses/fbneo.txt`) | Oui : couches pour la vue 3D |
| Arcade (jeux MAME) | MAME | [libretro/mame](https://github.com/libretro/mame) | 5f5a0774 | GPL v2 ; certains fichiers BSD 3 clauses, MIT ou autres (`licenses/mame.txt`) | Oui : recompilateur ARM64, étalonnage du pistolet, sauvegarde |
| NES | FCEUmm | [libretro/libretro-fceumm](https://github.com/libretro/libretro-fceumm) | ac0d00a | GPL v2 | Oui : couches pour la vue 3D |
| SNES | Snes9x | [libretro/snes9x](https://github.com/libretro/snes9x) | version du buildbot libretro | Licence Snes9x, non commerciale (`licenses/snes9x.txt`) | Non |
| N64 | Mupen64Plus-Next | [libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx) | 15bada8 | GPL v2 | Oui : image dessinée une fois par œil |
| Mega Drive, Master System, Game Gear, Mega-CD | Genesis Plus GX | [libretro/Genesis-Plus-GX](https://github.com/libretro/Genesis-Plus-GX) | version du buildbot libretro | Licence propre, non commerciale (`licenses/genesis-plus-gx.txt`) | Non |
| Saturn | Beetle Saturn | [libretro/beetle-saturn-libretro](https://github.com/libretro/beetle-saturn-libretro) | version du buildbot libretro | GPL v2 | Non |
| PS1 | PCSX ReARMed | [libretro/pcsx_rearmed](https://github.com/libretro/pcsx_rearmed) | version du buildbot libretro | GPL v2 | Non |
| PS2 | LRPS2 | [libretro/ps2](https://github.com/libretro/ps2) | bd22f44 | GPL v3 | Oui : GunCon 2, branchement à chaud USB, vue en relief |
| Namco 246/256 | Play! | [jpd002/Play-](https://github.com/jpd002/Play-) | 1f27232 | BSD 2 clauses (`licenses/play.txt`) | Oui : démarrage des jeux d'arcade, pistolet |
| Dreamcast, Naomi | Flycast | [libretro/flycast](https://github.com/libretro/flycast) | version du buildbot libretro | GPL v2 | Non |
| GameCube, Wii | Dolphin | [libretro/dolphin](https://github.com/libretro/dolphin) | c386bd3 | GPL v2 ou ultérieure | Non |
| PSP | PPSSPP | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | 659e05c | GPL v2 ou ultérieure ; fichiers de données et polices sous leurs propres licences (`LICENSE.TXT` fourni avec les données) | Oui : serveur ad hoc relayé |
| Xbox | xemu, via [X1 BOX](https://github.com/WinDroidEmulation/x1-box) | [xemu-project/xemu](https://github.com/xemu-project/xemu) | 4eed5f7 | GPL v2 | Oui : cœur libretro, pistolet, démarrage sans interface Java |
| Xbox 360 | Xenia, via [XenDroid](https://github.com/rfandango/XenDroid) | [xenia-project/xenia](https://github.com/xenia-project/xenia) | 1085d9f | BSD 3 clauses, copyright (c) 2015 Ben Vanik (`licenses/xenia.txt`) | Oui : cœur libretro, relief par œil |
| PS Vita | Vita3K, application séparée | [proelitem/Vita3K](https://github.com/proelitem/Vita3K) (branche `tremu`), d'après [Vita3K/Vita3K](https://github.com/Vita3K/Vita3K) 66ddb35 | c01da08 | GPL v2 | Oui : écran OpenXR, manettes Touch, lancement par Tremu |
| Wii U | Cemu Android, application séparée | [proelitem/Cemu](https://github.com/proelitem/Cemu) (branche `tremu`), d'après [SSimco/Cemu](https://github.com/SSimco/Cemu) 8759323 et [cemu-project/Cemu](https://github.com/cemu-project/Cemu) | 2d923b4 | MPL 2.0 | Oui : mode immersif OpenXR, manettes Touch, lancement par Tremu |
| PS3 | ARMSX3, application séparée | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) (branche `master`), d'après [ARMSX2/ARMSX3](https://github.com/ARMSX2/ARMSX3) 1.6.5 | 61724ad77 | GPL v2 | Oui : écran OpenXR, pistolet Touch (GunCon 3, PS Move), lancement par Tremu et retour |

Les cœurs sans modification viennent de la compilation du buildbot libretro (https://buildbot.libretro.com/) pour Android arm64.

## Applications compagnes

Tremu lance ces applications pour les consoles qu'il n'émule pas lui-même. Il faut **nos** versions :
les originales n'acceptent pas le jeu envoyé par Tremu ni le retour au carrousel. Chacune est un fork
public, sous la licence de son projet d'origine.

| Application | Paquet | Dépôt | Rôle |
|---|---|---|---|
| ARMSX3 (PS3) | `com.armsx3` | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) | Jeux PS3 |
| Cemu (Wii U) | `info.cemu.cemu` | [proelitem/Cemu](https://github.com/proelitem/Cemu), branche `tremu` | Jeux Wii U |
| Vita3K (PS Vita) | `org.vita3k.emulator` | [proelitem/Vita3K](https://github.com/proelitem/Vita3K), branche `tremu` | Jeux PS Vita |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr), branche `wonderfitter-m17` | Time Crisis en VR native, pistolet WonderFitter |

L'application Wonderfitter officielle n'est pas nécessaire : Tremu parle lui-même au pistolet en Bluetooth.

## Données incluses dans l'APK

- Table des jeux d'arcade `assets/arcade.tsv` : titres tirés des listes de FBNeo et des noms d'images du projet [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails). Les affiches elles-mêmes sont téléchargées par le casque quand il en a besoin et ne sont pas redistribuées.
- Fichiers `Sys` de Dolphin et définitions d'arcade de Play! : repris de leurs dépôts, mêmes licences.
- Logo, icône et fond du carrousel : créés par l'auteur de Tremu.

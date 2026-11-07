# Tremu

Émulateur pour Meta Quest, né comme émulateur de pistolets WonderFitter et étendu à des consoles Sony, Nintendo, Sega et Microsoft. Les jeux s'affichent sur un écran virtuel en réalité virtuelle (OpenXR) ; on vise avec le pistolet ou les manettes Touch.

Tremu ne fournit ni jeux, ni BIOS, ni clés, ni liens de téléchargement : le joueur apporte ses propres fichiers.

## Manettes

Les manettes Touch sont toujours le joueur 1 (pistolet ou pad selon le jeu, voir le menu « commandes
de la console »). En plus, jusqu'à quatre manettes USB ou Bluetooth, dans l'ordre où elles parlent :
la première s'ajoute au joueur 1, les suivantes sont les joueurs 2 à 4. Tout ce qu'Android reconnaît
comme manette est pris avec sa disposition standard : Xbox (360 filaire, One, Series), DualShock 4,
DualSense, 8BitDo et les pads HID génériques, sticks et bornes arcade USB, instruments Rock Band
(vus comme le pad qu'ils imitent). Select + Start ensemble, ou le bouton guide, ouvrent le menu ; dans
le carrousel, le stick ou la croix naviguent, le bouton du bas valide, celui de droite efface.

**Wiimote** : seule par Bluetooth, elle n'est pas une manette HID et Android n'a pas de pilote. Par
une **DolphinBar** (Mayflash W010, branchée en USB sur le casque, elle jumelle la Wiimote elle-même)
elle devient ce que le mode de la barre annonce : en mode 3 une manette (Wiimote + Nunchuk ou
manette Classic, prise par la couche manettes ci-dessus) ; en modes 1 et 2 une souris dont la
position est le pointeur infrarouge, plus des touches. Pour viser avec, activer « Visée par le
pointeur externe » dans le menu et poser la barre sous l'écran virtuel (recentrer l'écran devant soi
avec la barre en face) : le pointeur remplace alors le rayon de la manette Touch pour le joueur 1, dans
les jeux Wii comme dans les jeux de tir. Ses boutons passent par les rôles d'arme (A = clic gauche =
gâchette, B = clic droit = grenade / pédale), que le menu « Arme Bluetooth / USB » peut réapprendre.
Le mode 4 (Wiimote native pour Dolphin) n'est pas lisible par un cœur libretro.

Limites connues de la plate-forme, pas de Tremu : la DualShock 3 se jumelle mal en Bluetooth sans PC (en USB selon le noyau du casque),
et les GunCon, qui ne sont pas des périphériques HID, ne sont pas vus ; en VR c'est de toute façon la
manette Touch montée sur l'arme qui vise, un pistolet externe n'apporte que ses boutons (voir
`src/extgun.c`). Une partie en réseau n'emmène que le joueur 1 de chaque casque.

## Jeu en réseau

Les casques font tourner le même jeu image par image ; seules les commandes circulent (`src/netplay.c`).
Un joueur qui rejoint reçoit une copie de la partie de l'hôte, sauf pour les jeux MAME : leurs
sauvegardes d'état ne contiennent pas tout le jeu (mesuré sur Area 51 et CarnEvil, une copie s'écarte
de l'hôte en quelques secondes). Pour MAME, « Héberger » relance le jeu à zéro et l'hôte attend le
joueur 2 ; les deux casques démarrent ensemble une machine neuve (dossier `saves/netplay`, sans les
NVRAM ni les meilleurs scores du casque). Les casques comparent l'image toutes les secondes (la
mémoire pour les cœurs GPU) ; trois écarts de suite comptent comme une désynchronisation.
Chaque casque écrit son journal réseau dans `Tremu/netplay.log`.

Banc d'essai sans casque sur la tête (`test/nettest.c`, `_tools/nettest.ps1`) : `replay` dit si une
copie de partie reste exacte, `host` et `guest` font jouer deux processus ensemble sur un même Quest.

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

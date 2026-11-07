# Installer Tremu

[English](en.md) · [Deutsch](de.md) · [Español](es.md) · **Français** · [日本語](ja.md) · [Русский](ru.md) · [中文](zh.md)

Tremu transforme un Meta Quest 3 ou 3S en borne de tir : les jeux s'affichent sur un grand écran
virtuel, on vise avec le pistolet WonderFitter ou les manettes Touch. Tremu ne fournit **ni jeux, ni
BIOS, ni firmwares, ni clés** : tu utilises tes propres copies.

## 1. Préparer le casque

1. Active le **mode développeur** : dans l'application mobile Meta Horizon, ouvre ton casque, puis
   « Paramètres du casque » › « Mode développeur » (il faut une organisation développeur Meta, gratuite).
2. Installe un outil d'installation sur ton PC : [Meta Quest Developer Hub](https://developers.meta.com/horizon/documentation/android-apps/meta-quest-developer-hub/)
   ou [SideQuest](https://sidequestvr.com/). Branche le casque en USB et accepte le débogage USB dans le casque.

## 2. Installer Tremu

Installe `tremu.apk` avec l'outil (glisser-déposer l'APK sur le casque). Au premier lancement, Tremu demande :

- **Accès à tous les fichiers** : accepte. Tremu lit tes jeux dans `Download/Roms` et range ses
  fichiers dans le dossier `Tremu`.
- **Appareils à proximité (Bluetooth)** : accepte, pour le pistolet WonderFitter.

Tremu apparaît dans « Bibliothèque » › « Sources inconnues ».

## 3. Ajouter tes jeux

| Quoi | Où, sur le casque |
|---|---|
| Jeux | `Download/Roms/<console>`, un dossier par console (par ex. `Download/Roms/ps1`) |
| Jeux d'arcade MAME (Area 51, CarnEvil…) | `Download/Roms/mame/<jeu>` (le jeu dézippé, avec son `.chd`) |
| Jeux Wii U, PS3, PS Vita | `Download/Roms/wiiu`, `Download/Roms/ps3`, `Download/Roms/vita` |
| BIOS (PS2, Saturn, Mega-CD…) | `Tremu/bios` (PS2 : `Tremu/bios/pcsx2/bios`) |
| Système Xbox | `Tremu/Xbox/system` |
| Affiches | `Tremu/Banner` (les affiches manquantes se téléchargent seules) |

Sans câble : le bas du carrousel affiche « Ajouter des jeux : http://… » et un code. Ouvre cette
adresse depuis un PC ou un téléphone sur le même Wi-Fi et dépose tes fichiers ; Tremu les range.
Quand Tremu signale un fichier manquant (BIOS, disque dur), le message dit lequel et où le mettre.

## 4. Applications compagnes

Tremu émule lui-même l'arcade, la NES, la SNES, la N64, la Mega Drive, la Saturn, la PS1, la PS2, la PSP,
la Dreamcast, la GameCube, la Wii, la Xbox et la Xbox 360. Quatre consoles passent par une
application à part, que Tremu ouvre et qui revient au carrousel en fin de partie :

| Pour | Application | Source |
|---|---|---|
| PS3 | ARMSX3 (`com.armsx3`) | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) |
| Wii U | Cemu (`info.cemu.cemu`) | [proelitem/Cemu](https://github.com/proelitem/Cemu), branche `tremu` |
| PS Vita | Vita3K (`org.vita3k.emulator`) | [proelitem/Vita3K](https://github.com/proelitem/Vita3K), branche `tremu` |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr) |

Il faut **ces versions-là** : les versions officielles ne reçoivent pas le jeu choisi dans Tremu et
ne reviennent pas au carrousel. Si une version officielle est déjà installée (ARMSX3 surtout),
**désinstalle-la d'abord** : Android refuse de la remplacer par une version signée autrement.
Sans l'application, la tuile du jeu s'affiche quand même et Tremu dit laquelle installer.
Le firmware PS3, le firmware PS Vita (`.PUP`, dans `Download/Roms/vita`) et les clés Wii U sont à toi de fournir.

## 5. Pistolet WonderFitter

Allume le pistolet : Tremu le trouve tout seul en Bluetooth, sans l'application WonderFitter
officielle. Le pistolet n'accepte **qu'une application à la fois** : ferme l'application officielle
si elle est ouverte. Les réglages de montage (angle, décalages) se font dans le menu de Tremu et sont
gardés par pistolet. Sans pistolet, les manettes Touch visent.

Wiimote (jeux Wii) : seulement par une **DolphinBar** Mayflash branchée en USB sur le casque.

## 6. Jouer à plusieurs

Même Wi-Fi : un casque lance le jeu puis choisit menu › Multijoueur › « Héberger (Wi-Fi) » ; l'autre
voit une tuile « Rejoindre : … » dans le carrousel. Les deux casques doivent avoir **la même version
de Tremu et le même fichier de jeu**. Pour les jeux MAME, l'hôte redémarre le jeu à zéro et attend
le joueur 2 : on commence la partie ensemble.

Par Internet : « Héberger sur Internet » affiche un code à donner à l'autre joueur (menu ›
Rejoindre par code). Il n'y a aucun serveur payant : si le code ne vient pas, ouvre le port UDP 47617
sur ta box (ou active l'UPnP).

## Retour et menu

Bouton menu de la manette gauche : options du jeu. Maintenu 1,5 s dans un jeu lancé depuis Tremu
(PS3, Wii U, Vita, Time Crisis VR) : retour au carrousel.

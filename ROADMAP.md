# Tremu — planification

## À venir : Xbox et Wii U (applis Android séparées)

Ce ne sont pas des cœurs libretro : Tremu ne peut pas les charger en interne comme les autres
consoles. La piste est la même que pour Time Crisis VR et l'appli WonderFitter : une tuile dans le
carrousel qui lance l'appli installée à côté, avec le jeu choisi.

| Console | Projet | État constaté (29/10/2026) | À vérifier avant de s'engager |
|---|---|---|---|
| Xbox (originale) | [X1 BOX](https://github.com/WinDroidEmulation/x1-box), fork de xemu, GPL v2 | Appli Android sur Google Play et, au 30/10/2026, APK dans les releases (1.2.5, 20 Mo). Android 8+, arm64, **Vulkan obligatoire**, 8 Go de RAM conseillés. Manette Bluetooth/USB ou tactile. Il faut fournir MCPX Boot ROM, BIOS (Flash ROM), image de disque dur et jeux. Lu dans le code : `MainActivity` hérite de `SDLActivity` (SDL2 + QEMU dans le processus), lancement d'un jeu par intent déjà prévu (`VIEW`, extras `rom`/`path`/`uri`, `FrontendLaunchHelper.kt`), pilotes Turnip personnalisés déjà gérés (`GpuDriverHelper.kt`). Fork : NDK 29, CMake 3.30.3, Meson, JDK 21, Rust. | Tourne-t-elle sur Quest 3 et à quelle vitesse ? Les Touch sont-elles vues comme manette (sinon, à injecter dans notre fork) ? |
| Wii U | [Cemu Android (SSimco)](https://github.com/SSimco/Cemu), MPL 2.0 | Portage « précoce et expérimental », pas de release ni d'APK publiés, pas de doc sur l'entrée ni le lancement. | Compiler l'APK nous-mêmes, tester Vulkan sur Quest 3, voir si un lancement avec jeu (intent) existe ou s'ajoute, et comment brancher la visée (Wiimote/GamePad). |

Ces systèmes sont récents : il faudra les adapter (lancement par intent, armes, VR), avec d'éventuels
mods VR par jeu (voir plus bas).

Contraintes : on ne modifie pas ces applis si elles sont commerciales ; ici elles sont libres (GPL,
MPL), donc un fork avec un lancement par intent et l'entrée de nos armes reste possible. Les
fichiers système (BIOS Xbox, clés Wii U) restent fournis par le joueur.

## PS3 : tuile vers ARMSX3 (code écrit, à tester sur le casque)

PS3 suit le même schéma que Wii U : pas de cœur, une tuile par jeu, lancée dans l'appli ARMSX3
(fork d'ARMSX3 : branche `quest3/touch-input`, `applicationId` `com.armsx3`, écran virtuel OpenXR et
Touch). `Ps3Library.java` liste `Download/Roms/ps3` : un `.iso`, ou un dossier contenant
`PS3_GAME/PARAM.SFO` (titre et identifiant lus dans ce fichier). `WfgActivity.launchPs3()` envoie un
`ACTION_VIEW` à `com.armsx2.Main` avec l'extra `path`, comme le ferait un frontend (ES-DE,
Daijisho) : ARMSX3 démarre le dossier ou l'image tels quels.

À vérifier sur le casque : qu'ARMSX3 lit bien `Download/Roms/ps3` depuis un lancement externe
(droit d'accès au stockage), que le jeu part dans l'activité immersive, et comment revenir à
Tremu à la sortie (ARMSX3 n'a pas d'équivalent du `ReturnPackage` de Cemu). Les `.pkg` ne sont pas
listés : ARMSX3 les installe d'abord.

## PSP et PS Vita

**PSP (intégrée).** Notre PPSSPP (`tools/build_ppsspp.py`), jeux dans `Download/Roms/psp` (`.iso`, `.cso`, `.chd`, `EBOOT.PBP`), Touch réglables dans « Contrôles des consoles ». Le multijoueur passe par le mode ad hoc du jeu : chaque casque lance le serveur ad hoc intégré de PPSSPP, « Héberger » annonce la partie, et « Rejoindre » redémarre le jeu sur le serveur de l'hôte. Aucun serveur payant. Sur Internet, le code de partie envoie les invités sur le relais de l'hôte (ports TCP 27312 et 27313, ouverts par UPnP).

**PS Vita (intégrée, à essayer dans le casque).** Notre Vita3K (GPL-2.0, `_src/vita3k`, `_tools/vita3k_build.ps1`, vcpkg dédié `_src/vcpkg-vita`) est une appli à part, en OpenXR (`vita3k/android/jni/native_xr.cpp`). Une tuile par `.vpk` ou `.zip` de `Download/Roms/vita` ; `WfgLaunchActivity` installe au premier lancement les `.PUP` trouvés dans ce dossier, puis le jeu. Touch réglables dans « Contrôles des consoles », doigt sur l'écran avant ou arrière là où pointe la manette droite. Son ad hoc n'a pas de serveur : il passe par la diffusion sur le réseau local, donc même Wi-Fi ; compatibilité par jeu (Vita3K/Vita3K#3686). Sur Internet, il faudrait un VPN (ZeroTier, Tailscale). Le joueur fournit le firmware et ses jeux.

## À étudier : mods VR comme moteur pour d'autres jeux

But : réutiliser ce que ces projets ont résolu pour rendre d'autres jeux en vraie VR, ou redonner
vie à des classiques.

| Projet | Ce que c'est (29/10/2026) | Utilisable comment |
|---|---|---|
| [RDRVR](https://github.com/Disonomouss/RDRVR), MIT | Mod injecté (`dinput8.dll`) dans Red Dead Redemption **PC 2024**, Windows + DirectX 12 : rend la scène une fois par œil, gère la session OpenXR, la tête et les mains, et appelle les fonctions du jeu via RedHook. Adresses propres au jeu (`anchors.txt`). | Ne tourne pas sur Quest autonome (jeu PC, D3D12). Sert de **référence de conception** : rendu par œil, poses tardives, menu VR. Le code MIT est réutilisable avec sa licence. |
| vice-city-vr-quest | Kit de compilation d'un portage **Quest natif** de Vice City (OpenXR + Vulkan multiview), basé sur **reVC**. Pas d'APK : chacun compile le sien avec ses propres fichiers du jeu PC. | Comme Time Crisis VR : une **tuile du carrousel** qui lance l'APK que le joueur a compilé lui-même. Comme référence technique : couche Android/OpenXR, librw Vulkan multiview (MIT), remappage, calibrage par arme. |
| vice-city-vr | Même projet, version **PC VR** (Windows, D3D12 + OpenXR), aussi sur reVC. | Référence seulement. |

**Attention, reVC :** c'est une décompilation de Vice City que Take-Two a fait retirer (DMCA) puis
attaquée en justice en 2021. Tremu sera publié : on n'y intègre ni code reVC, ni code qui en
dérive. On se limite au lancement d'une appli que le joueur a compilée lui-même, et aux idées
techniques. Même règle pour RDRVR : on ne distribue rien du jeu.

**La piste « moteur » propre à Tremu :** une vraie 3D stéréo pour les consoles que nous rendons
déjà sur le GPU (N64 avec GLideN64, Dreamcast avec Flycast, PS2 avec LRPS2, GameCube et Wii avec
Dolphin). L'idée est de rendre chaque image deux fois, avec un petit décalage de caméra par œil
injecté dans la matrice de projection du cœur, à la manière de RDRVR mais dans nos propres builds
libres. C'est un niveau au-dessus des voxels et du relief IA d'aujourd'hui (vraie profondeur au
lieu d'une profondeur devinée). À prototyper d'abord sur un seul cœur (Flycast ou GLideN64).

## À faire : fond en réalité mixte

Remplacer le noir autour de l'écran, du carrousel et du menu par la vraie pièce, vue par les
caméras du casque (passthrough couleur du Quest 3).

- **Comment :** extension OpenXR `XR_FB_passthrough`. Un calque passthrough est posé sous nos
  calques (écran, carrousel, menu, voxels), et le manifeste déclare la fonction
  `com.oculus.feature.PASSTHROUGH`. Aujourd'hui `main.c` envoie un fond opaque
  (`XR_ENVIRONMENT_BLEND_MODE_OPAQUE`) sans calque derrière.
- **Option du menu :** « Fond : noir / pièce réelle / pièce assombrie » (opacité réglable), pour
  garder un fond noir quand on veut l'immersion d'une salle d'arcade.
- **Plus tard :** ancrer l'écran sur un mur détecté (Scene / MRUK : `XR_FB_scene`,
  `XR_FB_spatial_entity`), pour qu'il reste à sa place d'une partie à l'autre, comme une vraie
  borne dans la pièce. Utile aussi pour viser avec l'arme réelle en voyant ses mains et son
  pistolet.
- **Points à vérifier :** coût en performance avec les cœurs lourds (PS2, Wii), comportement avec
  la vue voxels et le relief IA, et les permissions (aucune caméra n'est lue par l'appli, le
  passthrough est composé par le système).

## À venir : jeux, mises à jour et DLC, sans tri manuel

Le joueur dépose ses fichiers (page web de Tremu ou `Download/Roms/<console>`, le dossier commun des ROM) ; il ne range rien de plus.

- **Une tuile par jeu** dans le carrousel, jamais une tuile par mise à jour ou DLC. Sa fiche montre ce qui est installé (« Mise à jour v208 », « DLC : oui », « Mise à jour manquante ») et permet de le supprimer.
- **Classement automatique** d'après le contenu du fichier, pas d'après son nom. Wii U : `meta/meta.xml` donne le titre, et le début de l'identifiant donne le type (`00050000` jeu, `0005000E` mise à jour, `0005000C` DLC). Un `.wua` peut regrouper les trois.
- **Vérifications** avant d'installer : même région que le jeu, espace libre, fichiers complets ; message clair sinon.
- **Mécanisme commun** : chaque système déclare comment reconnaître ses contenus (Wii U d'abord, puis Xbox 360 et PS3, qui ont aussi mises à jour et DLC).
- **Wii U** : notre Cemu doit lire les jeux hors de son dossier privé (droit d'accès au stockage, lecture de `Download/Roms/wiiu`). À tester.
- Tremu ne fournit ni jeux, ni clés, ni liens de téléchargement.

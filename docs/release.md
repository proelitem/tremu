# Publier Tremu (SideQuest)

Les textes de la fiche, dans les 7 langues de Tremu, sont dans [`docs/store/`](store/) ; le guide
d'installation pour les joueurs, dans [`docs/install/`](install/).

## Une fois : la clé de publication

Toutes les versions publiées doivent être signées avec **la même clé**. Si elle est perdue, plus
aucune mise à jour n'est possible : il faudrait une nouvelle fiche, et chaque joueur devrait
désinstaller Tremu. La clé ne va **jamais** dans le dépôt.

1. Crée-la hors du dépôt (par ex. `G:\Cles\tremu-release.jks`) ; `keytool` demande le mot de
   passe et ton nom :

   ```
   & 'G:\Projets\WFVR\_tools\jdk-17.0.20.1+1\bin\keytool.exe' -genkeypair -keystore G:\Cles\tremu-release.jks -alias tremu -keyalg RSA -keysize 4096 -validity 36500
   ```

2. Range le mot de passe dans un gestionnaire de mots de passe, et fais **deux copies** du `.jks`
   ailleurs que sur ce PC (clé USB, stockage chiffré).

La même clé signe les applications compagnes (ARMSX3, Cemu, Vita3K) quand elles auront leur fiche.

## À chaque version

1. Mets le numéro dans `VERSION` (`0.9.0`, puis `0.9.1`…) : `versionCode` en découle
   (`major*10000 + minor*100 + patch`) et doit toujours augmenter.
2. Vérifie le jeu en réseau sur le banc d'essai (`_tools/nettest.ps1`, voir le README) et joue
   quelques jeux dans le casque.
3. Mets à jour les correctifs des émulateurs : `python tools/make_patches.py`, puis commite tout.
4. Compile, avec la clé dans l'environnement de cette fenêtre PowerShell seulement :

   ```
   $env:TREMU_KEYSTORE = 'G:\Cles\tremu-release.jks'
   $env:TREMU_KEYSTORE_PASS = [Net.NetworkCredential]::new('', (Read-Host 'Mot de passe de la clé' -AsSecureString)).Password
   python build.py --release
   ```

   Résultat : `build/tremu-<version>.apk`, non débogable. Le script refuse s'il reste des
   changements non commités, pour que le code publié soit exactement celui de l'APK ; il affiche le
   commit et le certificat utilisés.
5. Marque le commit et pousse : `git tag v0.9.0`, `git push origin main --tags`.

Les casques qui ont une version de développement (signée avec la clé de débogage) doivent
désinstaller Tremu une fois avant d'installer une version publiée. Les réglages de Tremu sont
alors perdus ; les jeux, BIOS et affiches (dans `Download/Roms` et `Tremu/`) restent.

## Le code source

Tremu est sous GPL v3 et contient des émulateurs sous GPL : distribuer l'APK oblige à publier le code
qui l'a produit. Avant la première publication, le dépôt `proelitem/tremu` doit être public
(les forks des applications compagnes le sont déjà). `patches/` contient nos changements à chaque
émulateur, avec le commit d'origine.

FBNeo, Snes9x et Genesis Plus GX interdisent tout usage commercial : Tremu reste **gratuit**, sans
publicité ni contenu payant.

## La fiche SideQuest

Sur https://sidequestvr.com, avec ton compte développeur :

| Champ | Contenu |
|---|---|
| Nom, accroche, description, mots-clés | `docs/store/<langue>.md` |
| Paquet | `com.tremu.app` |
| Icône | `branding/tremu-icon.png` (512 × 512) |
| Logo, image de fond | `branding/tremu-logo.png`, `branding/tremu-carousel-bg.jpg` (aux dimensions demandées par SideQuest) |
| Captures | Dans le casque : bouton Meta › Appareil photo › Prendre une photo, pendant le carrousel et quelques parties ; elles sont dans `Oculus/Screenshots` |
| Code source | https://github.com/proelitem/tremu |
| Appareils | Quest 3 et 3S |
| APK | `build/tremu-<version>.apk` |

Relis les conditions développeur de SideQuest avant l'envoi (émulateurs, contenus tiers). Ne
publie ni jeu, ni BIOS, ni capture qui montre un logo de console comme si Tremu était officiel.
Time Crisis VR reste hors de SideQuest : il dépend d'une décompilation du jeu de Namco.

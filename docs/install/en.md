# Installing Tremu

**English** · [Deutsch](de.md) · [Español](es.md) · [Français](fr.md) · [日本語](ja.md) · [Русский](ru.md) · [中文](zh.md)

Tremu turns a Meta Quest 3 or 3S into a shooting cabinet: games are shown on a large virtual
screen and you aim with the WonderFitter gun or the Touch controllers. Tremu provides **no games,
BIOS, firmware or keys**: you use your own copies.

## 1. Prepare the headset

1. Turn on **developer mode**: in the Meta Horizon mobile app, open your headset, then
   "Headset settings" › "Developer mode" (it needs a Meta developer organization, which is free).
2. Install a sideloading tool on your PC: [Meta Quest Developer Hub](https://developers.meta.com/horizon/documentation/android-apps/meta-quest-developer-hub/)
   or [SideQuest](https://sidequestvr.com/). Plug the headset in over USB and allow USB debugging in the headset.

## 2. Install Tremu

Install `tremu.apk` with the tool (drag and drop the APK onto the headset). On first start, Tremu asks for:

- **All files access**: allow it. Tremu reads your games from `Download/Roms` and keeps its own
  files in the `Tremu` folder.
- **Nearby devices (Bluetooth)**: allow it, for the WonderFitter gun.

Tremu shows up under "Library" › "Unknown sources".

## 3. Add your games

| What | Where, on the headset |
|---|---|
| Games | `Download/Roms/<console>`, one folder per console (e.g. `Download/Roms/ps1`) |
| MAME arcade games (Area 51, CarnEvil…) | `Download/Roms/mame/<game>` (the game unzipped, with its `.chd`) |
| Wii U, PS3, PS Vita games | `Download/Roms/wiiu`, `Download/Roms/ps3`, `Download/Roms/vita` |
| BIOS (PS2, Saturn, Mega-CD…) | `Tremu/bios` (PS2: `Tremu/bios/pcsx2/bios`) |
| Xbox system files | `Tremu/Xbox/system` |
| Posters | `Tremu/Banner` (missing posters download by themselves) |

Without a cable: the bottom of the carousel shows "Add games: http://…" and a code. Open that
address from a PC or phone on the same Wi-Fi and drop your files; Tremu sorts them.
When Tremu reports a missing file (BIOS, hard disk), the message says which one and where it goes.

## 4. Companion apps

Tremu itself emulates arcade, NES, SNES, N64, Mega Drive, Saturn, PS1, PS2, PSP, Dreamcast,
GameCube, Wii, Xbox and Xbox 360. Four go through a separate app, which Tremu opens and which
returns to the carousel when the game ends:

| For | App | Source |
|---|---|---|
| PS3 | ARMSX3 (`com.armsx3`) | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) |
| Wii U | Cemu (`info.cemu.cemu`) | [proelitem/Cemu](https://github.com/proelitem/Cemu), branch `tremu` |
| PS Vita | Vita3K (`org.vita3k.emulator`) | [proelitem/Vita3K](https://github.com/proelitem/Vita3K), branch `tremu` |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr) |

You need **these builds**: the official ones do not receive the game picked in Tremu and do not
return to the carousel. If an official build is already installed (ARMSX3 especially),
**uninstall it first**: Android refuses to replace it with a differently signed build.
Without the app, the game's tile still shows and Tremu says which app to install.
PS3 firmware, PS Vita firmware (`.PUP`, in `Download/Roms/vita`) and Wii U keys are yours to provide.

## 5. WonderFitter gun

Turn the gun on: Tremu finds it over Bluetooth by itself, without the official WonderFitter app.
The gun accepts **only one app at a time**: close the official app if it is open. Mounting settings
(angle, offsets) are in Tremu's menu and are kept per gun. Without a gun, the Touch controllers aim.

Wiimote (Wii games): only through a Mayflash **DolphinBar** plugged into the headset over USB.

## 6. Playing together

Same Wi-Fi: one headset starts the game, then picks menu › Multiplayer › "Host (Wi-Fi)"; the other
sees a "Join: …" tile in the carousel. Both headsets need **the same Tremu version and the same
game file**. For MAME games the host restarts the game from the start and waits for player 2:
you begin the game together.

Over the Internet: "Host on the Internet" shows a code to give the other player (menu › Join with a
code). There is no paid server: if no code appears, open UDP port 47617 on your router (or turn on UPnP).

## Leaving and the menu

Left controller's menu button: the game's options. Held 1.5 s in a game started from Tremu
(PS3, Wii U, Vita, Time Crisis VR): back to the carousel.

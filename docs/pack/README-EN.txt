TREMU - FRIENDS PACK (version {VERSION})
========================================

Tremu turns a Meta Quest 3 or 3S into a shooting cabinet: your games are shown on a large
virtual screen, and you aim with the WonderFitter gun or the Touch controllers.

No games, BIOS, firmware or keys are included: use your own copies.

Full guide: https://github.com/proelitem/tremu/blob/main/docs/install/en.md


WHAT'S INSIDE
-------------

  1-Tremu\tremu-{VERSION}.apk       The app. The only one you need.
  2-Compagnes\ps3-armsx3.apk        For PS3 games.
  2-Compagnes\wiiu-cemu.apk         For Wii U games.
  2-Compagnes\psvita-vita3k.apk     For PS Vita games (experimental: slow).
  SHA256SUMS.txt                    To check the files are intact.

Tremu runs arcade, NES, Super Nintendo, N64, Mega Drive, Saturn, PS1, PS2, PSP, Dreamcast,
GameCube, Wii, Xbox and Xbox 360 by itself. Install a companion app only for the console
it is for.


STEPS
-----

1. Turn on developer mode: in the Meta Horizon phone app, open your headset, then
   "Headset settings" > "Developer mode" (Meta asks for a free developer organization).
2. Install SideQuest on your PC (https://sidequestvr.com, "Advanced Installer"), plug the
   headset in over USB and allow USB debugging in the headset.
3. Drag 1-Tremu\tremu-{VERSION}.apk onto the SideQuest window. If Tremu is listed on
   SideQuest, installing it from the listing gives the same file, with updates.
4. Companion apps (optional): FIRST uninstall any official ARMSX3, Cemu or Vita3K. The
   official versions don't receive the game picked in Tremu and don't return to the
   carousel. Then drag the APKs you want onto SideQuest. Tremu opens them itself.
5. In the headset: Library > Unknown sources > Tremu. Accept "All files access" and
   "Nearby devices (Bluetooth)".
6. Add games without a cable: the bottom of the carousel shows "Add games: http://..." and
   a code. Open it from a PC or phone on the same Wi-Fi and drop your files.
7. WonderFitter gun: switch it on and close the official WonderFitter app. Left
   controller menu button: game options. Held for 1.5 s in a PS3, Wii U or Vita game:
   back to the carousel.
8. Playing together: both headsets need THE SAME Tremu version and THE SAME game file.
   Same Wi-Fi: start the game, then menu > Multiplayer > "Host (Wi-Fi)"; the other headset
   sees a "Join: ..." tile. Over the Internet: "Host on the Internet" shows a code for the
   other player (menu > "Join with a code"); if the code doesn't come, open UDP port 47617
   or turn on UPnP.

If something goes wrong, tell me what you did and what you saw. For network play, send the
Tremu/netplay.log file from the headset.

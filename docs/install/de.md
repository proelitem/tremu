# Tremu installieren

[English](en.md) · **Deutsch** · [Español](es.md) · [Français](fr.md) · [日本語](ja.md) · [Русский](ru.md) · [中文](zh.md)

Tremu macht aus einer Meta Quest 3 oder 3S einen Schießautomaten: Die Spiele erscheinen auf einem
großen virtuellen Bildschirm, gezielt wird mit der WonderFitter-Pistole oder den Touch-Controllern.
Tremu liefert **keine Spiele, kein BIOS, keine Firmware und keine Schlüssel**: Du verwendest deine eigenen Kopien.

## 1. Headset vorbereiten

1. Aktiviere den **Entwicklermodus**: In der Meta-Horizon-App dein Headset öffnen, dann
   „Headset-Einstellungen“ › „Entwicklermodus“ (dafür braucht es eine kostenlose Meta-Entwicklerorganisation).
2. Installiere ein Sideload-Werkzeug auf dem PC: [Meta Quest Developer Hub](https://developers.meta.com/horizon/documentation/android-apps/meta-quest-developer-hub/)
   oder [SideQuest](https://sidequestvr.com/). Headset per USB anschließen und im Headset USB-Debugging erlauben.

## 2. Tremu installieren

Installiere `tremu.apk` mit dem Werkzeug (APK auf das Headset ziehen). Beim ersten Start fragt Tremu nach:

- **Zugriff auf alle Dateien**: erlauben. Tremu liest deine Spiele aus `Download/Roms` und legt
  seine Dateien im Ordner `Tremu` ab.
- **Geräte in der Nähe (Bluetooth)**: erlauben, für die WonderFitter-Pistole.

Tremu erscheint unter „Bibliothek“ › „Unbekannte Quellen“.

## 3. Spiele hinzufügen

| Was | Wo, auf dem Headset |
|---|---|
| Spiele | `Download/Roms/<Konsole>`, ein Ordner pro Konsole (z. B. `Download/Roms/ps1`) |
| MAME-Arcadespiele (Area 51, CarnEvil…) | `Download/Roms/mame/<Spiel>` (entpackt, mit seiner `.chd`) |
| Wii-U-, PS3-, PS-Vita-Spiele | `Download/Roms/wiiu`, `Download/Roms/ps3`, `Download/Roms/vita` |
| BIOS (PS2, Saturn, Mega-CD…) | `Tremu/bios` (PS2: `Tremu/bios/pcsx2/bios`) |
| Xbox-Systemdateien | `Tremu/Xbox/system` |
| Poster | `Tremu/Banner` (fehlende Poster werden selbst geladen) |

Ohne Kabel: Unten im Karussell steht „Spiele hinzufügen: http://…“ mit einem Code. Öffne diese
Adresse an einem PC oder Handy im selben WLAN und lege deine Dateien ab; Tremu sortiert sie.
Meldet Tremu eine fehlende Datei (BIOS, Festplatte), steht in der Meldung, welche und wohin.

## 4. Begleit-Apps

Tremu emuliert selbst Arcade, NES, SNES, N64, Mega Drive, Saturn, PS1, PS2, PSP, Dreamcast,
GameCube, Wii, Xbox und Xbox 360. Vier laufen über eine eigene App, die Tremu öffnet und die am
Spielende zum Karussell zurückkehrt:

| Für | App | Quelle |
|---|---|---|
| PS3 | ARMSX3 (`com.armsx3`) | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) |
| Wii U | Cemu (`info.cemu.cemu`) | [proelitem/Cemu](https://github.com/proelitem/Cemu), Branch `tremu` |
| PS Vita | Vita3K (`org.vita3k.emulator`) | [proelitem/Vita3K](https://github.com/proelitem/Vita3K), Branch `tremu` |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr) |

Es braucht **genau diese Versionen**: Die offiziellen erhalten das in Tremu gewählte Spiel nicht und
kehren nicht zum Karussell zurück. Ist eine offizielle Version schon installiert (vor allem ARMSX3),
**zuerst deinstallieren**: Android ersetzt sie nicht durch eine anders signierte Version.
Ohne die App wird die Kachel trotzdem angezeigt, und Tremu sagt, welche App fehlt.
PS3-Firmware, PS-Vita-Firmware (`.PUP`, in `Download/Roms/vita`) und Wii-U-Schlüssel bringst du selbst mit.

## 5. WonderFitter-Pistole

Pistole einschalten: Tremu findet sie selbst per Bluetooth, ohne die offizielle WonderFitter-App.
Die Pistole nimmt **nur eine App gleichzeitig** an: Schließe die offizielle App, falls sie offen ist.
Montage-Einstellungen (Winkel, Versatz) stehen im Tremu-Menü und werden pro Pistole gespeichert.
Ohne Pistole zielen die Touch-Controller.

Wiimote (Wii-Spiele): nur über eine Mayflash-**DolphinBar**, per USB am Headset.

## 6. Zusammen spielen

Selbes WLAN: Ein Headset startet das Spiel und wählt Menü › Mehrspieler › „Hosten (WLAN)“; das andere
sieht im Karussell eine Kachel „Beitreten: …“. Beide Headsets brauchen **dieselbe Tremu-Version und
dieselbe Spieldatei**. Bei MAME-Spielen startet der Host das Spiel neu und wartet auf Spieler 2:
Ihr beginnt gemeinsam.

Über das Internet: „Im Internet hosten“ zeigt einen Code für den anderen Spieler (Menü › Mit Code
beitreten). Es gibt keinen kostenpflichtigen Server: Erscheint kein Code, öffne UDP-Port 47617 am
Router (oder schalte UPnP ein).

## Beenden und Menü

Menütaste des linken Controllers: Optionen des Spiels. 1,5 s gedrückt in einem von Tremu gestarteten
Spiel (PS3, Wii U, Vita, Time Crisis VR): zurück zum Karussell.

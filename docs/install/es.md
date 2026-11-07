# Instalar Tremu

[English](en.md) · [Deutsch](de.md) · **Español** · [Français](fr.md) · [日本語](ja.md) · [Русский](ru.md) · [中文](zh.md)

Tremu convierte unas Meta Quest 3 o 3S en una máquina de disparos: los juegos se muestran en una gran
pantalla virtual y se apunta con la pistola WonderFitter o con los mandos Touch. Tremu **no incluye
juegos, BIOS, firmware ni claves**: usas tus propias copias.

## 1. Preparar el visor

1. Activa el **modo desarrollador**: en la app móvil Meta Horizon, abre tu visor y luego
   «Ajustes del visor» › «Modo desarrollador» (hace falta una organización de desarrollador de Meta, gratuita).
2. Instala una herramienta de instalación en tu PC: [Meta Quest Developer Hub](https://developers.meta.com/horizon/documentation/android-apps/meta-quest-developer-hub/)
   o [SideQuest](https://sidequestvr.com/). Conecta el visor por USB y acepta la depuración USB en el visor.

## 2. Instalar Tremu

Instala `tremu.apk` con la herramienta (arrastra el APK al visor). En el primer arranque, Tremu pide:

- **Acceso a todos los archivos**: acéptalo. Tremu lee tus juegos en `Download/Roms` y guarda sus
  archivos en la carpeta `Tremu`.
- **Dispositivos cercanos (Bluetooth)**: acéptalo, para la pistola WonderFitter.

Tremu aparece en «Biblioteca» › «Orígenes desconocidos».

## 3. Añadir tus juegos

| Qué | Dónde, en el visor |
|---|---|
| Juegos | `Download/Roms/<consola>`, una carpeta por consola (p. ej. `Download/Roms/ps1`) |
| Juegos arcade de MAME (Area 51, CarnEvil…) | `Download/Roms/mame/<juego>` (descomprimido, con su `.chd`) |
| Juegos de Wii U, PS3, PS Vita | `Download/Roms/wiiu`, `Download/Roms/ps3`, `Download/Roms/vita` |
| BIOS (PS2, Saturn, Mega-CD…) | `Tremu/bios` (PS2: `Tremu/bios/pcsx2/bios`) |
| Archivos de sistema de Xbox | `Tremu/Xbox/system` |
| Carátulas | `Tremu/Banner` (las que faltan se descargan solas) |

Sin cable: abajo en el carrusel aparece «Añadir juegos: http://…» y un código. Abre esa dirección
desde un PC o un móvil en la misma Wi-Fi y suelta tus archivos; Tremu los ordena.
Cuando Tremu avisa de un archivo que falta (BIOS, disco duro), el mensaje dice cuál y dónde va.

## 4. Aplicaciones complementarias

Tremu emula por sí mismo arcade, NES, SNES, N64, Mega Drive, Saturn, PS1, PS2, PSP, Dreamcast,
GameCube, Wii, Xbox y Xbox 360. Cuatro pasan por una aplicación aparte, que Tremu abre y que vuelve
al carrusel al terminar la partida:

| Para | Aplicación | Código fuente |
|---|---|---|
| PS3 | ARMSX3 (`com.armsx3`) | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) |
| Wii U | Cemu (`info.cemu.cemu`) | [proelitem/Cemu](https://github.com/proelitem/Cemu), rama `tremu` |
| PS Vita | Vita3K (`org.vita3k.emulator`) | [proelitem/Vita3K](https://github.com/proelitem/Vita3K), rama `tremu` |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr) |

Hacen falta **estas versiones**: las oficiales no reciben el juego elegido en Tremu y no vuelven al
carrusel. Si ya tienes una versión oficial (sobre todo ARMSX3), **desinstálala primero**: Android no
la reemplaza por una versión firmada de otra forma.
Sin la aplicación, la ficha del juego se muestra igual y Tremu dice cuál instalar.
El firmware de PS3, el de PS Vita (`.PUP`, en `Download/Roms/vita`) y las claves de Wii U los aportas tú.

## 5. Pistola WonderFitter

Enciende la pistola: Tremu la encuentra solo por Bluetooth, sin la app oficial de WonderFitter.
La pistola acepta **una sola aplicación a la vez**: cierra la app oficial si está abierta. Los ajustes
de montaje (ángulo, desplazamientos) están en el menú de Tremu y se guardan por pistola. Sin pistola,
apuntan los mandos Touch.

Wiimote (juegos de Wii): solo mediante una **DolphinBar** de Mayflash conectada por USB al visor.

## 6. Jugar juntos

Misma Wi-Fi: un visor inicia el juego y elige menú › Multijugador › «Hospedar (Wi-Fi)»; el otro ve una
ficha «Unirse: …» en el carrusel. Los dos visores necesitan **la misma versión de Tremu y el mismo
archivo de juego**. En los juegos de MAME, el anfitrión reinicia el juego desde cero y espera al
jugador 2: empezáis la partida juntos.

Por Internet: «Hospedar en Internet» muestra un código para el otro jugador (menú › Unirse con un
código). No hay ningún servidor de pago: si no aparece el código, abre el puerto UDP 47617 en tu
router (o activa UPnP).

## Salir y menú

Botón de menú del mando izquierdo: opciones del juego. Mantenido 1,5 s en un juego abierto desde
Tremu (PS3, Wii U, Vita, Time Crisis VR): vuelta al carrusel.

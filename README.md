# Le Chœur des Cendres

*Formerly Echoes of Ashes.* A PS1-style RPG (textured 3D terrain, 2D sprite characters) running on a round smartwatch: the Waveshare
ESP32-S3-Touch-AMOLED-1.75C (466 × 466 AMOLED). The same C99 engine also builds for the web (WebAssembly), for
Android (Wear OS watch and phone) and for RetroFW handhelds (LDK Game, 320 × 240).

This repository holds the engine, the watch firmware and the **safe edition** of the game world. Code comments and
most notes are in French.

## Play

Standalone builds of the safe edition are in the [release of 14 November 2026](https://github.com/Nerobeus/echoes-of-ashes/releases/tag/2026-11-14).
Nothing to install and no server: everything is inside the file.

- **PC**: [`le-choeur-des-cendres-pc-2026-11-14.html`](https://github.com/Nerobeus/echoes-of-ashes/releases/download/2026-11-14/le-choeur-des-cendres-pc-2026-11-14.html)
  (154 MB), a single web page to open in a recent Chrome, Edge, Firefox or Safari. Full screen and widescreen, with a
  PC-only renderer: 3D resolution from 854 × 480 to 1920 × 1080 (automatic by default: it follows the window and the speed
  of the PC), twice the view distance, depth of field, a 32-bit depth buffer. Keyboard keys can be changed in the Keys
  window (two keys per action, kept in the browser); the mouse clicks like a finger. H opens the help.
- **Android phone**: [`le-choeur-des-cendres-android-2026-11-14.apk`](https://github.com/Nerobeus/echoes-of-ashes/releases/download/2026-11-14/le-choeur-des-cendres-android-2026-11-14.apk)
  (264 MB, Android 9 or later).
- **Wear OS watch**: [`le-choeur-des-cendres-wearos-2026-11-14.apk`](https://github.com/Nerobeus/echoes-of-ashes/releases/download/2026-11-14/le-choeur-des-cendres-wearos-2026-11-14.apk)
  (261 MB, Wear OS 3 or later).
- **RetroFW 2.3 (LDK Game)**, experimental: [`le-choeur-des-cendres-retrofw-2026-11-14.ipk`](https://github.com/Nerobeus/echoes-of-ashes/releases/download/2026-11-14/le-choeur-des-cendres-retrofw-2026-11-14.ipk)
  (147 MB), to open from GMenuNX's file browser; or [`le-choeur-des-cendres-retrofw-2026-11-14.zip`](https://github.com/Nerobeus/echoes-of-ashes/releases/download/2026-11-14/le-choeur-des-cendres-retrofw-2026-11-14.zip)
  (137 MB), to unzip at the root of the console's card (`games/choeur_enfant/` and its menu entry). The game shows up
  in the Games section, full screen at 320 × 240. D-pad to run (hold Y to walk), A to talk and confirm, B to go back, START for
  the menu, L and R to turn the camera; in a vehicle, R accelerates and L brakes. SELECT + START held for one second quits.
  The menu entry asks for 600 MHz. Only tested on an LDK Game; the other JZ4760 handhelds that run RetroFW 2.3 should run it
  too: RS-97 (all versions, RS-97 Plus included, lines doubled on its 320 × 480 screen), RG-300 and RG-300 IPS, LDK Landscape,
  and, with the 320 × 240 image centred on their 480 × 272 screen, Retro Arcade Mini and PAP KIII Plus.

How to install the APKs on a phone or a watch: [Installing on Android](https://github.com/Nerobeus/echoes-of-ashes/wiki/Installing-on-Android).

## Screenshots

Rendered by the current engine (1 November 2026) at the watch's native resolution (466 × 466), interface included.
The LDK Game version, at 320 × 240 (shown twice as large), is further down.

<p align="center"><img src="screenshots/article/00_banniere.png" width="860" alt="Title screen, Lucerne at night, a race, a battle"></p>
<p align="center">
  <img src="screenshots/article/01_ecran_titre.png" width="210" alt="Title screen">
  <img src="screenshots/article/03_choix_heroine.png" width="210" alt="Choosing the heroine">
  <img src="screenshots/article/04_lucerne_jour.png" width="210" alt="A street of Lucerne">
  <img src="screenshots/article/05_lucerne_nuit.png" width="210" alt="Lucerne at night">
</p>
<p align="center">
  <img src="screenshots/article/06_phares.png" width="210" alt="Driving at night, headlights on">
  <img src="screenshots/article/07_boutique.png" width="210" alt="Inside a shop">
  <img src="screenshots/article/08_camp_des_friches.png" width="210" alt="A camp in the Wastes">
  <img src="screenshots/article/09_neige.png" width="210" alt="Snow on the peaks">
</p>
<p align="center">
  <img src="screenshots/article/10_pluie_de_cendre.png" width="210" alt="Ash rain in the city">
  <img src="screenshots/article/11_combat_entree.png" width="210" alt="Battle intro">
  <img src="screenshots/article/12_combat_commandes.png" width="210" alt="Battle: the command fan">
  <img src="screenshots/article/13_course_depart.png" width="210" alt="Race start">
</p>
<p align="center">
  <img src="screenshots/article/14_course_boost.png" width="210" alt="Racing on the neon circuit, boost">
  <img src="screenshots/article/15_menu.png" width="210" alt="The ring menu">
  <img src="screenshots/article/02_introduction.png" width="210" alt="Prologue card">
</p>

On the LDK Game (RetroFW), 11 November 2026:

<p align="center">
  <img src="screenshots/article-2026-11-11/02_friches_quete.png" width="420" alt="Exploring the Wastes, a new quest">
  <img src="screenshots/article-2026-11-11/03_lucerne_rue.png" width="420" alt="A street of Lucerne">
</p>
<p align="center">
  <img src="screenshots/article-2026-11-11/06_combat_commandes.png" width="420" alt="A battle">
  <img src="screenshots/article-2026-11-11/09_course_virage.png" width="420" alt="Racing on the neon circuit">
</p>

## Contents

| Folder | What |
|---|---|
| `engine/core` | the engine, without platform dependencies: world streaming, renderer (software, by bands), battles, story scripts, races, audio mixer, round UI |
| `engine/platform/esp32` | ESP-IDF 5.2.3 firmware for the watch (CO5300 display by bands, touch, SD card, ES8311 audio, Poké Ball Plus controller over BLE) |
| `engine/platform/web` | WebAssembly page |
| `engine/platform/android` | NativeActivity app (watch and phone), no Java besides a settings screen |
| `engine/platform/retrofw` | RetroFW 2.3 handhelds (LDK Game): a static MIPS32 binary without SDL (framebuffer, evdev, OSS), full screen 320 × 240 |
| `engine/tools` | build scripts (web, Android), world packaging, SD transfer, test renderer (`rendu_test.c`), balancing simulator |
| `engine/maquettes` | UI mock-ups |
| `engine/data/enfant` | the game world, safe edition (Git LFS) |
| `engine/data/musique.bin` | music (Git LFS) |
| `montre/` | the watch's home firmware (watch face, notifications, app launcher) |
| `commun/` | UI code shared by the watch firmwares |

The world itself (terrain, buildings, characters, story) is produced by a separate content tool that is not part of
this repository; `engine/data` only holds its output.

## Getting the data

The world files are stored with Git LFS (the world is 225 MB):

```
git lfs install
git clone https://github.com/Nerobeus/echoes-of-ashes.git
```

## Building

All builds use the safe edition through their `--enfant` / `_enfant` variants:

- **Web**: `node engine/tools/build_web.js --enfant` (C compiled with Zig: `python -m ziglang cc`) gives
  `engine/build/moteur_enfant.html`; `--pc --enfant` gives the standalone PC page, `engine/build/choeur_pc_enfant.html`
  (rectangular screen, `ENG_RECT` with `ENG_PREMIUM`: resolution chosen at run time, view distance doubled, depth of field
  and 32-bit depth; `engine/tools/verifier_pc.js` checks it in a headless Chrome).
- **Watch**: ESP-IDF 5.2.3, `engine/platform/esp32/idf_enfant.cmd build`; the world goes on the SD card
  (`/sd/echoes_enfant/`), the light package (`monde_esp32.pak`) in flash. The watch uses a partition table shared with
  other apps (`montre/partitions.csv`) and is flashed with `montre/flasher.py`.
- **Android**: `node engine/tools/build_android.js --telephone --enfant --autonome` for the phone, `--montre` instead of
  `--telephone` for a Wear OS watch (Android NDK, no Gradle; `--autonome`: world and music inside the APK).
- **RetroFW**: `node engine/tools/build_retrofw.js --enfant` (Zig, `mipsel-linux-musleabihf -mcpu=mips32`, static) gives
  `engine/build/retrofw/choeur_enfant.ipk` and `engine/build/retrofw/carte_sd/` to copy at the root of the card; `--simu`
  also builds a PC simulator that writes the screen to BMP files from a script of button presses.

## Licence

© 2026 Nerobeus. All rights reserved: the code and the game content are published for reading and personal play;
no licence to reuse, modify or redistribute them is granted. Third-party assets inside the world and the music keep
their own licences, listed in [THIRD_PARTY.md](THIRD_PARTY.md).

Related: [pokeball-plus-re](https://github.com/Nerobeus/pokeball-plus-re), how the Poké Ball Plus is used as the
watch's controller.

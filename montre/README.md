# App montre (accueil de la montre 1,75" C)

Troisième firmware de la montre Waveshare ESP32-S3-Touch-AMOLED-1.75C, à côté d'Echoes of Ashes (`engine/`), du
PC DOS (`pcdos/`) et du Game Boy (`gameboy/`). C'est lui qui démarre : cadran, notifications et appels du téléphone en
Bluetooth, réglages, stockage USB, et lanceur des autres applications.

## Partitions (table commune : `partitions.csv`, la même dans `engine/platform/esp32`, `pcdos/esp` et `gameboy`)

| Partition | Adresse | Taille | Contenu |
|---|---|---|---|
| nvs, otadata, phy | 0x9000 | 36 Ko | réglages, appairages Bluetooth, application de démarrage |
| `montre` (ota_0) | 0x20000 | 1,25 Mo | cette app (2 Mo avant le 31/10/2026) |
| `echoes` (ota_1) | 0x160000 | 2,5 Mo | le jeu (3 Mo avant le 31/10/2026) |
| `pcdos` (ota_2) | 0x3E0000 | 1,5 Mo | tiny386 (3 Mo avant le 31/10/2026) |
| `gameboy` (ota_3) | 0x560000 | 2,75 Mo | consoles (`gameboy/` : Game Boy, NES, Master System, Game Gear) |
| `monde` | 0x820000 | 7 Mo | paquet léger du jeu (`engine/data/monde_esp32.pak`) |
| `bios.bin`, `vgabios.bin` | 0xF20000 | 192 Ko | BIOS du PC |

On passe d'une application à l'autre par `esp_ota_set_boot_partition` puis un redémarrage (`apps.c`). Echoes (ligne
MONTRE du menu SYSTÈME, rangée MONTRE du menu des applications), le PC DOS (touche MONTRE, PWR tenu) et le Game Boy
(MONTRE de son menu, PWR tenu) reviennent ici.

## Pages et gestes

Glisser à gauche ou à droite : NOTIFICATIONS · CADRAN · APPS · RÉGLAGES. Toucher : agir. Le doigt ne rallume pas
l'écran ni une notification ni un appel (bandeau seulement écran allumé). PWR ou BOOT l'éteignent d'un appui court ; pour
l'allumer, il faut les tenir 0,1 s (`REVEIL_TENU_MS` ; 0,3 s jugé trop long ; la veille profonde, elle, se réveille à
tout appui de PWR). Chaque réveil revient au cadran, dessiné à l'heure et envoyé avant l'affichage (`lcd_sortir_veille`, puis
`lcd_afficher` : plus d'image d'avant la veille). Rallumée par un bouton et pas touchée, elle se rendort après
5 s (`REVEIL_BREF_S`, le temps de lire l'heure) ; le premier toucher rend la veille des RÉGLAGES. Interface commune avec le menu du PC DOS : `commun/ui_montre/` (dessin, polices, thème, et `pages.c` : pages
glissées, toucher, appui long, glissé vertical des listes), compilée dans les deux projets. Pendant un glissé, chaque
page est dessinée une fois dans une image en PSRAM puis recopiée décalée à chaque pas (redessiner une page coûtait
24 ms par image : glissés saccadés) ; doigt posé, lu toutes les 2 ms.

- CADRAN : heure, date, arc des secondes, batterie, état du Bluetooth, nombre de notifications (toucher : la liste).
- NOTIFICATIONS : les 20 dernières (aussi dans `/sd/montre/notifs.txt`) ; toucher : le détail, EFFACER (aussi sur le
  téléphone), RETOUR ; TOUT EFFACER.
- APPS : ECHOES OF ASHES, PC DOS, CONSOLES, STOCKAGE USB, RÉGLAGES. CONSOLES : la console (GAME BOY, NES, MASTER
  SYSTEM, GAME GEAR, DOOM), puis toutes ses ROM (`/sd/gameboy` : `.gb`, `.gbc` ; `/sd/nes` : `.nes` ; `/sd/sms` : `.sms`,
  `.sg` ; `/sd/gamegear` : `.gg` ; `/sd/doom` : `.wad` ; sans limite : liste en PSRAM, noms longs), le dernier jeu joué en tête, REPRISE
  quand un état l'attend (cherché page par page) ; cinq rangées, glissé vertical pour la suite ; RETOUR revient au choix
  de la console ; le chemin du jeu choisi va dans `/sd/gameboy/lancer.txt` (voir `gameboy/README.md`).
- RÉGLAGES : teinte, volume, luminosité, veille (15 s à 5 min, jamais), Bluetooth, BT EN VEILLE (non par défaut :
  Bluetooth gardé écran éteint, pour les notifications), oublier le téléphone ; moitié gauche d'une rangée = moins,
  droite = plus. Volume, luminosité et veille dans `/sd/echoes/montre.txt` (partagé avec Echoes et le PC
  DOS), le Bluetooth en NVS.
- Appel entrant : RÉPONDRE / REFUSER (envoyés au téléphone), RACCROCHER pendant l'appel.

## Bluetooth

Protocole avec l'app Android « Echoes Montre » : `PROTOCOLE.md` (`main/ble.c`). NimBLE en rôle périphérique, pile en
PSRAM, appairage « just works » avec liaison gardée ; diffusion lente (1 s) écran éteint, rapide (100 ms) écran allumé.

## Énergie

**Veille** (écran éteint par PWR, BOOT ou l'inactivité) : sans BT EN VEILLE, Bluetooth coupé puis **veille profonde**
(`dormir_profond`) : réglages écrits, AMOLED en SLPIN avec son reset tenu, reset du tactile tenu, amplificateur tenu
coupé (GPIO46), réveil par **PWR seulement** (BOOT est une broche d'amorçage : tenu au réveil, il mettrait la puce en
mode téléchargement), puis redémarrage sur le cadran (environ une demi-seconde). Toutes les autres sources de réveil sont
retirées avant (`esp_sleep_disable_wakeup_source`) : la veille légère automatique laissait armée la minuterie de sa
dernière attente, et jusqu'au 02/11/2026 la montre se réveillait quelques secondes après chaque veille profonde, écran
allumé pour toute la veille réglée, en boucle ; un réveil qui ne vient pas de PWR la rendort sans allumer l'écran. Restent alimentés : la puce en veille
profonde (RTC et ext0), l'AMOLED en veille, le tactile CST9217 (pas de commande de veille envoyée), la carte microSD au
repos, le codec ES8311 non initialisé, l'IMU non initialisée, l'AXP2101. Câble USB d'un PC branché : pas de veille
profonde (le port série reste), écran éteint et Bluetooth coupé. Avec BT EN VEILLE : l'ancienne veille légère
automatique ci-dessous, Bluetooth en veille de modem.

**Après la veille d'Echoes, du PC DOS ou du Game Boy** (drapeau `veille` de l'espace NVS `apps`, `app_veille` / `app_drapeau`
d'`engine/platform/esp32/main/apps.c`) : l'app démarre sans initialiser l'écran (rien ne s'affiche), puis veille
profonde (ou veille légère avec BT EN VEILLE) ; l'écran n'est initialisé qu'au premier allumage.

Fréquence dynamique 40 à 160 MHz et veille légère automatique (`CONFIG_PM_ENABLE`, tickless idle) : la tâche attend
40 ms (écran allumé) ou 250 ms (éteint) entre deux tours ; le cadran est redessiné une fois par seconde, les autres
pages seulement quand quelque chose change ; Bluetooth en veille de modem, horloge lente sur l'oscillateur interne (pas
de quartz 32 kHz sur la 1,75" C ; Espressif le dit trop imprécis pour une connexion, mais le quartz principal gardé
allumé en veille légère consomme davantage : écarté le 02/11/2026, l'économie d'énergie passe d'abord). Journal de
diagnostic provisoire : `/sd/montre/journal.txt` (démarrages et leur raison, allumages de l'écran, liens du téléphone
et raison de chaque déconnexion). Consommation à mesurer.

## Stockage USB

APPS › STOCKAGE USB : la montre redémarre dans ce mode (`usb.c`, TinyUSB MSC, composant `espressif/esp_tinyusb`) ; le
PC voit la carte SD comme une clé ; la console USB est coupée pendant ce temps. QUITTER, ou le PC qui lâche la carte,
ramène à l'accueil : la clé est déconnectée (`tud_disconnect`), le PHY rendu à la console et D+ laissé sans tirage
300 ms (`usb_console`) pour que le PC voie un débranchement, puis redémarrage : le port série (COM8) revient tout seul
(avant, Windows gardait un « stockage de masse » en erreur jusqu'à une extinction).

## Compiler et flasher

```bat
cmd /c "D:\Documents\ESP32\montre\idf.cmd build"
```

Flash par nom de partition (dans l'environnement de `idf.cmd`, ou avec un Python qui a esptool) :

```bat
python montre\flasher.py COM8 tout            :: chargeur, table, otadata, montre, echoes, pcdos, gameboy, monde, BIOS
python montre\flasher.py COM8 montre          :: une app seulement (montre, echoes, pcdos, gameboy, monde, bios, chargeur)
```

Ne plus utiliser `idf.py flash` ni `app-flash` dans `engine/platform/esp32`, `pcdos/esp` ou `gameboy` : ils
écriraient dans la première partition d'application, `montre`. Le flasheur refuse une image plus grande que sa
partition (le contrôle d'ESP-IDF ne le fait que si elle ne tient dans aucune). Après un changement de la table, flasher
aussi `chargeur` (table et otadata, depuis ce projet recompilé).

Polices : `outils/police.py` (Oxanium du jeu, SIL OFL 1.1) → `main/police.c` et `police.h`.

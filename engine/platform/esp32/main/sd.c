/*
 * Carte microSD (SDMMC 1 bit, FAT) pour les sauvegardes et l'histoire. Le moteur alterne lui-même les deux emplacements
 * de sauvegarde (numéro de séquence et somme de contrôle) : une coupure pendant une écriture laisse l'autre intact.
 * L'histoire (/sd/echoes/histoire.scn, copiée par engine/tools/maj_monde.js) reste ouverte : le moteur y lit à mesure
 * le code des événements et les répliques, sans limite de mémoire. Les musiques (/sd/echoes/musique.bin) aussi : le
 * moteur en lit les blocs à mesure (engine/core/music.c). Le monde aussi (/sd/echoes/monde.pak) : avec le paquet léger de
 * la flash, le moteur y lit chunks, planches et textures du sol à la demande (engine/tools/paquet_sd.js).
 * Broches : menuconfig > Echoes of Ashes (valeurs du projet tiny386, à vérifier sur la carte 1,75").
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "sd.h"

static const char *TAG = "sd";
static int ok;
static sdmmc_card_t *carte;

/* Carte retirée puis remise (copie par l'USB, sdx.c) : démontée et remontée ; les fichiers ouverts par le jeu ne
   valent plus rien, la carte doit redémarrer ensuite */
int sd_remonter(void) {
    if (carte) esp_vfs_fat_sdcard_unmount("/sd", carte);
    carte = NULL; ok = 0;
    return sd_init();
}

/* Fréquence demandée (kHz) et phase d'échantillonnage : menuconfig, ou SDX BANC (mesures depuis le PC) */
#if CONFIG_ECHOES_SD_RAPIDE
static int sd_khz = SDMMC_FREQ_HIGHSPEED, sd_phase;
#else
static int sd_khz = SDMMC_FREQ_DEFAULT, sd_phase;
#endif
int sd_remonter_a(int khz, int phase) { sd_khz = khz; sd_phase = phase; return sd_remonter(); }
int sd_freq_reelle(void) { return carte ? carte->real_freq_khz : 0; }
int sd_bus_bits(void) { return carte ? 1 << carte->log_bus_width : 0; }
/* Lecture brute de secteurs (banc : le pilote sans FATFS) ; 0 si tout va bien */
int sd_secteurs(void *dst, uint32_t debut, uint32_t n) { return carte && sdmmc_read_sectors(carte, dst, debut, n) == ESP_OK ? 0 : -1; }

int sd_init(void) {
    if (CONFIG_ECHOES_SD_CLK < 0) { ESP_LOGW(TAG, "carte sans lecteur SD : sauvegarde désactivée"); return 0; }
    esp_vfs_fat_sdmmc_mount_config_t mount = { .format_if_mount_failed = false, .max_files = 6, .allocation_unit_size = 16 * 1024 };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = CONFIG_ECHOES_SD_CLK; slot.cmd = CONFIG_ECHOES_SD_CMD; slot.d0 = CONFIG_ECHOES_SD_D0;
    if (CONFIG_ECHOES_SD_D1 >= 0 && CONFIG_ECHOES_SD_D2 >= 0 && CONFIG_ECHOES_SD_D3 >= 0) {   /* bus de 4 bits */
        slot.width = 4;
        slot.d1 = CONFIG_ECHOES_SD_D1; slot.d2 = CONFIG_ECHOES_SD_D2; slot.d3 = CONFIG_ECHOES_SD_D3;
    }
    host.max_freq_khz = sd_khz;
    host.input_delay_phase = sd_phase;   /* échantillonnage des données (haute vitesse seulement) */
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    sdmmc_card_t *card;
    esp_err_t e = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mount, &card);
    if (e != ESP_OK) { ESP_LOGW(TAG, "pas de carte SD (%s) : sauvegarde désactivée", esp_err_to_name(e)); return 0; }
    mkdir(SD_DIR, 0775);
    ok = 1; carte = card;
    ESP_LOGI(TAG, "carte SD montée : %llu Mo, %d bit%s à %d kHz", (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024),
             slot.width, slot.width > 1 ? "s" : "", card->real_freq_khz);
    return 1;
}

static void path(int slot, char *p, int n) { snprintf(p, n, SD_DIR "/sauve%d.bin", slot); }   /* parties : sauve0 à sauve5 (partie × 2 + séquence & 1) */

int sd_save_write(int slot, const void *data, int len) {
    if (!ok) return -1;
    char p[48]; path(slot, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    int w = (int)fwrite(data, 1, len, f);
    fflush(f); fsync(fileno(f));
    fclose(f);
    return w;
}

/* Histoire : taille du fichier (0 : pas de carte ou pas de fichier, le moteur prend alors la section SCEN du paquet) */
static FILE *scn;
uint32_t sd_scen_open(void) {
    if (!ok || !(scn = fopen(SD_DIR "/histoire.scn", "rb"))) return 0;
    if (fseek(scn, 0, SEEK_END)) { fclose(scn); scn = NULL; return 0; }
    const long n = ftell(scn);
    if (n <= 0) { fclose(scn); scn = NULL; return 0; }
    ESP_LOGI(TAG, "histoire : " SD_DIR "/histoire.scn, %ld octets", n);
    return (uint32_t)n;
}
int sd_scen_read(uint32_t off, void *dst, uint32_t len) {
    if (!scn || fseek(scn, (long)off, SEEK_SET)) return -1;
    return fread(dst, 1, len, scn) == len ? 0 : -1;
}

/* Intérieurs des bâtiments : taille du fichier (0 : le moteur prend la section INTR du paquet) ; lus à l'entrée */
static FILE *intr;
uint32_t sd_intr_open(void) {
    if (!ok || !(intr = fopen(SD_DIR "/interieurs.bin", "rb"))) return 0;
    if (fseek(intr, 0, SEEK_END)) { fclose(intr); intr = NULL; return 0; }
    const long n = ftell(intr);
    if (n <= 0) { fclose(intr); intr = NULL; return 0; }
    ESP_LOGI(TAG, "intérieurs : " SD_DIR "/interieurs.bin, %ld Ko", n / 1024);
    return (uint32_t)n;
}
int sd_intr_read(uint32_t off, void *dst, uint32_t len) {
    if (!intr || fseek(intr, (long)off, SEEK_SET)) return -1;
    return fread(dst, 1, len, intr) == len ? 0 : -1;
}

/* Monde entier (monde.pak) : taille du fichier (0 : absent) ; lu par le cœur 0 (préparation des images) */
static FILE *monde;
uint32_t sd_pak_open(void) {
    if (!ok || !(monde = fopen(SD_DIR "/monde.pak", "rb"))) return 0;
    if (fseek(monde, 0, SEEK_END)) { fclose(monde); monde = NULL; return 0; }
    const long n = ftell(monde);
    if (n <= 0) { fclose(monde); monde = NULL; return 0; }
    ESP_LOGI(TAG, "monde : " SD_DIR "/monde.pak, %ld Mo", n / (1024 * 1024));
    return (uint32_t)n;
}
int sd_pak_read(uint32_t off, void *dst, uint32_t len) {
    if (!monde || fseek(monde, (long)off, SEEK_SET)) return -1;
    return fread(dst, 1, len, monde) == len ? 0 : -1;
}

/* Musiques : taille du fichier (0 : pas de musique) */
static FILE *mus;
uint32_t sd_music_open(void) {
    if (!ok || !(mus = fopen(SD_DIR "/musique.bin", "rb"))) return 0;
    if (fseek(mus, 0, SEEK_END)) { fclose(mus); mus = NULL; return 0; }
    const long n = ftell(mus);
    if (n <= 0) { fclose(mus); mus = NULL; return 0; }
    ESP_LOGI(TAG, "musiques : " SD_DIR "/musique.bin, %ld Ko", n / 1024);
    return (uint32_t)n;
}
int sd_music_read(uint32_t off, void *dst, uint32_t len) {
    if (!mus || fseek(mus, (long)off, SEEK_SET)) return -1;
    return fread(dst, 1, len, mus) == len ? 0 : -1;
}

int sd_save_read(int slot, void *data, int cap) {
    if (!ok) return -1;
    char p[48]; path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    int r = (int)fread(data, 1, cap, f);
    fclose(f);
    return r;
}

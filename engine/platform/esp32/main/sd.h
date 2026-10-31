#ifndef SD_H
#define SD_H

#include <stdint.h>
#include "sdkconfig.h"

/* Dossier du jeu sur la carte : un par édition (menuconfig > Echoes of Ashes > Édition enfant ; idf_enfant.cmd), pour
   que l'édition enfant ne lise jamais l'histoire, les intérieurs ni les sauvegardes de l'édition 18+ */
#ifdef CONFIG_ECHOES_ENFANT
#define SD_DIR "/sd/echoes_enfant"
#else
#define SD_DIR "/sd/echoes"
#endif

/* Carte microSD en SDMMC 1 bit, montée sur /sd ; sauvegardes dans /sd/echoes/sauve0.bin et sauve1.bin, histoire dans
   /sd/echoes/histoire.scn */
int sd_init(void);
/* Démonter et remonter la carte (retirée puis remise) : 1 si elle est de nouveau là ; redémarrer ensuite */
int sd_remonter(void);
/* Remonter à une autre fréquence (kHz) et phase d'échantillonnage (0 à 3) ; fréquence obtenue (kHz, 0 sans carte) */
int sd_remonter_a(int khz, int phase);
int sd_freq_reelle(void);
int sd_bus_bits(void);
int sd_secteurs(void *dst, uint32_t debut, uint32_t n);
int sd_save_write(int slot, const void *data, int len);
int sd_save_read(int slot, void *data, int cap);
/* Histoire sur la carte : taille du fichier (0 s'il n'y est pas), puis lectures par morceaux (0 si tout va bien) */
uint32_t sd_scen_open(void);
int sd_scen_read(uint32_t off, void *dst, uint32_t len);
uint32_t sd_intr_open(void);
int sd_intr_read(uint32_t off, void *dst, uint32_t len);
/* Monde entier sur la carte (/sd/echoes/monde.pak), avec le paquet léger de la flash : même principe */
uint32_t sd_pak_open(void);
int sd_pak_read(uint32_t off, void *dst, uint32_t len);
/* Musiques sur la carte (/sd/echoes/musique.bin, tools/audio/preparer_musique.py) : même principe */
uint32_t sd_music_open(void);
int sd_music_read(uint32_t off, void *dst, uint32_t len);

#endif

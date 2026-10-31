/* Réglages de la montre (volume, luminosité, veille), gardés dans <dossier du jeu>/montre.txt de la carte SD, lus aussi
   par le PC DOS ; page RÉGLAGES du menu des applications (eng_config_t.regls) */
#ifndef REGLAGES_H
#define REGLAGES_H

#include "eng.h"

/* Au démarrage, carte montée : lecture du fichier (valeurs de menuconfig sinon) */
void reglages_lire(void);
int reglages_volume(void);          /* % */
int reglages_luminosite(void);      /* commande de l'écran (0 à 255) */
/* Réglages pour la page du moteur */
extern const eng_reglage_t reglages_montre[];
extern const int reglages_nombre;
extern const eng_reglage_t reglages_jeu[];   /* montrés aussi en jeu, en bas du menu SYSTÈME : MANETTE */
extern const int reglages_jeu_nombre;
/* Boucle du jeu, à chaque image une fois le son du démarrage rendu : volume changé ailleurs (cadran SON du jeu),
   luminosité (partie chargée), et écriture du fichier 2 s après le dernier changement */
void reglages_suivre(int64_t now);
/* Luminosité demandée par le moteur (eng_config_t.set_brightness) : notée comme réglage */
void reglages_luminosite_vue(int level);
/* Manette Bluetooth voulue (Poké Ball Plus, manette.c ; page RÉGLAGES, gardé en NVS) */
bool reglages_manette(void);
/* Manette coupée dans le réglage (mémoire qui manque au jeu avec elle : main.c) */
void reglages_sans_manette(void);

#endif

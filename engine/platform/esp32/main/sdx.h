#ifndef SDX_H
#define SDX_H

/* Copie vers la carte SD par l'USB (engine/tools/envoi_sd.py) : tâche de réception, démarrée après sd_init (même sans
   paquet ni carte : elle répond alors SDX SANS-CARTE) */
void sdx_start(int carte);
/* Boucle du jeu : 1 tant qu'une copie demande la pause (le jeu n'avance plus et ne lit plus la carte) ; appelée au
   début de chaque image, elle signale aussi que le jeu tourne */
int sdx_pause(void);
/* Entrée du manifeste oubliée (fichier remplacé par le mode Transfert : envoi_sd.py le renverra au besoin) */
void sdx_oublier(const char *nom);
/* Essais depuis le PC, pris par la boucle du jeu : SDX COURSE k a (course d'essai pilotée sur la piste k, a = 0 sans
   l'allègement du rendu : eng_essai_course) ; SDX ECRAN (capture de l'image présentée). 1 : une demande, une fois */
int sdx_course(int *piste, int *allege);
int sdx_essai(float *v);   /* SDX ESSAI x z cap pluie heure s : 1 et les six valeurs, une fois */
int sdx_ecran(void);
int sdx_xp(void);    /* essais de mesure (SDX XP n, firmware -DECHOES_PROFIL=1 seulement) : bits de main.c et render.c */

#endif

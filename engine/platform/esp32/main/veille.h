#ifndef VEILLE_H
#define VEILLE_H

#include <stdint.h>

/* finir : attend que l'image en cours de dessin soit à l'écran (main.c) ; lum : luminosité réglée, rendue au réveil ;
   demander : luminosité à appliquer entre deux images (atténuation avant la veille) */
void veille_init(void (*finir)(void), uint8_t *lum, void (*demander)(int));
/* À chaque image (tâche du jeu, entrées lues) : boot appuyé, doigt posé. Met la montre en veille s'il le faut et ne
   rend la main qu'au réveil (le moteur est alors verrouillé) : 1 dans ce cas, 0 sinon */
int veille_image(int boot, int doigt);
/* Délai de la veille automatique, en secondes (0 : jamais) ; réglage de la montre */
void veille_delai(int s);
/* Veille demandée par une autre tâche (SDX VEILLE, essais depuis le PC) : prise à l'image suivante */
void veille_demander(void);
/* 1 si BOOT est tenu depuis plus d'une seconde (son relâchement n'ouvre pas le menu) */
int veille_boot_long(void);

#endif

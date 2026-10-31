#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>

void touch_init(void);
/* Lit les doigts posés (deux au plus), en pixels natifs de l'écran : xy = x1, y1, x2, y2 ; renvoie leur nombre */
int touch_read(int xy[4]);
/* Comme touch_read, mais -1 quand le contrôleur n'a pas de trame nouvelle (doigt immobile entre deux rapports) : 0 est
   alors un vrai relâchement */
int touch_etat(int xy[4]);
/* 1 si la broche INT du tactile est basse (un doigt vient d'être posé : réveil de la veille) */
int touch_int_bas(void);

#endif

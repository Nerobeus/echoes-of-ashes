#ifndef HORLOGE_H
#define HORLOGE_H

#include <time.h>

/* Horloge PCF85063 (heure locale) : au démarrage, après board_power_init ; 1 si l'heure est connue (le système la prend) */
int horloge_init(void);
int horloge_lire(struct tm *t);
int horloge_ecrire(const struct tm *t);
/* Règle l'horloge et le système (SDX HEURE, depuis le PC) ; 1 si c'est fait */
int horloge_regler(const struct tm *t);
/* 1 si l'heure du système vient de l'horloge (lue ou réglée) */
int horloge_valide(void);
/* Avant esp_restart : l'heure du système gardée en mémoire RTC, reprise au démarrage suivant (sans PCF85063) */
void horloge_garder(void);

#endif

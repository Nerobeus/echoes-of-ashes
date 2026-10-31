/* Manette Bluetooth de la montre : la Poké Ball Plus de Nintendo (BLE), commune au Game Boy et à Echoes. L'ESP32-S3 n'a
   que le Bluetooth basse consommation : les manettes en Bluetooth classique (les 8BitDo, d'après Bluepad32) ne sont pas
   à sa portée. État brut ici ; chaque application en fait ses boutons */
#ifndef MANETTE_H
#define MANETTE_H

#include <stdbool.h>
#include <stdint.h>

#define MANETTE_HAUT 1       /* bouton du haut */
#define MANETTE_CLIC 2       /* clic du stick */
typedef struct { int boutons, x, y; } manette_t;   /* stick : x et y de -100 à 100 (droite et bas positifs) */

/* NimBLE en central : recherche de « Pokemon PBP », connexion, notifications ; 0 si la pile ne démarre pas (mémoire) */
bool manette_demarrer(void);
void manette_arreter(void);
bool manette_active(void);
bool manette_connectee(void);
manette_t manette_etat(void);      /* tout à zéro sans manette */
int manette_batterie(void);        /* niveau de la batterie (%), -1 : inconnu ou pas connectée */
int64_t manette_vue(void);         /* dernier changement (µs) : les veilles automatiques en tiennent compte */
/* Mesure (console) : trames reçues et plus grand écart entre deux (ms) depuis l'appel précédent */
void manette_mesure(int *trames, int *ecart_ms);
/* Effet de la balle (lumière et vibration), mêmes numéros que les ENG_EFFET_* du moteur ; rien sans balle connectée, un
   toutes les 0,4 s au plus. Effets sur mesure (activités 70 à 79, tools/pokeball/pbp_effets.py) si la balle les a,
   sinon sa légère vibration (activité 13, sans lumière ; rien pour le soin et le niveau) */
enum { MANETTE_VALIDER, MANETTE_COUP, MANETTE_KO, MANETTE_CRASH, MANETTE_COMBAT, MANETTE_VICTOIRE, MANETTE_SOIN,
       MANETTE_NIVEAU, MANETTE_DEFAITE, MANETTE_FRAPPE, MANETTE_EFFETS };
void manette_effet(int effet);
/* Lumière d'ambiance (activité 100 : cyan de l'interface pendant 10 s), relancée par manette_tick à la fin de chaque
   effet et toutes les 8 s ; éteinte (activité 101) par manette_arreter ; seulement avec les effets sur mesure. Echoes
   l'allume ; manette_tick à chaque image */
void manette_ambiance(bool on);
void manette_tick(void);

#endif

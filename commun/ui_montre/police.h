/* Polices bitmap de l'app montre (outils/police.py) : Oxanium et Share Tech Mono (SIL OFL 1.1), 4 bits par pixel */
#ifndef POLICE_H
#define POLICE_H
#include <stdint.h>

typedef struct { uint16_t code; uint8_t w, h; int8_t dx, dy; uint8_t av; uint32_t off; } glyphe_t;
typedef struct { const glyphe_t *g; int n; const uint8_t *px; uint8_t hauteur, base; } police_t;

extern const police_t P_TEXTE;   /* Oxanium-400.ttf, 18 px */
extern const police_t P_TITRE;   /* Oxanium-600.ttf, 26 px */
extern const police_t P_HEURE;   /* Oxanium-600.ttf, 72 px */
extern const police_t P_RANG;   /* Oxanium-600.ttf, 22 px */
extern const police_t P_GRAND;   /* Oxanium-600.ttf, 30 px */
extern const police_t P_MONO;   /* ShareTechMono-400.ttf, 18 px */

#endif

/* Thème de l'app montre : celui des menus d'Echoes of Ashes (engine/core/hud.c et accueil.c) — fond d'encre, rangées à
   coins coupés, titres Oxanium, textes Share Tech Mono, teintes NÉON, ROUILLE, TOXIQUE, ULTRAVIOLET, GLACE — et le cadran
   de la montre verrouillée d'Echoes (chiffres sept segments) */
#ifndef THEME_H
#define THEME_H

#include <stdbool.h>
#include <time.h>
#include "dessin.h"

#define T_INK     RGB(5, 4, 10)        /* 0x05040A */
#define T_NUIT    th_nuit              /* 0x0D1428 (montre) ; PC DOS : couleur de fond de son thème */
#define T_PANNEAU th_panneau_c         /* 0x060E1A (montre) */
extern uint16_t th_nuit, th_panneau_c;
#define T_BLANC   RGB(232, 246, 255)   /* 0xE8F6FF */
#define T_DIM     RGB(154, 166, 184)   /* 0x9AA6B8 */
#define T_GRIS    RGB(127, 147, 168)   /* 0x7F93A8 */
#define T_ETEINT  RGB(58, 74, 92)      /* 0x3A4A5C */

extern uint16_t T1, T2;   /* teinte choisie : couleur principale et seconde (choix, alerte) */
int teintes_nb(void);
const char *teinte_nom(int k);
void teinte_choisir(int k);
/* Couleurs d'une autre interface (PC DOS : son thème de cadre) : teinte, seconde teinte, disque de nuit, panneaux */
void th_couleurs(uint16_t t1, uint16_t t2, uint16_t nuit, uint16_t panneau);

/* Texte posé sur sa ligne de base (comme ui_text d'Echoes) */
int th_texte(const police_t *p, int x, int base, const char *s, uint16_t c, int align);
/* Fond des pages : encre, disque de nuit, anneau de la teinte */
void th_fond(void);
/* Titre (Oxanium 30) et sous-titre gris, lignes de base y et y + 26 */
void th_titre(int y, const char *titre, const char *sous);
/* Demi-largeur d'une rangée de hauteur h en y, dans le cercle (marge 40 comme Echoes) */
int th_demi(int y, int h);
/* Panneau à coins coupés (haut gauche et bas droit) : choisi = seconde teinte, fond plus vif, contour épais */
void th_panneau(int x, int y, int w, int h, uint16_t col, bool choisi);
/* Rangée centrée de hauteur h en y : libellé (Oxanium 22) à gauche, valeur (mono) à droite, ou libellé centré */
void th_rangee(int y, int h, const char *lab, const char *val, bool choisi, uint16_t lc);
/* Bouton retour hexagonal en bas (233, 428) */
void th_retour(bool choisi);
/* Repère des pages */
void th_points(int p, int n);

typedef struct {
    struct tm l;
    bool connue;           /* heure réglée */
    int batterie, usb;     /* % (-1 : inconnue), câble branché */
    int nnotifs;
    bool bt_on, bt_lien;
} cadran_info_t;
/* Cadran : celui de la montre verrouillée d'Echoes (verrou_build de hud.c), anneau des secondes en segments ; plein
   écran (sa partie fixe est gardée composée jusqu'à la minute suivante) */
void th_cadran(const cadran_info_t *i);

#endif

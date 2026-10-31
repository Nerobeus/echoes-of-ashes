/* Pages de l'interface de la montre (commun/ui_montre : app montre, menu du PC DOS) : pages côte à côte que le doigt
   fait glisser (la voisine entre avec elle, le relâchement l'amène ou la ramène), toucher bref, appui long, glissé
   vertical (listes), rangée sous le doigt en surbrillance. Le doigt est lu par touch_etat (CST9217 : trames tant que
   le doigt est posé, -1 sans trame nouvelle) ; le code vient de l'app montre (montre.c) */
#ifndef PAGES_H
#define PAGES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int n;                                /* nombre de pages */
    void (*dessiner)(void);               /* image entière (pg_pages, puis ce que l'application met par-dessus), envoyée */
    void (*dessiner_page)(int p);         /* page p, décalage déjà posé (d_decalage) */
    void (*toucher)(int x, int y);        /* toucher bref sur la page courante */
    int (*appui_long)(int x, int y);      /* facultatif : doigt tenu 0,6 s sans bouger ; 1 si pris, sinon toucher */
    void (*page_changee)(void);           /* facultatif : après un glissé abouti */
    int (*glisse_permis)(void);           /* facultatif : 0 pour ne pas glisser (écran d'appel) */
    int (*appui_permis)(void);            /* facultatif : 0 pour ne pas mettre la rangée en surbrillance (bandeau) */
    void (*vertical)(int sens);           /* facultatif : glissé vertical (1 : vers le haut, la suite ; -1 : vers le bas) */
    void (*redessiner)(void);             /* demande d'une image (l'application dessine à son tour de boucle) */
    void (*activite)(int64_t now);        /* doigt sur l'écran (veille automatique) */
} pg_def_t;

extern int pg_page;                       /* page courante */
void pg_init(const pg_def_t *d, int page);
/* Lit le doigt : à appeler doigt posé (pg_doigt), INT du tactile basse, ou après son interruption */
void pg_tactile(int64_t now);
bool pg_doigt(void);                      /* doigt posé */
int pg_glisse(void);                      /* page glissée sous le doigt (pixels), 0 au repos */
bool pg_sous_doigt(int y, int h);         /* doigt posé (sans glissé) dans la bande [y, y + h) */
int pg_appui_x(void);                     /* x du doigt posé */
bool pg_appui(void);                      /* doigt posé, sans glissé (une rangée est en surbrillance) */
void pg_annuler(void);                    /* bouton, veille : le doigt et le glissé sont oubliés */
/* La page courante (avec son décalage) et, pendant un glissé, sa voisine qui entre */
void pg_pages(void);
/* Relevé des lectures du tactile (console) */
extern int pg_lectures;
extern int64_t pg_lecture_us;

#endif

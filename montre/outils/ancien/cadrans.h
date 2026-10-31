/* Cadrans de l'app montre : fond illustré lu sur la carte SD (outils/cadrans.py) et parties vivantes dessinées par-dessus */
#ifndef CADRANS_H
#define CADRANS_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    struct tm l;
    float s;               /* secondes avec leur fraction (0 à 60) */
    bool connue;           /* heure réglée */
    int batterie, usb;     /* % (-1 : inconnue), câble branché */
    int temp;              /* température de la puce en °C (-100 : inconnue) */
    int nnotifs;
    bool bt_on, bt_lien;
} cadran_info_t;

int cadrans_nb(void);
const char *cadran_nom(int k);
/* Choisit le cadran k et charge son fond (cadran_<id>.cad du dossier du jeu ; absent : fond noir) */
void cadran_choisir(int k);
void cadran_dessiner(const cadran_info_t *i);

#endif

/* Liaison Bluetooth avec le téléphone (montre/PROTOCOLE.md) : NimBLE périphérique « Echoes Montre » */
#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>

/* Ce que le téléphone envoie, passé à montre.c (tâche de l'hôte NimBLE : copier, ne pas dessiner) */
typedef struct {
    void (*heure)(int64_t utc, int fuseau_min);
    void (*notif)(const char *id, const char *app, const char *titre, const char *texte, int64_t heure);
    void (*effacer)(const char *id);            /* « * » : tout */
    void (*appel)(const char *etat, const char *nom, const char *numero);
    void (*connexion)(bool connecte);
} ble_rappels_t;

void ble_demarrer(const ble_rappels_t *r);
void ble_arreter(void);
bool ble_connecte(void);
int ble_raison(void);   /* raison de la dernière déconnexion (0x208 : supervision, le lien perdu ; 0x213 : le téléphone) */
/* Diffusion : rapide (100 ms) après un réveil, lente (1 s) en veille */
void ble_diffusion_rapide(bool oui);
/* Montre → téléphone : ACTION (appel:repondre, notif:effacer:<id>, trouver...) ; ETAT et batterie à jour */
void ble_action(const char *texte);
void ble_etat(int batterie, bool charge);
/* Oublier les téléphones appairés */
void ble_oublier(void);

#endif

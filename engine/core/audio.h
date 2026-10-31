/*
 * Sons : banque du paquet (section AUDI) et mixage.
 *
 * Le jeu demande des sons : son_jouer pour un son ponctuel, son_boucle pour une boucle tenue par une clé (moteur du
 * véhicule, d'un véhicule du trafic), à redemander à chaque image ; audio_fin_image éteint en fondu les boucles qui
 * ne l'ont pas été. La plateforme tire les échantillons à sa cadence (eng_audio_mix, eng.h), depuis sa propre tâche.
 * L'ordre des sons est celui de tools/audio/preparer_sons.py (et de SONS dans l'émulateur).
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

enum {
    SON_MOTEUR_VOITURE, SON_MOTEUR_CAMION, SON_MOTEUR_MECH, SON_MOTEUR_BATEAU, SON_MOTEUR_VAISSEAU,
    SON_PAS_1, SON_PAS_2, SON_ATTAQUE, SON_COUP, SON_TECHNIQUE, SON_SOIN, SON_KO, SON_EXPLOSION, SON_PORTIERE,
    SON_BIP, SON_MENU,
    SON_CHOC, SON_CRASH, SON_BOIS, SON_FROTTEMENT,   /* chocs des véhicules (un paquet plus ancien ne les a pas : silence) */
    /* armes, éléments des sorts, eau, interface, porte d'un bâtiment (absents d'un paquet plus ancien : silence) */
    SON_TIR, SON_LASER, SON_LAME, SON_FOUDRE, SON_FEU, SON_GLACE, SON_SACRE, SON_TENEBRES, SON_POISON, SON_TERRE, SON_EAU,
    SON_VENT, SON_MAGIE, SON_PLOUF, SON_VALIDER, SON_ANNULER, SON_PORTE,
    /* moteurs d'autres sortes de véhicules, choisis par l'atelier d'après le nom (section SONM) */
    SON_MOTEUR_MOTO, SON_MOTEUR_SPORT, SON_MOTEUR_CHENILLES, SON_MOTEUR_ELECTRIQUE, SON_MOTEUR_REACTEUR,
    SON_N
};
#define SON_SIRENE SON_N         /* sirène de l'alarme : fabriquée par le moteur (dent de scie de 700 Hz) */
/* CLE_TRAFIC + k : véhicule k du trafic ; CLE_COURSE + k : concurrent k ; CLE_FROTTEMENT : tôle du véhicule conduit qui
   frotte contre un mur ou un obstacle */
enum { CLE_VEHICULE = 1, CLE_TRAFIC = 2, CLE_FROTTEMENT = 16, CLE_SIRENE = 17, CLE_COURSE = 32 };

void audio_load(const uint8_t *p, uint32_t size);
void son_jouer(int son, float vol, float hauteur);
void son_boucle(int cle, int son, float vol, float hauteur);
void audio_fin_image(void);

#endif

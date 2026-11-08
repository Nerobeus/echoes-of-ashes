/*
 * Mode course de l'écran titre : la carrière (document de conception du moteur, onglet « Jeu de course », étapes 1 et 2 ;
 * comme la carrière de l'émulateur, emulateur/carriere.js de l'Atelier, à garder identiques).
 *
 * Entrée COURSE de l'écran titre (accueil.c, pages AP_C*) : trois carrières, chacune ses sauvegardes (emplacements
 * CARR_SLOT0 + carrière × 2 + séquence & 1 de save_write, à part des parties de l'histoire ; format 'ECR3', CRC-32 comme
 * les parties ; 'ECR2' et 'ECR1' d'avant encore lus), et la course libre. Une carrière : des classes de vitesse (CENDRE,
 * BRAISE, FLAMME, FOUDRE), des coupes de trois courses (points 10, 6, 3, 1 ; médaille de la coupe sur ses points :
 * bronze, argent, or ; le bronze ouvre la coupe suivante de la classe, toutes les coupes d'une classe en bronze ouvrent la
 * classe suivante), des crédits par place et une prime par médaille gagnée, le garage (les véhicules « utilisables en
 * course » de la bibliothèque, leurs prix dans CARR). Un tour de référence par circuit et par classe (CARR) donne les
 * médailles de temps du meilleur tour. Recommencer à l'arrivée et dans la pause.
 *
 * Étape 2 (02/11/2026) : les épreuves hors des coupes (CARR version 2) : contre-la-montre (seul, contre son fantôme gardé
 * sur la carte SD, course.c), tour rapide (le meilleur tour, essais jusqu'à TERMINER dans la pause), élimination (le
 * dernier à passer la ligne sort à chaque tour), duel (contre le rival nommé d'une région, ouvert quand toutes les coupes
 * de cette région dans la classe ont le bronze ; la première victoire donne au choix son véhicule ou toutes les livrées du
 * sien), le défi du jour (une épreuve tirée d'après la date de la montre), trois difficultés par épreuve et par coupe ;
 * garage : améliorations (vitesse, 0 → maxi, adhérence, trois niveaux) et livrées (palettes PVAR) débloquées par les
 * victoires.
 *
 * Catégories (03/11/2026, CARR version 3, demande de l'utilisateur : « des compétitions par catégories : vaisseau,
 * tout-terrain, citadines ») : OUVERTE (la carrière d'avant : tous les véhicules, ses coupes, épreuves et duels), puis
 * CITADINES, TOUT-TERRAIN, SPORTIVES, GLISSEURS, VAISSEAUX… (celles qui ont des véhicules) : chacune ses classes, ses
 * coupes, ses épreuves et son garage (ses véhicules ; son véhicule choisi). Une catégorie s'ouvre dès le départ ou quand
 * toutes les coupes d'une classe d'une autre ont le bronze ; son véhicule le moins bien noté est donné quand on y entre
 * sans en avoir. Les classes d'une catégorie se suivent dans CARRD.classes ; la plus haute ouverte : car.classe[k].
 *
 * Les circuits sont sur les cartes de course (CART, drapeau 2 ; js/carriere.js de l'Atelier) : la carrière y emmène Sera
 * (world_carte), monte la course (course_classe, course_sorte, course_start) et reprend la main à sa fin
 * (carriere_course_finie, appelé par course_end). Pendant le mode course : pas d'histoire (script_update), pas de
 * sauvegarde de partie (auto_permise, eng_sauvegarder), temps sec sauf un circuit sous la pluie (E.weather rendu en le
 * quittant).
 */
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include "internal.h"
#include "audio.h"

#define CARR_MAGIC 0x31524345u    /* 'ECR1' (étape 1, 292 octets) */
#define CARR_MAGIC2 0x32524345u   /* 'ECR2' (étape 2, 680 octets) */
#define CARR_MAGIC3 0x33524345u   /* 'ECR3' (catégories) */
#define ECR1_OCT 288              /* champs de 'ECR1' avant son CRC : les premiers de 'ECR2' */
#define RIVAUX (COURSE_N - 1)
/* 'ECR2' (680 octets) et 'ECR1' (ses 288 premiers octets, puis le CRC) : lus pour être mis à la forme d'ECR3 */
typedef struct {
    uint32_t magic, seq, credits; float temps;
    uint8_t classe, vehicule, volant, coupe, course, coupe_niv, r0[2], places[3], r1, pts[4], garage[32];
    uint8_t coupe_med[16], coupe_pts[16];
    struct { uint16_t tour[4]; uint8_t med[4]; } circuits[16];
    struct { uint16_t rec; uint8_t med, niv; } epr[32];
    uint8_t amelio[64], livrees[64], couleur[64], victoires[64];
    uint16_t defi_jour; uint8_t defi_med, duels;
    uint32_t crc;
} ecr2_t;
typedef char ecr1_ok[offsetof(ecr2_t, epr) == ECR1_OCT ? 1 : -1];
typedef char ecr2_ok[sizeof(ecr2_t) == 680 ? 1 : -1];

carr_data_t CARRD ENG_FROID;   /* (en PSRAM sur l'ESP32 : la SRAM est comptée) */
static carr_save_t car ENG_FROID;        /* carrière en cours */
/* mode : 0 aucun, 1 course d'une coupe, 2 course libre, 3 épreuve, 4 défi du jour */
static int car_k = -1, mode, meteo0 = -1, relance, terminer, recomp_ok;   /* recomp_ok : duel gagné, sa récompense à choisir */
static carr_save_t infos[CARRIERES] ENG_FROID;   /* carrières lues (page du menu) */
static int infos_ok[CARRIERES];
/* Course lancée : circuit, véhicule, adversaires, niveau, armes, classe, tours, sorte, épreuve (rang dans CARRD.epr),
   rival (duel), difficulté par défaut (crédits relatifs) */
static struct { int on, circuit, perso, riv[RIVAUX], niv, armes, classe, tours, sorte, epr, rival, defaut; } EC;
/* Résultat de la dernière course ; fin de coupe ; défi du jour */
static carr_res_t RES;
static carr_fin_t FIN;
static carr_defi_t DEFI;
static const char *const SORTE_NOM[CS_N] = { "COURSE", "CONTRE-LA-MONTRE", "TOUR RAPIDE", "ÉLIMINATION", "DUEL" };
/* Duel : niveau du rival nommé (part de la vitesse des adversaires de la classe) selon la difficulté */
static const float DUEL_LV[3] = { 0.9f, 0.97f, 1.03f };

static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t crc32(const uint8_t *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}
static void nom_copie(char *d, int n, const uint8_t *s, int m) { const int l = m < n ? m : n; memcpy(d, s, (size_t)l); d[l - 1] = 0; }
const char *carr_sorte_nom(int s) { return s >= 0 && s < CS_N ? SORTE_NOM[s] : ""; }

/* CARR (format dans pak.h) : en-tête de 40 octets, classes (24), circuits (40 ; 64 en version 3), coupes (32), véhicules
   (8 ; 16 en version 2) ; version 2 : épreuves (32), réglages (32) ; version 3 : catégories (48). Version 1 : les réglages
   par défaut ci-dessous ; versions 1 et 2 : une seule catégorie, OUVERTE */
void carriere_load(const uint8_t *p, uint32_t size) {
    memset(&CARRD, 0, sizeof CARRD); car_k = -1; mode = 0; EC.on = 0;
    carr_data_t *D = &CARRD;
    static const int PRIX[3] = { 800, 2000, 4000 };
    static const float PAS[3] = { 0.025f, 0.08f, 0.05f }, GAIN[3] = { 0.85f, 1, 1.25f }, TEMPS[3] = { 1.05f, 1, 0.97f };
    for (int i = 0; i < 3; i++) { D->amelio_prix[i] = PRIX[i]; D->amelio_pas[i] = PAS[i]; D->diff_gain[i] = GAIN[i]; D->diff_temps[i] = TEMPS[i]; }
    D->clm_depart = 1.5f; D->defi_gain = 2; D->livree_vict = 2;
    if (!p || size < 40 || p[0] < 1 || p[0] > 3) return;
    D->version = p[0];
    const int v3 = D->version >= 3, vo = D->version >= 2 ? 16 : 8, co = v3 ? 64 : 40, nepr = D->version >= 2 ? rd16(p + 6) : 0, ncat = v3 ? p[15] : 0;
    D->nclasses = p[1] < CARR_CLASSES ? p[1] : CARR_CLASSES; D->ncircuits = p[2] < CARR_CIRCUITS ? p[2] : CARR_CIRCUITS;
    D->ncoupes = p[3] < CARR_COUPES ? p[3] : CARR_COUPES;
    const int nveh = rd16(p + 4);
    for (int i = 0; i < 4; i++) D->points[i] = p[8 + i];
    for (int i = 0; i < 3; i++) { D->medailles[i] = p[12 + i]; D->temps[i] = rd16(p + 16 + i * 2) / 1000.0f; D->prime[i] = rd16(p + 32 + i * 2); }
    for (int i = 0; i < 4; i++) D->gains[i] = rd16(p + 24 + i * 2);
    uint32_t o = 40;
    const uint32_t need = o + (uint32_t)p[1] * 24 + (uint32_t)p[2] * (uint32_t)co + (uint32_t)p[3] * 32 + (uint32_t)nveh * vo
                          + (D->version >= 2 ? (uint32_t)nepr * 32 + 32 : 0) + (uint32_t)ncat * 48;
    if (need > size) return;
    for (int k = 0; k < p[1]; k++, o += 24) if (k < D->nclasses) {
        carr_classe_t *c = &D->classes[k];
        nom_copie(c->nom, sizeof c->nom, p + o, 16);
        c->v = rd16(p + o + 16) / 1000.0f; c->v_rivaux = rd16(p + o + 18) / 100.0f; c->gains = rd16(p + o + 20) / 1000.0f;
        c->cat = v3 && p[o + 22] < CARR_CATS ? p[o + 22] : 0;
    }
    for (int k = 0; k < p[2]; k++, o += (uint32_t)co) if (k < D->ncircuits) {
        carr_circuit_t *c = &D->circuits[k];
        nom_copie(c->nom, sizeof c->nom, p + o, 24);
        c->piste = p[o + 24]; c->carte = p[o + 25]; c->tours = p[o + 26]; c->heure = rd16(p + o + 28) / 1000.0f;
        c->pluie = v3 ? p[o + 27] / 100.0f : 0;
        for (int i = 0; i < (v3 ? CARR_CLASSES : 4); i++) c->ref[i] = rd16(p + o + 32 + i * 2) / 100.0f;
    }
    for (int k = 0; k < p[3]; k++, o += 32) if (k < D->ncoupes) {
        carr_coupe_t *c = &D->coupes[k];
        nom_copie(c->nom, sizeof c->nom, p + o, 24);
        c->classe = p[o + 24] < D->nclasses ? p[o + 24] : 0; c->niveau = p[o + 25] < 3 ? p[o + 25] : 1; c->armes = p[o + 26];
        for (int i = 0; i < 3; i++) c->circuits[i] = p[o + 28 + i] < D->ncircuits ? p[o + 28 + i] : 0;
    }
    for (int k = 0; k < nveh; k++, o += (uint32_t)vo) if (D->nveh < CARR_VEH && rd16(p + o) < E.w.npersos) {
        carr_veh_t *v = &D->veh[D->nveh++];
        v->perso = rd16(p + o); v->recompense = vo > 8 && (p[o + 3] & 1); v->prix = rd32(p + o + 4);
        v->depart = v3 ? p[o + 2] : p[o + 2] ? 3 : 0;   /* (avant les catégories : le véhicule donné, celui d'OUVERTE) */
        if (vo > 8) { v->v = rd16(p + o + 8) / 100.0f; v->acc = rd16(p + o + 10) / 100.0f; v->grip = rd16(p + o + 12) / 1000.0f; }
        v->cat = v3 && p[o + 14] < CARR_CATS ? p[o + 14] : 0;
    }
    for (int k = 0; k < nepr; k++, o += 32) if (D->nepr < CARR_EPR) {
        carr_epr_t *e = &D->epr[D->nepr];
        nom_copie(e->nom, sizeof e->nom, p + o, 24);
        e->sorte = p[o + 24]; e->circuit = p[o + 25]; e->classe = p[o + 26]; e->tours = p[o + 27];
        e->rival = rd16(p + o + 28) == 0xFFFF || rd16(p + o + 28) >= E.w.npersos ? -1 : rd16(p + o + 28);
        e->niveau = p[o + 30] < 3 ? p[o + 30] : 1; e->armes = p[o + 31];
        if (e->sorte > CS_COURSE && e->sorte < CS_N && e->circuit < D->ncircuits && e->classe < D->nclasses && (e->sorte != CS_DUEL || e->rival >= 0)) D->nepr++;
    }
    if (D->version >= 2) {
        for (int i = 0; i < 3; i++) {
            D->amelio_prix[i] = rd16(p + o + i * 2); D->amelio_pas[i] = rd16(p + o + 6 + i * 2) / 1000.0f;
            D->diff_gain[i] = rd16(p + o + 12 + i * 2) / 1000.0f; D->diff_temps[i] = rd16(p + o + 18 + i * 2) / 1000.0f;
        }
        D->clm_depart = rd16(p + o + 24) / 100.0f; D->defi_gain = rd16(p + o + 26) / 1000.0f; D->livree_vict = p[o + 28] ? p[o + 28] : 2;
        o += 32;
    }
    for (int k = 0; k < ncat; k++, o += 48) if (D->ncats < CARR_CATS) {
        carr_cat_t *c = &D->cats[D->ncats++];
        nom_copie(c->nom, sizeof c->nom, p + o, 16); nom_copie(c->aide, sizeof c->aide, p + o + 16, 24);
        c->ouvre_cat = p[o + 40] < CARR_CATS ? p[o + 40] : -1; c->ouvre_classe = p[o + 41] < D->nclasses ? p[o + 41] : -1; c->toutes = p[o + 42] & 1;
    }
    if (!D->ncats) { D->ncats = 1; snprintf(D->cats[0].nom, sizeof D->cats[0].nom, "OUVERTE"); D->cats[0].ouvre_cat = D->cats[0].ouvre_classe = -1; D->cats[0].toutes = 1; }
    for (int k = 0; k < D->nclasses; k++) if (D->classes[k].cat >= D->ncats) D->classes[k].cat = 0;
    for (int k = 0; k < D->nveh; k++) if (D->veh[k].cat >= D->ncats) D->veh[k].cat = 0;
    D->ok = D->ncircuits > 0 && D->ncoupes > 0 && D->nclasses > 0;
}

int carriere_on(void) { return mode != 0 || accueil_carriere_page(); }
int carriere_mode(void) { return mode; }
int carr_sorte_course(void) { return EC.on ? EC.sorte : CS_COURSE; }
void carriere_temps(float dt) { if (car_k >= 0 && mode != 2) car.temps += dt; }
int carr_disponible(void) { return CARRD.ok && course_npistes() > 0; }
carr_save_t *carr_cur(void) { return car_k >= 0 ? &car : NULL; }
int carr_k(void) { return car_k; }
const carr_res_t *carr_res(void) { return &RES; }
const carr_fin_t *carr_fin(void) { return &FIN; }
int carr_course_circuit(void) { return EC.circuit; }
const int *carr_course_rivaux(void) { return EC.riv; }
/* Fantôme d'un circuit dans une classe : les classes d'OUVERTE aux emplacements d'avant les catégories (CARR_FANT0 + circuit
   × 4 + classe), les autres après (CARR_FANT0 + 64 + circuit × 12 + classe - 4) */
int carr_fant_slot(int ci, int kc) { return kc < 4 ? CARR_FANT0 + ci * 4 + kc : CARR_FANT0 + 64 + ci * (CARR_CLASSES - 4) + kc - 4; }

/* ---------- Catégories ---------- */
static int cat_de_classe(int kc) { return kc >= 0 && kc < CARRD.nclasses ? CARRD.classes[kc].cat : 0; }
static int premiere_classe(int k) { for (int i = 0; i < CARRD.nclasses; i++) if (CARRD.classes[i].cat == k) return i; return -1; }
/* Toutes les coupes de la classe kc ont le bronze (au moins une) */
static int classe_faite(const carr_save_t *c, int kc) {
    int n = 0;
    for (int i = 0; i < CARRD.ncoupes; i++) if (CARRD.coupes[i].classe == kc) { if (c->coupe_med[i] < 1) return 0; n++; }
    return n > 0;
}
int carr_cat_ouverte_de(const carr_save_t *c, int k) {
    if (k < 0 || k >= CARRD.ncats) return 0;
    const carr_cat_t *t = &CARRD.cats[k];
    return t->ouvre_cat < 0 || t->ouvre_classe < 0 || classe_faite(c, t->ouvre_classe);
}
int carr_cat_ouverte(int k) { return car_k >= 0 && carr_cat_ouverte_de(&car, k); }
int carr_cats_ouvertes(const carr_save_t *c) { int n = 0; for (int k = 0; k < CARRD.ncats; k++) n += carr_cat_ouverte_de(c, k); return n; }
/* Classe ouverte la plus haute de la catégorie k (rang de CARRD.classes ; sa première tant que rien n'est gardé) */
int carr_cat_classe(const carr_save_t *c, int k) {
    const int kc = k >= 0 && k < CARR_CATS ? c->classe[k] : 255;
    return kc < CARRD.nclasses && CARRD.classes[kc].cat == k ? kc : premiere_classe(k);
}
int carr_cat(void) { return car_k >= 0 && car.cat < CARRD.ncats ? car.cat : 0; }
/* Véhicule i (rang de CARRD.veh) dans la catégorie k : OUVERTE (toutes) les a tous */
int carr_veh_dans(int i, int k) { return i >= 0 && i < CARRD.nveh && (CARRD.cats[k < CARRD.ncats && k >= 0 ? k : 0].toutes || CARRD.veh[i].cat == k); }
static int possede_i(const carr_save_t *c, int i) { return i >= 0 && i < CARR_VEH && (c->garage[i >> 3] >> (i & 7) & 1); }
int carr_veh_rang(int perso) { for (int i = 0; i < CARRD.nveh; i++) if (CARRD.veh[i].perso == perso) return i; return -1; }
int carr_possede(int p) { return car_k >= 0 && possede_i(&car, carr_veh_rang(p)); }
/* Véhicule choisi dans la catégorie k : celui gardé s'il est à soi et de la catégorie, sinon le premier à soi de la
   catégorie ; -1 aucun */
int carr_cat_veh(int k) {
    if (car_k < 0 || k < 0 || k >= CARR_CATS) return -1;
    const int v = car.vehicule[k];
    if (v < CARRD.nveh && possede_i(&car, v) && carr_veh_dans(v, k)) return v;
    for (int i = 0; i < CARRD.nveh; i++) if (possede_i(&car, i) && carr_veh_dans(i, k)) return i;
    return -1;
}
int carr_veh_perso(int k) { const int v = carr_cat_veh(k); return v >= 0 ? CARRD.veh[v].perso : -1; }
/* Catégorie k ouverte et sans véhicule : son véhicule de départ (bit 0 de depart ; OUVERTE : bit 1), sinon le moins cher */
static void cat_garantir(int k) {
    if (car_k < 0 || !carr_cat_ouverte(k) || carr_cat_veh(k) >= 0) return;
    int d = -1;
#define VEH_PRESENT(i) (perso_present(CARRD.veh[i].perso) && E.w.persos[CARRD.veh[i].perso].sheet >= 0)   /* (une livrée de l'histoire attend son drapeau) */
    for (int i = 0; i < CARRD.nveh && d < 0; i++) if (carr_veh_dans(i, k) && !CARRD.veh[i].recompense && VEH_PRESENT(i) && (CARRD.veh[i].depart & (CARRD.cats[k].toutes ? 2 : 1))) d = i;
    for (int i = 0; i < CARRD.nveh && d < 0; i++) if (carr_veh_dans(i, k) && !CARRD.veh[i].recompense && VEH_PRESENT(i)) d = i;   /* (le moins cher : CARRD.veh est trié par prix) */
#undef VEH_PRESENT
    if (d < 0) return;
    car.garage[d >> 3] |= (uint8_t)(1u << (d & 7)); car.vehicule[k] = (uint8_t)d;
    carr_ecrire();
}
void carr_cat_choisir(int k) { if (car_k < 0 || k < 0 || k >= CARRD.ncats) return; car.cat = (uint8_t)k; cat_garantir(k); }
int carr_cat_coupes(int k, int *out) { int n = 0; for (int i = 0; i < CARRD.ncoupes; i++) if (cat_de_classe(CARRD.coupes[i].classe) == k) out[n++] = i; return n; }
int carr_cat_eprs(int k, int *out) { int n = 0; for (int i = 0; i < CARRD.nepr; i++) if (cat_de_classe(CARRD.epr[i].classe) == k) out[n++] = i; return n; }

/* ---------- Sauvegardes ---------- */
/* Une sauvegarde d'avant les catégories ('ECR2', ou 'ECR1' complétée de zéros) à la forme d'ECR3 : sa progression devient
   celle d'OUVERTE (classe, véhicule), son garage passe des personnages aux rangs des véhicules ; mêmes rangs de coupes,
   circuits, classes d'OUVERTE, épreuves et véhicules (améliorations, livrées) */
static void migrer(const ecr2_t *a, carr_save_t *s) {
    memset(s, 0, sizeof *s);
    s->magic = CARR_MAGIC3; s->seq = a->seq; s->credits = a->credits; s->temps = a->temps;
    s->volant = a->volant; s->coupe = a->coupe; s->course = a->course; s->coupe_niv = a->coupe_niv;
    memcpy(s->places, a->places, 3); memcpy(s->pts, a->pts, 4);
    memset(s->classe, 255, sizeof s->classe); memset(s->vehicule, 255, sizeof s->vehicule);
    s->classe[0] = a->classe;
    for (int i = 0; i < CARRD.nveh; i++) {
        const int p = CARRD.veh[i].perso;
        if (p < 256 && (a->garage[p >> 3] >> (p & 7) & 1)) s->garage[i >> 3] |= (uint8_t)(1u << (i & 7));
        if (p == a->vehicule) s->vehicule[0] = (uint8_t)i;
    }
    memcpy(s->coupe_med, a->coupe_med, 16); memcpy(s->coupe_pts, a->coupe_pts, 16);
    for (int c = 0; c < 16 && c < CARR_CIRCUITS; c++) for (int k = 0; k < 4; k++) { s->circuits[c].tour[k] = a->circuits[c].tour[k]; s->circuits[c].med[k] = a->circuits[c].med[k]; }
    for (int e = 0; e < 32 && e < CARR_EPR; e++) { s->epr[e].rec = a->epr[e].rec; s->epr[e].med = a->epr[e].med; s->epr[e].niv = a->epr[e].niv; }
    memcpy(s->amelio, a->amelio, 64); memcpy(s->livrees, a->livrees, 64); memcpy(s->couleur, a->couleur, 64); memcpy(s->victoires, a->victoires, 64);
    s->defi_jour = a->defi_jour; s->defi_med = a->defi_med; s->duels = a->duels;
}
/* Un emplacement : 'ECR3', sinon 'ECR2' ou 'ECR1' mis à sa forme (la coupe en cours d'ECR1 garde la difficulté de la coupe) */
static int lire_slot(int slot, carr_save_t *s) {
    static uint8_t brut[sizeof(carr_save_t) > sizeof(ecr2_t) ? sizeof(carr_save_t) : sizeof(ecr2_t)] ENG_FROID;
    static ecr2_t a ENG_FROID;
    if (!E.cfg.save_read) return 0;
    memset(brut, 0, sizeof brut);
    const int n = E.cfg.save_read(slot, brut, sizeof brut);
    const uint32_t m = rd32(brut);
    if (n == (int)sizeof *s && m == CARR_MAGIC3) {
        memcpy(s, brut, sizeof *s);
        return s->crc == crc32((const uint8_t *)s, offsetof(carr_save_t, crc));
    }
    if (n == (int)sizeof a && m == CARR_MAGIC2 && rd32(brut + offsetof(ecr2_t, crc)) == crc32(brut, offsetof(ecr2_t, crc))) { memcpy(&a, brut, sizeof a); migrer(&a, s); return 1; }
    if (n == ECR1_OCT + 4 && m == CARR_MAGIC && rd32(brut + ECR1_OCT) == crc32(brut, ECR1_OCT)) {
        memset(&a, 0, sizeof a); memcpy(&a, brut, ECR1_OCT);
        a.coupe_niv = (uint8_t)(a.coupe < CARRD.ncoupes ? CARRD.coupes[a.coupe].niveau : 1);
        migrer(&a, s);
        return 1;
    }
    return 0;
}
static int lire(int k, carr_save_t *out) {
    static carr_save_t b ENG_FROID;   /* (pas sur la pile : 1,3 Ko) */
    const int oa = lire_slot(CARR_SLOT0 + k * 2, out), ob = lire_slot(CARR_SLOT0 + k * 2 + 1, &b);
    if (ob && (!oa || b.seq > out->seq)) *out = b;
    return oa || ob;
}
int carr_ecrire(void) {
    if (car_k < 0 || !E.cfg.save_write) return 0;
    car.magic = CARR_MAGIC3; car.seq++; car.volant = (uint8_t)(E.volant + 1);
    car.crc = crc32((const uint8_t *)&car, offsetof(carr_save_t, crc));
    return E.cfg.save_write(CARR_SLOT0 + car_k * 2 + (int)(car.seq & 1), &car, sizeof car) == (int)sizeof car;
}
void carr_infos_lire(void) { for (int k = 0; k < CARRIERES; k++) infos_ok[k] = lire(k, &infos[k]); }
const carr_save_t *carr_info(int k) { return k >= 0 && k < CARRIERES && infos_ok[k] ? &infos[k] : NULL; }
/* Ouvre la carrière k (sauvegardée, sinon une nouvelle : le véhicule de départ d'OUVERTE) */
void carr_ouvrir(int k) {
    car_k = k;
    if (infos_ok[k]) { car = infos[k]; if (car.volant && !reglages_communs()) E.volant = car.volant - 1; if (car.cat >= CARRD.ncats) car.cat = 0; return; }   /* (réglages communs : la direction en cours reste) */
    memset(&car, 0, sizeof car);
    car.coupe = 255; memset(car.classe, 255, sizeof car.classe); memset(car.vehicule, 255, sizeof car.vehicule);
    cat_garantir(0);
    carr_ecrire();
}
/* Médailles d'une carrière (coupes, temps, épreuves) */
int carr_medailles(const carr_save_t *c) {
    int n = 0;
    for (int i = 0; i < CARR_COUPES; i++) n += c->coupe_med[i] > 0;
    for (int i = 0; i < CARR_CIRCUITS; i++) for (int k = 0; k < CARR_CLASSES; k++) n += c->circuits[i].med[k] > 0;
    for (int i = 0; i < CARR_EPR; i++) n += c->epr[i].med > 0;
    return n;
}

/* ---------- Règles ---------- */
/* Coupe ouverte : sa catégorie ouverte, sa classe au plus la plus haute ouverte de la catégorie ; la première de sa classe,
   sinon le bronze à la coupe d'avant de la même classe */
int carr_ouverte_de(const carr_save_t *c, int k) {
    if (k < 0 || k >= CARRD.ncoupes) return 0;
    const int kc = CARRD.coupes[k].classe, ct = cat_de_classe(kc);
    if (!carr_cat_ouverte_de(c, ct) || kc > carr_cat_classe(c, ct)) return 0;
    for (int i = k - 1; i >= 0; i--) if (CARRD.coupes[i].classe == kc) return c->coupe_med[i] >= 1;
    return 1;
}
int carr_ouverte(int k) { return carr_ouverte_de(&car, k); }
/* Région d'une coupe : la carte de son premier circuit. Duel : ouvert quand toutes les coupes de sa classe dans sa région
   ont le bronze (au moins une) */
static int region_faite(const carr_save_t *c, int classe, int carte) {
    int n = 0;
    for (int i = 0; i < CARRD.ncoupes; i++) {
        const carr_coupe_t *cp = &CARRD.coupes[i];
        if (cp->classe != classe || CARRD.circuits[cp->circuits[0]].carte != carte) continue;
        if (c->coupe_med[i] < 1) return 0;
        n++;
    }
    return n > 0;
}
int carr_epr_ouverte(int k) {
    if (k < 0 || k >= CARRD.nepr || car_k < 0) return 0;
    const carr_epr_t *e = &CARRD.epr[k];
    const int ct = cat_de_classe(e->classe);
    if (!carr_cat_ouverte(ct) || e->classe > carr_cat_classe(&car, ct)) return 0;
    return e->sorte != CS_DUEL || region_faite(&car, e->classe, CARRD.circuits[e->circuit].carte);
}
static int med_coupe(int pts) { return pts >= CARRD.medailles[2] ? 3 : pts >= CARRD.medailles[1] ? 2 : pts >= CARRD.medailles[0] ? 1 : 0; }
static int med_temps(float t, float ref) {
    if (!(ref > 0) || !(t > 0)) return 0;
    return t <= ref * CARRD.temps[2] ? 3 : t <= ref * CARRD.temps[1] ? 2 : t <= ref * CARRD.temps[0] ? 1 : 0;
}
/* Véhicules du garage de la catégorie de la page (rangs dans CARRD.veh) : présents ; une récompense de duel seulement si elle
   est à soi */
int carr_garage_liste(int *out) {
    int n = 0;
    const int k = carr_cat();
    for (int i = 0; i < CARRD.nveh; i++) {
        const carr_veh_t *v = &CARRD.veh[i];
        if (carr_veh_dans(i, k) && perso_present(v->perso) && E.w.persos[v->perso].sheet >= 0 && (!v->recompense || (car_k >= 0 && possede_i(&car, i)))) out[n++] = i;
    }
    return n;
}
/* Adversaires (rangs de PERS) : trois véhicules présents de la catégorie (pas les récompenses), d'autres modèles d'abord, à
   partir du rang base de la liste (rivaux de l'émulateur) ; une catégorie d'un seul modèle : lui, dans d'autres couleurs
   (lancer_n) */
static void rivaux_cat(int base, int joueur, int k, int *out) {
    int L[CARR_VEH], n = 0, m = 0;
    for (int i = 0; i < CARRD.nveh; i++) { const carr_veh_t *v = &CARRD.veh[i]; if (!v->recompense && carr_veh_dans(i, k) && perso_present(v->perso) && E.w.persos[v->perso].sheet >= 0) L[n++] = v->perso; }
    if (!n) { for (int i = 0; i < RIVAUX; i++) out[i] = joueur; return; }
    for (int pass = 0; pass < 2; pass++) for (int j = 0; j < n && m < RIVAUX; j++) {
        const int p = L[(base + j) % n];
        int pris = p == joueur;
        for (int q = 0; q < m; q++) pris |= out[q] == p || (!pass && E.w.persos[out[q]].sheet == E.w.persos[p].sheet);
        if (!pass && joueur >= 0 && E.w.persos[p].sheet == E.w.persos[joueur].sheet) pris = 1;
        if (!pris) out[m++] = p;
    }
    while (m < RIVAUX) { out[m] = L[m % n]; m++; }
}
void carr_rivaux(int base, int joueur, int *out) { rivaux_cat(base, joueur, carr_cat(), out); }
/* Circuits des coupes ouvertes d'une carrière (rangs, croissants ; k >= 0 : de la catégorie k seulement) */
static int circuits_ouverts(const carr_save_t *c, int k, int *out) {
    uint8_t vu[CARR_CIRCUITS] = { 0 };
    int n = 0;
    for (int i = 0; i < CARRD.ncoupes; i++) if ((k < 0 || cat_de_classe(CARRD.coupes[i].classe) == k) && carr_ouverte_de(c, i)) for (int j = 0; j < 3; j++) vu[CARRD.coupes[i].circuits[j]] = 1;
    for (int i = 0; i < CARRD.ncircuits; i++) if (vu[i]) out[n++] = i;
    return n;
}
/* Circuits ouverts dans au moins une carrière (course libre), sinon ceux de la première coupe */
int carr_circuits_libres(int *out) {
    uint8_t vu[CARR_CIRCUITS] = { 0 };
    int n = 0, L[CARR_CIRCUITS];
    for (int k = 0; k < CARRIERES; k++) if (infos_ok[k]) { const int m = circuits_ouverts(&infos[k], -1, L); for (int i = 0; i < m; i++) vu[L[i]] = 1; }
    for (int i = 0; i < CARRD.ncircuits; i++) if (vu[i]) out[n++] = i;
    if (!n && CARRD.ncoupes) for (int i = 0; i < 3; i++) { int dej = 0; for (int q = 0; q < n; q++) dej |= out[q] == CARRD.coupes[0].circuits[i]; if (!dej) out[n++] = CARRD.coupes[0].circuits[i]; }
    for (int a = 1; a < n; a++) for (int b = a; b > 0 && out[b - 1] > out[b]; b--) { const int t = out[b]; out[b] = out[b - 1]; out[b - 1] = t; }
    return n;
}
/* Véhicules de la course libre : à soi dans une carrière au moins, et ceux de départ (rangs de PERS) */
int carr_vehicules_libres(int *out) {
    int m = 0;
    for (int i = 0; i < CARRD.nveh; i++) {
        const carr_veh_t *v = &CARRD.veh[i];
        const int p = v->perso;
        if (!perso_present(p) || E.w.persos[p].sheet < 0) continue;
        int ok = v->depart != 0;
        for (int k = 0; k < CARRIERES; k++) ok |= infos_ok[k] && possede_i(&infos[k], i);
        if (ok) out[m++] = p;
    }
    return m;
}
void carr_coupe_commencer(int c) {
    if (car.coupe != c) {
        car.coupe = (uint8_t)c; car.course = 0; car.coupe_niv = (uint8_t)CARRD.coupes[c].niveau;
        memset(car.pts, 0, sizeof car.pts); memset(car.places, 0, sizeof car.places); carr_ecrire();
    }
}
/* Achat ou choix d'un véhicule du garage (rang dans CARRD.veh), pour la catégorie de la page : 1 pris, 2 acheté, 0 trop cher */
int carr_garage_choisir(int i) {
    if (i < 0 || i >= CARRD.nveh) return 0;
    const carr_veh_t *v = &CARRD.veh[i];
    const int k = carr_cat();
    if (possede_i(&car, i)) { car.vehicule[k] = (uint8_t)i; carr_ecrire(); return 1; }
    if (v->recompense || v->prix > car.credits) return 0;
    car.credits -= v->prix; car.garage[i >> 3] |= (uint8_t)(1u << (i & 7)); car.vehicule[k] = (uint8_t)i;
    carr_ecrire();
    return 2;
}
/* Améliorations : niveau 0 à 3 par statistique (0 vitesse, 1 accélération, 2 adhérence) */
int carr_amelio(int i, int stat) { return i >= 0 && i < CARR_VEH && stat >= 0 && stat < 3 ? car.amelio[i] >> (2 * stat) & 3 : 0; }
int carr_amelio_acheter(int i, int stat) {
    const int lv = carr_amelio(i, stat);
    if (i < 0 || i >= CARRD.nveh || stat < 0 || stat > 2 || lv >= 3 || !possede_i(&car, i)) return 0;
    const uint32_t prix = (uint32_t)CARRD.amelio_prix[lv];
    if (prix > car.credits) return 0;
    car.credits -= prix; car.amelio[i] = (uint8_t)(car.amelio[i] + (1u << (2 * stat)));
    carr_ecrire();
    return 1;
}
float carr_veh_stat(int i, int stat) {
    if (i < 0 || i >= CARRD.nveh) return 0;
    const carr_veh_t *v = &CARRD.veh[i];
    float vm, ac;
    veh_params(v->perso, &vm, &ac);
    const float b = stat == 0 ? (v->v > 0 ? v->v : vm) : stat == 1 ? (v->acc > 0 ? v->acc : ac) : (v->grip > 0 ? v->grip : E.w.veh_grip[v->perso]);
    return b * (1 + CARRD.amelio_pas[stat] * carr_amelio(i, stat));
}
/* Livrées : ses couleurs (0), puis les palettes PVAR débloquées */
int carr_couleurs(int i, int *out) {
    int n = 0;
    out[n++] = 0;
    if (i < 0 || i >= CARRD.nveh) return n;
    const int nv = E.w.persos[CARRD.veh[i].perso].nvar;
    for (int k = 1; k <= nv && k < 8; k++) if (car.livrees[i] >> k & 1) out[n++] = k;
    return n;
}
void carr_couleur(int i, int var) { if (i >= 0 && i < CARR_VEH) { car.couleur[i] = (uint8_t)var; carr_ecrire(); } }
/* Victoire avec le véhicule i : les palettes débloquées suivent (la palette k à k × livree_vict victoires) */
static void victoire(int i) {
    if (i < 0 || i >= CARRD.nveh) return;
    if (car.victoires[i] < 255) car.victoires[i]++;
    const int nv = E.w.persos[CARRD.veh[i].perso].nvar;
    for (int k = 1; k <= nv && k < 8; k++) if (car.victoires[i] >= k * CARRD.livree_vict) car.livrees[i] |= (uint8_t)(1u << k);
}

/* ---------- Défi du jour : catégorie (parmi les ouvertes), sorte, circuit (des coupes ouvertes de la catégorie),
   difficulté tirés d'après la date ---------- */
const carr_defi_t *carr_defi(void) {
    int L[CARR_CIRCUITS], C[CARR_CATS], n = 0, nc = 0;
    DEFI.jour = eng_date_jour();
    if (DEFI.jour < 0 || car_k < 0) { DEFI.jour = -1; return &DEFI; }
    for (int k = 0; k < CARRD.ncats; k++) if (carr_cat_ouverte(k) && circuits_ouverts(&car, k, L) > 0) C[nc++] = k;
    if (!nc) { DEFI.jour = -1; return &DEFI; }
    uint32_t h = (uint32_t)DEFI.jour * 2654435761u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    static const int S[3] = { CS_CLM, CS_TOUR, CS_ELIM };
    DEFI.cat = C[(h >> 20) % (uint32_t)nc];
    n = circuits_ouverts(&car, DEFI.cat, L);
    DEFI.sorte = S[h % 3]; DEFI.circuit = L[(h >> 4) % (uint32_t)n]; DEFI.classe = carr_cat_classe(&car, DEFI.cat); DEFI.niveau = 1 + (int)(h >> 12 & 1);
    return &DEFI;
}

/* ---------- Une course ---------- */
static int tours_de(int sorte, int ci, int tours) { return tours > 0 ? tours : sorte == CS_TOUR ? 9 : sorte == CS_ELIM ? 3 : CARRD.circuits[ci].tours; }
static int lancer_n(int ci, int perso, const int *riv, int nia, int niv, int armes, int classe, int tours, int sorte) {
    const carr_circuit_t *c = &CARRD.circuits[ci];
    const piste_t *P = course_piste(c->piste);
    if (!P || perso < 0 || perso >= E.w.npersos) return 0;
    if (meteo0 < 0) meteo0 = E.weather;
    /* Temps sec pendant le mode course, tout de suite ; un circuit sous la pluie (c->pluie) : la pluie tout de suite
       (réglage du joueur rendu en le quittant) */
    if (c->pluie > 0) { E.weather = 2; E.rain = E.w.rain_int / 100.0f; } else { E.weather = 1; E.rain = 0; }
    world_carte(c->carte, P->xd, P->zd);
    world_stream(E.sera.x, E.sera.z, 1); E.stream_hold = 0;
    E.heure = c->heure;
    course_trace(c->piste);
    const int kc = classe >= 0 && classe < CARRD.nclasses ? classe : 0;
    const carr_classe_t *cl = &CARRD.classes[kc];
    tours = tours_de(sorte, ci, tours);
    course_classe(cl->v, cl->v_rivaux);
    course_sorte(sorte);
    if (!course_start(perso, tours, riv, nia, niv, armes)) { course_classe(0, 0); course_sorte(CS_COURSE); return 0; }
    EC.on = 1; EC.circuit = ci; EC.perso = perso; EC.niv = niv; EC.armes = armes; EC.classe = kc; EC.tours = tours; EC.sorte = sorte;
    for (int i = 0; i < RIVAUX; i++) EC.riv[i] = i < nia ? riv[i] : -1;
    /* Carrière : le véhicule tel que le garage l'a fait (vitesse, accélération, adhérence, améliorations ; livrée) */
    const int vi = carr_veh_rang(perso);
    racer_t *j = &CR.r[CR.n - 1];
    int var = 0;
    if (car_k >= 0 && mode != 2 && vi >= 0) {
        j->vmax = carr_veh_stat(vi, 0) * cl->v; j->acc = carr_veh_stat(vi, 1) * cl->v; j->grip = carr_veh_stat(vi, 2);
        var = car.couleur[vi] <= E.w.persos[perso].nvar ? car.couleur[vi] : 0;
    }
    j->var = (uint8_t)var;
    if (E.ride >= 0) E.npcs[E.ride].var = (uint8_t)var;
    /* Adversaires du même modèle que le joueur ou qu'un autre (une catégorie d'un seul modèle) : d'autres couleurs */
    for (int k = 0; k < CR.n - 1; k++) {
        racer_t *r = &CR.r[k];
        int pareil = r->perso == perso;
        for (int q = 0; q < k; q++) pareil += CR.r[q].perso == r->perso;
        const int nv = E.w.persos[r->perso].nvar;
        if (pareil && nv > 0) { int v2 = (var + pareil) % (nv + 1); if (v2 == var) v2 = (v2 + 1) % (nv + 1); r->var = (uint8_t)v2; }
    }
    if (sorte == CS_DUEL && CR.n > 1) {   /* le rival nommé, plus fort que les adversaires des coupes */
        racer_t *r = &CR.r[0];
        float vr, ar; veh_params(r->perso, &vr, &ar);
        r->vmax = cl->v_rivaux * cl->v * DUEL_LV[niv < 0 ? 0 : niv > 2 ? 2 : niv]; r->acc = ar * cl->v * DUEL_LV[niv < 0 ? 0 : niv > 2 ? 2 : niv];
    }
    if (sorte == CS_CLM) course_fantome_lire(carr_fant_slot(ci, kc));
    accueil_course_partie();   /* l'ouverture laisse la place à la course */
    return 1;
}
/* Tour de référence (essais : rendu_test « cref ») : seul sur le circuit ci, le joueur piloté comme un adversaire, à la
   vitesse des adversaires de la classe (v_rivaux × v, accélération jusqu'à elle en 3 s, adhérence 1), trois tours */
int carr_essai_ref(int ci, int classe) {
    if (ci < 0 || ci >= CARRD.ncircuits || !CARRD.nveh) return 0;
    int riv[RIVAUX] = { 0 };
    mode = 2;
    if (!lancer_n(ci, CARRD.veh[0].perso, riv, 0, 1, 0, classe, 3, CS_COURSE)) { mode = 0; return 0; }
    const carr_classe_t *cl = &CARRD.classes[classe >= 0 && classe < CARRD.nclasses ? classe : 0];
    racer_t *j = &CR.r[CR.n - 1];
    j->vmax = cl->v_rivaux * cl->v; j->acc = j->vmax / 3; j->grip = 1;
    course_joueur_ia(1);
    return 1;
}
int carr_lancer_coupe(void) {
    if (car_k < 0 || car.coupe >= CARRD.ncoupes) return 0;
    const carr_coupe_t *cp = &CARRD.coupes[car.coupe];
    const int k = cat_de_classe(cp->classe);
    cat_garantir(k);
    const int pv = carr_veh_perso(k);
    if (pv < 0) return 0;
    int riv[RIVAUX];
    rivaux_cat(car.coupe * 5 + car.course * 3, pv, k, riv);
    mode = 1; EC.epr = -1; EC.defaut = cp->niveau;
    if (lancer_n(cp->circuits[car.course < 3 ? car.course : 2], pv, riv, RIVAUX, car.coupe_niv < 3 ? car.coupe_niv : cp->niveau, cp->armes, cp->classe, 0, CS_COURSE)) return 1;
    mode = 0;
    return 0;
}
/* Épreuve : contre-la-montre et tour rapide seuls, duel contre le rival nommé, élimination contre trois adversaires de la
   catégorie ; sans armes (seules les courses des coupes et de la course libre en ont) */
static int lancer_sorte(int sorte, int ci, int perso, int classe, int niv, int tours, int armes, int rival, int base) {
    int riv[RIVAUX] = { 0 }, nia = 0;
    if (sorte == CS_DUEL) { riv[0] = rival; nia = 1; }
    else if (sorte == CS_ELIM || sorte == CS_COURSE) { rivaux_cat(base, perso, mode == 2 ? 0 : cat_de_classe(classe), riv); nia = RIVAUX; }
    if (sorte != CS_COURSE) armes = 0;   /* épreuves sans armes (le hasard des armes n'y a pas sa place) */
    return lancer_n(ci, perso, riv, nia, niv, armes, classe, tours, sorte);
}
int carr_lancer_epr(int k, int niv) {
    if (car_k < 0 || !carr_epr_ouverte(k)) return 0;
    const carr_epr_t *e = &CARRD.epr[k];
    const int ct = cat_de_classe(e->classe);
    cat_garantir(ct);
    const int pv = carr_veh_perso(ct);
    if (pv < 0) return 0;
    mode = 3; EC.epr = k; EC.rival = e->rival; EC.defaut = e->niveau;
    if (lancer_sorte(e->sorte, e->circuit, pv, e->classe, niv, e->tours, e->armes, e->rival, k * 7)) return 1;
    mode = 0;
    return 0;
}
int carr_lancer_defi(void) {
    const carr_defi_t *d = carr_defi();
    if (d->jour < 0) return 0;
    cat_garantir(d->cat);
    const int pv = carr_veh_perso(d->cat);
    if (pv < 0) return 0;
    mode = 4; EC.epr = -1; EC.defaut = 1;
    if (lancer_sorte(d->sorte, d->circuit, pv, d->classe, d->niveau, 0, 0, -1, d->jour)) return 1;
    mode = 0;
    return 0;
}
int carr_lancer_libre_s(int ci, int perso, int niv, int tours, int sorte) {
    mode = 2; EC.epr = -1; EC.defaut = niv;
    if (lancer_sorte(sorte, ci, perso, 0, niv, tours, 1, -1, ci * 3)) return 1;
    mode = 0;
    return 0;
}
int carr_lancer_libre(int ci, int perso, int niv, int tours) { return carr_lancer_libre_s(ci, perso, niv, tours, CS_COURSE); }
int carr_relancer(void) {
    if (mode == 1) return carr_lancer_coupe();
    if (mode == 2) return carr_lancer_libre_s(EC.circuit, EC.perso, EC.niv, EC.tours, EC.sorte);
    if (mode == 3) return carr_lancer_epr(EC.epr, EC.niv);
    if (mode == 4) return carr_lancer_defi();
    return 0;
}
/* Pause : recommencer (la course en cours ne compte pas) ; terminer un tour rapide (son meilleur tour compte) */
int carr_recommencer(void) { if (!CR.on || !mode || !EC.on) return 0; relance = 1; course_abandon(); return 1; }
int carr_terminer(void) { if (!CR.on || !mode || EC.sorte != CS_TOUR) return 0; terminer = 1; course_abandon(); return 1; }

/* Crédits d'une épreuve : selon la médaille (solo) ou la place, × la classe, × la difficulté (rapportée à celle par
   défaut : une coupe à son niveau gagne ce qu'elle gagnait à l'étape 1) */
static int credits_de(int sorte, int place, int med) {
    const int r = sorte == CS_CLM || sorte == CS_TOUR ? 3 - med : sorte == CS_DUEL ? (place == 1 ? 0 : 3) : place - 1;
    if (r < 0 || r > 3) return 0;
    const float g = CARRD.gains[r] * (sorte == CS_DUEL && place == 1 ? 2 : 1) * (CARRD.classes[EC.classe].gains > 0 ? CARRD.classes[EC.classe].gains : 1);
    const float d = CARRD.diff_gain[EC.niv < 0 ? 0 : EC.niv > 2 ? 2 : EC.niv] / fmaxf(0.01f, CARRD.diff_gain[EC.defaut < 0 ? 0 : EC.defaut > 2 ? 2 : EC.defaut]);
    return (int)lroundf(g * d);
}
/* Rang du duel k parmi les duels (bit de car.duels) */
static int duel_bit(int k) { int b = 0; for (int i = 0; i < k && i < CARRD.nepr; i++) b += CARRD.epr[i].sorte == CS_DUEL; return b < 8 ? b : -1; }
/* Fin d'une course (course_end) : résultat, records de la carrière (meilleur tour, médailles, fantôme), page des résultats */
void carriere_course_finie(const course_res_t *r) {
    if (!mode || !EC.on) return;
    course_classe(0, 0);
    if (relance) { relance = 0; terminer = 0; carr_relancer(); return; }   /* recommencer (pause) : la course repart */
    const int abandon = r->abandon && !(terminer && EC.sorte == CS_TOUR && r->meilleur > 0);
    terminer = 0;
    const carr_circuit_t *c = &CARRD.circuits[EC.circuit];
    const int kc = EC.classe < CARR_CLASSES ? EC.classe : 0, s = EC.sorte, niv = EC.niv < 0 ? 0 : EC.niv > 2 ? 2 : EC.niv;
    memset(&RES, 0, sizeof RES);
    RES.ok = 1; RES.abandon = abandon; RES.place = abandon ? 0 : s == CS_TOUR ? 1 : r->place; RES.temps = r->temps; RES.meilleur = r->meilleur; RES.sorte = s;
    RES.ref = c->ref[kc];
    RES.med_t = med_temps(r->meilleur, RES.ref);
    if (!abandon) {
        if (s == CS_CLM) {
            RES.ref = c->ref[kc] > 0 ? c->ref[kc] * EC.tours + CARRD.clm_depart : 0;
            RES.med = r->temps > 0 ? med_temps(r->temps, RES.ref * CARRD.diff_temps[niv]) : 0;
            RES.record = (int)lroundf(fmaxf(0, r->temps) * 100);
            /* Fantôme : le parcours s'il bat celui de la carte SD (ou s'il n'y en a pas) */
            if (r->temps > 0 && (!course_fantome_ok() || r->temps < course_fantome_temps()))
                course_fantome_ecrire(carr_fant_slot(EC.circuit, kc), EC.perso, CR.r[CR.n - 1].var, kc, EC.circuit);
        } else if (s == CS_TOUR) {
            RES.med = med_temps(r->meilleur, RES.ref * CARRD.diff_temps[niv]);
            RES.record = (int)lroundf(fmaxf(0, r->meilleur) * 100);
        } else if (s == CS_ELIM) { RES.med = r->place >= 1 && r->place <= 3 ? 4 - r->place : 0; RES.record = r->place; }
        else if (s == CS_DUEL) { RES.med = r->place == 1 ? 3 : 0; RES.record = r->place; }
        else RES.med = r->place == 1 ? 3 : 0;
        if (r->place >= 1 && r->place <= 4 && s == CS_COURSE) {
            RES.points = CARRD.points[r->place - 1];
            for (int pl = 0; pl < r->n && pl < COURSE_N; pl++) if (r->ordre[pl] < RIVAUX) RES.riv_pts[r->ordre[pl]] = CARRD.points[pl];
        }
        RES.credits = mode == 2 ? 0 : credits_de(s, RES.place, RES.med);
        if (mode == 4 && RES.med >= 1 && car.defi_jour != (uint16_t)DEFI.jour) RES.bonus = (int)lroundf(RES.credits * (CARRD.defi_gain - 1));
    }
    if (mode == 1 && car_k >= 0 && !abandon && r->meilleur > 0 && EC.circuit < CARR_CIRCUITS && kc < CARR_CLASSES) {
        uint16_t *t = &car.circuits[EC.circuit].tour[kc];
        const int cs = (int)lroundf(r->meilleur * 100);
        if (!*t || cs < *t) *t = (uint16_t)(cs < 65535 ? cs : 65535);
        if (RES.med_t > car.circuits[EC.circuit].med[kc]) car.circuits[EC.circuit].med[kc] = (uint8_t)RES.med_t;
        carr_ecrire();
    }
    if (mode == 3 && car_k >= 0 && !abandon && EC.epr >= 0 && EC.epr < CARR_EPR) {   /* records de l'épreuve, gardés tout de suite */
        __typeof__(&car.epr[0]) e = &car.epr[EC.epr];
        const int rec = RES.record > 65535 ? 65535 : RES.record;
        if (rec > 0 && (!e->rec || rec < e->rec)) e->rec = (uint16_t)rec;
        if (RES.med > e->med || (RES.med && RES.med == e->med && niv > e->niv)) { e->med = (uint8_t)RES.med; e->niv = (uint8_t)niv; }
        const int b = duel_bit(EC.epr);
        RES.recomp = s == CS_DUEL && r->place == 1 && b >= 0 && !(car.duels >> b & 1);
        carr_ecrire();
    }
    accueil_carriere(AP_CRES);
}
/* Résultat validé (CONTINUER) : points, crédits, course suivante ; fin de coupe : médaille, prime, classe suivante de la
   catégorie, catégorie ouverte. Renvoie la page suivante (AP_CCOUPE, AP_CFIN, AP_CEPR, AP_CRECOMP, AP_CDEFI, ou AP_CLIBRE
   en course libre) */
int carr_valider(void) {
    if (mode == 2) { RES.ok = 0; return AP_CLIBRE; }
    if (mode == 3 || mode == 4) {
        const int p = mode == 3 ? (RES.ok && RES.recomp ? AP_CRECOMP : AP_CEPR) : AP_CDEFI;
        recomp_ok = p == AP_CRECOMP;
        if (RES.ok && !RES.abandon && car_k >= 0) {
            car.credits += (uint32_t)(RES.credits + RES.bonus);
            if (RES.med >= 3) victoire(carr_veh_rang(EC.perso));
            if (mode == 4 && RES.med >= 1) { car.defi_jour = (uint16_t)DEFI.jour; if (RES.med > car.defi_med || RES.bonus) car.defi_med = (uint8_t)RES.med; }
            carr_ecrire();
        }
        RES.ok = 0;
        return p;
    }
    if (mode != 1 || car_k < 0 || !RES.ok || RES.abandon || car.coupe >= CARRD.ncoupes) { RES.ok = 0; return AP_CCOUPE; }
    const int k = car.coupe, cl = CARRD.coupes[k].classe, ct = cat_de_classe(cl);
    car.pts[0] = (uint8_t)(car.pts[0] + RES.points);
    for (int i = 0; i < RIVAUX; i++) car.pts[1 + i] = (uint8_t)(car.pts[1 + i] + RES.riv_pts[i]);
    if (car.course < 3) car.places[car.course] = (uint8_t)RES.place;
    car.credits += (uint32_t)RES.credits; car.course++;
    if (RES.place == 1) victoire(carr_veh_rang(EC.perso));
    RES.ok = 0;
    if (car.course < 3) { carr_ecrire(); return AP_CCOUPE; }
    const int med = med_coupe(car.pts[0]), avant = car.coupe_med[k];
    int ouvertes0[CARR_CATS];
    for (int q = 0; q < CARRD.ncats; q++) ouvertes0[q] = carr_cat_ouverte(q);
    memset(&FIN, 0, sizeof FIN);
    FIN.coupe = k; FIN.med = med; for (int i = 0; i < 4; i++) FIN.pts[i] = car.pts[i];
    FIN.prime = med > avant ? CARRD.prime[med - 1] : 0;
    car.credits += (uint32_t)FIN.prime;
    if (med > avant) car.coupe_med[k] = (uint8_t)med;
    if (car.pts[0] > car.coupe_pts[k]) car.coupe_pts[k] = car.pts[0];
    /* Classe suivante de la catégorie : toutes les coupes de la classe en bronze ; catégorie ouverte par cette classe */
    const int haut = carr_cat_classe(&car, ct), suiv = cl + 1 < CARRD.nclasses && CARRD.classes[cl + 1].cat == ct ? cl + 1 : -1;
    int nouvelle = -1;
    for (int q = 0; q < CARRD.ncats; q++) if (!ouvertes0[q] && carr_cat_ouverte(q)) { nouvelle = q; break; }
    if (cl == haut && suiv >= 0 && classe_faite(&car, cl)) { car.classe[ct] = (uint8_t)suiv; snprintf(FIN.ouvre, sizeof FIN.ouvre, "Classe %s ouverte", CARRD.classes[suiv].nom); }
    else if (nouvelle >= 0) snprintf(FIN.ouvre, sizeof FIN.ouvre, "Catégorie %s ouverte", CARRD.cats[nouvelle].nom);
    else if (med && k + 1 < CARRD.ncoupes && CARRD.coupes[k + 1].classe == cl && !avant) snprintf(FIN.ouvre, sizeof FIN.ouvre, "%s ouverte", CARRD.coupes[k + 1].nom);
    else if (med && !avant) {   /* duel de la région ouvert */
        for (int e = 0; e < CARRD.nepr; e++) if (CARRD.epr[e].sorte == CS_DUEL && CARRD.epr[e].classe == cl && carr_epr_ouverte(e)
            && CARRD.circuits[CARRD.epr[e].circuit].carte == CARRD.circuits[CARRD.coupes[k].circuits[0]].carte) { snprintf(FIN.ouvre, sizeof FIN.ouvre, "%s ouvert", CARRD.epr[e].nom); break; }
    }
    car.coupe = 255; car.course = 0;
    carr_ecrire();
    return AP_CFIN;
}
/* Récompense d'un duel gagné (la première fois) : le véhicule du rival (vehicule = 1), sinon toutes les livrées du sien */
int carr_recompense(int vehicule) {
    const int b = duel_bit(EC.epr);
    if (car_k < 0 || b < 0 || !recomp_ok || (car.duels >> b & 1)) return 0;
    car.duels |= (uint8_t)(1u << b); recomp_ok = 0;
    const int ri = carr_veh_rang(EC.rival);
    if (vehicule && ri >= 0) car.garage[ri >> 3] |= (uint8_t)(1u << (ri & 7));
    else { const int i = carr_veh_rang(EC.perso); if (i >= 0) car.livrees[i] = 0xFE; }
    carr_ecrire();
    return 1;
}
/* Quitter le mode course (retour à l'écran titre) : météo du joueur, carte 0 (accueil.c montre le lieu du fond) */
void carr_quitter(void) {
    mode = 0; EC.on = 0; RES.ok = 0;
    if (meteo0 >= 0) { E.weather = meteo0; meteo0 = -1; }
}
void carr_page_menu(void) { mode = 0; EC.on = 0; }

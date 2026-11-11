/*
 * Scénario : quêtes et événements de l'atelier, et leur bytecode, compilé par js/scenario.js. Même machine que
 * l'émulateur : un seul script à la fois ; il attend la fin d'un dialogue, d'une pause, d'un carton ou d'un combat avant
 * l'instruction suivante. Les événements sont testés dans l'ordre : au lancement, à l'entrée dans une zone (pas à chaque
 * image passée dedans), en parlant à un personnage (le script remplace alors ses répliques). Un événement d'une quête ne
 * part qu'à son étape (0 : avant que la quête commence). Un combat perdu interrompt le script sans marquer l'événement
 * comme fait.
 *
 * Le scénario est lu sur la carte SD (histoire.scn, eng_config_t.scen_read) s'il y est, sinon dans la section SCEN du
 * paquet ; même contenu (scenBytes de l'émulateur). Seules la table des événements et les noms des quêtes restent en
 * mémoire : le code de l'événement qui part, chaque réplique et les objectifs des étapes sont lus au moment voulu.
 * L'histoire n'est donc limitée ni par la mémoire ni par la flash : 1 024 événements, 1 024 drapeaux, 256 quêtes,
 * 65 535 chaînes.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"

#define BOSS_ECART 0   /* combat du scénario : au plus ce nombre de niveaux au-dessus de l'équipe */
enum { OP_END, OP_SAY, OP_WAIT, OP_SET, OP_CLR, OP_JMP_IF_NOT, OP_JMP_IF, OP_GIVE, OP_BATTLE, OP_TELEPORT, OP_WEATHER,
       OP_HEAL, OP_JMP, OP_PARTY, OP_JMP_IF_FEWER, OP_JMP_IF_ENOUGH, OP_TITLE, OP_OBJECTIVE, OP_QUEST, OP_JMP_IF_QUEST_LT,
       OP_JMP_IF_QUEST_GE };
/* Course (js/scenario.js) : tours, adversaires, niveau, véhicule (255 : par défaut) ; place u8 (255 : annulée), adresse */
enum { OP_RACE = 21, OP_JMP_IF_NOT_RACE = 22, OP_JMP_IF_RACE = 23 };
/* Expérience u16 ; choix : n u8, n chaînes u16 ; si (non) choix : réponse u8, adresse ; crédits i32 ; si (non) crédits :
   montant u32, adresse */
enum { OP_EXP = 24, OP_CHOICE = 25, OP_JMP_IF_NOT_CHOICE = 26, OP_JMP_IF_CHOICE = 27, OP_CREDITS = 28,
       OP_JMP_IF_CREDITS_LT = 29, OP_JMP_IF_CREDITS_GE = 30 };
/* Musique : chaîne u16 (nom de la piste ; 65535 auto, 65534 silence) ; tenue : membre u8, chaîne u16 (son nom ; 65535 :
   la sienne) */
enum { OP_MUSIC = 31, OP_OUTFIT = 32, OP_JOB = 33, OP_RACE2 = 34, OP_VOYAGE = 35, OP_JMP_IF_NOT_HEROINE = 36, OP_JMP_IF_HEROINE = 37,
       OP_POSE = 38, OP_IMAGE = 39 };
#define SCENE_S 2.5f   /* illustration nouvelle : seule, en plein écran (script_image_seule) */
static int scene_seule;
/* Mise en scène (game.c, MISE) : caméra, acteur, marche, attente des marches, regard, fondu, secousse, fuite et son résultat */
enum { OP_CAMERA = 40, OP_ACTOR = 41, OP_WALK = 42, OP_WAIT_WALK = 43, OP_LOOK = 44, OP_FADE = 45, OP_SHAKE = 46, OP_FLEE = 47,
       OP_JMP_IF_NOT_FLEE = 48, OP_JMP_IF_FLEE = 49, OP_HOUR = 50 };
/* Combat contre un groupe (« combat A, B », « combat A x3 », « combat BOSS avec S x2 ») : mode u8 (0 parts égales, 1 boss et
   sbires), nombre u8 (1 à 4), puis un personnage u8 chacun (index PERS ; le premier : le chef) */
enum { OP_BATTLE2 = 51 };
/* Équipe sur deux octets (les recrues : NMEMB membres, 01/11/2026) : mode u8, membres u16 ; comme OP_PARTY */
enum { OP_PARTY2 = 52 };
/* Police (les casses de Lucerne) : niveau u8 ; si (non) recherchée : adresse u16 */
enum { OP_POLICE = 53, OP_JMP_IF_NOT_WANTED = 54, OP_JMP_IF_WANTED = 55 };
#define SC_ONCE 1
#define SC_NEG 2
#define SC_ADULTE 4             /* événement 18+ (édition unique) */
#define SC_ECHO 8               /* Écho à chercher (js/echos.js) : signal du Lien, JOURNAL */
#define SC_NUIT 16              /* ne part que la nuit (de ECHO_NUIT_A à ECHO_NUIT_B, parts de la journée) */
#define SC_PLUIE 32             /* ne part que sous la pluie */
#define ECHO_NUIT_A (20 / 24.0f)
#define ECHO_NUIT_B (6 / 24.0f)
#define ECHO_PLUIE 0.2f
#define SC_HEAD 52                        /* 'SCN5', drapeaux, chaînes, événements, index, table, plus long code, quêtes, leur
                                             table, objets, leur table, plan de musique, sa taille */
#define SC_ENTRY 28
#define SUJETS 8                          /* choix des sujets : événements « parler » retenus (3 proposés, puis RIEN) */
#define Q_DONE 255

static struct {
    int on, ev, pc, started;
    float wait;
    char right[24];                       /* dernier interlocuteur (portrait de droite) */
    char who[24], text[512];              /* réplique affichée (hud_say garde le pointeur du texte) */
    uint8_t inzone[SC_MAX_EVENTS / 8];    /* Sera dans la zone de l'événement à l'image précédente */
    int race;                             /* instruction course : attend la fin du menu, puis de la course */
    float race_x, race_z, race_yaw;       /* où était Sera quand le script a lancé la course : elle y revient après */
    int choice;                           /* dernière réponse à un choix (1 à 4), 0 : aucune */
    int appr;                             /* réplique (pc + 1) pour laquelle Sera s'est déjà approchée */
    int marche;                           /* attend la fin d'une marche (mise_marche : place, 255 Sera, -2 toutes), -1 : non */
    int fuite;                            /* attend la fin d'une fuite */
    int suj_att, nsuj, suj[SUJETS];       /* choix des sujets d'un personnage (script_talk) : en attente, événements proposés */
} VM ENG_FROID;

/* Étape lue en dernier (objectif, cible) : le menu et le guidage la redemandent à chaque image */
static struct { int q, st, ok, niv, carte; uint16_t x, z; char text[96]; } STEP = { -1, -1, 0, 0, 0, 0, 0, "" };

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static int rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static int bit(const uint8_t *b, int k) { return (b[k >> 3] >> (k & 7)) & 1; }
static void set_bit(uint8_t *b, int k, int v) { if (v) b[k >> 3] |= (uint8_t)(1 << (k & 7)); else b[k >> 3] &= (uint8_t)~(1 << (k & 7)); }
static int flag(int k) { return k < SC_MAX_FLAGS && bit(E.flags, k); }

/* len octets du scénario à partir de off : carte SD, sinon la section SCEN ; 0 si tout va bien */
static int sc_read(uint32_t off, void *dst, uint32_t len) {
    if ((uint64_t)off + len > E.w.sc_size) return -1;
    if (E.cfg.scen_read && E.cfg.scen_size) return E.cfg.scen_read(off, dst, len);
    memcpy(dst, E.w.sc_mem + off, len);
    return 0;
}

/* Chaîne i dans buf (cap octets, coupée au besoin) ; vide si elle manque */
static const char *str(int i, char *buf, int cap) {
    uint8_t o[4];
    buf[0] = 0;
    if (i >= E.w.sc_nstr || sc_read(E.w.sc_idx + 4u * (uint32_t)i, o, 4)) return buf;
    const uint32_t off = rd32(o);
    uint32_t n = (uint32_t)cap - 1;
    if (off >= E.w.sc_size) return buf;
    if (n > E.w.sc_size - off) n = E.w.sc_size - off;
    if (sc_read(off, buf, n)) { buf[0] = 0; return buf; }
    buf[n] = 0;
    /* Coupée au milieu d'un caractère UTF-8 : on retire le caractère entamé */
    size_t l = strlen(buf);
    if (l == n) { while (l && ((uint8_t)buf[l - 1] & 0xC0) == 0x80) l--; if (l && ((uint8_t)buf[l - 1] & 0xC0) == 0xC0) l--; buf[l] = 0; }
    return buf;
}

/* Objets de l'Atelier (16 octets chacun) : remplacent ceux d'origine, au nombre de départ ; noms et descriptions copiés ;
   tenue (type 4) : PV = chaîne du nom de la tenue */
static void load_items(uint32_t n, uint32_t off) {
    if (!n) return;
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    char *txt = E.cfg.alloc(n * 108, 0);   /* nom 24 + description 64 + tenue 20 */
    if (!txt) return;
    n_items = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint8_t q[16];
        if (sc_read(off + k * 16, q, 16)) break;
        item_t *it = &ITEMS[k];
        char *nm = txt + k * 108, *ds = nm + 24, *tn = ds + 64;
        it->name = str(rd16(q), nm, 24); it->desc = str(rd16(q + 2), ds, 64);
        it->type = q[4]; it->butin = q[5] & 1; it->team = (q[5] >> 1) & 1; it->adulte = (q[5] >> 2) & 1; it->releve = (q[5] >> 3) & 1; it->count = q[6]; it->icon = q[7]; it->hp = rd16(q + 8); it->mp = rd16(q + 10); it->prix = (int)rd32(q + 12);
        it->noyau = (q[5] >> 5 & 1) && it->mp < MAGIE_N ? it->mp + 1 : 0;   /* noyau élémentaire (08/11/2026) : drapeau 32, PM = son élément */
        it->tenue = NULL;
        /* tenue (type 4), ou armure qui se voit (drapeau 16 : bikini, 03/11/2026) : PV = chaîne du nom de sa tenue */
        if (it->type == 4 || (q[5] & 16)) { it->tenue = str(it->hp, tn, 20); it->hp = 0; }
        n_items = (int)k + 1;
    }
}

/* Plan de musique : pistes par défaut, des combats, de la victoire, de la défaite, des courses (chaînes), zones (x, z,
   rayon en 1/8 de case, piste), pistes des ennemis (personnage, piste), pistes des autres cartes (carte, piste) */
static void load_music(uint32_t off, uint32_t size) {
    world_t *w = &E.w;
    uint8_t h[16];
    if (size < 16 || sc_read(off, h, 16)) return;
    w->mus_plan = 1;
    w->mus_defaut = (uint16_t)rd16(h); w->mus_combat = (uint16_t)rd16(h + 2); w->mus_victoire = (uint16_t)rd16(h + 4);
    w->mus_defaite = (uint16_t)rd16(h + 6); w->mus_course = (uint16_t)rd16(h + 8);
    const int nz = rd16(h + 10), ne = rd16(h + 12), nc = rd16(h + 14);
    for (int k = 0; k < 16; k++) w->mus_carte[k] = 65535;
    if (16u + 8u * nz + 4u * ne > size) return;
    for (int k = 0; k < nc && 16u + 8u * nz + 4u * ne + 4u * (k + 1) <= size; k++) {
        uint8_t q[4];
        if (sc_read(off + 16 + 8 * nz + 4 * ne + 4 * k, q, 4)) break;
        if (q[0] < 16) w->mus_carte[q[0]] = (uint16_t)rd16(q + 2);
    }
    if (nz && (w->mus_z = E.cfg.alloc(sizeof(mus_zone_t) * nz, 0)))
        for (int k = 0; k < nz; k++) {
            uint8_t q[8];
            if (sc_read(off + 16 + 8 * k, q, 8)) break;
            w->mus_z[k] = (mus_zone_t){ rd16(q) / 8.0f, rd16(q + 2) / 8.0f, rd16(q + 4) / 8.0f, (uint16_t)rd16(q + 6) };
            w->mus_nz = k + 1;
        }
    if (ne && (w->mus_e = E.cfg.alloc(sizeof(mus_enn_t) * ne, 0)))
        for (int k = 0; k < ne; k++) {
            uint8_t q[4];
            if (sc_read(off + 16 + 8 * nz + 4 * k, q, 4)) break;
            w->mus_e[k] = (mus_enn_t){ q[0] | q[1] << 8, (uint16_t)rd16(q + 2) };
            w->mus_ne = k + 1;
        }
}

void script_string(int i, char *buf, int cap) { str(i, buf, cap); }

/* En-tête, table des événements (28 octets chacun), les quêtes (8 octets, noms lus maintenant), les objets, puis le plan
   de musique ; mem : section SCEN du paquet (NULL : aucune) */
void script_load(const uint8_t *mem, uint32_t len) {
    world_t *w = &E.w;
    memset(&VM, 0, sizeof VM);
    STEP.q = -1;
    ouvertures_load(0);   /* aucune tant que le scénario n'est pas lu (panneaux.c) */
    w->sc_nev = w->sc_nq = 0;
    w->mus_plan = w->mus_nz = w->mus_ne = 0;
    music_reset_names();
    if (E.cfg.scen_read && E.cfg.scen_size >= SC_HEAD) w->sc_size = E.cfg.scen_size;
    else if (mem && len >= SC_HEAD) { w->sc_mem = mem; w->sc_size = len; }
    else return;
    uint8_t h[SC_HEAD + 4];
    if (sc_read(0, h, SC_HEAD) || memcmp(h, "SCN5", 4)) { w->sc_size = 0; return; }
    /* Table des sujets (u16 par événement, intitulé du choix des sujets) : décalage en h + 52 quand l'en-tête a 56 octets,
       c'est-à-dire quand la première chaîne commence après 52 (scénarios plus anciens : 52 octets, pas de table) */
    uint32_t sujets = 0;
    { uint8_t b[4]; if (!sc_read(rd32(h + 16), b, 4) && rd32(b) >= SC_HEAD + 4 && !sc_read(SC_HEAD, h + SC_HEAD, 4)) sujets = rd32(h + SC_HEAD); }
    /* Table des ouvertures (marques « ! », panneaux de quêtes, 03/11/2026 ; panneaux.c) : décalage en h + 56 quand l'en-tête a
       60 octets (la première chaîne commence après 56) ; plus ancien : aucune (marques de l'étape 0 seulement) */
    uint32_t ouv = 0;
    { uint8_t b[4], c[4]; if (sujets && !sc_read(rd32(h + 16), b, 4) && rd32(b) >= SC_HEAD + 8 && !sc_read(SC_HEAD + 4, c, 4)) ouv = rd32(c); }
    uint32_t nstr = rd32(h + 8), nev = rd32(h + 12), evt = rd32(h + 20), code_max = rd32(h + 24), nq = rd32(h + 28), qt = rd32(h + 32);
    w->sc_nstr = nstr > 65535 ? 65535 : (int)nstr; w->sc_idx = rd32(h + 16);
    str(0, w->sc_player, sizeof w->sc_player);
    if (nq > SC_MAX_QUESTS) nq = SC_MAX_QUESTS;
    if (nq && (w->sc_q = E.cfg.alloc(sizeof(sc_quest_t) * nq, 0))) {
        for (uint32_t k = 0; k < nq; k++) {
            uint8_t q[8];
            if (sc_read(qt + k * 8, q, 8)) break;
            sc_quest_t *Q = &w->sc_q[k];
            str(rd16(q), Q->nom, sizeof Q->nom); Q->type = q[2]; Q->nsteps = q[3]; Q->steps_off = rd32(q + 4); Q->c_st = -1;
            w->sc_nq = (int)k + 1;
        }
    }
    load_items(rd32(h + 36), rd32(h + 40));
    load_music(rd32(h + 44), rd32(h + 48));
    if (nev > SC_MAX_EVENTS) nev = SC_MAX_EVENTS;
    if (code_max > 65535) code_max = 65535;
    if (!nev || !(w->sc_ev = E.cfg.alloc(sizeof(sc_event_t) * nev, 0)) || !(w->sc_code = E.cfg.alloc(code_max ? code_max : 1, 0))) return;
    for (uint32_t k = 0; k < nev; k++) {
        uint8_t q[SC_ENTRY];
        if (sc_read(evt + k * SC_ENTRY, q, SC_ENTRY)) break;
        sc_event_t *ev = &w->sc_ev[k];
        ev->trigger = q[0]; ev->perso = q[1]; ev->flags = q[2]; ev->step = q[3]; ev->cond = rd16(q + 4); ev->len = rd16(q + 6);
        ev->x = rdf(q + 8); ev->z = rdf(q + 12); ev->r = rdf(q + 16); ev->code_off = rd32(q + 20); ev->quest = rd16(q + 24); ev->carte = q[26];
        ev->sujet = 65535;
        if (sujets) { uint8_t b[2]; if (!sc_read(sujets + k * 2, b, 2)) ev->sujet = (uint16_t)rd16(b); }
        if ((uint32_t)ev->len > code_max) ev->len = 0;
        w->sc_nev = (int)k + 1;
    }
    ouvertures_load(ouv);
}
int script_lire(uint32_t off, void *dst, uint32_t len) { return sc_read(off, dst, len); }

/* ---------- Quêtes ---------- */

int script_quest_count(void) { return E.w.sc_nq; }
const char *script_quest_name(int q) { return q >= 0 && q < E.w.sc_nq ? E.w.sc_q[q].nom : ""; }
int script_quest_type(int q) { return q >= 0 && q < E.w.sc_nq ? E.w.sc_q[q].type : 0; }
int script_quest_steps(int q) { return q >= 0 && q < E.w.sc_nq ? E.w.sc_q[q].nsteps : 0; }
/* Niveau conseillé de la quête q (01/11/2026, demande de l'utilisateur) : celui de son étape en cours, la première si
   elle n'a pas commencé, la dernière si elle est finie ; 0 : aucun. Lu à part (sans toucher à STEP, l'étape suivie) et
   gardé : le menu QUÊTES en montre plusieurs à chaque image, la carte SD n'est relue qu'au changement (stepNiveau de
   l'émulateur) */
#define NIV_CACHE 16
static struct { int16_t q; uint8_t st; uint8_t niv; } niv_cache[NIV_CACHE];
static int niv_suiv;
int script_quest_niveau(int q) {
    if (q < 0 || q >= E.w.sc_nq) return 0;
    const int n = E.w.sc_q[q].nsteps, v = E.quests[q], st = v == Q_DONE ? n : v >= 1 && v <= n ? v : 1;
    if (st < 1 || st > n) return 0;
    for (int i = 0; i < NIV_CACHE; i++) if (niv_cache[i].q == q + 1 && niv_cache[i].st == st) return niv_cache[i].niv;
    uint8_t s[8];
    if (sc_read(E.w.sc_q[q].steps_off + 8u * (uint32_t)(st - 1), s, 8)) return 0;
    const int niv = (rd16(s + 6) & 0xfff) > NIV_MAX ? NIV_MAX : rd16(s + 6) & 0xfff;
    niv_cache[niv_suiv].q = (int16_t)(q + 1); niv_cache[niv_suiv].st = (uint8_t)st; niv_cache[niv_suiv].niv = (uint8_t)niv;
    niv_suiv = (niv_suiv + 1) % NIV_CACHE;
    return niv;
}

/* Étape st de la quête q dans STEP (objectif, cible), lue si ce n'est pas déjà la dernière lue ; 0 si elle n'existe pas */
static int step_load(int q, int st) {
    if (q < 0 || q >= E.w.sc_nq || st < 1 || st > E.w.sc_q[q].nsteps) return 0;
    if (STEP.q == q && STEP.st == st) return STEP.ok;
    uint8_t s[8];
    STEP.q = q; STEP.st = st; STEP.ok = 0; STEP.text[0] = 0;
    if (sc_read(E.w.sc_q[q].steps_off + 8u * (uint32_t)(st - 1), s, 8)) return 0;
    str(rd16(s), STEP.text, sizeof STEP.text); STEP.x = (uint16_t)rd16(s + 2); STEP.z = (uint16_t)rd16(s + 4);
    STEP.niv = (rd16(s + 6) & 0xfff) > NIV_MAX ? NIV_MAX : rd16(s + 6) & 0xfff; STEP.carte = rd16(s + 6) >> 12;   /* bits 12 à 15 : carte de la cible */
    return STEP.ok = 1;
}
const char *script_quest_text(int q, int st) { return step_load(q, st) ? STEP.text : ""; }

/* Niveau et cible de l'étape st de la quête q, gardés dans la quête (une lecture de la carte SD par changement d'étape) */
static int step_niv(int q, int st, uint16_t *x, uint16_t *z, int *carte) {
    sc_quest_t *Q = &E.w.sc_q[q];
    if (Q->c_st != st) {
        Q->c_st = st; Q->c_niv = 0; Q->c_x = Q->c_z = 65535; Q->c_carte = 0;
        if (step_load(q, st)) { Q->c_niv = STEP.niv; Q->c_x = STEP.x; Q->c_z = STEP.z; Q->c_carte = STEP.carte; }
    }
    if (x) { *x = Q->c_x; *z = Q->c_z; *carte = Q->c_carte; }
    return Q->c_niv;
}
int script_step_niveau(int q, int st) { return q >= 0 && q < E.w.sc_nq && st >= 1 && st <= E.w.sc_q[q].nsteps ? step_niv(q, st, NULL, NULL, NULL) : 0; }
/* Cible de l'étape st de la quête q (cases, carte) ; 0 : aucune (lieux visités d'une ancienne sauvegarde, hud.c) */
int script_step_cible(int q, int st, float *x, float *z, int *carte) {
    if (!step_load(q, st) || STEP.x == 65535) return 0;
    *x = STEP.x / 8.0f; *z = STEP.z / 8.0f; *carte = STEP.carte;
    return 1;
}

static int active(int q) { return E.quests[q] >= 1 && E.quests[q] < Q_DONE; }

int script_niveau_histoire(void) {
    int n = 0;
    for (int q = 0; q < E.w.sc_nq; q++) {   /* quêtes principales : l'étape en cours, la dernière d'une terminée */
        if (E.w.sc_q[q].type != 0 || !E.quests[q]) continue;
        const int v = script_step_niveau(q, E.quests[q] == Q_DONE ? E.w.sc_q[q].nsteps : E.quests[q]);
        if (v > n) n = v;
    }
    return n;
}
int script_niveau(float x, float z) {
    int n = script_niveau_histoire();
    for (int q = 0; q < E.w.sc_nq; q++) {   /* quête en cours dont l'objectif est proche : son niveau s'il est plus haut */
        if (!active(q) || E.quests[q] > E.w.sc_q[q].nsteps) continue;
        uint16_t ox, oz; int oc;
        const int v = step_niv(q, E.quests[q], &ox, &oz, &oc);
        if (v > n && ox != 65535 && oc == E.w.carte && hypotf(ox / 8.0f - x, oz / 8.0f - z) < NIV_RAYON) n = v;
    }
    return n || !E.w.ile[0] ? n : ile_niveau(x, z);   /* île sans quête en cours : le niveau de sa région (ile.c) */
}
/* Quête en cours à suivre : la première principale, sinon la première secondaire (-1 : aucune) */
static int next_active(void) {
    for (int q = 0; q < E.w.sc_nq; q++) if (active(q) && E.w.sc_q[q].type == 0) return q;
    for (int q = 0; q < E.w.sc_nq; q++) if (active(q)) return q;
    return -1;
}
void script_track(int q) { if (q >= 0 && q < E.w.sc_nq && active(q)) { E.qtrack = q; E.obj[0] = E.obj[1] = 65535; } }

/* Personne à aller voir (01/11/2026 : le repère restait sur le point fixe de l'étape, pas sur elle) : pour un événement
   « parler » possible de l'étape suivie qui la fait avancer (à usage unique : pas les rappels répétables, comme celui
   d'Odric qui renvoie vers Kess et attirait le repère), son acteur s'il est en scène, sinon son exemplaire présent le
   plus proche du point de l'étape (x, z), à moins de OBJ_PERS_R cases, qu'elle remplace (objectivePerso de l'émulateur) */
#define OBJ_PERS_R 48.0f
static int ev_ok(int k);
static int ev_quand(const sc_event_t *ev) {
    if ((ev->flags & SC_NUIT) && !(E.heure >= ECHO_NUIT_A || E.heure < ECHO_NUIT_B)) return 0;
    if ((ev->flags & SC_PLUIE) && E.rain < ECHO_PLUIE) return 0;
    return 1;
}
static void objective_perso(int q, float *x, float *z) {
    const float px = *x, pz = *z;
    float bd = 1e9f;
    for (int k = 0; k < E.w.sc_nev; k++) {
        const sc_event_t *ev = &E.w.sc_ev[k];
        if (ev->trigger != 2 || ev->quest != q || !(ev->flags & SC_ONCE) || !ev_ok(k)) continue;
        int acteur = 0;
        for (int a = 0; a < ACT_MAX; a++) {
            const acteur_t *A = &MISE.act[a];
            if (!A->on || A->chasse || A->perso != ev->perso) continue;
            acteur = 1;
            const float d = hypotf(A->x - px, A->z - pz);
            if (d < bd && d < OBJ_PERS_R) { bd = d; *x = A->x; *z = A->z; }
        }
        if (acteur) continue;   /* un acteur en scène : son personnage n'est pas dessiné ailleurs */
        for (int i = 0; i < E.nnpc; i++) {
            const npc_t *n = &E.npcs[i];
            if (n->perso != ev->perso || !npc_present(n)) continue;
            const float d = hypotf(n->x - px, n->z - pz);
            if (d < bd && d < OBJ_PERS_R) { bd = d; *x = n->x; *z = n->z; }
        }
    }
}

/* Cible sur une autre carte (07/11/2026, demande de l'utilisateur : remontée à la surface, plus d'indicateur) : le passage ouvert le plus proche de Sera, sur le plus court chemin de cartes jusqu'à elle
   (puits, ascenseur, aire d'envol, approche d'un astre) ; 0 : aucun chemin ouvert. passageVers de l'émulateur */
#define PASS_CARTES 32
int passage_vers(int carte, float *x, float *z) {
    const world_t *w = &E.w;
    if (carte == w->carte || carte < 0 || carte >= PASS_CARTES || !w->npass || w->carte >= PASS_CARTES) return 0;
    int8_t d[PASS_CARTES];
    memset(d, -1, sizeof d);
    d[carte] = 0;
    for (int tour = 0; tour < w->ncartes; tour++)
        for (int k = 0; k < w->npass; k++) {
            const passage_t *q = &w->pass[k];
            if (q->carte >= PASS_CARTES || q->vers >= PASS_CARTES || d[q->vers] < 0 || (q->cond < SC_MAX_FLAGS && !flag(q->cond))) continue;
            if (d[q->carte] < 0 || d[q->carte] > d[q->vers] + 1) d[q->carte] = (int8_t)(d[q->vers] + 1);
        }
    if (d[w->carte] < 0) return 0;
    float bd = 1e9f; int ok = 0;
    for (int k = 0; k < w->npass; k++) {
        const passage_t *q = &w->pass[k];
        if (q->carte != w->carte || q->vers >= PASS_CARTES || d[q->vers] != d[w->carte] - 1 || (q->cond < SC_MAX_FLAGS && !flag(q->cond))) continue;
        const float dd = hypotf(q->x - E.sera.x, q->z - E.sera.z);
        if (dd < bd) { bd = dd; *x = q->x; *z = q->z; ok = 1; }
    }
    return ok;
}
int objective_pos(float *x, float *z) {
    if (MISE.fuite_on) { *x = MISE.fuite_x; *z = MISE.fuite_z; return 1; }   /* fuite : le cercle à atteindre */
    if (E.obj[0] != 65535) {   /* objectif du script : sur la carte où il a été posé */
        if (E.obj_carte != E.w.carte) return passage_vers(E.obj_carte, x, z);
        *x = E.obj[0] / 8.0f; *z = E.obj[1] / 8.0f;
        if (E.qtrack >= 0 && E.qtrack < E.w.sc_nq) objective_perso(E.qtrack, x, z);
        return 1;
    }
    const int q = E.qtrack;
    if (q < 0 || q >= E.w.sc_nq || !step_load(q, E.quests[q]) || STEP.x == 65535) return 0;
    if (STEP.carte != E.w.carte) return passage_vers(STEP.carte, x, z);   /* cible sur une autre carte : le passage à prendre */
    *x = STEP.x / 8.0f; *z = STEP.z / 8.0f;
    objective_perso(q, x, z);
    return 1;
}
/* Carte du menu (04/11/2026, demande de l'utilisateur : « afficher les objectifs sur la carte ») : la cible de l'étape en
   cours de chaque quête ouverte, sur la carte où est Sera, hors de la quête suivie (son repère est objective_pos) ; la
   personne à aller voir à sa place (objective_perso : OBJ_PERSO, sa marque « ? » la montre déjà), un seul point par endroit
   (à moins d'une case d'un autre). Cibles gardées dans la quête (step_niv : une lecture de la carte SD par changement
   d'étape), rien de lu à chaque image (objectifsCarte de l'émulateur) */
int script_objectifs(float *x, float *z, uint8_t *genre, int n) {
    int c = 0;
    for (int q = 0; q < E.w.sc_nq && c < n; q++) {
        if (q == E.qtrack || !active(q) || E.quests[q] > E.w.sc_q[q].nsteps) continue;
        uint16_t ox, oz; int oc;
        step_niv(q, E.quests[q], &ox, &oz, &oc);
        if (ox == 65535 || oc != E.w.carte) continue;
        float px = ox / 8.0f, pz = oz / 8.0f;
        const float x0 = px, z0 = pz;
        objective_perso(q, &px, &pz);
        int dbl = 0;
        for (int i = 0; i < c && !dbl; i++) dbl = fabsf(x[i] - px) < 1 && fabsf(z[i] - pz) < 1;
        if (dbl) continue;
        x[c] = px; z[c] = pz;
        genre[c++] = (uint8_t)((E.w.sc_q[q].type == 0 ? OBJ_PRINC : 0) | (px != x0 || pz != z0 ? OBJ_PERSO : 0));
    }
    return c;
}

/* Nouvel état de la quête q (254 : étape suivante, après la dernière : terminée) : annonce, quête suivie, guidage */
static void set_quest(int q, int v) {
    if (q >= E.w.sc_nq) return;
    const int old = E.quests[q], n = E.w.sc_q[q].nsteps;
    const int st = v == 254 ? (old >= n ? Q_DONE : old + 1) : v;
    if (st == old) return;
    E.quests[q] = (uint8_t)st; E.obj[0] = E.obj[1] = 65535;
    if (E.w.sc_q[q].type == 0) hud_sauve_demande(AUTO_ETAPE);   /* quête principale : point de sauvegarde après la scène */
    /* Zones des événements de cette quête réarmées : Sera déjà dans le cercle d'un événement qui devient possible, il part
       au prochain tour (setQuest de l'émulateur) */
    for (int k = 0; k < E.w.sc_nev; k++) if (E.w.sc_ev[k].quest == q) set_bit(VM.inzone, k, 0);
    const char *nom = E.w.sc_q[q].nom;
    char kind[64];
    if (st == Q_DONE) hud_annonce("QUÊTE TERMINÉE", nom, "");
    else if (st && !old) {   /* avec son niveau conseillé */
        const int niv = script_quest_niveau(q);
        if (niv > 0) snprintf(kind, sizeof kind, "NOUVELLE QUÊTE · NIV. %d", niv); else snprintf(kind, sizeof kind, "NOUVELLE QUÊTE");
        hud_annonce(kind, nom, script_quest_text(q, st));
    }
    else if (st) { snprintf(kind, sizeof kind, "OBJECTIF · %s", nom); hud_annonce(kind, script_quest_text(q, st), ""); }
    /* Suivie : une quête qui commence si rien n'est suivi (une principale toujours) ; terminée, la suivante en cours */
    if (active(q) && (E.qtrack < 0 || !active(E.qtrack) || (E.w.sc_q[q].type == 0 && q != E.qtrack))) E.qtrack = q;
    if (!active(q) && E.qtrack == q) E.qtrack = next_active();
}

/* ---------- Machine ---------- */

/* L'événement k peut-il partir : pas déjà fait s'il ne vaut qu'une fois, condition sur un drapeau remplie, sa quête à
   son étape */
static int ev_ok(int k) {
    const sc_event_t *ev = &E.w.sc_ev[k];
    if ((ev->flags & SC_ONCE) && bit(E.ev_done, k)) return 0;
    if ((ev->flags & SC_ADULTE) && verrou18()) return 0;   /* événement 18+ : pas tant que le 18+ est verrouillé */
    if (!ev_quand(ev)) return 0;
    if (ev->quest < E.w.sc_nq && E.quests[ev->quest] != ev->step) return 0;
    if (ev->quest != 65535 && ev->quest >= E.w.sc_nq) return 0;
    if (ev->cond >= SC_MAX_FLAGS) return 1;
    return flag(ev->cond) != ((ev->flags & SC_NEG) != 0);
}

/* Le code c (len octets) contient-il un combat (BATTLE, BATTLE2) ? Parcours instruction par instruction, aux longueurs de
   exec (codeCombat de l'émulateur) ; une instruction inconnue arrête le parcours */
static int code_combat(const uint8_t *c, int len) {
    for (int pc = 0; pc < len;) {
        int n;
        switch (c[pc]) {
        case OP_BATTLE: case OP_BATTLE2: return 1;
        case OP_HEAL: case OP_WAIT_WALK: n = 1; break;
        case OP_WEATHER: case OP_POLICE: n = 2; break;
        case OP_WAIT: case OP_SET: case OP_CLR: case OP_JMP: case OP_GIVE: case OP_PARTY: case OP_EXP: case OP_MUSIC: case OP_IMAGE:
        case OP_HOUR: case OP_JMP_IF_NOT_FLEE: case OP_JMP_IF_FLEE: case OP_JMP_IF_NOT_WANTED: case OP_JMP_IF_WANTED: n = 3; break;
        case OP_QUEST: case OP_JMP_IF_NOT_RACE: case OP_JMP_IF_RACE: case OP_JMP_IF_NOT_CHOICE: case OP_JMP_IF_CHOICE: case OP_JMP_IF_NOT_HEROINE:
        case OP_JMP_IF_HEROINE: case OP_POSE: case OP_OUTFIT: case OP_JOB: case OP_LOOK: case OP_FADE: case OP_SHAKE: case OP_PARTY2: n = 4; break;
        case OP_SAY: case OP_JMP_IF_NOT: case OP_JMP_IF: case OP_JMP_IF_FEWER: case OP_JMP_IF_ENOUGH: case OP_TITLE: case OP_OBJECTIVE:
        case OP_TELEPORT: case OP_RACE: case OP_CREDITS: n = 5; break;
        case OP_JMP_IF_QUEST_LT: case OP_JMP_IF_QUEST_GE: case OP_VOYAGE: n = 6; break;
        case OP_JMP_IF_CREDITS_LT: case OP_JMP_IF_CREDITS_GE: n = 7; break;
        case OP_ACTOR: case OP_WALK: n = 8; break;
        case OP_FLEE: n = 10; break;
        case OP_CAMERA: n = 12; break;
        case OP_CHOICE: n = pc + 1 < len ? 2 + 2 * c[pc + 1] : 2; break;
        case OP_RACE2: n = pc + 10 < len ? 11 + (c[pc + 10] == 255 ? 0 : c[pc + 10]) : 11; break;
        default: return 0;   /* OP_END, ou inconnue */
        }
        pc += n;
    }
    return 0;
}
/* L'événement k contient-il un combat ? (essais : rendu_test ; son code est lu dans sc_code, rien ne doit tourner) */
/* Échos à chercher : voir internal.h ; echoProche de l'émulateur */
int script_echo_proche(float *x, float *z, float *d, int *pret, int *nuit) {
    int best = -1; float bd = ECHO_R;
    for (int k = 0; k < E.w.sc_nev; k++) {
        const sc_event_t *ev = &E.w.sc_ev[k];
        if (!(ev->flags & SC_ECHO) || ev->carte != E.w.carte || bit(E.ev_done, k)) continue;
        if (ev->cond < SC_MAX_FLAGS && flag(ev->cond) == ((ev->flags & SC_NEG) != 0)) continue;
        const float dd = hypotf(ev->x - E.sera.x, ev->z - E.sera.z);
        if (dd < bd) { bd = dd; best = k; }
    }
    if (best < 0) return 0;
    *x = E.w.sc_ev[best].x; *z = E.w.sc_ev[best].z; *d = bd; *pret = ev_quand(&E.w.sc_ev[best]); *nuit = (E.w.sc_ev[best].flags & SC_NUIT) != 0;
    return 1;
}
int script_cible_active(const cible_t *c) {
    if (c->quest & 0x8000) return ev_ok(c->quest & 0x7FFF);
    return c->quest < E.w.sc_nq && E.quests[c->quest] == c->step;
}
int script_ev_ouvert(int k) { const sc_event_t *ev = k >= 0 && k < E.w.sc_nev ? &E.w.sc_ev[k] : NULL; return ev && (ev->cond >= SC_MAX_FLAGS || flag(ev->cond) != ((ev->flags & SC_NEG) != 0)); }
int script_ev_fait(int k) { return k >= 0 && k < E.w.sc_nev && bit(E.ev_done, k); }
/* La première réplique de l'événement k (la voix d'un Écho) ; son code est lu dans sc_code, rien ne doit tourner */
int script_ev_texte(int k, char *txt, int n) {
    if (VM.on || k < 0 || k >= E.w.sc_nev || sc_read(E.w.sc_ev[k].code_off, E.w.sc_code, (uint32_t)E.w.sc_ev[k].len)) return 0;
    if (E.w.sc_ev[k].len < 5 || E.w.sc_code[0] != OP_SAY) return 0;
    str(rd16(E.w.sc_code + 3), txt, n);
    return 1;
}
int script_ev_combat(int k) {
    if (VM.on || k < 0 || k >= E.w.sc_nev || sc_read(E.w.sc_ev[k].code_off, E.w.sc_code, (uint32_t)E.w.sc_ev[k].len)) return 0;
    return code_combat(E.w.sc_code, E.w.sc_ev[k].len);
}
/* Lance l'événement k : son code est lu dans sc_code (illisible : rien ne part). Un événement qui contient un combat :
   sauvegarde automatique juste avant (hud_sauve_auto : rien n'en est fait ; une défaite qui recharge ce point remet Sera
   dans sa zone, qui repart quand elle y revient après en être sortie, ou devant le personnage à qui parler) */
/* Caméra de scène sur un personnage (07/11/2026, revue des caméras) : le point écrit est le lieu du personnage, mais le placement l'en écarte (hors de la chaussée, flânerie) ;
   à 1,4 case de là, il était derrière un portrait du dialogue. Le point devient sa place s'il est à moins de CAM_AIMANT, et si c'est un personnage à qui l'on parle dans
   l'histoire (pas un passant) ; pas de correction près d'un acteur (scène mise en place). camAimant de l'émulateur */
static void cam_aimant(float *x, float *z) {
    for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].on && !MISE.act[k].chasse && hypotf(MISE.act[k].x - *x, MISE.act[k].z - *z) < CAM_ACTEUR) return;
    float best = CAM_AIMANT, bx = *x, bz = *z;
    for (int q = 0; q < E.nnpc; q++) {
        const npc_t *n = &E.npcs[q];
        const float d = hypotf(n->x - *x, n->z - *z);
        if (d >= best || !npc_present(n)) continue;
        for (int k = 0; k < E.w.sc_nev; k++) if (E.w.sc_ev[k].trigger == 2 && E.w.sc_ev[k].perso == n->perso) { best = d; bx = n->x; bz = n->z; break; }
    }
    *x = bx; *z = bz;
}
#ifndef ESP_PLATFORM
static int run(int k);
int script_lancer_essai(int k) { return !VM.on && k >= 0 && k < E.w.sc_nev && run(k); }   /* rendu_test (scenecams) : l'événement k, sans ses conditions */
#endif
static int run(int k) {
    const sc_event_t *ev = &E.w.sc_ev[k];
    if (sc_read(ev->code_off, E.w.sc_code, (uint32_t)ev->len)) return 0;
    if (code_combat(E.w.sc_code, ev->len)) hud_sauve_auto(1);
    VM.on = 1; VM.ev = k; VM.pc = 0; VM.wait = 0; VM.marche = -1; VM.fuite = 0; VM.appr = 0; E.has_dest = 0;
    scene_cam_sortir();
    return 1;
}

static void finish(void) {
    if (E.w.sc_ev[VM.ev].flags & SC_ONCE) set_bit(E.ev_done, VM.ev, 1);
    VM.on = 0; E.scene = 0;
    mise_fin(0);   /* acteurs (sauf gardés), caméra, fondu */
}

/* Exécute jusqu'à la prochaine attente (dialogue, pause, combat) ou la fin du script */
static void exec(void) {
    const sc_event_t *ev = &E.w.sc_ev[VM.ev];
    const uint8_t *c = E.w.sc_code;
    char t[48];
    for (int guard = 0; VM.on && guard < 256; guard++) {
        const int pc = VM.pc;
        if (pc >= ev->len) { finish(); return; }
        switch (c[pc]) {
        case OP_SAY:
            str(rd16(c + pc + 1), VM.who, sizeof VM.who); str(rd16(c + pc + 3), VM.text, sizeof VM.text);
            if (VM.appr != pc + 1 && scene_approche(VM.who)) { VM.appr = pc + 1; VM.marche = 255; return; }   /* trop loin pour se parler : Sera y va d'abord, une fois */
            VM.pc += 5;
            if (strcmp(VM.who, E.w.sc_player) != 0) snprintf(VM.right, sizeof VM.right, "%s", VM.who);
            scene_regards(VM.who);
            hud_say(VM.who, VM.text, VM.right);
            return;
        case OP_WAIT: VM.wait = rd16(c + pc + 1) / 100.0f; VM.pc += 3; return;
        case OP_SET: if (rd16(c + pc + 1) < SC_MAX_FLAGS) set_bit(E.flags, rd16(c + pc + 1), 1); VM.pc += 3; break;
        case OP_CLR: if (rd16(c + pc + 1) < SC_MAX_FLAGS) set_bit(E.flags, rd16(c + pc + 1), 0); VM.pc += 3; break;
        case OP_JMP_IF_NOT: case OP_JMP_IF:   /* drapeau u16, adresse u16 */
            VM.pc = flag(rd16(c + pc + 1)) == (c[pc] == OP_JMP_IF) ? rd16(c + pc + 3) : pc + 5;
            break;
        case OP_JMP: VM.pc = rd16(c + pc + 1); break;
        case OP_GIVE: {
            const int i = c[pc + 1], n = (int8_t)c[pc + 2];
            VM.pc += 3;
            if (i < n_items) {
                int v = ITEMS[i].count + n;
                ITEMS[i].count = v < 0 ? 0 : v > 99 ? 99 : v;
                snprintf(t, sizeof t, "%+d %s", n, ITEMS[i].name);
                hud_toast(t);
            }
            break;
        }
        case OP_BATTLE: {   /* ennemi de la bibliothèque (index PERS) ; sans fiche de combat, pas de combat */
            const int p = c[pc + 1];
            VM.pc += 2;
            /* au niveau de l'étape de sa quête (sans niveau : celui de sa fiche) */
            const int niv = ev->quest < E.w.sc_nq ? script_step_niveau(ev->quest, ev->step) : 0;
            /* au plus au niveau de l'équipe (BOSS_ECART) : une équipe en retard n'est pas écrasée, une équipe à niveau
               garde le défi prévu (startBattle de l'émulateur) */
            for (int e = 0; e < E.w.nenemies; e++) if (E.w.enemies[e].perso == p) {
                int L = niv > 0 ? niv : E.w.enemies[e].lvl;
                if (L > E.level + BOSS_ECART) L = E.level + BOSS_ECART;
                battle_start(e, L, (script_quest_type(ev->quest) || ev->quest >= E.w.sc_nq) ? 2 : 1); return;   /* 2 : une annexe (BOSS_RW_ANNEXE) */
            }
            break;
        }
        case OP_BATTLE2: {   /* groupe : comme OP_BATTLE, au niveau de l'étape (sinon de la fiche du chef) ; sans fiche pour le chef, pas de combat */
            const int mode = c[pc + 1], n = c[pc + 2] > BT_EMAX ? BT_EMAX : c[pc + 2], lg = mode & 4 ? 2 : 1;   /* bit 2 : personnages sur deux octets */
            VM.pc += 3 + c[pc + 2] * lg;
            const int niv = ev->quest < E.w.sc_nq ? script_step_niveau(ev->quest, ev->step) : 0;
            int ks[BT_EMAX], nk = 0;
            for (int i = 0; i < n; i++)
                for (int e = 0; e < E.w.nenemies; e++) if (E.w.enemies[e].perso == (c[pc + 3 + i * lg] | (lg > 1 ? c[pc + 4 + i * lg] << 8 : 0))) { ks[nk++] = e; break; }
            if (!nk || E.w.enemies[ks[0]].perso != (c[pc + 3] | (lg > 1 ? c[pc + 4] << 8 : 0))) break;
            int L = niv > 0 ? niv : E.w.enemies[ks[0]].lvl;
            if (L > E.level + BOSS_ECART) L = E.level + BOSS_ECART;
            battle_start_groupe(ks, nk, L, (mode & 2) || (script_quest_type(ev->quest) || ev->quest >= E.w.sc_nq) ? 2 : 1, (mode & 1) ? GROUPE_SBIRES : GROUPE_PARTS, NULL);   /* mode : bit 1 « fretin » (gardiens d'un donjon) */
            return;
        }
        case OP_VOYAGE: {   /* carte u8, x, z u16 (1/8 de case) : changer de carte, ou se déplacer sur celle-ci */
            const int k = c[pc + 1];
            const float x = rd16(c + pc + 2) / 8.0f, z = rd16(c + pc + 4) / 8.0f;
            VM.pc += 6;
            int_reset();   /* hors de l'intérieur */
            if (k != E.w.carte && k < E.w.ncartes) world_carte(k, x, z);
            else { E.sera_y_ok = 0; sera_placer(x, z, 6); E.has_dest = 0; E.stream_hold = 1; BT.fade_t = E.clock; }
            break;
        }
        case OP_TELEPORT:
            int_reset();   /* hors de l'intérieur, comme l'émulateur */
            E.sera_y_ok = 0;
            sera_placer(rd16(c + pc + 1) / 8.0f, rd16(c + pc + 3) / 8.0f, 6);   /* à côté d'un garde, pas sur lui */
            E.has_dest = 0; E.stream_hold = 1;   /* fenêtre de chunks recentrée à la mise à jour suivante */
            BT.fade_t = E.clock;                 /* sortie du noir, comme après un combat */
            memset(VM.inzone, 0, sizeof VM.inzone);
            VM.pc += 5;
            return;
        case OP_WEATHER: E.weather = c[pc + 1] % 3 ? c[pc + 1] % 3 : E.meteo_choix; VM.pc += 2; break;   /* auto : le réglage du joueur */
        case OP_HEAL: for (int m = 0; m < NMEMB; m++) { PARTY[m].hp = PARTY[m].max; PARTY[m].mp = PARTY[m].maxmp; } VM.pc += 1; break;
        case OP_JMP_IF_FEWER: case OP_JMP_IF_ENOUGH: {   /* objet, nombre, adresse */
            const int few = (c[pc + 1] < n_items ? ITEMS[c[pc + 1]].count : 0) < c[pc + 2];
            VM.pc = few == (c[pc] == OP_JMP_IF_FEWER) ? rd16(c + pc + 3) : pc + 5;
            break;
        }
        case OP_PARTY: case OP_PARTY2: {   /* mode u8 (0 l'équipe exacte, Sera toujours ; 1 rejoindre ; 2 quitter ; 3 equipe libre),
                                              membres u8 (PARTY2 : u16 ; bit i : PARTY[i]) : equipe_scenario (battle.c) */
            const int mode = c[pc + 1], mask = c[pc] == OP_PARTY2 ? rd16(c + pc + 2) : c[pc + 2];
            VM.pc += c[pc] == OP_PARTY2 ? 4 : 3;
            const int moved = equipe_scenario(mode, mask);
            if (moved) {   /* « JADE, ONYX rejoint l'équipe » ; sans place au combat : en réserve (menu ÉQUIPE) */
                char noms[40], msg[80];
                equipe_noms(moved, noms, sizeof noms);
                snprintf(msg, sizeof msg, "%s %s%s", noms, mode == 1 ? "rejoint l'équipe" : "quitte l'équipe", mode == 1 && (moved & ~E.team) ? " (réserve)" : "");
                hud_toast(msg);
            }
            break;
        }
        case OP_OBJECTIVE: E.obj[0] = (uint16_t)rd16(c + pc + 1); E.obj[1] = (uint16_t)rd16(c + pc + 3); E.obj_carte = E.w.carte; VM.pc += 5; break;
        case OP_TITLE: {
            char sub[64];
            str(rd16(c + pc + 1), VM.text, sizeof VM.text); str(rd16(c + pc + 3), sub, sizeof sub);
            hud_title(VM.text, sub);
            VM.wait = HUD_TITLE_S; VM.pc += 5;
            return;
        }
        case OP_QUEST: set_quest(rd16(c + pc + 1), c[pc + 3]); VM.pc += 4; break;   /* quête u16, état u8 */
        case OP_JMP_IF_QUEST_LT: case OP_JMP_IF_QUEST_GE: {   /* quête u16, état u8, adresse u16 */
            const int q = rd16(c + pc + 1), lt = (q < E.w.sc_nq ? E.quests[q] : 0) < c[pc + 3];
            VM.pc = lt == (c[pc] == OP_JMP_IF_QUEST_LT) ? rd16(c + pc + 4) : pc + 6;
            break;
        }
        case OP_RACE: {   /* menu de course ; sans piste ni véhicule, le script continue (course annulée) */
            const int v[4] = { c[pc + 1], c[pc + 2], c[pc + 3], c[pc + 4] };
            VM.pc += 5;
            VM.race_x = E.sera.x; VM.race_z = E.sera.z; VM.race_yaw = E.yaw;
            if (hud_course_menu(v[0] == 255 ? -1 : v[0], v[1] == 255 ? -1 : v[1], v[2] == 255 ? -1 : v[2], v[3] == 255 ? -1 : v[3])) { VM.race = 1; return; }
            break;
        }
        case OP_RACE2: {   /* course imposée : tours, adversaires, niveau, véhicule u8, tracé u16, armes u8, chrono u16, rivaux u8 (+ n) */
            const int nr = c[pc + 10] == 255 ? -1 : c[pc + 10], tr = rd16(c + pc + 5);
            int riv[COURSE_N - 1];
            char nom[32];
            for (int k = 0; k < nr && k < COURSE_N - 1; k++) riv[k] = c[pc + 11 + k];
            if (tr != 65535) str(tr, nom, sizeof nom);
            VM.race_x = E.sera.x; VM.race_z = E.sera.z; VM.race_yaw = E.yaw;
            VM.pc += 11 + (nr < 0 ? 0 : nr);
            if (hud_course_impose(c[pc + 1] == 255 ? -1 : c[pc + 1], c[pc + 3] == 255 ? -1 : c[pc + 3], c[pc + 4] == 255 ? -1 : c[pc + 4],
                                  tr != 65535 ? nom : NULL, c[pc + 7] == 255 ? -1 : c[pc + 7], rd16(c + pc + 8), riv, nr < 0 ? -1 : nr > COURSE_N - 1 ? COURSE_N - 1 : nr)) {
                VM.race = 1; return;
            }
            break;
        }
        case OP_JMP_IF_NOT_RACE: case OP_JMP_IF_RACE: {   /* « si course 3 » : à la 3e place ou mieux ; 255 : annulée */
            const int n = c[pc + 1], pl = course_place(), ok = n == 255 ? pl == 255 : pl <= n;
            VM.pc = ok == (c[pc] == OP_JMP_IF_RACE) ? rd16(c + pc + 2) : pc + 4;
            break;
        }
        case OP_EXP: {   /* une quête annexe (ou hors quête) : × ANNEXE_XP (11/11/2026) */
            const int n0 = rd16(c + pc + 1), n = script_quest_type(ev->quest) || ev->quest >= E.w.sc_nq ? (int)lroundf(n0 * ANNEXE_XP) : n0;
            VM.pc += 3;
            const int L0 = E.level;
            if (party_gain_exp(n)) snprintf(t, sizeof t, "NIVEAU %d !", E.level); else snprintf(t, sizeof t, "+%d EXP", n);
            hud_toast(t);
            sorts_annoncer(L0);   /* sorts que les jobs apprennent aux niveaux gagnés (03/11/2026) */
            break;
        }
        case OP_CHOICE: {   /* le script attend la réponse (script_choose) */
            char o[4][48];
            const int n = c[pc + 1] > 4 ? 4 : c[pc + 1];
            for (int k = 0; k < n; k++) str(rd16(c + pc + 2 + 2 * k), o[k], sizeof o[k]);
            VM.pc += 2 + 2 * c[pc + 1];
            VM.choice = 0; E.has_dest = 0;
            hud_choice(o, n);
            return;
        }
        case OP_JMP_IF_NOT_CHOICE: case OP_JMP_IF_CHOICE:   /* réponse u8, adresse u16 */
            VM.pc = (VM.choice == c[pc + 1]) == (c[pc] == OP_JMP_IF_CHOICE) ? rd16(c + pc + 2) : pc + 4;
            break;
        case OP_CREDITS: {   /* montant i32 ; la bourse ne descend pas sous 0 */
            const int v = (int32_t)rd32(c + pc + 1);
            VM.pc += 5;
            E.credits = E.credits + v < 0 ? 0 : E.credits + v;
            snprintf(t, sizeof t, "%+d crédits", v);
            hud_toast(t);
            break;
        }
        case OP_JMP_IF_NOT_HEROINE: case OP_JMP_IF_HEROINE: {   /* personnage u8 (index PERS ; 255 : Sera), adresse u16 */
            const int k = c[pc + 1], est = k == 255 ? E.heroine < 0 : E.heroine == k;
            VM.pc = est == (c[pc] == OP_JMP_IF_HEROINE) ? rd16(c + pc + 2) : pc + 4;
            break;
        }
        case OP_IMAGE: {   /* scène u16 (SCNI ; 65535 : aucune) : illustration derrière les répliques qui suivent (hud.c) */
            const int k = rd16(c + pc + 1), avant = E.scene, im = scene_image(k);   /* (la variante de l'héroïne choisie) */
            E.scene = im >= 0 && !(E.w.scene18[k] && verrou18()) ? im + 1 : 0;   /* scène 18+ : rien tant que c'est verrouillé */
            VM.pc += 3;
            if (E.scene && E.scene != avant) { scene_seule = 1; VM.wait = SCENE_S; return; }   /* nouvelle : seule, en plein écran */
            break;
        }
        case OP_POSE:   /* animation u8 (ANIM_*), durée u16 (1/100 s) : Sera la joue sur place (game.c), le script attend */
            E.pose_anim = c[pc + 1] < ANIM_N ? c[pc + 1] : ANIM_IDLE;
            E.pose_t = VM.wait = rd16(c + pc + 2) / 100.0f;
            VM.pc += 4;
            return;
        case OP_JMP_IF_CREDITS_LT: case OP_JMP_IF_CREDITS_GE: {   /* montant u32, adresse u16 */
            const int lt = (uint32_t)E.credits < rd32(c + pc + 1);
            VM.pc = lt == (c[pc] == OP_JMP_IF_CREDITS_LT) ? rd16(c + pc + 5) : pc + 7;
            break;
        }
        case OP_MUSIC: E.mus_force = (uint16_t)rd16(c + pc + 1); VM.pc += 3; break;
        case OP_OUTFIT: {   /* membre u8, tenue u16 (chaîne : son nom ; 65535 : la sienne) */
            const int m = c[pc + 1], s = rd16(c + pc + 2);
            VM.pc += 4;
            if (m < NMEMB) E.tenue_eq[m] = (int8_t)(s == 65535 ? -1 : tenue_named(m, str(s, t, sizeof t)));
            break;
        }
        case OP_JOB: {   /* membre u8, job u8, mode u8 (0 appris et pris, 1 appris seulement) */
            const int m = c[pc + 1], j = c[pc + 2], md = c[pc + 3];
            VM.pc += 4;
            if (m < NMEMB && j < E.w.njobs) {
                /* appris seulement (un maître, 03/11/2026) : annoncé, avec l'endroit où le prendre */
                if (md && !(E.jobs_known[m] >> j & 1)) { char a[48]; snprintf(a, sizeof a, "%s · %s", PARTY[m].name, E.w.jobs[j].nom); hud_annonce("NOUVEAU JOB", a, "Menu ÉQUIP. › JOB"); }
                E.jobs_known[m] |= (uint16_t)(1u << j); if (!md) party_set_job(m, j);
            }
            break;
        }
        case OP_CAMERA: {   /* mode u8 (bit 0 : 0 un point, 1 Sera ; bits 1-7 : hauteur visée au-dessus du sol en 1/8 de case, 0 :
                               celle du jeu ; 05/11/2026), x, z u16 (1/8 de case), lacet u16 (1/10 de degré ; 65535 : celui du
                               moment), zoom u16 (1/100 ; 0 : idem), durée u16 (1/100 s) */
            const int la = rd16(c + pc + 6), zo = rd16(c + pc + 8), h8 = c[pc + 1] >> 1;
            const float d = rd16(c + pc + 10) / 100.0f;
            float cx0 = rd16(c + pc + 2) / 8.0f, cz0 = rd16(c + pc + 4) / 8.0f;
            if (!h8 && !(c[pc + 1] & 1)) cam_aimant(&cx0, &cz0);
            mise_camera((c[pc + 1] & 1) == 1, cx0, cz0, la == 65535 ? -10 : la / 10.0f * PI_F / 180, zo / 100.0f, d);
            if (h8 && !(c[pc + 1] & 1)) MISE.cam_b[4] = h8 / 8.0f;   /* un cadran au fronton, le haut d'une tour… */
            VM.pc += 12;
            if (MISE.cam_on) { VM.wait = MISE.cam_d + CAM_POSE; return; }   /* (sa durée peut être plus longue que celle demandée : mise_camera) ; puis le plan tenu */
            break;
        }
        case OP_ACTOR: {   /* personnage u8, mode u8 (0 apparaître, 1 retirer, 2 garder, 3 retirer tous), x, z u16, direction u8 (255 : vers Sera) */
            const int p = c[pc + 1], md = c[pc + 2], k = acteur_de(p);
            const float x = rd16(c + pc + 3) / 8.0f, z = rd16(c + pc + 5) / 8.0f;
            VM.pc += 8;
            if (md == 0) acteur_poser(p, x, z, c[pc + 7] == 255 ? atan2f(E.sera.x - x, E.sera.z - z) : c[pc + 7] * PI_F / 4);
            else if (md == 1) { if (k >= 0) MISE.act[k].on = 0; }
            else if (md == 2) { if (k >= 0) MISE.act[k].garder = 1; }
            else for (int a = 0; a < ACT_MAX; a++) MISE.act[a].on = 0;
            break;
        }
        case OP_WALK: {   /* personnage u8 (255 : Sera), x, z u16, vitesse u8 (1/10), drapeaux u8 (1 : sans attendre) */
            const int p = c[pc + 1];
            const float x = rd16(c + pc + 2) / 8.0f, z = rd16(c + pc + 4) / 8.0f, v = c[pc + 6] / 10.0f;
            int qui = -1;
            VM.pc += 8;
            if (p == 255) { if (E.ride < 0 && !CR.on) { mise_marcher_sera(x, z, v); qui = 255; } }
            else if ((qui = acteur_de(p)) >= 0) mise_marcher_acteur(qui, x, z, v);
            if (qui != -1 && !(c[pc + 7] & 1)) { VM.marche = qui; return; }
            break;
        }
        case OP_WAIT_WALK: VM.pc += 1; VM.marche = -2; return;
        case OP_LOOK: {   /* personnage u8 (255 : Sera), mode u8 (0 vers un personnage, 1 une direction), cible u8 */
            const int p = c[pc + 1], md = c[pc + 2], cb = c[pc + 3], k = p == 255 ? -1 : acteur_de(p);
            float *dir = p == 255 ? &E.sera.dir : k >= 0 ? &MISE.act[k].dir : NULL;
            const float ox = p == 255 ? E.sera.x : k >= 0 ? MISE.act[k].x : 0, oz = p == 255 ? E.sera.z : k >= 0 ? MISE.act[k].z : 0;
            VM.pc += 4;
            if (!dir) break;
            if (md == 1) *dir = cb * PI_F / 4;
            else if (cb == 255) *dir = atan2f(E.sera.x - ox, E.sera.z - oz);
            else { const int kt = acteur_de(cb); if (kt >= 0) *dir = atan2f(MISE.act[kt].x - ox, MISE.act[kt].z - oz); }
            break;
        }
        case OP_FADE: {   /* mode u8 (0 au noir, 1 retour), durée u16 (1/100 s) : le script attend la fin */
            const float d = rd16(c + pc + 2) / 100.0f;
            MISE.fondu_a = MISE.fondu; MISE.fondu_b = c[pc + 1] ? 0 : 1; MISE.fondu_t = 0; MISE.fondu_d = d > 0.01f ? d : 0.01f;
            VM.pc += 4; VM.wait = d;
            return;
        }
        case OP_SHAKE: MISE.sec_f = c[pc + 1] / 10.0f; MISE.sec_t = rd16(c + pc + 2) / 100.0f; VM.pc += 4; break;   /* force u8 (1/10), durée u16 */
        case OP_FLEE:   /* x, z u16, rayon u8 (1/4 de case), temps u16 (1/10 s), poursuivant u8 (255 : aucun), nombre u8 */
            mise_fuite(rd16(c + pc + 1) / 8.0f, rd16(c + pc + 3) / 8.0f, c[pc + 5] / 4.0f, rd16(c + pc + 6) / 10.0f, c[pc + 8] == 255 ? -1 : c[pc + 8], c[pc + 9]);
            VM.pc += 10; VM.fuite = 1;
            return;
        case OP_HOUR: {   /* heure u16 (centièmes d'heure) : l'horloge du cycle jour / nuit, qui continue ensuite ; pas l'heure réelle */
            const float h = rd16(c + pc + 1) / 2400.0f;
            if (E.cycle != 3) E.heure = h - floorf(h);
            VM.pc += 3; break;
        }
        case OP_POLICE: police_scenario(c[pc + 1]); VM.pc += 2; break;
        case OP_JMP_IF_NOT_WANTED: case OP_JMP_IF_WANTED:   /* adresse u16 : la police ne la cherche pas (la cherche) */
            VM.pc = (POL.niveau > 0) == (c[pc] == OP_JMP_IF_WANTED) ? rd16(c + pc + 1) : pc + 3;
            break;
        case OP_JMP_IF_NOT_FLEE: case OP_JMP_IF_FLEE:   /* adresse u16 : la dernière fuite a échoué (réussi) */
            VM.pc = MISE.fuite_ok == (c[pc] == OP_JMP_IF_FLEE) ? rd16(c + pc + 1) : pc + 3;
            break;
        default: finish(); return;
        }
    }
}

void script_choose(int k) { VM.choice = k; }
/* Illustration nouvelle (instruction « image ») : montrée seule, en plein écran, SCENE_S s avant les répliques qui suivent
   (avant le 31/10/2026, la bulle du dialogue la couvrait aussitôt) ; un toucher l'abrège (hud.c, tap ; sceneSeule de
   l'émulateur) */
int script_image_seule(void) { return VM.on && scene_seule && VM.wait > 0; }
void script_image_passer(void) { if (script_image_seule()) VM.wait = 0; }

/* Sera dans la zone de l'événement k : le cercle d'une zone (déclencheur 1), ou un bâtiment de sa carte dont le centre est
   à moins de r du point (3), sur la carte de l'événement (evDedans de l'émulateur) */
static int ev_dedans(int k) {
    const sc_event_t *ev = &E.w.sc_ev[k];
    if (ev->trigger == 1) return E.w.carte == ev->carte && hypotf(E.sera.x - ev->x, E.sera.z - ev->z) < ev->r;
    if (ev->trigger == 3) {
        const int_bat_t *b = E.in.on && E.in.bat < E.w.nbats ? &E.w.bats[E.in.bat] : NULL;
        return b && b->carte == ev->carte && hypotf(b->cx - ev->x, b->cz - ev->z) < ev->r;
    }
    return 0;
}

void script_update(float dt) {
    const world_t *w = &E.w;
    if (!w->sc_nev || BT.on || carriere_on()) return;   /* mode course de l'écran titre : pas d'histoire */
    if (!VM.on) {
        /* Choix des sujets d'un personnage (script_talk) : la réponse lance son événement ; RIEN ne lance rien */
        if (VM.suj_att) {
            if (hud_in_choice()) return;
            VM.suj_att = 0;
            const int r = VM.choice;
            if (r >= 1 && r <= VM.nsuj && ev_ok(VM.suj[r - 1]) && run(VM.suj[r - 1])) exec();
            return;
        }
        if (hud_in_dialog() || E.ui_block || CR.on || police_bloque()) return;   /* ni pendant une arrestation (police.c) */
        /* Zones qui partent au même moment : celle d'une quête en cours (étape > 0) d'abord, puis le plus petit rayon, puis
           l'ordre de la liste ; les autres gardent leur zone armée et partiront ensuite (scenarioStep de l'émulateur) */
        int best = -1, bprio = 0;
        for (int k = 0; k < w->sc_nev && !VM.on; k++) {
            const sc_event_t *ev = &w->sc_ev[k];
            if (ev->trigger == 0) { if (!VM.started && ev_ok(k)) run(k); continue; }
            if (ev->trigger != 1 && ev->trigger != 3) continue;
            const int in = ev_dedans(k);
            if (in && !bit(VM.inzone, k) && ev_ok(k)) {
                const int prio = ev->quest < w->sc_nq && ev->step > 0 ? 0 : 1;
                if (best < 0 || prio < bprio || (prio == bprio && ev->r < w->sc_ev[best].r)) { best = k; bprio = prio; }
                continue;   /* armée jusqu'à ce qu'elle parte */
            }
            set_bit(VM.inzone, k, in);
        }
        if (!VM.on && best >= 0) { set_bit(VM.inzone, best, 1); run(best); }
        VM.started = 1;
        if (!VM.on) return;
    }
    /* course : menu, puis course jusqu'au classement ; Sera revient ensuite où elle était (une course lancée loin du circuit
       continue la scène près de celui qui l'a proposée ; courseFinie de l'émulateur) */
    if (VM.race) {
        if (hud_course_menu_open() || CR.on) return;
        VM.race = 0;
        if (hypotf(E.sera.x - VM.race_x, E.sera.z - VM.race_z) > 1) eng_teleport(VM.race_x, VM.race_z, VM.race_yaw);
    }
    if (VM.wait > 0) { VM.wait -= dt; return; }
    scene_seule = 0;
    if (VM.fuite) { if (MISE.fuite_on) return; VM.fuite = 0; }   /* fuite : jusqu'au cercle, au temps écoulé ou à la prise */
    if (VM.marche != -1) { if (mise_marche(VM.marche)) return; VM.marche = -1; }
    if (hud_in_dialog() || hud_in_choice()) return;
    exec();
}

/* Intitulé d'un sujet : la chaîne du scénario (js/scenario.js : nom de la quête sans son préfixe, sinon celui de
   l'événement) ; scénario ancien : le nom de la quête, sinon PARLER */
static void sujet_nom(int k, char *o, int n) {
    const sc_event_t *ev = &E.w.sc_ev[k];
    if (ev->sujet != 65535) str(ev->sujet, o, n);
    else snprintf(o, n, "%s", ev->quest < E.w.sc_nq ? E.w.sc_q[ev->quest].nom : "PARLER");
}

/* En parlant à l'exemplaire target : les événements « parler » de son personnage qui conviennent remplacent ses
   répliques. Un seul : il part. Plusieurs : choix des sujets (SUJETS - 1 au plus, puis RIEN), d'abord ceux d'une quête en
   cours (étape > 0), puis ceux qui ouvrent une quête, puis les répétables, chacun dans l'ordre de la liste ; la réponse
   part dans script_update (scenTalk de l'émulateur). Renvoie 1 si un événement ou le choix est parti */
int script_talk(int target) {
    const world_t *w = &E.w;
    if (!w->sc_nev || VM.on || VM.suj_att || target < 0) return 0;
    const int p = E.npcs[target].perso;
    int cand[SUJETS], cl[SUJETS], n = 0;
    for (int k = 0; k < w->sc_nev; k++) {
        const sc_event_t *ev = &w->sc_ev[k];
        if (ev->trigger != 2 || ev->perso != p || !ev_ok(k)) continue;
        const int c = !(ev->flags & SC_ONCE) ? 2 : ev->quest < w->sc_nq && ev->step > 0 ? 0 : 1;
        const int plein = n == SUJETS;
        if (plein && cl[SUJETS - 1] <= c) continue;   /* plein : garde les plus prioritaires */
        int i = plein ? SUJETS - 1 : n++;
        while (i > 0 && cl[i - 1] > c) { cand[i] = cand[i - 1]; cl[i] = cl[i - 1]; i--; }
        cand[i] = k; cl[i] = c;
    }
    if (!n) return 0;
    if (n == 1) { if (!run(cand[0])) return 0; exec(); return 1; }
    char o[4][48];
    VM.nsuj = n > 3 ? 3 : n;
    for (int i = 0; i < VM.nsuj; i++) { VM.suj[i] = cand[i]; sujet_nom(cand[i], o[i], sizeof o[i]); }
    snprintf(o[VM.nsuj], sizeof o[VM.nsuj], "RIEN");
    VM.suj_att = 1; VM.choice = 0; E.has_dest = 0;
    hud_choice_titre(o, VM.nsuj + 1, w->persos[p].nom);
    return 1;
}

int script_running(void) { return VM.on; }
int script_ev_ok(int k) { return k >= 0 && k < E.w.sc_nev && ev_ok(k); }
/* Quête « d'affichage » choisie sur un panneau de quêtes (déclencheur 4, panneaux.c) : son événement part maintenant */
int script_panneau(int k) {
    if (VM.on || VM.suj_att || k < 0 || k >= E.w.sc_nev || E.w.sc_ev[k].trigger != 4 || !ev_ok(k) || !run(k)) return 0;
    exec();
    return 1;
}

/* Partie chargée : rien en cours, l'événement du lancement ne repart pas ; quête suivie gardée si elle est en cours.
   Sera chargée dans la zone d'un événement (point de sauvegarde d'avant un combat) : il ne repart qu'une fois qu'elle en
   est sortie puis revenue. Avant le 02/11/2026, il repartait aussitôt : battue par la Sentinelle du prologue, REPRENDRE
   relançait la même scène et le même combat, sans pouvoir aller se préparer (RÉESSAYER LE COMBAT reste là pour ça) */
void script_repartir(void) { memset(&VM, 0, sizeof VM); VM.marche = -1; mise_fin(1); E.scene = 0; if (E.qtrack < 0 || E.qtrack >= E.w.sc_nq || !active(E.qtrack)) E.qtrack = next_active(); }
void script_after_load(void) {
    memset(&VM, 0, sizeof VM); VM.started = 1; VM.marche = -1;
    for (int k = 0; k < E.w.sc_nev; k++) if (ev_dedans(k)) set_bit(VM.inzone, k, 1);
    mise_fin(1);
    if (E.qtrack < 0 || E.qtrack >= E.w.sc_nq || !active(E.qtrack)) E.qtrack = next_active();
}

/* Combat perdu : le script s'arrête sans marquer son événement comme fait (Sera repart du point de départ) */
void script_abort(void) { VM.on = 0; E.scene = 0; mise_cam_stop(); mise_fin(0); }

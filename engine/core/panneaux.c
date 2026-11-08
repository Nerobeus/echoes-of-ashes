/*
 * Marques des personnages et panneaux de quêtes (03/11/2026, demande de l'utilisateur : « je ne trouve pas beaucoup de
 * quêtes secondaires ») ; comme la section « Panneaux de quêtes » de l'émulateur (gardeOk, marquesMaj, panneauListe,
 * panneauMaj…), à garder identiques.
 *
 * Marques : au-dessus de la tête d'un personnage, « ! » s'il peut ouvrir une quête maintenant (un « parler » à lui qui
 * peut partir, dont le code ouvre une quête pas commencée : table des ouvertures du scénario, js/scenario.js, avec les
 * conditions des « si » qui mènent à « quete commencer »), « ? » s'il est visé par l'étape en cours d'une quête (un
 * « parler » à lui, à usage unique, de l'étape où en est sa quête) ; or pour une quête secondaire, cyan pour une quête
 * principale. Une marque par personnage (MQ_*), refaite seulement quand l'état du scénario change (empreinte des
 * drapeaux, des quêtes, des événements faits, de l'héroïne et du verrou 18+), pas à chaque image.
 *
 * Panneaux : section PANQ du paquet (posés par l'Atelier, js/panneaux.js, dessinés comme blocs du décor de leur chunk) ;
 * à portée, une bulle LIRE ; l'écran (hud.c, SUB_PANNEAU) liste les quêtes secondaires ouvrables de sa région (chaque
 * ouverture va au panneau le plus proche sur sa carte), avec le nom, l'accroche, le niveau et la distance ; en choisir
 * une met le guidage du panneau sur la personne ou le lieu qui la donne (PG : flèche verte, comme le menu GUIDAGE), ou
 * lance l'événement d'une quête « d'affichage » (déclencheur 4). Ni 18+ verrouillé (événements 18+ : script_ev_ok), ni
 * quête fermée (ses conditions ne tiennent plus).
 *
 * Mémoire : réservée au chargement du scénario (table des ouvertures, une marque par personnage) ; rien pendant le jeu.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "ui.h"

#define OUV_OCT 16              /* événement, quête, garde, accroche, x, z u16 ; carte, drapeaux, personnage, 0 u8 */
#define OUV_PANNEAU 1           /* drapeau : listée sur les panneaux */
#define OUV_AFFICHE 2           /* drapeau : ouverte par le panneau lui-même (déclencheur 4) */
#define PAN_PORTEE 1.3f         /* cases : la bulle LIRE */
#define PAN_ARRIVEE 2.5f        /* cases : arrivée du guidage d'un panneau */
#define PAN_PERS_R 48.0f        /* cases : guidage vers l'exemplaire présent le plus proche du lieu (comme objective_perso) */

typedef struct { uint16_t ev, quete, garde, accroche, x, z; uint8_t carte, drap, perso, pan; } ouv_t;
static struct {
    ouv_t *o; int n;            /* ouvertures (OUV_OCT octets chacune dans le scénario) */
    const uint8_t *g; int ng;   /* termes des gardes (4 octets : sorte, valeur, argument u16) */
    uint8_t *mq; int nmq;       /* marque de chaque personnage (MQ_*) */
    uint8_t pan_n[PANQ_MAX];    /* quêtes ouvrables de chaque panneau (carte du menu) */
    uint32_t sig; int frais;    /* empreinte de l'état quand les marques ont été faites */
    uint32_t us;                /* durée du dernier calcul (banc d'essai) */
} OV;
/* Guidage d'un panneau : lieu ou personne qui donne la quête choisie */
static struct { int on, carte, perso; float x, z, x0, z0; char nom[40]; } PG;

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { float f; uint32_t u = rd32(p); memcpy(&f, &u, 4); return f; }

/* Table des ouvertures du scénario (script.c, en-tête de 60 octets) : nombre u32, nombre de termes u32, ouvertures,
   termes (pak.h, SCEN) */
void ouvertures_load(uint32_t off) {
    uint8_t h[8];
    OV.o = NULL; OV.n = 0; OV.g = NULL; OV.ng = 0; OV.frais = 0;
    if (!off || script_lire(off, h, 8)) return;
    uint32_t n = rd32(h), ng = rd32(h + 4);
    if (n > SC_MAX_EVENTS || ng > 4096) return;
    uint8_t *b = n || ng ? E.cfg.alloc(n * sizeof(ouv_t) + ng * 4, 0) : NULL;
    if (!b) return;
    ouv_t *o = (ouv_t *)b;
    uint8_t *g = b + n * sizeof(ouv_t);
    for (uint32_t k = 0; k < n; k++) {
        uint8_t q[OUV_OCT];
        if (script_lire(off + 8 + k * OUV_OCT, q, OUV_OCT)) return;
        o[k] = (ouv_t){ (uint16_t)rd16(q), (uint16_t)rd16(q + 2), (uint16_t)rd16(q + 4), (uint16_t)rd16(q + 6), (uint16_t)rd16(q + 8), (uint16_t)rd16(q + 10), q[12], q[13], q[14], 255 };
    }
    if (ng && script_lire(off + 8 + n * OUV_OCT, g, ng * 4)) return;
    OV.o = o; OV.n = (int)n; OV.g = g; OV.ng = (int)ng;
}

/* Une marque par personnage : réservée au chargement (après PERS et le scénario) ; panneaux de la carte (PANQ) */
void panneaux_load(void) {
    uint32_t sz = 0;
    const uint8_t *p = pak_section("PANQ", &sz);
    E.w.panq = NULL; E.w.npanq = 0; E.panneau = -1; PG.on = 0; OV.frais = 0;
    if (p && sz >= 4 && 4 + (uint64_t)rd32(p) * PANQ_OCT <= sz) { E.w.panq = p + 4; E.w.npanq = (int)(rd32(p) < PANQ_MAX ? rd32(p) : PANQ_MAX); }
    if (E.w.npersos > 0 && OV.nmq < E.w.npersos) { OV.mq = E.cfg.alloc((size_t)E.w.npersos, 0); OV.nmq = OV.mq ? E.w.npersos : 0; }
    /* chaque ouverture va au panneau le plus proche sur sa carte (sa région) */
    for (int k = 0; k < OV.n; k++) {
        ouv_t *o = &OV.o[k];
        float bd = 1e9f;
        o->pan = 255;
        if (o->x == 65535) continue;
        for (int i = 0; i < E.w.npanq; i++) {
            const uint8_t *q = E.w.panq + i * PANQ_OCT;
            const float d = hypotf(rdf(q) - o->x / 8.0f, rdf(q + 4) - o->z / 8.0f);
            if (q[8] == o->carte && d < bd) { bd = d; o->pan = (uint8_t)i; }
        }
    }
}

/* ---------- Conditions ---------- */
static int flag(int f) { return f < SC_MAX_FLAGS && ((E.flags[f >> 3] >> (f & 7)) & 1); }
static int terme_ok(const uint8_t *t) {
    const int a = rd16(t + 2), v = t[1];
    switch (t[0]) {
    case 1: return flag(a);
    case 2: return !flag(a);
    case 3: return a == 255 ? E.heroine < 0 : E.heroine == a;    /* héroïne choisie (255 : Sera) */
    case 4: return !(a == 255 ? E.heroine < 0 : E.heroine == a);
    case 5: return a < E.w.sc_nq && E.quests[a] >= v;           /* quête à l'état v ou plus loin */
    case 6: return a >= E.w.sc_nq || E.quests[a] < v;
    default: return 1;
    }
}
/* Gardes d'une ouverture : chemins (termes séparés par 254, fin 255), l'un ou l'autre ; 65535 : toujours */
static int garde_ok(int g) {
    if (g == 65535) return 1;
    int ok = 1;
    for (int i = g; i < OV.ng; i++) {
        const uint8_t *t = OV.g + 4 * i;
        if (t[0] == 255) return ok;
        if (t[0] == 254) { if (ok) return 1; ok = 1; continue; }
        if (ok && !terme_ok(t)) ok = 0;
    }
    return ok;
}
/* L'ouverture k peut-elle partir maintenant : son événement le peut, sa quête n'a pas commencé, ses conditions tiennent,
   son donneur est là */
static int ouvrable(const ouv_t *o) {
    if (o->quete >= E.w.sc_nq || E.quests[o->quete] != 0 || !script_ev_ok(o->ev) || !garde_ok(o->garde)) return 0;
    return o->perso == 255 || o->perso >= E.w.npersos || perso_present(o->perso);   /* 255 : pas de donneur */
}
static int quete_type(int q) { return script_quest_type(q); }

/* ---------- Marques ---------- */
static uint32_t etat_sig(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < SC_MAX_FLAGS / 8; i++) h = (h ^ E.flags[i]) * 16777619u;
    for (int i = 0; i < SC_MAX_EVENTS / 8; i++) h = (h ^ E.ev_done[i]) * 16777619u;
    for (int q = 0; q < E.w.sc_nq; q++) h = (h ^ E.quests[q]) * 16777619u;
    h = (h ^ (uint32_t)(E.heroine + 2)) * 16777619u;
    h = (h ^ (uint32_t)verrou18()) * 16777619u;
    return (h ^ (uint32_t)E.recrues) * 16777619u;
}
static int perso_ennemi(int p) {
    for (int e = 0; e < E.w.nenemies; e++) if (E.w.enemies[e].perso == p) return 1;
    return 0;
}
static void marquer(int p, int m) { if (p >= 0 && p < OV.nmq && OV.mq[p] < m) OV.mq[p] = (uint8_t)m; }
static void marques_maj(void) {
    const uint32_t s = etat_sig();
    if (OV.frais && s == OV.sig) return;
    const uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    OV.sig = s; OV.frais = 1;
    if (OV.mq) memset(OV.mq, 0, (size_t)OV.nmq);
    memset(OV.pan_n, 0, sizeof OV.pan_n);
    const world_t *w = &E.w;
    /* « ? » : un « parler » à usage unique de l'étape où en est sa quête */
    for (int k = 0; k < w->sc_nev; k++) {
        const sc_event_t *ev = &w->sc_ev[k];
        if (ev->trigger != 2 || !(ev->flags & 1) || ev->quest >= w->sc_nq || ev->step < 1 || E.quests[ev->quest] != ev->step || !script_ev_ok(k)) continue;
        marquer(ev->perso, quete_type(ev->quest) == 0 ? MQ_SUITE_PRINC : MQ_SUITE);
    }
    /* « ! » : une ouverture d'un « parler » qui peut partir ; scénario sans table (en-tête de 56 octets) : un « parler » de
       l'étape 0 d'une quête pas commencée */
    if (OV.o) for (int k = 0; k < OV.n; k++) {
        const ouv_t *o = &OV.o[k];
        if (!ouvrable(o)) continue;
        if (o->ev < w->sc_nev && w->sc_ev[o->ev].trigger == 2) marquer(w->sc_ev[o->ev].perso, quete_type(o->quete) == 0 ? MQ_OUVRE_PRINC : MQ_OUVRE);
        if ((o->drap & OUV_PANNEAU) && o->pan < PANQ_MAX && OV.pan_n[o->pan] < 255) OV.pan_n[o->pan]++;
    }
    else for (int k = 0; k < w->sc_nev; k++) {
        const sc_event_t *ev = &w->sc_ev[k];
        if (ev->trigger == 2 && ev->quest < w->sc_nq && ev->step == 0 && E.quests[ev->quest] == 0 && script_ev_ok(k))
            marquer(ev->perso, quete_type(ev->quest) == 0 ? MQ_OUVRE_PRINC : MQ_OUVRE);
    }
    /* ni un ennemi posé (on ne lui parle pas), ni un véhicule */
    for (int p = 0; p < OV.nmq; p++)
        if (OV.mq[p] && ((E.w.persos[p].flags & PERS_VEHICULE) || perso_ennemi(p))) OV.mq[p] = 0;
    if (E.cfg.clock_us) OV.us = E.cfg.clock_us() - t0;
}
/* Marque du personnage p : celles faites à cette image (panneau_maj compare l'empreinte de l'état une fois par image) */
int marque_de(int p) {
    if (!OV.nmq || p < 0 || p >= OV.nmq) return MQ_AUCUNE;
    if (!OV.frais) marques_maj();
    return OV.mq[p];
}
/* Banc d'essai : marques à refaire au prochain appel ; durée du dernier calcul (µs) */
void marques_refaire(void) { OV.frais = 0; }
uint32_t marques_us(void) { return OV.us; }
int ouvertures_n(void) { return OV.n; }

/* Badge d'une marque (x, y : son centre ; k : taille, 1 au-dessus d'une tête) : hexagone sombre, bord et signe de sa
   couleur (or : quête secondaire, cyan : principale) */
void marque_dessin(float x, float y, int m, float k) {
    if (m <= MQ_AUCUNE) return;
    const uint16_t c = ui_rgb(m >= MQ_OUVRE_PRINC ? MQ_RGB_PRINC : MQ_RGB);
    float q[12];
    ui_hex_path(x, y, 30 * k, 28 * k, q);
    ui_poly(q, 6, ui_rgb(0x060A14), 225);
    ui_outline(q, 6, 2, c, 255);
    const char *s = m == MQ_SUITE || m == MQ_SUITE_PRINC ? "?" : "!";
    ui_text(ui_font(FONT_DISP, k >= 1 ? 22 : 18), x, y + 8 * k, UI_CENTER, c, 255, s);
}

/* Marques des exemplaires de la carte où est Sera (carte du menu, hors intérieur) : position et marque, n au plus */
int marques_points(float *x, float *z, uint8_t *m, int n) {
    int c = 0;
    const int i1 = E.in.on ? E.in.npc0 : E.nnpc;   /* les exemplaires d'un intérieur ne sont pas sur la carte */
    for (int i = 0; i < i1 && c < n; i++) {
        const npc_t *p = &E.npcs[i];
        const int mq = marque_de(p->perso);
        if (!mq || !npc_present(p)) continue;
        x[c] = p->x; z[c] = p->z; m[c] = (uint8_t)mq; c++;
    }
    return c;
}

/* ---------- Panneaux ---------- */
int panneau_n(void) { return E.w.npanq; }
const char *panneau_nom(int k) {
    static char nom[25];
    if (k < 0 || k >= E.w.npanq) return "";
    memcpy(nom, E.w.panq + k * PANQ_OCT + 12, 24); nom[24] = 0;
    return nom[0] ? nom : "PANNEAU";
}
int panneau_lieu(int k, float *x, float *z, int *carte) {
    if (k < 0 || k >= E.w.npanq) return 0;
    const uint8_t *q = E.w.panq + k * PANQ_OCT;
    *x = rdf(q); *z = rdf(q + 4); *carte = q[8];
    return 1;
}
/* Quêtes ouvrables du panneau k (pan_n) : la carte du menu en montre le nombre */
int panneau_quetes(int k) { if (!OV.frais) marques_maj(); return k >= 0 && k < PANQ_MAX ? OV.pan_n[k] : 0; }

/* Panneau à portée (panneauMaj de l'émulateur) : le plus proche de Sera sur sa carte, dehors, à pied, rien en cours ;
   arrivée du guidage d'un panneau ; sa cible suit l'exemplaire présent le plus proche de son lieu (personne qui donne) */
void panneau_maj(void) {
    marques_maj();   /* l'état du scénario a-t-il changé : les marques et les panneaux sont refaits (empreinte) */
    E.panneau = -1;
    if (PG.on && PG.carte == E.w.carte && !E.in.on) {
        if (PG.perso >= 0) {
            float bd = PAN_PERS_R;
            for (int i = 0; i < E.nnpc; i++) {
                const npc_t *n = &E.npcs[i];
                if (n->perso != PG.perso || !npc_present(n)) continue;
                const float d = hypotf(n->x - PG.x0, n->z - PG.z0);
                if (d < bd) { bd = d; PG.x = n->x; PG.z = n->z; }
            }
        }
        if (hypotf(E.sera.x - PG.x, E.sera.z - PG.z) < PAN_ARRIVEE / E.w.ech) {
            char t[64]; snprintf(t, sizeof t, "Arrivée : %s", PG.nom); hud_toast(t);
            PG.on = 0;
        }
    }
    if (BT.on || CR.on || E.in.on || E.ride >= 0 || police_bloque() || hud_in_dialog() || script_running()) return;
    float bd = PAN_PORTEE / E.w.ech;
    for (int k = 0; k < E.w.npanq; k++) {
        const uint8_t *q = E.w.panq + k * PANQ_OCT;
        if (q[8] != E.w.carte) continue;
        const float d = hypotf(rdf(q) - E.sera.x, rdf(q + 4) - E.sera.z);
        if (d < bd) { bd = d; E.panneau = k; }
    }
}
int panneau_position(float *x, float *y, float *z) {
    if (E.panneau < 0 || E.panneau >= E.w.npanq) return 0;
    const uint8_t *q = E.w.panq + E.panneau * PANQ_OCT;
    *x = rdf(q); *z = rdf(q + 4); *y = ground_y(*x, *z) + 1.5f / E.w.ech;
    return 1;
}
/* Le panneau à portée passe devant l'interlocuteur (OK) : personne à qui parler, ou plus près de Sera que lui */
int panneau_avant(void) {
    float x, y, z, tx, tz;
    if (E.panneau < 0 || !panneau_position(&x, &y, &z)) return 0;
    if (E.talk >= 0 && E.talk < E.nnpc) { tx = E.npcs[E.talk].x; tz = E.npcs[E.talk].z; }
    else if (E.talk <= TALK_TRAF(0) && TALK_TRAF(0) - E.talk < TRAFFIC_MAX) { tx = E.traffic[TALK_TRAF(0) - E.talk].x; tz = E.traffic[TALK_TRAF(0) - E.talk].z; }
    else return 1;
    return hypotf(x - E.sera.x, z - E.sera.z) < hypotf(tx - E.sera.x, tz - E.sera.z);
}
/* Quêtes ouvrables du panneau k (panneauListe de l'émulateur) : rang de chaque ouverture dans la table et sa distance à
   Sera, une par quête, de la plus proche à la plus loin ; n au plus */
int panneau_liste(int k, int16_t *ov, float *dist, int n) {
    int c = 0;
    marques_maj();
    for (int i = 0; i < OV.n; i++) {
        const ouv_t *o = &OV.o[i];
        if (o->pan != k || !(o->drap & OUV_PANNEAU) || !ouvrable(o)) continue;
        int deja = 0;
        for (int j = 0; j < c && !deja; j++) deja = OV.o[ov[j]].quete == o->quete;
        if (deja) continue;
        const float d = o->carte == E.w.carte ? hypotf(o->x / 8.0f - E.sera.x, o->z / 8.0f - E.sera.z) : 1e9f;
        int j;
        if (c < n) j = c++;
        else if (d < dist[n - 1]) j = n - 1;   /* pleine : la plus loin sort */
        else continue;
        while (j > 0 && dist[j - 1] > d) { ov[j] = ov[j - 1]; dist[j] = dist[j - 1]; j--; }
        ov[j] = (int16_t)i; dist[j] = d;
    }
    return c;
}
/* Ouverture i : quête, accroche (chaîne), drapeaux */
int ouverture_quete(int i) { return i >= 0 && i < OV.n ? OV.o[i].quete : -1; }
int ouverture_accroche(int i) { return i >= 0 && i < OV.n ? OV.o[i].accroche : 65535; }
/* Choix d'une quête sur un panneau : une quête d'affichage part tout de suite ; sinon le guidage du panneau va vers la
   personne ou le lieu qui la donne (le lieu suivi du menu GUIDAGE s'arrête). Rend 1 si l'événement est parti */
int ouverture_choisir(int i) {
    if (i < 0 || i >= OV.n) return 0;
    const ouv_t *o = &OV.o[i];
    if (o->drap & OUV_AFFICHE) { PG.on = 0; return script_panneau(o->ev); }
    if (o->x == 65535) return 0;
    PG.on = 1; PG.carte = o->carte; PG.perso = o->perso != 255 && o->perso < E.w.npersos && o->ev < E.w.sc_nev && E.w.sc_ev[o->ev].trigger == 2 ? o->perso : -1;
    PG.x = PG.x0 = o->x / 8.0f; PG.z = PG.z0 = o->z / 8.0f;
    snprintf(PG.nom, sizeof PG.nom, "%s", PG.perso >= 0 ? E.w.persos[PG.perso].nom : script_quest_name(o->quete));
    E.guide = -1;
    return 0;
}
/* Guidage d'un panneau, sur la carte où est Sera : sa cible (flèche verte, distance, carte du menu) ; 0 : aucun ici */
int panneau_guide(float *x, float *z) {
    if (!PG.on || PG.carte != E.w.carte) return 0;
    *x = PG.x; *z = PG.z;
    return 1;
}
const char *panneau_guide_nom(void) { return PG.on ? PG.nom : ""; }
void panneau_guide_arreter(void) { PG.on = 0; }

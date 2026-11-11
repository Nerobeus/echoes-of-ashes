/*
 * Île tropicale du jeu des héroïnes (D:\Documents\Stellar Exodus\generateur-ile, 09/11/2026 : « une version du monde testable
 * sur la montre ») : le moteur passe en mode île quand le paquet a la section ILE (le nom de l'île, écrite par l'émulateur de
 * l'île, w.ile). Repris de l'émulateur de l'île, à garder identiques :
 *  - équipe (ileEquipe) : l'héroïne choisie à « Qui es-tu ? » à la place 0 (celle de Sera), deux compagnes aux places 1 et 2
 *    de PARTY, renommées (planche du personnage de leur nom : E.w.party_perso), personne d'autre dans la bande ; leurs fiches
 *    (FICHES = ILE_MEMBRES : couleur, PV, PM, ATQ, VIT au niveau 20 comme PARTY, deux techniques d'épée) ; niveau 1 au départ.
 *    Compagnes : les deux premières héroïnes au choix (OUVR) après celle qu'on joue (le jeu web les fait choisir, ile.html) ;
 *  - compagnes sur la trace de ses pas, comme les suiveurs d'un JRPG (ileCompagnesMaj, ilePoint, ileTraceRemettre) : chacune à
 *    ILE_ECART × son rang de chemin derrière elle, à son allure, enfoncées dans l'eau comme elle, cachées quand elle conduit,
 *    dedans, en course ; reposées dans son dos après un saut (téléport, partie chargée) ; dessinées par render.c ;
 *  - niveau de chaque région quand aucune quête n'en donne (ileNiveau : il borne les ennemis rencontrés et les combats).
 */
#include <math.h>
#include <string.h>
#include "internal.h"

/* Fiches de l'île (ILE_MEMBRES de l'émulateur), par identifiant d'héroïne du studio (section ILE : ile_her ; un paquet d'avant le
   11/11/2026 : par le nom, sans la casse) ; effets : FX_* d'Echoes, FX_SORT(élément, niveau) ; delay : tours de retard infligés ;
   heal : soin au niveau 20. La dernière : toute autre héroïne */
typedef struct { const char *nom; uint32_t color; int hp, mp, atk, spd; tech_t tech[2]; int ntech; } ile_fiche_t;
static const ile_fiche_t FICHES[] = {
    { "hilda", 0xFF6FAE, 3700, 300, 450, 12, { { "TAILLE CROISÉE", 30, 0, 2.0f, 0, 0, FX_TAILLE }, { "LAME DU VENT", 40, 0, 1.4f, 1.2f, 0, FX_SORT(MG_VENT, 2) } }, 2 },
    { "pyra", 0xFF4A3A, 4300, 260, 490, 10, { { "ÉPÉE ARDENTE", 30, 0, 2.1f, 0, 0, FX_SORT(MG_BRASIER, 2) }, { "FLAMBOIEMENT", 45, 0, 1.2f, 1.3f, 0, FX_SORT(MG_BRASIER, 1) } }, 2 },
    { "mythra", 0xFFD84A, 3400, 420, 430, 13, { { "LAME DE LUMIÈRE", 35, 0, 2.1f, 0, 0, FX_SORT(MG_SACRE, 2) }, { "SOIN DE L’AUBE", 40, 1400, 0, 0, 0, 0 } }, 2 },
    { "kosmos", 0x46C8FF, 3600, 400, 440, 11, { { "RAYON D’ÉTHER", 35, 0, 2.0f, 0, 0, FX_LASER }, { "LAME DE GIVRE", 35, 0, 1.6f, 1.2f, 0, FX_SORT(MG_GIVRE, 2) } }, 2 },
    { "baal", 0xA77BFF, 3900, 360, 470, 10, { { "FOUDRE DIVINE", 35, 0, 2.1f, 0, 0, FX_SORT(MG_FOUDRE, 2) }, { "COUP DU NÉANT", 40, 0, 1.5f, 1.3f, 0, FX_SORT(MG_NEANT, 2) } }, 2 },
    { "", 0x2EE6D6, 3700, 300, 450, 11, { { "TAILLE CROISÉE", 30, 0, 2.0f, 0, 0, FX_TAILLE } }, 1 },
};
#define NFICHES ((int)(sizeof FICHES / sizeof FICHES[0]))

ile_comp_t ILE_COMP[ILE_COMPAGNES];
int ile_ncomp;

static int pareil(const char *a, const char *b) {   /* sans la casse (ASCII) */
    for (; *a && *b; a++, b++) { const int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b; if (x != y) return 0; }
    return *a == *b;
}
/* identifiant du studio d'une héroïne (personnage p), sinon son nom */
static const char *her_id(int p) {
    if (p < 0 || p >= E.w.npersos) return NULL;
    for (int k = 0; k < E.w.ile_nher; k++) if (E.w.ile_her[k].perso == p) return E.w.ile_her[k].id;
    return E.w.persos[p].nom;
}
static void fiche(member_t *m, int p) {
    const ile_fiche_t *f = &FICHES[NFICHES - 1];
    const char *nom = her_id(p);
    for (int i = 0; nom && i < NFICHES - 1; i++) if (pareil(FICHES[i].nom, nom)) { f = &FICHES[i]; break; }
    m->color = f->color;
    m->bhp = m->max = m->hp = f->hp; m->bmp = m->maxmp = m->mp = f->mp; m->batk = m->atk = f->atk; m->bspd = m->spd = f->spd;
    m->ntech = f->ntech;
    for (int t = 0; t < 2; t++) { m->tech[t] = f->tech[t]; m->tech[t].bheal = f->tech[t].heal; }
}

/* L'équipe de l'île (ileEquipe) pour l'héroïne jouée (E.heroine) : nouvelle partie (niveau 1, PV et PM pleins), ou héroïne
   d'une partie chargée (partie_appliquer : niveau, PV et PM de la sauvegarde ensuite), changée (code CODE_HEROINE) */
/* Drapeaux de l'histoire de l'île (10/11/2026, demande de l'utilisateur : le prologue n'a « ni armes, ni ennemi, ni familier ») : leur numéro
   dans la section ILE (65535 : pas dans le scénario, toujours levé) ; monstres ouvre les rencontres, esprits fait paraître les compagnes,
   l'équipe refaite quand il change (ileDrapeau, ileCalme de l'émulateur de l'île) */
static int ile_drapeau(uint16_t f) { return f >= SC_MAX_FLAGS || ((E.flags[f >> 3] >> (f & 7)) & 1); }
int ile_calme(void) { return E.w.ile[0] && !ile_drapeau(E.w.ile_monstres); }
/* Épuisée (11/11/2026, retour de l'utilisateur : on courait alors qu'on venait de faire naufrage) : drapeau epuise levé ; sans lui dans
   le paquet, jamais (ileEpuisee de l'émulateur) */
int ile_epuisee(void) { return E.w.ile[0] && E.w.ile_epuise < SC_MAX_FLAGS && ile_drapeau(E.w.ile_epuise); }
static int ile_esprits_vu = -1;   /* état du drapeau esprits à la dernière composition de l'équipe */
/* Lames éveillées (11/11/2026 : drapeau de chaque lame du paquet, js/lames-ile.js) : leurs esprits sont les compagnes, dans l'ordre des
   lames ; un paquet sans lames : les héroïnes au choix (OUVR) après celle qu'on joue, comme avant */
static uint64_t ile_lames_vu;
static uint64_t ile_lames_eveillees(void) {
    uint64_t m = 0;
    for (int k = 0; k < E.w.ile_nlames && k < 64; k++) { const uint16_t f = E.w.ile_lame[k].drapeau; if (f < SC_MAX_FLAGS && ((E.flags[f >> 3] >> (f & 7)) & 1)) m |= 1ull << k; }
    return m;
}
void ile_equipe(int nouvelle) {
    world_t *w = &E.w;
    if (!w->ile[0]) return;
    for (int i = 0; i < w->npersos; i++) if (w->persos[i].membre > 0 && w->persos[i].membre <= ILE_COMPAGNES) w->persos[i].membre = -1;
    ile_ncomp = 0;
    ile_esprits_vu = ile_drapeau(w->ile_esprits);
    ile_lames_vu = ile_lames_eveillees();
    if (w->ile_nlames) for (int k = 0; k < w->ile_nlames && k < 64 && ile_ncomp < ILE_COMPAGNES && ile_esprits_vu; k++) {
        const int p = w->ile_lame[k].perso;
        if (!((ile_lames_vu >> k) & 1) || p >= w->npersos || p == E.heroine) continue;
        memset(&ILE_COMP[ile_ncomp], 0, sizeof ILE_COMP[0]);
        ILE_COMP[ile_ncomp++].perso = p;
    }
    else for (int i = 0; i < w->ouv_nher && ile_ncomp < ILE_COMPAGNES && ile_esprits_vu; i++) {
        const int p = w->ouv_her[i];
        if (p == E.heroine || (w->ouv_her18[i] && (w->edition == 1 || verrou18()))) continue;
        memset(&ILE_COMP[ile_ncomp], 0, sizeof ILE_COMP[0]);
        ILE_COMP[ile_ncomp++].perso = p;
    }
    fiche(&PARTY[0], E.heroine);
    for (int m = 1; m <= ILE_COMPAGNES; m++) {
        w->party_perso[m] = -1;
        if (m > ile_ncomp) continue;
        const int p = ILE_COMP[m - 1].perso;
        PARTY[m].name = w->persos[p].nom;
        fiche(&PARTY[m], p);
        w->party_perso[m] = p; w->persos[p].membre = (int8_t)m;
    }
    E.recrues = E.team = (1 << (1 + ile_ncomp)) - 1; E.impose = 0;
    if (nouvelle) { E.exp = 0; party_set_level(1); }
    else party_set_level(E.level);
    if (nouvelle) for (int i = 0; i < NMEMB; i++) { PARTY[i].hp = PARTY[i].max; PARTY[i].mp = PARTY[i].maxmp; }
    else for (int m = 1; m <= ile_ncomp; m++) if (PARTY[m].hp <= 0) { PARTY[m].hp = PARTY[m].max; PARTY[m].mp = PARTY[m].maxmp; }   /* une compagne qui paraît : en forme */
    ile_trace_oublier();
}

/* Niveau de chaque région de l'île (ILE_NIV de l'émulateur), du port vers le volcan, d'après le terrain des rencontres
   (rangs de Continent.LAND) ; 6 ailleurs. Les fiches de boss (31 et plus) n'y sortent pas : elles attendent les quêtes */
int ile_niveau(float x, float z) {
    static const int8_t NIV[23] = {
        /* mer lac rive plaine champs foret montagne haute-montagne urbain gravats industriel rue cendre eau-claire neige */
        0, 0, 5, 6, 8, 12, 36, 40, 0, 0, 0, 0, 28, 0, 0,
        /* desert marais foret-regeneree caverne regolithe desert-rouge jungle espace */
        4, 20, 7, 0, 0, 0, 18, 0 };
    const int t = encounter_zone(x, z);
    return t >= 0 && t < 23 && NIV[t] ? NIV[t] : 6;
}

/* Trace des pas de l'héroïne (la plus ancienne d'abord), un point tous les ILE_PAS ; gardée un peu plus long que la place
   de la dernière compagne. En PSRAM (tableau froid) */
#define ILE_PAS 0.04f
#define ILE_SAUT 3.0f
#define ILE_TRACE 320
static float trace[ILE_TRACE][2] ENG_FROID;
static int ntr;
#define ILE_ECART (1.15f / E.w.ech)   /* écart de chemin d'une compagne à la suivante */
#define ILE_MARCHE (0.25f / E.w.ech)  /* en deçà de cette vitesse (cases par seconde) : à l'arrêt */
void ile_trace_oublier(void) { ntr = 0; }
/* Trace refaite dans le dos de l'héroïne, ses compagnes posées dessus */
static void remettre(void) {
    int n = (int)ceilf((ile_ncomp + 1) * ILE_ECART / 0.25f) + 1;
    if (n >= ILE_TRACE) n = ILE_TRACE - 1;
    ntr = 0;
    for (int i = n; i >= 0; i--) { trace[ntr][0] = E.sera.x - sinf(E.sera.dir) * i * 0.25f; trace[ntr][1] = E.sera.z - cosf(E.sera.dir) * i * 0.25f; ntr++; }
    for (int k = 0; k < ile_ncomp; k++) {
        ile_comp_t *c = &ILE_COMP[k];
        float q[2]; ile_point((k + 1) * ILE_ECART, q);
        c->x = q[0]; c->z = q[1]; c->dir = E.sera.dir; c->st = 0; c->frame = 0; c->anim = 0; c->v = 0;
    }
}
/* Point de la trace à la distance de chemin d derrière l'héroïne */
void ile_point(float d, float *q) {
    float px = E.sera.x, pz = E.sera.z;
    for (int i = ntr - 1; i >= 0; i--) {
        const float l = hypotf(trace[i][0] - px, trace[i][1] - pz);
        if (l >= d && l > 0) { q[0] = px + (trace[i][0] - px) * d / l; q[1] = pz + (trace[i][1] - pz) * d / l; return; }
        d -= l; px = trace[i][0]; pz = trace[i][1];
    }
    q[0] = px; q[1] = pz;
}
/* Compagnes visibles : sur la planète de l'île, à pied, dehors, hors course (et hors combat : render.c) */
int ile_compagnes_vues(void) { return E.w.ile[0] && ile_ncomp && ntr && E.ride < 0 && !E.in.on && !CR.on && !E.w.carte; }
void ile_compagnes_maj(float dt) {
    if (E.w.ile[0] && ile_esprits_vu >= 0 && (ile_drapeau(E.w.ile_esprits) != ile_esprits_vu || ile_lames_eveillees() != ile_lames_vu) && !BT.on) ile_equipe(0);   /* l'histoire vient de les faire paraître, ou une lame s'éveille */
    if (!E.w.ile[0] || !ile_ncomp) return;
    if (E.ride >= 0 || E.in.on || CR.on || E.w.carte) { ntr = 0; return; }   /* cachées ; reposées derrière elle ensuite */
    if (!ntr || hypotf(E.sera.x - trace[ntr - 1][0], E.sera.z - trace[ntr - 1][1]) > ILE_SAUT) { remettre(); return; }
    if (hypotf(E.sera.x - trace[ntr - 1][0], E.sera.z - trace[ntr - 1][1]) >= ILE_PAS) {
        if (ntr == ILE_TRACE) { memmove(trace, trace + 1, (ILE_TRACE - 1) * sizeof trace[0]); ntr--; }
        trace[ntr][0] = E.sera.x; trace[ntr][1] = E.sera.z; ntr++;
        float L = 0; int i = ntr - 1;
        const float max = (ile_ncomp + 1) * ILE_ECART + 1;
        for (; i > 0 && L < max; i--) L += hypotf(trace[i][0] - trace[i - 1][0], trace[i][1] - trace[i - 1][1]);
        if (i > 0) { memmove(trace, trace + i, (size_t)(ntr - i) * sizeof trace[0]); ntr -= i; }
    }
    for (int k = 0; k < ile_ncomp; k++) {
        ile_comp_t *c = &ILE_COMP[k];
        float q[2]; ile_point((k + 1) * ILE_ECART, q);
        const float dx = q[0] - c->x, dz = q[1] - c->z, d = hypotf(dx, dz);
        c->v = c->v * 0.75f + d / fmaxf(dt, 1e-3f) * 0.25f;   /* vitesse lissée : son allure */
        if (d > 1e-4f) { c->x = q[0]; c->z = q[1]; c->dir = atan2f(dx, dz); }
        /* l'allure de l'héroïne (toujours sur sa trace, sans retard à rattraper) ; à la nage : la marche */
        const int st = c->v <= ILE_MARCHE ? 0 : E.sera.state == 2 && eau_prof(c->x, c->z) <= 0 ? 2 : 1;
        if (c->st != st) { c->st = (uint8_t)st; c->frame = 0; c->anim = 0; }
        c->anim += dt;   /* attente : animation lente, comme Sera */
        if (c->anim > (st == 2 ? 0.09f : st ? 0.13f : 0.22f)) { c->anim = 0; c->frame++; }
    }
}

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
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "audio.h"

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
    /* familières (13/11/2026, succubes charmées, édition 18+ : « id~succube ») : leur pouvoir maléfique, une technique ultime en PM
       (sort de néant de niveau III : sur tout le groupe) */
    { "baal~succube", 0xE0457B, 4100, 460, 500, 12, { { "GRIFFES DE BRAISE", 30, 0, 2.0f, 0, 0, FX_TAILLE }, { "JUGEMENT DE CENDRE", 90, 0, 3.4f, 0, 0, FX_SORT(MG_NEANT, 3) } }, 2 },
    { "mammon~succube", 0xE8B23A, 3900, 460, 510, 13, { { "GRIFFE D'OR", 30, 0, 2.0f, 0, 0, FX_TAILLE }, { "AVIDITÉ ABYSSALE", 90, 0, 3.2f, 0, 0, FX_SORT(MG_NEANT, 3) } }, 2 },
    /* toute autre succube (rôle succube choisi dans le studio des sprites, 13/11/2026) */
    { "~succube", 0xC2185B, 4000, 460, 505, 12, { { "GRIFFES DE BRAISE", 30, 0, 2.0f, 0, 0, FX_TAILLE }, { "PACTE DE CENDRE", 90, 0, 3.3f, 0, 0, FX_SORT(MG_NEANT, 3) } }, 2 },
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
static void fiche(member_t *m, int p, int fam) {   /* fam : une familière (sa fiche « id~succube », sinon la sienne) */
    const ile_fiche_t *f = &FICHES[NFICHES - 1];
    const char *nom = her_id(p);
    char cle[32];
    if (fam && nom) {   /* la sienne, sinon celle de toute autre succube */
        snprintf(cle, sizeof cle, "%s~succube", nom);
        for (int i = 0; i < NFICHES - 1; i++) if (pareil(FICHES[i].nom, cle)) { f = &FICHES[i]; nom = NULL; break; }
        for (int i = 0; nom && i < NFICHES - 1; i++) if (pareil(FICHES[i].nom, "~succube")) { f = &FICHES[i]; nom = NULL; break; }
    }
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
/* Le soir sur l'île (12/11/2026, chantier des nuits, étape 1 ; docs/ile-de-nacre-scenario.md, « Les nuits ») : une fois le drapeau « nuits »
   levé, le coucher du soleil s'annonce à 18 h et la nuit à 20 h, chaque soir ; les passants rentrent entre 18 h et 20 h et ressortent entre
   6 h et 7 h (ile_nuit_part, prise par abri_maj de game.c comme la pluie de cendre). ileSoirMaj et ileNuitPart de l'émulateur de l'île, à
   garder identiques */
#define NUIT_ALERTE (18.0f / 24)
#define NUIT_DEBUT (20.0f / 24)
#define NUIT_AUBE (6.0f / 24)
#define NUIT_AUBE_FIN (7.0f / 24)
static int ile_soir_dit;   /* bit 1 : le coucher du soleil annoncé, bit 2 : la nuit (remis à zéro le jour) */
static int ile_nuits_on(void) { return E.w.ile[0] && E.w.ile_nuits < SC_MAX_FLAGS && ile_drapeau(E.w.ile_nuits); }
static float heure_ile(void) { return E.cycle == 1 ? 0.5f : E.cycle == 2 ? 0.0f : E.heure; }
float ile_nuit_part(void) {
    if (!ile_nuits_on()) return 0;
    const float h = heure_ile();
    if (h >= NUIT_DEBUT || h < NUIT_AUBE) return 1;
    if (h >= NUIT_ALERTE) return (h - NUIT_ALERTE) / (NUIT_DEBUT - NUIT_ALERTE);
    if (h < NUIT_AUBE_FIN) return 1 - (h - NUIT_AUBE) / (NUIT_AUBE_FIN - NUIT_AUBE);
    return 0;
}
void ile_soir_maj(void) {
    if (!ile_nuits_on()) { ile_soir_dit = 0; return; }
    const float h = heure_ile();
    if (h >= NUIT_AUBE_FIN && h < NUIT_ALERTE) { ile_soir_dit = 0; return; }   /* le jour : les alertes du prochain soir sont prêtes */
    if (BT.on) return;   /* annoncé après le combat */
    if (!(ile_soir_dit & 1) && h >= NUIT_ALERTE && h < NUIT_DEBUT) { ile_soir_dit |= 1; hud_annonce("CRÉPUSCULE", "Le soleil se couche", "Mets-toi à l'abri avant la nuit"); }
    if (!(ile_soir_dit & 2) && (h >= NUIT_DEBUT || h < NUIT_AUBE)) { ile_soir_dit |= 3; hud_annonce("NUIT", "La nuit tombe", "Ne reste pas dehors"); }
}
/* Les abris (12/11/2026, chantier des nuits, étape 2 ; docs/ile-de-nacre-scenario.md) : les lieux du repos (E.w.ile_abri : fares, buvettes,
   camp de la Garde, campements, temple). Une fois le drapeau « nuits » levé, de DORMIR_DEBUT 17 h à DORMIR_FIN 6 h, la bulle DORMIR paraît
   au-dessus de la couche d'un abri à moins de DORMIR_PORTEE ; DORMIR, après confirmation (hud.c) : fondu au noir, le matin (ILE_MATIN
   6 h 30), toute l'équipe soignée, les K.O. debout (comme une nuit en cellule, police.c), carton « LE LENDEMAIN MATIN » et le nom de
   l'abri, puis la sauvegarde. La section « Abris » de l'émulateur de l'île, à garder identique */
#define DORMIR_DEBUT (17.0f / 24)
#define DORMIR_FIN (6.0f / 24)
#define ILE_MATIN (6.5f / 24)
#define DORMIR_PORTEE 1.6f   /* cases : la bulle DORMIR */
static int dodo_k = -1, dodo_retenu = -1;
static struct { int k, phase; float t; } DODO;   /* phase 1 : le noir tombe ; 2 : le matin, le carton */
int ile_abri_ici(float x, float z) {
    if (!E.w.ile[0] || E.w.carte) return -1;
    for (int k = 0; k < E.w.ile_nabris; k++) {
        const ile_abri_t *a = &E.w.ile_abri[k];
        if (E.in.on) { for (int j = 0; j < a->nbats; j++) if (a->bats[j] == E.in.bat) return k; }
        else if (hypotf(x - a->cx, z - a->cz) < a->r) return k;
    }
    return -1;
}
int ile_abri_proche(float x, float z) {
    int b = -1; float bd = 1e9f;
    for (int k = 0; k < E.w.ile_nabris; k++) { const float d = hypotf(x - E.w.ile_abri[k].x, z - E.w.ile_abri[k].z); if (d < bd) { bd = d; b = k; } }
    return b;
}
int ile_abris_vus(void) { return E.w.ile_nabris && ile_nuits_on() && !E.w.carte; }
int ile_dort(void) { return DODO.phase != 0; }
int ile_dormir_k(void) { return dodo_k; }
static void dodo_fondu(float a, float b, float d) { MISE.fondu_a = a; MISE.fondu_b = b; MISE.fondu_t = 0; MISE.fondu_d = d; }
/* Défaite contre un rôdeur de la nuit (chantier des nuits, étape 4) : on la retrouve à l'abri le plus proche (de la porte, si c'était dans
   un bâtiment), au matin : le noir tout de suite, puis le matin du sommeil (l'équipe soignée, le carton, la sauvegarde) et « On t'a
   retrouvée sans connaissance » (rodReveil de l'émulateur de l'île) */
static int reveil_dit;
static int rod_reveil_prendre(void);
static void rod_nuit_dormie(void);   /* une nuit hantée passée en dormant (section « Enlèvements ») */   /* une défaite de nuit attend son réveil (section « Rôdeurs », plus bas) */
static void rod_reveil(void) {
    const float ox = E.in.on ? E.in.rx : E.sera.x, oz = E.in.on ? E.in.rz : E.sera.z;
    const int k = ile_abri_proche(ox, oz);
    if (k < 0) return;
    if (E.in.on) int_reset();   /* dehors */
    E.ride = -1; E.has_dest = 0;
    sera_placer(E.w.ile_abri[k].x, E.w.ile_abri[k].z, 3.0f);
    E.stream_hold = 1;   /* la fenêtre recentrée sur elle */
    DODO.k = k; DODO.phase = 1; DODO.t = 0.9f;   /* le matin à l'image suivante */
    dodo_fondu(1, 1, 0.05f);   /* noir */
    reveil_dit = 1;
}
void ile_dormir_maj(float dt) {
    dodo_k = -1;
    if (!BT.on && !DODO.phase && rod_reveil_prendre()) rod_reveil();
    if (DODO.phase) {
        DODO.t += dt; E.has_dest = 0;
        if (DODO.phase == 1 && DODO.t >= 0.9f) {   /* au noir : le matin, l'équipe soignée */
            if (E.cycle != 3) E.heure = ILE_MATIN;   /* l'horloge du cycle (comme une nuit en cellule) */
            rod_nuit_dormie();   /* endormie avant 20 h : la nuit hantée a eu lieu (un enlèvement) */
            for (int i = 0; i < NMEMB; i++) { PARTY[i].hp = PARTY[i].max; PARTY[i].mp = PARTY[i].maxmp; }
            hud_title("LE LENDEMAIN MATIN", E.w.ile_abri[DODO.k].nom);
            dodo_fondu(1, 0, 1.0f); DODO.phase = 2; DODO.t = 0;
        } else if (DODO.phase == 2 && DODO.t >= HUD_TITLE_S) {   /* le carton lu : la main rendue, la partie sauvegardée */
            DODO.phase = 0;
            if (reveil_dit) { reveil_dit = 0; hud_toast("On t'a {retrouvée|retrouvé} sans connaissance"); }
            if (!hud_sauve_auto(1)) hud_sauve_demande(AUTO_TEMPS);
        }
        return;
    }
    const float h = heure_ile();
    if (!ile_abris_vus() || !(h >= DORMIR_DEBUT || h < DORMIR_FIN) || E.in.on || E.ride >= 0 || BT.on || CR.on || police_bloque()
        || hud_in_dialog() || hud_in_choice() || script_running() || mise_bloque()) return;
    float bd = DORMIR_PORTEE / E.w.ech;
    for (int k = 0; k < E.w.ile_nabris; k++) { const float d = hypotf(E.w.ile_abri[k].x - E.sera.x, E.w.ile_abri[k].z - E.sera.z); if (d < bd) { bd = d; dodo_k = k; } }
}
int ile_dormir_position(float *x, float *y, float *z) {
    if (dodo_k < 0) return 0;
    *x = E.w.ile_abri[dodo_k].x; *z = E.w.ile_abri[dodo_k].z; *y = ground_y(*x, *z) + 1.3f / E.w.ech;
    return 1;
}
/* La couche passe devant l'interlocuteur (OK) : personne à qui parler, ou elle plus près de l'héroïne que lui (comme coffre_avant) */
int ile_dormir_avant(void) {
    float x, y, z;
    if (!ile_dormir_position(&x, &y, &z)) return 0;
    if (E.talk >= 0 && E.talk < E.nnpc) return hypotf(x - E.sera.x, z - E.sera.z) < hypotf(E.npcs[E.talk].x - E.sera.x, E.npcs[E.talk].z - E.sera.z);
    return 1;
}
const char *ile_dormir_nom(void) { return dodo_k >= 0 ? E.w.ile_abri[dodo_k].nom : ""; }
void ile_dormir_retenir(void) { dodo_retenu = dodo_k; }
void ile_dormir_lancer(void) {
    if (dodo_retenu < 0 || dodo_retenu >= E.w.ile_nabris || DODO.phase) return;
    DODO.k = dodo_retenu; DODO.phase = 1; DODO.t = 0; dodo_retenu = -1;
    E.has_dest = 0;
    dodo_fondu(MISE.fondu, 1, 0.8f);
}
int ile_epuisee(void) { return E.w.ile[0] && E.w.ile_epuise < SC_MAX_FLAGS && ile_drapeau(E.w.ile_epuise); }
static int ile_esprits_vu = -1;   /* état du drapeau esprits à la dernière composition de l'équipe */
/* Lames éveillées (11/11/2026 : drapeau de chaque lame du paquet, js/lames-ile.js) : leurs esprits sont les compagnes, dans l'ordre des
   lames ; un paquet sans lames : les héroïnes au choix (OUVR) après celle qu'on joue, comme avant */
static uint64_t ile_lames_vu;
static uint32_t ile_fam_vu;   /* familières à la dernière composition de l'équipe */
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
    ile_fam_vu = ile_familieres();
    /* la familière (une succube charmée, 13/11/2026) : après les esprits des lames (ILE_ESPRITS au plus), ILE_COMPAGNES - ILE_ESPRITS au plus */
    int fam[ILE_COMPAGNES], nfam = 0;
    for (int s = 0; s < w->ile_nsucc && nfam < ILE_COMPAGNES - ILE_ESPRITS; s++) if ((ile_fam_vu >> s) & 1) {
        const int p = w->ile_succ[s].perso, p2 = w->ile_succ[s].perso2;
        if (p < w->npersos && p != E.heroine) fam[nfam++] = p;
        if (p2 < w->npersos && p2 != E.heroine && nfam < ILE_COMPAGNES - ILE_ESPRITS) fam[nfam++] = p2;   /* (une paire : sa partenaire aussi) */
    }
#define EST_FAM(p) (nfam > 0 && fam[0] == (p))
    if (w->ile_nlames) for (int k = 0; k < w->ile_nlames && k < 64 && ile_ncomp < ILE_ESPRITS && ile_esprits_vu; k++) {
        const int p = w->ile_lame[k].perso;
        if (!((ile_lames_vu >> k) & 1) || p >= w->npersos || p == E.heroine || EST_FAM(p)) continue;
        { int deja = 0; for (int c = 0; c < ile_ncomp; c++) deja |= ILE_COMP[c].perso == p; if (deja) continue; }
        memset(&ILE_COMP[ile_ncomp], 0, sizeof ILE_COMP[0]);
        ILE_COMP[ile_ncomp++].perso = p;
    }
    else for (int i = 0; i < w->ouv_nher && ile_ncomp < ILE_ESPRITS && ile_esprits_vu; i++) {
        const int p = w->ouv_her[i];
        if (p == E.heroine || EST_FAM(p) || (w->ouv_her18[i] && (w->edition == 1 || verrou18()))) continue;
        memset(&ILE_COMP[ile_ncomp], 0, sizeof ILE_COMP[0]);
        ILE_COMP[ile_ncomp++].perso = p;
    }
#undef EST_FAM
    for (int k = 0; k < nfam && ile_ncomp < ILE_COMPAGNES; k++) { memset(&ILE_COMP[ile_ncomp], 0, sizeof ILE_COMP[0]); ILE_COMP[ile_ncomp].perso = fam[k]; ILE_COMP[ile_ncomp++].familiere = 1; }
    fiche(&PARTY[0], E.heroine, 0);
    for (int m = 1; m <= ILE_COMPAGNES; m++) {
        w->party_perso[m] = -1;
        if (m > ile_ncomp) continue;
        const int p = ILE_COMP[m - 1].perso;
        PARTY[m].name = w->persos[p].nom;
        fiche(&PARTY[m], p, ILE_COMP[m - 1].familiere);
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
    if (E.w.ile[0] && ile_esprits_vu >= 0 && (ile_drapeau(E.w.ile_esprits) != ile_esprits_vu || ile_lames_eveillees() != ile_lames_vu || ile_familieres() != ile_fam_vu) && !BT.on) ile_equipe(0);   /* l'histoire vient de les faire paraître, ou une lame s'éveille */
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

/* ---------- Rôdeurs de la nuit (13/11/2026, chantier des nuits, étape 3 ; docs/ile-de-nacre-scenario.md, « Les nuits ») ----------
   Une fois le drapeau « hantise » levé (le chapitre 3 ; E.w.ile_hantise, 65535 : jamais), chaque nuit de 20 h à 6 h, un reflet de cendre
   (E.w.ile_rodeur : la version aux couleurs inversées de l'héroïne d'une lame qui dort encore) hante l'île : celui dont la lame est la plus
   proche de l'héroïne au moment où il paraît, parmi ceux dont la lame n'est pas éveillée (éveillée, elle le purifie) et dont le drapeau
   éventuel est levé. Dehors, hors d'un abri, à pied, après ROD_ATTENTE s (ROD_REVIENT s quand il revient), il paraît en haut de l'écran comme
   une patrouille (patrouille_rodeur de battle.c) : il rôde, la chasse à vue, engage le combat au contact ; il entre dans les villages (ils ne
   sont pas sûrs la nuit) mais jamais dans la zone d'un abri : il s'arrête à la limite, puis s'en va après ROD_ABRI_S s. Dans une maison qui
   n'est pas un abri, après ROD_MAISON s, il entre par la porte (ou monte l'escalier) et va droit sur elle par le plus court chemin de la
   grille de marche (une carte des distances dans la place libre de la fenêtre de l'intérieur : rien de réservé en plus). Vaincu, il ne
   revient plus de la nuit ; fui, il revient. Au lever du jour, il s'en va. Section « Rôdeurs » de l'émulateur de l'île, à garder identique */
/* (constantes ROD_* : internal.h) */
int ile_rodeurs_force;
static struct { int k, vaincu, combat, npc, ci, bfs, reveil, vision, nuit_vue, npc2; float attente, seuil, maison; } ROD = { -1, 0, 0, -1, -1, 0, 0, 0, 0, -1, 0, 0, 0 };   /* npc2 : la partenaire d'une paire de succubes, entrée avec elle */   /* nuit_vue : la nuit hantée a commencé (enlèvements) */   /* vision : rang + 1 de la lame à annoncer */
static int hantise_on(void) { return E.w.ile[0] && (ile_rodeurs_force || (E.w.ile_hantise < SC_MAX_FLAGS && ile_drapeau(E.w.ile_hantise))); }
int ile_rodeurs_nuit(void) {
    if (!hantise_on() || E.w.carte || !E.w.ile_nrodeurs) return 0;
    const float h = heure_ile();
    return h >= NUIT_DEBUT || h < NUIT_AUBE;
}
int ile_rodeur_k(void) { return ROD.k; }
int ile_rodeur_ek(int k) {
    if (k < 0 || k >= E.w.ile_nrodeurs) return -1;
    const int p = E.w.ile_rodeur[k].perso;
    for (int j = 0; j < E.w.nenemies; j++) if (E.w.enemies[j].perso == p) return j;
    return -1;
}
int ile_abri_zone(float x, float z) {   /* (un village aux lanternes allumées compte comme un abri : ILE_ABRIS_MAX + son rang) */
    if (!E.w.ile[0] || E.w.carte) return -1;
    for (int k = 0; k < E.w.ile_nabris; k++) if (hypotf(x - E.w.ile_abri[k].cx, z - E.w.ile_abri[k].cz) < E.w.ile_abri[k].r) return k;
    const int v = ile_lanterne_zone(x, z);
    return v >= 0 ? ILE_ABRIS_MAX + v : -1;
}
static int rod_valide(int k) {
    const ile_rodeur_t *r = &E.w.ile_rodeur[k];
    if (r->perso >= E.w.npersos || E.w.persos[r->perso].masque) return 0;
    if (r->drapeau != 0xFFFF && !(r->drapeau < SC_MAX_FLAGS && ile_drapeau(r->drapeau))) return 0;
    if (r->lame < E.w.ile_nlames) { const uint16_t f = E.w.ile_lame[r->lame].drapeau; if (f < SC_MAX_FLAGS && ile_drapeau(f)) return 0; }   /* sa lame éveillée : purifié */
    if (r->sorte == 1 && (r->succube >= E.w.ile_nsucc || ile_succube_charmee(r->succube))) return 0;   /* une succube charmée ne rôde plus */
    return ile_rodeur_ek(k) >= 0;
}
static int rod_choisir(void) {   /* celui dont la lame est la plus proche de l'héroïne (dedans : de la porte par où elle est entrée) */
    int b = -1; float bd = 1e30f;
    const float sx = E.in.on ? E.in.rx : E.sera.x, sz = E.in.on ? E.in.rz : E.sera.z;
    for (int k = 0; k < E.w.ile_nrodeurs; k++) {
        if (!rod_valide(k)) continue;
        const ile_rodeur_t *r = &E.w.ile_rodeur[k];
        const float d = r->lame < E.w.ile_nlames && E.w.ile_lame[r->lame].x >= 0 ? hypotf(E.w.ile_lame[r->lame].x - sx, E.w.ile_lame[r->lame].z - sz)
                      : r->x >= 0 ? hypotf(r->x - sx, r->z - sz) : 1e20f;   /* sans lame (une succube) : sa place, son village de chasse */
        if (d < bd) { bd = d; b = k; }
    }
    return b;
}
static int rod_dehors(void) { for (int s = 0; s < PATR_MAX; s++) if (PATR[s].on && PATR[s].rodeur) return 1; return 0; }
static void rod_dehors_retirer(void) { for (int s = 0; s < PATR_MAX; s++) if (PATR[s].rodeur) PATR[s].on = PATR[s].rodeur = 0; }
int ile_rodeur_intrus(void) {
    const interieur_t *I = &E.in;
    if (ROD.npc < 0 || !I->on || ROD.k < 0 || ROD.npc < I->npc0 || ROD.npc >= I->npc0 + I->npcn || E.npcs[ROD.npc].perso != E.w.ile_rodeur[ROD.k].perso) return -1;
    return ROD.npc;
}
/* Paire de succubes (13/11/2026, décision de l'utilisateur : « paire fixe », plus bas) : l'ennemie de la partenaire du rôdeur k ; son exemplaire
   entré dans la maison avec elle (ajouté juste après elle) */
static int ek_de(int p) { for (int j = 0; p >= 0 && j < E.w.nenemies; j++) if (E.w.enemies[j].perso == p) return j; return -1; }
int ile_rodeur_ek2(int k) {
    if (k < 0 || k >= E.w.ile_nrodeurs || E.w.ile_rodeur[k].sorte != 1 || E.w.ile_rodeur[k].succube >= E.w.ile_nsucc) return -1;
    const ile_succ_t *s = &E.w.ile_succ[E.w.ile_rodeur[k].succube];
    return s->perso2 < E.w.npersos && s->ennemi2 < E.w.npersos ? ek_de(s->ennemi2) : -1;
}
static int rod_intrus2(void) {
    const interieur_t *I = &E.in; const int e2 = ile_rodeur_ek2(ROD.k);
    if (ROD.npc2 < 0 || e2 < 0 || !I->on || ROD.npc2 < I->npc0 || ROD.npc2 >= I->npc0 + I->npcn || E.npcs[ROD.npc2].perso != E.w.enemies[e2].perso) return -1;
    return ROD.npc2;
}
static void rod_intrus_retirer(void) {   /* l'exemplaire entré dans la maison s'en va (le dernier ajouté : int_pnj_ajouter), et sa partenaire */
    interieur_t *I = &E.in;
    if (rod_intrus2() >= 0 && ROD.npc2 == E.nnpc - 1) { E.nnpc--; I->npcn--; if (E.talk == ROD.npc2) E.talk = -2; }
    ROD.npc2 = -1;
    if (ile_rodeur_intrus() >= 0 && ROD.npc == E.nnpc - 1) { E.nnpc--; I->npcn--; if (E.talk == ROD.npc) E.talk = -2; }
    ROD.npc = -1;
}
void ile_rodeurs_reset(void) {
    rod_dehors_retirer();
    ROD.k = -1; ROD.vaincu = ROD.combat = 0; ROD.npc = ROD.ci = ROD.npc2 = -1; ROD.attente = ROD.maison = 0; ROD.seuil = ROD_ATTENTE;
    ROD.reveil = ROD.vision = ROD.nuit_vue = 0;
}
static int rod_reveil_prendre(void) { if (!ROD.reveil) return 0; ROD.reveil = 0; return 1; }
void ile_rodeur_engage(void) { ROD.combat = 1; BT.nuit = 1; }   /* combat de nuit (battle.c : drain, emprise, aube, défaite) */
void ile_rodeur_fin(int won, int fui) {
    if (!ROD.combat) return;
    ROD.combat = 0; ROD.maison = 0; ROD.npc = -1; ROD.seuil = ROD_REVIENT;
    if (won && !fui) {   /* vaincu : plus de la nuit ; la vision de sa lame (VISION_*) */
        ROD.vaincu = 1;
        const int l = ROD.k >= 0 ? E.w.ile_rodeur[ROD.k].lame : 255;
        const uint16_t f = l < E.w.ile_nlames ? E.w.ile_lame[l].vision : 0xFFFF;
        if (f < SC_MAX_FLAGS && !((E.flags[f >> 3] >> (f & 7)) & 1)) { E.flags[f >> 3] |= (uint8_t)(1 << (f & 7)); ROD.vision = l + 1; }
    }
    if (!won && !fui) ROD.reveil = 1;   /* défaite : le réveil à l'abri le plus proche, au matin (ile_dormir_maj) */
}
static int rod_niveau(void) { const int l = E.level + ROD_NIV; return l > NIV_MAX ? NIV_MAX : l; }
static const float ROD_SIDE[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };   /* normale d'une sortie (SIDE_N d'interieur.c) */
static int rod_entree(float *x, float *z) {   /* la porte (rez-de-chaussée), sinon le haut de l'escalier */
    const interieur_t *I = &E.in;
    const float f = I->f > 0 ? I->f : 1;
    if (I->etage == 0 && I->nsort > 0) {
        const int_sortie_t *s = &I->sort[0];
        *x = IO + (s->x0 + s->x1) / 2 - ROD_SIDE[s->side][0] * 0.9f * f; *z = IO + (s->z0 + s->z1) / 2 - ROD_SIDE[s->side][1] * 0.9f * f;
        return 1;
    }
    for (int k = 0; k < I->nesc; k++) if (I->esc[k].vers < 0) { *x = IO + (I->esc[k].x0 + I->esc[k].x1) / 2; *z = IO + (I->esc[k].z0 + I->esc[k].z1) / 2; return 1; }
    return 0;
}
static int rod_marche(int g) { return g >= 1 && g <= 4; }   /* sol, seuil, escalier (pas l'eau d'un bassin) */
static const int8_t ROD_V[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
/* Carte des distances (4 voisins) depuis la case de l'héroïne : cases de la grille de marche, dans la place libre de la fenêtre */
static int rod_bfs(int si, int sj) {
    const interieur_t *I = &E.in;
    const int W = I->gw, H = I->gh, N = W * H;
    if (N <= 0 || N > 65535 || !I->libre || I->nlibre < (uint32_t)N * 4u || si < 0 || sj < 0 || si >= W || sj >= H) return 0;
    uint16_t *d = (uint16_t *)I->libre, *q = d + N;
    for (int i = 0; i < N; i++) d[i] = 0xFFFF;
    int h = 0, t = 0;
    d[sj * W + si] = 0; q[t++] = (uint16_t)(sj * W + si);
    while (h < t) {
        const int c = q[h++], ci = c % W, cj = c / W;
        for (int v = 0; v < 4; v++) {
            const int ni = ci + ROD_V[v][0], nj = cj + ROD_V[v][1];
            if (ni < 0 || nj < 0 || ni >= W || nj >= H) continue;
            const int n = nj * W + ni;
            if (d[n] != 0xFFFF || !rod_marche(I->grid[n])) continue;
            d[n] = (uint16_t)(d[c] + 1); q[t++] = (uint16_t)n;
        }
    }
    return 1;
}
static void rod_maison(float dt) {
    interieur_t *I = &E.in;
    const int lant = E.in.bat < E.w.nbats && ile_lanterne_zone(E.w.bats[E.in.bat].cx, E.w.bats[E.in.bat].cz) >= 0;   /* une maison d'un village aux lanternes allumées */
    if (ile_abri_ici(E.sera.x, E.sera.z) >= 0 || lant) { rod_intrus_retirer(); ROD.maison = 0; return; }   /* un abri : rien n'y entre */
    if (ile_rodeur_intrus() < 0) ROD.npc = ROD.npc2 = -1;   /* l'étage a changé : ses exemplaires refaits */
    const float f = I->f > 0 ? I->f : 1;
    if (ROD.npc < 0) {
        ROD.maison += dt;
        if (ROD.maison < ROD_MAISON) return;
        float x, z;
        if (!rod_entree(&x, &z)) return;
        ROD.npc = int_pnj_ajouter(E.w.ile_rodeur[ROD.k].perso, x, z, atan2f(E.sera.x - x, E.sera.z - z));
        if (ROD.npc < 0) return;
        { const int e2 = ile_rodeur_ek2(ROD.k); ROD.npc2 = e2 >= 0 ? int_pnj_ajouter(E.w.enemies[e2].perso, x, z, atan2f(E.sera.x - x, E.sera.z - z)) : -1; }   /* une paire : sa partenaire entre avec elle */
        ROD.ci = -1;
        son_jouer(SON_PORTE, 0.7f, 0.75f);   /* la porte grince */
        hud_toast("Quelque chose vient d'entrer");
    }
    npc_t *n = &E.npcs[ROD.npc];
    if (hypotf(E.sera.x - n->x, E.sera.z - n->z) < ROD_INT_CONTACT * f) {   /* au contact : le combat */
        if (ile_succube_proposer(ROD.k, -1)) { n->walking = 0; if (rod_intrus2() >= 0) E.npcs[ROD.npc2].walking = 0; return; }   /* une succube : se battre, ou s'offrir à elle (plus bas) */
        int ek[2] = { ile_rodeur_ek(ROD.k), ile_rodeur_ek2(ROD.k) }, var[2] = { 0, 0 };   /* (une paire : les deux) */
        const int ne = ek[1] >= 0 && rod_intrus2() >= 0 ? 2 : 1;
        rod_intrus_retirer();   /* il passe au combat (après, l'intérieur garde ses exemplaires tels quels : int_pret) */
        ROD.maison = 0;
        battle_start_groupe(ek, ne, rod_niveau(), 0, GROUPE_PARTS, var);
        if (BT.on) ile_rodeur_engage();
        return;
    }
    /* vers l'héroïne : la case voisine la plus proche d'elle sur la carte des distances, sinon droit sur elle */
    const int W = I->gw, si = (int)floorf((E.sera.x - IO + 2) / I->res), sj = (int)floorf((E.sera.z - IO + 2) / I->res);
    const int ci = si >= 0 && sj >= 0 && si < W && sj < I->gh ? sj * W + si : -1;
    if (ci != ROD.ci) { ROD.bfs = ci >= 0 && rod_bfs(si, sj); ROD.ci = ci; }
    float tx = E.sera.x, tz = E.sera.z;
    const int ni = (int)floorf((n->x - IO + 2) / I->res), nj = (int)floorf((n->z - IO + 2) / I->res);
    if (ROD.bfs && ni >= 0 && nj >= 0 && ni < W && nj < I->gh) {
        const uint16_t *d = (const uint16_t *)I->libre;
        const int c = nj * W + ni;
        if (d[c] != 0xFFFF && d[c] > 0) {
            int b = -1; uint16_t bd = d[c];
            for (int v = 0; v < 4; v++) {
                const int ai = ni + ROD_V[v][0], aj = nj + ROD_V[v][1];
                if (ai < 0 || aj < 0 || ai >= W || aj >= I->gh) continue;
                if (d[aj * W + ai] < bd) { bd = d[aj * W + ai]; b = v; }
            }
            if (b >= 0) { tx = IO - 2 + (ni + ROD_V[b][0] + 0.5f) * I->res; tz = IO - 2 + (nj + ROD_V[b][1] + 0.5f) * I->res; }
        }
    }
    const float dx = tx - n->x, dz = tz - n->z, dl = hypotf(dx, dz), pas = fminf(dl, ROD_INT_V * f * dt);
    n->walking = 0;
    if (dl > 1e-4f) {
        const float nx = n->x + dx / dl * pas, nz = n->z + dz / dl * pas;
        if (rod_marche(int_case(nx, nz))) { n->x = nx; n->z = nz; }
        else if (rod_marche(int_case(nx, n->z))) n->x = nx;
        else if (rod_marche(int_case(n->x, nz))) n->z = nz;
        n->dir = atan2f(dx, dz); n->walking = 1;
    }
    n->anim += dt;
    if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
    if (rod_intrus2() >= 0) {   /* la partenaire suit celle qui mène, à ROD_SUIT près */
        npc_t *n2 = &E.npcs[ROD.npc2];
        const float ex = n->x - n2->x, ez = n->z - n2->z, el = hypotf(ex, ez), p2 = fminf(fmaxf(0, el - ROD_SUIT * f), ROD_INT_V * f * dt);
        n2->walking = 0;
        if (p2 > 1e-4f) { const float nx = n2->x + ex / el * p2, nz = n2->z + ez / el * p2; if (rod_marche(int_case(nx, nz))) { n2->x = nx; n2->z = nz; } n2->dir = atan2f(ex, ez); n2->walking = 1; }
        n2->anim += dt; if (n2->anim > 0.16f) { n2->anim = 0; n2->frame++; }
    }
}
static void enl_maj(void);
static void nuit_finie(void);
void ile_rodeurs_maj(float dt) {
    enl_maj();   /* un habitant enlevé : l'annonce du matin, son retour (section « Enlèvements », plus bas) */
    if (ROD.vision && !BT.on) { hud_annonce("VISION", E.w.ile_lame[ROD.vision - 1].nom, "Elle dort par ici · Voir la carte"); ROD.vision = 0; }   /* après le combat */
    if (ile_offrande_en_cours()) return;   /* une succube : la séquence (même si le jour se lève) */
    if (!ile_rodeurs_nuit()) {   /* le jour : il s'en va ; la nuit suivante repart de zéro */
        if (ROD.nuit_vue) nuit_finie();   /* la nuit hantée s'achève : un enlèvement, si son reflet n'a pas été vaincu */
        if (ROD.k >= 0 || ROD.vaincu || ROD.npc >= 0 || ROD.nuit_vue || rod_dehors()) { rod_intrus_retirer(); ile_rodeurs_reset(); }
        return;
    }
    ROD.nuit_vue = 1;
    if (BT.on || CR.on || accueil_on() || script_running() || mise_bloque() || hud_in_dialog() || hud_in_choice() || police_bloque()) return;
    if (ROD.k < 0 || !rod_valide(ROD.k)) { rod_dehors_retirer(); rod_intrus_retirer(); ROD.k = rod_choisir(); }
    if (ROD.k < 0 || ROD.vaincu) { rod_intrus_retirer(); return; }
    if (E.in.on) { rod_maison(dt); return; }
    ROD.npc = -1; ROD.maison = 0;   /* dehors : les exemplaires de la maison sont partis avec elle */
    if (rod_dehors()) { ROD.attente = 0; return; }
    if (E.ride >= 0 || E.nage || ile_abri_zone(E.sera.x, E.sera.z) >= 0) return;   /* à l'abri, à bord ou à la nage : rien ne vient */
    ROD.attente += dt;
    if (ROD.attente < (ROD.seuil > 0 ? ROD.seuil : ROD_ATTENTE)) return;
    if (patrouille_rodeur(ROD.k, rod_niveau()) >= 0) { ROD.attente = 0; ROD.seuil = ROD_REVIENT; }
    else ROD.attente -= 2;   /* pas de place devant elle : un peu plus tard */
}

/* Visions des lames (VISION_* d'internal.h ; ileVisions de l'émulateur de l'île) : la zone de chaque lame dont le reflet a été vaincu et qui dort
   encore ; son centre (x, z) ; leur nombre (max au plus) */
int ile_visions(float *x, float *z, int max) {
    int n = 0;
    if (!E.w.ile[0] || E.w.carte) return 0;
    for (int k = 0; k < E.w.ile_nlames && n < max; k++) {
        const ile_lame_t *l = &E.w.ile_lame[k];
        const int vu = l->vision < SC_MAX_FLAGS && ((E.flags[l->vision >> 3] >> (l->vision & 7)) & 1);
        const int eveillee = l->drapeau < SC_MAX_FLAGS && ((E.flags[l->drapeau >> 3] >> (l->drapeau & 7)) & 1);
        if (!vu || eveillee || l->x < 0) continue;
        const float a = k * 2.399963f;   /* l'angle d'or : une direction par lame */
        x[n] = l->x + cosf(a) * VISION_R * VISION_DEC; z[n] = l->z + sinf(a) * VISION_R * VISION_DEC; n++;
    }
    return n;
}

/* ---------- Lanternes de nacre et villageois enlevés (13/11/2026, chantier des nuits, étape 5 ; docs/ile-de-nacre-scenario.md) ----------
   Villages (E.w.ile_village : les lieux habités, HistoireIle.villages du générateur) : chacun son drapeau lanternes_<village>. Ses lanternes
   allumées (l'histoire lève le drapeau), rien n'y entre la nuit : un rôdeur s'arrête au bord du village comme à celui d'un abri
   (ile_abri_zone), n'entre dans aucune de ses maisons (rod_maison), et personne n'y est enlevé ; un anneau nacré l'entoure sur la carte.
   Enlèvements : une nuit hantée qui s'achève sans que son reflet ait été vaincu (on dormait, on l'a fui, on a perdu), il emmène un habitant
   du village sans lanternes le plus proche de sa lame (un passant : il flâne, a un genre, ni meneur ni suiveur) et le laisse près de sa
   lame, entre ENL_LOIN0 et ENL_LOIN1 cases ; au matin, l'annonce « ENLÈVEMENT · VILLAGE · Un habitant a disparu · Vu vers la région ». Un
   seul à la fois. Retrouvé, il remercie (deux répliques selon son genre), donne ENL_RW0 + ENL_RW1 × niveau crédits et rentre chez lui :
   il marche vers sa place, et il y est dès qu'on ne le voit plus (ENL_VUE cases). Gardé dans la sauvegarde (save_t.libre : sa graine, sa
   place, sa lame, son village). Section « Enlèvements » de l'émulateur de l'île, à garder identique */
static uint32_t rng_l = 0x41C64E6Du;   /* tirages des enlèvements (habitant, place) */
static float lrand(void) { rng_l ^= rng_l << 13; rng_l ^= rng_l >> 17; rng_l ^= rng_l << 5; return (float)(rng_l >> 8) * (1.0f / 16777216.0f); }
#define ENL_LOIN0 4.0f
#define ENL_LOIN1 7.0f
#define ENL_VUE 14.0f
#define ENL_RW0 30
#define ENL_RW1 6
static struct { int npc, retour, dit, lame, village; float x, z, ohx, ohz; } ENL = { -1, 0, 0, 0, 0, 0, 0, 0, 0 };
static const char *const REGIONS_VERS[7] = { "vers les plages", "vers les prairies", "vers le sous-bois", "vers la jungle", "vers la mangrove",
                                              "vers les flancs du volcan", "vers le sommet" };
static int village_eclaire(int k) {
    const uint16_t f = E.w.ile_village[k].lanterne;
    return f < SC_MAX_FLAGS && ((E.flags[f >> 3] >> (f & 7)) & 1);
}
int ile_lanterne_zone(float x, float z) {
    if (!E.w.ile[0] || E.w.carte) return -1;
    for (int k = 0; k < E.w.ile_nvillages; k++) if (village_eclaire(k) && hypotf(x - E.w.ile_village[k].x, z - E.w.ile_village[k].z) < E.w.ile_village[k].r) return k;
    return -1;
}
int ile_lanternes(float *x, float *z, float *r, int max) {   /* villages aux lanternes allumées (carte du menu) */
    int n = 0;
    for (int k = 0; k < E.w.ile_nvillages && n < max; k++) if (village_eclaire(k)) { x[n] = E.w.ile_village[k].x; z[n] = E.w.ile_village[k].z; r[n] = E.w.ile_village[k].r; n++; }
    return n;
}
static npc_t *enl_npc(void) { return ENL.npc >= 0 && E.npc_tous && ENL.npc < E.npc_cnt[0] ? &E.npc_tous[ENL.npc] : NULL; }
static void enl_oublier(void) {   /* l'habitant rentre chez lui (une partie chargée, ou retrouvé et hors de vue) */
    npc_t *n = enl_npc();
    if (n) { n->x = n->hx = n->tx = ENL.ohx; n->z = n->hz = n->tz = ENL.ohz; n->walking = 0; n->wait = 1; }
    ENL.npc = -1; ENL.retour = ENL.dit = 0;
}
static int enl_poser(int k, float x, float z) {   /* l'habitant k (planète) emmené en (x, z) */
    npc_t *n = &E.npc_tous[k];
    ENL.npc = k; ENL.retour = 0; ENL.x = x; ENL.z = z; ENL.ohx = n->hx; ENL.ohz = n->hz;
    n->x = n->hx = n->tx = x; n->z = n->hz = n->tz = z; n->walking = 0; n->wait = 1;
    return 1;
}
/* Fin d'une nuit hantée : un enlèvement, si le reflet de la nuit n'a pas été vaincu */
static void nuit_finie(void) {
    if (!hantise_on() || E.w.carte || ENL.npc >= 0 || ROD.vaincu || !E.w.ile_nvillages) return;
    const int k = ROD.k >= 0 ? ROD.k : rod_choisir();
    if (k < 0) return;
    const int li = E.w.ile_rodeur[k].lame;
    if (li >= E.w.ile_nlames || E.w.ile_lame[li].x < 0) return;
    const float lx = E.w.ile_lame[li].x, lz = E.w.ile_lame[li].z;
    /* les villages sans lanternes, du plus proche de la lame au plus loin : le premier qui a des passants */
    int ord[ILE_VILLAGES_MAX], no = 0, v = -1, cand[64], nc = 0;
    for (int i = 0; i < E.w.ile_nvillages; i++) if (!village_eclaire(i)) ord[no++] = i;
    for (int i = 1; i < no; i++) for (int j = i; j > 0 && hypotf(E.w.ile_village[ord[j]].x - lx, E.w.ile_village[ord[j]].z - lz) < hypotf(E.w.ile_village[ord[j - 1]].x - lx, E.w.ile_village[ord[j - 1]].z - lz); j--) { const int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    for (int o = 0; o < no && !nc; o++) {
        const ile_village_t *vg = &E.w.ile_village[ord[o]];
        for (int i = 0; i < E.npc_cnt[0] && nc < 64; i++) {   /* ses passants */
            const npc_t *n = &E.npc_tous[i];
            const perso_t *pe = &E.w.persos[n->perso];
            if (!(pe->flags & PERS_FLANE) || (pe->flags & (PERS_VEHICULE | PERS_OBJET)) || !PERS_GENRE(pe->flags) || n->meneur || n->chef || pe->membre > 0 || pe->masque) continue;
            if (hypotf(n->hx - vg->x, n->hz - vg->z) < vg->r) cand[nc++] = i;
        }
        if (nc) v = ord[o];
    }
    if (v < 0 || !nc) return;
    const int who = cand[(int)(lrand() * nc) % nc];
    for (int t = 0; t < 48; t++) {   /* sa place : près de la lame, praticable, hors d'un abri */
        const float a = t * 0.7853982f + lrand() * 0.5f, d = ENL_LOIN0 + (ENL_LOIN1 - ENL_LOIN0) * (t % 3) / 2.0f;
        const float x = lx + sinf(a) * d, z = lz + cosf(a) * d;
        if (!sol_marchable(x, z) || ile_abri_zone(x, z) >= 0) continue;
        enl_poser(who, x, z); ENL.lame = li; ENL.village = v; ENL.dit = 1;
        return;
    }
}
/* Une nuit passée en dormant avant qu'elle ne commence (de 17 h à 20 h) : comme si elle s'était achevée (le sommeil d'ile_dormir_maj) */
static void rod_nuit_dormie(void) { if (hantise_on() && !ROD.nuit_vue) nuit_finie(); }
/* Chaque image : l'annonce du matin ; l'habitant retrouvé rentre dès qu'on ne le voit plus */
static void enl_maj(void) {
    npc_t *n = enl_npc();
    if (!n) { ENL.npc = -1; return; }
    if (ENL.dit && !BT.on && !ile_dort() && !E.in.on) {
        const int f = PERS_GENRE(E.w.persos[n->perso].flags) == 1, li = ENL.lame;
        const int rg = li < E.w.ile_nlames ? E.w.ile_lame[li].region : 255;
        char t[96];
        snprintf(t, sizeof t, "%s a disparu · %s %s", f ? "Une habitante" : "Un habitant", f ? "Vue" : "Vu", rg < 7 ? REGIONS_VERS[rg] : "dans la nuit");
        hud_annonce("ENLÈVEMENT", E.w.ile_village[ENL.village].nom, t);
        ENL.dit = 0;
    }
    if (ENL.retour && !E.w.carte && hypotf(n->x - E.sera.x, n->z - E.sera.z) > ENL_VUE) enl_oublier();
}
/* Les répliques de l'habitant retrouvé (open_dialog de hud.c) : 1 s'il l'est ; rec : la récompense, puis il rentre (ile_enleve_retrouve) */
int ile_enleve_lignes(int npc, const char **l1, const char **l2, int *rec) {
    static char a[160], b[120];
    npc_t *n = enl_npc();
    if (!n || E.w.carte || npc < 0 || npc >= E.npc_cnt[0] || &E.npcs[npc] != n) return 0;
    const int g = PERS_GENRE(E.w.persos[n->perso].flags);
    const char *vn = E.w.ile_village[ENL.village].nom;
    const int aux = (vn[0] == 'L' || vn[0] == 'l') && vn[1] == 'e' && vn[2] == 's' && vn[3] == ' ';   /* Les Cascades : aux Cascades */
    char lieu[48];
    if (aux) snprintf(lieu, sizeof lieu, "aux %s", vn + 4); else snprintf(lieu, sizeof lieu, "à %s", vn);
    if (ENL.retour) { snprintf(a, sizeof a, "Merci encore. Je rentre %s.", lieu); *l1 = a; *l2 = NULL; *rec = 0; return 1; }
    snprintf(a, sizeof a, g == 1 ? "Tu m'as retrouvée ! Cette nuit, une silhouette aux couleurs de cendre m'a emmenée jusqu'ici."
                         : g == 2 ? "Tu m'as retrouvé ! Cette nuit, une silhouette aux couleurs de cendre m'a emmené jusqu'ici."
                                  : "Te voilà ! Cette nuit, une silhouette aux couleurs de cendre m'a fait venir jusqu'ici.");
    snprintf(b, sizeof b, "Je rentre %s. Prends ça, pour ta peine.", lieu);
    *l1 = a; *l2 = b; *rec = 1;
    return 1;
}
void ile_enleve_retrouve(void) {
    npc_t *n = enl_npc();
    if (!n || ENL.retour) return;
    ENL.retour = 1; n->hx = n->tx = ENL.ohx; n->hz = n->tz = ENL.ohz; n->wait = 0; n->walking = 1;   /* il marche vers sa place */
    const int rw = ENL_RW0 + ENL_RW1 * E.level;
    char t[48];
    E.credits += rw;
    snprintf(t, sizeof t, "+%d crédits", rw); hud_toast(t);
    son_jouer(SON_VALIDER, 0.6f, 1.1f);
}
int ile_enleve(float *x, float *z) { if (!enl_npc() || ENL.retour) return 0; *x = ENL.x; *z = ENL.z; return 1; }   /* (essais) */
/* Sauvegarde (save_t.libre, 12 octets) : graine + 1 u16, place x, z u16 (1/16 de case), lame + 1 u8, village + 1 u8 ; 0 : personne */
void ile_enleve_ecrire(uint8_t *o) {
    memset(o, 0, 12);
    npc_t *n = enl_npc();
    if (!n || ENL.retour) return;
    const int g = n->graine + 1, x = (int)lroundf(ENL.x * 16), z = (int)lroundf(ENL.z * 16);
    o[0] = (uint8_t)g; o[1] = (uint8_t)(g >> 8); o[2] = (uint8_t)x; o[3] = (uint8_t)(x >> 8); o[4] = (uint8_t)z; o[5] = (uint8_t)(z >> 8);
    o[6] = (uint8_t)(ENL.lame + 1); o[7] = (uint8_t)(ENL.village + 1);
}
void ile_enleve_lire(const uint8_t *o) {
    enl_oublier();   /* celui d'avant rentre chez lui */
    const int g = (o[0] | o[1] << 8) - 1;
    if (g < 0 || !E.w.ile[0] || !E.npc_tous || !o[7] || o[7] > E.w.ile_nvillages) return;
    for (int i = 0; i < E.npc_cnt[0]; i++) if (E.npc_tous[i].graine == g) {
        enl_poser(i, (o[2] | o[3] << 8) / 16.0f, (o[4] | o[5] << 8) / 16.0f);
        ENL.lame = o[6] ? o[6] - 1 : 0; ENL.village = o[7] - 1;
        return;
    }
}
void ile_enleve_reset(void) { enl_oublier(); }
int ile_enleve_est(const npc_t *n) { return n && enl_npc() == n && !ENL.retour; }

/* ---------- Succubes nommées : s'offrir à elle (13/11/2026, demande de l'utilisateur ; édition 18+) ----------
   Une succube nommée (E.w.ile_succ : son héroïne, la familière ; son ennemie SUCCUBE NOM ; son drapeau d'apparition ; ses 5 drapeaux de charme)
   rôde comme un reflet, depuis son village de chasse (rôdeur de sorte 1, choisi d'après la distance à ce village). Dans l'édition 18+
   déverrouillée (charme18 de battle.c), à son contact, un choix : TE BATTRE (le combat de nuit) ou T'OFFRIR À ELLE : le noir ; elle face à
   l'héroïne, du côté d'où elle est venue (dans une maison : là où elle est) ; la séquence de la rencontre (OFFR_ETAPES animations de
   OFFR_ETAPE_S s, les planches offrande<R><K> de son héroïne, ANIM_OFFR0 + rang dans SPRS, gardées ici par leur planche de base :
   ile_offr_noter ; sans elle, sa danse, à défaut son attente ; « à deux » : l'héroïne est dans l'image, cachée) ; le noir ; le matin
   (ILE_MATIN), son drapeau de charme de la rencontre levé, la nuit finie sans enlèvement (elle ne revient plus de la nuit), la sauvegarde.
   Après OFFR_RENCONTRES rencontres, elle est charmée : elle ne rôde plus, elle devient une familière, une compagne de plus (ile_equipe ; sa
   fiche « id~succube », avec son pouvoir en PM). OK ou un toucher passent à l'animation suivante après OFFR_PASSER_S s. Section « Succubes »
   de l'émulateur de l'île, à garder identique */
#define OFFR_ETAPES 3
#define OFFR_RENCONTRES 5
#define OFFR_ETAPE_S 6.0f
#define OFFR_PASSER_S 1.0f
#define OFFR_DIST 0.9f
/* Paire (13/11/2026, décision de l'utilisateur : « paire fixe ») : une succube qui mène et sa partenaire (ile_succ_t.perso2, ennemi2)
   chassent toujours ensemble : une patrouille de deux (patrouille_rodeur de battle.c), deux intruses dans une maison (la partenaire suit,
   ROD_SUIT), le combat contre les deux, la séquence avec les deux (la partenaire à OFFR_COTE d'elle ; ses animations à trois viendront de
   la page Scènes), un seul charme, les textes au pluriel ; charmées, toutes deux familières (la première suit l'équipe) */
#define OFFR_COTE 0.75f
/* Cut-ins (13/11/2026, images Forge choisies par l'utilisateur, une par animation : ile_succ_t.cut, tools/ile/succubes_cutins.py) :
   au début de chaque animation, son image en plein écran OFFR_CUT_S s (hud.c), un toucher l'abrège après OFFR_CUT_MIN s */
#define OFFR_CUT_S 2.5f
#define OFFR_CUT_MIN 0.4f
static struct { int base; int16_t sh[ANIM_OFFR_N]; } OFFRT[ILE_SUCCUBES_MAX];
static int noffrt;
void ile_offr_reset(void) { noffrt = 0; }
void ile_offr_noter(int base, int k, int sheet) {   /* SPRS : l'animation k (0 à 14) d'offrande de la planche de base */
    if (base < 0 || k < 0 || k >= ANIM_OFFR_N) return;
    int t = 0;
    while (t < noffrt && OFFRT[t].base != base) t++;
    if (t == noffrt) { if (noffrt >= ILE_SUCCUBES_MAX) return; OFFRT[t].base = base; for (int i = 0; i < ANIM_OFFR_N; i++) OFFRT[t].sh[i] = -1; noffrt++; }
    OFFRT[t].sh[k] = (int16_t)sheet;
}
static int flag_on(uint16_t f) { return f < SC_MAX_FLAGS && ((E.flags[f >> 3] >> (f & 7)) & 1); }
static int succ_charmes(int s) { int n = 0; for (int r = 0; r < OFFR_RENCONTRES; r++) n += flag_on(E.w.ile_succ[s].charme[r]); return n; }
int ile_succube_charmee(int s) { return s >= 0 && s < E.w.ile_nsucc && succ_charmes(s) >= OFFR_RENCONTRES; }
uint32_t ile_familieres(void) {
    uint32_t m = 0;
    if (!E.w.ile[0] || !charme18_ile()) return 0;
    for (int s = 0; s < E.w.ile_nsucc; s++) if (E.w.ile_succ[s].perso < E.w.npersos && ile_succube_charmee(s)) m |= 1u << s;
    return m;
}
static struct { int k, s, r, phase, passer, slot, frame, npc, a_p, npc2; float t, ft, x, z, dir, px, pz, cut, x2, z2, cx, cz, sdir; } OFFR = { .k = -1 };   /* cx, cz, sdir : la scène (page Scènes), son origine et son devant */   /* npc2, x2, z2 : sa partenaire (paire) */
static int succ_paire(const ile_succ_t *s) { return s->perso2 < E.w.npersos && s->ennemi2 < E.w.npersos; }   /* cut : temps restant du cut-in */
static int offr_cut(int slot) {   /* l'image du cut-in de l'animation slot de la succube en cours (index IMAG) ; -1 aucune */
    uint16_t c = OFFR.k >= 0 && slot >= 0 && slot < 15 ? E.w.ile_succ[OFFR.s].cut[slot] : 0xFFFF;
    if (OFFR.k >= 0 && E.w.ile_ocj) {   /* (13/11/2026, chantier 3) celle du personnage joué, sinon celle de tous (offrCut de l'émulateur) */
        const int jou = E.heroine >= 0 && E.heroine < E.w.npersos ? E.heroine : E.w.ile_jou_repli;
        for (int i = 0; i < E.w.ile_nocj; i++) { const uint8_t *e = E.w.ile_ocj + i * 6;
            if (e[0] == OFFR.s && e[1] == slot && (e[2] | e[3] << 8) == jou) { c = (uint16_t)(e[4] | e[5] << 8); break; } }
    }
    return c < E.w.nimg && charme18_ile() ? c : -1;
}
static int succ_k = -1, succ_patr = -1;   /* le choix ouvert : rôdeur, patrouille (-1 : l'intruse d'une maison) */
static float succ_x, succ_z;              /* (d'où elle est venue) */
int ile_offrande_en_cours(void) { return OFFR.k >= 0; }
/* Au contact d'une succube (patrouille slot, ou -1 dans une maison) : le choix, dans l'édition 18+ ; 0 : pas de choix (le combat tout de suite) */
int ile_succube_proposer(int k, int slot) {
    if (k < 0 || k >= E.w.ile_nrodeurs || E.w.ile_rodeur[k].sorte != 1 || E.w.ile_rodeur[k].succube >= E.w.ile_nsucc || !charme18_ile()) return 0;
    const int s = E.w.ile_rodeur[k].succube;
    succ_k = k; succ_patr = slot;
    if (slot >= 0 && slot < PATR_MAX) { succ_x = PATR[slot].x[0]; succ_z = PATR[slot].z[0]; } else succ_x = succ_z = -1;
    int r = succ_charmes(s) + 1; if (r > OFFR_RENCONTRES) r = OFFR_RENCONTRES;
    const ile_succ_t *sc = &E.w.ile_succ[s];
    if (succ_paire(sc)) hud_succube_demander(E.w.persos[sc->perso].nom, E.w.persos[sc->perso2].nom, r, OFFR_RENCONTRES);
    else hud_succube_demander(E.w.persos[E.w.ile_rodeur[k].perso].nom, NULL, r, OFFR_RENCONTRES);
    return 1;
}
static void offrande_lancer(int k) {
    const int s = E.w.ile_rodeur[k].succube, npc = E.in.on ? ile_rodeur_intrus() : -1, npc2 = npc >= 0 ? rod_intrus2() : -1;
    rod_dehors_retirer();
    if (npc < 0) rod_intrus_retirer();
    ROD.vaincu = 1;   /* (cette nuit, elle ne revient plus ; pas d'enlèvement) */
    memset(&OFFR, 0, sizeof OFFR);
    OFFR.k = k; OFFR.s = s; OFFR.npc = npc; OFFR.npc2 = npc2;
    OFFR.r = succ_charmes(s); if (OFFR.r > OFFR_RENCONTRES - 1) OFFR.r = OFFR_RENCONTRES - 1;
    OFFR.a_p = succ_x >= 0; OFFR.px = succ_x; OFFR.pz = succ_z;
    E.has_dest = 0;
    dodo_fondu(MISE.fondu, 1, 0.6f);   /* le noir */
}
void ile_succube_choix(int c) {   /* TE BATTRE (0), T'OFFRIR À ELLE (1) */
    const int k = succ_k, slot = succ_patr;
    succ_k = succ_patr = -1;
    if (k < 0) return;
    if (c == 1) { offrande_lancer(k); return; }
    if (slot >= 0) patrouille_combat(slot);   /* le combat de cette patrouille (battle.c) */
    else {   /* dans une maison (une paire : les deux) */
        int ek[2] = { ile_rodeur_ek(k), ile_rodeur_ek2(k) }, var[2] = { 0, 0 };
        const int ne = ek[1] >= 0 && rod_intrus2() >= 0 ? 2 : 1;
        rod_intrus_retirer(); ROD.maison = 0; battle_start_groupe(ek, ne, rod_niveau(), 0, GROUPE_PARTS, var); if (BT.on) ile_rodeur_engage();
    }
}
void ile_offrande_maj(float dt) {
    if (OFFR.k < 0) return;
    OFFR.t += dt; E.has_dest = 0;
    const ile_succ_t *sc = &E.w.ile_succ[OFFR.s];
    if (OFFR.phase == 0 && OFFR.t >= 0.65f) {   /* au noir : elle face à l'héroïne */
        if (OFFR.npc >= 0 && ile_rodeur_intrus() == OFFR.npc) {   /* dans une maison : là où elle est, à OFFR_DIST case de l'héroïne si la place est libre */
            npc_t *n = &E.npcs[OFFR.npc];
            const float fi = E.in.f > 0 ? E.in.f : 1, dn = hypotf(n->x - E.sera.x, n->z - E.sera.z), a = dn > 0.05f ? atan2f(n->x - E.sera.x, n->z - E.sera.z) : E.sera.dir;
            const float x = E.sera.x + sinf(a) * OFFR_DIST * fi, z = E.sera.z + cosf(a) * OFFR_DIST * fi;
            if (dn < OFFR_DIST * fi && rod_marche(int_case(x, z))) { n->x = x; n->z = z; }
            OFFR.x = n->x; OFFR.z = n->z; n->walking = 0;
        }
        else {   /* dehors : à OFFR_DIST case de l'héroïne, du côté d'où elle est venue */
            OFFR.npc = -1;
            const float a = OFFR.a_p ? atan2f(OFFR.px - E.sera.x, OFFR.pz - E.sera.z) : E.sera.dir, x = E.sera.x + sinf(a) * OFFR_DIST, z = E.sera.z + cosf(a) * OFFR_DIST;
            const int ok = sol_marchable(x, z);
            OFFR.x = ok ? x : E.sera.x + 0.5f; OFFR.z = ok ? z : E.sera.z;
        }
        OFFR.dir = atan2f(E.sera.x - OFFR.x, E.sera.z - OFFR.z);
        if (OFFR.npc >= 0) E.npcs[OFFR.npc].dir = OFFR.dir;
        if (succ_paire(sc)) {   /* la partenaire à côté d'elle, OFFR_COTE (dans une maison : son intruse, si la place est libre) */
            const float fi = E.in.on && E.in.f > 0 ? E.in.f : 1, ax = cosf(OFFR.dir) * OFFR_COTE * fi, az = -sinf(OFFR.dir) * OFFR_COTE * fi;
            OFFR.x2 = OFFR.x; OFFR.z2 = OFFR.z;
            for (int c = 0; c < 2; c++) { const float sg = c ? -1.0f : 1.0f, x = OFFR.x + ax * sg, z = OFFR.z + az * sg;
                if (E.in.on ? rod_marche(int_case(x, z)) : sol_marchable(x, z)) { OFFR.x2 = x; OFFR.z2 = z; break; } }
            if (OFFR.npc2 >= 0 && rod_intrus2() == OFFR.npc2) { npc_t *n2 = &E.npcs[OFFR.npc2]; n2->x = OFFR.x2; n2->z = OFFR.z2; n2->dir = atan2f(E.sera.x - OFFR.x2, E.sera.z - OFFR.z2); n2->walking = 0; }
            else OFFR.npc2 = -1;
        }
        E.sera.dir = atan2f(OFFR.x - E.sera.x, OFFR.z - E.sera.z); E.sera.state = 0; E.sera.frame = 0;
        OFFR.cx = (E.sera.x + OFFR.x) * 0.5f; OFFR.cz = (E.sera.z + OFFR.z) * 0.5f;   /* la scène : son origine entre elles deux, */
        OFFR.sdir = E.cam.yaw + PI_F;   /* son devant (ligne S de la planche) vers la caméra (sera_row) */
        OFFR.phase = 1; OFFR.t = 0; OFFR.frame = 0; OFFR.ft = 0; OFFR.slot = OFFR.r * OFFR_ETAPES;
        OFFR.cut = offr_cut(OFFR.slot) >= 0 ? OFFR_CUT_S : 0;
        dodo_fondu(1, 0, 0.6f);
    } else if (OFFR.phase >= 1 && OFFR.phase <= OFFR_ETAPES) {   /* l'animation K de la rencontre R */
        OFFR.slot = OFFR.r * OFFR_ETAPES + OFFR.phase - 1;
        if (OFFR.cut > 0) { OFFR.cut -= dt; if (OFFR.cut < 0) OFFR.cut = 0; OFFR.t = 0; }   /* son cut-in d'abord : l'animation part après */
        OFFR.ft += dt; if (OFFR.ft > 0.11f) { OFFR.ft = 0; OFFR.frame++; }
        if (OFFR.cut <= 0 && (OFFR.t >= OFFR_ETAPE_S || OFFR.passer)) {
            OFFR.passer = 0; OFFR.t = 0; OFFR.phase++;
            if (OFFR.phase <= OFFR_ETAPES) { OFFR.frame = 0; OFFR.ft = 0; OFFR.slot = OFFR.r * OFFR_ETAPES + OFFR.phase - 1; OFFR.cut = offr_cut(OFFR.slot) >= 0 ? OFFR_CUT_S : 0; }
            else dodo_fondu(MISE.fondu, 1, 0.8f);   /* (la dernière image reste le temps du fondu) */
        }
    } else if (OFFR.phase == OFFR_ETAPES + 1 && OFFR.t >= 0.9f) {   /* au noir : le matin, son charme de cette rencontre */
        if (OFFR.npc >= 0) { rod_intrus_retirer(); OFFR.npc = OFFR.npc2 = -1; }
        if (E.cycle != 3) E.heure = ILE_MATIN;
        const uint16_t f = sc->charme[OFFR.r];
        if (f < SC_MAX_FLAGS) E.flags[f >> 3] |= (uint8_t)(1 << (f & 7));
        ROD.vaincu = 1;
        const int deux = succ_paire(sc);
        char nom[48], t[96];
        if (deux) snprintf(nom, sizeof nom, "%s ET %s", E.w.persos[sc->perso].nom, E.w.persos[sc->perso2].nom); else snprintf(nom, sizeof nom, "%s", sc->perso < E.w.npersos ? E.w.persos[sc->perso].nom : "");
        if (ile_succube_charmee(OFFR.s)) { snprintf(t, sizeof t, deux ? "%s te suivent désormais" : "%s te suit désormais", nom); hud_title(deux ? "CHARMÉES" : "CHARMÉE", t); }
        else { snprintf(t, sizeof t, "%s · rencontre %d sur %d", nom, OFFR.r + 1, OFFR_RENCONTRES); hud_title("LE LENDEMAIN MATIN", t); }
        dodo_fondu(1, 0, 1.0f);
        OFFR.phase = OFFR_ETAPES + 2; OFFR.t = 0;
    } else if (OFFR.phase == OFFR_ETAPES + 2 && OFFR.t >= HUD_TITLE_S) {   /* le carton lu : la main rendue, la partie sauvegardée */
        OFFR.k = -1;
        if (!hud_sauve_auto(1)) hud_sauve_demande(AUTO_TEMPS);
    }
}
int ile_offrande_passer(void) {   /* OK : l'animation suivante (pendant le noir, la touche ne fait rien d'autre) */
    if (OFFR.k < 0) return 0;
    if (OFFR.cut > 0) { if (OFFR_CUT_S - OFFR.cut >= OFFR_CUT_MIN) OFFR.cut = 0; return 1; }   /* le cut-in abrégé */
    if (OFFR.phase >= 1 && OFFR.phase <= OFFR_ETAPES && OFFR.t >= OFFR_PASSER_S) OFFR.passer = 1;
    return 1;
}
int ile_offrande_cutin(void) { return OFFR.k >= 0 && OFFR.phase >= 1 && OFFR.phase <= OFFR_ETAPES && OFFR.cut > 0 ? offr_cut(OFFR.slot) : -1; }
static int offr_vue(void) { return OFFR.k >= 0 && OFFR.phase >= 1 && OFFR.phase <= OFFR_ETAPES + 1; }
/* La scène de l'emplacement slot de la succube s (page Scènes de l'Atelier du combat : une planche avec tous ses personnages) : celle
   rendue pour la personne jouée, sinon celle de tous ; planche (SPRS), -1 aucune ; durée de sa boucle, drapeaux (1 : la personne jouée
   y est, 2 : la partenaire aussi) */
static int offr_scene(int s, int slot, float *duree, int *drap) {
    const world_t *w = &E.w;
    if (!w->ile_osc || s < 0) return -1;
    const int jou = E.heroine >= 0 && E.heroine < w->npersos ? E.heroine : w->ile_jou_repli;
    int best = -1;
    for (int i = 0; i < w->ile_nosc; i++) {
        const uint8_t *e = w->ile_osc + i * 10;
        const int j = e[4] | e[5] << 8, sh = e[6] | e[7] << 8;
        if (e[0] != s || e[1] != slot || sh >= w->nsheets || !w->sheets[sh].ok) continue;
        if (j == jou) { best = i; break; }
        if (j == 0xFFFF && best < 0) best = i;
    }
    if (best < 0) return -1;
    const uint8_t *e = w->ile_osc + best * 10;
    *drap = e[2]; *duree = (e[8] | e[9] << 8) / 100.0f;
    return e[6] | e[7] << 8;
}
/* Ce qui est à dessiner de la séquence (render.c) : i 0, la succube, i 1, sa partenaire (paire) ; place, regard, personnage, planche,
   image ; 1 : un personnage, 2 : la scène de l'emplacement (une planche, tous ses personnages, sans table de couleurs ; i 0 seulement),
   0 : rien de la séquence, -1 : caché (dans la scène, ou pendant le cut-in qui la précède : le tampon commun est à l'image) */
int ile_offrande_acteur(int i, float *x, float *z, float *dir, int *perso, int *sheet, int *frame) {
    if (!offr_vue()) return 0;
    const ile_succ_t *sc = &E.w.ile_succ[OFFR.s];
    if (i && !succ_paire(sc)) return 0;
    { float du = 0; int dr = 0; const int scn = offr_scene(OFFR.s, OFFR.slot, &du, &dr);
      if (scn >= 0) {
          if (OFFR.cut > 0 || (i && (dr & 2))) return -1;
          if (!i) {
              const int nf = E.w.sheets[scn].frames;
              *x = OFFR.cx; *z = OFFR.cz; *dir = OFFR.sdir; *perso = sc->perso; *sheet = scn;
              *frame = nf > 0 ? (int)(OFFR.t / (du > 0.05f ? du : 1.0f) * nf) % nf : 0;
              return 2;
          }
      } }
    const int pp = i ? sc->perso2 : sc->perso;
    if (pp >= E.w.npersos) return 0;
    const int base = E.w.persos[pp].sheet;
    int sh = -1;
    for (int t = 0; t < noffrt; t++) if (OFFRT[t].base == base) sh = OFFRT[t].sh[OFFR.slot];
    if (sh < 0) sh = anim_sheet(base, has_anim(base, ANIM_DANSE) ? ANIM_DANSE : ANIM_IDLE);   /* pas encore d'animation : sa danse, à défaut son attente */
    if (sh < 0) return 0;
    const int fr = OFFR.frame + (i ? 3 : 0);
    *x = i ? OFFR.x2 : OFFR.x; *z = i ? OFFR.z2 : OFFR.z; *dir = i ? atan2f(E.sera.x - OFFR.x2, E.sera.z - OFFR.z2) : OFFR.dir;
    *perso = pp; *sheet = sh; *frame = E.w.sheets[sh].frames ? fr % E.w.sheets[sh].frames : 0;
    return 1;
}
int ile_offrande_npc(int i) { return offr_vue() ? (i ? OFFR.npc2 : OFFR.npc) : -1; }
int ile_offrande_cache_sera(void) {
    if (!offr_vue()) return 0;
    float du; int dr; if (offr_scene(OFFR.s, OFFR.slot, &du, &dr) >= 0) return dr & 1;   /* la scène : la personne jouée y est */
    return (E.w.ile_succ[OFFR.s].duo >> OFFR.slot) & 1;
}
void ile_offrande_reset(void) { OFFR.k = -1; succ_k = succ_patr = -1; }
/* Essais (rendu_test offrande:s) : la séquence de la succube s là où est l'héroïne, comme si elle venait de la toucher et qu'on s'offrait à elle */
int ile_offrande_essai(int s) {
    for (int k = 0; k < E.w.ile_nrodeurs; k++) if (E.w.ile_rodeur[k].sorte == 1 && E.w.ile_rodeur[k].succube == s) {
        succ_x = succ_z = -1; ROD.k = k;
        offrande_lancer(k);
        return 1;
    }
    return 0;
}
void ile_offrande_etat(int *phase, int *slot, float *t) { *phase = OFFR.k >= 0 ? OFFR.phase : -1; *slot = OFFR.slot; *t = OFFR.cut > 0 ? -OFFR.cut : OFFR.t; }   /* (t < 0 : le cut-in, son temps restant) */   /* essais (rendu_test offretat) */

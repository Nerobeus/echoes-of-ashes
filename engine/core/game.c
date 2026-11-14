/* Logique du jeu sur la world map : déplacement de Sera, collisions, caméra, cycle de l'eau, rencontres */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "internal.h"
#include "audio.h"
#include "ui.h"

static uint32_t rng_s = 0x9E3779B9u;
static float frand(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5; return (float)(rng_s >> 8) * (1.0f / 16777216.0f); }
/* Générateur propre aux rencontres : les tirer ne change pas la flânerie des exemplaires */
/* Palette d'un exemplaire (PVAR) d'après sa graine : 0 (ses couleurs) ou 1..nvar, comme pvarChoix de l'émulateur */
int pvar_choix(int perso, unsigned graine) {
    if (perso < 0 || perso >= E.w.npersos || !E.w.persos[perso].nvar) return 0;
    return (int)(((uint32_t)graine * 0x9E3779B1u) >> 16) % (E.w.persos[perso].nvar + 1);
}
int perso_remap(int perso, int var) {
    if (perso < 0 || perso >= E.w.npersos) return 0;
    const perso_t *pe = &E.w.persos[perso];
    return var > 0 && var <= pe->nvar ? pe->var0 + var - 1 : pe->remap;
}
static uint32_t rng_e = 0x6C078965u;
static float erand(void) { rng_e ^= rng_e << 13; rng_e ^= rng_e >> 17; rng_e ^= rng_e << 5; return (float)(rng_e >> 8) * (1.0f / 16777216.0f); }

/* Un personnage à moins de 0,6 m de (x, z), sauf self (NULL : Sera, qui ne se gêne pas elle-même) */
static int char_at(float x, float z, const npc_t *self) {
    const float r = 0.6f / E.w.ech;
    if (self && E.ride < 0 && hypotf(x - E.sera.x, z - E.sera.z) < r) return 1;
    for (int k = 0; k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (n == self || fabsf(n->x - x) >= r || fabsf(n->z - z) >= r || !npc_present(n)) continue;
        if (hypotf(n->x - x, n->z - z) < r) return 1;
    }
    for (int k = 0; k < TRAFFIC_MAX; k++)   /* véhicules du trafic : un peu plus larges */
        if (E.traffic[k].on && hypotf(E.traffic[k].x - x, E.traffic[k].z - z) < r * 1.6f) return 1;
    return 0;
}

/* Véhicule conduit : quelqu'un à moins de r de (x, z) dont on se rapproche en venant de (fx, fz). Au contact d'un
   véhicule garé, on peut toujours s'éloigner (sinon on resterait collé) */
static int char_block(float x, float z, float fx, float fz, const npc_t *self) {
    const float r = 0.6f / E.w.ech;
#define CLOSER(ox, oz, rr) (hypotf((ox) - x, (oz) - z) < (rr) && hypotf((ox) - x, (oz) - z) < hypotf((ox) - fx, (oz) - fz))
    for (int k = 0; k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (n == self || fabsf(n->x - x) >= r || fabsf(n->z - z) >= r || !npc_present(n)) continue;
        if (CLOSER(n->x, n->z, r)) return 1;
    }
    for (int k = 0; k < TRAFFIC_MAX; k++)
        if (E.traffic[k].on && CLOSER(E.traffic[k].x, E.traffic[k].z, r * 1.6f)) return 1;
#undef CLOSER
    return 0;
}

static int rect_casse(const float *r);   /* décors cassés (plus bas) */
/* Eau de la case (drapeaux f) : 0 terre ou pont, 1 eau claire, 2 eau noire */
static int water_kind(uint8_t f) { return (f & F_ROAD) || !(f & (F_WATER | F_RIVER)) ? 0 : (f & F_CLEAR) ? 1 : 2; }
static int sera_etanche(void) { const int t = tenue_of(0, 1); return t >= 0 && E.w.tenues[t].etanche; }
/* Case praticable : dans la carte, pas d'eau ni de rivière hors d'un pont (sauf à la nage, swim : l'eau claire, et les
   eaux noires avec une tenue étanche), ni bâtiment, ni autre personnage */
static int sol_libre(float x, float z, int swim);
static int walkable_for(float x, float z, const npc_t *self, int swim) {
    if (E.in.on && !self) return int_libre(x, z);   /* intérieur : sa grille de marche */
    return sol_libre(x, z, swim) && !char_at(x, z, self);
}
/* Pas de Sera à pied : comme walkable_for, mais on peut toujours s'éloigner de quelqu'un qu'on touche (posée contre un
   garde, sinon elle resterait collée) */
static int coul_sortie;   /* coulées (plus bas, avec l'avalanche) */
static int coulee_quitter(float x0, float z0, float x, float z, float marge);
static int sera_pas(float x, float z) {
    if (E.in.on) return int_libre(x, z);
    coul_sortie = E.w.ncoul ? coulee_quitter(E.sera.x, E.sera.z, x, z, COUL_MARGE / E.w.ech) : -1;   /* dans une coulée : on en sort */
    const int ok = sol_libre(x, z, 1) && !char_block(x, z, E.sera.x, E.sera.z, NULL);
    coul_sortie = -1;
    return ok;
}
/* Place libre pour le corps de Sera dehors (marges MARGE_MUR et MARGE_DECOR comprises ; sortie d'un intérieur : interieur.c) */
int sera_libre(float x, float z) { return !E.in.on && sol_libre(x, z, 0); }
/* Sera posée sur la place libre la plus proche de (x0, z0), jusqu'à rmax cases (nearestWalkable de l'émulateur) ;
   rien de libre (chunks pas encore chargés) : au point exact */
void sera_placer(float x0, float z0, float rmax) {
    for (float r = 0; r <= rmax; r += 0.5f)
        for (int a = 0; a < (r > 0 ? 16 : 1); a++) {
            const float x = x0 + cosf(a * PI_F / 8) * r, z = z0 + sinf(a * PI_F / 8) * r;
            if (walkable_for(x, z, NULL, 0)) { E.sera.x = x; E.sera.z = z; return; }
        }
    E.sera.x = x0; E.sera.z = z0;
}
#define MARGE_MUR 0.35f     /* m */
#define MARGE_DECOR 0.1f
static int sol_libre(float x, float z, int swim) {
    const world_t *w = &E.w;
    int n = w->n;
    if (x < 2 || z < 2 || x > n - 2 || z > n - 2) return 0;
    int i = (int)floorf(x), j = (int)floorf(z);
    const chunk_t *c = chunk_of_cell(i, j);   /* chunk pas encore chargé : on attend */
    if (!c) return 0;
    uint8_t f = c->flags[cell_local(i, j)];
    if ((f & F_RAIDE) && !(swim == 1 && case_denivele(i, j) <= PIED_RAIDE)) return 0;   /* trop raide : à pied, seulement au-delà de PIED_RAIDE */
    const int wk = water_kind(f);
    if (swim == 2) { if (!wk) return 0; }   /* bateau acteur (scene_libre_r) : l'eau seulement */
    else if (wk && !(swim && (wk == 1 || sera_etanche()))) return 0;
    /* Rectangles de collision : chacun est rangé dans tous les chunks qu'il touche, marge comprise. Marge (le corps de
       Sera) : MARGE_MUR contre un mur, MARGE_DECOR contre un petit obstacle (tronc, rocher, objet : moins d'une case de
       côté ; avec 0,35 m partout, un tronc de 0,26 case en bloquait 0,8 et une forêt dense ne se traversait pas) */
    const float mm = MARGE_MUR / w->ech, md = MARGE_DECOR / w->ech;
    for (uint32_t k = 0; k < c->nsolid; k++) {
        const float *r = &c->solids[k * 4];
        const float m = r[2] - r[0] < 1 && r[3] - r[1] < 1 ? md : mm;
        if (x > r[0] - m && x < r[2] + m && z > r[1] - m && z < r[3] + m && !rect_casse(r)) return 0;
    }
    if (w->ncoul && coulee_dans(x, z, COUL_MARGE / w->ech)) return 0;   /* coulée d'une avalanche (sauf celle qu'on quitte : coul_sortie) */
    return 1;
}

/* ---------- Héroïne choisie : la recrue de son nom se montre en Sera (internal.h) ---------- */
int heroine_recrue(void) {
    const int k = E.heroine;
    return k >= 0 && k < E.w.npersos && E.w.persos[k].membre > 0 ? E.w.persos[k].membre : -1;
}
int membre_en_sera(int m) { return m > 0 && m == heroine_recrue() && E.w.psheet >= 0; }
int perso_en_sera(int p) { return p >= 0 && p < E.w.npersos && membre_en_sera(E.w.persos[p].membre); }
/* Taille de Sera : celle de sa planche, même dans ses tenues */
static float sera_taille(void) { return E.w.psheet >= 0 ? E.w.sheets[E.w.psheet].taille : 1.0f; }
perso_vue_t perso_vue(int p, int var) {
    /* hors de la bibliothèque (-1 : aucun personnage) : Sera, sans lire avant le tableau (-Warray-bounds de l'ESP-IDF) */
    if (perso_en_sera(p) || p < 0 || p >= E.w.npersos) return (perso_vue_t){ E.w.psheet, 0, OMBRE_JOUEUR, 0, sera_taille() };
    const perso_t *pe = &E.w.persos[p];
    return (perso_vue_t){ pe->sheet, perso_remap(p, var), p, pe->vremap, pe->taille };
}

/* ---------- Tenues ---------- */
/* Porteur des tenues du membre m : le joueur (Sera, ou l'héroïne choisie au départ), sinon le personnage de la
   bibliothèque de son nom (-1 : aucun) ; la recrue de l'héroïne choisie : celles de Sera (TENUE_JOUEUR) */
static int tenue_owner(int m) {
    if (m == 0) return E.heroine >= 0 ? E.heroine : TENUE_JOUEUR;
    return membre_en_sera(m) ? TENUE_JOUEUR : E.w.party_perso[m];
}
/* Une tenue 18+ n'est à personne tant que le 18+ est verrouillé (édition unique, tenue_vue) */
int tenue_mine(int m, int t) { return m >= 0 && m < NMEMB && t >= 0 && t < E.w.ntenues && E.w.tenues[t].owner == tenue_owner(m) && tenue_vue(t); }
int tenue_named(int m, const char *nom) {
    for (int t = 0; nom && t < E.w.ntenues; t++) if (tenue_mine(m, t) && !strcmp(E.w.tenues[t].nom, nom)) return t;
    return -1;
}
/* Mise d'office : nage (Sera dans l'eau claire ; dans les eaux noires, sa tenue portée, étanche), course, drapeau du
   scénario levé (la dernière de la liste) ; sinon celle qu'il porte (menu ÉQUIP., instruction « tenue ») */
int tenue_of(int m, int sec) {
    const world_t *w = &E.w;
    const int own = m >= 0 && m < NMEMB ? tenue_owner(m) : -1;
    if (own < 0) return -1;
    int nage = -1, course = -1, drap = -1;
    for (int t = 0; t < w->ntenues; t++) {
        const tenue_t *T = &w->tenues[t];
        if (T->owner != own || !tenue_vue(t)) continue;
        if (T->ctx == TENUE_NAGE) { if (nage < 0) nage = t; }
        else if (T->ctx == TENUE_COURSE) { if (course < 0) course = t; }
        else if (T->ctx == TENUE_DRAPEAU && T->flag < SC_MAX_FLAGS && ((E.flags[T->flag >> 3] >> (T->flag & 7)) & 1)) drap = t;
    }
    /* Dans l'eau : la tenue choisie au menu ÉQUIP. (plus de tenue de bain mise d'office pour l'équipe, depuis le
       29/10/2026 ; les nageurs des piscines gardent la leur : tenue_nage) */
    (void)nage; (void)sec;
    if (CR.on && course >= 0) return course;
    if (drap >= 0) return drap;
    if (tenue_mine(m, E.tenue_eq[m])) return E.tenue_eq[m];
    /* Armure qui se voit (bikini, 18+ : sa tenue de même nom, ITEMS[].tenue) : portée tant que le menu ÉQUIP. ou
       l'instruction « tenue » n'en a pas mis une autre (03/11/2026, tenueOf de l'émulateur) */
    const int a = E.eq[m][EQ_ARMURE];
    return a != EQ_AUCUN && a < n_items && ITEMS[a].type == 5 && ITEMS[a].tenue ? tenue_named(m, ITEMS[a].tenue) : -1;
}
/* Tenue de base d'une variante de couleur (même porteur, même planche, sans table) : ses cut-ins servent aux variantes */
int tenue_base(int t) {
    if (t < 0 || t >= E.w.ntenues || !E.w.tenues[t].vremap) return t;
    for (int k = 0; k < E.w.ntenues; k++)
        if (!E.w.tenues[k].vremap && E.w.tenues[k].owner == E.w.tenues[t].owner && E.w.tenues[k].sheet == E.w.tenues[t].sheet) return k;
    return t;
}
int tenue_nage(int perso) {
    for (int t = 0; t < E.w.ntenues; t++) if (E.w.tenues[t].owner == perso && E.w.tenues[t].ctx == TENUE_NAGE) return t;
    return -1;
}
int member_sheet(int m) {
    const int t = tenue_of(m, 0);
    if (t >= 0) return E.w.tenues[t].sheet;
    return member_sheet_base(m);
}
/* Planche du membre m sans sa tenue */
int member_sheet_base(int m) {
    if (m == 0) return E.heroine >= 0 ? E.w.persos[E.heroine].sheet : E.w.psheet;
    const int pp = m > 0 && m < NMEMB ? E.w.party_perso[m] : -1;
    return pp >= 0 ? perso_vue(pp, 0).sheet : -1;   /* la recrue de l'héroïne choisie : la planche de Sera */
}
/* Planche du membre m en mouvement (04/11/2026) : une tenue de pose (pole dance, twerk, champagne de Jade : ni marche ni
   course, la même tenue que sa planche de base, qu'on ne fait pas marcher depuis sa pose) prend la marche et la course de
   la planche de base ; seraAnim de l'émulateur */
int member_sheet_mouv(int m) {
    const int ps = member_sheet(m), b = member_sheet_base(m);
    return ps >= 0 && b >= 0 && !has_anim(ps, ANIM_WALK) && !has_anim(ps, ANIM_RUN) ? b : ps;
}
/* Taille du joueur : celle de Sera (sa planche, et ses tenues), ou celle du personnage de l'héroïne choisie, même dans
   ses tenues (comme appliquerHeroine de l'émulateur ; avant le 01/11/2026, la taille de la planche portée, 1 pour une
   héroïne) */
float joueur_taille(void) { return E.heroine >= 0 ? E.w.persos[E.heroine].taille : sera_taille(); }
int member_tissu(int m) { const int t = tenue_of(m, 0); return t >= 0 ? E.w.tenues[t].vremap : 0; }
float swim_alt(void) {
    const int i = (int)floorf(E.sera.x), j = (int)floorf(E.sera.z);
    const chunk_t *c = chunk_of_cell(i, j);
    const float s = c && c->water_h[cell_local(i, j)] > -1e8f ? c->water_h[cell_local(i, j)] : E.w.wl, a = s - ground_y(E.sera.x, E.sera.z);
    return a > 0 ? a : 0;
}

/* ---------- Trafic ---------- */
/* Jusqu'à traf_max véhicules (ceux de la bibliothèque marqués PERS_TRAFIC, sauf bateau et vaisseau) autour de Sera. Ils
   apparaissent entre 10 et 18 cases d'elle et disparaissent au-delà de 22. Une seule circulation (01/11/2026, demande de
   l'utilisateur : plus de trafic des routes et de trafic des rues à part) : sur les routes, les véhicules suivent les axes
   (ROUT) sur la voie de droite ; dans les rues des villes et des villages, de case en case, sur la voie de droite de la
   rue (voie_rue), plus lentement (TRAF_LENT). Au bout d'un axe : un autre axe qui passe à moins de 1,2 case, ou la rue
   devant (rue_entree) ; dans une rue : le bout d'un axe à moins de SORTIE_R cases qui continue la marche (rue_suivant) ;
   d'un réseau à l'autre, une liaison en ligne droite (mode POUR_RALLIE), sans demi-tour ni saut. Demi-tour seulement au
   bout d'une impasse. Arrêt si un piéton est devant, un véhicule devant dans le même sens et la même voie (gene_veh : un
   véhicule qui vient en face ne gêne jamais ; sur un chemin ou une rue trop étroits pour deux, chacun se serre sur le
   bas-côté et ralentit), ou si Sera s'adresse à eux (pour les voler) ; comme l'émulateur. */
/* Case de rue pour le trafic « case par case » : les rues des villes (F_STREET) ; sans axes de routes (ROUT, ancien
   paquet), les routes aussi. Jamais sous un bâtiment ni dans un rectangle de collision. */
static int is_road(int i, int j) {
    if (i < 2 || j < 2 || i >= E.w.n - 2 || j >= E.w.n - 2) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c || !(c->flags[cell_local(i, j)] & (E.w.nroutes ? F_STREET : F_ROAD | F_STREET))) return 0;
    const float x = i + 0.5f, z = j + 0.5f;
    for (uint32_t k = 0; k < c->nsolid; k++) {
        const float *r = &c->solids[k * 4];
        if (x > r[0] && x < r[2] && z > r[1] && z < r[3]) return 0;
    }
    return 1;
}
static void pour_case(traffic_t *t);   /* poursuivants de la police (plus bas) : leurs choix aux carrefours */
static void pour_lane_fin(traffic_t *t, float ex, float ez, const int *cr, const int *cs, const float *ck, int nc);
static int pour_patrouille(float x, float z);
static void traffic_next(traffic_t *t);   /* rues : le pas suivant (plus bas, après les axes) */
static int chaussee_at(float x, float z);   /* route ou rue en (x, z) (plus bas, avec les piétons) */

/* ---------- Axes des routes (ROUT) : le trafic roule sur la voie de droite ---------- */
static void route_pt(const route_t *r, int i, float *x, float *z) { memcpy(x, r->p + i * 8, 4); memcpy(z, r->p + i * 8 + 4, 4); }
/* Normale vers la droite de l'axe parcouru dans le sens croissant, au point i (moyenne des deux segments voisins) */
static void route_nrm(const route_t *r, int i, float *nx, float *nz) {
    float ax, az, bx, bz;
    route_pt(r, i > 0 ? i - 1 : 0, &ax, &az); route_pt(r, i + 1 < r->n ? i + 1 : r->n - 1, &bx, &bz);
    float tx = bx - ax, tz = bz - az, l = hypotf(tx, tz);
    if (l < 1e-4f) l = 1;
    *nx = tz / l; *nz = -tx / l;
}
static float lane_len(const traffic_t *t) {
    float ax, az, bx, bz;
    route_pt(&E.w.routes[t->road], t->seg, &ax, &az); route_pt(&E.w.routes[t->road], t->seg + 1, &bx, &bz);
    const float l = hypotf(bx - ax, bz - az);
    return l > 1e-3f ? l : 1e-3f;
}
/* Croisement (01/11/2026, demande de l'utilisateur : les voitures ne se croisaient pas sur les chemins) : deux véhicules
   se croisent si chacun roule à CROISE_DEMI case au moins de l'axe. Sur une voie plus étroite (rue d'une case, ou chemin
   d'une case des paquets d'avant le 01/11/2026 : voie à 0,25 ; les chemins ont 2 cases depuis, voies à 0,5 comme la
   route), celui qui roule se serre sur le bas-côté (serre, jusqu'à SERRE_VOIE de l'axe) et ralentit (SERRE_ALLURE)
   quand un véhicule vient en face à moins de SERRE_DIST cases, sans s'arrêter ni faire demi-tour */
#define CROISE_DEMI 0.5f
#define SERRE_VOIE 0.62f
#define SERRE_RUE (SERRE_VOIE - 0.25f)   /* rue d'une case : de sa voie (0,25) au bas-côté */
#define SERRE_DIST 5.0f
#define SERRE_ALLURE 0.5f
#define SERRE_VITE 2.0f                  /* par seconde */
static float lane_off(const route_t *r, float serre) {
    const float o = r->hw * 0.5f;
    return o < CROISE_DEMI ? o + (SERRE_VOIE - o) * serre : o;
}
/* Place sur la voie de droite (au quart de la largeur depuis l'axe : milieu de la demi-chaussée) de l'abscisse k du
   segment seg de l'axe road parcouru dans le sens sgn */
static void lane_point(int road, int seg, int sgn, float k, float serre, float *x, float *z, float *dir) {
    const route_t *r = &E.w.routes[road];
    const int a = sgn > 0 ? seg : seg + 1, b = sgn > 0 ? seg + 1 : seg;
    float ax, az, bx, bz, nax, naz, nbx, nbz;
    route_pt(r, a, &ax, &az); route_pt(r, b, &bx, &bz); route_nrm(r, a, &nax, &naz); route_nrm(r, b, &nbx, &nbz);
    const float off = lane_off(r, serre) * sgn;
    *x = ax + (bx - ax) * k + (nax + (nbx - nax) * k) * off;
    *z = az + (bz - az) * k + (naz + (nbz - naz) * k) * off;
    if (dir) *dir = atan2f(bx - ax, bz - az);
}
static void lane_place(traffic_t *t) { lane_point(t->road, t->seg, t->sgn, t->k, t->serre, &t->x, &t->z, &t->dir); }
/* Bout de l'axe : un autre axe qui passe à moins de 1,2 case (carrefour, embranchement), dans le sens qui s'éloigne de
   son extrémité (au hasard au milieu d'un axe), ou la rue devant (entrée d'une ville ou d'un village : rue_entree, puis
   une liaison), au hasard ; sinon demi-tour */
static int rue_entree(const traffic_t *t, float ex, float ez, int *ri, int *rj);
static void liaison_rue(traffic_t *t, int ri, int rj, float hx, float hz);
static void lane_next(traffic_t *t) {
    const world_t *w = &E.w;
    const route_t *r = &w->routes[t->road];
    float ex, ez;
    route_pt(r, t->sgn > 0 ? r->n - 1 : 0, &ex, &ez);
    int cr[8], cs[8], cg[8], nc = 0; float ck[8];
    for (int q = 0; q < w->nroutes && nc < 8; q++) {
        if (q == t->road) continue;
        const route_t *o = &w->routes[q];
        float bd = 1.2f, bk = 0; int bs = -1;
        for (int s = 0; s + 1 < o->n; s++) {
            float ax, az, bx, bz;
            route_pt(o, s, &ax, &az); route_pt(o, s + 1, &bx, &bz);
            if (fminf(ax, bx) > ex + bd || fmaxf(ax, bx) < ex - bd || fminf(az, bz) > ez + bd || fmaxf(az, bz) < ez - bd) continue;
            const float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
            float k = l2 > 0 ? ((ex - ax) * dx + (ez - az) * dz) / l2 : 0;
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            const float d = hypotf(ax + dx * k - ex, az + dz * k - ez);
            if (d < bd) { bd = d; bs = s; bk = k; }
        }
        if (bs < 0) continue;
        const int at_start = bs == 0 && bk < 0.05f, at_end = bs == o->n - 2 && bk > 0.95f;
        cr[nc] = q; cs[nc] = bs; ck[nc] = bk; cg[nc] = at_start ? 1 : at_end ? -1 : frand() < 0.5f ? 1 : -1; nc++;
    }
    if (t->poursuite && !pour_patrouille(ex, ez)) { pour_lane_fin(t, ex, ez, cr, cs, ck, nc); return; }   /* poursuivant : vers sa cible */
    int ri = 0, rj = 0;
    const int rue = rue_entree(t, ex, ez, &ri, &rj);
    if (!nc && !rue) { t->sgn = -t->sgn; t->k = 0; return; }   /* impasse : demi-tour */
    const int c = (int)(frand() * (nc + rue)) % (nc + rue);
    if (c == nc) {   /* dans la rue */
        float ax, az;
        route_pt(r, t->sgn > 0 ? r->n - 2 : 1, &ax, &az);
        liaison_rue(t, ri, rj, ex - ax, ez - az);
        return;
    }
    t->road = cr[c]; t->seg = cs[c]; t->sgn = cg[c];
    t->k = t->sgn > 0 ? ck[c] : 1 - ck[c];
}

/* ---------- Rues : voie de droite, entrées et sorties (01/11/2026, circulation unique) ---------- */
#define RUE_CAP 4           /* cases de rue comptées de chaque côté (coupe en travers) */
#define RUE_LARGE 5         /* plus large en travers : carrefour ou place */
#define RUE_DROITE 0.3f     /* carrefour, place : à droite du milieu de la case (comme avant) */
#define ENTREE_R 3.5f       /* cases : rue cherchée devant le bout d'un axe */
#define SORTIE_R 3.5f       /* cases : bout d'axe cherché autour d'une case de rue (comme ENTREE_R : l'entrée a son retour) */
#define RUELLE 0.15f        /* poids d'une ruelle d'une case (impasse le plus souvent) prise depuis une rue */
static int rue_case(int i, int j) {
    if (i < 0 || j < 0 || i >= E.w.n || j >= E.w.n) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    return c && (c->flags[cell_local(i, j)] & F_STREET);
}
static int rue_suite(int i, int j, int di, int dj) {   /* cases de rue de suite après (i, j) dans le sens (di, dj) */
    int n = 0;
    while (n < RUE_CAP && rue_case(i + di * (n + 1), j + dj * (n + 1))) n++;
    return n;
}
/* Axe de la rue en (i, j) pour un véhicule qui va dans le sens (hx, hz) : la plus longue des suites de cases de rue en x et
   en z à travers la case (égalité : l'axe dominant du cap), dans le sens du cap (ux, uz) ; R cases de rue à sa droite
   ((uz, -ux)), L à sa gauche ; rend la largeur en travers (plus de RUE_LARGE : carrefour ou place) */
static int rue_axe(int i, int j, float hx, float hz, int *ux, int *uz, int *R, int *L) {
    const int lx = rue_suite(i, j, 1, 0) + rue_suite(i, j, -1, 0), lz = rue_suite(i, j, 0, 1) + rue_suite(i, j, 0, -1);
    const int ax = lx > lz ? 1 : lz > lx ? 0 : fabsf(hx) >= fabsf(hz);
    *ux = ax ? (hx >= 0 ? 1 : -1) : 0; *uz = ax ? 0 : (hz >= 0 ? 1 : -1);
    *R = rue_suite(i, j, *uz, -*ux); *L = rue_suite(i, j, -*uz, *ux);
    return *R + *L + 1;
}
/* Point de la voie de droite dans la case de rue (i, j), cap (hx, hz) : au quart de la largeur de la chaussée depuis son
   milieu (le même pour toutes les cases d'une coupe : changer de file de cases ne se voit pas) ; carrefour ou place :
   RUE_DROITE à droite du milieu de la case ; *etroit : rue trop étroite pour se croiser (voie à moins de CROISE_DEMI) */
static void voie_rue(int i, int j, float hx, float hz, float *x, float *z, uint8_t *etroit) {
    int ux, uz, R, L;
    const int W = rue_axe(i, j, hx, hz, &ux, &uz, &R, &L);
    const float off = W <= RUE_LARGE ? (R - L) * 0.5f + W * 0.25f : RUE_DROITE;
    *x = i + 0.5f + uz * off; *z = j + 0.5f - ux * off;
    if (etroit) *etroit = W <= RUE_LARGE && W * 0.25f < CROISE_DEMI;
}
/* Cap pour le point de voie de la case suivante : le pas (sx, sz) et la marche (hx, hz) ensemble (un pas de côté garde le
   sens de la rue) ; demi-tour : le pas seul */
static void cap_pas(float hx, float hz, float sx, float sz, float *cx, float *cz) {
    const float hl = hypotf(hx, hz), sl = hypotf(sx, sz);
    if (sl < 1e-4f) { *cx = hx; *cz = hz; return; }
    sx /= sl; sz /= sl;
    if (hl < 1e-4f || (hx * sx + hz * sz) / hl < -0.5f) { *cx = sx; *cz = sz; return; }
    *cx = hx / hl + sx; *cz = hz / hl + sz;
}
/* Marche du segment de rue en cours (sinon le pas de case) */
static void rue_marche(const traffic_t *t, float *hx, float *hz) {
    *hx = t->ex - t->sx; *hz = t->ez - t->sz;
    if (*hx * *hx + *hz * *hz < 1e-4f) { *hx = (float)(t->ni - t->ci); *hz = (float)(t->nj - t->cj); }
}
/* Pas de rue vers la case voisine (ni, nj) depuis la fin du segment en cours */
static void rue_pas(traffic_t *t, int ni, int nj) {
    float hx, hz, cx, cz;
    rue_marche(t, &hx, &hz);
    cap_pas(hx, hz, (float)(ni - t->ni), (float)(nj - t->nj), &cx, &cz);
    t->sx = t->ex; t->sz = t->ez; t->ci = t->ni; t->cj = t->nj; t->ni = ni; t->nj = nj;
    voie_rue(ni, nj, cx, cz, &t->ex, &t->ez, &t->etroit);
}
/* Demi-tour dans la rue, depuis (x, z) : vers la case d'où il vient, sur l'autre voie */
static void rue_demi(traffic_t *t, float x, float z) {
    const int ci = t->ci, cj = t->cj;
    float hx = (float)(ci - t->ni), hz = (float)(cj - t->nj);
    if (hx == 0 && hz == 0) { hx = -sinf(t->dir); hz = -cosf(t->dir); }
    t->ci = t->ni; t->cj = t->nj; t->ni = ci; t->nj = cj; t->sx = x; t->sz = z; t->k = 0;
    voie_rue(ci, cj, hx, hz, &t->ex, &t->ez, &t->etroit);
}
/* Liaison vers la rue (ri, rj), entrée dans le sens (hx, hz) : en ligne droite jusqu'à son point de voie */
static void liaison_rue(traffic_t *t, int ri, int rj, float hx, float hz) {
    t->mode = POUR_RALLIE; t->wl = 1; t->wi = (int16_t)ri; t->wj = (int16_t)rj; t->stuck = 0;
    voie_rue(ri, rj, hx, hz, &t->wx, &t->wz, NULL);
}
/* Rue devant le bout (ex, ez) de l'axe que t parcourt : la case de rue la plus proche du point une case plus loin dans la
   marche, à moins de ENTREE_R cases du bout et pas derrière lui ; 0 : aucune */
static int rue_entree(const traffic_t *t, float ex, float ez, int *ri, int *rj) {
    const route_t *r = &E.w.routes[t->road];
    float ax, az;
    route_pt(r, t->sgn > 0 ? r->n - 2 : 1, &ax, &az);
    float hx = ex - ax, hz = ez - az;
    const float hl = hypotf(hx, hz);
    if (hl < 1e-4f) return 0;
    hx /= hl; hz /= hl;
    const int m = (int)ceilf(ENTREE_R), i0 = (int)floorf(ex), j0 = (int)floorf(ez);
    float bd = 1e9f;
    int ok = 0;
    for (int dj = -m; dj <= m; dj++) for (int di = -m; di <= m; di++) {
        const float cx = i0 + di + 0.5f - ex, cz = j0 + dj + 0.5f - ez;
        if (cx * cx + cz * cz > ENTREE_R * ENTREE_R || cx * hx + cz * hz < -0.3f || !is_road(i0 + di, j0 + dj)) continue;
        const float d = hypotf(cx - hx, cz - hz);
        if (d < bd) { bd = d; *ri = i0 + di; *rj = j0 + dj; ok = 1; }
    }
    return ok;
}
/* Pas suivant dans les rues depuis la case (i, j), où le véhicule arrive au point (x0, z0) en allant dans le sens
   (hx, hz). Cap : l'axe de la rue dans le sens de sa marche (carrefour ou place : sa marche). Cases voisines : tout droit
   de préférence (6), en biais (1,5), à angle droit seulement à un carrefour (0,6 ; ailleurs ce n'est que la file de cases
   d'à côté), ou si rien d'autre ; jamais en arrière. Bouts d'axes à moins de SORTIE_R cases qui continuent sa marche
   (sortie d'une ville ou d'un village : mêmes poids, puis la liaison vers l'axe). Impasse : demi-tour. */
static void rue_suivant(traffic_t *t, int i, int j, float x0, float z0, float hx, float hz) {
    const world_t *w = &E.w;
    float hl = hypotf(hx, hz);
    if (hl < 1e-4f) { hx = sinf(t->dir); hz = cosf(t->dir); hl = 1; }
    hx /= hl; hz /= hl;
    int ux, uz, R, L;
    const int W0 = rue_axe(i, j, hx, hz, &ux, &uz, &R, &L), carrefour = W0 > RUE_LARGE;
    const float cx = carrefour ? hx : (float)ux, cz = carrefour ? hz : (float)uz;
    static const float W3[3] = { 0.6f, 1.5f, 6 };   /* en table : le ternaire faisait planter GCC Xtensa (postreload) */
    int odi[12], odj[12], oq[12], n = 0;
    float wt[12], sum = 0;
    for (int passe = 0; passe < 2 && !n; passe++)   /* 2e passe : à angle droit, faute de mieux */
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
            if ((!di && !dj) || !is_road(i + di, j + dj)) continue;
            const float c = (di * cx + dj * cz) / hypotf((float)di, (float)dj);   /* cosinus avec le cap */
            if (c <= -0.2f || (c <= 0.3f && !carrefour && !passe)) continue;
            /* ruelle d'une case de large (souvent une impasse) depuis une rue plus large : RUELLE du poids */
            int vx, vz, vr, vl;
            const float ru = W0 > 1 && rue_axe(i + di, j + dj, cx + di, cz + dj, &vx, &vz, &vr, &vl) == 1 ? RUELLE : 1;
            odi[n] = di; odj[n] = dj; oq[n] = -1; wt[n] = W3[(c > 0.3f) + (c > 0.9f)] * ru; sum += wt[n]; n++;
        }
    for (int q = 0; q < w->nroutes && n < 11; q++) {   /* sorties : bouts d'axes qui continuent sa marche */
        const route_t *r = &w->routes[q];
        if (r->n < 2) continue;
        for (int e = 0; e < 2 && n < 12; e++) {
            float ex, ez, bx, bz;
            route_pt(r, e ? r->n - 1 : 0, &ex, &ez); route_pt(r, e ? r->n - 2 : 1, &bx, &bz);
            const float dx = ex - (i + 0.5f), dz = ez - (j + 0.5f), vl = hypotf(bx - ex, bz - ez);
            if (dx * dx + dz * dz > SORTIE_R * SORTIE_R || dx * cx + dz * cz < -0.5f || vl < 1e-4f) continue;   /* loin, ou derrière */
            const float c = ((bx - ex) * cx + (bz - ez) * cz) / vl;
            if (c <= 0.3f) continue;
            odi[n] = 0; odj[n] = 0; oq[n] = q * 2 + e; wt[n] = W3[1 + (c > 0.9f)]; sum += wt[n]; n++;
        }
    }
    t->x = x0; t->z = z0;
    if (!n) {   /* impasse : demi-tour, vers la case de rue la plus en arrière */
        int bi = 0, bj = 0;
        float bc = 2;
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
            if ((!di && !dj) || !is_road(i + di, j + dj)) continue;
            const float c = (di * hx + dj * hz) / hypotf((float)di, (float)dj);
            if (c < bc) { bc = c; bi = di; bj = dj; }
        }
        if (bc > 1.5f) { t->on = 0; return; }   /* case de rue isolée */
        t->ci = i; t->cj = j; t->ni = i + bi; t->nj = j + bj; t->sx = x0; t->sz = z0;
        voie_rue(t->ni, t->nj, (float)bi, (float)bj, &t->ex, &t->ez, &t->etroit);
        return;
    }
    float rr = frand() * sum;
    int o = 0;
    for (; o < n - 1; o++) { rr -= wt[o]; if (rr <= 0) break; }
    t->ci = i; t->cj = j;
    if (oq[o] >= 0) {   /* sortie : liaison vers le bout de l'axe, sur sa voie de droite */
        const int q = oq[o] >> 1, e = oq[o] & 1, seg = e ? w->routes[q].n - 2 : 0, g = e ? -1 : 1;
        t->ni = i; t->nj = j; t->sx = t->ex = x0; t->sz = t->ez = z0; t->k = 0; t->stuck = 0;
        t->mode = POUR_RALLIE; t->wl = 0; t->wq = (int16_t)q; t->ws = (int16_t)seg; t->wg = (int8_t)g; t->wk = e ? 1.0f : 0.0f;
        lane_point(q, seg, g, 0, 0, &t->wx, &t->wz, NULL);
        return;
    }
    float qx, qz;
    cap_pas(hx, hz, (float)odi[o], (float)odj[o], &qx, &qz);
    t->ni = i + odi[o]; t->nj = j + odj[o]; t->sx = x0; t->sz = z0;
    voie_rue(t->ni, t->nj, qx, qz, &t->ex, &t->ez, &t->etroit);
}
/* Bout du segment de rue : le pas suivant (poursuivant : vers sa cible, pour_case) */
static void traffic_next(traffic_t *t) {
    if (t->poursuite) { pour_case(t); return; }
    float hx, hz;
    rue_marche(t, &hx, &hz);
    rue_suivant(t, t->ni, t->nj, t->ex, t->ez, hx, hz);
}
/* Fin de la liaison d'un véhicule du trafic : dans la rue (le pas suivant depuis sa case) ou sur l'axe */
static void traf_entrer(traffic_t *t) {
    t->mode = POUR_RESEAU; t->stuck = 0; t->k = 0;
    if (t->wl) { t->lane = 0; t->ex = t->x; t->ez = t->z; rue_suivant(t, t->wi, t->wj, t->x, t->z, sinf(t->dir), cosf(t->dir)); }
    else { t->lane = 1; t->road = t->wq; t->seg = t->ws; t->sgn = t->wg; t->k = t->wg > 0 ? t->wk : 1 - t->wk; }
}

/* Quelqu'un à moins de 1,6 case devant le véhicule (dans le sens de la marche, à moins de 0,7 de côté) */
static int ahead(const traffic_t *t, float hx, float hz, float x, float z) {
    const float dx = x - t->x, dz = z - t->z, f = dx * hx + dz * hz;
    return f > 0 && f < 1.6f && fabsf(dx * hz - dz * hx) < 0.7f;
}
/* Véhicule en (x, z), cap dir, qui gêne t (cap hx, hz) : devant, dans la même voie (ahead) et dans le même sens (caps à
   moins de 70°) ; un véhicule qui vient en face, dans l'autre voie, ne gêne jamais. Sera au volant (sera) : aussi en face
   dans la même voie (moins de FACE_VOIE de côté), pour ne pas lui passer dessus */
#define MEME_SENS 0.34f
#define FACE_VOIE 0.35f
static int gene_veh(const traffic_t *t, float hx, float hz, float x, float z, float dir, int sera) {
    if (!ahead(t, hx, hz, x, z)) return 0;
    if (sinf(dir) * hx + cosf(dir) * hz > MEME_SENS) return 1;
    return sera && fabsf((x - t->x) * hz - (z - t->z) * hx) < FACE_VOIE;
}
/* Véhicule en (x, z), cap dir, qui vient en face de t : devant à moins de SERRE_DIST cases, à moins de 2 de côté */
static int en_face(const traffic_t *t, float hx, float hz, float x, float z, float dir) {
    const float dx = x - t->x, dz = z - t->z, f = dx * hx + dz * hz;
    return f > 0 && f < SERRE_DIST && fabsf(dx * hz - dz * hx) < 2 && sinf(dir) * hx + cosf(dir) * hz < -0.5f;
}
/* Vitesse maxi (cases par seconde) et accélération (cases/s²) du véhicule p : celles de l'atelier (VEHI), sinon celles
   de son type (voiture, camion, mech, bateau, vaisseau) */
void veh_params(int p, float *vmax, float *acc) {
    static const float V[6] = { 3.2f, 7.7f, 6.2f, 2.8f, 5.0f, 8.5f }, T[6] = { 1.0f, 1.5f, 2.5f, 0.6f, 3.0f, 2.0f };
    const int t = p >= 0 && p < E.w.npersos ? VEH_TYPE(E.w.persos[p].flags) : 0, ty = t <= 5 ? t : 0;
    *vmax = p >= 0 && p < MAX_PERSOS && E.w.veh_v[p] > 0 ? E.w.veh_v[p] : V[ty];
    *acc = p >= 0 && p < MAX_PERSOS && E.w.veh_a[p] > 0 ? E.w.veh_a[p] : V[ty] / T[ty];
}
static traffic_t *traffic_free(void) {
    for (int k = 0; k < TRAFFIC_MAX; k++) if (!E.traffic[k].on) return &E.traffic[k];
    return NULL;
}
static int traffic_near(float x, float z) {
    for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].on && hypotf(E.traffic[k].x - x, E.traffic[k].z - z) < 4) return 1;
    return 0;
}
/* Nouveau véhicule sur un segment d'axe pris au hasard entre 10 et 18 cases de Sera, à son allure de croisière */
static int spawn_lane(int perso) {
    const world_t *w = &E.w;
    const float d0 = 10 / w->ech, d1 = 18 / w->ech;
    int br = -1, bs = 0, seen = 0;
    for (int q = 0; q < w->nroutes; q++) {
        const route_t *r = &w->routes[q];
        for (int s = 0; s + 1 < r->n; s++) {
            float ax, az, bx, bz;
            route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz);
            const float d = hypotf((ax + bx) / 2 - E.sera.x, (az + bz) / 2 - E.sera.z);
            if (d < d0 || d > d1 || traffic_near((ax + bx) / 2, (az + bz) / 2)) continue;
            if (frand() * ++seen < 1) { br = q; bs = s; }
        }
    }
    traffic_t *t = br >= 0 ? traffic_free() : NULL;
    if (!t) return 0;
    float vmax, acc;
    veh_params(perso, &vmax, &acc);
    memset(t, 0, sizeof *t);
    t->on = 1; t->lane = 1; t->perso = perso; t->var = (uint8_t)(frand() * (E.w.persos[perso].nvar + 1)); t->road = br; t->seg = bs; t->sgn = frand() < 0.5f ? 1 : -1; t->k = frand();
    t->v = vmax * 0.45f;
    lane_place(t);
    return 1;
}
/* Segment de rue de départ de (i, j) vers (ni, nj), sur la voie de droite */
static void rue_poser(traffic_t *t, int i, int j, int ni, int nj) {
    t->ci = i; t->cj = j; t->ni = ni; t->nj = nj; t->k = 0;
    voie_rue(i, j, (float)(ni - i), (float)(nj - j), &t->sx, &t->sz, NULL);
    voie_rue(ni, nj, (float)(ni - i), (float)(nj - j), &t->ex, &t->ez, &t->etroit);
    t->x = t->sx; t->z = t->sz; t->dir = atan2f((float)(ni - i), (float)(nj - j));
}
void traffic_rue_poser(traffic_t *t, int i, int j, int ni, int nj) { t->lane = 0; t->mode = POUR_RESEAU; rue_poser(t, i, j, ni, nj); }
/* Nouveau véhicule sur une case de rue prise au hasard entre 10 et 18 cases de Sera, dans un sens de la rue (en biais au
   besoin ; une case voisine quelconque faute de mieux) */
static int spawn_cell(int perso) {
    const world_t *w = &E.w;
    for (int a = 0; a < 24; a++) {
        const float ang = frand() * 2 * PI_F, d = (10 + frand() * 8) / w->ech;
        const int i = (int)floorf(E.sera.x + cosf(ang) * d), j = (int)floorf(E.sera.z + sinf(ang) * d);
        if (!is_road(i, j) || traffic_near(i + 0.5f, j + 0.5f)) continue;
        int ux, uz, R, L;
        rue_axe(i, j, cosf(ang * 3), sinf(ang * 3), &ux, &uz, &R, &L);
        int ndi[8], ndj[8], nn = 0;
        for (int passe = 0; passe < 2 && !nn; passe++)
            for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++)
                if ((di || dj) && (passe || di * ux + dj * uz > 0) && is_road(i + di, j + dj)) { ndi[nn] = di; ndj[nn] = dj; nn++; }
        traffic_t *t = nn ? traffic_free() : NULL;
        if (!t) continue;
        const int o = (int)(frand() * nn) % nn;
        memset(t, 0, sizeof *t);
        t->on = 1; t->perso = perso; t->var = (uint8_t)(frand() * (E.w.persos[perso].nvar + 1));
        rue_poser(t, i, j, i + ndi[o], j + ndj[o]);
        return 1;
    }
    return 0;
}
/* Trafic logique (comme TRAF_POIDS de l'émulateur) : fréquence de chaque véhicule selon son service (veh_service) ; les
   urgences (secours, pompiers, une voiture de police sur trois) roulent à 75 % de leur vitesse maxi au lieu de 45 % */
static const float TRAF_POIDS[7] = { 1, 0.25f, 0.12f, 0.06f, 0.15f, 0.3f, 0.35f };
/* Dans les rues : à TRAF_LENT de l'allure des routes (plus de quota des rues, TRAF_RUE, depuis la circulation unique) */
#define TRAF_LENT 0.6f
static int traf_tirage(const int *kinds, int nk) {
    float tot = 0;
    for (int i = 0; i < nk; i++) tot += TRAF_POIDS[E.w.veh_service[kinds[i]]];
    float r = frand() * tot;
    for (int i = 0; i < nk; i++) { r -= TRAF_POIDS[E.w.veh_service[kinds[i]]]; if (r < 0) return kinds[i]; }
    return kinds[nk - 1];
}

/* ---------- Poursuivants de la police (police.c) : voitures de police qui suivent le réseau vers leur cible ----------
   Cible : Sera vue à l'instant, sinon son dernier point connu (police_cible). Sur un axe (ROUT) : demi-tour si la cible
   est derrière (un par POUR_DEMI s au plus) ; au bout de l'axe, parmi les axes qui passent à moins de 1,2 case (dans les
   deux sens), le demi-tour et une rue à moins de 2 cases (entrée d'une ville), celui dont le point POUR_VISEE cases plus
   loin est le plus près de la cible. Dans les rues : la case voisine la plus proche de la cible (revenir en arrière coûte
   POUR_DEMI_PEN cases), ou le bout d'un axe de route à moins de 2,5 cases (sortie d'une ville). Elles ne s'arrêtent pour
   personne. À moins de POUR_DIRECT cases d'une cible que rien ne cache : tout droit (POUR_DROIT), arrêt à POUR_ARRET de
   Sera ; Sera à pied, la voiture se gare (POUR_GARE) et ses agents descendent (police_descendre) ; bloquée, ou au dernier
   point connu sans personne, elle rallie le réseau le plus proche (POUR_RALLIE). Pendant une arrestation, elles
   s'arrêtent ; semée, une voiture redevient du trafic (gyrophares allumés) dès qu'elle est sur le réseau. Comme pour* de
   l'émulateur. */
#define POUR_ALLURE 0.8f      /* part de la vitesse maxi (0,85 avant le 01/11/2026 : on ne les distançait guère) */
#define POUR_DIRECT 8.0f      /* cases */
#define POUR_ARRET 1.6f
#define POUR_DESCEND 4.0f     /* cases : Sera à pied, la voiture arrêtée à moins de tant d'elle se gare */
#define POUR_PIED 7.0f        /* cases : Sera à pied et suivie, une voiture sur le réseau à moins de tant d'elle se gare */
#define POUR_DEMI 4.0f        /* s */
#define POUR_DEMI_PEN 2.0f    /* cases */
#define POUR_VISEE 3.0f       /* cases */
#define POUR_LOIN 30.0f       /* cases : plus loin de Sera, elle abandonne (retirée) */
#define POUR_RALLIER 4.0f     /* cases : réseau cherché autour d'elle */
/* Point à L de l'abscisse k du segment s de l'axe r (0 : son point s, 1 : le suivant), dans le sens g (+1 : vers les
   points suivants), arrêté au bout de l'axe */
static void route_avance(const route_t *r, int s, float k, int g, float L, float *x, float *z) {
    float ax, az, bx, bz;
    route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz);
    float px = ax + (bx - ax) * k, pz = az + (bz - az) * k;
    for (int i = g > 0 ? s + 1 : s; L > 0 && i >= 0 && i < r->n; i += g) {
        float qx, qz;
        route_pt(r, i, &qx, &qz);
        const float d = hypotf(qx - px, qz - pz);
        if (d >= L) { px += (qx - px) * L / d; pz += (qz - pz) * L / d; break; }
        L -= d; px = qx; pz = qz;
    }
    *x = px; *z = pz;
}
/* Une voiture poursuivante peut être en (x, z) : dans la carte, chunk chargé, ni eau (hors pont) ni rectangle de collision
   (marge de 0,45 m, comme ride_ok ; décors cassés exceptés) */
static int voiture_ok(float x, float z) {
    if (x < 2 || z < 2 || x > E.w.n - 2 || z > E.w.n - 2) return 0;
    const int i = (int)floorf(x), j = (int)floorf(z);
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c) return 0;
    const uint8_t f = c->flags[cell_local(i, j)];
    if (!(f & F_ROAD) && (f & (F_WATER | F_RIVER))) return 0;
    const float m = 0.45f / E.w.ech;
    for (uint32_t s = 0; s < c->nsolid; s++) {
        const float *r = &c->solids[s * 4];
        if (x > r[0] - m && x < r[2] + m && z > r[1] - m && z < r[3] + m && !rect_casse(r)) return 0;
    }
    return !(E.w.ncoul && coulee_dans(x, z, m));
}
/* Rien n'arrête une voiture de (x0, z0) jusqu'à une case de (x1, z1) (tous les demi-cases) */
static int voie_libre(float x0, float z0, float x1, float z1) {
    const float dx = x1 - x0, dz = z1 - z0, d = hypotf(dx, dz), fin = d - 1.0f / E.w.ech;
    for (float s = 0.5f; s < fin; s += 0.5f) if (!voiture_ok(x0 + dx / d * s, z0 + dz / d * s)) return 0;
    return 1;
}
/* Champ des poursuivants : distance (en pas de case, 8 voisines) de chaque case de route ou de rue (F_ROAD, F_STREET, pas
   sous un bâtiment) d'une fenêtre de CHAMP_N × CHAMP_N cases centrée sur la cible, jusqu'aux cases de route à moins de 2
   cases d'elle (6 s'il n'y en a pas) ; refait quand la cible a bougé d'une case et demie, ou toutes les 2 s (chunks
   arrivés), au plus une fois par CHAMP_S s. Les choix des poursuivants le descendent : à vol d'oiseau, ils tournaient
   autour d'un pâté de maisons (champ* de l'émulateur). 12 Ko en PSRAM. */
#define CHAMP_N 64
#define CHAMP_S 0.5f
static uint8_t champ_d[CHAMP_N * CHAMP_N] ENG_FROID;   /* 255 : pas vue, 254 : pas une route */
static uint16_t champ_q[CHAMP_N * CHAMP_N] ENG_FROID;
static int champ_i0, champ_j0, champ_ok;
static float champ_t, champ_age, champ_cx, champ_cz;
static int champ_route(int i, int j) {
    if (i < 2 || j < 2 || i >= E.w.n - 2 || j >= E.w.n - 2) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c) return 0;
    const int l = cell_local(i, j);
    return (c->flags[l] & (F_ROAD | F_STREET)) && !c->hidden[l];
}
static void champ_faire(float cx, float cz) {
    const int ci = (int)floorf(cx), cj = (int)floorf(cz);
    champ_i0 = ci - CHAMP_N / 2; champ_j0 = cj - CHAMP_N / 2;
    memset(champ_d, 255, sizeof champ_d);
    int qh = 0, qt = 0;
    for (int r = 2; r <= 6 && !qt; r += 4)
        for (int dj = -r; dj <= r; dj++) for (int di = -r; di <= r; di++) {
            const int q = (CHAMP_N / 2 + dj) * CHAMP_N + CHAMP_N / 2 + di;
            if (di * di + dj * dj > r * r || champ_d[q] == 0 || !champ_route(ci + di, cj + dj)) continue;
            champ_d[q] = 0; champ_q[qt++] = (uint16_t)q;
        }
    while (qh < qt) {
        const int q = champ_q[qh++], li = q % CHAMP_N, lj = q / CHAMP_N, d = champ_d[q];
        if (d >= 253) continue;
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
            const int ni = li + di, nj = lj + dj, nq = nj * CHAMP_N + ni;
            if ((!di && !dj) || ni < 0 || nj < 0 || ni >= CHAMP_N || nj >= CHAMP_N || champ_d[nq] != 255) continue;
            if (!champ_route(champ_i0 + ni, champ_j0 + nj)) { champ_d[nq] = 254; continue; }
            champ_d[nq] = (uint8_t)(d + 1); champ_q[qt++] = (uint16_t)nq;
        }
    }
    champ_ok = 1; champ_cx = cx; champ_cz = cz; champ_t = CHAMP_S; champ_age = 0;
}
static void champ_maj(float dt) {
    float cx, cz;
    police_cible(&cx, &cz);
    champ_t -= dt; champ_age += dt;
    if (!champ_ok || (champ_t <= 0 && (hypotf(cx - champ_cx, cz - champ_cz) >= 1.5f || champ_age >= 2))) champ_faire(cx, cz);
}
/* Distance de (x, z) à la cible par le réseau (cases) ; hors de la fenêtre, ou sans chemin : à vol d'oiseau + CHAMP_N */
static float champ_dist(float x, float z, float cx, float cz) {
    const int li = (int)floorf(x) - champ_i0, lj = (int)floorf(z) - champ_j0;
    if (champ_ok && li >= 0 && lj >= 0 && li < CHAMP_N && lj < CHAMP_N && champ_d[lj * CHAMP_N + li] < 254) return champ_d[lj * CHAMP_N + li];
    return hypotf(x - cx, z - cz) + CHAMP_N;
}
/* Case de rue voisine de (i, j) la plus près de la cible (cx, cz) par le réseau (champ), en venant de (pi, pj) : revenir en
   arrière coûte POUR_DEMI_PEN cases ; 0 : aucune */
static int pour_voisin(int i, int j, int pi, int pj, float cx, float cz, int *ni, int *nj, float *score) {
    const int hx = i - pi, hz = j - pj;
    const float hl = hypotf((float)hx, (float)hz);
    float best = 1e9f;
    int ok = 0;
    for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        if ((!di && !dj) || !is_road(i + di, j + dj)) continue;
        const float c = hl > 0 ? (di * hx + dj * hz) / (hypotf((float)di, (float)dj) * hl) : 1;   /* cosinus avec le cap */
        const float s = champ_dist(i + di + 0.5f, j + dj + 0.5f, cx, cz) + (c <= -0.2f ? POUR_DEMI_PEN / E.w.ech : 0) + frand() * 0.15f;
        if (s < best) { best = s; *ni = i + di; *nj = j + dj; ok = 1; }
    }
    if (score) *score = best;
    return ok;
}
/* Au dernier point connu, sans la voir : une patrouille, au hasard comme le trafic (descendre le champ la ferait tourner sur
   place) */
static int pour_patrouille(float x, float z) { float cx, cz; return !police_cible(&cx, &cz) && champ_dist(x, z, cx, cz) <= 2; }
static void pour_case(traffic_t *t) {
    float cx, cz;
    police_cible(&cx, &cz);
    if (pour_patrouille(t->ni + 0.5f, t->nj + 0.5f)) { t->poursuite = 0; traffic_next(t); t->poursuite = 1; return; }
    int ni = 0, nj = 0;
    float s = 1e9f;
    const int ok = pour_voisin(t->ni, t->nj, t->ci, t->cj, cx, cz, &ni, &nj, &s);
    /* Sortie d'une ville : le bout d'un axe de route à moins de 2,5 cases, s'il rapproche plus de la cible */
    const float px = t->ni + 0.5f, pz = t->nj + 0.5f;
    int bq = -1, be = 0;
    float bd = 2.5f;
    for (int q = 0; q < E.w.nroutes; q++) {
        const route_t *r = &E.w.routes[q];
        for (int e = 0; e < 2 && r->n >= 2; e++) {
            float ex, ez;
            route_pt(r, e ? r->n - 1 : 0, &ex, &ez);
            const float d = hypotf(ex - px, ez - pz);
            if (d < bd) { bd = d; bq = q; be = e; }
        }
    }
    if (bq >= 0) {
        const route_t *r = &E.w.routes[bq];
        const int seg = be ? r->n - 2 : 0, g = be ? -1 : 1;
        float ax, az, ex, ez;
        route_avance(r, seg, be ? 1.0f : 0.0f, g, POUR_VISEE / E.w.ech, &ax, &az);
        if (!ok || champ_dist(ax, az, cx, cz) < s - 0.5f / E.w.ech) {
            route_pt(r, be ? r->n - 1 : 0, &ex, &ez);
            t->mode = POUR_RALLIE; t->wl = 0; t->wq = (int16_t)bq; t->ws = (int16_t)seg; t->wk = be ? 1.0f : 0.0f; t->wg = (int8_t)g;
            t->wx = ex; t->wz = ez; t->stuck = 0;
            return;
        }
    }
    if (!ok) { rue_demi(t, t->ex, t->ez); return; }   /* cul-de-sac */
    rue_pas(t, ni, nj);
}
static void pour_lane_fin(traffic_t *t, float ex, float ez, const int *cr, const int *cs, const float *ck, int nc) {
    float cx, cz, px, pz;
    police_cible(&cx, &cz);
    const float L = POUR_VISEE / E.w.ech;
    route_avance(&E.w.routes[t->road], t->seg, t->sgn > 0 ? 1.0f : 0.0f, -t->sgn, L, &px, &pz);   /* demi-tour */
    float best = champ_dist(px, pz, cx, cz) + POUR_DEMI_PEN / E.w.ech;
    int bc = -1, bg = 1;
    for (int c = 0; c < nc; c++)
        for (int g = -1; g <= 1; g += 2) {
            route_avance(&E.w.routes[cr[c]], cs[c], ck[c], g, L, &px, &pz);
            const float s = champ_dist(px, pz, cx, cz);
            if (s < best) { best = s; bc = c; bg = g; }
        }
    int ri = -1, rj = -1;   /* rue à moins de 2 cases du bout (entrée d'une ville) */
    float rd = 2.0f;
    for (int dj = -2; dj <= 2; dj++) for (int di = -2; di <= 2; di++) {
        const int i = (int)floorf(ex) + di, j = (int)floorf(ez) + dj;
        const float d = hypotf(i + 0.5f - ex, j + 0.5f - ez);
        if (d < rd && is_road(i, j)) { rd = d; ri = i; rj = j; }
    }
    if (ri >= 0 && champ_dist(ri + 0.5f, rj + 0.5f, cx, cz) < best) {
        t->mode = POUR_RALLIE; t->wl = 1; t->wi = (int16_t)ri; t->wj = (int16_t)rj; t->wx = ri + 0.5f; t->wz = rj + 0.5f; t->stuck = 0;
        return;
    }
    if (bc < 0) { t->sgn = -t->sgn; t->k = 0; t->demi = POUR_DEMI; return; }
    t->road = cr[bc]; t->seg = cs[bc]; t->sgn = bg; t->k = bg > 0 ? ck[bc] : 1 - ck[bc];
}
/* Le réseau le plus proche (POUR_RALLIER cases) : une case de rue, ou le point le plus proche d'un axe (pris dans le sens
   qui rapproche de la cible) ; 0 : aucun */
static int pour_rallier(traffic_t *t) {
    float cx, cz;
    police_cible(&cx, &cz);
    float bd = POUR_RALLIER / E.w.ech;
    int ri = -1, rj = -1, bq = -1, bs = 0;
    float bk = 0;
    const int i0 = (int)floorf(t->x), j0 = (int)floorf(t->z), m = (int)ceilf(bd);
    for (int dj = -m; dj <= m; dj++) for (int di = -m; di <= m; di++) {
        const float d = hypotf(i0 + di + 0.5f - t->x, j0 + dj + 0.5f - t->z);
        if (d < bd && is_road(i0 + di, j0 + dj)) { bd = d; ri = i0 + di; rj = j0 + dj; }
    }
    for (int q = 0; q < E.w.nroutes; q++) {
        const route_t *r = &E.w.routes[q];
        for (int s = 0; s + 1 < r->n; s++) {
            float ax, az, bx, bz;
            route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz);
            if (fminf(ax, bx) > t->x + bd || fmaxf(ax, bx) < t->x - bd || fminf(az, bz) > t->z + bd || fmaxf(az, bz) < t->z - bd) continue;
            const float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
            float k = l2 > 0 ? ((t->x - ax) * dx + (t->z - az) * dz) / l2 : 0;
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            const float d = hypotf(ax + dx * k - t->x, az + dz * k - t->z);
            if (d < bd) { bd = d; bq = q; bs = s; bk = k; }
        }
    }
    t->stuck = 0;
    if (bq >= 0) {
        const route_t *r = &E.w.routes[bq];
        float px, pz, qx, qz, ax, az, bx, bz;
        route_avance(r, bs, bk, 1, POUR_VISEE / E.w.ech, &px, &pz); route_avance(r, bs, bk, -1, POUR_VISEE / E.w.ech, &qx, &qz);
        route_pt(r, bs, &ax, &az); route_pt(r, bs + 1, &bx, &bz);
        t->wl = 0; t->wq = (int16_t)bq; t->ws = (int16_t)bs; t->wk = bk; t->wg = champ_dist(px, pz, cx, cz) <= champ_dist(qx, qz, cx, cz) ? 1 : -1;
        t->wx = ax + (bx - ax) * bk; t->wz = az + (bz - az) * bk;
    } else if (ri >= 0) { t->wl = 1; t->wi = (int16_t)ri; t->wj = (int16_t)rj; t->wx = ri + 0.5f; t->wz = rj + 0.5f; }
    else return 0;
    t->mode = POUR_RALLIE;
    return 1;
}
/* Arrivée au point de ralliement : sur la rue (la case voisine vers la cible) ou sur l'axe */
static void pour_entrer(traffic_t *t) {
    float cx, cz;
    police_cible(&cx, &cz);
    t->stuck = 0; t->demi = 1;
    if (t->wl) {
        int ni, nj;
        if (!pour_voisin(t->wi, t->wj, t->wi, t->wj, cx, cz, &ni, &nj, NULL)) { t->mode = POUR_DROIT; return; }
        t->lane = 0; t->ci = t->wi; t->cj = t->wj; t->ni = ni; t->nj = nj; t->k = 0; t->sx = t->x; t->sz = t->z;
        voie_rue(ni, nj, (float)(ni - t->wi), (float)(nj - t->wj), &t->ex, &t->ez, &t->etroit);
    } else {
        t->lane = 1; t->road = t->wq; t->seg = t->ws; t->sgn = t->wg; t->k = t->wg > 0 ? t->wk : 1 - t->wk;
        lane_place(t);
    }
    t->mode = POUR_RESEAU;
}
/* Tout droit vers (tx, tz) à la vitesse vc (virage de 3 rad/s au plus) ; contre un obstacle, il glisse le long, ou
   s'arrête (stuck : temps bloqué, la moitié en glissant) */
static void voiture_vers(traffic_t *t, float tx, float tz, float vc, float acc, float vmax, float dt) {
    const float dx = tx - t->x, dz = tz - t->z;
    if (dx * dx + dz * dz > 1e-6f) {
        float da = atan2f(dx, dz) - t->dir;
        da = atan2f(sinf(da), cosf(da));
        const float m = 3.0f * dt;
        t->dir += da > m ? m : da < -m ? -m : da;
    }
    t->v = t->v < vc ? fminf(vc, t->v + acc * dt) : fmaxf(vc, t->v - 2 * acc * dt);
    t->walking = t->v > 0.05f;
    if (!t->walking) { t->stuck = 0; return; }
    const float nx = t->x + sinf(t->dir) * t->v * dt, nz = t->z + cosf(t->dir) * t->v * dt;
    const int mx = voiture_ok(nx, t->z), mz = voiture_ok(t->x, nz);
    if (mx) t->x = nx;
    if (mz) t->z = nz;
    if (!mx && !mz) { t->v *= 0.5f; t->stuck += dt; }
    else t->stuck = mx && mz ? 0 : t->stuck + dt * 0.5f;
    t->anim += dt * fminf(1, t->v / (vmax * 0.3f));
    if (t->anim > 0.12f) { t->anim = 0; t->frame++; }
}
/* Sur le réseau, après sa place (traffic_update) : semée, elle redevient du trafic ; tout droit si sa cible est proche et
   dégagée ; sur un axe, demi-tour si le réseau rapproche plus de la cible derrière elle que devant (de 2 cases) */
static void pour_reseau(traffic_t *t, int k, float dt) {
    if (!POL.niveau && !POL.arr) { t->poursuite = 0; return; }
    if (t->demi > 0) t->demi -= dt;
    if (POL.arr) return;
    float cx, cz;
    const int suivi = police_cible(&cx, &cz);
    /* Sera à pied et suivie, près d'elle (sur une place, un trottoir) : la voiture se gare au bord, ses agents descendent */
    if (suivi && E.ride < 0 && hypotf(cx - t->x, cz - t->z) < POUR_PIED / E.w.ech) { t->mode = POUR_GARE; t->stuck = 0; police_descendre(k); return; }
    if (hypotf(cx - t->x, cz - t->z) < POUR_DIRECT / E.w.ech && voie_libre(t->x, t->z, cx, cz)) { t->mode = POUR_DROIT; t->stuck = 0; return; }
    if (t->lane && t->demi <= 0) {
        const route_t *r = &E.w.routes[t->road];
        const float kk = t->sgn > 0 ? t->k : 1 - t->k, L = POUR_VISEE / E.w.ech;   /* abscisse sur le segment, dans le sens des points */
        float ax, az, bx, bz;
        route_avance(r, t->seg, kk, t->sgn, L, &ax, &az); route_avance(r, t->seg, kk, -t->sgn, L, &bx, &bz);
        if (champ_dist(bx, bz, cx, cz) < champ_dist(ax, az, cx, cz) - 2) { t->sgn = -t->sgn; t->k = 1 - t->k; t->demi = POUR_DEMI; lane_place(t); }
    }
}
/* Hors du réseau : garée (agents dehors), tout droit vers la cible, ou vers le point de ralliement */
static void pour_hors_reseau(traffic_t *t, int k, float dt) {
    float vmax, acc, cx, cz;
    veh_params(t->perso, &vmax, &acc);
    const int suivi = police_cible(&cx, &cz);
    const float d = hypotf(cx - t->x, cz - t->z), fx = t->x + sinf(t->dir), fz = t->z + cosf(t->dir);
    if (t->demi > 0) t->demi -= dt;
    if (POL.arr) { voiture_vers(t, fx, fz, 0, acc, vmax, dt); return; }   /* arrestation : elle s'arrête */
    if (!POL.niveau && t->mode != POUR_RALLIE) { if (!pour_rallier(t)) t->on = 0; return; }   /* semée : elle rejoint le réseau */
    switch (t->mode) {
    case POUR_GARE:   /* agents dehors ; Sera repart en véhicule, ou loin : la poursuite reprend */
        voiture_vers(t, fx, fz, 0, acc, vmax, dt);
        if ((E.ride >= 0 && d > POUR_DESCEND / E.w.ech) || d > 1.5f * POUR_PIED / E.w.ech) t->mode = POUR_DROIT;
        break;
    case POUR_DROIT:
        voiture_vers(t, cx, cz, fminf(vmax * POUR_ALLURE, fmaxf(0, (d - (suivi ? POUR_ARRET : 0.4f) / E.w.ech) * 2.5f)), acc, vmax, dt);
        if (suivi && E.ride < 0 && d < POUR_DESCEND / E.w.ech && t->v < 0.3f) { t->mode = POUR_GARE; police_descendre(k); }
        else if (t->demi <= 0 && ((!suivi && d < 0.8f / E.w.ech) || d > POUR_DIRECT * 1.6f / E.w.ech || t->stuck > 0.8f) && !pour_rallier(t)) t->demi = 1;
        break;
    default:   /* POUR_RALLIE */
        voiture_vers(t, t->wx, t->wz, vmax * 0.5f, acc, vmax, dt);
        if (hypotf(t->wx - t->x, t->wz - t->z) < 0.35f || t->stuck > 1.5f) pour_entrer(t);
        break;
    }
}
/* Nouvelle voiture de police poursuivante (perso) sur un axe de route ou une rue, entre POL_LOIN0 et POL_LOIN1 cases de
   Sera, hors de vue (en ville, dans la rue plutôt), tournée vers sa cible ; une place libre du trafic, sinon celle du
   véhicule du trafic le plus loin ; 0 : pas de place */
int traffic_poursuivant(int perso) {
    const world_t *w = &E.w;
    if (perso < 0 || w->carte || CR.on) return 0;
    traffic_t *t = traffic_free();
    float far = -1;
    if (!t)
        for (int k = 0; k < TRAFFIC_MAX; k++) {
            const float d = hypotf(E.traffic[k].x - E.sera.x, E.traffic[k].z - E.sera.z);
            if (!E.traffic[k].poursuite && d > far) { far = d; t = &E.traffic[k]; }
        }
    if (!t) return 0;
    float cx, cz;
    police_cible(&cx, &cz);
    champ_faire(cx, cz);   /* champ à jour : seuls les points d'où le réseau mène à la cible */
    const float d0 = POL_LOIN0 / w->ech, d1 = POL_LOIN1 / w->ech;
    int br = -1, bs = 0, seen = 0, ci = -1, cj = -1, ni = 0, nj = 0;
    for (int q = 0; q < w->nroutes; q++) {
        const route_t *r = &w->routes[q];
        for (int s = 0; s + 1 < r->n; s++) {
            float ax, az, bx, bz;
            route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz);
            const float mx = (ax + bx) / 2, mz = (az + bz) / 2, d = hypotf(mx - E.sera.x, mz - E.sera.z);
            if (d < d0 || d > d1 || !police_hors_vue(mx, mz) || champ_dist(mx, mz, cx, cz) >= CHAMP_N) continue;
            if (frand() * ++seen < 1) { br = q; bs = s; }
        }
    }
    for (int a = 0; a < 24 && ci < 0; a++) {
        const float ang = frand() * 2 * PI_F, d = d0 + frand() * (d1 - d0);
        const int i = (int)floorf(E.sera.x + cosf(ang) * d), j = (int)floorf(E.sera.z + sinf(ang) * d);
        if (is_road(i, j) && police_hors_vue(i + 0.5f, j + 0.5f) && champ_dist(i + 0.5f, j + 0.5f, cx, cz) < CHAMP_N && pour_voisin(i, j, i, j, cx, cz, &ni, &nj, NULL)) { ci = i; cj = j; }
    }
    if (br < 0 && ci < 0) return 0;
    const int rue = ci >= 0 && (br < 0 || E.ville_v > 0.5f || frand() < 0.5f);
    float vmax, acc;
    veh_params(perso, &vmax, &acc);
    memset(t, 0, sizeof *t);
    t->on = 1; t->perso = perso; t->poursuite = 1; t->urgence = 1; t->v = vmax * 0.6f; t->demi = 1;   /* une seule livrée : palette 0 */
    if (rue) {
        rue_poser(t, ci, cj, ni, nj);
    } else {
        float ax, az, bx, bz;
        route_pt(&w->routes[br], bs, &ax, &az); route_pt(&w->routes[br], bs + 1, &bx, &bz);
        t->lane = 1; t->road = br; t->seg = bs; t->k = 0.5f; t->sgn = champ_dist(bx, bz, cx, cz) < champ_dist(ax, az, cx, cz) ? 1 : -1;
        lane_place(t);
    }
    return 1;
}

/* Voiture du trafic dans l'image, à SCENE_TRAF_MARGE pixels de l'interface près (son sprite déborde de son ancrage) ;
   calme de scène (traffic_update ; trafALecran de l'émulateur, en pixels de l'image 3D : la moitié) */
#define SCENE_TRAF_MARGE 40.0f
static int traf_a_lecran(const traffic_t *t) {
    float sx, sy;
    if (!project(t->x, ground_y(t->x, t->z), t->z, &sx, &sy)) return 0;
#if ENG_RECT   /* écran rectangulaire : toute l'image, de part et d'autre du cadre de l'interface */
    return sx > -ui_ox - SCENE_TRAF_MARGE && sx < ui_img_w - ui_ox + SCENE_TRAF_MARGE && sy > -ui_oy - SCENE_TRAF_MARGE && sy < ui_img_h - ui_oy + 2 * SCENE_TRAF_MARGE;
#endif
    return sx > -SCENE_TRAF_MARGE && sx < ui_w + SCENE_TRAF_MARGE && sy > -SCENE_TRAF_MARGE && sy < ui_h + 2 * SCENE_TRAF_MARGE;
}

static void traffic_update(float dt) {
    const world_t *w = &E.w;
    static int kinds[MAX_PERSOS] ENG_FROID;   /* statique, en PSRAM : la pile du jeu fait 8 Ko sur l'ESP32 */
    int nk = 0;
    for (int p = 0; p < w->npersos; p++) {
        const int f = w->persos[p].flags, ty = VEH_TYPE(f);
        if ((f & PERS_VEHICULE) && (f & PERS_TRAFIC) && ty != VEH_BATEAU && ty != VEH_VAISSEAU && w->persos[p].sheet >= 0) kinds[nk++] = p;
    }
    if (CR.on || w->carte) { for (int k = 0; k < TRAFFIC_MAX; k++) E.traffic[k].on = 0; return; }   /* trafic : carte 0, hors course */
    const int normal = w->traf_max && nk;   /* sans trafic du paquet, seules les voitures de police poursuivantes roulent */
    /* Calme de scène (11/11/2026, demande de l'utilisateur : « les voitures qui passent polluent le récit », dans les villages
       et à Lucerne, où la scène ne peut pas quitter la rue) : pendant une scène (scene_en_cours, hud_scene de hud.c), plus
       de naissance ; une voiture hors de l'écran, ou toutes pendant un fondu au noir, disparaît ; celle qu'on voit finit son
       passage et disparaît en sortant de l'image. Les poursuivants de la police ne sont pas concernés (trafCalme de
       l'émulateur, à garder identique) */
    const int calme = scene_en_cours();
    int count = 0, pours = 0;
    for (int k = 0; k < TRAFFIC_MAX; k++) {   /* poursuivants : plus loin (POUR_LOIN), et à part du nombre du trafic */
        traffic_t *t = &E.traffic[k];
        if (t->on && (hypotf(t->x - E.sera.x, t->z - E.sera.z) > (t->poursuite ? POUR_LOIN : 22.0f) / w->ech || (!normal && !t->poursuite))) t->on = 0;
        if (t->on && calme && !t->poursuite && (MISE.fondu >= 0.9f || !traf_a_lecran(t))) t->on = 0;
        count += t->on && !t->poursuite;
        pours += t->on && t->poursuite;
    }
    if (pours) champ_maj(dt); else champ_ok = 0;   /* champ des poursuivants */
    E.traf_spawn_t -= dt;
    if (normal && !calme && count < w->traf_max && E.traf_spawn_t <= 0) {
        /* Sur un axe de route ou dans une rue (une seule circulation), une fois sur deux chacun ; l'autre si le premier
           échoue */
        E.traf_spawn_t = 0.5f;
        const int perso = traf_tirage(kinds, nk), lane_first = w->nroutes && frand() < 0.5f;
        int avant[TRAFFIC_MAX];
        for (int k = 0; k < TRAFFIC_MAX; k++) avant[k] = E.traffic[k].on;
        if (!(lane_first && spawn_lane(perso)) && !spawn_cell(perso) && !lane_first) spawn_lane(perso);
        const int sv = w->veh_service[perso];
        for (int k = 0; k < TRAFFIC_MAX; k++)
            if (E.traffic[k].on && !avant[k]) E.traffic[k].urgence = sv == 2 || sv == 3 || (sv == 1 && frand() < 1.0f / 3);
    }
    /* Sera sur la chaussée (route ou rue) : seulement là, les voitures s'arrêtent pour elle (sur le trottoir, elles passent) */
    const int si = (int)floorf(E.sera.x), sj = (int)floorf(E.sera.z);
    const chunk_t *sc = E.in.on ? NULL : chunk_of_cell(si, sj);
    const int sera_route = sc && si < w->n && sj < w->n && (sc->flags[cell_local(si, sj)] & (F_ROAD | F_STREET));
    for (int k = 0; k < TRAFFIC_MAX; k++) {
        traffic_t *t = &E.traffic[k];
        if (!t->on) continue;
        if (t->poursuite && t->mode != POUR_RESEAU) { pour_hors_reseau(t, k, dt); continue; }   /* tout droit, ralliement, garée */
        const float hx = sinf(t->dir), hz = cosf(t->dir);
        /* Arrêt : quelqu'un devant (Sera aussi en véhicule), ou Sera, sur la chaussée, s'adresse à lui (pour le voler) ; les
           poursuivants ne s'arrêtent pour personne */
        int blocked = 0, face = 0;
        if (!t->poursuite) {
            /* Sera à pied devant ; au volant, devant dans le même sens, ou en face dans la même voie (gene_veh) */
            const float sd = E.ride >= 0 ? E.npcs[E.ride].dir : 0;
            blocked = sera_route && (E.talk == TALK_TRAF(k) || (E.ride < 0 ? ahead(t, hx, hz, E.sera.x, E.sera.z) : gene_veh(t, hx, hz, E.sera.x, E.sera.z, sd, 1)));
            if (sera_route && E.ride >= 0) face = en_face(t, hx, hz, E.sera.x, E.sera.z, sd);
            for (int m = 0; m < E.nnpc && !blocked; m++) {   /* piétons sur la chaussée (pas ceux du trottoir), véhicules garés */
                const npc_t *q = &E.npcs[m];
                if (m == E.ride || !npc_present(q) || fabsf(q->x - t->x) >= 2 || fabsf(q->z - t->z) >= 2) continue;
                if ((E.w.persos[q->perso].flags & PERS_VEHICULE) || chaussee_at(q->x, q->z)) blocked = ahead(t, hx, hz, q->x, q->z);
            }
            for (int m = 0; m < TRAFFIC_MAX; m++)   /* trafic : devant dans le même sens ; en face (se serrer) */
                if (m != k && E.traffic[m].on) {
                    const traffic_t *o = &E.traffic[m];
                    if (!blocked) blocked = gene_veh(t, hx, hz, o->x, o->z, o->dir, 0);
                    if (!face) face = en_face(t, hx, hz, o->x, o->z, o->dir);
                }
        }
        /* Croisement : sur une voie trop étroite pour deux, rangé sur le bas-côté tant qu'un véhicule vient en face */
        const int etroit = t->mode == POUR_RESEAU && (t->lane ? E.w.routes[t->road].hw * 0.5f < CROISE_DEMI : t->etroit);
        const float sv = etroit && face ? 1.0f : 0.0f;
        t->serre = t->serre < sv ? fminf(sv, t->serre + SERRE_VITE * dt) : fmaxf(sv, t->serre - SERRE_VITE * dt);
        /* Allure de croisière : 45 % de la vitesse maxi ; accélère au départ, freine (au double) devant un obstacle ; un
           poursuivant à POUR_ALLURE, arrêté pendant une arrestation ; rangé sur le bas-côté : SERRE_ALLURE */
        float vmax, acc;
        veh_params(t->perso, &vmax, &acc);
        const float cruise = blocked ? 0 : t->poursuite ? (POL.arr ? 0 : vmax * POUR_ALLURE)
                           : vmax * (t->urgence ? 0.75f : 0.45f) * (t->lane ? 1 : TRAF_LENT) * (1 - (1 - SERRE_ALLURE) * t->serre);
        t->v = t->v < cruise ? fminf(cruise, t->v + acc * dt) : fmaxf(cruise, t->v - 2 * acc * dt);
        t->walking = t->v > 0.05f;
        /* Bloqué plus de 2,5 s (un piéton immobile sur la chaussée) : demi-tour (sur un axe : voie d'en face) ; pendant
           une liaison, il attend */
        t->stuck = blocked && E.talk != TALK_TRAF(k) ? t->stuck + dt : 0;
        if (t->stuck > 2.5f) {
            if (t->mode != POUR_RESEAU) t->stuck = 0;
            else if (t->lane) { t->sgn = -t->sgn; t->k = 1 - t->k; }
            else rue_demi(t, t->x, t->z);
            t->stuck = 0; t->v = 0;
        }
        if (t->v > 0 && t->mode == POUR_RALLIE) {   /* liaison d'un réseau à l'autre, en ligne droite */
            const float dx = t->wx - t->x, dz = t->wz - t->z, d = hypotf(dx, dz), s = dt * t->v;
            if (d > 1e-3f) t->dir = atan2f(dx, dz);
            if (s >= d) { t->x = t->wx; t->z = t->wz; traf_entrer(t); }
            else { t->x += dx / d * s; t->z += dz / d * s; }
            t->anim += dt * fminf(1, t->v / (vmax * 0.3f));
            if (t->anim > 0.12f) { t->anim = 0; t->frame++; }
        } else if (t->v > 0) {
            if (t->lane) {
                t->k += dt * t->v / lane_len(t);
                for (int it = 0; it < 4 && t->k >= 1 && t->mode == POUR_RESEAU; it++) {   /* un poursuivant peut quitter le réseau (ralliement) */
                    t->k -= 1; t->seg += t->sgn;
                    if (t->seg < 0 || t->seg > w->routes[t->road].n - 2) { t->seg -= t->sgn; lane_next(t); }
                }
            } else {   /* rue : de point de voie en point de voie (segment d'au moins un quart de case pour la marche) */
                t->k += dt * t->v / fmaxf(0.25f, hypotf(t->ex - t->sx, t->ez - t->sz));
                for (int it = 0; it < 4 && t->k >= 1 && t->mode == POUR_RESEAU && t->on; it++) { t->k -= 1; traffic_next(t); }
            }
            t->anim += dt * fminf(1, t->v / (vmax * 0.3f));
            if (t->anim > 0.12f) { t->anim = 0; t->frame++; }
        }
        if (!t->on || t->mode != POUR_RESEAU) continue;   /* liaison, poursuivant parti vers le réseau voisin : il garde sa place */
        if (t->lane) lane_place(t);
        else {   /* sur la voie de droite de la rue ; rue étroite : rangé à droite (serre) */
            const float dx = t->ex - t->sx, dz = t->ez - t->sz, kk = fminf(t->k, 1), s = t->serre * SERRE_RUE;
            if (dx * dx + dz * dz > 0.0025f) t->dir = atan2f(dx, dz);
            t->x = t->sx + dx * kk + cosf(t->dir) * s;
            t->z = t->sz + dz * kk - sinf(t->dir) * s;
        }
        if (t->poursuite) pour_reseau(t, k, dt);   /* tout droit vers une cible proche, garée, demi-tour, ou plus de poursuite */
    }
}

/* ---------- Véhicules ---------- */
int ride_type(void) { return E.ride < 0 ? 0 : VEH_TYPE(E.w.persos[E.npcs[E.ride].perso].flags); }
static int is_vehicle(int k) { return k >= 0 && (E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE); }

/* ---------- Décors cassés ---------- */
/* Un véhicule lancé (plus de 1,2 case/s) qui heurte un grand décor solide (drapeau 4 de la plante) le casse, comme
   decorCasse de l'émulateur : son rectangle de collision ne compte plus, le véhicule perd 30 % de sa vitesse. Rocher
   (drapeau 8) : explosion, éclats et poussière ; sinon (arbre) : craquement, feuilles, il tombe dans le sens du véhicule
   (dessin des plantes). Les CASSE_N derniers sont retenus par leur position (un chunk rechargé les retrouve). */
#define CASSE_N 128
static struct { float x, z, t0, dx, dz; } casses[CASSE_N] ENG_FROID;
static int ncasses, casse_k;
static float casse_clock;
int decor_casse(float x, float z, float *t, float *dx, float *dz) {
    for (int k = 0; k < ncasses; k++)
        if (fabsf(casses[k].x - x) < 0.01f && fabsf(casses[k].z - z) < 0.01f) {
            *t = casse_clock - casses[k].t0; *dx = casses[k].dx; *dz = casses[k].dz;
            return 1;
        }
    return 0;
}
/* Rectangle de collision d'un décor cassé : un décor cassé en son centre */
static int rect_casse(const float *r) {
    for (int k = 0; k < ncasses; k++)
        if (casses[k].x > r[0] && casses[k].x < r[2] && casses[k].z > r[1] && casses[k].z < r[3]) return 1;
    return 0;
}
/* Le décor solide du rectangle r (dans le chunk c) casse ; 0 si le rectangle n'est pas celui d'un décor */
static int casser(const chunk_t *c, const float *r) {
    const pak_plant_t *p = NULL;
    for (uint32_t k = 0; k < c->nplant && !p; k++) {
        const pak_plant_t *q = &c->plants[k];
        if ((q->flags & 4) && q->x > r[0] && q->x < r[2] && q->z > r[1] && q->z < r[3]) p = q;
    }
    if (!p) return 0;
    const int k = ncasses < CASSE_N ? ncasses++ : (casse_k = (casse_k + 1) % CASSE_N);
    casses[k].x = p->x; casses[k].z = p->z; casses[k].t0 = casse_clock;
    casses[k].dx = sinf(E.sera.dir); casses[k].dz = cosf(E.sera.dir);
    E.ride_v *= 0.7f;
    const float size = p->size * E.w.sprite_scale;
    if (p->flags & 8) {
        son_jouer(SON_EXPLOSION, 0.55f, 1.1f);
        for (int n = 0; n < 14; n++) {
            const float a = frand() * 2 * PI_F, v = 1.5f + frand() * 2.5f;
            course_effet(3, p->x, p->y + frand() * size * 0.6f, p->z, cosf(a) * v, 2 + frand() * 3, sinf(a) * v, 0.9f + frand() * 0.5f, p->y);
        }
        for (int n = 0; n < 6; n++)
            course_effet(2, p->x + (frand() - 0.5f) * 0.6f, p->y + 0.2f, p->z + (frand() - 0.5f) * 0.6f, (frand() - 0.5f) * 0.8f, 0.5f, (frand() - 0.5f) * 0.8f, 1.1f, p->y);
    } else {
        son_jouer(SON_BOIS, 0.75f, 0.9f + 0.2f * frand());
        for (int n = 0; n < 12; n++)
            course_effet(4, p->x + (frand() - 0.5f) * size * 0.6f, p->y + size * (0.5f + frand() * 0.4f), p->z + (frand() - 0.5f) * size * 0.6f,
                         (frand() - 0.5f) * 1.5f, frand(), (frand() - 0.5f) * 1.5f, 1.6f + frand(), p->y);
        for (int n = 0; n < 4; n++)
            course_effet(2, p->x + (frand() - 0.5f) * 0.5f, p->y + 0.15f, p->z + (frand() - 0.5f) * 0.5f, (frand() - 0.5f) * 0.6f, 0.4f, (frand() - 0.5f) * 0.6f, 0.9f, p->y);
    }
    return 1;
}

/* Véhicules dans l'eau : voiture, camion et mech passent les rivières à gué (à moitié de leur vitesse) et entrent dans
   les lacs et la mer, ralentis à mesure que l'eau monte ; au-delà de leur hauteur de gué (VEH_GUE, en mètres), ceux qui
   roulent coulent : portés un instant, ils descendent au fond en COULE_T secondes, puis Sera en sort (à la nage, sinon
   sur la dernière rive). Ceux qui planent (option « roule » décochée) glissent sur l'eau sans couler. */
static const float VEH_GUE[] = { 0, 0.8f, 1.2f, 2.0f, 0, 0 };
#define COULE_T 2.5f
static uint8_t flags_at(float x, float z) {
    const int i = (int)floorf(x), j = (int)floorf(z);
    const chunk_t *c = chunk_of_cell(i, j);
    return c ? c->flags[cell_local(i, j)] : 0;
}
float eau_prof(float x, float z) {
    const int i = (int)floorf(x), j = (int)floorf(z);
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c || !water_kind(c->flags[cell_local(i, j)])) return 0;
    const float s = c->water_h[cell_local(i, j)] > -1e8f ? c->water_h[cell_local(i, j)] : E.w.wl, a = s - ground_y(x, z);
    return a > 0 ? a : 0;
}
/* Hauteur de gué (m) du véhicule p de type t : gué renforcé sur celui de Sera */
static float veh_gue(int p, int t) { return VEH_GUE[t] * (vk_monte(p, VK_GUE) ? VK_GUE_X : 1); }
int veh_flotte(int p) { const int t = VEH_TYPE(E.w.persos[p].flags); return t >= VEH_VOITURE && t <= VEH_MECH && veh_rolls(p) && vk_monte(p, VK_FLOTTEURS); }
/* À la surface de l'eau : ce qui plane, le véhicule à flotteurs et, sur l'île, tout bateau, « roule » coché ou non (11/11/2026, retour de
   l'utilisateur : la barque du prologue était posée au fond, ce qui ne se voyait pas tant que le lagon n'avait que quelques centimètres
   d'eau ; vehSurface de l'émulateur de l'île ; Echoes inchangé) */
int veh_surface(int p) { return !veh_rolls(p) || veh_flotte(p) || (E.w.ile[0] && VEH_TYPE(E.w.persos[p].flags) == VEH_BATEAU); }
/* Véhicule p noyé en (x, z) : il roule (voiture, camion, mech) et un lac ou la mer y dépasse sa hauteur de gué */
int veh_noye(int p, float x, float z) {
    const int t = VEH_TYPE(E.w.persos[p].flags);
    if (t < VEH_VOITURE || t > VEH_MECH || !veh_rolls(p)) return 0;
    const uint8_t f = flags_at(x, z);
    if (vk_monte(p, VK_FLOTTEURS)) return 0;   /* (flotteurs : il ne coule plus) */
    return !(f & (F_ROAD | F_RIVER)) && (f & F_WATER) && eau_prof(x, z) >= veh_gue(p, t) / E.w.ech;
}

/* ---------- Vol permanent : la carte du système solaire (CART, ASTR ; volEmbarquer de l'émulateur) ---------- */
/* Sur cette carte, Sera est toujours à bord du vaisseau de l'équipe (le premier exemplaire vaisseau de son bloc de PNJS) :
   à l'altitude de croisière de la carte, à VOL_VITESSE de sa vitesse, sans décollage ni descente ; il n'entre pas dans
   un astre ; les passages (approche d'un monde) partent en vol, les rencontres de la zone « espace » aussi */
int vol_permanent(void) { return E.w.cartes[E.w.carte].vol; }
static int vol_dans_astre(float x, float z) {
    for (int i = 0; i < E.w.nastres; i++) {
        const astre_t *a = &E.w.astres[i];
        if (a->carte == E.w.carte && a->r > 0 && hypotf(x - a->x, z - a->z) < a->r + VOL_MARGE) return 1;
    }
    return 0;
}
/* Astre i visité : toujours (65535 : la Terre), sinon son drapeau de visite (un drapeau inconnu : jamais) */
int astre_visite(int i) {
    const uint16_t f = E.w.astres[i].visite;
    return f == 65535 || (f < SC_MAX_FLAGS && ((E.flags[f >> 3] >> (f & 7)) & 1));
}
/* Le vaisseau est mis là où est Sera, Sera à bord ; près d'un astre (14 cases), tourné le long de son orbite (l'astre sur
   le côté), caméra derrière lui ; garder : direction et caméra gardées (partie chargée). Dans ses couleurs (palette 0) */
void vol_embarquer(int garder) {
    if (!vol_permanent() || !E.npcs) return;
    int k = -1;
    for (int i = 0; i < E.nnpc && k < 0; i++) {
        const int p = E.npcs[i].perso;
        if (p < E.w.npersos && (E.w.persos[p].flags & PERS_VEHICULE) && VEH_TYPE(E.w.persos[p].flags) == VEH_VAISSEAU) k = i;
    }
    if (k < 0) return;
    if (!garder) {
        const astre_t *best = NULL;
        float bd = 14;
        for (int i = 0; i < E.w.nastres; i++) {
            const astre_t *a = &E.w.astres[i];
            const float d = hypotf(E.sera.x - a->x, E.sera.z - a->z);
            if (a->carte == E.w.carte && a->r > 0 && d < bd) { bd = d; best = a; }
        }
        if (best) { const float d = atan2f(E.sera.x - best->x, E.sera.z - best->z) + PI_F / 2; E.sera.dir = atan2f(sinf(d), cosf(d)); }
        E.yaw = E.sera.dir;
    }
    if (E.ride != k) ride_lacher();
    npc_t *n = &E.npcs[k];
    n->x = n->hx = E.sera.x; n->z = n->hz = E.sera.z; n->dir = E.sera.dir; n->var = 0;
    const float dir = E.sera.dir;
    vehicle_board(k);
    E.sera.dir = n->dir = dir;
    E.ride_phase = 0; E.ride_alt = E.w.cartes[E.w.carte].vol_alt > 0 ? E.w.cartes[E.w.carte].vol_alt : SHIP_ALT / E.w.ech;
}

/* Case accessible au véhicule conduit : voiture, camion et mech partout, l'eau comprise (ils y passent à gué ou y
   coulent) ; bateau : seulement l'eau (rivières, lacs, sous les ponts) ; vaisseau : partout dans la carte (il survole) */
static int ride_ok(float x, float z) {
    const world_t *w = &E.w;
    const int t = ride_type(), n = w->n;
    if (x < 2 || z < 2 || x > n - 2 || z > n - 2) return 0;
    int i = (int)floorf(x), j = (int)floorf(z);
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c) return 0;
    if (t == VEH_VAISSEAU) return !vol_permanent() || !vol_dans_astre(x, z);   /* vol permanent : pas dans un astre */
    uint8_t f = c->flags[cell_local(i, j)];
    if (t == VEH_BATEAU && !(f & (F_WATER | F_RIVER))) return 0;
    if (t != VEH_BATEAU) {
        const float m = 0.45f / w->ech;
        for (uint32_t k = 0; k < c->nsolid; k++) {
            const float *r = &c->solids[k * 4];
            if (!(x > r[0] - m && x < r[2] + m && z > r[1] - m && z < r[3] + m) || rect_casse(r)) continue;
            if (fabsf(E.ride_v) > 1.2f && casser(c, r)) continue;   /* lancé : le grand décor casse au lieu d'arrêter */
            return 0;
        }
        if (w->ncoul) {   /* coulée d'une avalanche : on peut en sortir, pas y entrer */
            const int q = coulee_quitter(E.sera.x, E.sera.z, x, z, 0.45f / w->ech);
            coul_sortie = q; const int dans = coulee_dans(x, z, 0.45f / w->ech); coul_sortie = -1;
            if (dans) return 0;
        }
    }
    return !char_block(x, z, E.sera.x, E.sera.z, &E.npcs[E.ride]);
}

/* Vitesse maxi ici (cases par seconde) : hors route, la voiture et le camion n'en gardent que 55 % ; dans l'eau, un
   véhicule qui roule va à moitié de sa vitesse dans une rivière, et dans un lac de moins en moins vite à mesure que
   l'eau monte (le quart près de sa hauteur de gué) */
static float ride_speed(float x, float z, float vmax) {
    const int t = ride_type();
    const uint8_t f = flags_at(x, z);
    const int p = E.npcs[E.ride].perso;
    /* (à flot, il n'est plus « hors route » : sa vitesse sur l'eau n'est que celle des flotteurs) */
    float k = (t == VEH_VOITURE || t == VEH_CAMION) && !(f & (F_ROAD | F_STREET)) && !(water_kind(f) && vk_monte(p, VK_FLOTTEURS)) ? (vk_monte(p, VK_CHENILLES) ? VK_CHENILLES_K : 0.55f) : 1;
    if (t >= VEH_VOITURE && t <= VEH_MECH && veh_rolls(p) && water_kind(f))
        k *= (f & F_RIVER) ? 0.5f : fmaxf(vk_monte(p, VK_FLOTTEURS) ? VK_FLOT_K : 0.25f, 1 - 0.75f * eau_prof(x, z) * E.w.ech / veh_gue(p, t));
    return vmax * k;
}

/* Conduite : th accélère (1) ou recule (-1), st braque (-1 gauche, 1 droite), au clavier (haut, bas, gauche, droite)
   ou au doigt (E.drive_*, hud.c). La vitesse monte à l'accélération du véhicule, redescend au double (frein) ; la
   marche arrière va à 40 % du maxi. Le braquage tourne d'autant plus vite que le véhicule roule et s'inverse en marche
   arrière, comme une voiture. Contre un obstacle, il ne garde que la part de vitesse le long de l'obstacle. La caméra
   se replace derrière lui. */
static int vehicle_unboard_now(void);   /* Sera descend du véhicule (après l'atterrissage d'un vaisseau) */
static void vehicle_noye(void);         /* le véhicule a coulé : Sera en sort */
/* Choc contre un obstacle : perte, la vitesse perdue (cases/s). Choc, ou crash au-delà de 45 % de la vitesse maxi
   (2,5 cases/s au moins) ; plus grave pour le camion, le mech et le bateau ; un choc par 0,3 s au plus */
static float choc_t;
static void ride_choc(float perte, float vmax) {
    if (perte < 0.6f || fabsf(E.clock - choc_t) < 0.3f) return;
    choc_t = E.clock;
    const int t = ride_type();
    const float h = (t == VEH_CAMION ? 0.8f : t == VEH_MECH ? 0.7f : t == VEH_BATEAU ? 0.6f : 1.0f) * (0.95f + 0.1f * frand());
    if (perte > fmaxf(2.5f, 0.45f * vmax)) son_jouer(SON_CRASH, fminf(1, 0.55f + perte / (2 * vmax)), h);
    else son_jouer(SON_CHOC, fminf(0.85f, 0.3f + perte / 5), h);
}
/* Manette (stick analogique, eng_input_t.manette) : gaz à la verticale (zone morte de 20 %, plein gaz aux trois cinquièmes
   de la course : il reste du gaz en braquant fort), volant à l'horizontale (zone morte de 10 %), en proportion ; hors
   course, adouci (au carré) et réduit à MANETTE_VOLANT du braquage du clavier (au stick changé en touches, braquage
   à fond dès 38 % de la course : ingérable) ; en course, la courbe de la course s'applique (COURSE_EXPO, COURSE_VOLANT) */
#define MANETTE_VOLANT 0.4f
#define MANETTE_COURSE 0.8f   /* à pied : Sera court au-delà de cette part de la course du stick, marche en deçà */
/* Rotation de la caméra aux touches (ENG_KEY_ROT_L / R), en radians par seconde : clavier de la page web (A / E) ; écran
   rectangulaire (RetroFW, L / R de la LDK, 11/11/2026, demande de l'utilisateur : « tourner beaucoup plus vite ») 4, 45° en
   0,2 s */
#ifndef CAM_TOUCHE_V
#define CAM_TOUCHE_V (ENG_RECT ? 4.0f : 1.6f)
#endif
/* Sera conduit (véhicule ou course), sans menu, pause ni combat : la plateforme peut changer ses commandes (RetroFW :
   accélérer à la gâchette de droite, freiner à celle de gauche) */
int eng_conduite(void) { return (E.ride >= 0 || CR.on) && !BT.on && !hud_pause(); }
static float stick_axe(float v, float mort, float plein) {
    const float a = fabsf(v), t = a <= mort ? 0 : fminf(1, (a - mort) / (plein - mort));
    return v < 0 ? -t : t;
}
static void drive_input(const eng_input_t *in, float *th, float *st) {
    *th = *st = 0;
    if (E.ui_block) return;
    if (in->manette) {
        *th = -stick_axe(in->stick_y, 0.2f, 0.6f);
        *st = stick_axe(in->stick_x, 0.1f, 1);
        if (!CR.on) *st *= fabsf(*st) * MANETTE_VOLANT;
        return;
    }
    if (in->keys & ENG_KEY_UP) *th += 1;
    if (in->keys & ENG_KEY_DOWN) *th -= 1;
    if (in->keys & ENG_KEY_LEFT) *st -= 1;
    if (in->keys & ENG_KEY_RIGHT) *st += 1;
    if (E.drive_touch) { *th = E.drive_th; *st = E.drive_st; }
}
static void encounter_step(float dist);   /* rencontres (plus bas) : en vol permanent aussi */
/* Pas du mech conduit (01/11/2026) : un appui lourd (SON_PAS_MECH) toutes les MECH_FOULEE cases de chemin, foulée allongée
   pour un mech rapide (au plus un pas par MECH_CADENCE s à sa vitesse maxi) ; ses planches n'ont pas toutes une marche
   dont suivre les images. driveStep de l'émulateur */
#define MECH_FOULEE 1.6f
#define MECH_CADENCE 0.4f
static void ride_drive(const eng_input_t *in, float dt) {
    actor_t *s = &E.sera;
    float vmax, acc, th, st;
    veh_params(E.npcs[E.ride].perso, &vmax, &acc);
    const int vol = vol_permanent();
    if (vol) { vmax *= VOL_VITESSE; acc *= VOL_VITESSE; }   /* vol permanent : orbites resserrées */
    { const float mot = 1 + VK_MOTEUR_X * vk_monte(E.npcs[E.ride].perso, VK_MOTEUR); vmax *= mot; acc *= mot; }   /* son moteur (pièces du véhicule de Sera) */
    drive_input(in, &th, &st);
    pilote_conduire(&th, &st, ride_speed(s->x, s->z, vmax), vmax, acc, dt);   /* conduite assistée, pilote automatique (pilote.c) */
    /* Vaisseau : décollage et atterrissage à la verticale (1,2 case/s, sans avancer) ; en vol, les boutons d'altitude
       (ou R / F) le font monter ou descendre entre 0,3 case et son altitude maxi. Vol permanent : toujours à l'altitude de
       croisière de la carte */
    if (ride_type() == VEH_VAISSEAU) {
        const int p = E.npcs[E.ride].perso;
        const float amax = p < MAX_PERSOS && E.w.veh_alt[p] > 0 ? E.w.veh_alt[p] : 4.0f, cruise = fminf(amax, SHIP_ALT / E.w.ech);
        int a = E.drive_alt;
        if (!E.ui_block && (in->keys & ENG_KEY_ALT_UP)) a = 1;
        if (!E.ui_block && (in->keys & ENG_KEY_ALT_DOWN)) a = -1;
        if (vol) { E.ride_phase = 0; E.ride_alt = E.w.cartes[E.w.carte].vol_alt > 0 ? E.w.cartes[E.w.carte].vol_alt : cruise; }
        else if (E.ride_phase == 1) {
            th = st = 0; E.ride_alt += 1.2f * dt;
            if (E.ride_alt >= cruise) { E.ride_alt = cruise; E.ride_phase = 0; }
        } else if (E.ride_phase == 2) {
            th = st = 0; E.ride_alt -= 1.2f * dt;
            if (E.ride_alt <= 0) { E.ride_alt = 0; E.ride_phase = 0; if (vehicle_unboard_now()) return; }
        } else E.ride_alt = fmaxf(0.3f, fminf(amax, E.ride_alt + a * 1.5f * dt));
    }
    /* Lac ou mer plus profond que sa hauteur de gué : il coule (plus de gaz ni de volant ; il glisse sur son élan, que
       l'eau freine), porté d'abord à fleur d'eau puis jusqu'au fond en COULE_T secondes ; Sera en sort 0,6 s après */
    if (!water_kind(flags_at(s->x, s->z))) { E.rive_x = s->x; E.rive_z = s->z; }
    if (E.ride_coule > 0 || veh_noye(E.npcs[E.ride].perso, s->x, s->z)) {
        if (E.ride_coule <= 0) { son_jouer(SON_PLOUF, 0.8f, 0.7f); son_jouer(SON_CHOC, 0.35f, 0.5f); }
        E.ride_coule += dt; th = st = 0;
        E.ride_alt = fmaxf(0, eau_prof(s->x, s->z) - 0.6f * veh_gue(E.npcs[E.ride].perso, ride_type()) / E.w.ech) * fmaxf(0, 1 - E.ride_coule / COULE_T);
        if (E.ride_coule > COULE_T + 0.6f) { vehicle_noye(); return; }
    }
    const float brk = acc * 2, top = ride_speed(s->x, s->z, vmax);
    const float target = th > 0 ? top * th : th * top * 0.4f;
    float *v = &E.ride_v;
    if (E.ride_coule > 0) *v *= expf(-0.8f * dt);
    else if (*v < target) *v = fminf(target, *v + (*v < 0 ? brk : acc) * dt);   /* freine en arrière, ou accélère */
    else if (*v > target) *v = fmaxf(target, *v - (*v > 0 ? brk : acc) * dt);   /* freine, ou recule */
    if (fabsf(*v) < 0.02f && target == 0) *v = 0;
    /* Braquage : jusqu'à 2,4 rad/s, réduit à basse vitesse */
    const float grip = fminf(1, fabsf(*v) / (0.35f * vmax));
    s->dir += st * 2.4f * grip * (*v < 0 ? -1 : 1) * dt;
    s->dir = atan2f(sinf(s->dir), cosf(s->dir));
    if (*v != 0) {
        const float hx = sinf(s->dir), hz = cosf(s->dir), nx = s->x + hx * *v * dt, nz = s->z + hz * *v * dt;
        int mx = ride_ok(nx, s->z), mz = ride_ok(s->x, nz);
        /* En biais contre le coin d'un obstacle : chaque axe passe, pas les deux ensemble ; un seul (sinon le véhicule
           entrait dans la marge du décor et y restait coincé, chaque pas refusé ; 01/11/2026, driveStep de l'émulateur) */
        if (mx && mz && !ride_ok(nx, nz)) { if (fabsf(hx) >= fabsf(hz)) mz = 0; else mx = 0; }
        const float v0 = fabsf(*v), ox = s->x, oz = s->z;
        if (mx) s->x = nx;
        if (mz) s->z = nz;
        if (!mx && !mz) *v = 0;
        else if (!mx) *v *= fabsf(hz);
        else if (!mz) *v *= fabsf(hx);
        ride_choc(v0 - fabsf(*v), vmax);
        if (mx != mz && fabsf(*v) > 0.8f) E.frotte = fminf(1, fabsf(*v) / vmax);   /* il glisse le long de l'obstacle */
        if (vol && (mx || mz)) encounter_step(hypotf(s->x - ox, s->z - oz) * E.w.ech);   /* rencontres de l'espace, en vol */
        const float foulee = fmaxf(MECH_FOULEE, MECH_CADENCE * vmax);
        if (ride_type() == VEH_MECH && E.ride_coule <= 0 && (E.mech_pas += hypotf(s->x - ox, s->z - oz)) >= foulee) {
            E.mech_pas = fmodf(E.mech_pas, foulee);
            son_jouer(SON_PAS_MECH, 0.55f + 0.3f * fminf(1, v0 / vmax), 0.92f + 0.1f * frand());
        }
    }
    /* Caméra derrière le véhicule */
    const float dy = atan2f(sinf(s->dir - E.yaw), cosf(s->dir - E.yaw));
    E.yaw += dy * fminf(1, dt * 2.5f);
    const int mv = fabsf(*v) > 0.05f;
    if (s->state != mv) { s->state = mv; s->frame = 0; s->anim = 0; }
    s->anim += dt * (mv ? fminf(1, fabsf(*v) / (vmax * 0.3f)) : 1);
    if (s->anim > (mv ? 0.13f : 0.22f)) { s->anim = 0; s->frame = (s->frame + 1) & 1023; }
}

int vehicle_board(int k) {
    if (!is_vehicle(k)) return 0;
    E.ride = k; E.talk = -2; E.has_dest = 0; E.ride_v = 0;
    E.ride_alt = 0; E.ride_phase = VEH_TYPE(E.w.persos[E.npcs[k].perso].flags) == VEH_VAISSEAU;   /* décollage vertical */
    E.ride_coule = 0; E.rive_x = E.sera.x; E.rive_z = E.sera.z;
    son_jouer(SON_PORTIERE, 0.6f, 1);
    E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z; E.sera.dir = E.npcs[k].dir;
    return 1;
}

int ship_alt_ok(void) { return E.ride >= 0 && ride_type() == VEH_VAISSEAU && !E.ride_phase && !CR.on && !vol_permanent(); }   /* vol permanent : altitude de croisière */

/* Nouvel exemplaire du véhicule perso en (x, z) : place libre, sinon celle du véhicule ajouté (volé, de course) laissé
   le plus loin ; -1 s'il n'y en a pas */
int vehicle_spawn(int perso, float x, float z, float dir) {
    if (!E.npcs) return -1;
    int s = -1;
    if (E.nnpc < E.npc_cap) s = E.nnpc++;
    else {
        float far = -1;
        for (int m = E.nnpc_base; m < E.nnpc; m++) {
            const float d = hypotf(E.npcs[m].x - E.sera.x, E.npcs[m].z - E.sera.z);
            if (m != E.ride && !E.npcs[m].mien && d > far) { far = d; s = m; }   /* jamais le véhicule de Sera (garage.c) */
        }
    }
    if (s < 0) return -1;
    npc_t *n = &E.npcs[s];
    memset(n, 0, sizeof *n);
    n->x = n->hx = x; n->z = n->hz = z; n->dir = dir; n->perso = (uint8_t)perso;
    return s;
}

/* Conducteur éjecté au vol (comme ejecter de l'émulateur) : un passant de la bibliothèque (qui flâne, ni véhicule ni
   ennemi) ; garde ou officière de l'Ordre pour une voiture de police, soldat pour un véhicule de l'armée, s'il y en a. Posé
   à gauche du véhicule, il s'enfuit 7 cases à l'opposé de Sera, en courant (fuit), puis redevient un passant */
const char *vol_cri = "Véhicule volé";
static const char *const CRIS[] = { "« Au voleur ! »", "« Hé ! Ma voiture ! »", "« Rends-la-moi ! »", "« À l'aide ! On me vole ! »" };
static void ejecter(const traffic_t *t) {
    const world_t *w = &E.w;
    const int sv = w->veh_service[t->perso];
    static int cand[MAX_PERSOS] ENG_FROID, pref[MAX_PERSOS] ENG_FROID;   /* statiques, en PSRAM : la pile du jeu */
    int nc = 0, np = 0;
    for (int p = 0; p < w->npersos; p++) {
        const perso_t *pe = &w->persos[p];
        if (!(pe->flags & PERS_FLANE) || (pe->flags & PERS_VEHICULE) || pe->sheet < 0 || !perso_present(p) || !pe->nom) continue;
        int ennemi = 0;
        for (int e = 0; e < w->nenemies && !ennemi; e++) ennemi = w->enemies[e].perso == p;
        if (ennemi) continue;
        cand[nc++] = p;
        if ((sv == 1 && (strstr(pe->nom, "GARDE") || strstr(pe->nom, "OFFICI"))) || (sv == 4 && strstr(pe->nom, "SOLDAT"))) pref[np++] = p;
    }
    vol_cri = CRIS[(int)(frand() * 4) & 3];
    if (!nc) return;
    const int p = np ? pref[(int)(frand() * np) % np] : cand[(int)(frand() * nc) % nc];
    const float lx = -cosf(t->dir), lz = sinf(t->dir);   /* à gauche du cap (sin, cos) */
    float x = t->x + lx * 0.9f, z = t->z + lz * 0.9f;
    if (!walkable_for(x, z, NULL, 0)) { x = t->x - lx * 0.9f; z = t->z - lz * 0.9f; }
    const int s = vehicle_spawn(p, x, z, t->dir);
    if (s < 0) return;
    npc_t *n = &E.npcs[s];
    float ax = x - E.sera.x, az = z - E.sera.z, d = hypotf(ax, az);
    if (d < 0.01f) { ax = lx; az = lz; d = 1; }
    n->tx = x + ax / d * 7; n->tz = z + az / d * 7; n->fuit = 1; n->walking = 1;
    n->graine = (uint16_t)(frand() * 2048); n->var = (uint8_t)pvar_choix(p, n->graine);
}

/* Voler le véhicule k du trafic : il devient un exemplaire et Sera le conduit à sa vitesse du moment ; son conducteur
   est éjecté (ejecter) */
int vehicle_steal(int k) {
    traffic_t *t = &E.traffic[k];
    if (k < 0 || k >= TRAFFIC_MAX || !t->on) return 0;
    const int s = vehicle_spawn(t->perso, t->x, t->z, t->dir);
    if (s < 0) return 0;
    const float v = t->v;
    t->on = 0;
    E.npcs[s].var = t->var;   /* ses couleurs du trafic */
    vehicle_board(s);
    E.ride_v = v;
    ejecter(t);
    police_vol(s);   /* les autorités sont alertées : recherchée (police.c) */
    return 1;
}
/* Arrestation (police.c) : Sera sort du véhicule conduit, arrêté net, sur la première place libre autour de lui (sinon à
   sa place) */
static int sera_poser(float x0, float z0, int swim);
void vehicule_sortir(void) {
    if (E.ride < 0) return;
    npc_t *v = &E.npcs[E.ride];
    v->walking = 0;
    E.ride = -1; E.ride_v = 0; E.ride_alt = 0; E.ride_coule = 0; E.ride_phase = 0; E.has_dest = 0; E.talk = -2;
    if (!sera_poser(v->x, v->z, 0)) { E.sera.x = v->x; E.sera.z = v->z; }
    son_jouer(SON_PORTIERE, 0.6f, 0.9f);
}
/* Véhicule ajouté k (volé, de course ; pas celui que Sera conduit) retiré du monde : confisqué ; le dernier exemplaire prend
   sa place (comme course_rendre) */
void vehicule_retirer(int k) {
    if (k < E.nnpc_base || k >= E.nnpc || k == E.ride) return;
    const int der = E.nnpc - 1;
    if (k != der) { E.npcs[k] = E.npcs[der]; if (E.ride == der) E.ride = k; }
    E.nnpc--; E.talk = -2;
}
int sol_pieton(float x, float z) { return sol_libre(x, z, 0); }

/* Descendre : Sera se pose sur la première place praticable à pied autour du véhicule (le vaisseau doit être au-dessus
   d'une case praticable : il atterrit) */
int vehicle_unboard(void) {
    if (E.ride < 0 || E.ride_phase || vol_permanent()) return 0;   /* vol permanent : on ne descend jamais du vaisseau */
    if (E.ride_coule > 0) { vehicle_noye(); return 1; }   /* il coule : Sera en sort tout de suite */
    if (ride_type() == VEH_VAISSEAU) {
        const npc_t *v = &E.npcs[E.ride];
        int i = (int)floorf(v->x), j = (int)floorf(v->z);
        const chunk_t *c = chunk_of_cell(i, j);
        uint8_t f = c ? c->flags[cell_local(i, j)] : 0xFF;
        if (!c || (!(f & F_ROAD) && (f & (F_WATER | F_RIVER)))) return 0;
        if (E.ride_alt > 0.02f) { E.ride_phase = 2; return 1; }   /* atterrissage, puis Sera descend (ride_drive) */
    }
    return vehicle_unboard_now();
}
/* Sera se pose sur la première place praticable de 0,7 à 2,6 m de (x0, z0) (swim : à la nage aussi) */
static int sera_poser(float x0, float z0, int swim) {
    for (float r = 0.7f; r <= 2.6f; r += 0.3f)
        for (int a = 0; a < 16; a++) {
            float x = x0 + cosf(a * PI_F / 8) * r / E.w.ech, z = z0 + sinf(a * PI_F / 8) * r / E.w.ech;
            if (walkable_for(x, z, NULL, swim)) { E.sera.x = x; E.sera.z = z; return 1; }
        }
    return 0;
}
/* Fin d'une course courue dans un véhicule amené pour elle (course_start) : il repart, retiré du monde (le dernier
   exemplaire prend sa place) ; Sera reprend le véhicule qu'elle conduisait avant (laissé près de la ligne), sinon elle
   descend là où elle est (courseRendre de l'émulateur) */
void course_rendre(int pret, int avant) {
    if (pret < E.nnpc_base || pret >= E.nnpc) return;
    const float x = E.npcs[pret].x, z = E.npcs[pret].z;
    const int der = E.nnpc - 1;
    E.ride = -1; E.ride_v = 0; E.ride_alt = 0; E.ride_coule = 0; E.ride_phase = 0; E.has_dest = 0; E.talk = -2;
    if (pret != der) { E.npcs[pret] = E.npcs[der]; if (avant == der) avant = pret; }
    E.nnpc--;
    son_jouer(SON_PORTIERE, 0.6f, 0.9f);
    if (avant >= 0 && avant < E.nnpc && is_vehicle(avant)) vehicle_board(avant);
    else if (!sera_poser(x, z, 0)) { E.sera.x = x; E.sera.z = z; }
}
void ride_lacher(void) {
    if (E.ride >= 0) E.npcs[E.ride].walking = 0;
    E.ride = -1; E.ride_v = 0; E.ride_alt = 0; E.ride_coule = 0; E.ride_phase = 0; E.talk = -2;
}
static int vehicle_unboard_now(void) {
    npc_t *v = &E.npcs[E.ride];
    if (!sera_poser(v->x, v->z, 1)) return 0;   /* dans l'eau : à la nage, là où elle peut nager */
    son_jouer(SON_PORTIERE, 0.6f, 0.9f); v->walking = 0; E.ride = -1; E.ride_v = 0; E.has_dest = 0;
    return 1;
}
/* Le véhicule conduit a coulé : il reste au fond ; Sera en sort à la nage si elle peut nager là, sinon elle revient sur
   la dernière rive où il roulait (fondu au noir) */
static void vehicle_noye(void) {
    npc_t *v = &E.npcs[E.ride];
    v->walking = 0; E.ride = -1; E.ride_v = 0; E.ride_alt = 0; E.ride_coule = 0; E.has_dest = 0;
    if (!sera_poser(v->x, v->z, 1)) {
        if (!sera_poser(E.rive_x, E.rive_z, 0)) { E.sera.x = E.rive_x; E.sera.z = E.rive_z; }
        BT.fade_t = E.clock;
    }
    hud_toast("Le véhicule a coulé");
}

/* Interlocuteur : l'exemplaire présent le plus proche de Sera à moins de 2,4 m (-2 : personne), ou un véhicule du trafic */
/* Personnage p : un ennemi (fiche de combat de ENNE) ? */
static int perso_ennemi(int p) {
    for (int e = 0; e < E.w.nenemies; e++) if (E.w.enemies[e].perso == p) return 1;
    return 0;
}
static int talk_target(void) {
    float bd = 2.4f / E.w.ech;
    int best = -2;
    for (int k = 0; k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (!npc_present(n) || fabsf(n->x - E.sera.x) >= bd || fabsf(n->z - E.sera.z) >= bd) continue;
        if (acteur_de(n->perso) >= 0) continue;   /* remplacé par son acteur, le temps d'une scène */
        if (perso_ennemi(n->perso)) continue;     /* ennemi de l'histoire posé sur la carte (Glaneur blanc, Sentinelle…) : on ne lui parle pas, sa zone lance la scène (02/11/2026 ; talkTarget de l'émulateur) */
        if (E.in.on && is_vehicle(k)) continue;   /* voiture garée d'un garage : ni dialogue ni conduite */
        if (E.w.ile[0] && is_vehicle(k) && VEH_TYPE(E.w.persos[n->perso].flags) == VEH_BATEAU) continue;   /* île : barque amarrée (pirogue de Maia, 11/11/2026) : on ne la prend pas ; talkTarget de l'émulateur de l'île (sauf le sous-marin, dans le navigateur) */
        if (is_vehicle(k) && veh_noye(n->perso, n->x, n->z)) continue;   /* au fond d'un lac : perdu */
        float d = hypotf(n->x - E.sera.x, n->z - E.sera.z);
        if (d < bd) { bd = d; best = k; }
    }
    for (int k = 0; k < TRAFFIC_MAX; k++) {   /* véhicule du trafic : il s'arrête, on peut le voler */
        const traffic_t *t = &E.traffic[k];
        if (!t->on) continue;
        float d = hypotf(t->x - E.sera.x, t->z - E.sera.z);
        if (d < bd) { bd = d; best = TALK_TRAF(k); }
    }
    return best;
}

/* Promeneurs : trois flâneurs sur cinq (d'après leur graine, comme l'émulateur : promeneur) marchent le long des
   trottoirs : un cap tenu (souvent le même, sinon un quart de tour, rarement un huitième ou un demi-tour), une cible 3 à
   7 cases plus loin, atteinte par des cases praticables hors des chaussées (PROM_TRAVERSE du temps, ils traversent :
   jusqu'au trottoir d'en face, près d'un carrefour de préférence) ; au-delà de PROM_RAYON cases de leur place, ils
   prennent un cap qui les en rapproche. Les autres flânent autour de leur place, jamais sur la chaussée. */
#define PROM_RAYON 30.0f
#define PROM_TRAVERSE 0.06f   /* part des promeneurs qui traversent la chaussée (promCible de l'émulateur) */
#define PROM_TRAVERSE_L 9.0f  /* cases : une traversée va jusqu'au trottoir d'en face (artère de 5 cases comprise) */
static int promeneur(const npc_t *n) { return !ile_enleve_est(n) && (n->meneur || ((n->graine * 2654435761u) >> 16) % 5 < 3); }   /* meneur d'une patrouille : toujours ; l'habitant enlevé (île) : jamais */
/* Suiveur d'une patrouille (comme suivreChef de l'émulateur) : un point derrière son meneur, 0,9 case par rang, dans la
   direction où il regarde ; il presse le pas s'il traîne, le rejoint s'il s'est perdu ; arrêté, il regarde où regarde
   son meneur */
/* Chaussée (route ou rue) en (x, z) : les piétons n'y marchent que pour traverser */
static int chaussee_at(float x, float z) { return (flags_at(x, z) & (F_ROAD | F_STREET)) != 0; }
/* Exemplaire posé sur la chaussée (passant semé sur une rue, personnage de l'histoire, garde d'une porte : le trafic s'y
   arrêtait et faisait demi-tour) : sa place passe sur la case praticable hors de la chaussée la plus proche (HORS_CHAUSSEE
   cases au plus), une fois, quand il arrive à moins de deux fois le rayon d'activité de Sera (avant d'être vu) ; les
   véhicules, les objets (épaves), le starter et un conducteur qui s'enfuit restent (horsChaussee de l'émulateur, à garder
   identiques) */
#define HORS_CHAUSSEE 4.0f
static void hors_chaussee(npc_t *n) {
    const perso_t *pe = &E.w.persos[n->perso];
    if ((pe->flags & PERS_VEHICULE) || PERS_EST_OBJET(pe->flags) || n->depart || n->fuit) return;
    for (int c = 0; c < 4; c++)   /* chunks autour pas encore chargés : plus tard */
        if (!chunk_of_cell((int)floorf(n->hx + (c & 1 ? HORS_CHAUSSEE : -HORS_CHAUSSEE)), (int)floorf(n->hz + (c & 2 ? HORS_CHAUSSEE : -HORS_CHAUSSEE)))) return;
    n->place_ok = 1;
    if (!chaussee_at(n->hx, n->hz)) return;
    for (float r = 0.5f; r <= HORS_CHAUSSEE; r += 0.5f)
        for (int a = 0; a < 16; a++) {
            const float x = n->hx + cosf(a * PI_F / 8) * r, z = n->hz + sinf(a * PI_F / 8) * r;
            if (chaussee_at(x, z) || !walkable_for(x, z, n, 0)) continue;
            n->hx = n->x = n->tx = x; n->hz = n->z = n->tz = z; n->walking = 0;
            return;
        }
}
static void suivre_chef(npc_t *n, float dt) {
    const npc_t *c = &E.npcs[n->chef - 1];
    const float d0 = 0.9f * n->rang;
    float tx = c->x - sinf(c->dir) * d0, tz = c->z - cosf(c->dir) * d0;
    const int sur = chaussee_at(tx, tz) && !chaussee_at(c->x, c->z);
    if (sur) { tx = c->x; tz = c->z; }   /* sa place derrière tombe sur la chaussée : vers son meneur */
    const float dx = tx - n->x, dz = tz - n->z, d = hypotf(dx, dz);
    if (d > 10) {   /* perdu : posé à sa place, ou près de son meneur, hors de la chaussée */
        for (float r = sur ? 0.5f : 0; r <= 2; r += 0.5f)
            for (int a = 0; a < (r > 0 ? 8 : 1); a++) {
                const float x = tx + cosf(a * PI_F / 4) * r, z = tz + sinf(a * PI_F / 4) * r;
                if (walkable_for(x, z, n, 0) && !chaussee_at(x, z)) { n->x = x; n->z = z; return; }
            }
        return;
    }
    if (d < 0.25f) {
        n->walking = 0; n->dir = c->dir; n->anim += dt;
        if (n->anim > 0.22f) { n->anim = 0; n->frame++; }
        return;
    }
    float sp = (d > 1.5f ? 2.0f : 1.3f) / E.w.ech * dt;
    if (sp > d) sp = d;
    const float nx = n->x + dx / d * sp, nz = n->z + dz / d * sp;
    if (!walkable_for(nx, nz, n, 0) && !(chaussee_at(n->x, n->z) && sol_libre(nx, nz, 0))) { n->walking = 0; return; }   /* gêné (sur la chaussée : il en sort quand même) */
    /* pas sur la chaussée tant que son meneur n'y est pas (il traverse) : il attend au bord */
    if (chaussee_at(nx, nz) && !chaussee_at(n->x, n->z) && !chaussee_at(c->x, c->z)) { n->walking = 0; return; }
    n->x = nx; n->z = nz; n->dir = atan2f(dx, dz); n->walking = 1; n->anim += dt;
    if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
}
/* Près d'un carrefour (une case de rue à moins de 2 cases plus large que RUE_LARGE en travers) : là où les passages
   piétons sont peints (WorldExport.passagesPietons de l'Atelier : juste avant un carrefour) */
static int pres_carrefour(float x, float z) {
    for (int dj = -2; dj <= 2; dj++) for (int di = -2; di <= 2; di++) {
        const int i = (int)floorf(x) + di, j = (int)floorf(z) + dj;
        int ux, uz, R, L;
        if (rue_case(i, j) && rue_axe(i, j, 1, 0, &ux, &uz, &R, &L) > RUE_LARGE && rue_axe(i, j, 0, 1, &ux, &uz, &R, &L) > RUE_LARGE) return 1;
    }
    return 0;
}
static int prom_cible(npc_t *n) {
    static const signed char ORDRE[8] = { 0, 2, -2, 1, -1, 3, -3, 4 };   /* écarts au cap actuel, en huitièmes de tour */
    const int h0 = (int)lroundf(n->dir / (PI_F / 4)) & 7, garde = frand() < 0.7f;
    const float ox = n->hx - n->x, oz = n->hz - n->z, loin = hypotf(ox, oz) > PROM_RAYON;
    const int sur = chaussee_at(n->x, n->z), traverse = frand() < PROM_TRAVERSE || sur;   /* déjà sur la chaussée : il en sort */
    /* Traversée : jusqu'au trottoir d'en face (PROM_TRAVERSE_L cases au plus), près d'un carrefour (passages piétons) de
       préférence (première passe), ailleurs sinon ; jamais d'arrêt sur la chaussée (la cible recule jusqu'au trottoir) */
    for (int passe = 0; passe < (traverse ? 2 : 1); passe++)
        for (int t = 0; t < 8; t++) {
            int e = ORDRE[t];
            if (!garde && t == 0) continue;                                   /* tourner cette fois-ci */
            if (!garde && t < 3 && frand() < 0.5f) e = -e;                    /* à gauche ou à droite */
            const int h = (h0 + e + 8) & 7;
            const float sx = sinf(h * PI_F / 4), sz = cosf(h * PI_F / 4);
            if (loin && sx * ox + sz * oz <= 0) continue;                     /* trop loin : un cap qui rapproche */
            const float L = traverse ? PROM_TRAVERSE_L : 3 + frand() * 4;
            float ok = 0, cx = 0, cz = 0, run = 0, run_max = 0;
            int pave = 0;
            for (float s = 0.25f; s <= L; s += 0.25f) {   /* au quart de case : une clôture mince n'est pas sautée */
                const float px = n->x + sx * s, pz = n->z + sz * s;
                if (!walkable_for(px, pz, n, 0)) break;
                if (chaussee_at(px, pz)) {
                    if (!traverse) break;
                    if (!pave) {   /* au plus une fois et demie la largeur de la rue (en biais, pas le long) ; route : 3 cases */
                        int ux, uz, R, Lc;
                        const int i = (int)floorf(px), j = (int)floorf(pz), W = rue_case(i, j) ? rue_axe(i, j, sx, sz, &ux, &uz, &R, &Lc) : 2;
                        pave = 1; cx = px; cz = pz; run_max = W > RUE_LARGE ? 6.0f : W * 1.5f;
                    }
                    if ((run += 0.25f) > run_max) break;
                }
                ok = s;
            }
            while (ok >= 0.25f && chaussee_at(n->x + sx * ok, n->z + sz * ok)) ok -= 0.25f;   /* pas d'arrêt sur la chaussée */
            if (ok < (sur ? 0.5f : 1.5f) || (pave && !passe && !sur && !pres_carrefour(cx, cz))) continue;   /* sur la chaussée : le plus court pour en sortir */
            n->tx = n->x + sx * ok; n->tz = n->z + sz * ok; n->walking = 1;
            return 1;
        }
    return 0;
}

/* ---------- Pluie de cendre (cendreMaj de l'émulateur, à garder identiques) ---------- */
/* Sur la planète, toute pluie est de la cendre : celle qui a suivi la Cendre et tué des enfants (E04), que les Glaneurs
   ramassent et compactent en briques (document « Scénario et lore »). Effets, à partir de CENDRE_SEUIL d'intensité :
   - dehors, sans abri (intérieur, habitacle d'un véhicule : tout sauf une moto) ni MASQUE À FILTRE dans le sac, l'équipe
     tousse : tous les CENDRE_PAS secondes passées dessous (ni menu, ni dialogue, ni scène, ni course), chaque membre
     perd CENDRE_PV de ses PV max × l'intensité, jamais sous CENDRE_PLANCHER de ses PV max (la cendre affaiblit avant un
     combat, elle ne tue pas) ; la jauge des PV pulse à chaque coup (hud.c) ; une annonce la première fois, puis un
     message au premier coup de chaque averse et quand toute l'équipe est au plancher ;
   - masque ou non : brouillard plus dense et plus gris (render.c : CENDRE_BRUME, render_ambiance), rencontres plus
     fréquentes (× 1 + CENDRE_RENC × l'intensité) et Glaneurs sortis la ramasser (poids × 1 + CENDRE_GLANEUR ×
     l'intensité), passants à l'abri (abri_maj). */
#define CENDRE_SEUIL 0.15f
#define CENDRE_PAS 4.0f
#define CENDRE_PV 0.025f
#define CENDRE_PLANCHER 0.25f
#define CENDRE_RENC 0.6f
#define CENDRE_GLANEUR 3.0f
float cendre_dehors(void) { return !E.w.ile[0] && !E.w.ciel && (!E.in.on || BT.on) && !neige_ici() ? E.rain : 0; }   /* île : pluie d'eau, sans cendre (cendreDehors de l'émulateur de l'île) */
/* Neige (01/11/2026, demande de l'utilisateur) : sur la planète, dehors, dans la classe « neige » (LAND_NEIGE, rang de
   Continent.LAND) de la grille des zones sous Sera, la pluie tombe en neige : flocons (render.c), ni cendre (toux,
   brouillard gris, rencontres) ni éclairs (neigeIci de l'émulateur) */
#define LAND_NEIGE 14
#define LAND_MONTAGNE 6    /* (rangs de Continent.LAND de l'atelier) */
#define LAND_CAVERNE 18
int neige_ici(void) {
    const world_t *w = &E.w;
    if (w->carte || w->ciel || !w->zone || w->n <= 0 || (E.in.on && !BT.on)) return 0;
    const int i = (int)floorf(E.sera.x) * w->zone_n / w->n, j = (int)floorf(E.sera.z) * w->zone_n / w->n;
    return i >= 0 && j >= 0 && i < w->zone_n && j < w->zone_n && w->zone[j * w->zone_n + i] == LAND_NEIGE;
}
/* Brouillard en ville (31/10/2026) : dans les rues, les bâtiments bornent déjà la vue, le brouillard ne faisait que griser
   les façades. Sera sur une zone urbaine de la planète (urbain, industriel, rue : rangs 8, 10, 11 de Continent.LAND,
   bloc de la grille des rencontres) : E.ville_v tend vers 1 en VILLE_FONDU s, et render.c fait commencer le brouillard aux
   VILLE_RECUL de sa profondeur ; la pluie de cendre le rend (ambiance) en proportion de son intensité (villeMaj de
   l'émulateur) */
#define VILLE_FONDU 1.5f
#define ZOOM_FONDU 1.2f
/* (x, z) sur une zone urbaine de la planète (bloc de la grille des rencontres) ; aussi pour les lieux visités du téléport
   (hud.c) et le pilote automatique (pilote.c) */
int zone_urbaine(float x, float z) {
    const world_t *w = &E.w;
    const int zn = w->c0.zone_n, n = w->cartes[0].n;   /* la grille de la planète, même depuis une autre carte */
    if (!w->c0.zone || n <= 0) return 0;
    const int i = (int)floorf(x) * zn / n, j = (int)floorf(z) * zn / n;
    if (i < 0 || j < 0 || i >= zn || j >= zn) return 0;
    const int t = w->c0.zone[j * zn + i];
    return t == 8 || t == 10 || t == 11;
}
static void ville_maj(float dt) {
    const int ville = (!E.w.carte && zone_urbaine(E.sera.x, E.sera.z)) || E.w.cartes[E.w.carte].vision;   /* (vision : la ville d'avant la Cendre, toute en ville) */
    const float cible = ville ? fmaxf(0, 1 - 2 * cendre_dehors()) : 0;
    E.ville_v += (cible - E.ville_v) * fminf(1, dt / VILLE_FONDU * 3);
    /* Zoom fixe selon le lieu (01/11/2026, demande de l'utilisateur) : ZOOM_VILLE (300 %) en ville, ZOOM_DEHORS (300 % depuis le 07/11/2026, 200 % avant)
       ailleurs, ZOOM_INTERIEUR (350 %) dans un intérieur, quel que soit le zoom du paquet ; au changement de lieu, le zoom
       y va en ZOOM_FONDU s, sans annonce (hud.c), et il y revient après tout autre changement (sauvegarde chargée,
       molette de la page web) : ni pincement ni réglage du menu, sauf avec le code CODE_ZOOM. Pas pendant une caméra de
       scène, une course ni le vol permanent (villeMaj de l'émulateur) */
    /* Intérieur (02/11/2026) : ZOOM_INTERIEUR, pris d'un coup au passage de la porte (le fondu le cache) ; code CODE_ZOOM :
       zoom manuel, le lieu ne l'impose plus */
    const int lieu = E.in.on ? 3 : ville ? 2 : 1, porte = (lieu == 3) != (E.zoom_lieu == 3) && E.zoom_lieu;
    if (lieu != E.zoom_lieu) { E.zoom_lieu = lieu; E.zoom_cible = lieu == 3 ? (E.w.ile[0] ? ILE_ZOOM_INTERIEUR : ZOOM_INTERIEUR) : E.w.ile[0] ? ILE_ZOOM : ville ? ZOOM_VILLE : ZOOM_DEHORS; }   /* (île : ILE_ZOOM, ILE_ZOOM_INTERIEUR) */
    E.zoom_auto = !E.zoom_libre;
    if (E.zoom_libre) return;
    if (porte && !MISE.cam_on && !carte_plein()) eng_set_zoom(E.zoom_cible);
    if (E.zoom != E.zoom_cible && !MISE.cam_on && !CR.on && !vol_permanent() && !carte_plein()) {   /* carte du menu : eng_set_zoom la zoomerait */
        float z = E.zoom + (E.zoom_cible - E.zoom) * fminf(1, dt * 3 / ZOOM_FONDU);
        if (fabsf(z - E.zoom_cible) < 0.005f) z = E.zoom_cible;
        eng_set_zoom(z);
    }
}
/* Passants à l'abri : un flâneur dont le nom est tiré (générique ou métier ; ni meneur ni suiveur d'une patrouille, ni
   starter, ni conducteur en fuite) se met à l'abri si sa graine le place sous ABRI_PART × l'intensité ; il ne disparaît
   ni ne revient sous les yeux de Sera : son état ne change qu'à plus de ABRI_LOIN cases d'elle (abriMaj de l'émulateur) */
#define ABRI_PART 0.85f
#define ABRI_LOIN 12.0f
static void abri_maj(npc_t *n, float cendre, float nuit) {
    const float dx = n->x - E.sera.x, dz = n->z - E.sera.z, loin = ABRI_LOIN / E.w.ech;
    if (dx * dx + dz * dz < loin * loin) return;
    const perso_t *pe = &E.w.persos[n->perso];
    const int passant = (pe->flags & PERS_FLANE) && PERS_GENRE(pe->flags) && !n->meneur && !n->chef && !n->depart && !n->fuit;
    n->abri = passant && (((uint32_t)n->graine * 2246822519u) >> 16) % 100 < fmaxf(cendre * ABRI_PART, nuit) * 100;   /* île : la nuit, ils rentrent (ile_nuit_part) */
}

/* Exemplaires proches de Sera : l'interlocuteur la regarde ; ceux qui flânent marchent vers un point tiré autour de
   leur place, s'arrêtent quelques secondes et repartent (comme l'émulateur) ; les promeneurs suivent les trottoirs
   (prom_cible) ; les autres restent immobiles ; sous la cendre, des passants se mettent à l'abri (abri_maj) */
static void update_npcs(float dt) {
    const float act = 24.0f / E.w.ech, cendre = cendre_dehors(), nuit = ile_nuit_part();
    for (int k = 0; k < E.nnpc; k++) {
        npc_t *n = &E.npcs[k];
        const float ax = fabsf(n->x - E.sera.x), az = fabsf(n->z - E.sera.z);
        if (!n->place_ok && !E.in.on && ax < 2 * act && az < 2 * act) hors_chaussee(n);   /* pas posé sur la chaussée (avant d'être vu) */
        if (ax > act || az > act) continue;
        if (cendre > 0 || nuit > 0 || n->abri) abri_maj(n, cendre, nuit);
        if (!npc_present(n)) continue;
        const perso_t *pe = &E.w.persos[n->perso];
        if (pe->flags & PERS_VEHICULE) continue;
        if (PERS_EST_OBJET(pe->flags)) continue;   /* objet inanimé (épave) : il ne se tourne pas vers Sera, ne bouge pas */
        if (k == E.talk) { n->dir = atan2f(E.sera.x - n->x, E.sera.z - n->z); n->frame = 0; n->walking = 0; n->wait = 2; continue; }
        if (n->chef && n->chef <= E.nnpc) { suivre_chef(n, dt); continue; }
        if (n->fuit) {   /* conducteur éjecté : il court (2,6 m/s à l'échelle) vers son point de fuite, puis redevient passant */
            const float fx = n->tx - n->x, fz = n->tz - n->z, fd = hypotf(fx, fz);
            float sp = 2.6f / E.w.ech * dt;
            if (sp > fd) sp = fd;
            if (fd < 0.1f || !walkable_for(n->x + fx / fd * sp, n->z + fz / fd * sp, n, 0)) { n->fuit = 0; n->walking = 0; n->hx = n->x; n->hz = n->z; n->wait = 1 + frand() * 2; n->place_ok = 0; continue; }   /* sa place : hors de la chaussée (hors_chaussee) */
            n->x += fx / fd * sp; n->z += fz / fd * sp; n->dir = atan2f(fx, fz); n->anim += dt;
            if (n->anim > 0.1f) { n->anim = 0; n->frame++; }
            continue;
        }
        if (!(pe->flags & PERS_FLANE)) continue;
        if (!n->walking) {
            n->anim += dt;
            if (n->anim > 0.22f) { n->anim = 0; n->frame++; }
            n->wait -= dt;
            if (n->wait <= 0) {
                if (promeneur(n)) { if (!prom_cible(n)) n->wait = 0.5f + frand(); }
                else {
                    float a = frand() * 2 * PI_F, r = (0.6f + frand() * 1.6f) / E.w.ech;
                    n->tx = n->hx + cosf(a) * r; n->tz = n->hz + sinf(a) * r; n->walking = 1;
                    if (chaussee_at(n->x, n->z)) { n->tx = n->hx; n->tz = n->hz; }   /* sur la chaussée : il revient à sa place */
                    else if (chaussee_at(n->tx, n->tz) || chaussee_at((n->x + n->tx) / 2, (n->z + n->tz) / 2)) { n->walking = 0; n->wait = 1 + frand() * 2; }   /* pas de flânerie sur la chaussée, ni à travers */
                }
            }
            continue;
        }
        float dx = n->tx - n->x, dz = n->tz - n->z, d = hypotf(dx, dz);
        /* Arrivé : un promeneur repart presque aussitôt (parfois une pause), un flâneur attend 2 à 7 s */
        if (d < 0.08f) { n->walking = 0; n->wait = promeneur(n) ? (frand() < 0.15f ? 3 + frand() * 4 : frand() * 0.6f) : 2 + frand() * 5; continue; }
        float sp = 1.3f / E.w.ech * dt;
        if (sp > d) sp = d;
        float nx = n->x + dx / d * sp, nz = n->z + dz / d * sp;
        /* sur la chaussée (une traversée), il finit de traverser même gêné par quelqu'un : pas d'arrêt au milieu */
        if (walkable_for(nx, nz, n, 0) || (chaussee_at(n->x, n->z) && sol_libre(nx, nz, 0))) {
            n->x = nx; n->z = nz; n->dir = atan2f(dx, dz); n->anim += dt;
            if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
        } else { n->walking = 0; n->wait = promeneur(n) ? 0.3f + frand() : 1 + frand() * 2; }
    }
}

/* Éclairs (01/11/2026, demande de l'utilisateur) : sous une forte pluie (au moins ECLAIR_PLUIE d'intensité), dehors,
   sur la planète, hors combat et hors neige, un éclair toutes les ECLAIR_MIN à ECLAIR_MIN + ECLAIR_ECART secondes :
   voile de lumière blanche dessiné par hud.c (draw_eclair, d'après eclair_age), tonnerre (le son de la foudre, grave)
   de ECLAIR_SON à 4 × ECLAIR_SON s après (eclairMaj de l'émulateur) */
#define ECLAIR_PLUIE 0.45f
#define ECLAIR_MIN 7.0f
#define ECLAIR_ECART 18.0f
#define ECLAIR_SON 0.4f
static float eclair_t = -1, eclair_prochain = ECLAIR_MIN, tonnerre_t = -1;
float eclair_age(void) { return eclair_t; }
static void eclair_maj(float dt) {
    if (eclair_t >= 0 && (eclair_t += dt) > 1) eclair_t = -1;
    if (tonnerre_t >= 0 && (tonnerre_t -= dt) < 0) son_jouer(SON_FOUDRE, 0.55f, 0.45f);
    if (E.rain < ECLAIR_PLUIE || E.w.ciel || E.w.carte || E.in.on || BT.on || neige_ici()) return;
    if ((eclair_prochain -= dt) > 0) return;
    eclair_prochain = ECLAIR_MIN + frand() * ECLAIR_ECART;
    eclair_t = 0;
    tonnerre_t = ECLAIR_SON * (1 + frand() * 3);
}

/* Boîte de nuit (E.in.club, 03/11/2026, demande de l'utilisateur : « des effets de lumière, comme les éclairs ») : un
   éclair de couleur toutes les CLUB_MIN à CLUB_MIN + CLUB_ECART secondes, parfois une salve de deux ou trois à
   CLUB_SALVE d'écart, chacun d'une autre couleur que le précédent ; dessinés par hud.c (draw_club, d'après club_age) ;
   clubMaj de l'émulateur */
#define CLUB_MIN 0.35f
#define CLUB_ECART 1.1f
#define CLUB_SALVE 0.12f
static float club_t = -1, club_prochain = CLUB_MIN;
static int club_salve, club_c;
float club_age(int *couleur) { *couleur = club_c; return club_t; }
static void club_maj(float dt) {
    if (club_t >= 0 && (club_t += dt) > 0.3f) club_t = -1;
    if (!E.in.on || !E.in.club || BT.on) { club_t = -1; return; }
    if ((club_prochain -= dt) > 0) return;
    club_t = 0; club_c = (club_c + 1 + (int)(frand() * 3)) % 4;
    if (club_salve > 0) club_salve--;
    else club_salve = frand() < 0.35f ? 1 + (int)(frand() * 2) : 0;
    club_prochain = club_salve > 0 ? CLUB_SALVE : CLUB_MIN + frand() * CLUB_ECART;
}

/* Météo : en automatique, des phases de 40 à 120 s, pluvieuses avec la probabilité rain_freq ; l'intensité suit la
   cible en RAIN_FADE secondes (la pluie s'installe et s'arrête en douceur) */
#define RAIN_FADE 6.0f
static void weather_update(float dt) {
    eclair_maj(dt);
    club_maj(dt);
    const world_t *w = &E.w;
    if (E.weather == 0) {
        E.phase_left -= dt;
        if (E.phase_left <= 0) {
            E.raining = frand() * 100 < w->rain_freq;
            E.phase_left = 40 + frand() * 80;
        }
    }
    float target = w->ciel || E.weather == 1 ? 0 : E.weather == 2 || E.raining ? w->rain_int / 100.0f : 0;   /* souterrain, espace : jamais */
    float step = dt / RAIN_FADE;
    if (E.rain < target) { E.rain += step; if (E.rain > target) E.rain = target; }
    else if (E.rain > target) { E.rain -= step; if (E.rain < target) E.rain = target; }
}

/* Pluie de cendre sur l'équipe (voir plus haut) ; cendre_dit : annonces faites (une fois par lancement : bit 0 la toux,
   bit 1 le masque), cendre_averse : messages de l'averse en cours (bit 0 le premier coup, bit 1 le plancher) */
static int son_moteur(int p);
static int masque_filtre(void) {
    static const char *const NOMS[2] = { "MASQUE À FILTRE", "FILTRE AMÉLIORÉ" };   /* l'un ou l'autre protège */
    static int k[2] = { -1, -1 };   /* rangs des objets dans ITEMS, cherchés tant qu'ils ne sont pas trouvés (nouveau scénario) */
    for (int m = 0; m < 2; m++) {
        if (k[m] >= n_items || (k[m] >= 0 && (!ITEMS[k[m]].name || strcmp(ITEMS[k[m]].name, NOMS[m])))) k[m] = -1;
        for (int i = 0; k[m] < 0 && i < n_items; i++) if (ITEMS[i].name && !strcmp(ITEMS[i].name, NOMS[m])) k[m] = i;
        if (k[m] >= 0 && ITEMS[k[m]].count > 0) return 1;
    }
    return 0;
}
static uint8_t cendre_dit, cendre_averse;
static void cendre_update(float dt) {
    if (E.w.ile[0] || E.w.ciel || E.rain < CENDRE_SEUIL || neige_ici()) { cendre_averse = 0; E.cendre_t = 0; return; }   /* temps sec, sous terre, ailleurs, neige ; île : une pluie d'eau (ni annonce ni toux, 11/11/2026 ; cendreMaj de l'émulateur de l'île) */
    if (E.in.on || E.ui_block || script_running() || CR.on) return;   /* à l'abri, ou le temps de la cendre arrêté */
    if (E.ride >= 0 && son_moteur(E.npcs[E.ride].perso) != SON_MOTEUR_MOTO) return;   /* habitacle fermé */
    if (masque_filtre()) {
        if (!(cendre_dit & 2)) { cendre_dit |= 2; cendre_averse |= 1; hud_annonce("PLUIE DE CENDRE", "Le masque à filtre protège", "Les machines sortent la ramasser"); }
        return;
    }
    if (!(cendre_dit & 1)) { cendre_dit |= 1; cendre_averse |= 1; hud_annonce("PLUIE DE CENDRE", "La cendre brûle la gorge", "S'abriter, ou MASQUE À FILTRE"); }
    E.cendre_t += dt;
    if (E.cendre_t < CENDRE_PAS) return;
    E.cendre_t -= CENDRE_PAS;
    int membres = 0, bas = 0, touche = 0;
    for (int m = 0; m < NMEMB; m++) {
        member_t *p = &PARTY[m];
        if (!in_team(m) || p->max <= 0) continue;
        const int plancher = p->max * CENDRE_PLANCHER > 1 ? (int)(p->max * CENDRE_PLANCHER) : 1;
        membres++;
        if (p->hp > plancher) {
            const int perte = (int)ceilf(p->max * CENDRE_PV * E.rain);
            p->hp = p->hp - perte > plancher ? p->hp - perte : plancher; touche = 1;
        }
        if (p->hp <= plancher) bas++;
    }
    if (touche) {
        E.cendre_coup = E.clock;
        if (!(cendre_averse & 1)) { cendre_averse |= 1; hud_toast("L'équipe tousse sous la cendre"); }
    }
    if (membres && bas == membres && !(cendre_averse & 2)) { cendre_averse |= 2; hud_toast("La cendre épuise l'équipe : à l'abri !"); }
}

/* ---------- Rencontres (encounterStep de l'émulateur) ---------- */
/* Le terrain sous Sera (ZONE, par bloc de 2 × 2 cases) choisit les ennemis possibles : ceux dont le masque le contient,
   tirés selon leur poids */
/* Ennemi possible ici : son niveau de fiche ne dépasse pas celui de l'histoire de plus de RENC_ECART (au moins RENC_MIN :
   au prologue, Glaneurs, Brouteurs, Pillardes), et une élite (sorts, ou plus de ELITE_PV PV par niveau de fiche) ne vient
   qu'avec ELITE_EQUIPE membres au moins (encOk de l'émulateur) */
#define RENC_ECART 2
#define RENC_MIN 5
#define ELITE_PV 600
#define ELITE_EQUIPE 3
static int enc_ok(const enemy_t *en, int histoire, int membres) {
    const int plafond = histoire + RENC_ECART > RENC_MIN ? histoire + RENC_ECART : RENC_MIN;
    if (histoire > 0 && en->lvl > plafond) return 0;
    if ((en->sorts || en->hp > ELITE_PV * (en->lvl + 6)) && membres < ELITE_EQUIPE) return 0;
    return 1;
}
/* Un terrain qui a des ennemis n'est jamais vide (06/11/2026, retour de jeu : les fiches les plus faibles passées au niveau 8, plus une seule
   rencontre tant que le niveau de l'histoire restait sous 6, tout le prologue) : personne sous le plafond, il monte jusqu'à l'ennemi le moins
   fort du terrain, ramené au niveau du combat comme les autres (encounter_level ; encHistoire de l'émulateur, table de monde.js) */
static int enc_histoire(int t, int histoire, int membres) {
    const world_t *w = &E.w;
    if (histoire <= 0) return histoire;
    int bas = -1;
    for (int k = 0; k < w->nenemies; k++)
        if ((w->enemies[k].zones >> t) & 1 && enc_ok(&w->enemies[k], 0, membres) && (bas < 0 || w->enemies[k].lvl < bas)) bas = w->enemies[k].lvl;
    return bas - RENC_ECART > histoire ? bas - RENC_ECART : histoire;
}
/* Poids d'un ennemi au tirage : sous la pluie de cendre, les Glaneurs sortent la ramasser (poidsRenc de l'émulateur) */
static float enc_poids(const enemy_t *en, float cendre) {
    const char *nom = en->perso >= 0 && en->perso < E.w.npersos ? E.w.persos[en->perso].nom : NULL;
    return en->poids * (cendre > 0 && nom && strstr(nom, "GLANEUR") ? 1 + CENDRE_GLANEUR * cendre : 1);
}
/* Terrain des rencontres sous (x, z) (rang de Continent.LAND), -1 : aucun. Bloc de la zone à l'échelle de la carte : 2 × 2
   cases d'ordinaire, 64 × 64 sur le système solaire (zoneOctets de l'émulateur) */
int encounter_zone(float x, float z) {
    const world_t *w = &E.w;
    if (!w->zone || !w->nenemies || w->cartes[w->carte].vision) return -1;   /* vision (la ville d'avant la Cendre) : aucun ennemi (zoneRenc de l'émulateur) */
    const int i = (int)floorf(x) * w->zone_n / (w->n > 0 ? w->n : 1), j = (int)floorf(z) * w->zone_n / (w->n > 0 ? w->n : 1);
    if (i < 0 || j < 0 || i >= w->zone_n || j >= w->zone_n) return -1;
    const int t = w->zone[j * w->zone_n + i];
    if (t == LAND_MONTAGNE && w->ciel == 1) return LAND_CAVERNE;   /* sous terre, la roche des parois (praticable) : la caverne (08/11/2026 ; sinon les oiseaux des montagnes de la planète) */
    return t >= 32 ? -1 : t;   /* 255 : terrain inconnu */
}
static int membres_equipe(void) { return equipe_nombre(); }
int encounter_pick(void) { return encounter_pick_at(E.sera.x, E.sera.z); }
int encounter_pick_at(float x, float z) {
    const world_t *w = &E.w;
    const int t = encounter_zone(x, z);
    if (t < 0) return -1;
    int membres = membres_equipe(), last = -1;
    const int histoire = enc_histoire(t, script_niveau(x, z), membres);
    const float cendre = cendre_dehors();
    float total = 0;
    for (int k = 0; k < w->nenemies; k++) if ((w->enemies[k].zones >> t) & 1 && enc_ok(&w->enemies[k], histoire, membres)) total += enc_poids(&w->enemies[k], cendre);
    if (total <= 0) return -1;
    float r = erand() * total;
    for (int k = 0; k < w->nenemies; k++) {
        if (!((w->enemies[k].zones >> t) & 1) || !enc_ok(&w->enemies[k], histoire, membres)) continue;
        last = k; r -= enc_poids(&w->enemies[k], cendre);
        if (r <= 0) return k;
    }
    return last;
}
/* Groupes de rencontre (encounterTaille, encounterGroupe de l'émulateur, à garder identiques). Taille : seul tant que le
   niveau de l'histoire (sinon celui de l'équipe) est sous GROUPE_NIV, avec Sera seule, ou pour une élite (sorts, PV d'un
   boss de zone) ; ensuite un groupe de deux avec la chance (niveau - GROUPE_NIV) / GROUPE_PENTE (GROUPE_P2 au plus), et
   de trois (trois membres au moins) pour une part de ces groupes, (niveau - GROUPE_NIV3) / GROUPE_PENTE3 (GROUPE_P3 au
   plus) : les groupes viennent plus souvent à mesure que l'histoire avance. La fiche peut borner la taille (ENNE : 1 seul).
   Compagnons : le même ennemi que le chef (GROUPE_MEME), sinon un autre de la zone, de même nature (machine ou vivant),
   possible ici et pas une élite, au poids */
#define GROUPE_NIV 8
#define GROUPE_PENTE 40.0f
#define GROUPE_P2 0.5f
#define GROUPE_NIV3 20
#define GROUPE_PENTE3 50.0f
#define GROUPE_P3 0.5f
#define GROUPE_MEME 0.6f
static int enc_elite(const enemy_t *en) { return en->sorts || en->hp > ELITE_PV * (en->lvl + 6); }
int encounter_taille(int k, int histoire, int membres) {
    const enemy_t *en = &E.w.enemies[k];
    const int h = histoire > 0 ? histoire : E.level;
    if (h < GROUPE_NIV || membres < 2 || enc_elite(en) || en->groupe == 1) return 1;
    int n = erand() < fminf(GROUPE_P2, (h - GROUPE_NIV) / GROUPE_PENTE) ? 2 : 1;
    if (n == 2 && membres >= 3 && erand() < fminf(GROUPE_P3, (h - GROUPE_NIV3) / GROUPE_PENTE3)) n = 3;
    return en->groupe > 1 && n > en->groupe ? en->groupe : n;
}
int encounter_groupe(int k, float x, float z, int n, int *out) {
    const world_t *w = &E.w;
    const int t = encounter_zone(x, z), membres = membres_equipe(), histoire = t >= 0 ? enc_histoire(t, script_niveau(x, z), membres) : script_niveau(x, z);
    const float cendre = cendre_dehors();
    if (n > BT_EMAX) n = BT_EMAX;
    if (n < 1) n = 1;
    out[0] = k;
    for (int i = 1; i < n; i++) {
        int c = k;
        if (t >= 0 && erand() >= GROUPE_MEME) {
            float tot = 0;
#define COMPAGNON(q) ((q) != k && ((w->enemies[q].zones >> t) & 1) && enc_ok(&w->enemies[q], histoire, membres) && !enc_elite(&w->enemies[q]) \
                      && w->enemies[q].coeur == w->enemies[k].coeur && w->enemies[q].groupe != 1)
            for (int q = 0; q < w->nenemies; q++) if (COMPAGNON(q)) tot += enc_poids(&w->enemies[q], cendre);
            float r = erand() * tot;
            for (int q = 0; tot > 0 && q < w->nenemies; q++)
                if (COMPAGNON(q)) { c = q; r -= enc_poids(&w->enemies[q], cendre); if (r <= 0) break; }
#undef COMPAGNON
        }
        out[i] = c;
    }
    return n;
}
int sol_marchable(float x, float z) { return sol_libre(x, z, 0); }

/* Une rencontre en moyenne tous les RENC unités de marche (loi exponentielle, distance × ech comme l'émulateur) ;
   jamais en véhicule, ni sur un terrain sans ennemi (la distance est alors retirée au pas suivant). Son niveau suit
   l'équipe (niveau + 1, moins 0 à 2) sans dépasser celui de l'histoire à cet endroit (script_niveau) ; sans niveau
   dans le scénario, celui de la fiche */
int encounter_level(void) {
    const int n = script_niveau(E.sera.x, E.sera.z);
    if (n <= 0) return 0;
    const int l = (n < E.level + 1 ? n : E.level + 1) - (int)(erand() * 3);
    return l < 1 ? 1 : l;
}
/* Réglage RENCONTRES du menu SYSTÈME : distance moyenne du paquet × RENC_FOIS (0 : aucune) ; la distance tirée vaut au moins
   la moitié de la moyenne (un peu de liberté après chaque combat) ; sous la pluie de cendre, les machines sont de sortie :
   la marche compte 1 + CENDRE_RENC × l'intensité fois plus ; encounterStep de l'émulateur. Deux fois plus de rencontres
   depuis le 03/11/2026 (demande de l'utilisateur, pour rééquilibrer l'aventure ; avant : 3, 6, 12, 0, 1,5), et encore le
   04/11/2026 (demande de l'utilisateur : le réglage le plus fort, 0,75, devient NORMALES, celui par défaut, et FRÉQUENTES un
   seuil au-dessus ; mêmes rangs, les sauvegardes gardent leur nom de réglage ; avant : 1,5, 3, 6, 0, 0,75) */
/* 06/11/2026, demande de l'utilisateur (on marche beaucoup plus depuis « Le prix du passage » : trop de combats) : NORMALES prend la valeur de RARES ;
   FRÉQUENTES, celle d'avant */
static const float RENC_FOIS[5] = { 1.5f, 3, 6, 0, 0.75f };
static float enc_left;
static void encounter_step(float dist) {
    const world_t *w = &E.w;
    const float pas = w->renc * RENC_FOIS[E.renc_mode % 5];
    if (pas <= 0 || !w->zone || (E.ride >= 0 && !vol_permanent()) || BT.on || E.ui_block || script_running()) return;   /* en véhicule, jamais, sauf en vol permanent */
    if (zone_sure(E.sera.x, E.sera.z)) return;   /* zone sûre (battle.c) : le chemin n'y compte pas */
    if (ile_calme()) return;   /* île : pas encore de monstres (prologue, ile.c) */
    if (enc_left <= 0) enc_left = pas * (0.5f - 0.5f * logf(1 - erand()));
    enc_left -= dist * (1 + CENDRE_RENC * cendre_dehors());
    if (enc_left > 0) return;
    enc_left = 0;
    /* À pied : les ennemis se voient (06/11/2026, demande de l'utilisateur) : un groupe paraît devant elle, hors de l'écran, et rôde (patrouille_poser de
       battle.c). En vol permanent : pas de sprites dans le vide, le combat tout de suite, comme avant */
    if (!vol_permanent()) { if (patrouille_poser(0) < 0) enc_left = PATR_REESSAI; return; }   /* pas de place devant elle : un peu plus loin (08/11/2026) */
    const int k = encounter_pick();
    if (k < 0) return;
    int g[BT_EMAX];   /* le chef et ses compagnons (groupe de rencontre) */
    const int n = encounter_groupe(k, E.sera.x, E.sera.z, encounter_taille(k, script_niveau(E.sera.x, E.sera.z), membres_equipe()), g);
    battle_start_groupe(g, n, encounter_level(), 0, GROUPE_PARTS, NULL);
}

/* État des rencontres, à la demande (SDX ETAT du firmware, rendu_test etat ; 08/11/2026, retour de jeu : aucune rencontre sur la montre alors
   que le PC en trouvait) : tout ce qui décide d'une rencontre à pied, sur une ligne */
int eng_etat_rencontres(char *b, int n) {
    const world_t *w = &E.w;
    const int t = encounter_zone(E.sera.x, E.sera.z), mb = membres_equipe(), sn = script_niveau(E.sera.x, E.sera.z);
    const int hi = t >= 0 ? enc_histoire(t, sn, mb) : sn;
    int cand = 0, zon = 0, pat = 0;
    for (int k = 0; k < w->nenemies && t >= 0; k++) if ((w->enemies[k].zones >> t) & 1) { zon++; cand += enc_ok(&w->enemies[k], hi, mb); }
    int o = snprintf(b, (size_t)n, "carte %d en %.1f %.1f cap %.2f yaw %.2f | vehicule %d nage %d interieur %d combat %d course %d script %d scene %d bloque %d pause %d vol %d"
                     " | mode %d pas %.1f reste %.1f sure %d zone %d | niveau %d histoire %d (plafond %d) equipe %d | ennemis de la zone %d, permis %d",
                     w->carte, E.sera.x, E.sera.z, E.sera.dir, E.yaw, E.ride >= 0, E.nage, E.in.on, BT.on, CR.on, script_running(), mise_bloque(), E.ui_block, hud_pause(), vol_permanent(),
                     E.renc_mode, w->renc * RENC_FOIS[E.renc_mode % 5], enc_left, zone_sure(E.sera.x, E.sera.z), t, E.level, sn, hi, mb, zon, cand);
    for (int s = 0; s < PATR_MAX; s++) if (PATR[s].on && o > 0 && o < n) { pat++; o += snprintf(b + o, (size_t)(n - o), " | groupe %d : etat %d a %.1f", s, PATR[s].etat, hypotf(PATR[s].x[0] - E.sera.x, PATR[s].z[0] - E.sera.z)); }
    if (!pat && o > 0 && o < n) snprintf(b + o, (size_t)(n - o), " | aucun groupe");
    return o;
}

/* Passages (PASS) : Sera entre dans la zone d'un passage de sa carte (à pied, hors combat, script et intérieur), condition
   remplie : elle passe sur la carte d'arrivée. Rien ne repart avant qu'elle sorte de toute zone de passage. */
static void passage_step(void) {
    const world_t *w = &E.w;
    if (!w->npass || BT.on || E.in.on || (E.ride >= 0 && !vol_permanent()) || CR.on || script_running() || E.ui_block) return;   /* en vol permanent : à bord */
    const passage_t *p = NULL;
    for (int k = 0; k < w->npass && !p; k++) {
        const passage_t *q = &w->pass[k];
        if (q->carte != w->carte || hypotf(E.sera.x - q->x, E.sera.z - q->z) >= q->r) continue;
        if (q->cond < SC_MAX_FLAGS && !((E.flags[q->cond >> 3] >> (q->cond & 7)) & 1)) continue;
        p = q;
    }
    if (!p) { E.pass_arme = 1; return; }
    if (!E.pass_arme || world_carte(p->vers, p->ax, p->az)) return;
    BT.fade_t = E.clock;   /* sortie du noir, comme après un combat */
    son_jouer(SON_PORTIERE, 0.6f, 0.8f);
}

/* Cycle de palette de l'eau : décalage des 8 teintes toutes les 0,14 s */
static void water_cycle(float dt) {
    E.pal_t += dt;
    if (E.pal_t < 0.14f) return;
    E.pal_t = 0;
    world_t *w = &E.w;
    uint8_t first[3] = { w->pal[w->water[0]][0], w->pal[w->water[0]][1], w->pal[w->water[0]][2] };
    for (int k = 0; k < 7; k++) for (int c = 0; c < 3; c++) w->pal[w->water[k]][c] = w->pal[w->water[k + 1]][c];
    for (int c = 0; c < 3; c++) w->pal[w->water[7]][c] = first[c];
    render_palette_changed(w->water, 8);
}

/* Sons de l'image : moteur du véhicule conduit (hauteur et volume selon sa vitesse), moteurs des véhicules du trafic
   à moins de 9 cases (plus faibles avec la distance), tôle qui frotte (E.frotte, image précédente) ; en combat, aucun.
   Les boucles non redemandées s'éteignent. */
static int son_moteur(int p) {
    if (p >= 0 && p < MAX_PERSOS && E.w.son_moteur[p] < SON_N) return E.w.son_moteur[p];   /* choisi dans l'atelier */
    const int t = p >= 0 && p < E.w.npersos ? VEH_TYPE(E.w.persos[p].flags) : VEH_VOITURE;
    return SON_MOTEUR_VOITURE + (t >= VEH_VOITURE && t <= VEH_VAISSEAU ? t - VEH_VOITURE : 0);
}
static void sons_update(void) {
    if (!BT.on && !hud_pause()) {   /* en pause : moteurs éteints */
        float vmax, acc;
        if (E.ride >= 0) {
            const int p = E.npcs[E.ride].perso;
            veh_params(p, &vmax, &acc);
            const float r = fminf(1, fabsf(E.ride_v) / vmax);
            son_boucle(CLE_VEHICULE, son_moteur(p), 0.45f + 0.4f * r, 0.75f + 0.8f * r);
        }
        for (int k = 0; k < TRAFFIC_MAX; k++) {
            const traffic_t *t = &E.traffic[k];
            const float d = t->on ? hypotf(t->x - E.sera.x, t->z - E.sera.z) * E.w.ech : 99;
            if (d >= 9) continue;
            veh_params(t->perso, &vmax, &acc);
            son_boucle(CLE_TRAFIC + k, son_moteur(t->perso), 0.35f * (1 - d / 9), 0.7f + 0.7f * fminf(1, t->v / vmax));
        }
        for (int k = 0; CR.on && k < CR.n; k++) {   /* adversaires de la course à moins de 12 cases */
            const racer_t *r = &CR.r[k];
            const float d = hypotf(r->x - E.sera.x, r->z - E.sera.z);
            if (r->joueur || d >= 12) continue;
            son_boucle(CLE_COURSE + k, son_moteur(r->perso), 0.32f * (1 - d / 12), 0.7f + 0.8f * fminf(1, fabsf(r->v) / r->vmax));
        }
        if (E.frotte > 0) son_boucle(CLE_FROTTEMENT, SON_FROTTEMENT, 0.2f + 0.4f * E.frotte, 0.8f + 0.5f * E.frotte);
    }
    E.frotte = 0;   /* redemandé à chaque image par la conduite ou la course */
    audio_fin_image();
}

/* ---------- Mise en scène du scénario (mise de l'émulateur) ---------- */
mise_t MISE ENG_FROID;
static float ecart_angle(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
int acteur_de(int perso) {
    for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].on && !MISE.act[k].chasse && MISE.act[k].perso == perso) return k;
    return -1;
}
/* Acteur du personnage perso en (x, z) : le sien s'il est déjà là, sinon une place libre ; -1 sans planche ou sans place */
static int cam_dedans(float x, float z, float yaw, float zoom, float h);
/* Place libre pour un personnage de scène (perso : le sien, acteur : sa place dans MISE.act, -1 : Sera) : ni obstacle, ni un autre personnage de la scène, ni un passant ; le monde
   pas encore chargé : on ne sait pas, libre. sceneLibre de l'émulateur */
static int scene_libre_r(float x, float z, int perso, int acteur, float R) {
    if (E.in.on) return int_libre(x, z);
    if (x < 2 || z < 2 || x > E.w.n - 2 || z > E.w.n - 2 || !chunk_of_cell((int)floorf(x), (int)floorf(z))) return 1;
    /* un acteur bateau (11/11/2026, île : traversées montrées) va sur l'eau, jamais sur la terre (sceneLibre de l'émulateur) */
    const int bateau = perso >= 0 && perso < E.w.npersos && (E.w.persos[perso].flags & PERS_VEHICULE) && VEH_TYPE(E.w.persos[perso].flags) == VEH_BATEAU;
    if (!sol_libre(x, z, bateau ? 2 : 0)) return 0;
    if (acteur >= 0 && E.ride < 0 && hypotf(x - E.sera.x, z - E.sera.z) < R) return 0;
    for (int k = 0; k < ACT_MAX; k++) {
        const acteur_t *a = &MISE.act[k];
        if (k == acteur || !a->on || a->chasse) continue;
        if (hypotf(x - a->x, z - a->z) < R || (a->walking && hypotf(x - a->tx, z - a->tz) < R)) return 0;   /* (ni là où il va) */
    }
    for (int k = 0; k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (n->perso == perso || fabsf(n->x - x) >= R || fabsf(n->z - z) >= R || !npc_present(n)) continue;
        if (hypotf(n->x - x, n->z - z) < R) return 0;
    }
    return 1;
}
static int scene_libre(float x, float z, int perso, int acteur) { return scene_libre_r(x, z, perso, acteur, SCENE_R); }
/* La place libre la plus proche de (x, z), à rmax cases au plus ; 0 : aucune, rien ne change */
static int scene_place(float *x, float *z, float rmax, int perso, int acteur) {
    for (float r = 0; r <= rmax; r += 0.5f)
        for (int a = 0; a < (r > 0 ? 16 : 1); a++) {
            const float qx = *x + cosf(a * PI_F / 8) * r, qz = *z + sinf(a * PI_F / 8) * r;
            if (scene_libre_r(qx, qz, perso, acteur, SCENE_POSE_R)) { *x = qx; *z = qz; return 1; }
        }
    return 0;
}
/* Un pas vers (tx, tz) en contournant ce qui gêne : tout droit, sinon de plus en plus de biais, du côté déjà pris d'abord ; 1 : arrivé, ou trop long (lim), il s'arrête où il est.
   pasEvite de l'émulateur */
static int pas_evite(float *x, float *z, float *dir, float tx, float tz, float sp, int perso, int acteur, int *cote, float *t, float lim, float dt) {
    static const float BIAIS[4] = { 0, 0.6f, 1.2f, 1.8f };
    const float dx = tx - *x, dz = tz - *z, d = hypotf(dx, dz);
    if (d <= sp || d < 0.02f) { *x = tx; *z = tz; return 1; }
    if ((*t += dt) > lim) return 1;
    const float base = atan2f(dx, dz);
    if (!*cote) *cote = 1;
    for (int i = 0; i < 4; i++) for (int s = 0; s < (i ? 2 : 1); s++) {
        const int c = s ? -*cote : *cote;
        const float a = base + c * BIAIS[i], nx = *x + sinf(a) * sp, nz = *z + cosf(a) * sp;
        if (!scene_libre(nx, nz, perso, acteur)) continue;
        *x = nx; *z = nz; *dir = a;
        if (i) *cote = c;
        return 0;
    }
    *dir = base;
    return 0;
}
/* Temps au bout duquel une marche renonce : le double du trajet tout droit, et une seconde */
static float marche_limite(float x, float z, float tx, float tz, float v) { return hypotf(tx - x, tz - z) / (MARCHE_MS * fmaxf(0.1f, v) / E.w.ech) * 2 + 1; }
void mise_marcher_acteur(int k, float x, float z, float v) {
    acteur_t *a = &MISE.act[k];
    if (!scene_place(&x, &z, 4, a->perso, k)) { x = a->x; z = a->z; }
    a->tx = x; a->tz = z; a->v = v; a->walking = 1; a->court = v > 1.5f; a->marche_t = 0; a->marche_lim = marche_limite(a->x, a->z, x, z, v);
    if (!chunk_of_cell((int)floorf(x), (int)floorf(z))) a->place_att = 1;
}
void mise_marcher_sera(float x, float z, float v) {
    if (!scene_place(&x, &z, 4, -1, -1)) { x = E.sera.x; z = E.sera.z; }
    MISE.sera_on = 1; MISE.sera_tx = x; MISE.sera_tz = z; MISE.sera_v = v; MISE.sera_mt = 0; MISE.sera_cote = 1;
    MISE.sera_lim = marche_limite(E.sera.x, E.sera.z, x, z, v);
}
/* Celui qui dit la réplique (Sera : celui à qui elle parle, l'acteur le plus proche, sinon son interlocuteur), à PARLE_R cases d'elle au plus ; npc : son rang s'il n'est pas un acteur */
#define SCENE_PARLE_R 20.0f
static int scene_parle_pos(const char *who, float *x, float *z, int *npc) {
    *npc = -1;
    if (!strcmp(who, E.w.sc_player)) {
        float bd = SCENE_PARLE_R; int ok = 0;
        for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].on && !MISE.act[k].chasse && MISE.act[k].carte == E.w.carte) { const float d = hypotf(MISE.act[k].x - E.sera.x, MISE.act[k].z - E.sera.z); if (d < bd) { bd = d; *x = MISE.act[k].x; *z = MISE.act[k].z; ok = 1; } }
        if (E.talk >= 0 && E.talk < E.nnpc) { const npc_t *n = &E.npcs[E.talk]; const float d = hypotf(n->x - E.sera.x, n->z - E.sera.z); if (d < bd) { *x = n->x; *z = n->z; *npc = E.talk; ok = 1; } }
        return ok;
    }
    for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].on && !MISE.act[k].chasse && MISE.act[k].carte == E.w.carte && MISE.act[k].perso < E.w.npersos && !strcmp(E.w.persos[MISE.act[k].perso].nom, who)) { *x = MISE.act[k].x; *z = MISE.act[k].z; return hypotf(*x - E.sera.x, *z - E.sera.z) < SCENE_PARLE_R; }
    if (E.talk >= 0 && E.talk < E.nnpc && !strcmp(E.w.persos[E.npcs[E.talk].perso].nom, who)) { *x = E.npcs[E.talk].x; *z = E.npcs[E.talk].z; *npc = E.talk; return 1; }
    float bd = SCENE_PARLE_R; int ok = 0;
    for (int q = 0; q < E.nnpc; q++) {
        const npc_t *n = &E.npcs[q];
        const float d = hypotf(n->x - E.sera.x, n->z - E.sera.z);
        if (d < bd && npc_present(n) && n->perso < E.w.npersos && !strcmp(E.w.persos[n->perso].nom, who)) { bd = d; *x = n->x; *z = n->z; *npc = q; ok = 1; }
    }
    return ok;
}
/* Personnes trop loin pour se parler (146 scènes de l'audit du 07/11/2026) : Sera va vers celui qui parle. Une voix (personne dans les parages) ne fait rien. sceneApproche de l'émulateur */
int scene_approche(const char *who) {
    float x, z; int npc;
    if (E.ride >= 0 || CR.on || BT.on || E.in.on || E.nage || MISE.sera_on || MISE.fondu >= 0.9f || !scene_parle_pos(who, &x, &z, &npc)) return 0;
    const float d = hypotf(x - E.sera.x, z - E.sera.z);
    if (d <= DLG_LOIN || (!strcmp(who, E.w.sc_player) && d > DLG_SERA_LOIN)) return 0;
    mise_marcher_sera(x + (E.sera.x - x) / d * DLG_PROCHE, z + (E.sera.z - z) / d * DLG_PROCHE, d > DLG_COURT ? 3 : 1.5f);
    return 1;
}
/* À chaque réplique (780 répliques de l'audit où Sera tournait le dos) : Sera se tourne vers celui qui parle, sinon vers celui à qui elle parle ; celui qui parle vers elle ; les autres acteurs vers
   celui qui parle ; une caméra qui suit Sera et serait dans un bâtiment en sort. sceneRegards de l'émulateur */
void scene_cam_sortir(void) {
    if (!MISE.cam_on && !E.in.on && !BT.on && !CR.on && E.ride < 0 && cam_dedans(E.sera.x, E.sera.z, E.yaw, E.zoom, 1.0f / E.w.ech)) mise_camera(0, E.sera.x, E.sera.z, E.yaw, 0, FIN_CAM);
}
void scene_regards(const char *who) {
    float x = 0, z = 0; int npc = -1;
    const int elle = !strcmp(who, E.w.sc_player);
    int ok = scene_parle_pos(who, &x, &z, &npc);
    if (!ok && scene_parle_pos(E.w.sc_player, &x, &z, &npc)) { ok = 1; who = E.w.sc_player; }   /* un membre de l'équipe, sans sprite, parle : Sera regarde qui est là */
    const float dse = hypotf(x - E.sera.x, z - E.sera.z);
    if (ok && dse > 0.05f && E.ride < 0 && !MISE.sera_on && !E.nage && !CR.on && E.sera.state != 3) E.sera.dir = atan2f(x - E.sera.x, z - E.sera.z);
    for (int k = 0; k < ACT_MAX; k++) {
        acteur_t *a = &MISE.act[k];
        if (!a->on || a->chasse || a->walking || a->carte != E.w.carte) continue;
        const int parle = !elle && a->perso < E.w.npersos && !strcmp(E.w.persos[a->perso].nom, who);
        const float tx = elle || parle || !ok ? E.sera.x : x, tz = elle || parle || !ok ? E.sera.z : z;
        if (hypotf(tx - a->x, tz - a->z) > 0.05f) a->dir = atan2f(tx - a->x, tz - a->z);
    }
    if (!elle && npc >= 0 && !PERS_EST_OBJET(E.w.persos[E.npcs[npc].perso].flags) && !(E.w.persos[E.npcs[npc].perso].flags & PERS_VEHICULE) && dse > 0.05f) E.npcs[npc].dir = atan2f(E.sera.x - x, E.sera.z - z);
    scene_cam_sortir();
}
int acteur_poser(int perso, float x, float z, float dir) {
    if (perso < 0 || perso >= E.w.npersos || E.w.persos[perso].sheet < 0) return -1;
    int s = acteur_de(perso);
    for (int k = 0; k < ACT_MAX && s < 0; k++) if (!MISE.act[k].on) s = k;
    if (s < 0) return -1;
    acteur_t *a = &MISE.act[s];
    memset(a, 0, sizeof *a);
    a->on = 1; a->perso = (uint8_t)perso; a->dir = dir; a->v = 1; a->carte = (uint8_t)E.w.carte; a->cote = 1;
    a->place_att = !chunk_of_cell((int)floorf(x), (int)floorf(z));
    { float px = x, pz = z; scene_place(&px, &pz, 4, perso, s); a->x = a->tx = px; a->z = a->tz = pz; }   /* jamais dans un obstacle ni dans quelqu'un */
    /* palette de l'exemplaire posé qu'il remplace (le plus proche à moins de PAL_VUE cases) : pas de changement de couleurs
       quand l'acteur prend sa place (acteurPoser de l'émulateur) */
    float bd = PAL_VUE;
    for (int j = 0; j < E.nnpc; j++) {
        const float d = hypotf(E.npcs[j].x - x, E.npcs[j].z - z);
        if (E.npcs[j].perso == perso && npc_present(&E.npcs[j]) && d < bd) { bd = d; a->var = E.npcs[j].var; }
    }
    return s;
}
int mise_marche(int qui) {
    if (qui == 255) return MISE.sera_on;
    if (qui >= 0) return qui < ACT_MAX && MISE.act[qui].on && MISE.act[qui].walking;
    if (MISE.sera_on) return 1;
    for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].on && !MISE.act[k].chasse && MISE.act[k].walking) return 1;
    return 0;
}
int mise_bloque(void) { return MISE.cam_on || MISE.sera_on || police_bloque() || ile_dort() || ile_offrande_en_cours(); }   /* île : la séquence d'une succube (ile.c) */   /* arrestation (police.c) aussi */
/* Lacet de la caméra au multiple de 45° le plus proche */
float cam_pas(float yaw) { const float p = PI_F / 4; return roundf(yaw / p) * p; }
/* Le point est-il dans la boîte d'un bâtiment (d'après ses polygones) ? Un bâtiment est rangé dans le chunk de son centre : les 3 × 3 autour du point. pointDansBat de l'émulateur */
int point_dans_bat(float x, float y, float z) {
    const int ci = (int)floorf(x), cj = (int)floorf(z);
    {   /* pas de mur ni de toit ici (une cour, un renfoncement, la boîte d'un bâtiment tourné) : non */
        const chunk_t *c0 = chunk_of_cell(ci, cj);
        int sol = 0;
        if (c0) for (uint32_t k = 0; k < c0->nsolid && !sol; k++) { const float *r = c0->solids + k * 4; sol = x > r[0] && x < r[2] && z > r[1] && z < r[3]; }
        if (!sol) return 0;
    }
    for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        const int i = ci + di * CS, j = cj + dj * CS;
        if (i < 0 || j < 0 || i >= E.w.n || j >= E.w.n) continue;
        const chunk_t *c = chunk_of_cell(i, j);
        if (!c) continue;
        for (uint32_t b = 0; b < c->nbld; b++) {
            const pak_building_t *pb = &c->bld[b];
            if (hypotf(x - pb->cx, z - pb->cz) > pb->r + 1) continue;
            float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, y1 = -1e9f;
            for (uint32_t m = 0; m < pb->count; m++) {
                const pak_poly_t *pp = chunk_poly(c, pb->first + m);
                for (int v = 0; v < pp->nv; v++) {
                    if (pp->v[v].x < x0) x0 = pp->v[v].x;
                    if (pp->v[v].x > x1) x1 = pp->v[v].x;
                    if (pp->v[v].z < z0) z0 = pp->v[v].z;
                    if (pp->v[v].z > z1) z1 = pp->v[v].z;
                    if (pp->v[v].y > y1) y1 = pp->v[v].y;
                }
            }
            if (x > x0 && x < x1 && z > z0 && z < z1 && y < y1) return 1;
        }
    }
    return 0;
}
/* Caméra de scène qui visant (x, z) au lacet et au zoom donnés serait dans un bâtiment (même calcul que setup_camera) */
static int cam_dedans(float x, float z, float yaw, float zoom, float h) {
    const float dist = 12.5f / E.w.ech / zoom, cp = cosf(E.pitch), sp = sinf(E.pitch);
    return point_dans_bat(x - sinf(yaw) * cp * dist, ground_y(x, z) + h + sp * dist, z - cosf(yaw) * cp * dist);
}
/* Plan qui tomberait dans un bâtiment : un peu plus près, sinon un autre angle, le plus proche possible de celui demandé, sinon plus près encore (camHorsBat de l'émulateur) */
static void cam_hors_bat(void) {
    float *b = MISE.cam_b;
    if (E.in.on || !cam_dedans(b[0], b[1], b[2], b[3], b[4])) return;
    for (float z = b[3] * 1.25f; z <= b[3] * 1.6f && z <= ZOOM_MAX; z *= 1.25f) if (!cam_dedans(b[0], b[1], b[2], z, b[4])) { b[3] = z; return; }
    for (int i = 1; i <= 4; i++) for (int s = -1; s <= 1; s += 2) {
        const float y = b[2] + s * i * PI_F / 4;
        if (!cam_dedans(b[0], b[1], y, b[3], b[4])) { b[2] = MISE.cam_a[2] + ecart_angle(y - MISE.cam_a[2]); return; }
    }
    for (float z = b[3] * 1.25f; z <= ZOOM_MAX; z *= 1.25f) if (!cam_dedans(b[0], b[1], b[2], z, b[4])) { b[3] = z; return; }
}
/* Caméra de script : de la caméra du moment (celle qui suit Sera, ou la caméra de script en cours) vers un point, un lacet
   (radians ; < -9 : celui du moment) et un zoom (0 : celui du moment) ; retour : vers Sera, avec le lacet et le zoom d'avant
   la première caméra de script, puis la caméra normale reprend. Point visé à une case du sol (1 / ech, comme la caméra du
   jeu) ; « hauteur » de l'instruction (05/11/2026) : MISE.cam_b[4], posé par le script après cet appel */
void mise_camera(int retour, float x, float z, float lacet, float zoom, float d) {
    if (retour && !MISE.cam_on) return;
    const float cur[5] = { MISE.cam_on ? MISE.cam[0] : E.sera.x, MISE.cam_on ? MISE.cam[1] : E.sera.z, MISE.cam_on ? MISE.cam[2] : E.yaw, MISE.cam_on ? MISE.cam[3] : E.zoom,
                           MISE.cam_on ? MISE.cam[4] : 1.0f / E.w.ech };
    if (!MISE.cam_on) { MISE.yaw0 = E.yaw; MISE.zoom0 = E.zoom; }
    /* lacet au pas de 45° le plus proche (cam_pas : les 8 directions des planches tombent juste) */
    const float yb = cam_pas(retour ? MISE.yaw0 : lacet < -9 ? cur[2] : lacet), zb = retour ? MISE.zoom0 : zoom > 0 ? zoom * SCENE_ZOOM : cur[3];
    for (int i = 0; i < 5; i++) MISE.cam_a[i] = MISE.cam[i] = cur[i];
    MISE.cam_b[0] = retour ? E.sera.x : x; MISE.cam_b[1] = retour ? E.sera.z : z; MISE.cam_b[4] = 1.0f / E.w.ech;
    MISE.cam_b[2] = cur[2] + ecart_angle(yb - cur[2]);   /* par le plus court chemin */
    MISE.cam_b[3] = zb < E.w.zoom_min ? E.w.zoom_min : zb > ZOOM_MAX ? ZOOM_MAX : zb;
    if (!retour) cam_hors_bat();
    /* Vitesse bornée : un trajet ou un demi-tour trop grand passe par le noir, le monde se charge pendant ce temps ; déjà au noir (fondu du script) : tout de suite */
    const float dist = hypotf(MISE.cam_b[0] - cur[0], MISE.cam_b[1] - cur[1]), dy = fabsf(MISE.cam_b[2] - cur[2]);
    MISE.coupe = 0; MISE.cam_auto = 0;
    if (MISE.fondu >= 0.9f) { d = 0.01f; if (!E.in.on && !BT.on) world_stream(MISE.cam_b[0], MISE.cam_b[1], 1); }
    else if (dist > (retour ? CAM_COUPE_RETOUR : CAM_COUPE) || dy > CAM_COUPE_A) { MISE.coupe = 1; MISE.coupe_f = MISE.fondu; d = 2 * COUPE_T; }
    else {
        const int bouge = dist > 0.05f || dy > 0.02f || fabsf(MISE.cam_b[3] - cur[3]) > 0.02f;
        d = fmaxf(d, fmaxf(dist / CAM_V, dy / CAM_A));
        if (bouge) d = fmaxf(d, CAM_MIN_D);   /* jamais un coup de fouet */
    }
    MISE.cam_on = 1; MISE.cam_retour = retour; MISE.cam_t = 0; MISE.cam_d = d > 0.01f ? d : 0.01f;
}
/* Fuite : cercle (x, z, r) à atteindre en t s ; n poursuivants du personnage perso partent à FUITE_RECUL cases derrière
   Sera (à l'opposé du but), en éventail, sur une case libre */
void mise_fuite(float x, float z, float r, float t, int perso, int n) {
    MISE.fuite_on = 1; MISE.fuite_ok = 0; MISE.fuite_x = x; MISE.fuite_z = z; MISE.fuite_r = r; MISE.fuite_t = t;
    float dx = x - E.sera.x, dz = z - E.sera.z, d = hypotf(dx, dz);
    if (d < 0.01f) { dx = 0; dz = 1; d = 1; }
    dx /= d; dz /= d;
    if (perso < 0 || perso >= E.w.npersos || E.w.persos[perso].sheet < 0) return;
    for (int i = 0; i < n; i++) {
        const float lat = (i - (n - 1) * 0.5f) * 1.5f, x0 = E.sera.x - dx * FUITE_RECUL + dz * lat, z0 = E.sera.z - dz * FUITE_RECUL - dx * lat;
        float px = x0, pz = z0;
        for (float rr = 0; rr <= 4; rr += 0.5f) {
            int trouve = 0;
            for (int a = 0; a < (rr > 0 ? 16 : 1) && !trouve; a++) {
                const float qx = x0 + cosf(a * PI_F / 8) * rr, qz = z0 + sinf(a * PI_F / 8) * rr;
                if (sol_libre(qx, qz, 0)) { px = qx; pz = qz; trouve = 1; }
            }
            if (trouve) break;
        }
        int s = -1;
        for (int k = 0; k < ACT_MAX && s < 0; k++) if (!MISE.act[k].on) s = k;
        if (s < 0) return;
        acteur_t *a = &MISE.act[s];
        memset(a, 0, sizeof *a);
        a->on = 1; a->chasse = 1; a->walking = 1; a->court = 1; a->perso = (uint8_t)perso; a->v = 1; a->carte = (uint8_t)E.w.carte;
        a->x = a->tx = px; a->z = a->tz = pz; a->dir = atan2f(E.sera.x - px, E.sera.z - pz);
    }
}
void mise_cam_stop(void) {
    if (MISE.cam_on) { E.yaw = MISE.yaw0; eng_set_zoom(MISE.zoom0); }
    MISE.cam_on = MISE.coupe = 0;
}
/* ---------- Avalanche et coulées (14/11/2026 ; internal.h, « Avalanche » ; avalancheMaj, coulee* de l'émulateur) ---------- */
aval_t AVAL ENG_FROID;
/* Part du trajet parcourue par le front : il part doucement et prend de la vitesse en dévalant */
float aval_avance(float t) {
    const float k = AVAL.d > 0 ? t / AVAL.d : 1;
    return k >= 1 ? 1 : k <= 0 ? 0 : k * k * (1.6f - 0.6f * k);
}
void aval_lancer(float ax, float az, float bx, float bz, float w, float d, int drapeau) {
    AVAL.on = 1; AVAL.t = 0; AVAL.d = d > 0.2f ? d : 0.2f; AVAL.ax = ax; AVAL.az = az; AVAL.bx = bx; AVAL.bz = bz;
    AVAL.w = w > 0.5f ? w : 0.5f; AVAL.drapeau = drapeau; AVAL.leve = 0; AVAL.son_t = 0;
}
void aval_maj(float dt) {
    if (!AVAL.on) return;
    AVAL.t += dt;
    const float s = aval_avance(AVAL.t);
    if (AVAL.t < AVAL.d) {
        /* grondement qui enfle (un roulement grave toutes les 0,3 s), l'image tremble de plus en plus */
        if ((AVAL.son_t -= dt) <= 0) { AVAL.son_t = 0.3f; son_jouer(SON_TERRE, 0.35f + 0.55f * s, 0.45f + 0.1f * s); }
        MISE.sec_f = 0.4f + 1.4f * s; MISE.sec_t = 0.2f;
    } else if (!AVAL.leve) {
        /* l'arrivée : choc, voile blanc (hud.c), la coulée paraît sous le voile */
        AVAL.leve = 1;
        if (AVAL.drapeau >= 0 && AVAL.drapeau < SC_MAX_FLAGS) E.flags[AVAL.drapeau >> 3] |= (uint8_t)(1 << (AVAL.drapeau & 7));
        son_jouer(SON_CRASH, 0.9f, 0.5f); son_jouer(SON_EXPLOSION, 0.7f, 0.4f);
        MISE.sec_f = 2.0f; MISE.sec_t = 1.0f;
    }
    if (AVAL.t >= AVAL.d + AVAL_APRES) AVAL.on = 0;
}
int coulee_visible(int k) {
    const coulee_t *c = &E.w.coul[k];
    return c->carte == E.w.carte && (c->drapeau >= SC_MAX_FLAGS || (E.flags[c->drapeau >> 3] >> (c->drapeau & 7) & 1));
}
/* Place de (x, z) dans l'ellipse de la coulée k agrandie de marge : moins de 1 dedans */
static float coulee_val(int k, float x, float z, float marge) {
    const coulee_t *c = &E.w.coul[k];
    const float dx = x - c->x, dz = z - c->z, u = (dx * c->ca + dz * c->sa) / (c->rx + marge), v = (dz * c->ca - dx * c->sa) / (c->rz + marge);
    return u * u + v * v;
}
static int coul_sortie = -1;   /* coulée où Sera se trouve et qu'elle quitte (sera_pas, ride_ok) : pas un obstacle pour ce pas */
int coulee_dans(float x, float z, float marge) {
    for (int k = 0; k < E.w.ncoul; k++) if (k != coul_sortie && coulee_visible(k) && coulee_val(k, x, z, marge) < 1) return 1;
    return 0;
}
/* Pas de (x0, z0) vers (x, z) : dans une coulée visible où l'on est déjà, on peut toujours s'en éloigner (posée dedans par une
   scène, une partie chargée) */
static int coulee_quitter(float x0, float z0, float x, float z, float marge) {
    for (int k = 0; k < E.w.ncoul; k++)
        if (coulee_visible(k) && coulee_val(k, x0, z0, marge) < 1 && coulee_val(k, x, z, marge) >= coulee_val(k, x0, z0, marge)) return k;
    return -1;
}
/* Partie chargée : l'événement d'une avalanche est fait, sa coulée est là (parties d'avant les coulées comprises) */
void coulee_apres_chargement(void) {
    for (int k = 0; k < E.w.ncoul; k++) {
        const coulee_t *c = &E.w.coul[k];
        if (c->drapeau < SC_MAX_FLAGS && c->ev < E.w.sc_nev && script_ev_fait(c->ev)) E.flags[c->drapeau >> 3] |= (uint8_t)(1 << (c->drapeau & 7));
    }
}
void mise_fin(int tout) {
    if (tout) AVAL.on = 0;
    for (int k = 0; k < ACT_MAX; k++) if (tout || !MISE.act[k].garder || MISE.act[k].chasse) MISE.act[k].on = 0;
    MISE.sera_on = MISE.fuite_on = 0;
    MISE.fondu = MISE.fondu_t = MISE.fondu_d = 0; MISE.sec_t = 0;
    if (tout) mise_cam_stop();
    else if (MISE.cam_on && !MISE.cam_retour) mise_camera(1, 0, 0, -10, 0, FIN_CAM);   /* fin d'une scène : la caméra revient vers Sera, sans saut */
}
void mise_update(float dt) {
    /* Personnage qui parle, trop loin pour que l'image glisse jusqu'à lui (render.c) : la caméra de la scène va le chercher, une fois posée ; mise_fin la rend à la fin du script */
    if (eng_dlg_hors && script_running() && (!MISE.cam_on || (!MISE.cam_retour && MISE.cam_t >= MISE.cam_d))) {
        eng_dlg_hors = 0;
        /* Au milieu d'elle et de celui à qui elle a affaire, s'il est tout près : tous les deux restent à l'écran, la caméra ne va pas de l'un à l'autre à chaque réplique */
        float tx = eng_dlg_x, tz = eng_dlg_z, ox = tx, oz = tz; int npc;
        if (hypotf(tx - E.sera.x, tz - E.sera.z) < 0.05f && !scene_parle_pos(E.w.sc_player, &ox, &oz, &npc)) { ox = tx; oz = tz; }
        if (E.ride < 0 && hypotf(ox - E.sera.x, oz - E.sera.z) < DLG_MILIEU) { tx = (ox + E.sera.x) / 2; tz = (oz + E.sera.z) / 2; }
        mise_camera(0, tx, tz, -10, 0, DLG_CHERCHE);
        MISE.cam_auto = 1;
        if (!MISE.coupe && MISE.fondu < 0.9f && hypotf(MISE.cam_b[0] - MISE.cam_a[0], MISE.cam_b[1] - MISE.cam_a[1]) > CAM_COUPE_AUTO) { MISE.coupe = 1; MISE.coupe_f = MISE.fondu; MISE.cam_d = 2 * COUPE_T; }
    }
    if (MISE.cam_on) {   /* glissement doux (courbe du cinquième degré : départ et arrivée sans à-coup) ; au retour, le but suit Sera ; coupe : le noir, le monde chargé, le plan posé, le retour */
        MISE.cam_t += dt;
        if (MISE.cam_retour) { MISE.cam_b[0] = E.sera.x; MISE.cam_b[1] = E.sera.z; }
        if (MISE.coupe) {
            const float kc = fminf(1, MISE.cam_t / COUPE_T);
            if (MISE.coupe == 1) {
                MISE.fondu = MISE.coupe_f + (1 - MISE.coupe_f) * kc;
                if (kc >= 1) {
                    if (!E.in.on && !BT.on) world_stream(MISE.cam_b[0], MISE.cam_b[1], 1);
                    if (!MISE.cam_retour) cam_hors_bat();   /* (le monde d'arrivée n'était peut-être pas chargé) */
                    for (int i = 0; i < 5; i++) MISE.cam[i] = MISE.cam_a[i] = MISE.cam_b[i];
                    MISE.coupe = 2; MISE.cam_t = 0;
                }
            } else {
                MISE.fondu = MISE.coupe_f + (1 - MISE.coupe_f) * (1 - kc);
                if (kc >= 1) { MISE.fondu = MISE.coupe_f; MISE.coupe = 0; MISE.cam_t = MISE.cam_d; }
            }
        } else {
            float k = MISE.cam_t / MISE.cam_d;
            if (k > 1) k = 1;
            k = k * k * k * (k * (k * 6 - 15) + 10);
            for (int i = 0; i < 5; i++) MISE.cam[i] = MISE.cam_a[i] + (MISE.cam_b[i] - MISE.cam_a[i]) * k;
        }
        if (MISE.cam_retour && !MISE.coupe && MISE.cam_t >= MISE.cam_d) { MISE.cam_on = 0; E.yaw = MISE.yaw0; eng_set_zoom(MISE.zoom0); }
    }
    if (MISE.fondu_t < MISE.fondu_d) {
        MISE.fondu_t += dt;
        const float k = MISE.fondu_t >= MISE.fondu_d ? 1 : MISE.fondu_t / MISE.fondu_d;
        MISE.fondu = MISE.fondu_a + (MISE.fondu_b - MISE.fondu_a) * k;
    }
    if (MISE.sec_t > 0) MISE.sec_t -= dt;
    /* Sera conduite par le script : marche (course au-delà de 1,5) en ligne droite */
    if (MISE.sera_on && (E.ride >= 0 || CR.on)) MISE.sera_on = 0;
    if (MISE.sera_on) {
        actor_t *s = &E.sera;
        const int st = MISE.sera_v > 1.5f ? 2 : 1;
        if (s->state != st) { s->state = st; s->frame = 0; s->anim = 0; }
        if (pas_evite(&s->x, &s->z, &s->dir, MISE.sera_tx, MISE.sera_tz, MARCHE_MS * MISE.sera_v / E.w.ech * dt, -1, -1, &MISE.sera_cote, &MISE.sera_mt, MISE.sera_lim, dt)) { MISE.sera_on = 0; s->state = 0; s->frame = 0; s->anim = 0; }
        else {
            s->anim += dt;
            if (s->anim > (st == 2 ? 0.09f : 0.13f)) { s->anim = 0; s->frame = (s->frame + 1) & 1023; }
        }
    }
    /* Acteurs : marche du script, ou poursuite (vers Sera, en glissant le long des obstacles) */
    for (int k = 0; k < ACT_MAX; k++) {
        acteur_t *a = &MISE.act[k];
        if (!a->on) continue;
        if (a->carte != E.w.carte) { a->on = 0; continue; }
        if (a->place_att && chunk_of_cell((int)floorf(a->x), (int)floorf(a->z)) && chunk_of_cell((int)floorf(a->tx), (int)floorf(a->tz))) {   /* le monde est là : sa place libre, ou celle où il va */
            a->place_att = 0;
            if (a->walking) scene_place(&a->tx, &a->tz, 4, a->perso, k);
            else { scene_place(&a->x, &a->z, 4, a->perso, k); a->tx = a->x; a->tz = a->z; }
        }
        if (a->chasse) {
            if (!MISE.fuite_on) { a->on = 0; continue; }
            const float dx = E.sera.x - a->x, dz = E.sera.z - a->z, d = hypotf(dx, dz), sp = FUITE_VITESSE / E.w.ech * dt;
            if (d > 1e-4f) {
                const float nx = a->x + dx / d * sp, nz = a->z + dz / d * sp;
                if (sol_libre(nx, nz, 0)) { a->x = nx; a->z = nz; }
                else if (sol_libre(nx, a->z, 0)) a->x = nx;
                else if (sol_libre(a->x, nz, 0)) a->z = nz;
                a->dir = atan2f(dx, dz);
            }
        } else if (a->walking) {
            int cote = a->cote ? a->cote : 1;
            const int fini = pas_evite(&a->x, &a->z, &a->dir, a->tx, a->tz, MARCHE_MS * a->v / E.w.ech * dt, a->perso, k, &cote, &a->marche_t, a->marche_lim, dt);
            a->cote = (int8_t)cote;
            if (fini) { a->walking = 0; a->frame = 0; a->anim = 0; }
        }
        a->anim += dt;
        if (a->anim > (a->walking ? (a->court ? 0.1f : 0.16f) : 0.22f)) { a->anim = 0; a->frame++; }
    }
    /* Fuite : le cercle atteint, le temps écoulé, ou un poursuivant au contact */
    if (MISE.fuite_on) {
        int fin = 0;
        MISE.fuite_t -= dt;
        if (hypotf(E.sera.x - MISE.fuite_x, E.sera.z - MISE.fuite_z) < MISE.fuite_r) { fin = 1; MISE.fuite_ok = 1; }
        else if (MISE.fuite_t <= 0) fin = 1;
        for (int k = 0; k < ACT_MAX && !fin; k++)
            if (MISE.act[k].on && MISE.act[k].chasse && hypotf(MISE.act[k].x - E.sera.x, MISE.act[k].z - E.sera.z) < FUITE_PRISE) fin = 1;
        if (fin) { MISE.fuite_on = 0; for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].chasse) MISE.act[k].on = 0; }
    }
}

/* Destination hors d'atteinte : Sera s'arrête si elle a fait moins de BLOQUE_PAS unités de chemin en BLOQUE_S s */
#define BLOQUE_S 0.5f
#define BLOQUE_PAS 0.3f
/* Profilage du jeu (ENG_PROFILE) : cycles de chaque étape de eng_update, voir eng_profile_update */
#ifdef ENG_PROFILE
#include "esp_cpu.h"
static uint32_t uprof[UPROF_N], uprof_t;
#define UP0() (uprof_t = esp_cpu_get_cycle_count())
#define UP(k) do { const uint32_t t_ = esp_cpu_get_cycle_count(); uprof[k] += t_ - uprof_t; uprof_t = t_; } while (0)
void eng_profile_update(uint32_t *out) { for (int k = 0; k < UPROF_N; k++) { if (out) out[k] = uprof[k]; uprof[k] = 0; } }
#else
#define UP0() ((void)0)
#define UP(k) ((void)0)
void eng_profile_update(uint32_t *out) { if (out) memset(out, 0, UPROF_N * sizeof *out); }
#endif

/* Bandeaux défilants des enseignes (DEFI ; enseignesTextures et defileMaj de l'émulateur, mêmes règles) : le message avance
   de defi_v colonnes par seconde, d'une colonne à la fois ; la texture d'un bandeau n'est réécrite que quand il bouge */
static float defi_t;
static void defi_maj(float dt) {
    world_t *w = &E.w;
    defi_t += dt;
    for (int i = 0; i < w->defi_n; i++) {
        const int off = (int)((uint32_t)(defi_t * (float)w->defi_v) % w->defi[i].len);
        if (off == w->defi[i].last) continue;
        w->defi[i].last = off;
        uint8_t *b = w->defi_buf + (size_t)i * (size_t)(w->defi_w * w->defi_h);
        for (int x = 0; x < w->defi_w; x++) {
            const uint8_t c = w->defi[i].cols[(off + x) % w->defi[i].len];
            for (int y = 0; y < w->defi_h; y++) b[y * w->defi_w + x] = (c >> y & 1) ? w->defi[i].lit : w->defi[i].dim;
        }
    }
}
/* Essai de fluidité (07/11/2026 ; SDX ESSAI de platform/esp32/main/sdx.c, capture_montre.py essai, rendu_test essai:…) : Sera posée en
   (x, z) sur la planète, à pied, pluie (1) ou ciel clair (0) tout de suite, à l'heure donnée (0 à 1 ; < 0 : celle du moment), sans rencontre,
   puis elle court vers le cap (degrés) pendant s secondes : les mêmes conditions à chaque mesure ; 0 : refusé (combat, course, scène,
   véhicule, autre carte) */
static struct { int on, renc, weather; float cap, fin, px, pz, pt, x0, z0, heure, rain; } ESM;   /* x0 … rain : l'état d'avant l'essai, rendu à la fin */   /* px, pz, pt : position il y a une seconde (bloquée : elle tourne) */
int eng_essai_monde(float x, float z, float cap, int pluie, float heure, float s) {
    if (accueil_on() && E.w.ile[0]) accueil_fin();   /* île (essais de la montre) : une partie neuve depuis l'écran titre, jamais sauvegardée (essai_mesure) */
    if (BT.on || CR.on || script_running() || E.ride >= 0 || E.w.carte || accueil_on()) return 0;
    if (E.in.on) int_reset();
    if (!ESM.on) { ESM.renc = E.renc_mode; ESM.x0 = E.sera.x; ESM.z0 = E.sera.z; ESM.heure = E.heure; ESM.weather = E.weather; ESM.rain = E.rain; }
    E.essai_mesure = 1;   /* l'état de l'essai n'ira pas dans la partie : plus de sauvegarde automatique avant un chargement */
    world_stream(x, z, 1);
    sera_placer(x, z, 4.0f);
    E.weather = pluie ? 2 : 1; E.rain = pluie ? E.w.rain_int / 100.0f : 0;
    if (heure >= 0) E.heure = heure;
    E.renc_mode = 3;   /* aucune rencontre : un combat couperait la mesure */
    ESM.on = 1; ESM.cap = cap * PI_F / 180; ESM.fin = E.clock + s; ESM.px = E.sera.x; ESM.pz = E.sera.z; ESM.pt = E.clock;
    return 1;
}
int eng_essai_actif(void) { return ESM.on; }   /* un essai en cours : la montre ne s'endort pas (main.c, veille_image) */
/* Une sauvegarde pendant un essai (la veille de SDX VEILLE avant un flash, une sauvegarde automatique) prend l'état d'avant l'essai :
   le réglage des rencontres et la place de Sera (08/11/2026, retour de jeu : RENCONTRES restées à AUCUNE dans la partie après les
   essais de la passe de performance, plus un seul combat). Rencontres d'avant l'essai (sa place en *x, *z), sinon -1 */
int essai_partie(float *x, float *z) { if (!ESM.on) return -1; *x = ESM.x0; *z = ESM.z0; return ESM.renc; }
static void essai_monde_maj(void) {
    if (!ESM.on) return;
    if (E.clock > ESM.fin || BT.on || CR.on || script_running() || E.ride >= 0) {
        /* Fin de l'essai : Sera remise à sa place, avec l'heure et la météo d'avant, et la partie de nouveau sauvegardée
           (08/11/2026 : la sauvegarde de la veille, eng_sauvegarder, ne regarde pas essai_mesure, et la position d'essai
           était restée dans la partie) ; interrompu (combat, course, scène) : rien de rendu, ni sauvegarde automatique */
        const int fini = E.clock > ESM.fin && !BT.on && !CR.on && !script_running() && E.ride < 0;
        ESM.on = 0; E.has_dest = 0; E.renc_mode = ESM.renc;
        if (fini) { world_stream(ESM.x0, ESM.z0, 1); sera_placer(ESM.x0, ESM.z0, 4.0f); E.heure = ESM.heure; E.weather = ESM.weather; E.rain = ESM.rain; E.essai_mesure = 0; }
        else E.weather = 0;
        return;
    }
    if (E.clock - ESM.pt > 1) {   /* bloquée depuis une seconde : un quart de tour et un peu plus */
        if (hypotf(E.sera.x - ESM.px, E.sera.z - ESM.pz) < 0.5f) ESM.cap += 1.75f;
        ESM.px = E.sera.x; ESM.pz = E.sera.z; ESM.pt = E.clock;
    }
    E.has_dest = 1; E.dest_court = 1; E.dest_x = E.sera.x + sinf(ESM.cap) * 8; E.dest_z = E.sera.z + cosf(ESM.cap) * 8;
}
void eng_update(float dt, const eng_input_t *in) {
    UP0();
#if ENG_DYN
    /* Plein écran : touchers dans le repère de l'interface (cadre de 466 × 466 centré dans l'image) */
    eng_input_t loc = *in;
    loc.touch_x -= ui_ox; loc.touch_y -= ui_oy; loc.touch2_x -= ui_ox; loc.touch2_y -= ui_oy;
    in = &loc;
#endif
    if (AG.on) { ag_update(dt, in); return; }   /* course anti-gravité (antigrav.c) */
    if (dt > 0.05f) dt = 0.05f;
    E.clock += dt;
    E.manette = in->manette;
    defi_maj(dt);   /* bandeaux défilants des enseignes (même à l'écran titre : le monde se voit derrière) */
    /* Montre verrouillée (réveil de la veille) : le monde est arrêté, seul le glissement qui déverrouille compte */
    if (hud_verrou(dt, in)) { sons_update(); music_update(dt); return; }
    /* Ouverture (écran titre, introduction, choix de l'héroïne) : le monde est arrêté, la lumière est celle du lieu
       montré */
    if (accueil_on()) {
        if (E.stream_hold) { world_stream(E.sera.x, E.sera.z, 1); E.stream_hold = 0; }   /* lieu montré changé */
        render_ambiance(accueil_heure());
        sons_update(); music_update(dt);
        hud_update(dt, in);
        return;
    }
    E.temps_jeu += dt;
    essai_monde_maj();
    /* Cycle jour / nuit : l'heure avance (sauf jour ou nuit fixés dans le menu Système), l'ambiance suit */
    /* Cycle « RÉEL » : l'heure de l'horloge de la montre (sans elle, comme le cycle automatique) */
    if (E.cycle == 3 && E.horloge >= 0) E.heure = E.horloge / 86400.0f;
    else if ((E.cycle == 0 || E.cycle == 3) && E.w.jour_s > 0 && !hud_pause()) { E.heure += dt / E.w.jour_s; if (E.heure >= 1) E.heure -= 1; }
    render_ambiance(E.cycle == 1 ? 0.5f : E.cycle == 2 ? 0.0f : E.heure);
    sons_update();
    music_update(dt);    /* piste selon la situation (combat compris), lecture sur la carte SD */
    UP(UPROF_DEBUT);
    /* Fenêtre recentrée (début ou fin de combat) : l'image préparée depuis n'avait pas de 3D, plus rien ne lit les
       textures de l'ancienne fenêtre */
    if (E.stream_hold) {
        if (BT.on) world_stream(E.w.arena_x, E.w.arena_z + 2, 1); else world_stream(E.sera.x, E.sera.z, 1);
        E.stream_hold = 0;
    }
    /* Combat fini pendant une visite : l'intérieur avait rendu le bloc de la fenêtre à l'arène, il est relu */
    if (E.in.on && !BT.on) int_pret();
    hud_update(dt, in);   /* gestes, menus, dialogue, combat : peut fixer une destination ou bloquer Sera */
    UP(UPROF_HUD);
    /* Combat : le monde s'arrête (personnages, météo), seule l'eau continue de couler */
    if (BT.on) { battle_update(dt); water_cycle(dt); return; }
    /* Menu ouvert ou course en pause : le monde s'arrête (personnages, trafic, course, heure, scénario) */
    if (hud_pause()) return;
    casse_clock += dt;
    if (!CR.on) course_fx_maj(dt);   /* éclats, feuilles et poussière des décors cassés */
    if ((in->keys & ENG_KEY_ROT_L) && !mise_bloque()) E.yaw -= dt * CAM_TOUCHE_V;
    if ((in->keys & ENG_KEY_ROT_R) && !mise_bloque()) E.yaw += dt * CAM_TOUCHE_V;
    /* Pas magnétiques (01/11/2026, demande de l'utilisateur) : à pied, caméra lâchée, elle glisse vers le multiple de 45°
       le plus proche, où les 8 directions des planches tombent juste (en véhicule, elle reste derrière lui ; caméras de
       scène : mise_camera) */
    if (E.ride < 0 && !CR.on && !mise_bloque() && !(in->keys & (ENG_KEY_ROT_L | ENG_KEY_ROT_R)) && !hud_camera_tourne())
        E.yaw += (cam_pas(E.yaw) - E.yaw) * fminf(1, dt * CAM_PAS_VIF);

    /* Direction voulue : croix (relative à la caméra), sinon la destination touchée */
    float mx = 0, mz = 0, vx = 0, vz = 0;
    if (mise_bloque()) E.has_dest = 0;   /* caméra de script, marche de Sera : le joueur ne la dirige pas */
    if (!E.ui_block && !mise_bloque()) {
        if (in->keys & ENG_KEY_UP) mz += 1;
        if (in->keys & ENG_KEY_DOWN) mz -= 1;
        if (in->keys & ENG_KEY_LEFT) mx -= 1;
        if (in->keys & ENG_KEY_RIGHT) mx += 1;
    }
    actor_t *s = &E.sera;
    if (mx != 0 || mz != 0) {
        E.has_dest = 0;
        float l = hypotf(mx, mz), cy = cosf(E.yaw), sy = sinf(E.yaw);
        vx = (mx * cy + mz * sy) / l; vz = (-mx * sy + mz * cy) / l;
    } else if (E.has_dest && !E.ui_block) {
        float dx = E.dest_x - s->x, dz = E.dest_z - s->z, d = hypotf(dx, dz);
        if (d < 0.15f) E.has_dest = 0; else { vx = dx / d; vz = dz / d; }
    }
    /* Dans l'eau : à la nage (tenue de nage, 2,2 unités par seconde, ni course, ni rencontre, ni bruit de pas) */
    {
        const int i = (int)floorf(s->x), j = (int)floorf(s->z);
        const chunk_t *c = E.ride < 0 && !CR.on && !E.in.on ? chunk_of_cell(i, j) : NULL;
        const int avant = E.nage;
        E.nage = E.in.on ? int_case(s->x, s->z) == 5 : c ? water_kind(c->flags[cell_local(i, j)]) : 0;   /* intérieur : l'eau d'un bassin */
        if (E.nage && E.w.ile[0] && !E.in.on && eau_prof(s->x, s->z) <= ILE_GUE) E.nage = 0;   /* île : pas assez de fond, elle marche dans l'eau */
        if (E.nage && !avant && E.ride < 0) son_jouer(SON_PLOUF, 0.45f, 1.1f);   /* elle entre dans l'eau */
    }
    /* Pose du scénario (instruction « pose ») : Sera reste sur place le temps de son animation */
    if (E.pose_t > 0) { E.pose_t -= dt; if (E.ride >= 0 || CR.on) E.pose_t = 0; else { vx = vz = 0; E.has_dest = 0; } }
    UP(UPROF_FIN);
    if (PIL.on && (E.ride < 0 || CR.on)) pilote_arreter(NULL);   /* descendue du véhicule, course : pilote automatique coupé */
    if (E.ride >= 0 && CR.on) { float th, st; drive_input(in, &th, &st); course_update(th, st, dt); }
    else if (E.ride >= 0) ride_drive(in, dt);
    else if (E.pose_t > 0) {   /* son animation, à la cadence des danseuses */
        if (s->state != 3) { s->state = 3; s->frame = 0; s->anim = 0; }
        s->anim += dt;
        if (s->anim > 0.15f) { s->anim = 0; s->frame = (s->frame + 1) & 1023; }
    }
    else if (MISE.sera_on) { /* marche du script : mise_update */ }
    else if (vx != 0 || vz != 0) {
        /* À pied : course si la destination a été touchée loin de Sera à l'écran (E.dest_court, hud.c : toucher loin, doigt
           maintenu loin d'elle) ou si le stick de la manette est poussé à fond (MANETTE_COURSE), marche sinon ; avec les
           deux animations, 3,0 et 5,2 unités par seconde, avec une seule l'allure d'origine de 4,2 */
        const int ps = member_sheet_mouv(0), both = !E.nage && has_anim(ps, ANIM_WALK) && has_anim(ps, ANIM_RUN);   /* (tenue de pose : sa planche de base) */
        const int far = ((E.has_dest && E.dest_court) || (in->manette && hypotf(in->stick_x, in->stick_y) >= MANETTE_COURSE)) && !ile_epuisee();   /* (île : épuisée, marche seulement) */
        const int st = both && far ? 2 : 1;
        if (s->state != st) { s->state = st; s->frame = 0; s->anim = 0; }
        float sp = (E.nage ? 2.2f : both ? (far ? 5.2f : 3.0f) : 4.2f) / E.w.ech * dt * (E.in.on && int_pente(s->x, s->z) ? INT_MARCHE_ESC : 1)   /* (escalier : plus lentement) */
            * (E.w.ile[0] && !E.nage && !E.in.on && eau_prof(s->x, s->z) > 0.1f ? ILE_GUE_ALLURE : 1), nx = s->x + vx * sp, nz = s->z + vz * sp;   /* (île : dans l'eau, plus lente) */
        const float ox = s->x, oz = s->z;
        int moved = 0;
        if (sera_pas(nx, s->z)) { s->x = nx; moved = 1; }
        if (sera_pas(s->x, nz)) { s->z = nz; moved = 1; }
        if (moved && !E.nage && !E.in.on) encounter_step(hypotf(s->x - ox, s->z - oz) * E.w.ech);
        if (!moved) E.has_dest = 0;
        /* Destination hors d'atteinte (rocher, mur, décor : Sera glisse contre l'obstacle ou piétine sur place ; un pas sur
           un axe suffisait à la faire continuer) : moins de BLOQUE_PAS unités de chemin en BLOQUE_S s, elle s'arrête (31/10/2026 ;
           marcherVers de l'émulateur) */
        else if (E.has_dest) {
            if (hypotf(s->x - E.bloque_x, s->z - E.bloque_z) > BLOQUE_PAS / E.w.ech) { E.bloque_x = s->x; E.bloque_z = s->z; E.bloque_t = 0; }
            else if ((E.bloque_t += dt) > BLOQUE_S) { E.has_dest = 0; E.dest_bloque = 1; E.dest_bx = E.dest_x; E.dest_bz = E.dest_z; }
        }
        s->dir = atan2f(vx, vz);
        s->anim += dt;
        if (s->anim > (s->state == 2 ? 0.09f : 0.13f)) {
            s->anim = 0; s->frame = (s->frame + 1) & 1023;
            if (!E.nage && ps >= 0) {   /* deux appuis par cycle de l'animation : à son début et à sa moitié */
                const int n = E.w.sheets[anim_sheet(ps, st == 2 ? ANIM_RUN : ANIM_WALK)].frames, c = n < 4 ? 4 : n, f = s->frame % c;
                if (f == 0 || f == c / 2) son_jouer(f ? SON_PAS_2 : SON_PAS_1, st == 2 ? 0.34f : 0.28f, 0.95f + 0.1f * frand());
            }
        }
    } else {   /* attente : pose, ou animation lente */
        if (s->state != 0) { s->state = 0; s->frame = 0; s->anim = 0; }
        s->anim += dt;
        if (s->anim > 0.22f) { s->anim = 0; s->frame = (s->frame + 1) & 1023; }
    }
    if (E.ride >= 0) {   /* le véhicule suit Sera (sa position est celle du conducteur) */
        npc_t *v = &E.npcs[E.ride];
        v->x = s->x; v->z = s->z; v->dir = s->dir;
        v->frame = (uint8_t)s->frame; v->walking = s->state != 0;   /* roule (marche) ou à l'arrêt (attente) */
        E.talk = -2;
    } else if (!E.ui_block) E.talk = talk_target();   /* gardé pendant un dialogue ; dedans, les personnages de l'intérieur */
    if (E.in.on && E.talk >= 0) { npc_t *n = &E.npcs[E.talk]; if (!PERS_EST_OBJET(E.w.persos[n->perso].flags)) n->dir = atan2f(E.sera.x - n->x, E.sera.z - n->z); }   /* il regarde Sera (pas un objet) */
    E.sera_y = ground_y(E.sera.x, E.sera.z); E.sera_y_ok = 1;   /* hauteur suivie (pont) */
    UP(UPROF_COURSE);
    /* Dans un intérieur, le monde ne bouge pas autour de lui (personnages, fenêtre de chunks, trafic) */
    if (!E.in.on) update_npcs(dt);
    ile_compagnes_maj(dt);   /* île du jeu des héroïnes : ses compagnes sur la trace de ses pas (ile.c) */
    if (!E.in.on) patrouille_maj(dt);   /* patrouilles hostiles (battle.c) : elles rôdent, chassent Sera, engagent le combat */
    UP(UPROF_PNJ);
    mise_update(dt);   /* mise en scène du scénario : caméra, acteurs, fondu, fuite */
    aval_maj(dt);      /* avalanche du scénario : front, grondement, secousse, drapeau de sa coulée */
    passage_step();   /* passage vers une autre carte (puits, égout, ascenseur…) */
    /* Chunks qui entrent dans la fenêtre : autour de ce que vise la caméra d'une scène (travelling vers un lieu lointain :
       sans cela, il n'y avait rien à montrer là-bas ; les 3 × 3 autour du point visé se chargent tout de suite, le reste
       un par image), sinon autour de Sera */
    if (!E.stream_hold && !E.in.on) world_stream(MISE.cam_on ? MISE.cam[0] : s->x, MISE.cam_on ? MISE.cam[1] : s->z, 0);
    UP(UPROF_CHUNKS);
    weather_update(dt);
    cendre_update(dt);   /* pluie de cendre : l'équipe tousse dehors sans masque */
    ile_soir_maj();      /* île : le coucher du soleil et la nuit s'annoncent */
    ile_dormir_maj(dt);  /* île : la couche d'un abri à portée, le sommeil jusqu'au matin */
    ile_rodeurs_maj(dt); /* île : le reflet de la nuit paraît, chasse, se glisse dans les maisons (ile.c) */
    ile_offrande_maj(dt); /* île : la séquence d'une succube, jusqu'au matin (ile.c) */
    ville_maj(dt);       /* brouillard reculé en ville */
    repl_maj();          /* répliques : le lot de la zone de Sera, lu quand elle en change (repliques.c) */
    script_update(dt);   /* scénario : déclencheurs, script en cours */
    UP(UPROF_SCRIPT);
    if (!E.in.on) traffic_update(dt);
    UP(UPROF_TRAFIC);
    int_update(dt);      /* porte devant Sera, sortie d'un intérieur, alarme */
    police_update(dt);   /* recherche, poursuite, arrestation (après la porte et l'interlocuteur : elle les retire pendant la scène) */
    hud_sauve_maj(dt);   /* sauvegarde automatique aux points clés, quand rien n'est en cours (hud.c) */
    coffre_update();     /* coffre à portée (coffres.c) */
    panneau_maj();       /* panneau de quêtes à portée, guidage d'un panneau (panneaux.c) */
    garage_maj();        /* véhicule de Sera : suivi, ou reposé à sa place (garage.c) */
    water_cycle(dt);
    UP(UPROF_FIN);
}

/* Outil de banc d'essai : place Sera et la caméra */
void eng_teleport(float x, float z, float yaw) { E.sera.x = x; E.sera.z = z; E.yaw = yaw; E.has_dest = 0; E.sera_y_ok = 0; if (!BT.on) world_stream(x, z, 1); }
void eng_move_to(float x, float z) { E.sera.x = x; E.sera.z = z; E.has_dest = 0; }
int eng_world_size(void) { return E.w.n; }
/* Zoom de la caméra, du zoom mini du paquet (1 par défaut depuis le 31/10/2026 ; 0,8 avant : plus loin qu'à l'origine) à ZOOM_MAX */
void eng_set_zoom(float z) {
    if (carte_plein() && E.zoom > 0) { carte_molette(z / E.zoom); return; }   /* molette, lunette : la carte du menu ouverte */
    const float lo = E.w.zoom_min;
    E.zoom = z < lo ? lo : z > ZOOM_MAX ? ZOOM_MAX : z;
    E.cam_dist = 12.5f / E.w.ech / E.zoom;
}
float eng_zoom(void) { return E.zoom; }
void eng_start_position(float *x, float *z) { *x = E.w.start_x; *z = E.w.start_z; }
int eng_textures_in_ram(void) { return E.w.tex_copied; }

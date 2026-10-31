/* Logique du jeu sur la world map : déplacement de Sera, collisions, caméra, cycle de l'eau, rencontres */
#include <math.h>
#include <string.h>
#include "internal.h"
#include "audio.h"

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
static int sera_pas(float x, float z) {
    if (E.in.on) return int_libre(x, z);
    return sol_libre(x, z, 1) && !char_block(x, z, E.sera.x, E.sera.z, NULL);
}
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
    const int wk = water_kind(f);
    if (wk && !(swim && (wk == 1 || sera_etanche()))) return 0;
    /* Rectangles de collision : chacun est rangé dans tous les chunks qu'il touche, marge comprise. Marge (le corps de
       Sera) : MARGE_MUR contre un mur, MARGE_DECOR contre un petit obstacle (tronc, rocher, objet : moins d'une case de
       côté ; avec 0,35 m partout, un tronc de 0,26 case en bloquait 0,8 et une forêt dense ne se traversait pas) */
    const float mm = MARGE_MUR / w->ech, md = MARGE_DECOR / w->ech;
    for (uint32_t k = 0; k < c->nsolid; k++) {
        const float *r = &c->solids[k * 4];
        const float m = r[2] - r[0] < 1 && r[3] - r[1] < 1 ? md : mm;
        if (x > r[0] - m && x < r[2] + m && z > r[1] - m && z < r[3] + m && !rect_casse(r)) return 0;
    }
    return 1;
}

/* ---------- Tenues ---------- */
/* Porteur des tenues du membre m : le joueur (Sera, ou l'héroïne choisie au départ), sinon le personnage de la
   bibliothèque de son nom (-1 : aucun) */
static int tenue_owner(int m) { return m == 0 ? (E.heroine >= 0 ? E.heroine : TENUE_JOUEUR) : E.w.party_perso[m]; }
/* Une tenue 18+ n'est à personne tant que le 18+ est verrouillé (édition unique, tenue_vue) */
int tenue_mine(int m, int t) { return m >= 0 && m < 4 && t >= 0 && t < E.w.ntenues && E.w.tenues[t].owner == tenue_owner(m) && tenue_vue(t); }
int tenue_named(int m, const char *nom) {
    for (int t = 0; nom && t < E.w.ntenues; t++) if (tenue_mine(m, t) && !strcmp(E.w.tenues[t].nom, nom)) return t;
    return -1;
}
/* Mise d'office : nage (Sera dans l'eau claire ; dans les eaux noires, sa tenue portée, étanche), course, drapeau du
   scénario levé (la dernière de la liste) ; sinon celle qu'il porte (menu ÉQUIP., instruction « tenue ») */
int tenue_of(int m, int sec) {
    const world_t *w = &E.w;
    const int own = m >= 0 && m < 4 ? tenue_owner(m) : -1;
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
    return tenue_mine(m, E.tenue_eq[m]) ? E.tenue_eq[m] : -1;
}
int tenue_nage(int perso) {
    for (int t = 0; t < E.w.ntenues; t++) if (E.w.tenues[t].owner == perso && E.w.tenues[t].ctx == TENUE_NAGE) return t;
    return -1;
}
int member_sheet(int m) {
    const int t = tenue_of(m, 0);
    if (t >= 0) return E.w.tenues[t].sheet;
    if (m == 0) return E.heroine >= 0 ? E.w.persos[E.heroine].sheet : E.w.psheet;
    const int pp = m > 0 && m < 4 ? E.w.party_perso[m] : -1;
    return pp >= 0 ? E.w.persos[pp].sheet : -1;
}
int member_tissu(int m) { const int t = tenue_of(m, 0); return t >= 0 ? E.w.tenues[t].vremap : 0; }
float swim_alt(void) {
    const int i = (int)floorf(E.sera.x), j = (int)floorf(E.sera.z);
    const chunk_t *c = chunk_of_cell(i, j);
    const float s = c && c->water_h[cell_local(i, j)] > -1e8f ? c->water_h[cell_local(i, j)] : E.w.wl, a = s - ground_y(E.sera.x, E.sera.z);
    return a > 0 ? a : 0;
}

/* ---------- Trafic ---------- */
/* Jusqu'à traf_max véhicules (ceux de la bibliothèque marqués PERS_TRAFIC, sauf bateau et vaisseau) autour de Sera. Ils
   apparaissent entre 10 et 18 cases d'elle et disparaissent au-delà de 22. Sur les routes, ils suivent les axes (ROUT)
   sur la voie de droite ; au bout d'un axe, un autre qui passe à moins de 1,2 case, sinon demi-tour. Dans les rues des
   villes, de case en case (8 voisines), tout droit de préférence, un peu sur la droite. Arrêt si quelqu'un est devant ou
   si Sera s'adresse à eux (pour les voler) ; comme l'émulateur. */
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
static void traffic_next(traffic_t *t) {
    if (t->poursuite) { pour_case(t); return; }
    const int hx = t->ni - t->ci, hz = t->nj - t->cj;
    int odi[8], odj[8], n = 0; float w[8], sum = 0;
    for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        if ((!di && !dj) || !is_road(t->ni + di, t->nj + dj)) continue;
        const float c = (di * hx + dj * hz) / (hypotf((float)di, (float)dj) * hypotf((float)hx, (float)hz));   /* cosinus avec le cap */
        if (c <= -0.2f) continue;
        static const float W[3] = { 0.6f, 1.5f, 6 };   /* en table : le ternaire faisait planter GCC Xtensa (postreload) */
        odi[n] = di; odj[n] = dj; w[n] = W[(c > 0.3f) + (c > 0.9f)]; sum += w[n]; n++;
    }
    if (!n) { int a = t->ci, b = t->cj; t->ci = t->ni; t->cj = t->nj; t->ni = a; t->nj = b; return; }   /* cul-de-sac */
    float r = frand() * sum; int o = 0;
    for (; o < n - 1; o++) { r -= w[o]; if (r <= 0) break; }
    t->ci = t->ni; t->cj = t->nj; t->ni += odi[o]; t->nj += odj[o];
}

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
/* Place sur la voie de droite : au quart de la largeur depuis l'axe (milieu de la demi-chaussée) */
static void lane_place(traffic_t *t) {
    const route_t *r = &E.w.routes[t->road];
    const int a = t->sgn > 0 ? t->seg : t->seg + 1, b = t->sgn > 0 ? t->seg + 1 : t->seg;
    float ax, az, bx, bz, nax, naz, nbx, nbz;
    route_pt(r, a, &ax, &az); route_pt(r, b, &bx, &bz); route_nrm(r, a, &nax, &naz); route_nrm(r, b, &nbx, &nbz);
    const float k = t->k, off = r->hw * 0.5f * t->sgn;
    t->x = ax + (bx - ax) * k + (nax + (nbx - nax) * k) * off;
    t->z = az + (bz - az) * k + (naz + (nbz - naz) * k) * off;
    t->dir = atan2f(bx - ax, bz - az);
}
/* Bout de l'axe : un autre axe qui passe à moins de 1,2 case (carrefour, embranchement), pris au hasard, dans le sens
   qui s'éloigne de son extrémité (au hasard au milieu d'un axe) ; sinon demi-tour */
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
    if (!nc) { t->sgn = -t->sgn; t->k = 0; return; }   /* cul-de-sac : demi-tour */
    const int c = (int)(frand() * nc) % nc;
    t->road = cr[c]; t->seg = cs[c]; t->sgn = cg[c];
    t->k = t->sgn > 0 ? ck[c] : 1 - ck[c];
}

/* Quelqu'un à moins de 1,6 case devant le véhicule (dans le sens de la marche, à moins de 0,7 de côté) */
static int ahead(const traffic_t *t, float hx, float hz, float x, float z) {
    const float dx = x - t->x, dz = z - t->z, f = dx * hx + dz * hz;
    return f > 0 && f < 1.6f && fabsf(dx * hz - dz * hx) < 0.7f;
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
/* Nouveau véhicule sur une case de rue prise au hasard entre 10 et 18 cases de Sera */
static int spawn_cell(int perso) {
    const world_t *w = &E.w;
    for (int a = 0; a < 24; a++) {
        const float ang = frand() * 2 * PI_F, d = (10 + frand() * 8) / w->ech;
        const int i = (int)floorf(E.sera.x + cosf(ang) * d), j = (int)floorf(E.sera.z + sinf(ang) * d);
        if (!is_road(i, j) || traffic_near(i + 0.5f, j + 0.5f)) continue;
        int ndi[8], ndj[8], nn = 0;
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) if ((di || dj) && is_road(i + di, j + dj)) { ndi[nn] = di; ndj[nn] = dj; nn++; }
        traffic_t *t = nn ? traffic_free() : NULL;
        if (!t) continue;
        const int o = (int)(frand() * nn) % nn;
        memset(t, 0, sizeof *t);
        t->on = 1; t->perso = perso; t->var = (uint8_t)(frand() * (E.w.persos[perso].nvar + 1)); t->ci = i; t->cj = j; t->ni = i + ndi[o]; t->nj = j + ndj[o];
        t->x = i + 0.5f; t->z = j + 0.5f; t->dir = atan2f((float)ndi[o], (float)ndj[o]);
        return 1;
    }
    return 0;
}
/* Trafic logique (comme TRAF_POIDS de l'émulateur) : fréquence de chaque véhicule selon son service (veh_service) ; les
   urgences (secours, pompiers, une voiture de police sur trois) roulent à 75 % de leur vitesse maxi au lieu de 45 % */
static const float TRAF_POIDS[7] = { 1, 0.25f, 0.12f, 0.06f, 0.15f, 0.3f, 0.35f };
/* En ville (rues, hors des axes de routes) : TRAF_RUE du nombre de véhicules au plus, à TRAF_LENT de l'allure des routes */
#define TRAF_RUE 0.4f
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
#define POUR_ALLURE 0.85f     /* part de la vitesse maxi */
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
    return 1;
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
    if (!ok) { int a = t->ci, b = t->cj; t->ci = t->ni; t->cj = t->nj; t->ni = a; t->nj = b; return; }   /* cul-de-sac */
    t->ci = t->ni; t->cj = t->nj; t->ni = ni; t->nj = nj;
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
        t->lane = 0; t->ci = t->wi; t->cj = t->wj; t->ni = ni; t->nj = nj; t->k = 0;
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
        t->ci = ci; t->cj = cj; t->ni = ni; t->nj = nj;
        t->x = ci + 0.5f; t->z = cj + 0.5f; t->dir = atan2f((float)(ni - ci), (float)(nj - cj));
    } else {
        float ax, az, bx, bz;
        route_pt(&w->routes[br], bs, &ax, &az); route_pt(&w->routes[br], bs + 1, &bx, &bz);
        t->lane = 1; t->road = br; t->seg = bs; t->k = 0.5f; t->sgn = champ_dist(bx, bz, cx, cz) < champ_dist(ax, az, cx, cz) ? 1 : -1;
        lane_place(t);
    }
    return 1;
}

static void traffic_update(float dt) {
    const world_t *w = &E.w;
    int kinds[MAX_PERSOS], nk = 0;
    for (int p = 0; p < w->npersos; p++) {
        const int f = w->persos[p].flags, ty = VEH_TYPE(f);
        if ((f & PERS_VEHICULE) && (f & PERS_TRAFIC) && ty != VEH_BATEAU && ty != VEH_VAISSEAU && w->persos[p].sheet >= 0) kinds[nk++] = p;
    }
    if (CR.on || w->carte) { for (int k = 0; k < TRAFFIC_MAX; k++) E.traffic[k].on = 0; return; }   /* trafic : carte 0, hors course */
    const int normal = w->traf_max && nk;   /* sans trafic du paquet, seules les voitures de police poursuivantes roulent */
    int count = 0, pours = 0;
    for (int k = 0; k < TRAFFIC_MAX; k++) {   /* poursuivants : plus loin (POUR_LOIN), et à part du nombre du trafic */
        traffic_t *t = &E.traffic[k];
        if (t->on && (hypotf(t->x - E.sera.x, t->z - E.sera.z) > (t->poursuite ? POUR_LOIN : 22.0f) / w->ech || (!normal && !t->poursuite))) t->on = 0;
        count += t->on && !t->poursuite;
        pours += t->on && t->poursuite;
    }
    if (pours) champ_maj(dt); else champ_ok = 0;   /* champ des poursuivants */
    E.traf_spawn_t -= dt;
    if (normal && count < w->traf_max && E.traf_spawn_t <= 0) {
        /* Sur les axes des routes ou dans les rues des villes, une fois sur deux chacun ; l'autre si le premier échoue */
        E.traf_spawn_t = 0.5f;
        const int perso = traf_tirage(kinds, nk), lane_first = w->nroutes && frand() < 0.5f;
        int avant[TRAFFIC_MAX], en_rue = 0;
        for (int k = 0; k < TRAFFIC_MAX; k++) { avant[k] = E.traffic[k].on; en_rue += E.traffic[k].on && !E.traffic[k].lane && !E.traffic[k].poursuite; }
        int rue_max = (int)lroundf(w->traf_max * TRAF_RUE);
        if (rue_max < 1) rue_max = 1;
        if (en_rue >= rue_max) { if (w->nroutes) spawn_lane(perso); }
        else if (!(lane_first && spawn_lane(perso)) && !spawn_cell(perso) && !lane_first) spawn_lane(perso);
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
        int blocked = 0;
        if (!t->poursuite) {
            blocked = sera_route && (E.talk == TALK_TRAF(k) || ahead(t, hx, hz, E.sera.x, E.sera.z));
            for (int m = 0; m < E.nnpc && !blocked; m++)
                if (m != E.ride && npc_present(&E.npcs[m]) && fabsf(E.npcs[m].x - t->x) < 2 && fabsf(E.npcs[m].z - t->z) < 2) blocked = ahead(t, hx, hz, E.npcs[m].x, E.npcs[m].z);
            for (int m = 0; m < TRAFFIC_MAX && !blocked; m++) if (m != k && E.traffic[m].on) blocked = ahead(t, hx, hz, E.traffic[m].x, E.traffic[m].z);
        }
        /* Allure de croisière : 45 % de la vitesse maxi ; accélère au départ, freine (au double) devant un obstacle ; un
           poursuivant à POUR_ALLURE, arrêté pendant une arrestation */
        float vmax, acc;
        veh_params(t->perso, &vmax, &acc);
        const float cruise = blocked ? 0 : t->poursuite ? (POL.arr ? 0 : vmax * POUR_ALLURE) : vmax * (t->urgence ? 0.75f : 0.45f) * (t->lane ? 1 : TRAF_LENT);
        t->v = t->v < cruise ? fminf(cruise, t->v + acc * dt) : fmaxf(cruise, t->v - 2 * acc * dt);
        t->walking = t->v > 0.05f;
        /* Bloqué plus de 2,5 s (un piéton immobile sur la chaussée) : demi-tour (sur un axe : voie d'en face) */
        t->stuck = blocked && E.talk != TALK_TRAF(k) ? t->stuck + dt : 0;
        if (t->stuck > 2.5f) {
            if (t->lane) t->sgn = -t->sgn;
            else { int a = t->ci, b = t->cj; t->ci = t->ni; t->cj = t->nj; t->ni = a; t->nj = b; }
            t->k = 1 - t->k; t->stuck = 0; t->v = 0;
        }
        if (t->v > 0) {
            if (t->lane) {
                t->k += dt * t->v / lane_len(t);
                for (int it = 0; it < 4 && t->k >= 1 && t->mode == POUR_RESEAU; it++) {   /* un poursuivant peut quitter le réseau (ralliement) */
                    t->k -= 1; t->seg += t->sgn;
                    if (t->seg < 0 || t->seg > w->routes[t->road].n - 2) { t->seg -= t->sgn; lane_next(t); }
                }
            } else {
                t->k += dt * t->v / hypotf((float)(t->ni - t->ci), (float)(t->nj - t->cj));
                while (t->k >= 1 && t->mode == POUR_RESEAU) { t->k -= 1; traffic_next(t); }
            }
            t->anim += dt * fminf(1, t->v / (vmax * 0.3f));
            if (t->anim > 0.12f) { t->anim = 0; t->frame++; }
        }
        if (t->mode != POUR_RESEAU) continue;   /* poursuivant parti vers le réseau voisin : il garde sa place */
        if (t->lane) lane_place(t);
        else {
            const float len = hypotf((float)(t->ni - t->ci), (float)(t->nj - t->cj)), ux = (t->ni - t->ci) / len, uz = (t->nj - t->cj) / len;
            t->x = t->ci + 0.5f + (t->ni - t->ci) * t->k + uz * 0.3f;   /* décalé à droite de la chaussée */
            t->z = t->cj + 0.5f + (t->nj - t->cj) * t->k - ux * 0.3f;
            t->dir = atan2f(ux, uz);
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
static struct { float x, z, t0, dx, dz; } casses[CASSE_N];
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
/* Véhicule p noyé en (x, z) : il roule (voiture, camion, mech) et un lac ou la mer y dépasse sa hauteur de gué */
int veh_noye(int p, float x, float z) {
    const int t = VEH_TYPE(E.w.persos[p].flags);
    if (t < VEH_VOITURE || t > VEH_MECH || !veh_rolls(p)) return 0;
    const uint8_t f = flags_at(x, z);
    return !(f & (F_ROAD | F_RIVER)) && (f & F_WATER) && eau_prof(x, z) >= VEH_GUE[t] / E.w.ech;
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
    }
    return !char_block(x, z, E.sera.x, E.sera.z, &E.npcs[E.ride]);
}

/* Vitesse maxi ici (cases par seconde) : hors route, la voiture et le camion n'en gardent que 55 % ; dans l'eau, un
   véhicule qui roule va à moitié de sa vitesse dans une rivière, et dans un lac de moins en moins vite à mesure que
   l'eau monte (le quart près de sa hauteur de gué) */
static float ride_speed(float x, float z, float vmax) {
    const int t = ride_type();
    const uint8_t f = flags_at(x, z);
    float k = (t == VEH_VOITURE || t == VEH_CAMION) && !(f & (F_ROAD | F_STREET)) ? 0.55f : 1;
    if (t >= VEH_VOITURE && t <= VEH_MECH && veh_rolls(E.npcs[E.ride].perso) && water_kind(f))
        k *= (f & F_RIVER) ? 0.5f : fmaxf(0.25f, 1 - 0.75f * eau_prof(x, z) * E.w.ech / VEH_GUE[t]);
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
static void ride_drive(const eng_input_t *in, float dt) {
    actor_t *s = &E.sera;
    float vmax, acc, th, st;
    veh_params(E.npcs[E.ride].perso, &vmax, &acc);
    const int vol = vol_permanent();
    if (vol) { vmax *= VOL_VITESSE; acc *= VOL_VITESSE; }   /* vol permanent : orbites resserrées */
    drive_input(in, &th, &st);
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
        E.ride_alt = fmaxf(0, eau_prof(s->x, s->z) - 0.6f * VEH_GUE[ride_type()] / E.w.ech) * fmaxf(0, 1 - E.ride_coule / COULE_T);
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
        const int mx = ride_ok(nx, s->z), mz = ride_ok(s->x, nz);
        const float v0 = fabsf(*v), ox = s->x, oz = s->z;
        if (mx) s->x = nx;
        if (mz) s->z = nz;
        if (!mx && !mz) *v = 0;
        else if (!mx) *v *= fabsf(hz);
        else if (!mz) *v *= fabsf(hx);
        ride_choc(v0 - fabsf(*v), vmax);
        if (mx != mz && fabsf(*v) > 0.8f) E.frotte = fminf(1, fabsf(*v) / vmax);   /* il glisse le long de l'obstacle */
        if (vol && (mx || mz)) encounter_step(hypotf(s->x - ox, s->z - oz) * E.w.ech);   /* rencontres de l'espace, en vol */
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
            if (m != E.ride && d > far) { far = d; s = m; }
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
    int cand[MAX_PERSOS], nc = 0, pref[MAX_PERSOS], np = 0;
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
static int talk_target(void) {
    float bd = 2.4f / E.w.ech;
    int best = -2;
    for (int k = 0; k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (!npc_present(n) || fabsf(n->x - E.sera.x) >= bd || fabsf(n->z - E.sera.z) >= bd) continue;
        if (acteur_de(n->perso) >= 0) continue;   /* remplacé par son acteur, le temps d'une scène */
        if (E.in.on && is_vehicle(k)) continue;   /* voiture garée d'un garage : ni dialogue ni conduite */
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
   7 cases plus loin, atteinte par des cases praticables hors des chaussées (une fois sur cinq, ils traversent) ; au-delà
   de PROM_RAYON cases de leur place, ils prennent un cap qui les en rapproche. Les autres flânent autour de leur place. */
#define PROM_RAYON 30.0f
#define PROM_TRAVERSE 0.06f   /* part des promeneurs qui traversent la chaussée (promCible de l'émulateur) */
static int promeneur(const npc_t *n) { return n->meneur || ((n->graine * 2654435761u) >> 16) % 5 < 3; }   /* meneur d'une patrouille : toujours */
/* Suiveur d'une patrouille (comme suivreChef de l'émulateur) : un point derrière son meneur, 0,9 case par rang, dans la
   direction où il regarde ; il presse le pas s'il traîne, le rejoint s'il s'est perdu ; arrêté, il regarde où regarde
   son meneur */
static void suivre_chef(npc_t *n, float dt) {
    const npc_t *c = &E.npcs[n->chef - 1];
    const float d0 = 0.9f * n->rang, tx = c->x - sinf(c->dir) * d0, tz = c->z - cosf(c->dir) * d0;
    const float dx = tx - n->x, dz = tz - n->z, d = hypotf(dx, dz);
    if (d > 10) { if (walkable_for(tx, tz, n, 0)) { n->x = tx; n->z = tz; } return; }
    if (d < 0.25f) {
        n->walking = 0; n->dir = c->dir; n->anim += dt;
        if (n->anim > 0.22f) { n->anim = 0; n->frame++; }
        return;
    }
    float sp = (d > 1.5f ? 2.0f : 1.3f) / E.w.ech * dt;
    if (sp > d) sp = d;
    const float nx = n->x + dx / d * sp, nz = n->z + dz / d * sp;
    if (!walkable_for(nx, nz, n, 0)) return;
    n->x = nx; n->z = nz; n->dir = atan2f(dx, dz); n->walking = 1; n->anim += dt;
    if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
}
static int prom_cible(npc_t *n) {
    static const signed char ORDRE[8] = { 0, 2, -2, 1, -1, 3, -3, 4 };   /* écarts au cap actuel, en huitièmes de tour */
    const int h0 = (int)lroundf(n->dir / (PI_F / 4)) & 7, garde = frand() < 0.7f;
    const float ox = n->hx - n->x, oz = n->hz - n->z, loin = hypotf(ox, oz) > PROM_RAYON;
    const int traverse = frand() < PROM_TRAVERSE;
    for (int t = 0; t < 8; t++) {
        int e = ORDRE[t];
        if (!garde && t == 0) continue;                                   /* tourner cette fois-ci */
        if (!garde && t < 3 && frand() < 0.5f) e = -e;                    /* à gauche ou à droite */
        const int h = (h0 + e + 8) & 7;
        const float sx = sinf(h * PI_F / 4), sz = cosf(h * PI_F / 4);
        if (loin && sx * ox + sz * oz <= 0) continue;                     /* trop loin : un cap qui rapproche */
        const float L = 3 + frand() * 4;
        float ok = 0;
        for (float s = 0.5f; s <= L; s += 0.5f) {
            const float px = n->x + sx * s, pz = n->z + sz * s;
            if (!walkable_for(px, pz, n, 0) || (!traverse && (flags_at(px, pz) & (F_ROAD | F_STREET)))) break;
            ok = s;
        }
        if (ok < 1.5f) continue;
        while (ok > 1.5f && (flags_at(n->x + sx * ok, n->z + sz * ok) & (F_ROAD | F_STREET))) ok -= 0.5f;   /* pas d'arrêt sur la chaussée */
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
float cendre_dehors(void) { return !E.w.ciel && (!E.in.on || BT.on) ? E.rain : 0; }
/* Brouillard en ville (31/10/2026) : dans les rues, les bâtiments bornent déjà la vue, le brouillard ne faisait que griser
   les façades. Sera sur une zone urbaine de la planète (urbain, industriel, rue : rangs 8, 10, 11 de Continent.LAND,
   bloc de la grille des rencontres) : E.ville_v tend vers 1 en VILLE_FONDU s, et render.c fait commencer le brouillard aux
   VILLE_RECUL de sa profondeur ; la pluie de cendre le rend (ambiance) en proportion de son intensité (villeMaj de
   l'émulateur) */
#define VILLE_FONDU 1.5f
static void ville_maj(float dt) {
    const world_t *w = &E.w;
    int ville = 0;
    if (!w->carte && w->zone && w->n > 0) {
        const int i = (int)floorf(E.sera.x) * w->zone_n / w->n, j = (int)floorf(E.sera.z) * w->zone_n / w->n;
        if (i >= 0 && j >= 0 && i < w->zone_n && j < w->zone_n) { const int t = w->zone[j * w->zone_n + i]; ville = t == 8 || t == 10 || t == 11; }
    }
    const float cible = ville ? fmaxf(0, 1 - 2 * cendre_dehors()) : 0;
    E.ville_v += (cible - E.ville_v) * fminf(1, dt / VILLE_FONDU * 3);
}
/* Passants à l'abri : un flâneur dont le nom est tiré (générique ou métier ; ni meneur ni suiveur d'une patrouille, ni
   starter, ni conducteur en fuite) se met à l'abri si sa graine le place sous ABRI_PART × l'intensité ; il ne disparaît
   ni ne revient sous les yeux de Sera : son état ne change qu'à plus de ABRI_LOIN cases d'elle (abriMaj de l'émulateur) */
#define ABRI_PART 0.85f
#define ABRI_LOIN 12.0f
static void abri_maj(npc_t *n, float cendre) {
    const float dx = n->x - E.sera.x, dz = n->z - E.sera.z, loin = ABRI_LOIN / E.w.ech;
    if (dx * dx + dz * dz < loin * loin) return;
    const perso_t *pe = &E.w.persos[n->perso];
    const int passant = (pe->flags & PERS_FLANE) && PERS_GENRE(pe->flags) && !n->meneur && !n->chef && !n->depart && !n->fuit;
    n->abri = passant && (((uint32_t)n->graine * 2246822519u) >> 16) % 100 < cendre * ABRI_PART * 100;
}

/* Exemplaires proches de Sera : l'interlocuteur la regarde ; ceux qui flânent marchent vers un point tiré autour de
   leur place, s'arrêtent quelques secondes et repartent (comme l'émulateur) ; les promeneurs suivent les trottoirs
   (prom_cible) ; les autres restent immobiles ; sous la cendre, des passants se mettent à l'abri (abri_maj) */
static void update_npcs(float dt) {
    const float act = 24.0f / E.w.ech, cendre = cendre_dehors();
    for (int k = 0; k < E.nnpc; k++) {
        npc_t *n = &E.npcs[k];
        if (fabsf(n->x - E.sera.x) > act || fabsf(n->z - E.sera.z) > act) continue;
        if (cendre > 0 || n->abri) abri_maj(n, cendre);
        if (!npc_present(n)) continue;
        const perso_t *pe = &E.w.persos[n->perso];
        if (pe->flags & PERS_VEHICULE) continue;
        if (k == E.talk) { n->dir = atan2f(E.sera.x - n->x, E.sera.z - n->z); n->frame = 0; n->walking = 0; n->wait = 2; continue; }
        if (n->chef && n->chef <= E.nnpc) { suivre_chef(n, dt); continue; }
        if (n->fuit) {   /* conducteur éjecté : il court (2,6 m/s à l'échelle) vers son point de fuite, puis redevient passant */
            const float fx = n->tx - n->x, fz = n->tz - n->z, fd = hypotf(fx, fz);
            float sp = 2.6f / E.w.ech * dt;
            if (sp > fd) sp = fd;
            if (fd < 0.1f || !walkable_for(n->x + fx / fd * sp, n->z + fz / fd * sp, n, 0)) { n->fuit = 0; n->walking = 0; n->hx = n->x; n->hz = n->z; n->wait = 1 + frand() * 2; continue; }
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
                    if (flags_at(n->tx, n->tz) & (F_ROAD | F_STREET)) { n->walking = 0; n->wait = 1 + frand() * 2; }   /* pas de flânerie sur la chaussée */
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
        if (walkable_for(nx, nz, n, 0)) {
            n->x = nx; n->z = nz; n->dir = atan2f(dx, dz); n->anim += dt;
            if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
        } else { n->walking = 0; n->wait = promeneur(n) ? 0.3f + frand() : 1 + frand() * 2; }
    }
}

/* Météo : en automatique, des phases de 40 à 120 s, pluvieuses avec la probabilité rain_freq ; l'intensité suit la
   cible en RAIN_FADE secondes (la pluie s'installe et s'arrête en douceur) */
#define RAIN_FADE 6.0f
static void weather_update(float dt) {
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
    if (E.w.ciel || E.rain < CENDRE_SEUIL) { cendre_averse = 0; E.cendre_t = 0; return; }   /* temps sec, sous terre, ailleurs */
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
    for (int m = 0; m < 4; m++) {
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
/* Poids d'un ennemi au tirage : sous la pluie de cendre, les Glaneurs sortent la ramasser (poidsRenc de l'émulateur) */
static float enc_poids(const enemy_t *en, float cendre) {
    const char *nom = en->perso >= 0 && en->perso < E.w.npersos ? E.w.persos[en->perso].nom : NULL;
    return en->poids * (cendre > 0 && nom && strstr(nom, "GLANEUR") ? 1 + CENDRE_GLANEUR * cendre : 1);
}
int encounter_pick(void) {
    const world_t *w = &E.w;
    if (!w->zone || !w->nenemies) return -1;
    /* Bloc de la zone à l'échelle de la carte : 2 × 2 cases d'ordinaire, 64 × 64 sur le système solaire (zoneOctets de
       l'émulateur) */
    const int i = (int)floorf(E.sera.x) * w->zone_n / (w->n > 0 ? w->n : 1), j = (int)floorf(E.sera.z) * w->zone_n / (w->n > 0 ? w->n : 1);
    if (i < 0 || j < 0 || i >= w->zone_n || j >= w->zone_n) return -1;
    const int t = w->zone[j * w->zone_n + i];
    if (t >= 32) return -1;   /* 255 : terrain inconnu */
    const int histoire = script_niveau(E.sera.x, E.sera.z);
    const float cendre = cendre_dehors();
    int membres = 0, last = -1;
    float total = 0;
    for (int m = 0; m < 4; m++) membres += in_team(m);
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
   la marche compte 1 + CENDRE_RENC × l'intensité fois plus ; encounterStep de l'émulateur */
static const float RENC_FOIS[5] = { 3, 6, 12, 0, 1.5f };
static float enc_left;
static void encounter_step(float dist) {
    const world_t *w = &E.w;
    const float pas = w->renc * RENC_FOIS[E.renc_mode % 5];
    if (pas <= 0 || !w->zone || (E.ride >= 0 && !vol_permanent()) || BT.on || E.ui_block || script_running()) return;   /* en véhicule, jamais, sauf en vol permanent */
    if (enc_left <= 0) enc_left = pas * (0.5f - 0.5f * logf(1 - erand()));
    enc_left -= dist * (1 + CENDRE_RENC * cendre_dehors());
    if (enc_left > 0) return;
    enc_left = 0;
    const int k = encounter_pick();
    if (k >= 0) battle_start(k, encounter_level(), 0);
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
mise_t MISE;
static float ecart_angle(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
int acteur_de(int perso) {
    for (int k = 0; k < ACT_MAX; k++) if (MISE.act[k].on && !MISE.act[k].chasse && MISE.act[k].perso == perso) return k;
    return -1;
}
/* Acteur du personnage perso en (x, z) : le sien s'il est déjà là, sinon une place libre ; -1 sans planche ou sans place */
int acteur_poser(int perso, float x, float z, float dir) {
    if (perso < 0 || perso >= E.w.npersos || E.w.persos[perso].sheet < 0) return -1;
    int s = acteur_de(perso);
    for (int k = 0; k < ACT_MAX && s < 0; k++) if (!MISE.act[k].on) s = k;
    if (s < 0) return -1;
    acteur_t *a = &MISE.act[s];
    memset(a, 0, sizeof *a);
    a->on = 1; a->perso = (uint8_t)perso; a->x = a->tx = x; a->z = a->tz = z; a->dir = dir; a->v = 1; a->carte = (uint8_t)E.w.carte;
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
int mise_bloque(void) { return MISE.cam_on || MISE.sera_on || police_bloque(); }   /* arrestation (police.c) aussi */
/* Caméra de script : de la caméra du moment (celle qui suit Sera, ou la caméra de script en cours) vers un point, un lacet
   (radians ; < -9 : celui du moment) et un zoom (0 : celui du moment) ; retour : vers Sera, avec le lacet et le zoom d'avant
   la première caméra de script, puis la caméra normale reprend */
void mise_camera(int retour, float x, float z, float lacet, float zoom, float d) {
    if (retour && !MISE.cam_on) return;
    const float cur[4] = { MISE.cam_on ? MISE.cam[0] : E.sera.x, MISE.cam_on ? MISE.cam[1] : E.sera.z, MISE.cam_on ? MISE.cam[2] : E.yaw, MISE.cam_on ? MISE.cam[3] : E.zoom };
    if (!MISE.cam_on) { MISE.yaw0 = E.yaw; MISE.zoom0 = E.zoom; }
    const float yb = retour ? MISE.yaw0 : lacet < -9 ? cur[2] : lacet, zb = retour ? MISE.zoom0 : zoom > 0 ? zoom : cur[3];
    for (int i = 0; i < 4; i++) MISE.cam_a[i] = MISE.cam[i] = cur[i];
    MISE.cam_b[0] = retour ? E.sera.x : x; MISE.cam_b[1] = retour ? E.sera.z : z;
    MISE.cam_b[2] = cur[2] + ecart_angle(yb - cur[2]);   /* par le plus court chemin */
    MISE.cam_b[3] = zb < E.w.zoom_min ? E.w.zoom_min : zb > ZOOM_MAX ? ZOOM_MAX : zb;
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
void mise_fin(int tout) {
    for (int k = 0; k < ACT_MAX; k++) if (tout || !MISE.act[k].garder || MISE.act[k].chasse) MISE.act[k].on = 0;
    if (MISE.cam_on) { E.yaw = MISE.yaw0; eng_set_zoom(MISE.zoom0); }
    MISE.cam_on = MISE.sera_on = MISE.fuite_on = 0;
    MISE.fondu = MISE.fondu_t = MISE.fondu_d = 0; MISE.sec_t = 0;
}
/* Un pas de sp vers (tx, tz) en ligne droite ; 1 : arrivé */
static int pas_vers(float *x, float *z, float *dir, float tx, float tz, float sp) {
    const float dx = tx - *x, dz = tz - *z, d = hypotf(dx, dz);
    if (d > 1e-4f) *dir = atan2f(dx, dz);
    if (d <= sp || d < 0.02f) { *x = tx; *z = tz; return 1; }
    *x += dx / d * sp; *z += dz / d * sp;
    return 0;
}
void mise_update(float dt) {
    if (MISE.cam_on) {   /* glissement doux (smoothstep) ; au retour, le but suit Sera */
        MISE.cam_t += dt;
        float k = MISE.cam_t / MISE.cam_d;
        if (k > 1) k = 1;
        k = k * k * (3 - 2 * k);
        if (MISE.cam_retour) { MISE.cam_b[0] = E.sera.x; MISE.cam_b[1] = E.sera.z; }
        for (int i = 0; i < 4; i++) MISE.cam[i] = MISE.cam_a[i] + (MISE.cam_b[i] - MISE.cam_a[i]) * k;
        if (MISE.cam_retour && MISE.cam_t >= MISE.cam_d) { MISE.cam_on = 0; E.yaw = MISE.yaw0; eng_set_zoom(MISE.zoom0); }
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
        if (pas_vers(&s->x, &s->z, &s->dir, MISE.sera_tx, MISE.sera_tz, MARCHE_MS * MISE.sera_v / E.w.ech * dt)) { MISE.sera_on = 0; s->state = 0; s->frame = 0; s->anim = 0; }
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
        } else if (a->walking && pas_vers(&a->x, &a->z, &a->dir, a->tx, a->tz, MARCHE_MS * a->v / E.w.ech * dt)) { a->walking = 0; a->frame = 0; a->anim = 0; }
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
void eng_update(float dt, const eng_input_t *in) {
#if ENG_DYN
    /* Plein écran : touchers dans le repère de l'interface (cadre de 466 × 466 centré dans l'image) */
    eng_input_t loc = *in;
    loc.touch_x -= ui_ox; loc.touch_y -= ui_oy; loc.touch2_x -= ui_ox; loc.touch2_y -= ui_oy;
    in = &loc;
#endif
    if (dt > 0.05f) dt = 0.05f;
    E.clock += dt;
    E.manette = in->manette;
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
    /* Cycle jour / nuit : l'heure avance (sauf jour ou nuit fixés dans le menu Système), l'ambiance suit */
    /* Cycle « RÉEL » : l'heure de l'horloge de la montre (sans elle, comme le cycle automatique) */
    if (E.cycle == 3 && E.horloge >= 0) E.heure = E.horloge / 86400.0f;
    else if ((E.cycle == 0 || E.cycle == 3) && E.w.jour_s > 0 && !hud_pause()) { E.heure += dt / E.w.jour_s; if (E.heure >= 1) E.heure -= 1; }
    render_ambiance(E.cycle == 1 ? 0.5f : E.cycle == 2 ? 0.0f : E.heure);
    sons_update();
    music_update(dt);    /* piste selon la situation (combat compris), lecture sur la carte SD */
    /* Fenêtre recentrée (début ou fin de combat) : l'image préparée depuis n'avait pas de 3D, plus rien ne lit les
       textures de l'ancienne fenêtre */
    if (E.stream_hold) {
        if (BT.on) world_stream(E.w.arena_x, E.w.arena_z + 2, 1); else world_stream(E.sera.x, E.sera.z, 1);
        E.stream_hold = 0;
    }
    /* Combat fini pendant une visite : l'intérieur avait rendu le bloc de la fenêtre à l'arène, il est relu */
    if (E.in.on && !BT.on) int_pret();
    hud_update(dt, in);   /* gestes, menus, dialogue, combat : peut fixer une destination ou bloquer Sera */
    /* Combat : le monde s'arrête (personnages, météo), seule l'eau continue de couler */
    if (BT.on) { battle_update(dt); water_cycle(dt); return; }
    /* Menu ouvert ou course en pause : le monde s'arrête (personnages, trafic, course, heure, scénario) */
    if (hud_pause()) return;
    casse_clock += dt;
    if (!CR.on) course_fx_maj(dt);   /* éclats, feuilles et poussière des décors cassés */
    if ((in->keys & ENG_KEY_ROT_L) && !mise_bloque()) E.yaw -= dt * 1.6f;
    if ((in->keys & ENG_KEY_ROT_R) && !mise_bloque()) E.yaw += dt * 1.6f;

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
        if (E.nage && !avant && E.ride < 0) son_jouer(SON_PLOUF, 0.45f, 1.1f);   /* elle entre dans l'eau */
    }
    /* Pose du scénario (instruction « pose ») : Sera reste sur place le temps de son animation */
    if (E.pose_t > 0) { E.pose_t -= dt; if (E.ride >= 0 || CR.on) E.pose_t = 0; else { vx = vz = 0; E.has_dest = 0; } }
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
        const int ps = member_sheet(0), both = !E.nage && has_anim(ps, ANIM_WALK) && has_anim(ps, ANIM_RUN);
        const int far = (E.has_dest && E.dest_court) || (in->manette && hypotf(in->stick_x, in->stick_y) >= MANETTE_COURSE);
        const int st = both && far ? 2 : 1;
        if (s->state != st) { s->state = st; s->frame = 0; s->anim = 0; }
        float sp = (E.nage ? 2.2f : both ? (far ? 5.2f : 3.0f) : 4.2f) / E.w.ech * dt, nx = s->x + vx * sp, nz = s->z + vz * sp;
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
    if (E.in.on && E.talk >= 0) { npc_t *n = &E.npcs[E.talk]; n->dir = atan2f(E.sera.x - n->x, E.sera.z - n->z); }   /* il regarde Sera */
    E.sera_y = ground_y(E.sera.x, E.sera.z); E.sera_y_ok = 1;   /* hauteur suivie (pont) */
    /* Dans un intérieur, le monde ne bouge pas autour de lui (personnages, fenêtre de chunks, trafic) */
    if (!E.in.on) update_npcs(dt);
    mise_update(dt);   /* mise en scène du scénario : caméra, acteurs, fondu, fuite */
    passage_step();   /* passage vers une autre carte (puits, égout, ascenseur…) */
    /* Chunks qui entrent dans la fenêtre : autour de ce que vise la caméra d'une scène (travelling vers un lieu lointain :
       sans cela, il n'y avait rien à montrer là-bas ; les 3 × 3 autour du point visé se chargent tout de suite, le reste
       un par image), sinon autour de Sera */
    if (!E.stream_hold && !E.in.on) world_stream(MISE.cam_on ? MISE.cam[0] : s->x, MISE.cam_on ? MISE.cam[1] : s->z, 0);
    weather_update(dt);
    cendre_update(dt);   /* pluie de cendre : l'équipe tousse dehors sans masque */
    ville_maj(dt);       /* brouillard reculé en ville */
    script_update(dt);   /* scénario : déclencheurs, script en cours */
    if (!E.in.on) traffic_update(dt);
    int_update(dt);      /* porte devant Sera, sortie d'un intérieur, alarme */
    police_update(dt);   /* recherche, poursuite, arrestation (après la porte et l'interlocuteur : elle les retire pendant la scène) */
    water_cycle(dt);
}

/* Outil de banc d'essai : place Sera et la caméra */
void eng_teleport(float x, float z, float yaw) { E.sera.x = x; E.sera.z = z; E.yaw = yaw; E.has_dest = 0; E.sera_y_ok = 0; if (!BT.on) world_stream(x, z, 1); }
void eng_move_to(float x, float z) { E.sera.x = x; E.sera.z = z; E.has_dest = 0; }
int eng_world_size(void) { return E.w.n; }
/* Zoom de la caméra, du zoom mini du paquet (1 par défaut depuis le 31/10/2026 ; 0,8 avant : plus loin qu'à l'origine) à ZOOM_MAX */
void eng_set_zoom(float z) {
    const float lo = E.w.zoom_min;
    E.zoom = z < lo ? lo : z > ZOOM_MAX ? ZOOM_MAX : z;
    E.cam_dist = 12.5f / E.w.ech / E.zoom;
}
float eng_zoom(void) { return E.zoom; }
void eng_start_position(float *x, float *z) { *x = E.w.start_x; *z = E.w.start_z; }
int eng_textures_in_ram(void) { return E.w.tex_copied; }

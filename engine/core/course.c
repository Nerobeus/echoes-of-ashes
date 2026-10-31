/*
 * Course anti-gravité sur les pistes du paquet (section PIS2), comme la course de l'émulateur. Relief : la hauteur de
 * la piste suit son axe (pente : gravité de 5 cases/s²) ; sur un pont, les murs sont au bord de la chaussée.
 *
 * L'abscisse s d'un point de la piste se compte depuis la ligne de départ (0..L) ; la progression d'un concurrent (prog)
 * la cumule tour après tour : négative sur la grille, tours × L à l'arrivée. Elle ne suit la piste que de proche en
 * proche (projection sur les segments voisins), et un tour ne compte qu'une fois : couper ou repasser la ligne à
 * l'envers ne rapporte rien. Murs au bord de la piste (demi-largeur moins le rayon d'un concurrent).
 *
 * Physique : la vitesse (vx, vz) est distincte du cap. La poussée agit le long du cap, l'adhérence ne rattrape la
 * vitesse latérale qu'en partie (dérapage ; adhérence de la surface fois celle du véhicule, section VEH2) ; braquage à
 * fond : aérofrein (tourne plus serré, freine). Les adversaires sont les véhicules choisis au menu de course, à la
 * vitesse du véhicule du joueur fois leur niveau ; un élastique de 6 % au plus les rapproche du joueur. Tracés : les
 * pistes qui partent de la même ligne (noms : section PNOM).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "audio.h"

#define PISTES_MAX 8
#define COURSE_V 1.0f      /* vitesse en course : celle du véhicule × COURSE_V (la vitesse maxi de l'atelier telle quelle) */
#define COURSE_R 0.42f     /* rayon d'un concurrent (cases) */
#define COURSE_FEUX 0.8f   /* un feu de plus toutes les COURSE_FEUX s, départ au quatrième temps */
/* Accélération (plaque, turbo) : vitesse maxi × COURSE_BOOST, poussée en plus (cases/s²), durées (s) ; à la fin, la
   vitesse en trop retombe vite (COURSE_RETOUR par seconde) */
#define COURSE_BOOST 1.15f
#define COURSE_POUSSEE 5.0f
#define COURSE_PLAQUE_T 1.4f   /* 0,8 s : trop court */
#define COURSE_TURBO_T 1.8f
#define COURSE_RETOUR 2.5f
#define COURSE_EXPO 1.8f   /* direction du joueur en course : on tournait trop */
/* Virage du joueur en course, réglage SYSTÈME › DIRECTION (E.volant : TRÈS DOUCE à TRÈS VIVE, VOLANT_DEFAUT au départ) :
   rotation voulue × COURSE_VOLANT, rejointe à COURSE_VOLANT_VIF par seconde (12 pour les adversaires). Essais du
   31/10/2026 : 1 et 12 (comme les adversaires) bien trop fort au doigt, 0,5 et 5 trop raide */
static const float COURSE_VOLANT[5] = { 0.5f, 0.6f, 0.7f, 0.8f, 1.0f };
static const float COURSE_VOLANT_VIF[5] = { 5, 6.5f, 8, 9.5f, 12 };
static int essai_pilote;   /* course d'essai (eng_essai_course) : joueur piloté */
static int essai_cycle = -1;   /* cycle jour / nuit du joueur, rendu à la fin de la course d'essai (elle se court de jour) */

/* Surfaces de piste : adhérence (vitesse latérale rattrapée par seconde), vitesse maxi, dérapage qui fait partir en
   tête-à-queue (rad), poussière soulevée */
typedef struct { const char *nom; float grip, v, spin; int pous; } surface_t;
static const surface_t SURF[5] = { { "ASPHALTE", 5, 1, 0.7f, 0 }, { "LASER", 5.5f, 1.08f, 0.7f, 0 }, { "GLACE", 1.5f, 1, 1.2f, 0 },
                                   { "GRAVIER", 3.2f, 0.86f, 0.85f, 1 }, { "TERRE", 3, 0.9f, 0.85f, 1 } };
static const surface_t *surf(void) { return &SURF[PISTE.surface >= 0 && PISTE.surface < 5 ? PISTE.surface : 0]; }
const char *course_surface_nom(void) { return surf()->nom; }

static piste_t PISTES[PISTES_MAX];
static int npistes, actif = -1;   /* actif : index de PISTE dans PISTES */
piste_t PISTE;
course_t CR;

static inline float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* PIS2 : nombre u32, puis pour chaque piste : points u32, demi-largeur f32 (chaussée), tours u8, bandes de gravier en
   1/10 de case u8, surface u8, 0, point de départ u32, points (x, y, z) f32, un u8 par point (1 : sur un pont), complétés à
   4 octets, puis les plaques : nombre u32, 4 octets chacune (point u16, écart i8 en 1/10 de case, sorte u8). Axe,
   hauteurs et abscisses copiés en mémoire (E.cfg.alloc) */
void course_load(const uint8_t *p, uint32_t size) {
    memset(&PISTE, 0, sizeof PISTE); memset(&CR, 0, sizeof CR); memset(PISTES, 0, sizeof PISTES); npistes = 0; actif = -1;
    if (!p || size < 4) return;
    const uint32_t count = rd32(p);
    uint32_t o = 4;
    for (uint32_t k = 0; k < count && npistes < PISTES_MAX && o + 16 <= size; k++) {
        const uint32_t n = rd32(p + o);
        uint32_t len = 16 + n * 12 + ((n + 3) & ~3u);
        if (n < 4 || o + len > size) break;
        piste_t *P = &PISTES[npistes];
        float *mem = E.cfg.alloc ? E.cfg.alloc(sizeof(float) * (4 * n + 1) + n, 0) : NULL;
        if (!mem) break;
        float *X = mem, *Y = mem + n, *Z = mem + 2 * n, *S = mem + 3 * n;
        uint8_t *PB = (uint8_t *)(mem + 4 * n + 1);
        P->hw = rdf(p + o + 4); P->tours = p[o + 8] ? p[o + 8] : 3; P->bande = p[o + 9] / 10.0f; P->surface = p[o + 10]; P->dep = (int)(rd32(p + o + 12) % n);
        if (!(P->hw > 0.5f && P->hw < 20)) P->hw = 2;
        P->x0 = P->z0 = 1e9f; P->x1 = P->z1 = -1e9f;
        for (uint32_t i = 0; i < n; i++) {
            X[i] = rdf(p + o + 16 + i * 12); Y[i] = rdf(p + o + 20 + i * 12); Z[i] = rdf(p + o + 24 + i * 12);
            PB[i] = p[o + 16 + n * 12 + i];
            P->x0 = fminf(P->x0, X[i]); P->x1 = fmaxf(P->x1, X[i]);
            P->z0 = fminf(P->z0, Z[i]); P->z1 = fmaxf(P->z1, Z[i]);
        }
        S[0] = 0;
        for (uint32_t i = 1; i <= n; i++) S[i] = S[i - 1] + hypotf(X[i % n] - X[i - 1], Z[i % n] - Z[i - 1]);
        P->X = X; P->Y = Y; P->Z = Z; P->S = S; P->PB = PB; P->L = S[n]; P->n = (int)n;
        if (o + len + 4 <= size) {   /* plaques */
            const uint32_t m = rd32(p + o + len);
            if (o + len + 4 + m * 4 <= size) { P->plaques = p + o + len + 4; P->nplaques = (int)m; len += 4 + m * 4; }
        }
        npistes++;
        o += len;
    }
    if (npistes) PISTE = PISTES[0];
    actif = npistes ? 0 : -1;
}

/* PNOM : noms des tracés (menu de course) : nombre u32, puis 24 octets par piste (UTF-8 complété de 0) */
void course_noms(const uint8_t *p, uint32_t size) {
    for (int k = 0; k < npistes; k++) {
        if (p && size >= 4 && (uint32_t)k < rd32(p) && 4 + (uint32_t)(k + 1) * 24 <= size) { memcpy(PISTES[k].nom, p + 4 + k * 24, 23); PISTES[k].nom[23] = 0; }
        else snprintf(PISTES[k].nom, sizeof PISTES[k].nom, "PISTE %d", k + 1);
    }
    if (actif >= 0) PISTE = PISTES[actif];
}

int course_npistes(void) { return npistes; }
const piste_t *course_piste(int k) { return k >= 0 && k < npistes ? &PISTES[k] : NULL; }
int course_piste_active(void) { return actif; }
void course_trace(int k) { if (k >= 0 && k < npistes && !CR.on) { PISTE = PISTES[k]; actif = k; } }
/* Tracés d'un même circuit (js/circuit.js : boucle et raccourcis, sens inverse) : les pistes dont la ligne de départ
   est à moins de 8 cases de celle de la piste active */
int course_traces(int *out) {
    int n = 0;
    if (actif < 0) return 0;
    for (int k = 0; k < npistes; k++) {
        const piste_t *P = &PISTES[k];
        if (hypotf(P->X[P->dep] - PISTE.X[PISTE.dep], P->Z[P->dep] - PISTE.Z[PISTE.dep]) < 8) out[n++] = k;
    }
    return n;
}

/* La piste la plus proche de (x, z) (distance à sa ligne de départ) devient la piste active */
int course_choisir(float x, float z) {
    int best = -1;
    float bd = 1e30f;
    for (int k = 0; k < npistes; k++) {
        const piste_t *P = &PISTES[k];
        const float d = hypotf(P->X[P->dep] - x, P->Z[P->dep] - z);
        if (d < bd) { bd = d; best = k; }
    }
    if (best >= 0 && !CR.on) { PISTE = PISTES[best]; actif = best; }
    return best >= 0;
}

/* Point de l'axe le plus proche de (x, z) parmi les segments seg - 8 .. seg + 8 (tous si seg < 0) : segment, abscisse,
   normale à droite, écart à l'axe, tangente, hauteur de la piste, pente (montée le long de l'axe), sur un pont */
typedef struct { int seg, pont; float s, nx, nz, lat, tx, tz, y, pente; } proj_t;
static proj_t piste_proj(float x, float z, int seg) {
    const float *PX = PISTE.X, *PZ = PISTE.Z, *PY = PISTE.Y;
    const int n = PISTE.n, k0 = seg < 0 ? 0 : seg - 8, k1 = seg < 0 ? n - 1 : seg + 8;
    proj_t b = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    float bd = 1e30f, bpx = 0, bpz = 0, bt = 0, bl = 1;
    for (int k = k0; k <= k1; k++) {
        const int i = (k % n + n) % n, j = (i + 1) % n;
        const float dx = PX[j] - PX[i], dz = PZ[j] - PZ[i], l2 = fmaxf(dx * dx + dz * dz, 1e-6f);
        float t = ((x - PX[i]) * dx + (z - PZ[i]) * dz) / l2;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const float px = PX[i] + dx * t, pz = PZ[i] + dz * t, d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
        if (d2 < bd) {
            const float l = sqrtf(l2);
            bd = d2; b.seg = i; b.s = PISTE.S[i] + l * t; b.nx = dz / l; b.nz = -dx / l; b.tx = dx / l; b.tz = dz / l;
            bpx = px; bpz = pz; bt = t; bl = l;
        }
    }
    const int j = (b.seg + 1) % n;
    b.lat = (x - bpx) * b.nx + (z - bpz) * b.nz;
    b.s = fmodf(b.s - PISTE.S[PISTE.dep] + PISTE.L, PISTE.L);
    b.y = PY[b.seg] + (PY[j] - PY[b.seg]) * bt + 0.03f;
    b.pente = (PY[j] - PY[b.seg]) / bl;
    b.pont = PISTE.PB[b.seg] || PISTE.PB[j];
    return b;
}

/* Point de l'axe à l'abscisse s (depuis la ligne), décalé de lat vers la droite, et cap de l'axe */
static void piste_pt(float s, float lat, float *x, float *z, float *dir) {
    const float *PX = PISTE.X, *PZ = PISTE.Z, *PS_ = PISTE.S;
    float a = fmodf(s + PS_[PISTE.dep], PISTE.L);
    if (a < 0) a += PISTE.L;
    int lo = 0, hi = PISTE.n;
    while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (PS_[m] <= a) lo = m; else hi = m; }
    const int j = (lo + 1) % PISTE.n;
    const float l = fmaxf(PS_[lo + 1] - PS_[lo], 1e-6f), t = (a - PS_[lo]) / l, dx = PX[j] - PX[lo], dz = PZ[j] - PZ[lo];
    *x = PX[lo] + dx * t + dz / l * lat; *z = PZ[lo] + dz * t - dx / l * lat;
    if (dir) *dir = atan2f(dx, dz);
}

/* Hauteur de la piste sous le joueur par rapport au sol (négative sous un pont, où le sol est le tablier) */
float course_alt_joueur(void) {
    if (!CR.on || CR.n < 1) return 0;
    const racer_t *j = &CR.r[CR.n - 1];
    return j->y - ground_y(j->x, j->z);
}

/* Drapeau : en véhicule, arrêté à moins de 3 cases de la ligne */
int course_ok(void) {
    if (!npistes || CR.on || E.ride < 0 || E.ride_phase || BT.on || E.ui_block || fabsf(E.ride_v) > 0.3f) return 0;
    course_choisir(E.sera.x, E.sera.z);
    float x, z;
    piste_pt(0, 0, &x, &z, NULL);
    return hypotf(E.sera.x - x, E.sera.z - z) < PISTE.hw + 3;
}

/* Grille : deux files derrière la ligne, le joueur en dernier ; adversaires : les véhicules rivaux (choisis au menu de
   course), chacun avec son adhérence (VEH2) et la vitesse du véhicule du joueur fois son niveau. Le véhicule du joueur : celui qu'il conduit s'il est du personnage voulu, sinon un
   exemplaire amené sur la grille (l'autre reste où il est) ; altitude imposée, la même pour tous.
   Niveau (0 facile, 1 normal, 2 difficile) : vitesse des adversaires, part de celle du joueur, du premier au dernier. */
int course_start(int perso, int tours, const int *rivaux, int nia, int niv, int armes) {
    if (!PISTE.n || CR.on || BT.on || perso < 0 || perso >= E.w.npersos || !(E.w.persos[perso].flags & PERS_VEHICULE)) return 0;
    const int avant = E.ride;
    int pret = -1;   /* exemplaire amené pour la course : il repart à la fin (course_rendre) */
    if (E.ride < 0 || E.npcs[E.ride].perso != perso) {
        const int s = vehicle_spawn(perso, E.sera.x, E.sera.z, 0);
        if (s < 0) return 0;
        if (E.ride >= 0) E.npcs[E.ride].walking = 0;
        E.ride = -1;
        vehicle_board(s);
        pret = s;
    }
    E.ride_phase = 0;
    E.ride_alt = 0;   /* altitude imposée en course : tous au ras de la piste (render.c), vaisseau compris */
    nia = nia < 0 ? 0 : nia > COURSE_N - 1 ? COURSE_N - 1 : nia;
    niv = niv < 0 ? 0 : niv > 2 ? 2 : niv;
    static const float BASE[3] = { 0.72f, 0.86f, 0.96f }, PAS[3] = { 0.03f, 0.025f, 0.02f };
    float v0, a0;
    veh_params(perso, &v0, &a0);
    memset(&CR, 0, sizeof CR);
    CR.n = nia + 1; CR.tours = tours < 1 ? 1 : tours > 9 ? 9 : tours; CR.armes = armes != 0;
    CR.pret = pret; CR.avant = pret >= 0 ? avant : -1;
    for (int k = 0; k < CR.n; k++) {
        racer_t *r = &CR.r[k];
        const int joueur = k == CR.n - 1;
        const float lv = joueur ? 1 : BASE[niv] - PAS[niv] * k;
        float dir;
        piste_pt(-1.2f - 1.3f * k, (k & 1 ? 1 : -1) * PISTE.hw * 0.4f, &r->x, &r->z, &dir);
        const proj_t q = piste_proj(r->x, r->z, -1);
        const int rv = joueur || !rivaux ? perso : rivaux[k];
        r->joueur = joueur; r->perso = rv >= 0 && rv < E.w.npersos ? rv : perso; r->grip = E.w.veh_grip[r->perso];
        r->dir = dir; r->seg = q.seg; r->s = q.s; r->y = q.y; r->prog = q.s - PISTE.L; r->tour = -1; r->fin = -1;
        r->vmax = v0 * COURSE_V * lv; r->acc = a0 * COURSE_V * lv; r->rb = 1;
        r->off = joueur ? 0 : ((k % 3) - 1) * 0.35f;
        r->vis = r->cam = dir; r->plaque = -1;
    }
    CR.on = 1; CR.phase = CP_GRILLE;
    const racer_t *j = &CR.r[CR.n - 1];
    E.sera.x = j->x; E.sera.z = j->z; E.sera.dir = j->dir; E.yaw = j->dir; E.ride_v = 0; E.has_dest = 0; E.sera_y_ok = 0;
    for (int k = 0; k < TRAFFIC_MAX; k++) E.traffic[k].on = 0;
    return 1;
}

/* Place de la dernière course (255 : annulée, ou aucune) : instruction « si course » du scénario */
static int last_place = 255;
int course_place(void) { return last_place; }
void course_cancel(void) { last_place = 255; }
/* Abandon (menu de pause) : la course s'arrête là, place 255 pour le scénario (« si course abandon ») */
void course_abandon(void) { course_end(); last_place = 255; }
/* Fin : un vaisseau reprend ensuite son altitude de croisière (décollage vertical) */
void course_end(void) {
    const racer_t *j = &CR.r[CR.n - 1];
    last_place = CR.chrono > 0 && (j->fin < 0 || j->fin > CR.chrono) ? 9 : course_rank(NULL);   /* temps à battre raté : 9 */
    CR.on = 0; E.ride_v = 0; essai_pilote = 0; course_allege = 1;
    if (essai_cycle >= 0) { E.cycle = essai_cycle; essai_cycle = -1; }
    if (CR.pret >= 0 && E.ride == CR.pret) course_rendre(CR.pret, CR.avant);
    else if (ride_type() == VEH_VAISSEAU) { E.ride_alt = 0; E.ride_phase = 1; }
    CR.pret = CR.avant = -1;
}

/* Classement : arrivés dans l'ordre d'arrivée, puis les autres par progression ; renvoie la place du joueur (1..) */
int course_rank(int *order) {
    int o[COURSE_N], place = 0;
    for (int k = 0; k < CR.n; k++) o[k] = k;
    for (int a = 1; a < CR.n; a++)
        for (int b = a; b > 0; b--) {
            const racer_t *x = &CR.r[o[b - 1]], *y = &CR.r[o[b]];
            const int swap = x->fin >= 0 || y->fin >= 0 ? (x->fin < 0 ? 1e9f : x->fin) > (y->fin < 0 ? 1e9f : y->fin) : x->prog < y->prog;
            if (!swap) break;
            const int t = o[b]; o[b] = o[b - 1]; o[b - 1] = t;
        }
    for (int k = 0; k < CR.n; k++) { if (order) order[k] = o[k]; if (CR.r[o[k]].joueur) place = k + 1; }
    return place;
}

/* Pilote : vise l'axe, décalé de sa trajectoire, à 1,8 + 0,3 × vitesse cases devant ; s'écarte d'un concurrent juste
   devant ; lève le pied avant un virage serré */
static void pilote(const racer_t *r, float *th, float *st) {
    const float sp = hypotf(r->vx, r->vz), hx = sinf(r->dir), hz = cosf(r->dir), lim = PISTE.hw - 0.6f;
    float off = r->off * PISTE.hw;
    for (int k = 0; k < CR.n; k++) {
        const racer_t *o = &CR.r[k];
        if (o == r) continue;
        const float dx = o->x - r->x, dz = o->z - r->z, f = dx * hx + dz * hz, l = dx * hz - dz * hx;
        if (f > 0 && f < 2 && fabsf(l) < 0.9f) off += l > 0 ? -0.8f : 0.8f;
    }
    off = off < -lim ? -lim : off > lim ? lim : off;
    float gx, gz, c0, c1, x1, z1;
    piste_pt(r->s + 1.8f + 0.3f * sp, off, &gx, &gz, NULL);
    float a = atan2f(gx - r->x, gz - r->z) - r->dir;
    a = atan2f(sinf(a), cosf(a));
    piste_pt(r->s, 0, &x1, &z1, &c0);
    piste_pt(r->s + 3 + 0.4f * sp, 0, &x1, &z1, &c1);
    const float cv = fabsf(atan2f(sinf(c1 - c0), cosf(c1 - c0)));
    *th = cv > 0.9f ? 0.75f : 1;
    *st = a * 2.5f; *st = *st < -1 ? -1 : *st > 1 ? 1 : *st;
}

/* Petit tirage pseudo-aléatoire des effets (0..1) */
static uint32_t fx_rng = 0x2545F491u;
static float frnd(void) { fx_rng ^= fx_rng << 13; fx_rng ^= fx_rng >> 17; fx_rng ^= fx_rng << 5; return (float)(fx_rng >> 8) * (1.0f / 16777216.0f); }
static float wrap(float a) { return atan2f(sinf(a), cosf(a)); }

/* Rotation (w, rad/s) : elle suit le braquage voulu, sauf en tête-à-queue (spin > 0) où elle s'amortit seule, sans
   poussée ni commande, avec peu d'adhérence : le véhicule glisse en tournant sur lui-même. Un dérapage trop fort à vive
   allure (plus de 0,7 + COURSE_TAQ rad entre le cap et la route suivie, sur l'asphalte) ou un choc qui fait tourner vite déclenche un tête-à-queue.
   Choc : impulsion j au point de contact, dans la direction (nx, nz) du centre vers le contact ; elle fait tourner
   (× COURSE_COUP) d'autant plus que le contact est sur un coin, dans le sens qui écarte ce coin ; tête-à-queue
   seulement si le choc lui-même fait tourner de plus de COURSE_COUP_TAQ rad/s (un mur frôlé dans un virage dévie le
   véhicule sans le faire partir). Gravier (au-delà de la chaussée,
   écart de l'image précédente) : moitié de l'accélération et de l'adhérence ; pluie : adhérence réduite de moitié au
   plus fort (intensité E.rain). */
#define SPIN_W 4.5f
/* Tenue de route : adhérence latérale de la surface et du véhicule × COURSE_GRIP ; braquage qui croît avec la vitesse
   (+ COURSE_VIRAGE au-delà de 30 % du maxi) et à fond (aérofrein, + 40 %) ; tête-à-queue au-delà du seuil de la surface
   + COURSE_TAQ (rad). Un virage serré fait déraper (fumée) sans faire partir en tête-à-queue, sauf sur la glace ou sous
   une forte pluie. */
#define COURSE_GRIP 1.6f
#define COURSE_COUP 2.0f
#define COURSE_COUP_TAQ 3.0f
#define COURSE_VIRAGE 1.0f
#define COURSE_TAQ 0.35f
static void tourne(racer_t *r, float w) { r->w = w; if (fabsf(w) > SPIN_W && r->spin <= 0) r->spin = 0.9f; }
static void coup(racer_t *r, float nx, float nz, float j) {
    const float hx = sinf(r->dir), hz = cosf(r->dir), front = nx * hx + nz * hz, side = nx * hz - nz * hx;
    const float dw = -COURSE_COUP * j * front * side;
    r->w += dw;
    if (fabsf(dw) > COURSE_COUP_TAQ && r->spin <= 0) r->spin = 0.9f;
}

static void physique(racer_t *r, float th, float st, float dt) {
    r->gravier = fabsf(r->lat) > PISTE.hw + 0.05f;
    const surface_t *sf = surf();
    const float ab = fmaxf(0, fabsf(st) - 0.8f) / 0.2f, vmax = r->vmax * r->rb * sf->v * (r->boost > 0 ? COURSE_BOOST : 1), acc = r->acc * (r->gravier ? 0.5f : 1);
    float hx = sinf(r->dir), hz = cosf(r->dir);
    const float vf0 = r->vx * hx + r->vz * hz, sp = fminf(1, fabsf(vf0) / (0.3f * vmax));
    if (r->spin > 0) { r->spin -= dt; r->w *= expf(-1.8f * dt); th = 0; }
    else {
        const int vo = E.volant % 5;
        const float wt = st * (1 + COURSE_VIRAGE * sp) * (1 + 0.4f * ab) * (vf0 < -0.1f ? -1 : 1) * (r->joueur ? COURSE_VOLANT[vo] : 1);
        r->w += (wt - r->w) * fminf(1, (r->joueur ? COURSE_VOLANT_VIF[vo] : 12) * dt);
    }
    r->dir = wrap(r->dir + r->w * dt);
    hx = sinf(r->dir); hz = cosf(r->dir);
    float vf = r->vx * hx + r->vz * hz, vl = r->vx * hz - r->vz * hx;
    if (th > 0) vf += acc * (th - fmaxf(0, vf) / vmax) * dt;
    else if (th < 0) vf = vf > 0 ? fmaxf(0, vf + th * acc * 2 * dt) : fmaxf(-0.3f * vmax, vf + th * acc * dt);
    else vf -= vf * 0.35f * dt;
    vf -= vf * 0.6f * ab * dt;
    vf -= 5 * r->pente * dt;   /* pente : ralentit en montée, accélère en descente */
    if (r->boost > 0) { r->boost -= dt; if (vf < vmax) vf = fminf(vmax, vf + COURSE_POUSSEE * dt); }   /* accélération : poussée en plus */
    else if (vf > vmax) vf -= (vf - vmax) * COURSE_RETOUR * dt;   /* après : retour rapide à la vitesse maxi */
    r->halo = r->boost > 0 ? fminf(1, r->halo + 8 * dt) : fmaxf(0, r->halo - 2.5f * dt);   /* halo de l'écran (hud.c) */
    vl *= expf(-(r->spin > 0 ? 1.2f : (r->gravier ? 2.5f : sf->grip) * r->grip * COURSE_GRIP * (1 - 0.5f * E.rain)) * dt);   /* adhérence du véhicule */
    r->vx = hx * vf + hz * vl; r->vz = hz * vf - hx * vl; r->v = vf;
    r->x += r->vx * dt; r->z += r->vz * dt;
    /* Dérapage : angle entre le cap et la route suivie ; trop fort à vive allure, tête-à-queue */
    const float spd = hypotf(r->vx, r->vz), vdir = spd > 0.8f ? atan2f(r->vx, r->vz) : r->dir;
    r->slip = spd > 0.8f && vf > 0 ? wrap(r->dir - vdir) : 0;
    if (r->spin <= 0 && fabsf(r->slip) > sf->spin * sqrtf(r->grip) + COURSE_TAQ && spd > 0.45f * vmax) tourne(r, (r->slip > 0 ? 1 : -1) * SPIN_W * 1.2f);
    /* Sprite : le cap, dérapage accentué (une direction de plus dès 15°) ; caméra : la route suivie (en marche avant et en
       tête-à-queue, où l'on voit le véhicule tourner), sinon le cap */
    const float exa = r->slip * 0.6f;   /* dérapage accentué */
    r->vis = r->spin > 0 ? r->dir : r->dir + (exa < -0.5f ? -0.5f : exa > 0.5f ? 0.5f : exa);
    r->cam = spd > 1.5f && (vf > 0.5f || r->spin > 0) ? vdir : r->dir;
    r->spd = spd;
    const int mv = fabsf(vf) > 0.05f;
    if (r->walking != mv) { r->walking = (uint8_t)mv; r->frame = 0; r->anim = 0; }
    r->anim += dt * (mv ? fminf(1, fabsf(vf) / (vmax * 0.3f)) : 1);
    if (r->anim > (mv ? 0.12f : 0.22f)) { r->anim = 0; r->frame++; }
}

/* Effets : une place libre, sinon la suivante après la dernière prise (la plus ancienne en général) */
void course_effet(int kind, float x, float y, float z, float vx, float vy, float vz, float life, float sol) {
    int k = 0;
    while (k < COURSE_FX && CR.fx[k].t < CR.fx[k].life) k++;
    if (k == COURSE_FX) k = CR.fxk = (CR.fxk + 1) % COURSE_FX;
    course_fx_t *f = &CR.fx[k];
    f->kind = (uint8_t)kind; f->x = x; f->y = y; f->z = z; f->vx = vx; f->vy = vy; f->vz = vz; f->t = 0; f->life = life; f->sol = sol;
}
static void effet(int kind, float x, float y, float z, float vx, float vy, float vz, float life) { course_effet(kind, x, y, z, vx, vy, vz, life, y - 0.3f); }
static void etincelles(float x, float y0, float z, int n, float e) {
    const float y = y0 + 0.25f;
    for (int k = 0; k < n; k++) {
        const float a = frnd() * 2 * PI_F, v = (0.6f + frnd()) * fminf(6, 1.5f + e);
        effet(1, x, y, z, cosf(a) * v, 1.5f + frnd() * 2.5f, sinf(a) * v, 0.25f + frnd() * 0.2f);
    }
}

/* Murs (au bord des bandes de gravier) : le concurrent revient sur la piste, perd sa vitesse vers le mur (rebond de
   40 %) et un peu de l'autre, tourne selon l'angle du choc ; étincelles au contact (quelques-unes aussi en frottant le
   long du mur) ; puis sa progression, ses tours, son arrivée */
static void mur(racer_t *r, float dt) {
    const proj_t q = piste_proj(r->x, r->z, r->seg);
    const float lim = PISTE.hw + (q.pont ? 0 : PISTE.bande) - COURSE_R, L = PISTE.L;
    if (fabsf(q.lat) > lim) {
        const float sg = q.lat > 0 ? 1.0f : -1.0f, over = fabsf(q.lat) - lim, vn = (r->vx * q.nx + r->vz * q.nz) * sg;
        r->x -= q.nx * sg * over; r->z -= q.nz * sg * over;
        const float cx = r->x + q.nx * sg * COURSE_R, cz = r->z + q.nz * sg * COURSE_R;
        if (vn > 0) {
            const float perte = 1 - fminf(0.4f, vn / r->vmax);
            r->vx = (r->vx - q.nx * sg * vn * 1.4f) * perte; r->vz = (r->vz - q.nz * sg * vn * 1.4f) * perte;
            coup(r, q.nx * sg, q.nz * sg, vn);
            if (vn > 0.6f && r->joueur)   /* de face (plus de 3 cases/s vers le mur) : crash */
                son_jouer(vn > 3 ? SON_CRASH : SON_CHOC, fminf(vn > 3 ? 1 : 0.8f, 0.25f + vn / 5), 0.9f + 0.15f * frnd());
            if (vn > 0.5f) etincelles(cx, q.y, cz, (int)fminf(10, 2 + vn * 2), vn);
        }
        const float sp = hypotf(r->vx, r->vz);
        if (sp > 3 && frnd() < dt * 12) etincelles(cx, q.y, cz, 1, 1);
        if (sp > 2 && r->joueur) E.frotte = fminf(1, sp / r->vmax);   /* la tôle frotte le long du mur */
    }
    r->seg = q.seg;
    r->lat = q.lat < -lim ? -lim : q.lat > lim ? lim : q.lat;
    r->y = q.y; r->pente = q.pente * (sinf(r->dir) * q.tx + cosf(r->dir) * q.tz);   /* montée dans le sens du cap */
    float ds = q.s - r->s;
    if (ds > L / 2) ds -= L;
    if (ds < -L / 2) ds += L;
    r->s = q.s; r->prog += ds;
    if (r->joueur) r->inv = ds < -0.002f ? r->inv + dt : 0;
    const int tour = (int)floorf(r->prog / L);
    if (tour > r->tour) {
        r->tour = tour;
        if (tour >= CR.tours && r->fin < 0) {
            r->fin = CR.t;
            if (r->joueur) { CR.phase = CP_ARRIVEE; CR.t_fin = CR.clock; son_jouer(SON_SOIN, 0.6f, 1); }
        } else if (r->joueur && tour >= 1) {
            char t[24];
            if (tour == CR.tours - 1) snprintf(t, sizeof t, "DERNIER TOUR");
            else snprintf(t, sizeof t, "TOUR %d", tour + 1);
            hud_toast(t); son_jouer(SON_MENU, 0.6f, 1.3f);
        }
    }
}

/* Chocs entre concurrents : cercles de rayon COURSE_R, séparés à parts égales, échange de vitesse (rebond de 30 %),
   rotation selon le coin touché (un choc à l'arrière fait partir en tête-à-queue), étincelles entre les deux */
static void chocs(void) {
    const float m = 2 * COURSE_R;
    for (int a = 0; a < CR.n; a++)
        for (int b = a + 1; b < CR.n; b++) {
            racer_t *A = &CR.r[a], *B = &CR.r[b];
            const float dx = B->x - A->x, dz = B->z - A->z, d = hypotf(dx, dz);
            if (d >= m || d < 1e-4f) continue;
            const float nx = dx / d, nz = dz / d, o = (m - d) / 2, rel = (B->vx - A->vx) * nx + (B->vz - A->vz) * nz;
            A->x -= nx * o; A->z -= nz * o; B->x += nx * o; B->z += nz * o;
            if (rel < 0) {
                const float j = -rel * 0.65f;
                A->vx -= nx * j; A->vz -= nz * j; B->vx += nx * j; B->vz += nz * j;
                coup(A, nx, nz, j); coup(B, -nx, -nz, j);
                if (-rel > 0.8f && (A->joueur || B->joueur))   /* carambolage : crash au-delà de 4 cases/s */
                    son_jouer(-rel > 4 ? SON_CRASH : SON_CHOC, fminf(-rel > 4 ? 1 : 0.8f, 0.25f - rel / 6), 1.1f + 0.15f * frnd());
                if (-rel > 0.8f) etincelles((A->x + B->x) / 2, (A->y + B->y) / 2, (A->z + B->z) / 2, (int)fminf(10, 2 - rel * 1.5f), -rel);
            }
        }
}

/* Fumée des dérapages et des tête-à-queue, poussière dans le gravier (une bouffée toutes les 0,08 s à l'arrière ; 0,05 avant
   le 31/10/2026 : disques transparents de l'interface, coûteux) ;
   les effets vivent leur vie (les étincelles retombent et s'éteignent au sol) */
static void effets(float dt) {
    for (int k = 0; k < CR.n; k++) {
        racer_t *r = &CR.r[k];
        const int pous = (r->gravier || surf()->pous) && r->spd > 1.5f;   /* gravier, piste de terre : poussière */
        if (!pous && !(r->spd > 2 && (fabsf(r->slip) > 0.22f || r->spin > 0))) { r->fumee = 0; continue; }
        r->fumee -= dt;
        if (r->fumee > 0) continue;
        r->fumee = 0.08f;
        const float hx = sinf(r->dir), hz = cosf(r->dir), l = (frnd() - 0.5f) * 0.3f, x = r->x - hx * 0.35f + hz * l, z = r->z - hz * 0.35f - hx * l;
        effet(pous ? 2 : 0, x, r->y + 0.12f, z, -r->vx * 0.15f + (frnd() - 0.5f) * 0.3f, 0.35f, -r->vz * 0.15f + (frnd() - 0.5f) * 0.3f, 0.8f);
    }
    course_fx_maj(dt);
}
/* Étincelles et éclats retombent et s'éteignent au sol, feuilles qui planent puis s'y posent, fumée freinée */
void course_fx_maj(float dt) {
    const float drag = expf(-2 * dt);
    for (int k = 0; k < COURSE_FX; k++) {
        course_fx_t *f = &CR.fx[k];
        if (f->t >= f->life) continue;
        f->t += dt; f->x += f->vx * dt; f->y += f->vy * dt; f->z += f->vz * dt;
        if (f->kind == 1 || f->kind == 3) { f->vy -= 9 * dt; if (f->y < f->sol) f->t = f->life; }
        else if (f->kind == 4) { f->vy = fmaxf(f->vy - 3 * dt, -0.8f); f->vx *= drag; f->vz *= drag; if (f->y < f->sol) { f->y = f->sol; f->vy = 0; } }
        else { f->vx *= drag; f->vz *= drag; }
    }
}

/* Plaques : un concurrent qui passe dessus (moins de 0,8 case le long, 0,85 en travers) ; accélération : 0,8 s à 115 % ;
   armes (si la course en a) : une au hasard s'il n'en a pas. Une plaque ne sert qu'une fois par passage. */
static float ecart_s(float a, float b) { const float L = PISTE.L; return fabsf(fmodf(fmodf(a - b, L) + 1.5f * L, L) - L / 2); }
static float plaque_s(int k) { const int i = PISTE.plaques[k * 4] | PISTE.plaques[k * 4 + 1] << 8; return fmodf(PISTE.S[i % PISTE.n] - PISTE.S[PISTE.dep] + PISTE.L, PISTE.L); }
static void plaques(racer_t *r) {
    if (r->plaque >= 0 && ecart_s(r->s, plaque_s(r->plaque)) > 2) r->plaque = -1;
    for (int k = 0; k < PISTE.nplaques; k++) {
        const float lat = (int8_t)PISTE.plaques[k * 4 + 2] / 10.0f;
        if (k == r->plaque || ecart_s(r->s, plaque_s(k)) > 0.8f || fabsf(r->lat - lat) > 0.85f) continue;
        r->plaque = k;
        if (PISTE.plaques[k * 4 + 3] == 0) { r->boost = COURSE_PLAQUE_T; if (r->joueur) son_jouer(SON_TECHNIQUE, 0.5f, 1.5f); }
        else if (CR.armes && !r->arme) {
            static const char *NOM[5] = { "", "MISSILE", "MINE", "BOUCLIER", "TURBO" };
            r->arme = 1 + (int)(frnd() * 4) % 4; r->arme_t = 1 + frnd() * 2;
            if (r->joueur) { hud_toast(NOM[r->arme]); son_jouer(SON_MENU, 0.6f, 1.6f); }
        }
    }
}

/* Tir : missile (vers le premier concurrent devant, à 40 cases au plus, sinon tout droit), mine (posée derrière),
   bouclier (4 s), turbo (1,2 s d'accélération) */
static void tir(int k) {
    racer_t *r = &CR.r[k];
    const int a = r->arme;
    if (!a || CR.phase == CP_GRILLE) return;
    r->arme = 0;
    const float hx = sinf(r->dir), hz = cosf(r->dir);
    if (a == COURSE_ARME_MISSILE) {
        int cible = -1, s = -1; float bg = 40;
        for (int o = 0; o < CR.n; o++) { const float g = CR.r[o].prog - r->prog; if (o != k && g > 0 && g < bg) { bg = g; cible = o; } }
        for (int m = 0; m < 8 && s < 0; m++) if (!CR.mis[m].on) s = m;
        if (s < 0) return;
        const float v = fmaxf(18, r->spd + 8);
        CR.mis[s].on = 1; CR.mis[s].x = r->x + hx * 0.6f; CR.mis[s].z = r->z + hz * 0.6f; CR.mis[s].y = r->y + 0.5f;
        CR.mis[s].vx = hx * v; CR.mis[s].vz = hz * v; CR.mis[s].v = v; CR.mis[s].t = 0; CR.mis[s].owner = k; CR.mis[s].cible = cible;
        son_jouer(SON_ATTAQUE, 0.6f, 1.3f);
    } else if (a == COURSE_ARME_MINE) {
        int s = 0;
        for (int m = 0; m < 12; m++) if (!CR.mines[m].on) { s = m; break; } else if (CR.mines[m].t > CR.mines[s].t) s = m;
        CR.mines[s].on = 1; CR.mines[s].x = r->x - hx * 0.9f; CR.mines[s].z = r->z - hz * 0.9f; CR.mines[s].y = r->y + 0.1f; CR.mines[s].t = 0; CR.mines[s].owner = k;
        son_jouer(SON_BIP, 0.5f, 0.6f);
    } else if (a == COURSE_ARME_BOUCLIER) { r->bouclier = 4; son_jouer(SON_TECHNIQUE, 0.5f, 1); }
    else { r->boost = COURSE_TURBO_T; son_jouer(SON_TECHNIQUE, 0.6f, 1.4f); }
}
void course_tir(void) { if (CR.on && CR.n && !CR.pause) tir(CR.n - 1); }

/* Touché (missile, mine) : le bouclier encaisse ; sinon tête-à-queue, vitesse au tiers, étincelles, explosion */
static void touche(racer_t *r, float x, float z) {
    etincelles(x, r->y, z, 12, 5);
    if (r->bouclier > 0) { r->bouclier = 0; son_jouer(SON_COUP, 0.6f, 1.4f); return; }
    r->vx *= 0.35f; r->vz *= 0.35f; tourne(r, (frnd() < 0.5f ? -1 : 1) * 7.5f);
    son_jouer(SON_EXPLOSION, 0.7f, 1);
    if (r->joueur) hud_toast("TOUCHÉ");
}

/* Missiles (guidés : virent vers leur cible de 3 rad/s au plus, 3,5 s de vol) et mines (actives après 0,6 s, 25 s) ;
   les pilotes tirent : missile s'il y a quelqu'un devant à 25 cases, mine si quelqu'un suit à 6 cases, bouclier
   quand un missile arrive, turbo sur une portion droite */
static void armes(float dt) {
    for (int m = 0; m < 8; m++) {
        if (!CR.mis[m].on) continue;
        __typeof__(&CR.mis[0]) M = &CR.mis[m];
        M->t += dt;
        if (M->cible >= 0) {
            const racer_t *c = &CR.r[M->cible];
            const float cur = atan2f(M->vx, M->vz), da = wrap(atan2f(c->x - M->x, c->z - M->z) - cur);
            const float nd = cur + (da < -3 * dt ? -3 * dt : da > 3 * dt ? 3 * dt : da);
            M->vx = sinf(nd) * M->v; M->vz = cosf(nd) * M->v;
            M->y += (c->y + 0.5f - M->y) * fminf(1, dt * 4);
        }
        M->x += M->vx * dt; M->z += M->vz * dt;
        if (frnd() < dt * 30) effet(0, M->x, M->y, M->z, 0, 0.2f, 0, 0.35f);
        for (int k = 0; k < CR.n; k++)
            if ((k != M->owner || M->t > 0.4f) && hypotf(CR.r[k].x - M->x, CR.r[k].z - M->z) < 0.6f) { touche(&CR.r[k], M->x, M->z); M->t = 99; break; }
        if (M->t >= 3.5f) M->on = 0;
    }
    for (int m = 0; m < 12; m++) {
        if (!CR.mines[m].on) continue;
        CR.mines[m].t += dt;
        if (CR.mines[m].t >= 25) { CR.mines[m].on = 0; continue; }
        if (CR.mines[m].t < 0.6f) continue;
        for (int k = 0; k < CR.n; k++)
            if ((k != CR.mines[m].owner || CR.mines[m].t > 1.5f) && hypotf(CR.r[k].x - CR.mines[m].x, CR.r[k].z - CR.mines[m].z) < 0.6f) {
                touche(&CR.r[k], CR.mines[m].x, CR.mines[m].z); CR.mines[m].on = 0; break;
            }
    }
    for (int k = 0; k < CR.n; k++) {
        racer_t *r = &CR.r[k];
        if (r->bouclier > 0) r->bouclier -= dt;
        if (r->joueur || !r->arme || CR.phase != CP_COURSE) continue;
        r->arme_t -= dt;
        if (r->arme_t > 0) continue;
        r->arme_t = 0.5f;
        int devant = 0, suit = 0, menace = 0;
        for (int o = 0; o < CR.n; o++) {
            const float g = CR.r[o].prog - r->prog;
            if (o != k && g > 0 && g < 25) devant = 1;
            if (o != k && g < 0 && g > -6) suit = 1;
        }
        for (int m = 0; m < 8; m++) if (CR.mis[m].on && CR.mis[m].cible == k && hypotf(CR.mis[m].x - r->x, CR.mis[m].z - r->z) < 8) menace = 1;
        const float avance = sinf(r->dir) * r->vx + cosf(r->dir) * r->vz;
        if ((r->arme == COURSE_ARME_MISSILE && devant) || (r->arme == COURSE_ARME_MINE && suit) || (r->arme == COURSE_ARME_BOUCLIER && menace) ||
            (r->arme == COURSE_ARME_TURBO && fabsf(r->slip) < 0.1f && avance > r->vmax * 0.6f)) tir(k);
    }
}

/* Une image de course (th, st : commandes du joueur, comme la conduite) : feux, pilotes, chocs, murs, effets ; à son arrivée,
   le joueur passe en pilote automatique ; résultats quand tous sont arrivés, au plus 6 s après lui. Sera suit son
   véhicule, la caméra se replace derrière lui. */
/* Course d'essai (eng_essai_course) : le joueur à plein gaz, braqué vers l'axe 6 points devant (course:n de rendu_test) */
static void essai_commande(const racer_t *j, float *th, float *st) {
    const int e = (j->seg + 6) % PISTE.n;
    float a = atan2f(PISTE.X[e] - j->x, PISTE.Z[e] - j->z) - j->dir;
    a = atan2f(sinf(a), cosf(a));
    *th = 1; *st = a > 0.12f ? 1.0f : a < -0.12f ? -1.0f : 0.0f;
}
int eng_essai_course(int piste, int allege) {
    const piste_t *pp = course_piste(piste);
    if (!pp || BT.on || E.in.on) return 0;
    if (CR.on) course_abandon();
    accueil_fin(); script_abort(); hud_battle_start();
    if (E.ride >= 0) ride_lacher();
    if (E.w.carte) world_carte(0, pp->X[pp->dep], pp->Z[pp->dep]);
    /* Joueur : le premier véhicule de course (un BOLIDE s'il y en a) ; rivaux : d'autres planches */
    int moi = -1, riv[COURSE_N - 1], nr = 0;
    for (int p = 0; p < E.w.npersos; p++) {
        if (!(E.w.persos[p].flags & PERS_VEHICULE) || !E.w.veh_course[p]) continue;
        if (moi < 0 || (strstr(E.w.persos[p].nom, "BOLIDE") && !strstr(E.w.persos[moi].nom, "BOLIDE"))) moi = p;
    }
    if (moi < 0) return 0;
    for (int p = 0; p < E.w.npersos && nr < COURSE_N - 1; p++) {
        if (p == moi || !(E.w.persos[p].flags & PERS_VEHICULE) || !E.w.veh_course[p]) continue;
        int vu = E.w.persos[p].sheet == E.w.persos[moi].sheet;
        for (int k = 0; k < nr; k++) vu |= E.w.persos[riv[k]].sheet == E.w.persos[p].sheet;
        if (!vu) riv[nr++] = p;
    }
    E.sera.x = pp->X[pp->dep]; E.sera.z = pp->Z[pp->dep]; E.has_dest = 0; E.sera_y_ok = 0;
    course_choisir(E.sera.x, E.sera.z);
    world_stream(E.sera.x, E.sera.z, 1);
    if (!course_start(moi, 2, riv, nr, 1, 1)) return 0;
    course_allege = allege != 0;
    essai_pilote = 1;
    if (essai_cycle < 0) essai_cycle = E.cycle;
    E.cycle = 1;   /* de jour : mesures comparables (la nuit, la lueur des feux coûte plus que la course) */
    return 1;
}

void course_update(float th, float st, float dt) {
    if (CR.pause) return;
    racer_t *j = &CR.r[CR.n - 1];
    if (essai_pilote) essai_commande(j, &th, &st);
    CR.clock += dt;
    if (CR.phase == CP_GRILLE) {
        const int n = (int)(CR.clock / COURSE_FEUX);
        if (n > CR.feux && n <= 3) son_jouer(SON_BIP, 0.6f, 1);
        CR.feux = n;
        if (n >= 4) { CR.phase = CP_COURSE; CR.t = 0; son_jouer(SON_BIP, 0.8f, 1.6f); }
    } else if (CR.phase != CP_RESULTATS) CR.t += dt;
    if (CR.phase == CP_ARRIVEE) {
        int all = 1;
        for (int k = 0; k < CR.n; k++) all &= CR.r[k].fin >= 0;
        if (all || CR.clock - CR.t_fin > 6) CR.phase = CP_RESULTATS;
    }
    const int go = CR.phase == CP_COURSE || CR.phase == CP_ARRIVEE;
    for (int k = 0; k < CR.n; k++) {
        racer_t *r = &CR.r[k];
        float t = 0, s = 0;
        /* Joueur : direction adoucie (|st| puissance COURSE_EXPO : les petits gestes tournent peu ; force du virage :
           COURSE_VOLANT dans physique) */
        if (go && r->joueur && r->fin < 0) { t = th; s = copysignf(powf(fabsf(st), COURSE_EXPO), st); }
        else if (go) { pilote(r, &t, &s); if (r->fin >= 0) t *= 0.5f; }
        /* Élastique : un adversaire loin devant le joueur ralentit, loin derrière il accélère un peu */
        const float e = (j->prog - r->prog) / 30;   /* devant : jusqu'à 10 % plus lent ; derrière : 4 % plus rapide */
        r->rb = r->joueur ? 1 : 1 + (e < 0 ? fmaxf(-0.1f, e * 0.1f) : fminf(0.04f, e * 0.04f));
        physique(r, t, s, dt);
    }
    chocs();
    for (int k = 0; k < CR.n; k++) { mur(&CR.r[k], dt); if (go) plaques(&CR.r[k]); }
    if (go) armes(dt);
    effets(dt);
    actor_t *sr = &E.sera;   /* Sera : sprite du véhicule (dérapage) ; caméra : la route suivie */
    sr->x = j->x; sr->z = j->z; sr->dir = j->vis; E.ride_v = j->v;
    sr->state = j->walking; sr->frame = j->frame;
    E.yaw += wrap(j->cam - E.yaw) * fminf(1, dt * 3);
}

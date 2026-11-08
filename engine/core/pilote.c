/*
 * Conduite assistée et pilote automatique, hors course (section « Pilote » de l'émulateur : assistVoie, piloteConduire…,
 * à garder identiques).
 *
 * Conduite assistée (SYSTÈME › CONDUITE : ASSISTÉE au départ, ou LIBRE ; E.conduite, gardée dans la sauvegarde ESVF) :
 * voiture, camion et mech en marche avant à plus de ASSIST_V cases/s sur une route ou une rue suivent la voie tant que le
 * joueur ne braque pas, comme une aide au maintien de voie ; il accélère, freine et garde la main. Un braquage de plus de
 * ASSIST_MAIN la lui rend tout entière ; le volant lâché, l'aide revient en ASSIST_RAMPE s, sur la voie la plus proche du
 * cap (à moins de ASSIST_ANGLE) : il tourne à un carrefour ou quitte la route sans qu'elle le ramène. Sur un axe de route
 * (ROUT, à moins de sa demi-largeur + ASSIST_AXE) : la voie de droite, comme le trafic ; dans les rues : la direction la
 * plus dégagée devant (un rayon tous les 5°, jusqu'à ASSIST_RAYONS de chaque côté, sur ASSIST_VUE cases), au milieu de la
 * chaussée, un peu à droite sur une voie d'au moins ASSIST_LARGE cases (bords cherchés à ASSIST_BORD cases au plus ; un
 * côté ouvert, carrefour ou place : la direction seule). Point visé à ASSIST_VISEE + ASSIST_VISEE_V × la vitesse devant,
 * braquage proportionnel à l'écart de cap (ASSIST_GAIN), ASSIST_MAX au plus. Rien hors route, en marche arrière, en
 * course (course.c a sa direction), en vaisseau ni en bateau.
 *
 * Pilote automatique (menu GUIDAGE ou toucher sur la carte du menu, en véhicule ; planète seulement, hors course et hors
 * recherche de la police) : le véhicule va seul par les routes et les rues jusqu'à la destination, à PIL_ALLURE de sa
 * vitesse maxi, plus lentement dans les virages et derrière le trafic ou les passants ; il s'arrête à l'arrivée, ou dès
 * que le joueur touche aux commandes (gaz, frein, volant, au doigt, au clavier ou au stick). Une scène, un choix, un
 * dialogue : il attend, freiné. Deux champs de distances, de la famille de celui des poursuivants de la police (game.c) :
 *  - grossier, sur toute la planète, une fois par destination : cases de PIL_G × PIL_G cases, « voie » (une rue de la
 *    grille des rencontres, ou un axe de route ROUT qui y passe), « urbaine » (bloc urbain ou industriel, PIL_URBAIN fois
 *    plus chère) ou fermée ; distance de chanfrein (2 par pas droit, 3 en biais) depuis les cases à moins de PIL_DEST_G
 *    cases grossières de la destination, à PIL_HORS fois leur distance (élargi jusqu'à PIL_DEST_GMAX pour toucher une
 *    voie : une destination hors des routes) ; passes alternées (avant, arrière) jusqu'à ce que rien ne change. Mémoire :
 *    2 bits et un u16 par case ;
 *  - fin, sur les chunks à PIL_FC chunks de celui de Sera (5 × 5, 80 × 80 cases : la fenêtre des chunks de la montre),
 *    en pas de case (8 voisines) sur les cases de route ou de rue (pas sous un bâtiment, comme les poursuivants) : depuis
 *    les cases de route du bord, chacune à la distance grossière de sa case, et les cases de route à moins de PIL_DEST_R
 *    de la destination, à PIL_HORS fois leur distance à vol d'oiseau ; refait tous les PIL_FIN_D cases parcourues, ou
 *    toutes les PIL_FIN_S s.
 * Le véhicule descend le champ fin (voisine la plus basse ; à égalité, celle qui a le plus de cases de route autour : le
 * milieu de la chaussée, puis la plus droite) et vise le point du chemin à la distance de visée ; sur un axe de route qui
 * va dans le même sens (à moins de PIL_AXE_ANGLE), la voie de droite ; au moins PIL_V_MIN de sa vitesse maxi, pour
 * tourner. Point visé derrière (plus de PIL_DEMI) : il manœuvre en marche arrière, braquage inversé. Gêné à l'arrêt plus
 * de PIL_ATTENTE s par un passant ou un véhicule : il le contourne (point visé décalé de PIL_EVITE_D pendant PIL_EVITE_S
 * s, du côté où il y a de la route), en reculant d'abord s'il le touche. Bloqué PIL_BLOQUE_S s : marche arrière
 * PIL_RECUL_S s, braquage inversé ; au bout de PIL_RECULS essais sans repartir, il s'arrête. Arrivée : à moins du rayon
 * du lieu (PIL_ARRIVEE au moins), ou au bout du chemin sur une graine de la destination (« au plus près » : la route
 * s'arrête avant elle). Vaisseau en vol : tout droit. Bandeau « PILOTE AUTO » et distance restante (hud.c, draw_pilote).
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "internal.h"

pilote_t PIL ENG_FROID;

#define ASSIST_V 0.8f          /* cases/s */
#define ASSIST_MAIN 0.04f      /* braquage du joueur au-delà duquel il a la main */
#define ASSIST_RAMPE 0.5f      /* s */
#define ASSIST_ANGLE 0.6f      /* rad (34°) */
#define ASSIST_AXE 0.6f        /* cases */
#define ASSIST_VOIE 0.9f       /* cases : demi-largeur d'un axe à deux voies (plus étroit : on roule au milieu) */
#define ASSIST_PAS_A 0.0872665f   /* 5° */
#define ASSIST_RAYONS 9
#define ASSIST_VUE 5.0f
#define ASSIST_BORD 3.0f
#define ASSIST_LARGE 3.0f
#define ASSIST_VISEE 1.2f
#define ASSIST_VISEE_V 0.45f
#define ASSIST_GAIN 1.6f
#define ASSIST_MAX 0.8f

#define PIL_G 8                /* cases par case grossière (plus si la planète dépasse PIL_GN_MAX × PIL_G cases) */
#define PIL_GN_MAX 128
#define PIL_URBAIN 2
#define PIL_DEST_G 1
#define PIL_DEST_GMAX 4
#define PIL_HORS 2.0f          /* le dernier bout, hors des routes jusqu'à la destination, compte double */
#define PIL_PASSES 200
#define PIL_FC 2              /* fenêtre fine : les chunks à PIL_FC chunks du chunk de Sera (5 × 5 : celle de la montre) */
#define PIL_FN ((2 * PIL_FC + 1) * CS)
#define PIL_DEST_R 32.0f       /* cases : les cases de route à moins de tant de la destination sont des graines (au plus près) */
#define PIL_FIN_S 1.0f
#define PIL_FIN_D 3.0f
#define PIL_GRAINES 2048
#define PIL_ALLURE 0.7f
#define PIL_ALLURE_VOL 0.8f
#define PIL_GAIN 1.8f
#define PIL_AXE_ANGLE 0.6f
#define PIL_ARRIVEE 3.0f
#define PIL_BLOQUE_S 1.5f
#define PIL_RECUL_S 1.0f
#define PIL_RECULS 4
#define PIL_LIBRE_S 3.0f       /* s à rouler franchement : les essais de dégagement sont oubliés */
#define PIL_DEMI 1.9f          /* rad (110°) : point visé derrière, il manœuvre en marche arrière… */
#define PIL_DEMI_FIN 1.0f      /* … jusqu'à ce qu'il soit à moins de tant devant */
#define PIL_V_MIN 0.2f         /* part de la vitesse maxi au moins, pour tourner (rayon de braquage mini : 0,15 × vmax / 2,4) */
#define PIL_ATTENTE 2.0f       /* s gêné à l'arrêt avant de contourner */
#define PIL_EVITE_S 2.5f
#define PIL_EVITE_D 1.2f       /* cases */
#define PIL_DEGAGE_D 0.5f      /* cases : décalage après un dégagement (contre un mur, un décor) */
#define PIL_EVITE_MARGE 0.35f  /* cases : de côté, pendant l'évitement, ce qui gêne encore */
#define PIL_MARGE 0.25f        /* m : une case dont le centre est à moins de tant d'un décor est fermée */
#define PIL_INF 0xFFFF         /* champ : pas de chemin (fin : case de route pas atteinte) */
#define PIL_NR 0xFFFE          /* champ fin : pas une case de route (terrain, eau) */
#define PIL_DUR 0xFFFD         /* champ fin : fermée par un bâtiment ou un décor (pas de pas en biais entre deux) */
#define FERMEE(v) ((v) == PIL_NR || (v) == PIL_DUR)
#define PIL_EPAULE 3           /* pas vers un accotement (case libre au bord d'une route) : trois fois plus cher */

static struct {
    int ok, gn, gs, cls_ok, f_ok;
    uint8_t *cls;              /* 2 bits par case grossière : 0 fermée, 1 voie, 2 urbaine */
    uint16_t *gd;              /* distance grossière à la destination (chanfrein) */
    uint16_t *fd, *fq;         /* champ fin : distance (pas de case), file */
    uint8_t *ep;               /* champ fin : accotements, un bit par case */
    uint32_t *gr;              /* graines du champ fin : distance << 16 | case locale */
    int fi0, fj0;              /* origine de la fenêtre fine */
    float f_t, f_x, f_z, libre_t, st_recul;
    float attente_t, evite_t, evite_d; int evite_cote, recul_vise;   /* gêné à l'arrêt depuis ; évitement : temps restant, côté ; marche arrière de manœuvre */
} PG;

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ecart(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
static void route_pt(const route_t *r, int i, float *x, float *z) { memcpy(x, r->p + i * 8, 4); memcpy(z, r->p + i * 8 + 4, 4); }

/* Case de route ou de rue, pas sous un bâtiment (champ_route de game.c) */
static int route_case(int i, int j) {
    if (i < 2 || j < 2 || i >= E.w.n - 2 || j >= E.w.n - 2) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c) return 0;
    const int l = cell_local(i, j);
    return (c->flags[l] & (F_ROAD | F_STREET)) && !c->hidden[l];
}
static int route_en(float x, float z) { return route_case((int)floorf(x), (int)floorf(z)); }

/* Mémoire des champs : le tampon commun (celui des images, img_emprunter de world.c, que eng_init fait assez grand),
   emprunté pendant la conduite ; une image demandée pendant ce temps coupe le pilote. 01/11/2026 : avec 69 Ko à lui,
   la PSRAM de la montre n'avait plus la place des images, puis de la musique */
static uint32_t pg_taille;
static int pg_gn(int *gs_) {
    int gs = PIL_G;
    while ((E.w.n + gs - 1) / gs > PIL_GN_MAX) gs++;
    if (gs_) *gs_ = gs;
    return (E.w.n + gs - 1) / gs;
}
static void pg_decouper(uint8_t *m) {
    const int gn = PG.gn;
    uint32_t o = 0;
#define PG_PART(champ, type, n) do { if (m) PG.champ = (type *)(m + o); o += ((uint32_t)(n) + 3) & ~3u; } while (0)
    PG_PART(gd, uint16_t, gn * gn * 2);
    PG_PART(fd, uint16_t, PIL_FN * PIL_FN * 2);
    PG_PART(fq, uint16_t, PIL_FN * PIL_FN * 2);
    PG_PART(gr, uint32_t, PIL_GRAINES * 4);
    PG_PART(cls, uint8_t, (gn * gn + 3) / 4);
    PG_PART(ep, uint8_t, (PIL_FN * PIL_FN + 7) / 8);
#undef PG_PART
    pg_taille = o;
}
uint32_t pilote_taille(void) {
    if (E.w.n <= 0 || (!E.w.nroutes && !E.w.c0.zone)) return 0;
    const int gn0 = PG.gn;
    PG.gn = pg_gn(NULL); pg_decouper(NULL); PG.gn = gn0;   /* taille seulement */
    return pg_taille;
}
void pilote_init(void) {
    memset(&PIL, 0, sizeof PIL); memset(&PG, 0, sizeof PG);
    E.conduite = CONDUITE_DEFAUT;
    if (E.w.n <= 0 || (!E.w.nroutes && !E.w.c0.zone)) return;   /* ni routes ni zones : pas de pilote automatique */
    PG.gn = pg_gn(&PG.gs);
    pg_decouper(NULL);
    PG.ok = pg_taille <= E.w.img_cap;   /* le tampon commun (eng_init le fait assez grand) */
}

/* ---------- Conduite assistée ---------- */
/* Axe de route (ROUT) sous (x, z), à moins de sa demi-largeur + ASSIST_AXE de son tracé : son segment et l'abscisse du
   point le plus proche ; -1 : aucun */
static int axe_proche(float x, float z, int *seg, float *k) {
    int best = -1; float bd = 1e9f;
    for (int q = 0; q < E.w.nroutes; q++) {
        const route_t *r = &E.w.routes[q];
        const float lim = r->hw + ASSIST_AXE;
        for (int s = 0; s + 1 < r->n; s++) {
            float ax, az, bx, bz;
            route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz);
            if (fminf(ax, bx) > x + lim || fmaxf(ax, bx) < x - lim || fminf(az, bz) > z + lim || fmaxf(az, bz) < z - lim) continue;
            const float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
            const float kk = clampf(l2 > 0 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0, 0, 1);
            const float d = hypotf(ax + dx * kk - x, az + dz * kk - z);
            if (d < lim && d < bd) { bd = d; best = q; *seg = s; *k = kk; }
        }
    }
    return best;
}
/* Point à L de l'abscisse k du segment s de l'axe r, dans le sens g, arrêté au bout (route_avance de game.c) */
static void axe_avance(const route_t *r, int s, float k, int g, float L, float *x, float *z) {
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
/* Point de la voie de droite de l'axe q, L cases devant (s, k) dans le sens g : sur l'axe, décalé de la moitié de sa
   demi-largeur à droite du sens de marche (lane_place de game.c) */
static void voie_point(int q, int s, float k, int g, float L, float *x, float *z) {
    const route_t *r = &E.w.routes[q];
    float px, pz, ox, oz, tx, tz;
    axe_avance(r, s, k, g, L, &px, &pz);
    axe_avance(r, s, k, g, L > 0.5f ? L - 0.5f : 0, &ox, &oz);
    tx = px - ox; tz = pz - oz;
    float l = hypotf(tx, tz);
    if (l < 1e-3f) { float ax, az, bx, bz; route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz); tx = (bx - ax) * g; tz = (bz - az) * g; l = hypotf(tx, tz); }
    if (l < 1e-3f) { *x = px; *z = pz; return; }
    const float off = r->hw >= ASSIST_VOIE ? r->hw * 0.5f : 0;   /* un chemin étroit : au milieu */
    *x = px + tz / l * off; *z = pz - tx / l * off;   /* à droite du sens de marche (tx, tz) : (tz, -tx) */
}
/* Voie sous le véhicule : point visé (ax, az) à L devant, 1 axe de route, 2 rue ; 0 : aucune (hors route, ou la voie s'écarte
   du cap de plus de ASSIST_ANGLE : croisement, virage serré) */
static int voie_visee(float x, float z, float dir, float L, float *ax, float *az) {
    const float hx = sinf(dir), hz = cosf(dir);
    int seg = 0; float k = 0;
    const int q = axe_proche(x, z, &seg, &k);
    if (q >= 0) {
        const route_t *r = &E.w.routes[q];
        float px, pz, bx, bz;
        route_pt(r, seg, &px, &pz); route_pt(r, seg + 1, &bx, &bz);
        const float tx = bx - px, tz = bz - pz, tl = hypotf(tx, tz);
        if (tl < 1e-4f) return 0;
        const int g = tx * hx + tz * hz >= 0 ? 1 : -1;
        if (fabsf(ecart(atan2f(tx * g, tz * g) - dir)) > ASSIST_ANGLE) return 0;
        voie_point(q, seg, k, g, L, ax, az);
        return 1;
    }
    if (!route_en(x, z)) return 0;
    /* Rues : longueur dégagée de chaque rayon (tous les 5°, ±ASSIST_RAYONS), la meilleure (un peu moins vers les côtés),
       puis le milieu de la suite de rayons presque aussi longs qui l'entoure */
    float lr[2 * ASSIST_RAYONS + 1];
    int kb = 0; float sb = -1e9f;
    for (int i = -ASSIST_RAYONS; i <= ASSIST_RAYONS; i++) {
        const float a = dir + i * ASSIST_PAS_A, sx = sinf(a), sz = cosf(a);
        float l = 0;
        for (float d = 0.5f; d <= ASSIST_VUE + 1e-3f && route_en(x + sx * d, z + sz * d); d += 0.5f) l = d;
        lr[i + ASSIST_RAYONS] = l;
        const float sc = l - 0.03f * (i < 0 ? -i : i);
        if (sc > sb) { sb = sc; kb = i; }
    }
    const float lb = lr[kb + ASSIST_RAYONS];
    if (lb < 1.0f) return 0;
    int i0 = kb, i1 = kb;
    while (i0 > -ASSIST_RAYONS && lr[i0 - 1 + ASSIST_RAYONS] >= lb - 0.5f) i0--;
    while (i1 < ASSIST_RAYONS && lr[i1 + 1 + ASSIST_RAYONS] >= lb - 0.5f) i1++;
    const float a = dir + (i0 + i1) * 0.5f * ASSIST_PAS_A;
    if (fabsf(ecart(a - dir)) > ASSIST_ANGLE) return 0;
    const float fx = sinf(a), fz = cosf(a), rx = fz, rz = -fx;   /* devant, à droite */
    /* Bords de la chaussée à droite et à gauche (au quart de case) ; un côté ouvert : pas de recentrage */
    float dr = ASSIST_BORD, dl = ASSIST_BORD;
    for (float d = 0.25f; d <= ASSIST_BORD; d += 0.25f) if (!route_en(x + rx * d, z + rz * d)) { dr = d - 0.125f; break; }
    for (float d = 0.25f; d <= ASSIST_BORD; d += 0.25f) if (!route_en(x - rx * d, z - rz * d)) { dl = d - 0.125f; break; }
    float lat = 0;
    if (dr < ASSIST_BORD && dl < ASSIST_BORD) {
        const float w = dr + dl, dr0 = w >= ASSIST_LARGE ? 0.3f * w : 0.5f * w;
        lat = dr - dr0;   /* > 0 : trop à gauche, le point visé passe à droite */
    }
    *ax = x + fx * L + rx * lat; *az = z + fz * L + rz * lat;
    return 2;
}
static void assister(float *th, float *st, float dt) {
    const int t = ride_type();
    if (E.conduite != CONDUITE_ASSISTEE || CR.on || vol_permanent() || t < VEH_VOITURE || t > VEH_MECH || E.ride_coule > 0) { PIL.autorite = 0; return; }
    if (fabsf(*st) > ASSIST_MAIN) { PIL.autorite = 0; return; }   /* le joueur braque : il a la main */
    if (E.ride_v < ASSIST_V || *th < 0) return;
    const float L = clampf(ASSIST_VISEE + ASSIST_VISEE_V * E.ride_v, 1.5f, 4.0f);
    float ax = 0, az = 0;
    const int v = voie_visee(E.sera.x, E.sera.z, E.sera.dir, L, &ax, &az);
    if (!v) return;
    PIL.assist = v; PIL.ax = ax; PIL.az = az;
    PIL.autorite = fminf(1, PIL.autorite + dt / ASSIST_RAMPE);
    const float e = ecart(atan2f(ax - E.sera.x, az - E.sera.z) - E.sera.dir);
    *st += clampf(e * ASSIST_GAIN, -ASSIST_MAX, ASSIST_MAX) * PIL.autorite;
}

/* ---------- Pilote automatique : champ grossier ---------- */
static int cls_de(int q) { return PG.cls[q >> 2] >> ((q & 3) * 2) & 3; }
static void cls_poser(int gi, int gj, int c) {
    if (gi < 0 || gj < 0 || gi >= PG.gn || gj >= PG.gn) return;
    const int q = gj * PG.gn + gi, o = cls_de(q);
    if (o && o <= c) return;   /* une voie l'emporte sur l'urbain */
    PG.cls[q >> 2] = (uint8_t)((PG.cls[q >> 2] & ~(3 << ((q & 3) * 2))) | c << ((q & 3) * 2));
}
/* Classes des cases grossières (une fois) : rues (11), blocs urbains et industriels (8, 10) de la grille des rencontres de
   la planète, puis les axes de route (un point toutes les demi-cases) */
static void cls_faire(void) {
    const int gn = PG.gn, gs = PG.gs, n = E.w.n, zn = E.w.c0.zone_n;
    const uint8_t *zone = E.w.c0.zone;
    memset(PG.cls, 0, (size_t)(gn * gn + 3) / 4);
    for (int zj = 0; zone && zj < zn; zj++)
        for (int zi = 0; zi < zn; zi++) {
            const int t = zone[zj * zn + zi], c = t == 11 ? 1 : t == 8 || t == 10 ? 2 : 0;
            if (c) cls_poser((int)((zi + 0.5f) * n / zn) / gs, (int)((zj + 0.5f) * n / zn) / gs, c);
        }
    for (int q = 0; q < E.w.nroutes; q++) {
        const route_t *r = &E.w.routes[q];
        for (int s = 0; s + 1 < r->n; s++) {
            float ax, az, bx, bz;
            route_pt(r, s, &ax, &az); route_pt(r, s + 1, &bx, &bz);
            const float l = hypotf(bx - ax, bz - az);
            for (float d = 0; d <= l; d += 0.5f) {
                /* sans ternaire : « l > 0 ? d / l : 0 » faisait planter GCC Xtensa (erreur interne, postreload) ; l = 0 : d = 0 */
                const float k = d / fmaxf(l, 1e-6f);
                cls_poser((int)floorf((ax + (bx - ax) * k) / gs), (int)floorf((az + (bz - az) * k) / gs), 1);
            }
        }
    }
    PG.cls_ok = 1;
}
/* Distance grossière à (dx, dz) ; 0 : aucune voie près de la destination */
static int gd_faire(float dx, float dz) {
    const int gn = PG.gn, gs = PG.gs, ci = (int)floorf(dx / gs), cj = (int)floorf(dz / gs);
    for (int q = 0; q < gn * gn; q++) PG.gd[q] = PIL_INF;
    int voie = 0;
    for (int R = PIL_DEST_G; R <= PIL_DEST_GMAX && !voie; R++)
        for (int gj = cj - R; gj <= cj + R; gj++)
            for (int gi = ci - R; gi <= ci + R; gi++) {
                if (gi < 0 || gj < 0 || gi >= gn || gj >= gn) continue;
                const float dg = hypotf((gi + 0.5f) * gs - dx, (gj + 0.5f) * gs - dz) / gs;   /* en cases grossières */
                if (dg > R + 0.5f) continue;
                const int q = gj * gn + gi;
                const uint16_t d = (uint16_t)lroundf(dg * 2 * PIL_HORS);   /* chanfrein : 2 par case grossière */
                if (d < PG.gd[q]) PG.gd[q] = d;
                voie |= cls_de(q) != 0;
            }
    if (!voie) return 0;
    for (int p = 0; p < PIL_PASSES; p++) {
        int ch = 0;
        for (int pass = 0; pass < 2; pass++) {
            const int s = pass ? -1 : 1;   /* avant : voisines déjà vues à gauche et au-dessus ; arrière : l'inverse */
            static const int VI[4] = { -1, -1, 0, 1 }, VJ[4] = { 0, -1, -1, -1 };
            for (int gj = pass ? gn - 1 : 0; gj >= 0 && gj < gn; gj += s)
                for (int gi = pass ? gn - 1 : 0; gi >= 0 && gi < gn; gi += s) {
                    const int q = gj * gn + gi, c = cls_de(q);
                    if (!c) continue;
                    unsigned best = PG.gd[q];
                    for (int v = 0; v < 4; v++) {
                        const int ni = gi + VI[v] * s, nj = gj + VJ[v] * s;
                        if (ni < 0 || nj < 0 || ni >= gn || nj >= gn) continue;
                        const int nq = nj * gn + ni;
                        if (PG.gd[nq] == PIL_INF) continue;
                        const int k = c == 2 || cls_de(nq) == 2 ? PIL_URBAIN : 1;
                        const unsigned cand = PG.gd[nq] + (unsigned)((VI[v] && VJ[v] ? 3 : 2) * k);
                        if (cand < best) best = cand;
                    }
                    if (best < PG.gd[q]) { PG.gd[q] = (uint16_t)best; ch = 1; }
                }
        }
        if (!ch) break;
    }
    return 1;
}
/* Distance grossière (cases) de (x, z) ; < 0 : pas de chemin */
static float gd_cases(float x, float z) {
    const int gi = (int)floorf(x / PG.gs), gj = (int)floorf(z / PG.gs);
    if (gi < 0 || gj < 0 || gi >= PG.gn || gj >= PG.gn || PG.gd[gj * PG.gn + gi] == PIL_INF) return -1;
    return PG.gd[gj * PG.gn + gi] * PG.gs * 0.5f;
}

/* ---------- Pilote automatique : champ fin ---------- */
static void graine(int *ng, int q, float cout) {
    if (*ng >= PIL_GRAINES || cout < 0 || FERMEE(PG.fd[q])) return;
    const uint32_t c = cout > 60000 ? 60000 : (uint32_t)ceilf(cout);
    PG.gr[(*ng)++] = c << 16 | (uint32_t)q;
}
static int graine_cmp(const void *a, const void *b) { const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
/* Parcours depuis les graines (triées ; chacune entre dans la file quand le niveau atteint sa distance), niveau par niveau :
   un pas vers une case de route coûte 1, vers un accotement PIL_EPAULE (trois pas : un jeton qui attend deux niveaux dans
   la file, entrée codée case | attente << 13) ; pas en biais entre deux cases fermées (deux décors, un mur et un décor) ;
   file en anneau de PIL_FN × PIL_FN entrées (pleine : le pas est perdu) */
#define EPAULE_DE(q) (PG.ep[(q) >> 3] >> ((q) & 7) & 1)
static void fin_bfs(int ng) {
    uint32_t *g = PG.gr;
    qsort(g, (size_t)ng, sizeof *g, graine_cmp);
    uint16_t *fd = PG.fd, *fq = PG.fq;
    const int QN = PIL_FN * PIL_FN;
    int qh = 0, qt = 0, si = 0;
#define POUSSER(v) do { if (qt - qh < QN) fq[qt++ % QN] = (uint16_t)(v); } while (0)
    unsigned L = ng ? g[0] >> 16 : 0;
    while (ng) {
        while (si < ng && (g[si] >> 16) <= L) { const int q = (int)(g[si] & 0xFFFF); if (!FERMEE(fd[q]) && fd[q] > (g[si] >> 16)) { fd[q] = (uint16_t)(g[si] >> 16); POUSSER(q); } si++; }
        const int fin = qt;
        if (qh == fin) { if (si >= ng) break; L = g[si] >> 16; continue; }
        while (qh < fin) {
            const int v = fq[qh++ % QN], q = v & 0x1FFF, att = v >> 13;
            if (att) { POUSSER(q | (att - 1) << 13); continue; }   /* jeton d'un accotement : un niveau de plus */
            if (fd[q] != L) continue;   /* dépassée depuis */
            const int li = q % PIL_FN, lj = q / PIL_FN;
            for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
                const int ni = li + di, nj = lj + dj;
                if ((!di && !dj) || ni < 0 || nj < 0 || ni >= PIL_FN || nj >= PIL_FN) continue;
                const int nq = nj * PIL_FN + ni;
                if (FERMEE(fd[nq])) continue;
                if (di && dj && fd[lj * PIL_FN + ni] == PIL_DUR && fd[nj * PIL_FN + li] == PIL_DUR) continue;   /* entre deux décors (pas l'eau d'un pont en biais) */
                if (!EPAULE_DE(nq)) { if (fd[nq] > L + 1) { fd[nq] = (uint16_t)(L + 1); POUSSER(nq); } }
                else if (fd[nq] > L + PIL_EPAULE) { fd[nq] = (uint16_t)(L + PIL_EPAULE); POUSSER(nq | (PIL_EPAULE - 1) << 13); }
            }
        }
        L++;
    }
#undef POUSSER
}
static int fin_local(float x, float z) {
    const int li = (int)floorf(x) - PG.fi0, lj = (int)floorf(z) - PG.fj0;
    return li >= 0 && lj >= 0 && li < PIL_FN && lj < PIL_FN ? lj * PIL_FN + li : -1;
}
/* Case de départ : celle de Sera si elle est atteinte, sinon la case de route atteinte la plus proche à moins de 3 cases ;
   -1 : aucune */
static int fin_depart(float x, float z) {
    const int q0 = fin_local(x, z);
    if (q0 >= 0 && PG.fd[q0] < PIL_DUR) return q0;
    int best = -1; float bd = 9;
    for (int dj = -3; dj <= 3; dj++) for (int di = -3; di <= 3; di++) {
        const int q = fin_local(x + di, z + dj);
        if (q < 0 || PG.fd[q] >= PIL_DUR) continue;
        const float d = hypotf(PG.fi0 + q % PIL_FN + 0.5f - x, PG.fj0 + q / PIL_FN + 0.5f - z);
        if (d < bd) { bd = d; best = q; }
    }
    return best;
}
/* Graines de la destination : les cases de route de la fenêtre à moins de PIL_DEST_R d'elle, à PIL_HORS fois leur distance
   à vol d'oiseau (le véhicule s'en approche par la route autant qu'il peut ; une route qui s'arrête avant elle, un village
   dont les rues ne la rejoignent pas : il s'arrête au plus près) */
static void graines_dest(int *ng) {
    const int i0 = (int)floorf(PIL.x - PIL_DEST_R), i1 = (int)floorf(PIL.x + PIL_DEST_R), j0 = (int)floorf(PIL.z - PIL_DEST_R), j1 = (int)floorf(PIL.z + PIL_DEST_R);
    for (int j = j0 > PG.fj0 ? j0 : PG.fj0; j <= j1 && j < PG.fj0 + PIL_FN; j++)
        for (int i = i0 > PG.fi0 ? i0 : PG.fi0; i <= i1 && i < PG.fi0 + PIL_FN; i++) {
            const float d = hypotf(i + 0.5f - PIL.x, j + 0.5f - PIL.z);
            if (d <= PIL_DEST_R) graine(ng, (j - PG.fj0) * PIL_FN + i - PG.fi0, d * PIL_HORS);
        }
}
/* Case sous un bâtiment (fermée : pas de pas en biais entre deux) */
static int case_dure(int i, int j) {
    const chunk_t *c = i < 0 || j < 0 ? NULL : chunk_of_cell(i, j);
    return c && c->hidden[cell_local(i, j)];
}
/* Case libre hors route (pas sous un bâtiment, pas dans l'eau, pas dans un rectangle de collision) : un accotement possible */
static int case_libre(int i, int j) {
    if (i < 2 || j < 2 || i >= E.w.n - 2 || j >= E.w.n - 2) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    if (!c) return 0;
    const int l = cell_local(i, j);
    if (c->hidden[l] || (c->flags[l] & (F_WATER | F_RIVER))) return 0;
    const float x = i + 0.5f, z = j + 0.5f;
    for (uint32_t k = 0; k < c->nsolid; k++) { const float *r = &c->solids[k * 4]; if (x > r[0] && x < r[2] && z > r[1] && z < r[3]) return 0; }
    return 1;
}
/* Cases de la fenêtre fine : route (PIL_INF, pas encore atteinte) ou fermée (PIL_NR ; PIL_DUR : sous un bâtiment). Une case
   dont le centre est à moins de la marge du véhicule d'un rectangle de collision (décor planté au bord ou au carrefour)
   est fermée (PIL_DUR) : le chemin le contourne (is_road du trafic de game.c ferme de même les cases sous un décor). Accotements (bit de PG.ep) : une case libre hors route qui touche une case de route ;
   on y passe trois fois plus cher (PIL_EPAULE) : le bout d'une route coupée à l'entrée d'un village et ses rues (Secteur 6 :
   une ou deux cases entre elles), un carrefour encombré de décors (Ferrailleurs), sans couper par les trottoirs */
static void fin_cases(void) {
    for (int lj = 0; lj < PIL_FN; lj++)
        for (int li = 0; li < PIL_FN; li++) PG.fd[lj * PIL_FN + li] = route_case(PG.fi0 + li, PG.fj0 + lj) ? PIL_INF : case_dure(PG.fi0 + li, PG.fj0 + lj) ? PIL_DUR : PIL_NR;
    const float m = PIL_MARGE / E.w.ech;   /* une part de la marge du véhicule contre un décor (0,45 m : ride_ok de game.c) */
    for (int cz = PG.fj0 / CS; cz < PG.fj0 / CS + 2 * PIL_FC + 1; cz++)
        for (int cx = PG.fi0 / CS; cx < PG.fi0 / CS + 2 * PIL_FC + 1; cx++) {
            const chunk_t *c = chunk_at(cx, cz);
            for (uint32_t k = 0; c && k < c->nsolid; k++) {
                const float *r = &c->solids[k * 4];
                for (int j = (int)floorf(r[1] - m - 0.5f) + 1; j <= (int)ceilf(r[3] + m - 0.5f) - 1; j++)
                    for (int i = (int)floorf(r[0] - m - 0.5f) + 1; i <= (int)ceilf(r[2] + m - 0.5f) - 1; i++) {
                        const int li = i - PG.fi0, lj = j - PG.fj0;
                        if (li >= 0 && lj >= 0 && li < PIL_FN && lj < PIL_FN) PG.fd[lj * PIL_FN + li] = PIL_DUR;
                    }
            }
        }
    memset(PG.ep, 0, (PIL_FN * PIL_FN + 7) / 8);
    for (int lj = 0; lj < PIL_FN; lj++)
        for (int li = 0; li < PIL_FN; li++) {
            const int q = lj * PIL_FN + li;
            if (PG.fd[q] != PIL_NR) continue;
            int bord = 0;
            for (int dj = -1; dj <= 1 && !bord; dj++) for (int di = -1; di <= 1 && !bord; di++) {
                const int ni = li + di, nj = lj + dj;
                bord = (di || dj) && ni >= 0 && nj >= 0 && ni < PIL_FN && nj < PIL_FN && PG.fd[nj * PIL_FN + ni] == PIL_INF && !EPAULE_DE(nj * PIL_FN + ni);
            }
            if (bord && case_libre(PG.fi0 + li, PG.fj0 + lj)) PG.ep[q >> 3] |= (uint8_t)(1u << (q & 7));
        }
    for (int q = 0; q < PIL_FN * PIL_FN; q++) if (EPAULE_DE(q)) PG.fd[q] = PIL_INF;
}
static void fin_faire(float x, float z) {
    PG.fi0 = ((int)floorf(x) / CS - PIL_FC) * CS; PG.fj0 = ((int)floorf(z) / CS - PIL_FC) * CS;
    PG.f_ok = 1; PG.f_t = 0; PG.f_x = x; PG.f_z = z;
    fin_cases();
    /* Graines : les cases de route du bord, à leur distance grossière, et celles de la destination si elle est dans la
       fenêtre (le plus court peut sortir de la fenêtre et y revenir : sans le bord, un détour dans la fenêtre l'emportait,
       et d'une fenêtre à la suivante le véhicule repartait en arrière) */
    int ng = 0;
    for (int k = 0; k < 4 * (PIL_FN - 1); k++) {
        const int s = k / (PIL_FN - 1), o = k % (PIL_FN - 1);
        const int li = s == 0 ? o : s == 1 ? PIL_FN - 1 : s == 2 ? PIL_FN - 1 - o : 0;
        const int lj = s == 0 ? 0 : s == 1 ? o : s == 2 ? PIL_FN - 1 : PIL_FN - 1 - o;
        const int q = lj * PIL_FN + li;
        if (!FERMEE(PG.fd[q])) graine(&ng, q, gd_cases(PG.fi0 + li + 0.5f, PG.fj0 + lj + 0.5f));
    }
    graines_dest(&ng);
    fin_bfs(ng);
}
/* Cases de route autour de la case locale q (0 à 8 ; pas les accotements) */
static int autour(int q) {
    const int li = q % PIL_FN, lj = q / PIL_FN;
    int n = 0;
    for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        const int ni = li + di, nj = lj + dj;
        n += (di || dj) && ni >= 0 && nj >= 0 && ni < PIL_FN && nj < PIL_FN && PG.fd[nj * PIL_FN + ni] < PIL_DUR && !EPAULE_DE(nj * PIL_FN + ni);
    }
    return n;
}
/* Pas suivant de la descente depuis q (pas précédent hi, hj) : la voisine la plus basse ; à égalité, celle qui a le plus
   de cases de route autour (milieu : centre), puis la plus droite ; -1 : aucune plus basse (une graine) */
static int fin_suivant(int q, int hi, int hj, int milieu) {
    const int li = q % PIL_FN, lj = q / PIL_FN, d = PG.fd[q];
    int best = -1; float bs = -1e9f;
    for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        const int ni = li + di, nj = lj + dj;
        if ((!di && !dj) || ni < 0 || nj < 0 || ni >= PIL_FN || nj >= PIL_FN) continue;
        const int nq = nj * PIL_FN + ni, dn = PG.fd[nq];
        if (dn >= d || (di && dj && PG.fd[lj * PIL_FN + ni] == PIL_DUR && PG.fd[nj * PIL_FN + li] == PIL_DUR)) continue;   /* pas entre deux décors */
        const float sc = -dn * 100.0f + (milieu ? autour(nq) * 10.0f : 0) + (float)(hi * di + hj * dj) / (hypotf((float)hi, (float)hj) * hypotf((float)di, (float)dj) + 1e-3f);
        if (sc > bs) { bs = sc; best = nq; }
    }
    return best;
}
/* Chemin descendu depuis (x, z) : point visé à L le long du chemin (ax, az), point à 2,5 L (fx, fz) ; distance restante
   (cases : le chemin, plus la distance de la graine où il finit) ; 0 : Sera n'est pas sur le champ */
static int fin_chemin(float x, float z, float L, float *ax, float *az, float *fx, float *fz, float *reste, float *bout) {
    int q = fin_depart(x, z);
    if (q < 0) return 0;
    float px = x, pz = z, s = 0;
    int hi = 0, hj = 0, va = 0, vf = 0;
#define PAS_VERS(cx, cz) do { const float cx_ = (cx), cz_ = (cz), d_ = hypotf(cx_ - px, cz_ - pz); \
        if (!va && s + d_ >= L) { const float k_ = d_ > 0 ? (L - s) / d_ : 0; *ax = px + (cx_ - px) * k_; *az = pz + (cz_ - pz) * k_; va = 1; } \
        if (!vf && s + d_ >= 2.5f * L) { const float k_ = d_ > 0 ? (2.5f * L - s) / d_ : 0; *fx = px + (cx_ - px) * k_; *fz = pz + (cz_ - pz) * k_; vf = 1; } \
        s += d_; px = cx_; pz = cz_; } while (0)
    if (fin_local(x, z) != q) PAS_VERS(PG.fi0 + q % PIL_FN + 0.5f, PG.fj0 + q / PIL_FN + 0.5f);   /* hors de la case de départ : son centre d'abord */
    for (int n = 0; n < 4 * PIL_FN; n++) {
        const int nq = fin_suivant(q, hi, hj, n < 12);
        if (nq < 0) break;
        hi = nq % PIL_FN - q % PIL_FN; hj = nq / PIL_FN - q / PIL_FN; q = nq;
        PAS_VERS(PG.fi0 + q % PIL_FN + 0.5f, PG.fj0 + q / PIL_FN + 0.5f);
    }
#undef PAS_VERS
    if (!va) { *ax = px; *az = pz; }
    if (!vf) { *fx = px; *fz = pz; }
    *reste = s + PG.fd[q];
    *bout = s;
    return 1;
}

/* ---------- Pilote automatique ---------- */
int pilote_possible(int dire) {
    const char *m = NULL;
    const int t = ride_type();
    if (E.ride < 0) m = "Monter d'abord dans un véhicule";
    else if (CR.on) m = "Pas pendant une course";
    else if (E.w.carte || vol_permanent()) m = "Pilote automatique : sur la planète seulement";
    else if (POL.niveau) m = "Recherchée : pas de pilote automatique";
    else if (t == VEH_BATEAU) m = "Pas de pilote automatique en bateau";
    else if (t < VEH_VOITURE || t > VEH_VAISSEAU) m = "Pas de pilote automatique ici";
    else if (E.ride_coule > 0) m = "Le véhicule coule";
    else if (!PG.ok) m = "Pilote automatique : mémoire insuffisante";
    if (m && dire) hud_toast(m);
    return !m;
}
void pilote_arreter(const char *msg) {
    if (!PIL.on) return;
    PIL.on = 0; PIL.recul_t = 0; PIL.reculs = 0; PG.f_ok = 0;
    img_rendre(TC_PILOTE);   /* tampon commun rendu */
    if (msg) hud_toast(msg);
}
int pilote_lancer(float x, float z, int guide, const char *nom) {
    if (!pilote_possible(1)) return 0;
    char t[80];
    const float r = guide >= 0 && guide < E.w.nguides ? fmaxf(E.w.guides[guide].r, PIL_ARRIVEE) : PIL_ARRIVEE;
    if (hypotf(x - E.sera.x, z - E.sera.z) <= r) { hud_toast("Déjà sur place"); return 0; }
    if (ride_type() != VEH_VAISSEAU) {
        {   /* champs dans le tampon commun, le temps de la conduite (son contenu n'a pas survécu aux images) */
            uint8_t *m = img_emprunter(TC_PILOTE, pg_taille);
            if (!m) { hud_toast("Pilote automatique : mémoire insuffisante"); return 0; }
            pg_decouper(m); PG.cls_ok = 0;
        }
        if (!PG.cls_ok) cls_faire();
        if (!gd_faire(x, z)) { img_rendre(TC_PILOTE); hud_toast("Pilote automatique : aucune route jusque-là"); return 0; }
        int ok = 0;   /* Sera sur le réseau, ou tout près */
        for (int dj = -1; dj <= 1 && !ok; dj++) for (int di = -1; di <= 1 && !ok; di++) ok = gd_cases(E.sera.x + di * PG.gs, E.sera.z + dj * PG.gs) >= 0;
        if (!ok) { img_rendre(TC_PILOTE); hud_toast("Pilote automatique : rejoindre une route d'abord"); return 0; }
    }
    memset(&PIL, 0, sizeof PIL);
    PIL.on = 1; PIL.x = x; PIL.z = z; PIL.r = r; PIL.guide = guide; PIL.reste = hypotf(x - E.sera.x, z - E.sera.z);
    snprintf(PIL.nom, sizeof PIL.nom, "%s", nom ? nom : "");
    PG.f_ok = 0; PG.libre_t = 0; PG.attente_t = PG.evite_t = 0; PG.recul_vise = 0;
    snprintf(t, sizeof t, "Pilote automatique : %s", PIL.nom);
    hud_toast(t);
    return 1;
}
/* Arrivé (au plus près : la route s'arrête avant la destination) : il freine, le pilote se coupe à l'arrêt */
static void arrivee(void) {
    char t[96];
    const float d = hypotf(PIL.x - E.sera.x, PIL.z - E.sera.z);
    if (d <= PIL.r + 2) snprintf(t, sizeof t, "Arrivée : %s", PIL.nom);
    else snprintf(t, sizeof t, "Au plus près : %s à %d m", PIL.nom, (int)lroundf(d));
    hud_toast(t);
    PIL.on = 2;
}
/* Vitesse voulue derrière ce qui est devant : un véhicule du trafic ou un passant (pnj : les passants comptent) à moins de
   1,2 + 0,9 × v cases devant, à moins de marge de côté (au contact : l'arrêt) ; *cote : côté du plus gênant (1 à droite, -1
   à gauche, 0 : rien) */
static float devant(float v, float marge, int pnj, int *cote) {
    const float hx = sinf(E.sera.dir), hz = cosf(E.sera.dir), lim = 1.2f + 0.9f * v;
    float vc = 1e9f;
    *cote = 0;
#define DEVANT(ox, oz) do { const float dx_ = (ox) - E.sera.x, dz_ = (oz) - E.sera.z, f_ = dx_ * hx + dz_ * hz, l_ = dx_ * hz - dz_ * hx; \
        if (f_ > 0 && f_ < lim && fabsf(l_) < marge && fmaxf(0, (f_ - 1.2f) * 1.8f) < vc) { vc = fmaxf(0, (f_ - 1.2f) * 1.8f); *cote = l_ >= 0 ? 1 : -1; } } while (0)
    for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].on) DEVANT(E.traffic[k].x, E.traffic[k].z);
    for (int k = 0; pnj && k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (k == E.ride || fabsf(n->x - E.sera.x) > lim || fabsf(n->z - E.sera.z) > lim || !npc_present(n)) continue;
        DEVANT(n->x, n->z);
    }
#undef DEVANT
    return vc;
}
static void piloter(float *th, float *st, float top, float vmax, float acc, float dt) {
    const actor_t *s = &E.sera;
    *th = *st = 0;
    if (E.ui_block || script_running() || mise_bloque()) return;   /* une scène, un choix, un dialogue : il attend, freiné */
    if (PIL.on == 2) { if (fabsf(E.ride_v) < 0.1f) pilote_arreter(NULL); return; }
    const float d = hypotf(PIL.x - s->x, PIL.z - s->z), v = fmaxf(0, E.ride_v);
    if (ride_type() == VEH_VAISSEAU) {   /* en vol : tout droit */
        if (E.ride_phase) return;
        if (d <= PIL.r) { arrivee(); return; }
        const float e = ecart(atan2f(PIL.x - s->x, PIL.z - s->z) - s->dir);
        float vc = PIL_ALLURE_VOL * vmax * clampf(1 - fabsf(e) / 1.2f, 0.25f, 1);
        vc = fminf(vc, sqrtf(2 * acc * fmaxf(0, d - PIL.r * 0.5f)));
        *st = clampf(e * PIL_GAIN, -1, 1); *th = top > 0 ? clampf(vc / top, 0, 1) : 0;
        PIL.reste = d; PIL.ax = PIL.x; PIL.az = PIL.z;
        return;
    }
    /* Marche arrière en cours (manœuvre ou dégagement) : braquage inversé, jusqu'à ce que le point visé repasse devant */
    if (PIL.recul_t > 0) { PIL.recul_t -= dt; *th = -0.6f; *st = PG.st_recul; if (PG.recul_vise && fabsf(ecart(atan2f(PIL.ax - s->x, PIL.az - s->z) - s->dir)) < PIL_DEMI_FIN) PIL.recul_t = 0; return; }
    PG.f_t += dt;
    if (!PG.f_ok || PG.f_t >= PIL_FIN_S || hypotf(s->x - PG.f_x, s->z - PG.f_z) >= PIL_FIN_D) {
        const uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
        fin_faire(s->x, s->z);
        if (E.cfg.clock_us) { PIL.us_fin = (int)(E.cfg.clock_us() - t0); if (PIL.us_fin > PIL.us_fin_max) PIL.us_fin_max = PIL.us_fin; }   /* mesure (rendu_test, console de l'ESP32) */
    }
    const float L = clampf(ASSIST_VISEE + ASSIST_VISEE_V * v, 1.5f, 4.0f);
    float ax = 0, az = 0, fx = 0, fz = 0, reste = 0, bout = 0;   /* (GCC : peut-être pas initialisées) */
    if (!fin_chemin(s->x, s->z, L, &ax, &az, &fx, &fz, &reste, &bout)) { pilote_arreter("Pilote automatique : plus de route"); return; }
    PIL.reste = reste;
    /* Arrivé : dans le rayon du lieu, ou au bout du chemin sur une graine de la destination (sa distance est celle à vol
       d'oiseau jusqu'à elle : au plus près) */
    const float dfin = hypotf(PIL.x - fx, PIL.z - fz);
    if (d <= PIL.r || (bout < 1.0f && dfin <= PIL_DEST_R + 1 && fabsf(reste - bout - dfin * PIL_HORS) < 1.5f)) { arrivee(); return; }
    /* Sur un axe de route qui va dans le même sens que le chemin : la voie de droite */
    float lx = 0, lz = 0;
    if (voie_visee(s->x, s->z, atan2f(ax - s->x, az - s->z), L, &lx, &lz) == 1 && fabsf(ecart(atan2f(lx - s->x, lz - s->z) - atan2f(ax - s->x, az - s->z))) < PIL_AXE_ANGLE) { ax = lx; az = lz; }
    /* Évitement en cours : le point visé décalé de PIL_EVITE_D sur le côté choisi */
    if (PG.evite_t > 0) {
        const float a = atan2f(ax - s->x, az - s->z);
        PG.evite_t -= dt; ax += cosf(a) * PG.evite_cote * PG.evite_d; az -= sinf(a) * PG.evite_cote * PG.evite_d;
    }
    PIL.ax = ax; PIL.az = az;
    const float e = ecart(atan2f(ax - s->x, az - s->z) - s->dir), ef = ecart(atan2f(fx - s->x, fz - s->z) - s->dir);
    /* Point visé derrière, à l'arrêt ou presque : marche arrière en braquant à l'inverse (manœuvre) */
    if (fabsf(e) > PIL_DEMI && v < 1.5f) { PIL.recul_t = PIL_RECUL_S; PG.recul_vise = 1; PG.st_recul = e > 0 ? -1.0f : 1.0f; *th = -0.6f; *st = PG.st_recul; return; }
    float vc = PIL_ALLURE * top * clampf(1 - fabsf(e) / 0.9f, 0.3f, 1) * clampf(1 - fabsf(ef) / 1.6f, 0.35f, 1);
    vc = fmaxf(vc, PIL_V_MIN * top);   /* assez d'allure pour tourner */
    vc = fminf(vc, sqrtf(2 * acc * fmaxf(0, reste - 1)) + 0.3f);   /* freine avant l'arrivée */
    int cote;
    const float vd = devant(v, PG.evite_t > 0 ? PIL_EVITE_MARGE : 0.85f, PG.evite_t <= 0, &cote);   /* en le contournant : plus les passants (le choc les écarte de toute façon) */
    vc = fminf(vc, vd);
    /* Gêné à l'arrêt plus de PIL_ATTENTE s (passant planté sur la chaussée, garde d'une porte, véhicule garé) : il le
       contourne, du côté opposé s'il y a de la route, sinon de l'autre */
    if (cote && vd < 0.5f && v < 0.2f) PG.attente_t += dt; else PG.attente_t = 0;
    if (PG.attente_t > PIL_ATTENTE) {
        const float a = atan2f(ax - s->x, az - s->z);
        int c = -cote;
        if (!route_en(s->x + sinf(a) * 1.5f + cosf(a) * c * PIL_EVITE_D, s->z + cosf(a) * 1.5f - sinf(a) * c * PIL_EVITE_D)) c = -c;
        PG.evite_cote = c; PG.evite_t = PIL_EVITE_S; PG.evite_d = PIL_EVITE_D; PG.attente_t = 0;
        /* collé contre lui : d'abord en arrière, le nez tourné vers ce côté (en marche arrière, le braquage s'inverse) */
        if (vd <= 0.05f) { PIL.recul_t = PIL_RECUL_S; PG.recul_vise = 0; PG.st_recul = (float)-c; }
    }
    *st = clampf(e * PIL_GAIN, -1, 1); *th = top > 0 ? clampf(vc / top, 0, 1) : 0;
    PIL.st_prec = *st;
    /* Bloqué (contre un mur, un décor) : il recule pour se dégager ; trop d'essais sans repartir, il s'arrête */
    if (vc > 0.5f && fabsf(E.ride_v) < 0.15f) PIL.bloque_t += dt; else PIL.bloque_t = 0;
    if (E.ride_v > 1.0f && (PG.libre_t += dt) > PIL_LIBRE_S) PIL.reculs = 0;
    if (PIL.bloque_t > PIL_BLOQUE_S) {
        if (++PIL.reculs > PIL_RECULS) {   /* tout près de la destination : arrivé au plus près ; sinon il s'arrête */
            if (d <= PIL_DEST_R) arrivee(); else pilote_arreter("Pilote automatique : bloqué");
            return;
        }
        PIL.recul_t = PIL_RECUL_S; PIL.bloque_t = 0; PG.libre_t = 0; PG.recul_vise = 0; PG.st_recul = -*st;
        PG.evite_cote = PIL.reculs & 1 ? -1 : 1; PG.evite_t = PIL_EVITE_S; PG.evite_d = PIL_DEGAGE_D;   /* puis il repart un peu de côté, à gauche puis à droite d'un essai à l'autre */
    }
}

/* ---------- Conduite au point touché (SYSTÈME › COMMANDES : POINTER ; 04/11/2026, demande de l'utilisateur : « le
   véhicule va dans la direction du point de touche, comme Sera ») ----------
   Le doigt posé sur le sol (hud.c : pilote_doigt à chaque image, le point suit le doigt), le véhicule y va : braquage
   proportionnel à l'écart de cap (PIL_GAIN), à DOIGT_ALLURE de sa vitesse maxi au plus, fois la part donnée par hud.c
   (distance du doigt au véhicule à l'écran : DOIGT_PLEIN du rayon de l'écran pour toute l'allure ; le doigt près du
   véhicule, on manœuvre au pas, DOIGT_LENT au moins), plus lentement dans les virages et derrière le trafic ou les
   passants (devant) ; le doigt levé, il finit d'y aller, freiné avant le point, et s'arrête à DOIGT_ARRIVEE (doigt
   tenu, pas de freinage pour le point : il avance avec la caméra). Point derrière (plus de PIL_DEMI) : à moins de
   DOIGT_RECUL_D cases, il y recule ; plus loin, à l'arrêt ou presque, il manœuvre en marche arrière, braquage inversé
   (PIL_RECUL_S s au plus, ou jusqu'à ce que le point repasse à moins de PIL_DEMI_FIN devant) ; un vaisseau tourne sans
   reculer. Le doigt levé, un point à côté (plus de DOIGT_COTE d'écart de cap, moins de PIL_DEMI, à moins de deux fois DOIGT_ARRIVEE) est
   atteint : le rayon de braquage ne permet pas mieux. Bloqué DOIGT_BLOQUE_S s : par un passant ou un véhicule devant, il
   renonce au point ; contre un mur ou un décor, il se dégage une fois à l'inverse (PIL_RECUL_S s, braquage inversé), la
   seconde il renonce, comme Sera (BLOQUE_S de game.c) ; essais oubliés après PIL_LIBRE_S s à rouler. Clavier, stick, autres
   commandes, une course ou un autre véhicule : le point est oublié. Ni conduite assistée ni pilote automatique pendant ce
   temps (un doigt posé coupe le pilote). piloteDoigt… de l'émulateur, à garder identiques */
#define DOIGT_ALLURE 0.8f
#define DOIGT_LENT 0.3f
#define DOIGT_PLEIN 0.6f       /* part du rayon de l'écran (hud.c) */
#define DOIGT_ARRIVEE 0.8f     /* cases */
#define DOIGT_RECUL_D 4.0f     /* cases */
#define DOIGT_BLOQUE_S 1.0f
#define DOIGT_COTE 1.0f        /* rad (57°) */
static struct { int on, tenu, ride, reculs, vise; float x, z, part, bloque_t, libre_t, recul_t, th_recul, st_recul; } PD;

void pilote_doigt(float x, float z, float part) {
    if (PIL.on) pilote_arreter("Pilote automatique coupé");
    if (!PD.on || PD.ride != E.ride) { PD.reculs = 0; PD.recul_t = 0; PD.bloque_t = 0; PD.libre_t = 0; }
    PD.on = 1; PD.tenu = 1; PD.ride = E.ride; PD.x = x; PD.z = z; PD.part = clampf(part / DOIGT_PLEIN, DOIGT_LENT, 1);
}
void pilote_doigt_tenu(int tenu) { PD.tenu = tenu; }
int pilote_doigt_cible(float *x, float *z) {
    if (!PD.on || PD.ride != E.ride) return 0;
    *x = PD.x; *z = PD.z;
    return 1;
}
static void doigt_conduire(float *th, float *st, float top, float acc, float dt) {
    const actor_t *s = &E.sera;
    *th = *st = 0;
    if (E.ui_block || script_running() || mise_bloque()) return;   /* une scène, un choix, un dialogue : il attend, freiné */
    const int vaisseau = ride_type() == VEH_VAISSEAU;
    if (vaisseau && E.ride_phase) return;   /* décollage, atterrissage */
    const float d = hypotf(PD.x - s->x, PD.z - s->z), v = E.ride_v, e = ecart(atan2f(PD.x - s->x, PD.z - s->z) - s->dir);
    /* Marche arrière en cours (manœuvre), ou dégagement à l'inverse */
    if (PD.recul_t > 0) {
        PD.recul_t -= dt; *th = PD.th_recul; *st = PD.st_recul;
        if (PD.vise && fabsf(e) < PIL_DEMI_FIN) PD.recul_t = 0;
        return;
    }
    if (d <= DOIGT_ARRIVEE || (!PD.tenu && d <= 2 * DOIGT_ARRIVEE && fabsf(e) > DOIGT_COTE && fabsf(e) < PIL_DEMI)) {   /* arrivé (ou à côté) : il freine ; le doigt levé, le point est oublié à l'arrêt */
        *th = v > 0.3f ? -0.5f : v < -0.3f ? 0.5f : 0;
        if (!PD.tenu && fabsf(v) < 0.3f) PD.on = 0;
        return;
    }
    const float allure = DOIGT_ALLURE * top * PD.part;
    const float frein = PD.tenu ? 1e9f : sqrtf(2 * acc * fmaxf(0, d - DOIGT_ARRIVEE)) + 0.3f;   /* le doigt levé : freine avant le point */
    float vc = 0, vd = 1e9f;   /* allure voulue (cases/s), limite derrière ce qui est devant */
    if (fabsf(e) > PIL_DEMI && !vaisseau && d < DOIGT_RECUL_D) {
        /* Tout près derrière : il y recule (en marche arrière, l'arrière va du côté opposé au braquage) */
        vc = fminf(allure, frein);
        *st = clampf(-ecart(e - PI_F) * PIL_GAIN, -1, 1);
        *th = top > 0 ? -clampf(vc / top, 0, 1) : 0;
    } else if (fabsf(e) > PIL_DEMI && !vaisseau && v < 1.5f) {
        /* Plus loin derrière, à l'arrêt ou presque : marche arrière en braquant à l'inverse, le nez vers le point */
        PD.recul_t = PIL_RECUL_S; PD.vise = 1; PD.th_recul = -0.6f; PD.st_recul = e > 0 ? -1.0f : 1.0f;
        *th = PD.th_recul; *st = PD.st_recul;
        return;
    } else {
        vc = allure * clampf(1 - fabsf(e) / 0.9f, 0.3f, 1);
        vc = fmaxf(vc, PIL_V_MIN * top);   /* assez d'allure pour tourner */
        vc = fminf(vc, frein);
        int cote;
        vd = devant(fmaxf(0, v), 0.85f, 1, &cote);   /* derrière le trafic et les passants */
        *st = clampf(e * PIL_GAIN, -1, 1); *th = top > 0 ? clampf(fminf(vc, vd) / top, 0, 1) : 0;
    }
    /* Bloqué : par ce qui est devant, il renonce ; contre un mur, un décor, un dégagement à l'inverse, puis il renonce */
    if (vc > 0.5f && fabsf(E.ride_v) < 0.15f) PD.bloque_t += dt; else PD.bloque_t = 0;
    if (fabsf(E.ride_v) > 1.0f && (PD.libre_t += dt) > PIL_LIBRE_S) PD.reculs = 0;
    if (PD.bloque_t > DOIGT_BLOQUE_S) {
        PD.bloque_t = 0; PD.libre_t = 0;
        if (vd < 0.5f || PD.reculs++ || vaisseau) { PD.on = 0; *th = *st = 0; return; }
        PD.recul_t = PIL_RECUL_S; PD.vise = 0; PD.th_recul = *th > 0 ? -0.6f : 0.6f; PD.st_recul = -*st;
    }
}

void pilote_conduire(float *th, float *st, float top, float vmax, float acc, float dt) {
    PIL.assist = 0;
    /* Point touché : un autre véhicule, d'autres commandes, une course, le clavier ou le stick l'oublient */
    if (PD.on && (PD.ride != E.ride || E.commandes != COMMANDES_POINTER || CR.on || fabsf(*th) > 0.05f || fabsf(*st) > 0.05f)) PD.on = 0;
    if (PIL.on) {
        if (fabsf(*th) > 0.05f || fabsf(*st) > 0.05f) pilote_arreter("Pilote automatique coupé");   /* le joueur reprend la main */
        else if (!pilote_possible(0)) pilote_arreter(POL.niveau ? "Recherchée : pilote automatique coupé" : "Pilote automatique coupé");
        else { piloter(th, st, top, vmax, acc, dt); return; }
    }
    if (PD.on) { doigt_conduire(th, st, top, acc, dt); return; }
    assister(th, st, dt);
}

/* Banc d'essai : grille grossière (côté, cases par case, classes sur 2 bits, distances) et fenêtre fine (origine, distances) */
int pilote_champs(int *gn, int *gs, const uint8_t **cls, const uint16_t **gd, int *fi0, int *fj0, int *fn, const uint16_t **fd) {
    *gn = PG.gn; *gs = PG.gs; *cls = PG.cls; *gd = PG.gd; *fi0 = PG.fi0; *fj0 = PG.fj0; *fn = PIL_FN; *fd = PG.fd;
    return PG.ok;
}
/* Bandeau du pilote : « PILOTE AUTO · 1,2 km » ; 0 : coupé */
int pilote_bandeau(char *t, int n) {
    if (!PIL.on) return 0;
    const float d = PIL.reste;
    if (PIL.on == 2) snprintf(t, n, "PILOTE AUTO · ARRIVÉE");
    else if (d < 1000) snprintf(t, n, "PILOTE AUTO · %d m", (int)lroundf(d));
    else { const int hm = (int)lroundf(d / 100); snprintf(t, n, "PILOTE AUTO · %d,%d km", hm / 10, hm % 10); }
    return 1;
}

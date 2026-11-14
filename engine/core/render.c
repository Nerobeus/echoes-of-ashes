/*
 * Rendu par bandes (portage du rastériseur de l'émulateur)
 *
 * eng_build_frame : caméra, puis chaque primitive visible (polygone projeté, sprite, ombre) est rangée dans
 * l'un des OTN seaux de la table d'ordre selon sa profondeur ; la liste est ensuite aplatie du fond vers l'avant.
 * eng_render_band : ciel, puis les primitives qui touchent la bande, dans cet ordre (algorithme du peintre).
 * Dehors, un tampon de profondeur de la taille d'une bande (zb, 1/z) corrige l'ordre quand il se trompe de plus de
 * 1/64 de profondeur (voir Z_REL). Textures 8 bits, couleur finale = cmap[lumière][brouillard][index] en RGB565.
 * Correction de perspective : u/z, v/z, 1/z interpolés, texture exacte tous les SUB pixels.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if ENG_RECT
#include <stdio.h>   /* eng_diag_rendu (LDK) */
#endif
#include "internal.h"
#include "ui.h"

/* Profilage de la préparation (ENG_PROFILE) : cycles de chaque étape de eng_build_frame, voir eng_profile_build */
#ifdef ENG_PROFILE
#include "esp_cpu.h"
static uint32_t bprof[BPROF_N], bprof_t;
#define BP0() (bprof_t = esp_cpu_get_cycle_count())
#define BP(k) do { const uint32_t t_ = esp_cpu_get_cycle_count(); bprof[k] += t_ - bprof_t; bprof_t = t_; } while (0)
#else
#define BP0() ((void)0)
#define BP(k) ((void)0)
#endif

#if ENG_DYN || ENG_PREMIUM
/* Écran choisi à l'exécution (eng_set_screen : app Android plein écran ; version PC premium) : tableaux à la taille maximale */
static int RW = ENG_W, RH = ENG_H_PX;
#else
#define RW ENG_W
#define RH ENG_H_PX
#endif
#define RW_MAX ENG_W
#define RH_MAX ENG_H_PX
#define CXF (RW / 2.0f)
#define CYF (RH / 2.0f)
#define NEAR_Z 0.4f
/* Plein écran (téléphone) et version PC premium : distance de vue doublée (VUE_K), assez de mémoire pour les primitives de plus */
#if ENG_DYN || ENG_PREMIUM
#define VUE_K 2.0f
#define FAR_Z 80.0f
#else
#define VUE_K 1.0f
#define FAR_Z 40.0f
#endif
#define OTN 512
/* Tampon de profondeur : clé ZK / z sur 16 bits (z ≥ NEAR_Z : 62 500 au plus), qui grandit vers la caméra. Un pixel
   déjà peint ne l'est de nouveau que par une surface plus proche de plus de 1/64 (Z_PLUS) : en deçà, l'ordre de la
   table décide (une route sur son sol, une affiche sur son mur, un auvent contre sa façade) ; au-delà, la profondeur
   (un mur long devant une façade courte, le sol d'une butte devant un bâtiment, un personnage devant un mur). Même
   règle dans l'émulateur (zVu, Z_REL), qui dessine du fond vers l'avant */
/* Version PC premium (ENG_PREMIUM) : clé sur 32 bits, ZS fois plus fine (ZK × ZS) ; les personnages, ombres et anneaux gardent
   la leur sur 16 bits dans prim_t.tile (ZK_PRIM / z), multipliée par ZS pour la comparer au tampon */
#if ENG_PREMIUM
typedef uint32_t zcle_t;
#define ZS 16
#else
typedef uint16_t zcle_t;
#define ZS 1
#endif
#define ZK_PRIM 25000.0f
#define ZK (ZK_PRIM * ZS)
int eng_sans_z;   /* banc d'essai (rendu_test « sansz ») : tampon de profondeur coupé, l'ordre de la table seul */
#define Z_PLUS(k) ((k) + ((k) >> 6))
/* Roulis de la caméra (14/11/2026 : vue cockpit de la course anti-gravité, le dévers de la piste) : versions de la course seulement
   (build_retrofw.js et build_web.js --antigrav), rien de plus dans to_view ailleurs ; marge AG_ROUL_M des tables du ciel penché */
#ifndef ENG_ROULIS
#define ENG_ROULIS 0
#endif
#define AG_ROUL_M (ENG_ROULIS ? (RW_MAX + RH_MAX) / 2 + 32 : 0)
#define LV 16
#define FV 8
#if ENG_RECT && !ENG_PREMIUM && !ENG_ROULIS
#define SUB 32   /* écran rectangulaire (LDK, 11/11/2026, choix de l'utilisateur : « perspective allégée ») : une division tous les 32 pixels */
#else   /* (course anti-gravité, 14/11/2026, retour de l'utilisateur : « des textures de bâtiment ou de mur qui glissent, elles bougent en
           fonction de la distance d'affichage » : 16 pixels, et le seuil affine bas, AFFINE_RATIO) */
#define SUB 16
#endif
#if ENG_DYN || ENG_PREMIUM
#define VIEW_R 38                 /* rayon de vue à l'échelle 1 (taille des tableaux) ; à l'échelle ech, VIEW_R / ech */
#define MAX_PRIMS 18000
#define MAX_VERTS 72000
#else
#define VIEW_R 19                 /* rayon de vue à l'échelle 1 (taille des tableaux) ; à l'échelle ech, VIEW_R / ech */
/* Pires cas mesurés le 29/10/2026 (rendu_test sans plafond, -DMAX_PRIMS=30000 -DMAX_VERTS=120000 : les 644 intérieurs et
   140 scènes de rues, campagne, combats, course) : 4 686 primitives et 14 078 sommets dans le plus meublé des intérieurs
   (3 sur 644 dépassent 3 000 primitives), 1 400 et 4 266 dehors ; environ 28 % de marge. Les deux images en double en
   PSRAM : 1,6 Mo (2,1 Mo avec 6 000 et 24 000). À remesurer si les intérieurs s'enrichissent.
   Mesure du 06/11/2026, depuis les zooms fixes (rendu_test pires, piresrue, maxstats) : 1 391 primitives et 4 252 sommets dans le pire des 816 intérieurs,
   248 et 939 dehors à Lucerne, 337 et 1 305 en caméra de scène, 376 et 1 459 en course ; 4 000 et 12 000 gardent de quoi voir le plus meublé des intérieurs
   au zoom libre (code 1111), et rendent 520 Ko de PSRAM */
#ifndef MAX_PRIMS
#define MAX_PRIMS 4000
#endif
#ifndef MAX_VERTS
#define MAX_VERTS 12000
#endif
#endif
#define T_WATER 5
#define T_BED 17                 /* eau claire : fond (sable et galets) */
#define T_SURF 18                /* eau claire : surface (reflets ; texel 0 : le corps de l'eau, tramé selon la profondeur) */
#define T_RUST 8
#define T_METAL 9
#define T_LEVRE 19                      /* bord relevé néon d'une piste de course (T.LEVRE de l'émulateur) */
#define T_RED 11
#define T_DARK 15
#define T_WHITE 16                      /* tuile blanche de l'émulateur (Sentinelle touchée) */

static float FOCAL;
/* Centre de projection : celui de l'image, décalé de quelques pixels pendant une secousse du combat (toute la 3D bouge,
   comme l'image décalée de l'émulateur ; l'interface reste en place) */
static float pcx, pcy;                  /* posés par render_init, puis à chaque image */
int eng_dlg_hors; float eng_dlg_x, eng_dlg_z;
/* Plan vide (07/11/2026, revue des caméras) : pendant une réplique, caméra de scène posée sur du sol nu (zone d'événement qui n'a rien à montrer), personne dans la bande visible
   et aucun bâtiment à moins de PLAN_BAT cases du point : la caméra va sur Sera (eng_dlg_hors), planVide de l'émulateur */
#define PLAN_BAT 1.5f
static int plan_bat;
#define DLG_DECAL (140.0f / ENG_SCALE)   /* pixels natifs */
#define DLG_VITE 4.0f
/* Celui qui parle, dans l'espace libre entre les portraits (130 pixels au milieu de la bande) : DLG_GAP d'écart au plus avec le centre, pieds au-dessus du panneau (DLG_PIED),
   tête dans l'écran (DLG_TETE) ; l'image glisse du minimum, en DLG_SUIT, et pas au-delà de DLG_X_MAX, DLG_Y_MAX (trop loin : elle ne bouge pas) */
#define DLG_GAP (40.0f / ENG_SCALE)
#define DLG_PIED (166.0f / ENG_SCALE)
#define DLG_TETE (10.0f / ENG_SCALE)
#define DLG_X_MAX (170.0f / ENG_SCALE)
#define DLG_Y_MAX (90.0f / ENG_SCALE)
#define DLG_SUIT 5.0f
/* Distances divisées par l'échelle du monde (w->ech), fixées par render_init :
   view_r   rayon de vue autour de Sera, en cases
   fog_z0   profondeur où le brouillard commence ; fog_k : niveaux de brouillard par unité de profondeur
   fog_full fog_of atteint son maximum : au-delà, un polygone a la couleur du fond
   lod_dist au-delà (distance du centre du bloc à Sera), un bloc de 2 × 2 cases devient un polygone
   plant_r  rayon autour de Sera où les plantes sont dessinées */
static int view_r;
static float fog_z0, fog_k, fog_full, lod_dist, plant_r;
/* Distance d'affichage selon le zoom (01/11/2026, demande de l'utilisateur ; distK de l'émulateur) : le zoom est fixe
   selon le lieu (ZOOM_DEHORS, ZOOM_VILLE de internal.h) ; dehors, un cran plus loin (DIST_DEHORS), en ville, où l'on
   voit de plus près, un peu moins (DIST_VILLE) ; entre les deux pendant la transition du zoom ; 1 en course, en combat,
   en vol permanent et dans un intérieur. Rayon de vue (borné par VIEW_R, la taille des tableaux), brouillard, sol
   simplifié et herbes (PLANT_R, 16 au lieu de 13 depuis le même jour : on voyait les décors apparaître) */
#define DIST_DEHORS 1.2f
#define DIST_VILLE 0.85f
/* Zooms auxquels ces distances ont été réglées (le 01/11/2026 : 200 % dehors, 300 % en ville) ; depuis le 07/11/2026, 300 % partout
   (ZOOM_DEHORS) : DIST_VILLE partout, on voit aussi près dehors qu'en ville */
#define DIST_Z_LOIN 2.0f
#define DIST_Z_PRES 3.0f
#define PLANT_R 16.0f
static float view_r0, lod_dist0, plant_r0, dist_cur = 1;   /* valeurs pour dist_k = 1 (render_init) */
static float dist_k(void) {
    if (CR.on || BT.on || E.in.on || vol_permanent()) return 1;
    float t = (E.zoom - DIST_Z_LOIN) / (DIST_Z_PRES - DIST_Z_LOIN);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return DIST_DEHORS + (DIST_VILLE - DIST_DEHORS) * t;
}
static void dist_maj(void) {
    dist_cur = dist_k();
    const int r = (int)ceilf(view_r0 * dist_cur);
    view_r = r > VIEW_R ? VIEW_R : r;
    lod_dist = lod_dist0 * dist_cur; plant_r = plant_r0 * dist_cur;
}
/* Pluie de cendre : brouillard plus dense, CENDRE_BRUME fois l'intensité de plus au plus fort (même début, fin plus
   proche) ; fog_kc, fog_fullc : ceux de l'image en préparation (eng_build_frame ; brume de l'émulateur) */
#define CENDRE_BRUME 0.5f
/* En course (31/10/2026, images par seconde) : sol simplifié en blocs de 2 × 2 cases dès COURSE_LOD × lod_dist (piste 0 :
   200 -> 150 triangles de sol par image, sans différence visible à la vitesse de la course ; rendu_test course:n). Un
   brouillard 1,25 fois plus court, essayé avec (800 -> 650 triangles en tout), raccourcissait trop la vue : retiré. */
#ifndef COURSE_LOD
#define COURSE_LOD 0.6f
#endif
int course_allege = 1;
static float fog_kc, fog_fullc, fog_z0c;
/* En ville (E.ville_v, game.c) : le brouillard ne commence qu'aux VILLE_RECUL de sa profondeur ; il cache encore la limite
   de la vue (VILLE_RECUL de l'émulateur) */
#define VILLE_RECUL 0.7f
static float cam_dist_cur, int_noir_k;   /* distance de la caméra à Sera ; intérieur éteint : niveaux d'obscurité par unité */
/* Hauteur la plus basse du sol soumis à l'image en préparation (cases et blocs du rayon de vue, surface de l'eau
   comprise) : plan des trous de draw_sky (08/11/2026) */
static float sol_bas;
/* Profondeur sur un octet (flou de profondeur) : DEP_K par unité de distance à la caméra, 254 au plus (255 : ciel) */
#if ENG_DOF || ENG_DYN
#define DEP_K 6.0f
static inline uint8_t dep_of(float z) { const float d = z * DEP_K; return (uint8_t)(d < 0 ? 0 : d > 254 ? 254 : d); }
#endif
static int16_t span_l[RH_MAX], span_r[RH_MAX];
static uint16_t *cmap;                  /* LV × FV × 256 */

/* Sommet projeté : position écran, u/z, v/z, 1/z, lumière, brouillard */
typedef struct { float x, y, qu, qv, w, l, f; } sv_t;

enum { K_POLY, K_SPRITE, K_SHADOW, K_ANNEAU };
typedef struct {
    uint8_t kind, alpha, nv;
    uint8_t fade;                       /* polygone : fondu d'un bâtiment devant Sera (0 : opaque, sinon 1..16 / 16 pixels sautés) */
    uint16_t tile;                      /* polygone : texture ; sprite : clé de profondeur de ses pieds (ZK / z) */
    uint16_t remap;                     /* table de couleurs (E.w.remap : plus de 256 avec les palettes des personnages, PVAR) */
    int16_t miny, maxy;
    int32_t next;
    uint32_t v0;                        /* polygone : premier sommet */
    float sx, sy, s, ry;                /* sprite : position, échelle, ry > 0 : coupé à cette ligne (surface d'un bassin) ; ombre : rx = s, ry */
    uint8_t col, row, L, F, sera;
    uint8_t teinte;                     /* sprite : membre transformé (ÉVEIL) + 1, teinté de son aura (eveil_aura) ; 0 : aucun */
    uint16_t sheet;                     /* sprite : 0 = planche commune, k + 1 = planche propre k (SPRS) */
    uint16_t tissu;                     /* sprite : table de la variante de couleur sur le tissu de la planche (0 : aucune) */
#if ENG_DOF
    uint8_t zd;                         /* sprite : profondeur (voir worker_t.dep) */
#endif
} prim_t;


#define RAIN_MAX 200                    /* gouttes et éclaboussures par image, à pleine intensité */

/* Répartition par bande : chaque primitive est notée une fois dans les bandes qu'elle touche, dans l'ordre de
   dessin ; une bande ne relit que les siennes (les primitives sont en PSRAM : les relire toutes à chaque bande
   saturait le cache). Index en SRAM. */
#define NBANDS ((RH + ENG_BAND - 1) / ENG_BAND)
#define NBANDS_MAX ((RH_MAX + ENG_BAND - 1) / ENG_BAND)
#if ENG_PREMIUM
#define BIN_CAP 60000                   /* version PC premium : jusqu'à 1080 lignes, 18 000 primitives (bin_first sur 16 bits) */
#elif ENG_DYN
#define BIN_CAP 16384                   /* écran haut : plus de bandes par primitive */
#else
#define BIN_CAP 4096
#endif
/* Bande en cours de dessin, une par cœur : lignes [by0, by1), colonnes [bx0, bx0 + bstride), pixels de Sera
   (silhouette), bande 3D interne du mode doublé, compteurs */
#define SIL_MAX 2048
typedef struct {
    uint16_t *band; int by0, by1, bx0, bstride;
    int sil_n; uint16_t *sil_o, *sil_v; uint8_t *sil_e;   /* SIL_MAX chacun, en PSRAM : quelques centaines de pixels par image */
    uint16_t *buf;
    uint8_t *cov;                /* couverture de la bande : bit 0 pixel déjà peint, bit 1 ombre en attente, bits 2 à 7 : créneau de teinte en attente (surface d'un bassin) */
    uint8_t *fon;                /* un bit par pixel de la bande : peint par une face en fondu (bâtiment devant Sera) ; la lueur des feux le laisse (lum_band) */
    uint8_t *per;                /* île (ile.c) : un bit par pixel de la bande, peint par un personnage qui n'est pas Sera (sa silhouette ne compte que le décor) */
    uint16_t *att;               /* couleur en attente d'une face en fondu, translucide (créneaux ATT_SLOT0 à 63 du masque : fondu_px) */
    zcle_t *zb;                  /* profondeur du pixel peint (clé ZK / z), lue seulement là où le bit 0 de cov est posé : jamais effacée */
#if ENG_DOF
    uint8_t *dep;                /* profondeur du premier peint de chaque pixel (DEP_K par unité, 255 : ciel), pour le flou de profondeur */
#endif
    uint16_t teinte[64]; uint8_t teinte_a[64]; int nteinte;   /* teintes en attente de la bande : couleur, opacité sur 16 (créneau 0 : aucune) */
    uint32_t prof[PROF_N];       /* profilage (ENG_PROFILE) : cycles par poste, voir eng_profile */
    int px, segs;
} worker_t;
static worker_t *WK[ENG_WORKERS];

/* Données d'une image, en double : le cœur 0 prépare l'image suivante (B) pendant que le cœur 1 dessine la
   précédente (RF) ; eng_present échange les deux. Sur une plateforme à un seul fil, B et RF se suivent. */
/* Lueur d'un feu à l'écran (lum_setup) : flamme (pixels du rendu interne), profondeur, rayon (cases, pixels), gain, couleur ;
   LUM_DRAW lumières au plus par image (8 le temps des lampadaires éclairés : leur lueur, de 5 cases de rayon, faisait
   tomber la montre à 5 images par seconde la nuit en ville ; retirés de LUMIERES le 31/10/2026) */
#ifndef LUM_DRAW
#define LUM_DRAW 4
#endif
/* phare : faisceau des phares (phare_setup) : ellipsoïde le long de fx, fy, fz (axe du véhicule dans le repère de la
   caméra, fy dans le sens des lignes de l'image), inverses des carrés de ses demi-axes (ia2 le long, il2 de côté) */
typedef struct { float sx, sy, z, r, rp, g, fx, fy, fz, ia2, il2; uint8_t cr, cg, cb, phare; } lum_vue_t;
typedef struct {
    sv_t *verts; prim_t *prims; int32_t *order;
    int nverts, nprims, norder;
    uint16_t *bin_idx, bin_first[NBANDS_MAX + 1];
    int bin_ok;
    int ui_cover;                             /* ligne native d'où l'interface couvre tout (dialogue opaque) : pas de 3D dessous, 0 : aucune */
    int sera_faded;                           /* un bâtiment en fondu passe devant Sera : silhouette sans intérieur */
    int inside;                               /* image d'un intérieur : fond sombre, ni ciel ni pluie */
    int noir;                                 /* montre verrouillée (hud_verrou_on) : son cadran seul, bandes noires sans 3D */
    int ciel;                                 /* ciel de la carte (0 celui du monde, 1 souterrain, 2 espace : étoiles, 3 autre planète) */
    int torche;                               /* lampe torche allumée (torche_on) : halo autour de Sera, torche_band */
    int ztest;                                /* tampon de profondeur : dehors (un intérieur est soumis dans son ordre exact) */
    float torche_x, torche_y;                 /* centre du halo (Sera), en pixels du rendu interne */
    int nlum;                                 /* lueur des feux de l'image (lum_setup) : flamme à l'écran, profondeur, rayon */
    lum_vue_t lum[LUM_DRAW + 1];              /* + les phares du véhicule du joueur */
    /* Pluie de l'image : intensité, gouttes (traits obliques) et éclaboussures (len 0), en pixels du rendu interne */
    float rain;
    int nrain;
    struct { int16_t x, y; int8_t len, dx; } drops[RAIN_MAX];
    float sky_hy;                             /* horizon de l'image (ligne) */
    float trou_hk, trou_z0, trou_k;           /* fond sous l'horizon (draw_sky, trous) : profondeur × (ligne - horizon), brouillard */
    /* Horizon de la course anti-gravité (ag_image) : pixels du panorama (0 : le ciel), palettes voilées (4 bandes de 256), largeur
       (360°), hauteur, ligne posée sur l'horizon, lignes du panorama par ligne de l'image, colonne du panorama de chaque colonne */
    const uint8_t *hz_px; const uint16_t *hz_pal; int hz_w, hz_h, hz_base; float hz_k; const uint16_t *hz_u;
    const uint8_t *ck_px; const uint16_t *ck_pal; const int16_t *ck_u; int ck_w, ck_h; float ck_y, ck_k; uint8_t ck_dep;   /* cadre du cockpit (ag_cockpit_band) */
    int roule; float roul_c, roul_s, roul_x, roul_y; const uint16_t *ciel_tab;   /* caméra penchée (ENG_ROULIS) : cos, sin, centre de projection, couleur du ciel par ligne non penchée */
    eng_stats_t stats;
    int px_w[ENG_WORKERS], seg_w[ENG_WORKERS];   /* pixels et segments de chaque cœur */
    uint32_t us3d_w[ENG_WORKERS], usui_w[ENG_WORKERS];
} frame_t;
static frame_t FR[2], *B = &FR[0], *RF = &FR[0];
#if ENG_RECT
/* Diagnostic de la LDK (11/11/2026 : plantage, FR[x].bin_idx devenu nul en cours de jeu) : les pointeurs des deux images,
   relevés par render_init, comparés à chaque étape par la plateforme (eng_diag_intact) et écrits au plantage (eng_diag_rendu) */
static const void *diag_fr[2][4];
int eng_diag_intact(void) {
    if (RF != &FR[0] && RF != &FR[1]) return 0;
    if (B != &FR[0] && B != &FR[1]) return 0;
    for (int k = 0; k < 2; k++)
        if (diag_fr[k][0] && (FR[k].verts != diag_fr[k][0] || FR[k].prims != diag_fr[k][1] || FR[k].order != diag_fr[k][2] || FR[k].bin_idx != diag_fr[k][3])) return 0;
    return 1;
}
int eng_diag_rendu(char *s, int n) {
    int o = snprintf(s, (size_t)n, "RF %p B %p FR %p", (void *)RF, (void *)B, (void *)FR);
    for (int k = 0; k < 2 && o < n; k++)
        o += snprintf(s + o, (size_t)(n - o), " | FR%d %p %p %p %p (%p) ok %d prims %d ordre %d", k, (void *)FR[k].verts, (void *)FR[k].prims,
                      (void *)FR[k].order, (void *)FR[k].bin_idx, diag_fr[k][3], FR[k].bin_ok, FR[k].nprims, FR[k].norder);
    return o;
}
#endif
static int32_t ot_head[OTN], ot_tail[OTN];

/* ---------- Couleurs ---------- */
static inline uint16_t pack565(int r, int g, int b) {
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return E.cfg.swap565 ? (uint16_t)((c >> 8) | (c << 8)) : c;
}
/* Pixel d'un sprite transformé (ÉVEIL) : poussé vers la couleur de l'aura (ar, ag, ab) selon sa luminosité, éclairci : la
   lumière des armures des cut-ins ; les ombres restent plus sombres (teinteAura de l'émulateur) */
static inline uint16_t teinte565(uint16_t c0, int ar, int ag, int ab) {
    const uint16_t c = E.cfg.swap565 ? (uint16_t)((c0 >> 8) | (c0 << 8)) : c0;
    const int r = (c >> 11) << 3, g = (c >> 5 & 63) << 2, b = (c & 31) << 3, k = 96 + ((r * 2 + g * 5 + b) >> 3) * 3 / 4;
    return pack565((r + (ar * k >> 8)) / 2 + 24, (g + (ag * k >> 8)) / 2 + 24, (b + (ab * k >> 8)) / 2 + 24);
}

/* Cycle jour / nuit (ambianceA de l'émulateur) : heure 0..1 (0 minuit, 0,5 midi). L'ambiance est interpolée entre nuit,
   aube, jour et crépuscule : haut et horizon du ciel, brouillard (blanc le jour ; la nuit, celui du monde), lumière (part
   et teinte des couleurs non lumineuses), nuit (fenêtres de la ville du ciel, ligne d'horizon néon) */
typedef struct { float h, top[3], hor[3], fog[3], lum, tint[3], nuit; int fog_monde; } amb_t;
#define AMB_NUIT(h) { h, { 6, 5, 26 }, { 156, 33, 86 }, { 0, 0, 0 }, 0.62f, { 0.85f, 0.9f, 1.15f }, 1, 1 }
#define AMB_JOUR(h) { h, { 78, 146, 222 }, { 222, 232, 244 }, { 226, 232, 240 }, 1, { 1, 1, 1 }, 0, 0 }
static const amb_t AMB[8] = {
    AMB_NUIT(0), AMB_NUIT(0.2f),
    { 0.28f, { 52, 58, 120 }, { 248, 160, 120 }, { 220, 172, 160 }, 0.86f, { 1.1f, 0.97f, 0.92f }, 0.3f, 0 },
    AMB_JOUR(0.36f), AMB_JOUR(0.64f),
    { 0.72f, { 58, 36, 104 }, { 246, 118, 84 }, { 206, 142, 140 }, 0.8f, { 1.15f, 0.92f, 0.88f }, 0.4f, 0 },
    AMB_NUIT(0.8f), AMB_NUIT(1)
};
static amb_t amb;          /* ambiance de la table des couleurs (fog, lum, tint) et du ciel (top, hor, nuit) */
static int amb_ok;

static void ambiance_de(float h, amb_t *a) {
    int k = 0;
    while (k + 2 < 8 && AMB[k + 1].h <= h) k++;
    const amb_t *A = &AMB[k], *Bk = &AMB[k + 1];
    float t = (h - A->h) / (Bk->h - A->h);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    t = t * t * (3 - 2 * t);
    for (int c = 0; c < 3; c++) {
        const float fa = A->fog_monde ? E.w.fog_rgb[c] : A->fog[c], fb = Bk->fog_monde ? E.w.fog_rgb[c] : Bk->fog[c];
        a->top[c] = A->top[c] + (Bk->top[c] - A->top[c]) * t;
        a->hor[c] = A->hor[c] + (Bk->hor[c] - A->hor[c]) * t;
        a->fog[c] = fa + (fb - fa) * t;
        a->tint[c] = A->tint[c] + (Bk->tint[c] - A->tint[c]) * t;
    }
    a->lum = A->lum + (Bk->lum - A->lum) * t;
    a->nuit = A->nuit + (Bk->nuit - A->nuit) * t;
}

/* Table lumière × brouillard, comme buildCmap de l'émulateur (lumière et brouillard de l'ambiance) */
static void build_cmap(const uint8_t *only, int count) {
    const world_t *w = &E.w;
    if (!amb_ok) { ambiance_de(0, &amb); amb_ok = 1; }
    for (int L = 0; L < LV; L++) for (int F = 0; F < FV; F++) {
        float lf = (0.16f + 0.84f * (L / 15.0f)) * amb.lum, ff = (F / 7.0f) * 0.94f;
        int row = (L * FV + F) << 8;
        for (int k = 0; k < (only ? count : 256); k++) {
            int i = only ? only[k] : k;
            const int f1 = w->full[i];
            float m = f1 ? 1.0f : lf;
            const uint8_t *c = w->pal[i];
            cmap[row | i] = pack565((int)lroundf(c[0] * m * 1.04f * (f1 ? 1 : amb.tint[0]) * (1 - ff) + amb.fog[0] * ff),
                                    (int)lroundf(c[1] * m * (f1 ? 1 : amb.tint[1]) * (1 - ff) + amb.fog[1] * ff),
                                    (int)lroundf(c[2] * m * 1.06f * (f1 ? 1 : amb.tint[2]) * (1 - ff) + amb.fog[2] * ff));
        }
    }
}

/* Nouvelle heure : le ciel suit à chaque image ; la table des couleurs (32 768 entrées) n'est refaite que si le
   brouillard, la lumière ou la teinte ont changé de façon visible (jamais pendant le plein jour ni la pleine nuit) */
/* Intérieur (ambianceInterieur de l'émulateur) : sa lumière à lui, pas celle de l'heure. Éclairé : blanche, un peu
   chaude. Éteint (E.in.sombre) : sombre et bleutée, un peu plus claire le jour (les fenêtres), et l'obscurité gagne au-delà
   de Sera (fog_of : brouillard de la couleur du fond, INT_FOND) */
static const float INT_FOND[3] = { 12, 10, 18 };
/* Boîte de nuit (E.in.club, 03/11/2026, demande de l'utilisateur) : sombre et violette, les éclairs de couleur
   (club_maj de game.c, draw_club de hud.c) l'illuminent par instants */
#define CLUB_LUM 0.42f
static void ambiance_interieur(amb_t *a, int sombre) {
    static const float T_ECL[3] = { 1.06f, 1, 0.9f }, T_SOMBRE[3] = { 0.8f, 0.9f, 1.2f }, T_CLUB[3] = { 1.08f, 0.78f, 1.25f };
    for (int c = 0; c < 3; c++) { a->fog[c] = INT_FOND[c]; a->tint[c] = E.in.club ? T_CLUB[c] : sombre ? T_SOMBRE[c] : T_ECL[c]; }
    a->lum = E.in.club ? CLUB_LUM : sombre ? 0.4f + 0.25f * (1 - a->nuit) : 1;
}
void render_ambiance(float h) {
    amb_t a;
    ambiance_de(h, &a);
    if (E.in.on && !BT.on) ambiance_interieur(&a, E.in.sombre);
    else if (E.w.ciel == 3) {   /* autre planète : lumière de l'heure, ciel et brouillard de sa couleur (assombris la nuit) */
        const float k = 1 - 0.75f * a.nuit;
        for (int c = 0; c < 3; c++) { a.fog[c] = E.w.fog_rgb[c] * k; a.hor[c] = a.fog[c]; a.top[c] = a.fog[c] * 0.5f; }
    }
    else if (E.w.ciel) {   /* souterrain, espace : leur lumière et leur brouillard, pas ceux de l'heure */
        static const float T_SOUS[3] = { 0.92f, 0.95f, 1.1f }, T_ESP[3] = { 1, 1, 1.05f };
        for (int c = 0; c < 3; c++) { a.fog[c] = E.w.fog_rgb[c]; a.tint[c] = E.w.ciel == 1 ? T_SOUS[c] : T_ESP[c]; }
        a.lum = E.w.ciel == 1 ? 0.7f : 0.9f; a.nuit = 0;
    }
    else {   /* pluie de cendre : brouillard gris comme le ciel (draw_sky), par paliers de 1/8 (la table des couleurs n'est
                refaite qu'à chaque palier, 8 fois au plus quand l'averse arrive) ; ambianceMaj de l'émulateur */
        const float q = floorf(cendre_dehors() * 8 + 0.5f) / 8;
        for (int c = 0; c < 3; c++) a.fog[c] = a.fog[c] * (1 - 0.6f * q) + 24 * q * (c == 2 ? 1.2f : 1);
    }
    /* Écran titre : le monde assombri derrière la page (accueil_voile), par la table des couleurs, sans pixel de plus */
    const float voile = accueil_voile();
    if (voile > 0 && voile < 1) {
        const float k = 1 - voile;
        a.lum *= k;
        for (int c = 0; c < 3; c++) { a.fog[c] *= k; a.top[c] *= k; a.hor[c] *= k; }
    }
    float d = fabsf(a.lum - amb.lum) * 200;
    for (int c = 0; c < 3; c++) { d = fmaxf(d, fabsf(a.fog[c] - amb.fog[c])); d = fmaxf(d, fabsf(a.tint[c] - amb.tint[c]) * 200); }
    if (!amb_ok || d > 1.5f) { amb = a; amb_ok = 1; build_cmap(NULL, 0); }
    else { for (int c = 0; c < 3; c++) { amb.top[c] = a.top[c]; amb.hor[c] = a.hor[c]; } amb.nuit = a.nuit; }
}
void render_palette_changed(const uint8_t *indices, int count) { build_cmap(indices, count); }

/* Générateur de l'émulateur (pour la même silhouette de ville au ciel) */
static uint32_t rng_a;
static float rng_next(void) {
    rng_a += 0x6D2B79F5u;
    uint32_t a = rng_a, t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) / 4294967296.0);
}

static void vc_alloc(void);
static int torche_on(void);
static void lum_setup(int bat);
/* Brouillard : il commence à FOG_DEBUT et cache tout à FOG_FIN (unités du rayon de vue, × VUE_K / ech) ; FOG_DEBUT, FOG_FIN, fogDeb, fogFin de l'émulateur.
   Concentré au loin le 08/11/2026 (demande de l'utilisateur : 9 avant) : la même distance d'affichage, l'horizon aussi opaque, moins de pixels
   à mêler (brouillard lissé : deux teintes par pixel entre les deux) */
#ifndef FOG_DEBUT
#define FOG_DEBUT 15.0f
#endif
#define FOG_FIN 27.0f

void render_init(void) {
    FOCAL = (ENG_RECT ? CYF : CXF) / tanf(25.0f * PI_F / 180.0f);   /* (écran rectangulaire : la hauteur de vue de la montre, plus large) */
    pcx = CXF; pcy = CYF;
    const float ech = E.w.ech;
    view_r0 = VIEW_R / ech;
    fog_z0 = FOG_DEBUT * VUE_K / ech; fog_k = 8.0f * ech / ((FOG_FIN - FOG_DEBUT) * VUE_K); fog_full = FOG_FIN * VUE_K / ech;
    fog_kc = fog_k; fog_fullc = fog_full; fog_z0c = fog_z0;
    int_noir_k = 8.0f * ech / 6.0f;   /* intérieur éteint : noir 6 m derrière Sera */
    lod_dist0 = 9.0f * (VUE_K > 1 ? 1.5f : 1) / ech; plant_r0 = PLANT_R * (VUE_K > 1 ? 1.5f : 1) / ech;
    dist_cur = 1; view_r = (int)ceilf(view_r0) > VIEW_R ? VIEW_R : (int)ceilf(view_r0); lod_dist = lod_dist0; plant_r = plant_r0;
    for (int y = 0; y < RH; y++) {
#if ENG_ROUND
        float dy = y + 0.5f - CYF, r = RW / 2.0f;
        if (fabsf(dy) < r) { float hw = sqrtf(r * r - dy * dy); span_l[y] = (int16_t)ceilf(CXF - hw - 0.5f); span_r[y] = (int16_t)floorf(CXF + hw - 0.5f); }
        else { span_l[y] = 1; span_r[y] = 0; }
#else
        span_l[y] = 0; span_r[y] = RW - 1;
#endif
    }
    /* Réservations rapides (SRAM interne de l'ESP32 s'il en reste), des plus utiles aux moins utiles (08/11/2026 : la table
       des couleurs, 64 Ko, demandée d'abord, ne trouvait jamais sa place avec la manette, et les tampons touchés à chaque
       pixel venaient après) : tampons de bande de chaque fil, masque, profondeur puis couleur (fil 0 d'abord : le cœur 1 de
       la montre, qui dessine le plus de bandes), puis les listes des bandes, puis la table des couleurs */
    for (int k = 0; k < ENG_WORKERS; k++) {
        WK[k] = E.cfg.alloc(sizeof(worker_t), 1);
        if (WK[k]) {
            WK[k]->cov = E.cfg.alloc(RW_MAX * ENG_BAND, 1);
            WK[k]->zb = E.cfg.alloc(RW_MAX * ENG_BAND * sizeof(zcle_t), 1);
            WK[k]->buf = E.cfg.alloc(RW_MAX * ENG_BAND * 2, 1);
            WK[k]->fon = E.cfg.alloc((RW_MAX * ENG_BAND + 7) / 8, 0);
            WK[k]->per = E.cfg.alloc((RW_MAX * ENG_BAND + 7) / 8, 0);
            WK[k]->att = E.cfg.alloc(RW_MAX * ENG_BAND * 2, 0);
            if (!WK[k]->zb) WK[k]->zb = E.cfg.alloc(RW_MAX * ENG_BAND * sizeof(zcle_t), 0);   /* plus de SRAM interne : en PSRAM (une bande tient dans le cache) */
            WK[k]->sil_o = E.cfg.alloc(SIL_MAX * 2, 0); WK[k]->sil_v = E.cfg.alloc(SIL_MAX * 2, 0); WK[k]->sil_e = E.cfg.alloc(SIL_MAX, 0);
#if ENG_DOF
            WK[k]->dep = E.cfg.alloc(RW_MAX * ENG_BAND, 1);
            if (!WK[k]->dep) { E.error = "mémoire insuffisante (profondeur)"; return; }
#endif
        }
        if (!WK[k] || !WK[k]->buf || !WK[k]->cov || !WK[k]->zb || !WK[k]->fon || !WK[k]->per || !WK[k]->att || !WK[k]->sil_o || !WK[k]->sil_v || !WK[k]->sil_e) { E.error = "mémoire insuffisante (bandes)"; return; }
    }
    for (int k = 0; k < 2; k++) {
        FR[k].verts = E.cfg.alloc(sizeof(sv_t) * MAX_VERTS, 0);
        FR[k].prims = E.cfg.alloc(sizeof(prim_t) * MAX_PRIMS, 0);
        FR[k].order = E.cfg.alloc(sizeof(int32_t) * MAX_PRIMS, 0);
        FR[k].bin_idx = E.cfg.alloc(BIN_CAP * 2, 1);
        if (!FR[k].verts || !FR[k].prims || !FR[k].order || !FR[k].bin_idx) { E.error = "mémoire insuffisante (rendu)"; return; }
#if ENG_RECT
        diag_fr[k][0] = FR[k].verts; diag_fr[k][1] = FR[k].prims; diag_fr[k][2] = FR[k].order; diag_fr[k][3] = FR[k].bin_idx;
#endif
    }
    cmap = E.cfg.alloc(LV * FV * 256 * 2, 1);
    if (!cmap) { E.error = "mémoire insuffisante (rendu)"; return; }
    build_cmap(NULL, 0);
    rng_a = 99;
    vc_alloc();
}

void eng_span(int y, int *x0, int *x1) { *x0 = span_l[y]; *x1 = span_r[y]; }

/* Décors à orientation fixe (07/11/2026, retour de jeu : carcasses et rochers tournaient avec la caméra, à chaque virage en véhicule) : rochers et objets (drapeau 16 du décor ; arbres, buissons,
   herbes : on ne voit pas qu'ils pivotent). Le décor n'a qu'une image, sur une carte verticale : huit orientations d'après sa phase, rel = lacet de la caméra - celui de la carte, ramené à un quart
   de tour ; jusqu'à FIXE_TIENT, la carte ne bouge pas dans le monde (et ne devient jamais une ligne) ; au-delà, elle revient vite face à la caméra et repart sur la carte perpendiculaire, sans à-coup
   sauf le miroir des deux quarts de tour du fond. Lacets de repos de la caméra (multiples de 45°, cam_pas) : rel = ± 22,5°, jamais à la bascule. Même calcul dans fixeMaj de l'émulateur */
#define FIXE_TIENT (PI_F / 6)
static float fixe_ax[8], fixe_az[8];
static uint8_t fixe_mir[8];
static void fixe_maj(float yaw) {
    const float q4 = PI_F / 2;
    for (int k = 0; k < 8; k++) {
        const float rel = remainderf(yaw - (k + 0.5f) * PI_F / 4, 2 * PI_F);
        const int q = (int)lroundf(rel / q4);
        const float r = rel - q * q4, ar = fabsf(r);
        const float e = ar <= FIXE_TIENT ? r : (r < 0 ? -FIXE_TIENT : FIXE_TIENT) * (q4 / 2 - ar) / (q4 / 2 - FIXE_TIENT), b = yaw - e;
        fixe_ax[k] = cosf(b); fixe_az[k] = -sinf(b);
        fixe_mir[k] = ((q % 4 + 4) % 4) >= 2;
    }
}

/* ---------- Caméra ---------- */
typedef struct { float x, y, z; } v3;
static void setup_camera(float tx, float ty, float tz, float yaw, float pitch, float dist) {
    camera_t *c = &E.cam;
    c->cy = cosf(yaw); c->sy = sinf(yaw); c->cp = cosf(pitch); c->sp = sinf(pitch);
    c->x = tx - c->sy * c->cp * dist; c->y = ty + c->sp * dist; c->z = tz - c->cy * c->cp * dist;
    cam_dist_cur = dist;
    c->yaw = yaw; c->pitch = pitch;
    fixe_maj(yaw);
}
#if ENG_ROULIS
static int cam_roule;
static float cam_rc = 1, cam_rs;
static uint16_t *ag_ciel_tab[2];   /* (ciel_table, ag_rendu_reserver) */
#endif
static inline v3 to_view(float x, float y, float z) {
    const camera_t *c = &E.cam;
    float dx = x - c->x, dy = y - c->y, dz = z - c->z;
    float x1 = dx * c->cy - dz * c->sy, z1 = dx * c->sy + dz * c->cy;
    v3 o = { x1, dy * c->cp + z1 * c->sp, -dy * c->sp + z1 * c->cp };
#if ENG_ROULIS
    if (cam_roule) { const float a = o.x, b = o.y; o.x = a * cam_rc - b * cam_rs; o.y = a * cam_rs + b * cam_rc; }
#endif
    return o;
}
static inline float fog_of(float z) {
    /* Pas de brouillard dans un intérieur ; éteint, l'obscurité gagne à partir de Sera (fogOf de l'émulateur) */
    if (E.in.on && !BT.on) {
        if (!E.in.sombre) return 0;
        const float f = (z - cam_dist_cur) * int_noir_k;
        return f < 0 ? 0 : f > 7.99f ? 7.99f : f;
    }
    if (!E.fog_on) return 0;
    float f = (z - fog_z0c) * fog_kc;
    return f < 0 ? 0 : f > 7.99f ? 7.99f : f;
}
static float far_z = FAR_Z;   /* profondeur du dernier seau (course anti-gravité : la sienne, ag_image) */
static inline int bucket_of(float z) {
    int b = (int)((z - NEAR_Z) / (far_z - NEAR_Z) * OTN);
    return b < 0 ? 0 : b >= OTN ? OTN - 1 : b;
}

/* Côtés de l'image : ce qui est entièrement à droite de la colonne RW + 2, ou à gauche de la colonne -2, n'a aucun pixel
   à l'écran (submit_poly l'écarterait après l'avoir projeté). Pour un point (dx, dy, dz) relatif à la caméra,
   x_vue - k·z_vue = a·dx + b·dy + c·dz (k : pente de la colonne du plan) ; un polygone dont tous les sommets sont du
   mauvais côté d'un plan est invisible, coupé par le plan proche ou non. Les 2 pixels de marge couvrent les arrondis. */
typedef struct { float a, b, c, aa, ab, ac; } champ_t;
static champ_t champ[2];   /* 0 : côté droit (dehors si > 0), 1 : côté gauche (dehors si < 0) */
static void champ_init(void) {
    const camera_t *c = &E.cam;
    const float k[2] = { (RW + 2 - pcx) / FOCAL, (-2 - pcx) / FOCAL };
    for (int s = 0; s < 2; s++) {
        champ_t *p = &champ[s];
        p->a = c->cy - k[s] * c->cp * c->sy; p->b = k[s] * c->sp; p->c = -c->sy - k[s] * c->cp * c->cy;
        p->aa = fabsf(p->a); p->ab = fabsf(p->b); p->ac = fabsf(p->c);
    }
}
/* Boîte de centre (x, y, z) et de demi-côtés (hx, hy, hz), en coordonnées monde, entièrement hors du champ */
static inline int boite_hors_champ(float x, float y, float z, float hx, float hy, float hz) {
    const camera_t *cm = &E.cam;
    const float dx = x - cm->x, dy = y - cm->y, dz = z - cm->z;
    const champ_t *r = &champ[0], *l = &champ[1];
    if (r->a * dx + r->b * dy + r->c * dz - (r->aa * hx + r->ab * hy + r->ac * hz) > 0) return 1;
    return l->a * dx + l->b * dy + l->c * dz + (l->aa * hx + l->ab * hy + l->ac * hz) < 0;
}
/* hypotf(dx, dz) comparé à r sans calculer la racine (en logiciel sur l'ESP32) : les carrés décident hors d'une
   bande de ±0,02 % autour de r, hypotf dedans ; même réponse que hypotf(dx, dz) > r (dist_gt) ou < r (dist_lt) */
static inline int dist_gt(float dx, float dz, float r) {
    const float d2 = dx * dx + dz * dz, r2 = r * r;
    if (r > 0 && d2 > r2 * 1.0002f) return 1;
    if (r > 0 && d2 < r2 * 0.9998f) return 0;
    return hypotf(dx, dz) > r;
}
static inline int dist_lt(float dx, float dz, float r) {
    const float d2 = dx * dx + dz * dz, r2 = r * r;
    if (r > 0 && d2 < r2 * 0.9998f) return 1;
    if (r > 0 && d2 > r2 * 1.0002f) return 0;
    return hypotf(dx, dz) < r;
}
/* min et max sans appel (fminf et fmaxf sont des fonctions de bibliothèque sur l'ESP32) ; jamais de NaN ici */
static inline float min_f(float a, float b) { return a < b ? a : b; }
static inline float max_f(float a, float b) { return a > b ? a : b; }
#define BAT_CHAMP_H 40.0f   /* cases : de part et d'autre du premier polygone, quand la hauteur du bâtiment n'est pas connue (au-delà de BAT_CACHE) */
#define BAT_DEBORD 1.5f     /* cases : auvents, enseignes, corniches au-delà de l'emprise (le rayon du paquet ne couvre qu'elle) */
/* Hauteur de chaque bâtiment des chunks de la fenêtre (le test par bâtiment de la préparation) : relevée une fois, quand le chunk arrive dans sa
   place (cx, cz), sur tous ses polygones ; en seizièmes de case sur 16 bits, arrondis vers l'extérieur (la boîte ne rétrécit jamais) :
   7 Ko réservés au démarrage (tableau statique, en PSRAM sur la montre) */
#define BAT_CACHE 64
#define BAT_Y_PAS 16.0f
static struct { int ok, cx, cz, carte; const void *ch; int16_t ylo[BAT_CACHE], yhi[BAT_CACHE]; } bat_y[CWIN * CWIN] ENG_FROID;   /* clé : le chunk, sa place et sa carte */
static void bat_y_lire(int sl, const chunk_t *ch) {
    if (bat_y[sl].ok && bat_y[sl].ch == (const void *)ch && bat_y[sl].cx == ch->cx && bat_y[sl].cz == ch->cz && bat_y[sl].carte == E.w.carte) return;
    bat_y[sl].ok = 1; bat_y[sl].ch = ch; bat_y[sl].cx = ch->cx; bat_y[sl].cz = ch->cz; bat_y[sl].carte = E.w.carte;
    for (uint32_t k = 0; k < ch->nbld && k < BAT_CACHE; k++) {
        const pak_building_t *b = &ch->bld[k];
        float lo = 1e9f, hi = -1e9f;
        for (uint32_t m = 0; m < b->count; m++) {
            const pak_poly_t *pp = chunk_poly(ch, b->first + m);
            for (int i = 0; i < pp->nv && i < 8; i++) { if (pp->v[i].y < lo) lo = pp->v[i].y; if (pp->v[i].y > hi) hi = pp->v[i].y; }
        }
        lo = floorf(lo * BAT_Y_PAS); hi = ceilf(hi * BAT_Y_PAS);
        bat_y[sl].ylo[k] = (int16_t)(lo < -32768 ? -32768 : lo > 32767 ? 32767 : lo); bat_y[sl].yhi[k] = (int16_t)(hi < -32768 ? -32768 : hi > 32767 ? 32767 : hi);
    }
}
#ifdef ENG_COMPTE
#define BAT_ECARTE() do { extern unsigned long eng_bat_ecartes, eng_bat_polys_sautes; eng_bat_ecartes++; eng_bat_polys_sautes += b->count; } while (0)
#else
#define BAT_ECARTE() ((void)0)
#endif
/* Polygone du paquet (ses 8 premiers sommets, ceux que submit_pak_poly prend) entièrement hors du champ */
static int poly_hors_champ(const pak_poly_t *pp) {
    const camera_t *cm = &E.cam;
    const int n = pp->nv > 8 ? 8 : pp->nv;
    if (n <= 0) return 0;
    for (int s = 0; s < 2; s++) {
        const champ_t *p = &champ[s];
        int k = 0;
        for (; k < n; k++) {
            const pak_vert_t *q = &pp->v[k];
            const float f = p->a * (q->x - cm->x) + p->b * (q->y - cm->y) + p->c * (q->z - cm->z);
            if (s == 0 ? f <= 0 : f >= 0) break;
        }
        if (k == n) return 1;
    }
    return 0;
}

/* Île (10/11/2026, demande de l'utilisateur : « faire disparaître les murs quand on passe derrière, ne garder que la base du mur sur
   quelques pixels pour voir la limite ; l'idéal serait de le faire aussi en extérieur, ça gagnerait des fps »), puis Echoes le même jour
   (« propager ça à Echoes, intérieur et extérieur ») : ce qui cache l'héroïne n'est plus estompé (trame, transparence) mais coupé, d'un
   coup (« sans animation »). Dehors, le bâtiment entier (« l'ensemble du bâtiment ensemble, sinon c'est très bizarre ») à COUPE_FONDATION
   au-dessus de son pied, faces de dos comprises (le contour de la fondation entier) ; dedans, chaque mur qui la cache à COUPE_MUR_BASE
   au-dessus du pied des murs du niveau (linteaux compris). Moins de pixels peints, plus de mélange. La couverture de l'héroïne se juge sur
   les polygones entiers (coupés, ils ne la couvriraient plus) et seulement quand elle est vraiment cachée (couverture). Mêmes règles dans
   les émulateurs d'Echoes et de l'île (coupeHauteur, coupeY, couvPoly). */
static float coupe_hauteur(float lo, float hi, float a) { return lo + fmaxf(COUPE_MUR_BASE, (hi - lo) * (1 - a)); }
/* Polygone d'une porte d'un bâtiment coupé (le même jour, demande de l'utilisateur : « il faut que l'on voie les portes des bâtiments
   masqués ») : tous ses sommets dans la boîte d'une de ses portes (PORT : la largeur de l'ouverture et le cadre de part et d'autre du
   seuil, une case de chaque côté de la façade, à moins de COUPE_PORTE_H du pied du bâtiment) ; il reste entier (estPorte des émulateurs) */
static int est_porte(const pak_poly_t *pp, const int_porte_t *const *pts, int npt, float lo) {
    const int n = pp->nv > 8 ? 8 : pp->nv;
    for (int d = 0; d < npt; d++) {
        const int_porte_t *q = pts[d];
        const float demi = q->w / 2 + 0.2f;
        int k = 0;
        for (; k < n; k++) {
            const pak_vert_t *v = &pp->v[k];
            const float dx = v->x - q->x, dz = v->z - q->z;
            if (fabsf(dz * q->nx - dx * q->nz) > demi || fabsf(dx * q->nx + dz * q->nz) > 1.0f || v->y > lo + COUPE_PORTE_H) break;
        }
        if (n && k == n) return 1;
    }
    return 0;
}
/* Polygone du paquet à l'écran, coupé au plan proche comme submit_poly le fait, sans le soumettre : n points (16 au plus ; moins de 3 :
   rien) et sa boîte ; 0 : hors de l'image */
static int poly_ecran(const pak_poly_t *pp, float *sx, float *sy, float *bx) {
    const int n = pp->nv > 8 ? 8 : pp->nv;
    v3 v[8];
    int devant = 0;
    for (int i = 0; i < n; i++) { v[i] = to_view(pp->v[i].x, pp->v[i].y, pp->v[i].z); devant |= v[i].z >= NEAR_Z; }
    if (!devant) return 0;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    int np = 0;
#define BOITE_PT(X, Y, Z) do { const float sx_ = pcx + (X) * FOCAL / (Z), sy_ = pcy - (Y) * FOCAL / (Z); sx[np] = sx_; sy[np] = sy_; np++; \
        minx = min_f(minx, sx_); maxx = max_f(maxx, sx_); miny = min_f(miny, sy_); maxy = max_f(maxy, sy_); } while (0)
    for (int i = 0; i < n; i++) {
        const v3 *a = &v[i], *b = &v[(i + 1) % n];
        if (a->z >= NEAR_Z) BOITE_PT(a->x, a->y, a->z);
        if ((a->z >= NEAR_Z) != (b->z >= NEAR_Z)) { const float t = (NEAR_Z - a->z) / (b->z - a->z); BOITE_PT(a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t, NEAR_Z); }
    }
#undef BOITE_PT
    if (np < 3 || maxx < 0 || minx > RW || maxy < 0 || miny > RH) return 0;
    bx[0] = minx; bx[1] = maxx; bx[2] = miny; bx[3] = maxy;
    return np;
}

/* ---------- Table d'ordre ---------- */
static prim_t *new_prim(int b) {
    if (B->nprims >= MAX_PRIMS) return NULL;
    int i = B->nprims++;
    prim_t *p = &B->prims[i];
    p->next = -1; p->fade = 0;
    if (ot_tail[b] >= 0) B->prims[ot_tail[b]].next = i; else ot_head[b] = i;
    ot_tail[b] = i;
    return p;
}

static int cur_cat;   /* catégorie des polygones soumis (statistiques) */
static int fade_mark; /* faces de bâtiment en cours de soumission : noter celles qui sont devant Sera */

/* Sommets du terrain transformés une fois par image (partagés par quatre cases) : relief et niveau de l'eau */
#define VC (2 * VIEW_R + 4)          /* les blocs de 2 × 2 cases peuvent déborder d'une case */
static v3 *vc_ground, *vc_water;
static uint32_t *vc_stamp, frame_stamp;
static int vc_i0, vc_j0;
/* Cache des sommets, réservé à l'initialisation (rien n'est réservé pendant le jeu) */
static void vc_alloc(void) {
    vc_ground = E.cfg.alloc(sizeof(v3) * VC * VC, 0); vc_water = E.cfg.alloc(sizeof(v3) * VC * VC, 0);
    vc_stamp = E.cfg.alloc(4 * VC * VC, 0);
    if (!vc_ground || !vc_water || !vc_stamp) { E.error = "mémoire insuffisante (sommets)"; vc_stamp = NULL; return; }
    memset(vc_stamp, 0, 4 * VC * VC);
}
static inline v3 grid_vertex(int x, int z, int water) {
    int k = (z - vc_j0) * VC + (x - vc_i0), bit = water ? 2 : 1;
    v3 *c = water ? &vc_water[k] : &vc_ground[k];
    uint32_t st = vc_stamp[k];
    if ((st >> 2) != frame_stamp) st = frame_stamp << 2;
    if (!(st & bit)) { *c = to_view((float)x, water ? E.w.wl : hv(x, z), (float)z); st |= bit; }
    vc_stamp[k] = st;
    return *c;
}

/* Sommet en vue avant projection */
typedef struct { float x, y, z, u, v, l; } vv_t;

/* Personnage à l'écran : pied (sx, sy), échelle s, seau de l'ombre b (le sprite est en b - 1). 0 si trop près. */
typedef struct { float y, z, sx, sy, s; int b; } char_view_t;
static int char_view(float x, float z, float alt, char_view_t *c) {
    c->y = ground_y(x, z) + alt;
    v3 o = to_view(x, c->y, z);
    if (o.z < NEAR_Z + 0.2f) return 0;
    c->z = o.z; c->sx = pcx + o.x * FOCAL / o.z; c->sy = pcy - o.y * FOCAL / o.z;
    c->s = FOCAL * 2.0f / E.w.ech / (o.z * E.w.fh) * E.w.sprite_scale;   /* échelle globale des sprites */
    c->b = bucket_of(o.z - 0.25f);
    return 1;
}

#ifndef ESP_PLATFORM
int eng_essai_projeter(float x, float z, float *sx, float *sy, float *h) {   /* rendu_test (scenecams) : pieds et hauteur d'un personnage à l'écran (pixels natifs) */
    char_view_t c;
    if (!char_view(x, z, 0, &c)) return 0;
    *sx = c.sx * ENG_SCALE; *sy = c.sy * ENG_SCALE; *h = E.w.fh * c.s * ENG_SCALE;
    return 1;
}
#endif
/* Quelqu'un à cette place se voit-il dans la bande que le dialogue laisse ? Le milieu de son corps : dans le disque, au-dessus du panneau, hors des deux portraits (centres en
   112 et 354, 112, rayon 56 : pixels natifs) */
static float dlg_glisse = 1;   /* glissement du dialogue (0 à 1 ; 1 : l'image est relevée, ou rien ne glisse) */
static int dans_bande(float x, float z) {
    char_view_t c;
    /* (île : à la surface de l'eau qui couvre le sol, comme un bateau ; la barque de la traversée, comptée au fond, 6 m plus bas, sortait
       de la bande et la caméra quittait le bateau pour l'héroïne restée sur la plage, 11/11/2026 ; bande de l'émulateur de l'île) */
    if (!char_view(x, z, E.w.ile[0] ? eau_prof(x, z) : 0, &c)) return 0;
    const float S = ENG_SCALE, mx = c.sx * S, my = (c.sy - E.w.fh * c.s / 2) * S;
    return my > 5 && my < 166 && hypotf(mx - 233, my - 233) < 233 && hypotf(mx - 112, my - 112) > 56 && hypotf(mx - 354, my - 112) > 56;
}
/* Personnages devant ou derrière les faces planes (bâtiments, blocs) : la profondeur moyenne d'un grand mur peut être
   plus proche que Sera alors que le mur est derrière elle. Une face ne peut cacher un personnage que si son plan le
   sépare de la caméra (test au milieu de sa hauteur) : elle passe alors après lui, sinon avant. Seulement pour les
   faces qui recouvrent le sprite à l'écran. occ_poly : face du paquet en cours de soumission, NULL pour le reste. */
typedef struct { float x, y, z, x0, x1, y0, y1, vz; int sb; } occ_t;   /* vz : profondeur (vue) */
static occ_t occ[2 + NPC_DRAW];
static int nocc, occ_sera;              /* occ_sera : place de Sera dans occ (-1 : hors de la vue) */
/* Sujet d'une caméra de scène (07/11/2026, revue des caméras : le point visé était souvent derrière un immeuble) : ce qui le cache s'estompe, comme ce qui cache Sera.
   Boîte de la taille de trois personnages (occSujet de l'émulateur) */
static occ_t occ_sujet, occ_parle;      /* occ_parle : celui qui dit la réplique */
static int sujet_on, parle_on;
static int sujet_ici;                   /* le bâtiment soumis ne contient pas le point visé (s'il le contient, c'est lui, le sujet : la grande porte de Lucerne ; SUJET_DANS × son rayon) */
#define SUJET_DANS 0.7f
/* Grands décors (arbres, rochers) proches, au même titre que les personnages : soumis avant les bâtiments, ils sont
   déjà dans leur seau ; une face derrière l'un d'eux va dans le seau suivant (dessinée avant lui), une face dont le plan
   le sépare de la caméra dans le seau d'avant (après lui). Sinon, un long mur oblique derrière un arbre, plus proche en
   moyenne, le coupait. */
#define ARB_OCC 32                      /* au plus, pris dans l'ordre des cases */
#define ARB_R 14.0f                     /* rayon autour de Sera (cases du monde × ech) */
static occ_t arb[ARB_OCC];
static int narb;
static const pak_poly_t *occ_poly;
static float poly_bias;                 /* profondeur retirée avant le choix du seau (face de bâtiment : selon son rang) */
/* Sol (cases, blocs lointains) reculé de SOL_RECUL avant le choix du seau : dessiné avant ce qui se tient dessus. Une
   case du bord d'un bâtiment (emprise hors de la grille des cases) est à moitié sous lui : triée après son mur, elle
   en repeignait le pied (bas en escalier, scintillement) ; le relief lointain (colline devant un bâtiment) garde
   son ordre (sol de l'émulateur : SOL_RECUL) */
#define SOL_RECUL 2.5f
static int char_bucket = -1;            /* seau imposé à un personnage (intérieur : tout dans le même, dans l'ordre) */
static float char_devant, char_devant_z; /* > 0 : profondeurs (vue) que le personnage précède, seau et clé : l'attaquant au corps à corps devant l'ennemi qu'il frappe */
/* L'image de la direction row descend sous son ancrage (véhicule ancré au milieu de son emprise) : des pixels opaques à plus de
   2 pixels sous lui, le vide sous le pixel le plus bas (foot) déduit. Avant le 11/11/2026, ce vide comptait : un personnage
   ancré à ses pieds avec 3 ou 4 lignes vides sous eux (Sera, Bors, Varn selon la direction) avait sa profondeur avancée comme
   un camion et passait devant ses voisins en combat (retour de jeu ; sousAncrage de l'émulateur) */
static int sous_ancrage(int sheet, int row) {
    if (sheet < 0 || !E.w.sheets[sheet].has_pv) return 0;
    const sheet_t *S = &E.w.sheets[sheet];
    const int vide = S->footok >> (row & 7) & 1 ? S->foot[row & 7] : 0;
    return S->fh - S->pv[row & 7][1] / 2.0f - vide > 2;
}
static int char_teinte;                 /* membre transformé (ÉVEIL) + 1 du personnage soumis (prim_t.teinte ; 0 : aucun) */
#define TEINTE_FANTOME 255              /* fantôme du contre-la-montre (prim_t.teinte) */
static int char_tissu;                  /* variante de couleur de la tenue du personnage soumis (member_tissu ; 0 : aucune) */
static float char_anneau;               /* anneau au sol du personnage soumis : son rayon (pixels de l'interface ; 0 : aucun) */
static uint32_t char_anneau_c;          /* sa couleur (pack565) */
static float char_clip_x = NAN, char_clip_z;   /* nageur d'un bassin : bord proche du bassin, au ras du sol ; le sprite s'arrête à sa ligne (au-delà, le sol de la pièce le cache), sans ombre */
static float sp_minx, sp_maxx, sp_miny, sp_maxy;   /* boîte à l'écran du dernier polygone soumis */
/* Fondu des bâtiments : face soumise dont le plan sépare Sera de la caméra (front_face), bâtiment dont une telle face
   recouvre Sera à l'écran (bld_hit). Chaque bâtiment qui cache Sera, ou qui la cachait, a une place dans fades,
   repérée par son centre : son niveau monte vers 1 en FADE_S secondes tant qu'il la cache, puis redescend. */
#define FADE_N 16
#define FADE_S 0.35f
#define FADE_MAX 10                     /* pixels sautés au plus, sur 16 (tramage de Bayer 4 × 4) */
typedef struct { float cx, cz, a; int used, hit; float r; } fade_t;   /* r : rayon du bâtiment (sol_fondu), 0 pour un décor */
static fade_t fades[FADE_N];
static float fade_time = -1;
static int bld_hit;
#ifndef ESP_PLATFORM
int eng_essai_fondus(void) { int n = 0; for (int f = 0; f < FADE_N; f++) if (fades[f].used && fades[f].a > 0.5f && fades[f].r > 0) n++; return n; }   /* rendu_test (scenecams) : bâtiments estompés */
#endif

/* Grand décor (plus de 1,2 de haut : arbres, rochers) entre la caméra et Sera, qui la recouvre à l'écran : place de fondu
   repérée par sa position, comme un bâtiment ; prim : son quadrilatère, q : ses sommets en vue */
static void plant_fade(float px, float pz, prim_t *prim, const vv_t *q) {
    if ((occ_sera < 0 && !sujet_on && !parle_on) || !prim) return;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, vz = 0;
    int close = 0;   /* un coin derrière le plan proche : le décor est juste devant la caméra (zoom fort), on l'estompe */
    for (int m = 0; m < 4; m++) {
        if (q[m].z < NEAR_Z) { close = 1; continue; }
        const float sx = pcx + q[m].x * FOCAL / q[m].z, sy = pcy - q[m].y * FOCAL / q[m].z;
        minx = fminf(minx, sx); maxx = fmaxf(maxx, sx); miny = fminf(miny, sy); maxy = fmaxf(maxy, sy); vz += q[m].z / 4;
    }
#define COUVRE_OCC(o) (vz < (o)->vz - 0.2f && !(maxx < (o)->x0 || minx > (o)->x1 || maxy < (o)->y0 || miny > (o)->y1))
    const int hit = close || (occ_sera >= 0 && COUVRE_OCC(&occ[occ_sera])) || (sujet_on && COUVRE_OCC(&occ_sujet)) || (parle_on && COUVRE_OCC(&occ_parle));
    int slot = -1, freeslot = -1;
    for (int f = 0; f < FADE_N; f++) {
        if (!fades[f].used) { if (freeslot < 0) freeslot = f; continue; }
        if (fabsf(fades[f].cx - px) < 0.01f && fabsf(fades[f].cz - pz) < 0.01f) { slot = f; break; }
    }
    if (slot < 0 && hit && freeslot >= 0) { slot = freeslot; fades[slot] = (fade_t){ px, pz, 0, 1, 0, 0 }; }
    if (slot < 0) return;
    if (hit) fades[slot].hit = 1;
    prim->fade = (uint8_t)(slot + 1);
}
static void occ_add(float x, float z) {
    char_view_t c;
    if (nocc >= 2 + NPC_DRAW || !char_view(x, z, 0, &c)) return;
    occ_t *o = &occ[nocc++];
    float hw = E.w.fw * c.s / 2;
    o->x = x; o->y = c.y + 1.0f / E.w.ech; o->z = z;
    o->x0 = c.sx - hw; o->x1 = c.sx + hw; o->y0 = c.sy - E.w.fh * c.s; o->y1 = c.sy;
    o->sb = c.b > 0 ? c.b - 1 : 0; o->vz = c.z;
}
/* Plan de la face pp entre la caméra et Sera, et la face recouvre-t-elle Sera à l'écran ? (bit 0, bit 1) */
static int front_de(const occ_t *o, const pak_poly_t *pp, float minx, float maxx, float miny, float maxy) {
    const pak_vert_t *q = &pp->v[0];
    const camera_t *c = &E.cam;
    float cs = (c->x - q->x) * pp->n[0] + (c->y - q->y) * pp->n[1] + (c->z - q->z) * pp->n[2];
    float ps = (o->x - q->x) * pp->n[0] + (o->y - q->y) * pp->n[1] + (o->z - q->z) * pp->n[2];
    if ((ps < 0) == (cs < 0)) return 0;
    return 1 | (maxx < o->x0 || minx > o->x1 || maxy < o->y0 || miny > o->y1 ? 0 : 2);
}
/* Couverture d'un personnage par un bâtiment ou un mur (10/11/2026, demande de l'utilisateur : « le masquage du bâtiment seulement si le
   personnage joué est caché à plus de 50 % par le bâtiment, pour éviter que les bâtiments disparaissent quand on longe les côtés sans
   être réellement masqué ») : COUV_NX × COUV_NY points sur son corps à l'écran (colonnes au milieu du cadre de sa planche, de la
   cheville à la tête), couverts ou non par les faces placées devant lui (polygones entiers, à l'écran : chaque point d'un masque de
   bits, l'union de toutes les faces du bâtiment ou de l'objet mur). Coupé à COUV_COUPE points et plus (la moitié), gardé coupé tant qu'il
   en couvre COUV_GARDE (sans clignoter au seuil). Avant : la boîte d'une seule face qui touchait le cadre du personnage suffisait.
   couvPoly, couvPoints des émulateurs, à garder identiques. */
#define COUV_NX 3
#define COUV_NY 7
#define COUV_COUPE 11
#define COUV_GARDE 8
static uint32_t couv_points(const occ_t *o, const float *sx, const float *sy, int n) {
    const float w = o->x1 - o->x0, h = o->y1 - o->y0, cx = (o->x0 + o->x1) * 0.5f;
    uint32_t m = 0;
    for (int j = 0; j < COUV_NY; j++) for (int i = 0; i < COUV_NX; i++) {
        const float x = cx + (i - (COUV_NX - 1) * 0.5f) * 0.15f * w, y = o->y1 - h * (0.06f + 0.84f * j / (COUV_NY - 1));
        int dedans = 0;
        for (int a = 0, b = n - 1; a < n; b = a++)
            if ((sy[a] > y) != (sy[b] > y) && x < sx[a] + (sx[b] - sx[a]) * (y - sy[a]) / (sy[b] - sy[a])) dedans = !dedans;
        if (dedans) m |= 1u << (j * COUV_NX + i);
    }
    return m;
}
static int couv_nb(uint32_t m) { int n = 0; while (m) { m &= m - 1; n++; } return n; }
/* Ce que la face pp couvre de chaque personnage suivi (Sera, le sujet de la caméra, celui qui parle), ajouté à cv[0..2] ; seulement si
   son plan les sépare de la caméra */
static void couv_face(const pak_poly_t *pp, int sujet, uint32_t *cv) {
    float sx[16], sy[16], bx[4];
    const int n = poly_ecran(pp, sx, sy, bx);
    if (n < 3) return;
    const occ_t *os[3] = { occ_sera >= 0 ? &occ[occ_sera] : NULL, sujet ? &occ_sujet : NULL, parle_on ? &occ_parle : NULL };
    for (int k = 0; k < 3; k++) if (os[k] && (front_de(os[k], pp, bx[0], bx[1], bx[2], bx[3]) & 2)) {
        const uint32_t m = couv_points(os[k], sx, sy, n);
        cv[k] |= m;
#ifdef ENG_DEBUG_COUV   /* mise au point (rendu_test compilé avec -DENG_DEBUG_COUV) : chaque face qui couvre un personnage suivi */
        if (m && k == 0) printf("COUV face (%.2f %.2f %.2f) n (%.2f %.2f %.2f) nv %d : %d points, sera y %.2f%c", pp->v[0].x, pp->v[0].y, pp->v[0].z, pp->n[0], pp->n[1], pp->n[2], pp->nv, couv_nb(m), os[k]->y, 10);
#endif
    }
}
static int couv_cache(const uint32_t *cv, int coupe) {
    const int s = coupe ? COUV_GARDE : COUV_COUPE;
    return couv_nb(cv[0]) >= s || couv_nb(cv[1]) >= s || couv_nb(cv[2]) >= s;
}
static int sera_front(const pak_poly_t *pp, float minx, float maxx, float miny, float maxy) {
    return (occ_sera >= 0 ? front_de(&occ[occ_sera], pp, minx, maxx, miny, maxy) : 0) | (sujet_ici ? front_de(&occ_sujet, pp, minx, maxx, miny, maxy) : 0)
        | (parle_on ? front_de(&occ_parle, pp, minx, maxx, miny, maxy) : 0);
}
static int occ_order(int b, float minx, float maxx, float miny, float maxy) {
    const pak_poly_t *pp = occ_poly;
    const pak_vert_t *q = &pp->v[0];
    const camera_t *c = &E.cam;
    float cs = (c->x - q->x) * pp->n[0] + (c->y - q->y) * pp->n[1] + (c->z - q->z) * pp->n[2];
    for (int k = 0; k < nocc; k++) {
        const occ_t *o = &occ[k];
        if (maxx < o->x0 || minx > o->x1 || maxy < o->y0 || miny > o->y1) continue;
        float ps = (o->x - q->x) * pp->n[0] + (o->y - q->y) * pp->n[1] + (o->z - q->z) * pp->n[2];
        if ((ps < 0) != (cs < 0)) { if (b >= o->sb) b = o->sb > 0 ? o->sb - 1 : 0; }
        else if (b < o->sb) b = o->sb;
    }
    for (int k = 0; k < narb; k++) {
        const occ_t *o = &arb[k];
        if (maxx < o->x0 || minx > o->x1 || maxy < o->y0 || miny > o->y1) continue;
        float ps = (o->x - q->x) * pp->n[0] + (o->y - q->y) * pp->n[1] + (o->z - q->z) * pp->n[2];
        if ((ps < 0) != (cs < 0)) { if (b >= o->sb) b = o->sb > 0 ? o->sb - 1 : 0; }
        else if (b <= o->sb) b = o->sb < OTN - 1 ? o->sb + 1 : OTN - 1;   /* le décor est déjà dans son seau : avant lui */
    }
    return b;
}
/* Grand décor soumis dans le seau sb, sommets en vue q : occultant des faces des bâtiments (pied un peu au-dessus du sol) */
static void arb_add(float x, float y, float z, int sb, const vv_t *q) {
    if (narb >= ARB_OCC || sb < 0) return;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int m = 0; m < 4; m++) {
        if (q[m].z < NEAR_Z) return;   /* juste devant la caméra : estompé (plant_fade), pas d'ordre à corriger */
        const float sx = pcx + q[m].x * FOCAL / q[m].z, sy = pcy - q[m].y * FOCAL / q[m].z;
        minx = fminf(minx, sx); maxx = fmaxf(maxx, sx); miny = fminf(miny, sy); maxy = fmaxf(maxy, sy);
    }
    occ_t *o = &arb[narb++];
    o->x = x; o->y = y + 0.5f / E.w.ech; o->z = z; o->x0 = minx; o->x1 = maxx; o->y0 = miny; o->y1 = maxy; o->sb = sb; o->vz = 0;
}

/* Polygone en coordonnées de vue ; forced : seau imposé (-1 : profondeur moyenne). Renvoie le seau, -1 si invisible. */
static int submit_poly(const vv_t *in, int n, int tile, int forced, int alpha) {
    vv_t clip[12];
    const vv_t *poly = in;
    int all_out = 1, some_out = 0;
    for (int i = 0; i < n; i++) { if (in[i].z >= NEAR_Z) all_out = 0; else some_out = 1; }
    if (all_out) return -1;
    if (some_out) {
        int m = 0;
        for (int i = 0; i < n && m < 11; i++) {
            const vv_t *a = &in[i], *b = &in[(i + 1) % n];
            int ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
            if (ain) clip[m++] = *a;
            if (ain != bin) {
                float t = (NEAR_Z - a->z) / (b->z - a->z);
                vv_t c = { a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t, NEAR_Z, a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t, a->l + (b->l - a->l) * t };
                clip[m++] = c;
            }
        }
        if (m < 3) return -1;
        poly = clip; n = m;
    }
    if (B->nverts + n > MAX_VERTS) return -1;
    float zs = 0, zmin = 1e9f, minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    sv_t *sp = &B->verts[B->nverts];
    for (int i = 0; i < n; i++) {
        float w = 1.0f / poly[i].z;
        zs += poly[i].z; zmin = min_f(zmin, poly[i].z);
        sp[i].x = pcx + poly[i].x * FOCAL * w; sp[i].y = pcy - poly[i].y * FOCAL * w;
        sp[i].qu = poly[i].u * w; sp[i].qv = poly[i].v * w; sp[i].w = w;
        sp[i].l = poly[i].l; sp[i].f = fog_of(poly[i].z);
        minx = min_f(minx, sp[i].x); maxx = max_f(maxx, sp[i].x);
        miny = min_f(miny, sp[i].y); maxy = max_f(maxy, sp[i].y);
    }
    if (maxx < 0 || minx > RW || maxy < 0 || miny > RH) return -1;
    sp_minx = minx; sp_maxx = maxx; sp_miny = miny; sp_maxy = maxy;
    /* Texture de bâtiment vue de loin ou de biais (plus de 1,15 texel par pixel sur chaque axe ; 2,3 : niveau 2) : sa
       version réduite (MIPS), sans le scintillement des motifs fins quand la caméra bouge (submitPoly de l'émulateur) */
    if ((uint32_t)tile >= E.w.mip0 && (uint32_t)tile - E.w.mip0 < E.w.nmips) {
        float as = 0, at = 0;
        for (int i = 0; i < n; i++) {
            const int j = (i + 1) % n;
            as += sp[i].x * sp[j].y - sp[j].x * sp[i].y;
            at += poly[i].u * poly[j].v - poly[j].u * poly[i].v;
        }
        const float r = fabsf(at) / fmaxf(1e-6f, fabsf(as));
        const int lv = r > 5.3f ? 2 : r > 1.32f ? 1 : 0;
        if (lv) {
            const uint8_t *m = E.w.mips + ((uint32_t)tile - E.w.mip0) * 4 + (lv - 1) * 2;
            tile = m[0] | m[1] << 8;
            const float k = lv == 2 ? 0.25f : 0.5f;
            for (int i = 0; i < n; i++) { sp[i].qu *= k; sp[i].qv *= k; }
        }
    }
#if ENG_ROUND
    /* Hors du cercle : la boîte englobante ne touche pas le disque de l'écran */
    {
        float dx = minx > CXF ? minx - CXF : maxx < CXF ? CXF - maxx : 0, dy = miny > CYF ? miny - CYF : maxy < CYF ? CYF - maxy : 0;
        if (dx * dx + dy * dy > (RW / 2.0f) * (RW / 2.0f)) { B->stats.tris_outside += n - 2; return -1; }
    }
#endif
    /* Entièrement dans le brouillard (au-delà de fog_full, la couleur est celle du fond) */
    if (E.fog_on && zmin >= fog_fullc) { B->stats.tris_fogged += n - 2; return -1; }
    int b = forced >= 0 ? forced : bucket_of(zs / n - poly_bias);
    if (occ_poly && forced < 0) b = occ_order(b, minx, maxx, miny, maxy);
    prim_t *p = new_prim(b);
    if (!p) return -1;
    B->stats.cat_tris[cur_cat] += n - 2;
    p->kind = K_POLY; p->tile = (uint16_t)tile; p->alpha = (uint8_t)alpha; p->nv = (uint8_t)n; p->v0 = (uint32_t)B->nverts;
    if (occ_poly && fade_mark) {   /* face de bâtiment : candidate au fondu (place définitive à la fin du bâtiment) */
        int f = sera_front(occ_poly, minx, maxx, miny, maxy);
        if (f) { p->fade = 255; if (f & 2) bld_hit = 1; }
    }
    /* 0 ≤ miny (borné) et maxy ≥ 0 (sinon écarté plus haut) : plancher et plafond en entiers, sans appel */
    const float ylo = miny < 0 ? 0 : miny, yhi = maxy > RH ? RH : maxy;
    const int iyhi = (int)yhi;
    p->miny = (int16_t)(int)ylo; p->maxy = (int16_t)(iyhi + (yhi > (float)iyhi));
    B->nverts += n;
    return b;
}

/* Polygone du paquet (coordonnées monde) */
static int submit_pak_poly(const pak_poly_t *pp, int tile, int forced, int alpha) {
    if (poly_hors_champ(pp)) return -1;
    vv_t v[8];
    int n = pp->nv > 8 ? 8 : pp->nv;
    for (int i = 0; i < n; i++) {
        const pak_vert_t *q = &pp->v[i];
        v3 o = to_view(q->x, q->y, q->z);
        v[i].x = o.x; v[i].y = o.y; v[i].z = o.z; v[i].u = q->u; v[i].v = q->v; v[i].l = q->l;
    }
    return submit_poly(v, n, tile, forced, alpha);
}
/* Polygone du paquet coupé à la hauteur yc (île : coupe_hauteur) : sa partie sous yc ; -1 s'il n'en reste rien ; dos : face tournée
   de l'autre côté (le dos d'un mur, vu dans un bâtiment coupé), ses sommets pris à l'envers */
static int submit_pak_poly_coupe(const pak_poly_t *pp, int tile, int forced, int alpha, float yc, int dos) {
    if (poly_hors_champ(pp)) return -1;
    vv_t v[10];
    const int n = pp->nv > 8 ? 8 : pp->nv;
    int m = 0;
    for (int i = 0; i < n; i++) {
        const pak_vert_t *a = &pp->v[dos ? n - 1 - i : i], *b = &pp->v[dos ? (2 * n - 2 - i) % n : (i + 1) % n];
        pak_vert_t q[2]; int nq = 0;
        if (a->y <= yc) q[nq++] = *a;
        if ((a->y <= yc) != (b->y <= yc)) {
            const float t = (yc - a->y) / (b->y - a->y);
            q[nq].x = a->x + (b->x - a->x) * t; q[nq].y = yc; q[nq].z = a->z + (b->z - a->z) * t;
            q[nq].u = a->u + (b->u - a->u) * t; q[nq].v = a->v + (b->v - a->v) * t; q[nq].l = a->l + (b->l - a->l) * t; nq++;
        }
        for (int j = 0; j < nq && m < 10; j++) { const v3 o = to_view(q[j].x, q[j].y, q[j].z); v[m].x = o.x; v[m].y = o.y; v[m].z = o.z; v[m].u = q[j].u; v[m].v = q[j].v; v[m].l = q[j].l; m++; }
    }
    return m < 3 ? -1 : submit_poly(v, m, tile, forced, alpha);
}
static int culled(const pak_poly_t *pp) {
    if (!(pp->flags & POLY_CULL)) return 0;
    const camera_t *c = &E.cam;
    const pak_vert_t *q = &pp->v[0];
    return (c->x - q->x) * pp->n[0] + (c->y - q->y) * pp->n[1] + (c->z - q->z) * pp->n[2] <= 0;
}

static int sprite_row(float dir) {
    float rel = dir - E.cam.yaw;
    rel = atan2f(sinf(rel), cosf(rel));
    float a = fabsf(rel);
    if (a < PI_F / 4) return 3;
    if (a > 3 * PI_F / 4) return 0;
    return rel > 0 ? 2 : 1;
}

/* Ligne d'une planche propre d'après l'angle entre la marche et la caméra : S, SE, E, NE, N (dos), NW, W, SW */
static int sera_row(float dir) {
    float rel = dir - E.cam.yaw;
    rel = atan2f(sinf(rel), cosf(rel));
    int k = (int)lroundf(rel / (PI_F / 4));
    return ((4 - k) % 8 + 8) % 8;
}

/* sheet : planche propre (-1 : commune) ; who : 0 Sera, 1 interlocuteur (bulle), 2 autre personnage, WHO_MEMBRE + k membre k
   de l'équipe en combat (position à l'écran pour ses barres), WHO_ENNEMI + k l'ennemi k du groupe (point visé, nom) ;
   taille : échelle à l'écran (0 : celle de la planche) ; alt : hauteur au-dessus du sol (vaisseau ; ombre au sol) ;
   ground : véhicule qui roule, image descendue de son vide (foot) pour que son bas touche le sol ;
   ombre : réglage d'ombre (E.w.ombre, OMBRE_JOUEUR pour le joueur), -1 : l'ombre d'origine */
#define NAGE_SINK 0.55f          /* à la nage : part de la hauteur du sprite sous la surface */
#define ILE_NAGE_PLEIN 0.3f      /* île : profondeur (cases) où l'enfoncement atteint ILE_NAGE_SINK (ileNage de l'émulateur de l'île) */
/* Île (11/11/2026, demande de l'utilisateur : les héroïnes à moitié immergées quand elles nagent) : l'enfoncement suit la profondeur ;
   ILE_NAGE_SINK (internal.h) plutôt que NAGE_SINK, qui ne laissait que la tête hors de l'eau ; en deçà d'ILE_GUE, on ne nage pas */
static float ile_nage(float prof, float H) { return prof - ILE_NAGE_SINK * H * fminf(1.0f, prof / ILE_NAGE_PLEIN); }
/* Réfraction (11/11/2026, demande de l'utilisateur : la partie immergée visible, d'une autre couleur et plus écrasée) : sous la ligne d'eau,
   un sprite de l'île est tassé vers la surface (ILE_REFRAC_K de sa hauteur immergée) et mêlé à la couleur de l'eau (ILE_EAU_*, mélange
   ILE_EAU_MELE / 16) ; ligne d'eau à l'écran dans v0 du sprite (y + 1, 0 : aucune) ; drawSprite de l'émulateur de l'île */
#define ILE_REFRAC_K 0.6f
#define ILE_EAU_R 0x1E
#define ILE_EAU_G 0x7F
#define ILE_EAU_B 0x96
#define ILE_EAU_MELE 7
#define WHO_MEMBRE 3
#define WHO_ENNEMI (WHO_MEMBRE + NMEMB)
static void add_character(float x, float z, int frame, int row, int remap, int sheet, int who, float taille, float alt, int ground, int ombre) {
    char_view_t c, g;
    if (!char_view(x, z, alt, &c)) return;
    /* Planche lue à la demande (carte SD) : une direction de plus par image, le personnage attend la sienne */
#ifndef ESP_PLATFORM
    { extern int eng_essai_planche_absente; if (sheet >= 0 && !sheet_block(sheet, row, script_running() ? 2 : 1)) eng_essai_planche_absente++; }   /* rendu_test (sceneaudit) */
#endif
    if (sheet >= 0 && !sheet_block(sheet, row, script_running() ? 2 : 1)) return;   /* dans une scène, personne ne paraît en retard : sa planche est lue tout de suite */
    /* ombre : au sol ; sur l'île, ce qui est à la surface de l'eau ou au-dessus (bateau, vaisseau) l'a sur l'eau, pas sur le fond (11/11/2026 :
       la pirogue semblait voler, son ombre sur le fond du lagon ; addCharacter de l'émulateur de l'île) */
    float eo = 0;
    if (E.w.ile[0] && alt > 0) { const float p = eau_prof(x, z); if (p > 0.05f && alt >= p - 0.05f) eo = p; }
    if (alt <= 0 || !char_view(x, z, eo, &g)) g = c;
    float sx = c.sx, sy = c.sy, s = c.s;
    /* taille voulue (PERS ; Sera : celle de sa planche) × normalisation de la silhouette de la planche dessinée (SNRM) */
    const float t = (taille > 0 ? taille : sheet >= 0 ? E.w.sheets[sheet].taille : 1.0f) * (sheet >= 0 ? E.w.sheets[sheet].norm : 1.0f), ht = E.w.fh * s * t;
    /* Position à l'écran natif, pour les gestes et la bulle d'interaction */
    screen_pos_t *sp = who == 0 ? &E.scr_sera : who == 1 ? &E.scr_npc : who >= WHO_MEMBRE && who < WHO_ENNEMI ? &E.scr_party[who - WHO_MEMBRE]
                     : who >= WHO_ENNEMI && who < WHO_ENNEMI + BT_EMAX ? &E.scr_enemy[who - WHO_ENNEMI] : NULL;
    if (who == 0) { B->torche_x = sx; B->torche_y = sy - ht * 0.55f; }
    if (sp) { sp->vis = 1; sp->cx = sx * ENG_SCALE - ui_ox; sp->cy = (sy - ht * 0.55f) * ENG_SCALE - ui_oy; sp->top = (sy - ht) * ENG_SCALE - ui_oy; sp->foot = sy * ENG_SCALE - ui_oy; }
    int F = (int)fog_of(c.z); if (F > 7) F = 7;
    /* Seau : avancé de 0,3 × la largeur du sprite (dans le monde) en plus des 0,25 de char_view : les cases de sol de
       part et d'autre d'un grand sprite (camion) ne passent plus devant son bas */
    const float ratio = sheet >= 0 ? (float)E.w.sheets[sheet].fw / E.w.sheets[sheet].fh : (float)E.w.fw / E.w.fh;
    int b = char_bucket >= 0 ? char_bucket : bucket_of(c.z - 0.25f - 0.3f * ratio * ht * c.z / FOCAL);
    if (char_devant > 0 && bucket_of(char_devant) < b) b = bucket_of(char_devant);
    /* Ombre au sol : réglée par personnage (décalage en part de la cellule, taille en part de l'ombre d'origine) */
    const ombre_t *om = ombre >= 0 && ombre <= MAX_PERSOS ? &E.w.ombre[ombre] : NULL;
    const int clip = char_clip_x == char_clip_x;
    char_view_t cw;
    const float clip_y = clip && char_view(char_clip_x, char_clip_z, -ground_y(char_clip_x, char_clip_z), &cw) ? fmaxf(0.5f, cw.sy) : 0;
    prim_t *sh = (om && om->off) || clip || char_teinte == TEINTE_FANTOME ? NULL : new_prim(b);
    if (sh) {
        float ox = 0, oy = 0, rx = 9 * g.s * t, ry = 3.2f * g.s * t;
        if (om) {
            rx *= om->l / 100.0f; ry *= om->h / 100.0f;
            if (sheet >= 0) {
                const float k = g.s * E.w.fh / (float)E.w.sheets[sheet].fh * t;   /* pixels à l'écran par pixel de planche */
                ox = om->dx / 100.0f * E.w.sheets[sheet].fw * k; oy = om->dy / 100.0f * E.w.sheets[sheet].fh * k;
            }
        }
        sh->kind = K_SHADOW; sh->sx = g.sx + ox; sh->sy = g.sy - oy; sh->s = rx; sh->ry = ry;
        sh->miny = (int16_t)floorf(sh->sy - ry); sh->maxy = (int16_t)ceilf(sh->sy + ry + 1);
    }
    /* Anneau de combat (celui qui choisit, l'ennemi visé) : au sol, sous le personnage, pas par-dessus (07/11/2026, demande de l'utilisateur ; il était dessiné par l'interface) */
    if (char_anneau > 0) {
        prim_t *an = new_prim(b);
        if (an) {
            const float rx = char_anneau / ENG_SCALE, ry = rx * 0.32f;
            an->kind = K_ANNEAU; an->sx = g.sx; an->sy = g.sy; an->s = rx; an->ry = ry; an->v0 = char_anneau_c; an->tile = (uint16_t)(ZK_PRIM / c.z);
            an->miny = (int16_t)floorf(g.sy - ry - 3); an->maxy = (int16_t)ceilf(g.sy + ry + 4);
        }
    }
    prim_t *p = new_prim(char_bucket >= 0 ? char_bucket : b > 0 ? b - 1 : 0);
    if (!p) return;
    p->kind = K_SPRITE; p->sx = sx; p->sy = sy; p->s = s; p->col = (uint8_t)frame; p->row = (uint8_t)row; p->ry = clip_y;
#if ENG_DOF
    p->zd = dep_of(c.z);
#endif
    p->remap = (uint16_t)remap; p->L = (uint8_t)(13 - (int)lroundf(B->rain * 2.5f)); p->F = (uint8_t)F; p->sera = who == 0; p->sheet = (uint16_t)(sheet + 1);
    p->tissu = (uint16_t)(sheet >= 0 && E.w.sheets[sheet].tissu ? char_tissu : 0);
    p->teinte = (uint8_t)char_teinte;
    p->tile = (uint16_t)(ZK_PRIM / c.z);   /* clé de profondeur des pieds (× ZS au dessin) */
    p->v0 = 0;   /* île : ligne d'eau à l'écran (réfraction : draw_sprite) */
    if (E.w.ile[0] && !clip) {
        const float pe = eau_prof(x, z);
        char_view_t ve;
        if (pe > 0 && alt < pe - 0.02f && char_view(x, z, pe, &ve)) { const float yw = ve.sy + (sy - c.sy); if (yw > 0 && yw < 65000) p->v0 = (uint32_t)lroundf(yw) + 1; }
    }
    /* Image qui descend sous son ancrage (véhicule : ancré au milieu de son emprise) : ce bas est devant l'ancrage, et le
       sol qu'il recouvre passait devant lui au test de profondeur (camion coupé en bas) ; la clé est avancée comme le
       seau. Un personnage, ancré à ses pieds, garde la sienne */
    if (sous_ancrage(sheet, row)) {
        const float zf = fmaxf(c.z * 0.5f, c.z - 0.25f - 0.3f * ratio * ht * c.z / FOCAL);
        p->tile = (uint16_t)fminf(65535.0f, ZK_PRIM / zf);
    }
    if (char_devant_z > 0) p->tile = (uint16_t)fmaxf(p->tile, fminf(65535.0f, ZK_PRIM / char_devant_z));
    /* Planche propre : même hauteur à l'écran que la planche commune, fois la taille du personnage */
    if (sheet >= 0) p->s = s * E.w.fh / (float)E.w.sheets[sheet].fh * t;
    /* Ancrage de la direction (PIVO) : ce point de l'image sur la position au sol ; sans lui, un véhicule qui roule est
       posé sur son pixel le plus bas */
    if (sheet >= 0 && E.w.sheets[sheet].has_pv) {
        const sheet_t *S = &E.w.sheets[sheet];
        p->sx = sx + (S->fw / 2.0f - S->pv[row & 7][0] / 2.0f) * p->s;
        p->sy = sy + (S->fh - S->pv[row & 7][1] / 2.0f) * p->s;
    } else if (ground && sheet >= 0) p->sy = sy + E.w.sheets[sheet].foot[row & 7] * p->s;
    p->miny = (int16_t)floorf(p->sy - ht - 1); p->maxy = (int16_t)ceilf(p->sy + 1);
}
/* Profondeur (vue) la plus proche que prend le sprite d'un personnage en (x, z) dans add_character : celle de son seau,
   avancée de 0,25 et de 0,3 × sa largeur (pas plus près que la moitié de la sienne) ; 0 hors de la vue (charAvant de
   l'émulateur). *cle : la profondeur de sa clé, celle de ses pieds, ou celle du seau pour une image qui descend sous son
   ancrage dans la direction row (machine) */
static float char_avant(float x, float z, int sheet, float taille, int row, float *cle) {
    char_view_t c;
    if (!char_view(x, z, 0, &c)) return 0;
    const float t = (taille > 0 ? taille : sheet >= 0 ? E.w.sheets[sheet].taille : 1.0f) * (sheet >= 0 ? E.w.sheets[sheet].norm : 1.0f), ht = E.w.fh * c.s * t;
    const float ratio = sheet >= 0 ? (float)E.w.sheets[sheet].fw / E.w.sheets[sheet].fh : (float)E.w.fw / E.w.fh;
    const float av = fmaxf(c.z * 0.5f, c.z - 0.25f - 0.3f * ratio * ht * c.z / FOCAL);
    *cle = sous_ancrage(sheet, row) ? av : c.z;
    return av;
}

/* Eau (EAUC : toujours transparente) : sorte d'une rive (celle d'une case d'eau voisine, sinon la première des terrains) */
static int eau_autour(int i, int j) {
    for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        const chunk_t *c = chunk_of_cell(i + di, j + dj);
        const int e = c && i + di < E.w.n && j + dj < E.w.n ? c->water[cell_local(i + di, j + dj)] : 0;
        if (e && e <= E.w.neaux) return e - 1;
    }
    return E.w.neaux > 1 ? 1 : 0;
}
/* Dérive des reflets : la même pour toute l'image (fmodf, lent sur l'ESP32 : calculé une fois par heure d'image) */
static void eau_derive(float *du, float *dv) {
    static float t = NAN, u, v;
    if (E.time_s != t) { t = E.time_s; u = fmodf(E.time_s * 1.6f, 16.0f); v = fmodf(E.time_s * 0.9f, 16.0f); }
    *du = u; *dv = v;
}
/* Surface (tuile T_SURF, polygone alpha 2) : couleur du corps de l'eau de la sorte dans remap */
static int eau_poly_a(const vv_t *v, int n, int forced, int sorte, int alpha) {   /* alpha 3 : translucide (bassins, voir raster_poly) */
    const int b = submit_poly(v, n, T_SURF, forced, alpha);
    if (b >= 0) B->prims[B->nprims - 1].remap = (uint16_t)E.w.eaux[sorte].corps;
    return b;
}
static int eau_poly(const vv_t *v, int n, int forced, int sorte) { return eau_poly_a(v, n, forced, sorte, 2); }
/* Surface au niveau wh : reflets qui dérivent ; à chaque sommet, l'opacité du corps de l'eau = profondeur / profondeur
   opaque de la sorte (× 16, négative là où le sol sort de l'eau), juste devant le fond (seau bb - 1) */
/* Tuiles de surface par case (UREP_SURF de l'émulateur) : reflets deux fois plus fins */
#define UREP_SURF 2
static int eau_surface(int i, int j, float wh, int bb, int sorte) {
    static const int CX4[4] = { 0, 1, 1, 0 }, CZ4[4] = { 0, 0, 1, 1 };
    float du, dv;
    eau_derive(&du, &dv);
    if (sorte >= E.w.neaux) sorte = E.w.neaux - 1;
    vv_t q[4];
    for (int k = 0; k < 4; k++) {
        const int x = i + CX4[k], z = j + CZ4[k];
        const v3 o = to_view((float)x, wh, (float)z);
        q[k].x = o.x; q[k].y = o.y; q[k].z = o.z; q[k].u = CX4[k] * 16.0f * UREP_SURF + du; q[k].v = CZ4[k] * 16.0f * UREP_SURF + dv;
        /* plafonnée à l'opacité pleine (16) : sans quoi, entre un sommet à 30 (mer profonde) et un à 5 (lagon), la limite opaque
           collait au sommet peu profond et dessinait des marches le long du maillage (11/11/2026 ; eauSurface de l'émulateur de l'île) */
        q[k].l = (wh - hv(x, z)) / E.w.eaux[sorte].prof * 16.0f;
        if (E.w.ile[0] && q[k].l > 16.0f) q[k].l = 16.0f;   /* (île seulement : Echoes garde l'eau de son émulateur) */
    }
    cur_cat = 1;
    return eau_poly(q, 4, bb > 0 ? bb - 1 : bb, sorte);
}
/* Morceau de route d'une case ; rivière (tuile d'eau) : son lit (gravier, à la hauteur du ruban), puis la surface
   d'opacité fixe (sorte 0 : le ruban n'a pas de lit creusé), même seau que la case */
/* En course, un tablier (pont, viaduc) ou son garde-corps au-dessus du joueur de plus de PONT_HAUT, de la caméra jusqu'à
   PONT_AVANT cases devant lui et à moins de PONT_LARGE de part et d'autre, n'est pas dessiné : on voit le véhicule passer
   dessous et la piste devant lui (01/11/2026, demande de l'utilisateur ; avant : tabliers de verre translucides, chers
   sur la montre) ; sur le pont, le tablier est à la hauteur du joueur et reste dessiné. pontCache de l'émulateur */
#define PONT_AVANT 8.0f
#define PONT_LARGE 10.0f
#define PONT_HAUT 1.0f
static int pont_cache(float x, float y, float z) {
    if (!CR.on || CR.n < 1 || BT.on) return 0;
    const racer_t *j = &CR.r[CR.n - 1];
    if (y < j->y + PONT_HAUT) return 0;
    const float dx = x - j->x, dz = z - j->z, f = dx * E.cam.sy + dz * E.cam.cy, l = dx * E.cam.cy - dz * E.cam.sy;
    return f < PONT_AVANT && f > -(cam_dist_cur * E.cam.cp + 3.0f) && fabsf(l) < PONT_LARGE;
}
/* Morceau de route d'une case, dans le seau cb de son sol ; un tablier (pont : plus d'une case au-dessus du terrain) a sa
   propre profondeur, plus proche que le sol dessous : il passe devant ce qui roule en dessous (comme l'émulateur). Les
   tabliers de verre du grand circuit (rang 1 des morceaux de route) sont opaques depuis le 01/11/2026 (avant : tramés,
   puis translucides, VERRE_FADE 8 sur 16) */
static void road_piece(const pak_poly_t *pp, int cb) {
    if (poly_hors_champ(pp)) return;
    if (pp->tile != T_WATER && !(pp->flags & POLY_ALPHA) && pp->nv >= 3) {
        float x = 0, y = 0, z = 0;
        for (int i = 0; i < pp->nv; i++) { x += pp->v[i].x; y += pp->v[i].y; z += pp->v[i].z; }
        x /= pp->nv; y /= pp->nv; z /= pp->nv;
        if (y > height_at(x, z) + 1.2f) {
            if (pont_cache(x, y, z)) return;
            cb = -1;
        }
    }
    if (pp->tile != T_WATER || !E.w.neaux) {
        submit_pak_poly(pp, pp->tile, cb, pp->flags & POLY_ALPHA);
        return;
    }
    vv_t v[8] = { { 0 } };
    const int n = pp->nv > 8 ? 8 : pp->nv;
    float du, dv;
    if (n < 3) return;
    eau_derive(&du, &dv);
    for (int i = 0; i < n; i++) {
        const pak_vert_t *q = &pp->v[i];
        const v3 o = to_view(q->x, q->y, q->z);
        v[i].x = o.x; v[i].y = o.y; v[i].z = o.z; v[i].u = q->u; v[i].v = q->v; v[i].l = q->l;
    }
    submit_poly(v, n, T_BED, cb, 0);
    for (int i = 0; i < n; i++) { v[i].u = v[i].u * UREP_SURF + du; v[i].v = v[i].v * UREP_SURF + dv; v[i].l = E.w.eaux[0].prof * 16.0f; }
    eau_poly(v, n, cb, 0);
}

/* Véhicule du personnage p qui roule (posé sur la chaussée), sinon il plane : un peu au-dessus du sol, en ondulant */
int veh_rolls(int p) { return E.w.veh_roule[p] >= 0 ? E.w.veh_roule[p] : VEH_TYPE(E.w.persos[p].flags) != VEH_VAISSEAU; }
static float veh_hover(int p, float phase) { return veh_rolls(p) ? 0 : (0.15f + 0.05f * sinf(E.clock * 2.4f + phase)) / E.w.ech; }

/* Animation de combat de l'unité u (0 : Sera, BT_ENEMY + k : l'ennemi k du groupe) dont la planche de base est base, et son
   image : KO (dernière image ; un ennemi la joue pendant sa chute, depuis mort_t ; abattu mais pas encore tombé : coup reçu
   puis attente), action en cours (l'ennemi : attaque, ou technique ; un membre :
   attaque au corps à corps seulement, technique s'il l'a pour un sort, un piratage ou un objet, sinon son attente ; un tir :
   l'attente, avec le recul de battle.c),
   coup reçu (0,45 s), défense, victoire, sinon attente ; sans l'animation voulue, la marche pendant l'élan (walk_fr) et
   la première image sinon, comme avant */
/* Élan au corps à corps (ELAN_* d'internal.h ; même règle dans battleAnim de l'émulateur) : la course à l'aller et au retour (sans course : la marche ; tenue de pose : celles de sa planche de base),
   entre les deux l'attaque, sa frappe à mi-animation à l'instant du coup (FX_HIT : 0,3 à 0,36 s), revenue à sa place : l'attente */
#define TIR_ANIM 0.6f             /* tir (07/11/2026) : l'arme levée en quatre images, à peu près à l'instant du coup (FX_HIT : 0,32 s pour un tir) */

static int battle_anim(int base, int u, int walk_fr, int *frame) {
    const float now = E.clock;
    const bt_en_t *en = u >= BT_ENEMY ? &BT.en[(u - BT_ENEMY) % BT_EMAX] : NULL;
    const int hp = en ? en->hp : PARTY[u].hp;
    int a = -1; float t0 = 0, dur = 0;   /* dur > 0 : animation jouée une fois en dur secondes, sinon en boucle */
    if (en ? hp <= 0 && en->mort_t > 0 : hp <= 0) { a = ANIM_KO; t0 = en ? en->mort_t : BT.t0; dur = en ? 0.9f : 0.001f; }
    else if (BT.phase == BP_ANIM && BT.an.who == u) {
        if (en) a = !BT.an.cast ? ANIM_ATTACK : has_anim(base, ANIM_CAST) ? ANIM_CAST : ANIM_ATTACK;
        else a = BT.an.melee ? ANIM_ATTACK : BT.an.cast && has_anim(base, ANIM_CAST) ? ANIM_CAST : BT.an.tir && BT.an.fx != FX_FLECHE && has_anim(base, ANIM_TIR) ? ANIM_TIR : ANIM_IDLE;   /* un arc : pas le fusil */
        t0 = BT.an.t0; dur = a == ANIM_TIR ? TIR_ANIM : 0.7f;
        if (!en && BT.an.melee) {
            const float tm = now - BT.an.t0, tr = tm - ELAN_FRAPPE;
            if (tm < 0 || (tr >= 0 && tr < BT.an.retour)) {
                const int mv = member_sheet_mouv(u), pied = mv < 0 ? -1 : has_anim(mv, ANIM_RUN) ? ANIM_RUN : has_anim(mv, ANIM_WALK) ? ANIM_WALK : -1;
                if (pied >= 0) {
                    const int sp = anim_sheet(mv, pied), np = E.w.sheets[sp].frames;
                    *frame = (int)((tm < 0 ? tm + BT.an.elan : tr) / ELAN_CYCLE * np) % np;
                    return sp;
                }
            }
            else if (tr > 0) { a = ANIM_IDLE; dur = 0; }
        }
    }
    else if (!en && BT.avance[u] > 0.001f && BT.avance[u] < 0.999f) {   /* le pas du membre qui choisit : la marche, calée sur la distance parcourue */
        const int mv = member_sheet_mouv(u), pied = mv < 0 ? -1 : has_anim(mv, ANIM_WALK) ? ANIM_WALK : has_anim(mv, ANIM_RUN) ? ANIM_RUN : -1;
        if (pied >= 0) {
            const int sp = anim_sheet(mv, pied), np = E.w.sheets[sp].frames;
            *frame = (int)(BT.avance[u] * PAS_ACTIF / PAS_FOULEE * np) % np;
            return sp;
        }
        a = ANIM_IDLE;
    }
    else if (now - BT.hurt_t[u] < 0.45f && has_anim(base, ANIM_HURT)) { a = ANIM_HURT; t0 = BT.hurt_t[u]; dur = 0.45f; }
    else if (u < BT_ENEMY && BT.defending[u] && has_anim(base, ANIM_DEFEND)) { a = ANIM_DEFEND; t0 = -100; dur = 0.001f; }
    else if (u < BT_ENEMY && BT.phase == BP_WIN && has_anim(base, ANIM_VICTORY)) a = ANIM_VICTORY;
    else a = ANIM_IDLE;
    if (!has_anim(base, a)) {   /* animation absente : comme avant (marche pendant l'élan, sinon première image) */
        const int walk = anim_sheet(base, ANIM_WALK);
        *frame = BT.phase == BP_ANIM && BT.an.who == u && BT.an.melee ? walk_fr % E.w.sheets[walk].frames : 0;
        return BT.phase == BP_ANIM && BT.an.who == u ? walk : anim_sheet(base, ANIM_IDLE);
    }
    const int sh = anim_sheet(base, a), n = E.w.sheets[sh].frames;
    if (dur > 0) { int f = (int)((now - t0) / dur * n); *frame = f < 0 ? 0 : f >= n ? n - 1 : f; }
    else *frame = (int)(now / (a == ANIM_VICTORY ? 0.16f : 0.22f)) % n;
    return sh;
}

/* Bloc de 2 × 2 cases dessiné en un seul polygone : texture réduite disponible et loin de Sera */
/* Deux diagonales d'une case plus éloignées que ça : deux triangles. raster_poly prend le placage de la texture sur un
   seul plan (celui du plus grand triangle) : sur une case dont les quatre coins ne sont pas dans un plan, le coin restant
   recevait une texture décalée, qui ne raccordait plus avec la case voisine (éclats et traits le long des rives, 08/11/2026,
   retour de jeu : 0,35 avant, seulement contre les trous des cases très tordues) */
#define SOL_TORDU 0.02f
#define LOD_RAIDE 1.1f    /* dénivelé d'un bloc de 2 × 2 cases au-delà duquel il reste en cases : un seul polygone ne suivrait pas ce relief, et y laisserait des trous */
static int block_lod(int bi, int bj, float fx, float fz) {
    const chunk_t *c = bi < 0 || bj < 0 ? NULL : chunk_of_cell(bi * 2, bj * 2);
    if (!c || c->lod[(bj % (CS / 2)) * (CS / 2) + bi % (CS / 2)] >= LOD_PENDING) return 0;
    {
        const float *h = &c->hv[(bj * 2 % CS) * CV + bi * 2 % CS];
        float a = h[0], b = h[0];
        for (int z = 0; z < 3; z++) for (int x = 0; x < 3; x++) { const float v = h[z * CV + x]; if (v < a) a = v; if (v > b) b = v; }
        if (b - a > LOD_RAIDE) return 0;
    }
    return dist_gt(bi * 2 + 1 - fx, bj * 2 + 1 - fz, CR.on && !BT.on && course_allege ? lod_dist * COURSE_LOD : lod_dist);
}

/* Polygone d'un bloc : quatre coins, plus le milieu des côtés voisins d'un bloc dessiné case par case (ces cases
   ont un sommet à cet endroit : sans lui, la différence de hauteur laisserait une fente). Puis les routes de ses
   quatre cases, dans le même seau. */
/* Décors de la case c du chunk ch (case i, j du monde), dans le seau du sol cb (jamais avant leur case) : herbes jusqu'à
   plant_r de Sera, grands décors (plus d'une case de haut) dans tout le rayon de vue, y compris sur les blocs de sol
   simplifiés au loin ; en combat, aucun décor à moins de 9 cases de l'arène (il cacherait l'équipe et l'ennemi) */
static void cell_plants(const chunk_t *ch, int c, int i, int j, int cb, float focx, float focz) {
    const world_t *w = &E.w;
    const int bat = BT.on;
    const float rx0 = E.cam.cy, rz0 = -E.cam.sy, now = E.time_s * 1000.0f / 900.0f;
    vv_t q[4];
    const float dfx = i + 0.5f - focx, dfz = j + 0.5f - focz;
    const int near_plants = dist_lt(dfx, dfz, plant_r);
    if (ch->plant_cnt[c] && !(bat && dist_lt(dfx, dfz, 9))) {
        for (int k = 0; k < ch->plant_cnt[c]; k++) {
            const pak_plant_t *p = &ch->plants[ch->plant_first[c] + k];
            if (!near_plants && p->size <= 1) continue;
            /* Décor cassé : rocher en éclats (plus dessiné) ; arbre qui bascule dans le sens du véhicule en 0,7 s
               (sommet poussé de sin θ, abaissé de cos θ), reste couché, puis s'efface en 0,8 s (tramage) */
            float tombe = 0, cdx = 0, cdz = 0; int efface = 0;
            if ((p->flags & 4) && !bat) {
                float td;
                if (decor_casse(p->x, p->z, &td, &cdx, &cdz)) {
                    if ((p->flags & 8) || td > 2.1f) continue;
                    const float kt = fminf(1, td / 0.7f);
                    tombe = kt * kt * PI_F / 2;
                    if (td > 1.3f) efface = (int)fminf(16, lroundf((td - 1.3f) / 0.8f * 16));
                }
            }
            /* Grand décor : quadrilatère aux proportions du sprite rogné (table DECO) */
            const float size = p->size * w->sprite_scale;   /* échelle globale des sprites */
            float uv = 15.99f, ua = 0, ub = 15.99f, vt = 0, hw = size / 2;
            if (p->tile >= TILE_BIG) {
                uint32_t kd = (uint32_t)(p->tile - TILE_BIG);
                if (kd < w->deco_n) {
                    const uint8_t *r = w->deco + kd * 12;
                    const int sw = r[4] | (r[5] << 8), shh = r[6] | (r[7] << 8);
                    ub = sw - 0.01f; uv = shh - 0.01f;
                    hw = size * sw / (float)(shh ? shh : 1) / 2;
                }
            }
            float sway = (p->flags & 2) && !tombe ? sinf(now + p->phase) * 0.06f * size : 0;
            /* Orientation fixe dans le monde : sa carte et son miroir (fixe_maj), d'après la phase (octet de 0 à 255 du paquet) */
            const int fk = p->flags & 16 ? (int)(p->phase * 256 / (2 * PI_F) + 0.5f) >> 5 & 7 : -1;
            const float rx = fk >= 0 ? fixe_ax[fk] : rx0, rz = fk >= 0 ? fixe_az[fk] : rz0;
            const int fl = p->flip ^ (fk >= 0 ? fixe_mir[fk] : 0);
            float u0 = fl ? ub : ua, u1 = fl ? ua : ub;
            const float hy = size * cosf(tombe), tx = cdx * size * sinf(tombe), tz = cdz * size * sinf(tombe);
            /* Hors du champ : boîte du pied au sommet (hauteur size au plus), largeur, balancement et chute compris */
            const float ex = hw + fabsf(sway) + 0.01f;
            if (boite_hors_champ(p->x, p->y + size / 2, p->z, ex + fabsf(tx), size / 2 + 0.01f, ex + fabsf(tz))) continue;
            float P[4][5] = { { p->x - rx * hw, p->y, p->z - rz * hw, u0, uv }, { p->x + rx * hw, p->y, p->z + rz * hw, u1, uv },
                              { p->x + rx * (hw + sway) + tx, p->y + hy, p->z + rz * (hw + sway) + tz, u1, vt },
                              { p->x - rx * (hw - sway) + tx, p->y + hy, p->z - rz * (hw - sway) + tz, u0, vt } };
            for (int m = 0; m < 4; m++) {
                v3 o = to_view(P[m][0], P[m][1], P[m][2]);
                q[m].x = o.x; q[m].y = o.y; q[m].z = o.z; q[m].u = P[m][3]; q[m].v = P[m][4]; q[m].l = p->l;
            }
            int own = bucket_of(to_view(p->x, p->y, p->z).z);
            cur_cat = 2;
            const int np0 = B->nprims;
            const int pb = submit_poly(q, 4, p->tile, cb >= 0 && cb < own ? cb : own, 1);
            if (size > 1.2f && !bat && B->nprims > np0) {
                plant_fade(p->x, p->z, &B->prims[np0], q);
                if (dist_lt(i + 0.5f - focx, j + 0.5f - focz, ARB_R / w->ech)) arb_add(p->x, p->y, p->z, pb, q);
            }
            if (efface && B->nprims > np0) B->prims[np0].fade = (uint8_t)(200 + efface);   /* niveau direct (≥ 200) */
        }
    }
}

/* Sol sombre sous un bâtiment en fondu (01/11/2026, demande de l'utilisateur) : les cases qu'il couvre sont cachées (pas
   de sol sous un toit) ; à travers ses murs translucides, on voyait le fond du ciel, blanc, et les murs mêlés à lui
   semblaient disparaître. Une case cachée à moins du rayon d'un bâtiment en fondu (sa place de fondu, de l'image
   précédente) reçoit un sol T_DARK sans lumière, à son relief, ordonné comme le sol (solFondu de l'émulateur) */
static int sol_fondu(float x, float z) {
    for (int f = 0; f < FADE_N; f++) {
        const fade_t *fd = &fades[f];
        if (fd->used && fd->r > 0 && fd->a > 0 && dist_lt(x - fd->cx, z - fd->cz, fd->r)) return 1;
    }
    return 0;
}
static void sol_noir(int i, int j, const chunk_t *ch, int lx, int lz, int c) {
    static const int CX4[4] = { 0, 1, 1, 0 }, CZ4[4] = { 0, 0, 1, 1 };
    /* Le bâtiment coupé (coupe_hauteur) laisse voir son plancher : le sol de la case, à l'ombre (COUPE_SOL_OMBRE ; île, puis Echoes le 10/11/2026) */
    const int ile = !ch->water[c], t = ile ? ch->tile[c] : T_DARK;
    const float S = ile && t >= (int)E.w.g0 ? 32.0f : 16.0f;
    vv_t q[4];
    for (int k = 0; k < 4; k++) {
        const v3 o = grid_vertex(i + CX4[k], j + CZ4[k], 0);
        q[k].x = o.x; q[k].y = o.y; q[k].z = o.z; q[k].u = CX4[k] * S; q[k].v = CZ4[k] * S;
        q[k].l = ile ? ch->lv[(lz + CZ4[k]) * CV + lx + CX4[k]] * COUPE_SOL_OMBRE : 0;
    }
    cur_cat = 0;
    poly_bias = -SOL_RECUL;
    submit_poly(q, 4, t, -1, 0);
    poly_bias = 0;
}

static void submit_block(int bi, int bj, float fx, float fz) {
    static const int BX[8] = { 0, 1, 2, 2, 2, 1, 0, 0 }, BZ[8] = { 0, 0, 0, 1, 2, 2, 2, 1 };
    static const int NX[8] = { 0, 0, 0, 1, 0, 0, 0, -1 }, NZ[8] = { 0, -1, 0, 0, 0, 1, 0, 0 };
    const chunk_t *ch = chunk_of_cell(bi * 2, bj * 2);   /* block_lod l'a trouvé */
    const int lx = (bi % (CS / 2)) * 2, lz = (bj % (CS / 2)) * 2;
    /* Sol du bloc hors du champ : pas de polygone (boîte des hauteurs de ses huit sommets du bord, prises comme
       grid_vertex) ; ses routes et ses décors ont leur propre test */
    float ylo = 1e9f, yhi = -1e9f;
    for (int k = 0; k < 8; k++) { const float h = hv(bi * 2 + BX[k], bj * 2 + BZ[k]); ylo = h < ylo ? h : ylo; yhi = h > yhi ? h : yhi; }
    if (ylo < sol_bas) sol_bas = ylo;
    int cb = -1;
    if (!boite_hors_champ(bi * 2 + 1.0f, (ylo + yhi) / 2, bj * 2 + 1.0f, 1, (yhi - ylo) / 2 + 0.01f, 1)) {
        vv_t q[8];
        int m = 0;
        for (int k = 0; k < 8; k++) {
            if ((k & 1) && block_lod(bi + NX[k], bj + NZ[k], fx, fz)) continue;
            int x = bi * 2 + BX[k], z = bj * 2 + BZ[k];
            v3 o = grid_vertex(x, z, 0);
            q[m].x = o.x; q[m].y = o.y; q[m].z = o.z; q[m].u = BX[k] * 16.0f; q[m].v = BZ[k] * 16.0f;
            q[m].l = ch->lv[(lz + BZ[k]) * CV + lx + BX[k]];
            m++;
        }
        cur_cat = 0;
        poly_bias = -SOL_RECUL;
        cb = submit_poly(q, m, ch->lod[(bj % (CS / 2)) * (CS / 2) + bi % (CS / 2)], -1, 0);
        poly_bias = 0;
    }
    cur_cat = 3;
    for (int dj = 0; dj < 2; dj++) for (int di = 0; di < 2; di++) {
        int c = (lz + dj) * CS + lx + di;
        for (int k = 0; k < ch->road_count[c]; k++) {
            road_piece(chunk_poly(ch, ch->road_first[c] + k), cb);
        }
        cell_plants(ch, c, bi * 2 + di, bj * 2 + dj, cb, fx, fz);   /* décors des cases du bloc (grands décors au loin) */
    }
}

/* ---------- Intérieur (interieurScene de l'émulateur) ----------
   Sans tampon de profondeur, dans un ordre exact, tout dans le dernier seau (dessiné dans l'ordre d'arrivée) : sols et
   plaques, puis l'arbre des pièces du plus loin au plus proche de la caméra (côté lointain d'une coupe, objets posés
   sur la coupe, côté proche) ; dans une pièce, ses objets et Sera triés par leurs plans de séparation. Ce qui est
   dessiné après Sera et la recouvre s'estompe (niveau de chaque face dans in.fade). */
#define INT_ROOM_MAX 256
#define INT_TRI_MAX 1024
static float int_fade_t = -1, int_dt;
static int int_after;
static void draw_sera(void);
/* Surface d'un bassin : reflets qui dérivent, corps de sa sorte d'eau ; devant : seulement sa partie du côté de la
   caméra du plan vertical qui passe par (x, z), face à elle (devant un nageur) */
static void int_eau(const pak_poly_t *pp, int devant, float x, float z) {
    if (!E.w.neaux) return;
    vv_t v[10];
    float du, dv;
    eau_derive(&du, &dv);
    const int nv = pp->nv > 8 ? 8 : pp->nv, so = pp->flags >> 4;
    int n = 0;
    for (int k = 0; k < nv; k++) {
        const pak_vert_t *a = &pp->v[k], *b = &pp->v[(k + 1) % nv];
        const float da = devant ? (a->x - x) * E.cam.sy + (a->z - z) * E.cam.cy : -1, db = devant ? (b->x - x) * E.cam.sy + (b->z - z) * E.cam.cy : -1;
        pak_vert_t q[2]; int nq = 0;
        if (da <= 0) q[nq++] = *a;
        if ((da <= 0) != (db <= 0)) {
            const float t = da / (da - db);
            q[nq].x = a->x + (b->x - a->x) * t; q[nq].y = a->y + (b->y - a->y) * t; q[nq].z = a->z + (b->z - a->z) * t;
            q[nq].u = a->u + (b->u - a->u) * t; q[nq].v = a->v + (b->v - a->v) * t; q[nq].l = a->l + (b->l - a->l) * t; nq++;
        }
        for (int j = 0; j < nq; j++) {
            const v3 o = to_view(q[j].x, q[j].y, q[j].z);
            v[n].x = o.x; v[n].y = o.y; v[n].z = o.z; v[n].u = q[j].u + du; v[n].v = q[j].v + dv; v[n].l = q[j].l; n++;
        }
    }
    if (n >= 3) eau_poly_a(v, n, OTN - 1, so < E.w.neaux ? so : E.w.neaux - 1, 3);
}
/* Surface du bassin sous (x, z) (NULL : aucun) ; char_clip_x, char_clip_z : son bord proche, vers la caméra */
static const pak_poly_t *int_bassin(float x, float z) {
    const interieur_t *I = &E.in;
    for (int i = 0; i < I->nsol; i++) {
        const pak_poly_t *pp = int_poly(I, i);
        if (!(pp->flags & INT_EAU) || pp->nv < 3) continue;
        float x0 = pp->v[0].x, x1 = x0, z0 = pp->v[0].z, z1 = z0;
        for (int k = 1; k < pp->nv; k++) { x0 = fminf(x0, pp->v[k].x); x1 = fmaxf(x1, pp->v[k].x); z0 = fminf(z0, pp->v[k].z); z1 = fmaxf(z1, pp->v[k].z); }
        if (x < x0 || x > x1 || z < z0 || z > z1) continue;
        const float fx = E.cam.sy, fz = E.cam.cy;
        float t = 1e9f;
        if (fx > 1e-6f) t = fminf(t, (x - x0) / fx); else if (fx < -1e-6f) t = fminf(t, (x - x1) / fx);
        if (fz > 1e-6f) t = fminf(t, (z - z0) / fz); else if (fz < -1e-6f) t = fminf(t, (z - z1) / fz);
        char_clip_x = x - fx * t; char_clip_z = z - fz * t;
        return pp;
    }
    return NULL;
}
/* Pied et haut des murs de l'intérieur chargé (relevés une fois par niveau) : la coupe va du haut au pied, linteaux compris */
static void int_murs_y(const interieur_t *I, float *lo, float *hi) {
    static int cur = -1, np = -1; static float l, h;
    if (cur != I->cur || np != I->npoly) {
        cur = I->cur; np = I->npoly; l = 1e9f; h = -1e9f;
        for (int i = 0; i < I->npoly; i++) {
            const pak_poly_t *pp = int_poly(I, i);
            if (!(pp->flags & INT_WALL)) continue;
            for (int k = 0; k < pp->nv && k < 8; k++) { l = fminf(l, pp->v[k].y); h = fmaxf(h, pp->v[k].y); }
        }
        if (l > h) { l = 0; h = I->h; }
    }
    *lo = l; *hi = h;
}
static int int_mur_cache = 0;   /* l'objet mur en cours cache l'héroïne (int_draw_obj : couverture de ses faces) */
static void int_submit(interieur_t *I, int i) {
    const pak_poly_t *pp = int_poly(I, i);
    if (culled(pp)) return;
    if (pp->flags & INT_EAU) { int_eau(pp, 0, 0, 0); return; }
    if ((pp->flags & INT_WALL) && occ_sera >= 0) {   /* le mur qui cache l'héroïne est coupé (coupe_hauteur), pas estompé */
        float lo, hi;
        I->fade[i] = int_mur_cache ? 255 : 0;   /* d'un coup, sans descente */
        if (int_mur_cache) { int_murs_y(I, &lo, &hi); submit_pak_poly_coupe(pp, pp->tile, OTN - 1, pp->flags & POLY_ALPHA, coupe_hauteur(lo, hi, 1), 0); }
        else submit_pak_poly(pp, pp->tile, OTN - 1, pp->flags & POLY_ALPHA);
        return;
    }
    submit_pak_poly(pp, pp->tile, OTN - 1, pp->flags & POLY_ALPHA);
}
/* Triangles d'un meuble low poly : du plus loin au plus proche (centre de chaque face) */
typedef struct { float z; int i; } int_tri_t;
static int tri_cmp(const void *a, const void *b) { const float d = ((const int_tri_t *)b)->z - ((const int_tri_t *)a)->z; return d > 0 ? 1 : d < 0 ? -1 : 0; }
/* Affiche au mur (o->mur, 03/11/2026) : elle s'estompe avec le polygone du pan de mur qui la porte, pas par elle-même (avant,
   elle restait seule, opaque, devant son mur en fondu ; draw de interieurScene de l'émulateur, colleMur, à garder identiques).
   Le pan : une face de son mur tournée du même côté qu'elle, qu'elle a devant soi, la plus proche une fois son centre
   projeté sur le plan de la face (distance au plan et débord du rectangle de la face). 08/11/2026, retour de jeu : des
   affiches proches disparaissaient selon l'angle de la caméra ; l'ancien test (centre dans le rectangle au sol d'une face, à
   1 cm près) ne trouvait jamais le pan, l'affiche étant décollée de son mur, et elle prenait le fondu de la première face
   de l'objet mur, une autre (jusqu'au fondu complet) ; aucun pan : pas de fondu */
int int_affiche_pan(const interieur_t *I, const int_obj_t *o) {
    const int_obj_t *W = &I->obj[o->mur - 1];
    const pak_poly_t *c = int_poly(I, o->p0);
    if (c->nv < 1) return -1;
    float x = 0, y = 0, z = 0;
    for (int v = 0; v < c->nv; v++) { x += c->v[v].x; y += c->v[v].y; z += c->v[v].z; }
    x /= c->nv; y /= c->nv; z /= c->nv;
    int wp = -1; float best = 1e9f;
    for (int k = W->p0; k < W->p0 + W->np; k++) {
        const pak_poly_t *q = int_poly(I, k);
        if (q->nv < 3 || q->n[0] * c->n[0] + q->n[1] * c->n[1] + q->n[2] * c->n[2] < 0.9f) continue;
        const float d = (x - q->v[0].x) * q->n[0] + (y - q->v[0].y) * q->n[1] + (z - q->v[0].z) * q->n[2];
        if (d < -0.01f) continue;
        const float px = x - d * q->n[0], py = y - d * q->n[1], pz = z - d * q->n[2];
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
        for (int v = 0; v < q->nv; v++) { x0 = fminf(x0, q->v[v].x); x1 = fmaxf(x1, q->v[v].x); y0 = fminf(y0, q->v[v].y); y1 = fmaxf(y1, q->v[v].y); z0 = fminf(z0, q->v[v].z); z1 = fmaxf(z1, q->v[v].z); }
        const float s = fabsf(d) + fmaxf(fmaxf(x0 - px, px - x1), 0) + fmaxf(fmaxf(y0 - py, py - y1), 0) + fmaxf(fmaxf(z0 - pz, pz - z1), 0);
        if (s < best) { best = s; wp = k; }
    }
    return wp;
}
static void int_draw_colle(interieur_t *I, const int_obj_t *o) {
    const int wp = int_affiche_pan(I, o);
    for (int i = o->p0; i < o->p0 + o->np; i++) {
        const pak_poly_t *pp = int_poly(I, i);
        if (culled(pp)) continue;
        I->fade[i] = wp >= 0 ? I->fade[wp] : 0;   /* coupée avec son pan de mur */
        float lo, hi;
        if (I->fade[i]) { int_murs_y(I, &lo, &hi); submit_pak_poly_coupe(pp, pp->tile, OTN - 1, pp->flags & POLY_ALPHA, coupe_hauteur(lo, hi, I->fade[i] / 255.0f), 0); }
        else submit_pak_poly(pp, pp->tile, OTN - 1, pp->flags & POLY_ALPHA);
    }
}
static void int_draw_obj(interieur_t *I, const int_obj_t *o) {
    if (o->mur && o->np) { int_draw_colle(I, o); return; }
    if (!o->mesh) {
        /* un objet mur dessiné après l'héroïne (devant elle) la cache-t-il ? Union de ses faces (couverture), seuil abaissé s'il était coupé */
        int_mur_cache = 0;
        if (int_after && occ_sera >= 0) {
            uint32_t cv[3] = { 0, 0, 0 };
            int coupe = 0, murs = 0;
            for (int i = o->p0; i < o->p0 + o->np; i++) {
                const pak_poly_t *pp = int_poly(I, i);
                if (!(pp->flags & INT_WALL) || culled(pp)) continue;
                murs = 1; coupe |= I->fade[i] != 0;
                float sx[16], sy[16], bx[4];
                const int n = poly_ecran(pp, sx, sy, bx);
                const occ_t *so = &occ[occ_sera];
                if (n >= 3 && !(bx[1] < so->x0 || bx[0] > so->x1 || bx[3] < so->y0 || bx[2] > so->y1)) cv[0] |= couv_points(so, sx, sy, n);
            }
            int_mur_cache = murs && couv_cache(cv, coupe);
            if (int_mur_cache) B->sera_faded = 1;
        }
        for (int i = o->p0; i < o->p0 + o->np; i++) int_submit(I, i);
        int_mur_cache = 0;
        return;
    }
    static int_tri_t T[INT_TRI_MAX] ENG_FROID;
    int n = 0;
    for (int k = 0; k < o->np && n < INT_TRI_MAX; k++) {
        const pak_poly_t *pp = int_poly(I, o->p0 + k);
        if (culled(pp)) continue;
        float x = 0, y = 0, z = 0;
        for (int v = 0; v < pp->nv; v++) { x += pp->v[v].x; y += pp->v[v].y; z += pp->v[v].z; }
        T[n].z = to_view(x / pp->nv, y / pp->nv, z / pp->nv).z; T[n].i = o->p0 + k; n++;
    }
    qsort(T, (size_t)n, sizeof T[0], tri_cmp);
    for (int k = 0; k < n; k++) int_submit(I, T[k].i);
}
/* A avant B (+1), B avant A (-1), indifférent (0) : un plan vertical les sépare, le plus proche est du côté de la caméra */
typedef struct { float x0, z0, x1, z1; } int_rect_t;
static float int_cx, int_cz;
static int avant(const int_rect_t *A, const int_rect_t *Bo) {
    if (A->x1 <= Bo->x0 + 1e-3f) return int_cx < (A->x1 + Bo->x0) / 2 ? -1 : 1;
    if (Bo->x1 <= A->x0 + 1e-3f) return int_cx < (Bo->x1 + A->x0) / 2 ? 1 : -1;
    if (A->z1 <= Bo->z0 + 1e-3f) return int_cz < (A->z1 + Bo->z0) / 2 ? -1 : 1;
    if (Bo->z1 <= A->z0 + 1e-3f) return int_cz < (Bo->z1 + A->z0) / 2 ? 1 : -1;
    return 0;
}
/* Personnages de la pièce (Sera, ceux de l'intérieur) : rectangle au sol, exemplaire (-1 : Sera), haut : sur un sol surélevé
   (escalier, gradin, estrade) */
typedef struct { int_rect_t r; int npc, haut; } int_perso_t;
static void draw_int_npc(int k) {
    const npc_t *nc = &E.npcs[k];
    {   /* île : la succube de la séquence « s'offrir à elle » (ile.c), son animation d'offrande */
        float ox, oz, od; int op, osh, ofr;
        const int oi = k == ile_offrande_npc(0) ? 0 : k == ile_offrande_npc(1) ? 1 : -1;   /* (1 : sa partenaire, paire) */
        const int oa = oi >= 0 ? ile_offrande_acteur(oi, &ox, &oz, &od, &op, &osh, &ofr) : 0;
        if (oa < 0) return;   /* dans la scène de l'emplacement (ou pendant son cut-in) */
        if (oa > 0) {
            const perso_vue_t ov = perso_vue(nc->perso, nc->var);
            char_bucket = OTN - 1; char_tissu = oa == 2 ? 0 : ov.tissu;
            add_character(ox, oz, ofr, sera_row(od), oa == 2 ? 0 : ov.remap, osh, 2, ov.taille, 0, 0, ov.ombre);   /* (2 : la scène, ses couleurs telles quelles) */
            char_tissu = 0; char_bucket = -1;
            return;
        }
    }
    const perso_t *pe = &E.w.persos[nc->perso];
    const perso_vue_t pv = perso_vue(nc->perso, nc->var);   /* (la recrue de l'héroïne choisie : en Sera) */
    /* Une danseuse (drapeau de sa place) danse si sa planche a l'animation, sinon elle attend ; une voiture garée (garages) :
       son image posée au sol */
    const int i = k - E.in.npc0, danse = i >= 0 && i < E.in.npnj && E.in.pnj[i].danse && has_anim(pv.sheet, ANIM_DANSE);
    /* Nageur (bassin), comme dans un lac : sa tenue de nage, le sprite entier, NAGE_SINK de sa hauteur sous la surface ;
       la partie de la surface entre lui et la caméra, soumise après lui, voile son corps immergé (sa tête reste nette) ;
       il ne s'arrête qu'au bord proche du bassin, que le sol de la pièce cache */
    const int nage = i >= 0 && i < E.in.npnj && E.in.pnj[i].nage, tn = nage ? tenue_nage(nc->perso) : -1;
    const int ps = tn >= 0 ? E.w.tenues[tn].sheet : pv.sheet;
    /* qui marche (clients, garde de la prison : police.c) : sa marche (01/11/2026 ; il glissait dans son attente) */
    const int marche = !danse && nc->walking && has_anim(ps, ANIM_WALK);
    /* Danse de foule (DANSEUR_* d'internal.h) : qui danse sans animation de danse garde son attente (la marche, une place
       de 32 Ko par direction dans le cache des planches de la montre, le saturait avec une dizaine de danseurs qui
       tournent), sautille (de 60 à 100 % de DANSEUR_SAUT selon sa place) et se tourne de 45° à droite ou à gauche tous
       les deux temps (DANSEUR_TOUR) ; sa marche seulement pendant ses quelques pas (int_danseurs), un pas par temps */
    static const int8_t DANSEUR_TOUR[8] = { 0, 1, 0, -1, 0, -1, 0, 1 };
    const int foule = i >= 0 && i < E.in.npnj && E.in.pnj[i].danse && !danse && !nage;
    const float tb = E.time_s * (DANSEUR_BPM / 60);
    const int pas = foule && nc->walking && has_anim(ps, ANIM_WALK);
    const int an = danse ? ANIM_DANSE : marche && nage && has_anim(ps, ANIM_NAGE) ? ANIM_NAGE : marche || pas ? ANIM_WALK : ANIM_IDLE, sh = anim_sheet(ps, an);   /* (nageur : la nage de sa tenue) */
    const int fr = !has_anim(ps, an) || sh < 0 ? 0 : pas ? (int)(tb * E.w.sheets[sh].frames / 2) % E.w.sheets[sh].frames
                 : marche ? nc->frame % E.w.sheets[sh].frames : (int)(E.time_s * 1000 / 220) % E.w.sheets[sh].frames;
    const float ys = INT_EAU_Y * (E.in.f > 0 ? E.in.f : 1), H = 2.0f / E.w.ech * E.w.sprite_scale * (pv.taille > 0 ? pv.taille : ps >= 0 ? E.w.sheets[ps].taille : 1) * (ps >= 0 ? E.w.sheets[ps].norm : 1);
    char_bucket = OTN - 1;
    const pak_poly_t *bassin = nage ? int_bassin(nc->x, nc->z) : NULL;
    const int veh = (pe->flags & PERS_VEHICULE) && veh_rolls(nc->perso);
    /* Pole dance (scène à barre, 03/11/2026, demande de l'utilisateur) : la danseuse tourne autour de la barre, sa direction
       avance de POLE_TOUR rad/s (chaque direction de sa danse est calée sur la barre, qui ne bouge pas) */
    const float dir = danse && E.in.pnj[i].barre ? nc->dir + (float)fmod(E.time_s * POLE_TOUR, 2 * PI_F)
                    : foule && k != E.talk ? nc->dir + DANSEUR_TOUR[((int)tb / 2 + i * 3) & 7] * (PI_F / 4) : nc->dir;
    const float saut = foule ? DANSEUR_SAUT * (E.in.f > 0 ? E.in.f : 1) * (0.6f + 0.4f * (float)(i * 5 % 4) / 3) * fabsf(sinf(PI_F * tb)) : 0;
    char_tissu = pv.tissu;
    add_character(nc->x, nc->z, fr, sera_row(dir), pv.remap, sh, k == E.talk ? 1 : 2, pv.taille, nage ? ys - NAGE_SINK * H : saut, veh, pv.ombre);
    char_tissu = 0;
    char_clip_x = NAN;
    if (bassin) int_eau(bassin, 1, nc->x, nc->z);
    char_bucket = -1;
}
/* Ordre de i et j dans une pièce (avant) ; un personnage sur un sol surélevé dont l'objet contient les pieds passe après lui
   (03/11/2026 : leurs rectangles se recouvrent, avant ne tranchait pas, et la distance des centres décidait au hasard :
   l'estrade passait devant la danseuse ; ordre de room dans interieurScene de l'émulateur) */
static int int_porte_pied(const int_rect_t *O, const int_rect_t *P) {
    const float cx = (P->x0 + P->x1) / 2, cz = (P->z0 + P->z1) / 2;
    return cx >= O->x0 && cx <= O->x1 && cz >= O->z0 && cz <= O->z1;
}
static int int_ordre(const int_rect_t *R, int i, int j, int count, const int_perso_t *pers) {
    const int r = avant(&R[i], &R[j]);
    if (r) return r;
    if (i < count && j >= count && pers[j - count].haut && int_porte_pied(&R[i], &R[j])) return 1;
    if (j < count && i >= count && pers[i - count].haut && int_porte_pied(&R[j], &R[i])) return -1;
    return 0;
}
/* Pièce : ses objets et ses personnages (pers) en tri topologique ; sans ordre imposé ou en cas de cycle, le plus loin
   de la caméra d'abord */
static void int_room(interieur_t *I, int first, int count, const int_perso_t *pers, int npers) {
    static int_rect_t R[INT_ROOM_MAX + 2 + INT_PNJ_MAX] ENG_FROID;
    static int16_t cnt[INT_ROOM_MAX + 2 + INT_PNJ_MAX] ENG_FROID;
    static uint8_t done[INT_ROOM_MAX + 2 + INT_PNJ_MAX];
    static float d[INT_ROOM_MAX + 2 + INT_PNJ_MAX] ENG_FROID;
    if (count > INT_ROOM_MAX) count = INT_ROOM_MAX;
    if (npers > 2 + INT_PNJ_MAX) npers = 2 + INT_PNJ_MAX;
    const int n = count + npers;
    for (int i = 0; i < n; i++) {
        if (i < count) { const int_obj_t *o = &I->obj[first + i]; R[i].x0 = o->x0; R[i].z0 = o->z0; R[i].x1 = o->x1; R[i].z1 = o->z1; }
        else R[i] = pers[i - count].r;
        d[i] = hypotf((R[i].x0 + R[i].x1) / 2 - int_cx, (R[i].z0 + R[i].z1) / 2 - int_cz);
        cnt[i] = 0; done[i] = 0;
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
        const int r = int_ordre(R, i, j, count, pers);
        if (r > 0) cnt[j]++; else if (r < 0) cnt[i]++;
    }
    for (int k = 0; k < n; k++) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            if (best < 0 || (cnt[i] == 0 && cnt[best] != 0) || ((cnt[i] == 0) == (cnt[best] == 0) && d[i] > d[best])) best = i;
        }
        done[best] = 1;
        for (int j = 0; j < n; j++) if (!done[j] && int_ordre(R, best, j, count, pers) > 0) cnt[j]--;
        if (best < count) int_draw_obj(I, &I->obj[first + best]);
        else if (pers[best - count].npc < 0) draw_sera();
        else draw_int_npc(pers[best - count].npc);
    }
}
/* Feuille de l'arbre (pièce) du point (x, z) du plan */
static int int_leaf(const interieur_t *I, float x, float z) {
    int leaf = 0;
    for (int guard = 0; I->node[leaf].kind && guard < 64; guard++) leaf = (I->node[leaf].kind == 1 ? x : z) < I->node[leaf].c ? I->node[leaf].lo : I->node[leaf].hi;
    return leaf;
}
static int_perso_t int_pers[2 + INT_PNJ_MAX]; static int int_pers_leaf[2 + INT_PNJ_MAX], int_npers;   /* (Sera, les places, un rôdeur entré : ile.c) */
static void int_visit(interieur_t *I, int k) {
    const int_node_t *nd = &I->node[k];
    if (!nd->kind) {
        static int_perso_t here[2 + INT_PNJ_MAX]; int nh = 0;   /* (statique : int_visit est récursif, la pile du jeu fait 8 Ko) */
        for (int i = 0; i < int_npers; i++) if (int_pers_leaf[i] == k) here[nh++] = int_pers[i];
        int_room(I, nd->first, nd->count, here, nh);
        return;
    }
    const int near = (nd->kind == 1 ? int_cx : int_cz) < nd->c ? nd->lo : nd->hi;
    int_visit(I, near == nd->lo ? nd->hi : nd->lo);
    for (int i = 0; i < nd->count; i++) int_draw_obj(I, &I->obj[nd->first + i]);
    int_visit(I, near);
}
/* Ombre du joueur : celle de l'héroïne choisie, sinon celle de Sera */
#define OMBRE_SERA (E.heroine >= 0 ? E.heroine : OMBRE_JOUEUR)
/* Planche de l'animation de Sera selon son état : 0 attente, 1 marche, 2 course, 3 pose du scénario (instruction « pose » :
   son animation, sinon l'attente) */
static int sera_anim(int ps) {
    const int st = E.sera.state;
    if (st == 3) return anim_sheet(ps, has_anim(ps, E.pose_anim) ? E.pose_anim : ANIM_IDLE);
    if (st != 0 && E.nage && has_anim(ps, ANIM_NAGE)) return anim_sheet(ps, ANIM_NAGE);   /* à la nage : la nage de sa tenue */
    if (st != 0) ps = member_sheet_mouv(0);   /* tenue de pose : la marche et la course de sa planche de base */
    return anim_sheet(ps, st == 0 ? ANIM_IDLE : st == 2 && has_anim(ps, ANIM_RUN) ? ANIM_RUN : ANIM_WALK);
}
static void draw_sera(void) {
    if (ile_offrande_cache_sera()) { int_after = 1; return; }   /* île : une animation « à deux » de la succube (ile.c) */
    const int ps = member_sheet(0);
    char_bucket = OTN - 1;
    if (ps >= 0) {
        const int st = E.sera.state, sh = sera_anim(ps);
        const int fr = st == 0 && !has_anim(ps, ANIM_IDLE) ? 0 : E.sera.frame % E.w.sheets[sh].frames;
        /* À la nage (bassin) : NAGE_SINK de sa hauteur sous la surface, voilée par l'eau devant elle (draw_int_npc) */
        const float ys = INT_EAU_Y * (E.in.f > 0 ? E.in.f : 1), H = 2.0f / E.w.ech * E.w.sprite_scale * joueur_taille() * E.w.sheets[ps].norm;
        const pak_poly_t *bassin = E.nage ? int_bassin(E.sera.x, E.sera.z) : NULL;
        char_tissu = member_tissu(0); char_teinte = eveil_hors_combat();
        add_character(E.sera.x, E.sera.z, fr, sera_row(E.sera.dir), 0, sh, 0, joueur_taille(), E.nage ? ys - NAGE_SINK * H : 0, 0, OMBRE_SERA);
        char_tissu = char_teinte = 0;
        char_clip_x = NAN;
        if (bassin) int_eau(bassin, 1, E.sera.x, E.sera.z);
    } else add_character(E.sera.x, E.sera.z, E.sera.frame % 4, sprite_row(E.sera.dir), 0, -1, 0, 0, 0, 0, -1);
    char_bucket = -1;
    int_after = 1;
}
static void interieur_scene(float time_s) {
    interieur_t *I = &E.in;
    int_dt = int_fade_t < 0 ? 0 : time_s - int_fade_t;
    if (int_dt < 0 || int_dt > 0.1f) int_dt = 0.1f;
    int_fade_t = time_s;
    B->rain = 0; B->nrain = 0; B->sera_faded = 0;
    int_after = 0;
    int_cx = E.cam.x - IO; int_cz = E.cam.z - IO;
    const float sx = E.sera.x - IO, sz = E.sera.z - IO, rs = 0.15f * (I->f > 0 ? I->f : 1);
    for (int i = 0; i < I->nsol; i++) int_submit(I, i);
    /* Sera et les personnages de l'intérieur, chacun dans sa pièce */
    int_npers = 0;
    int_pers[0].r = (int_rect_t){ sx - rs, sz - rs, sx + rs, sz + rs }; int_pers[0].npc = -1; int_pers[0].haut = ground_y(E.sera.x, E.sera.z) > 0.01f;
    int_pers_leaf[0] = int_leaf(I, sx, sz); int_npers = 1;
    for (int k = I->npc0; k < I->npc0 + I->npcn && int_npers < 2 + INT_PNJ_MAX; k++) {
        const float px = E.npcs[k].x - IO, pz = E.npcs[k].z - IO;
        int_pers[int_npers].r = (int_rect_t){ px - rs, pz - rs, px + rs, pz + rs }; int_pers[int_npers].npc = k; int_pers[int_npers].haut = int_sol_y(E.npcs[k].x, E.npcs[k].z) > 0.01f;
        int_pers_leaf[int_npers++] = int_leaf(I, px, pz);
    }
    int_visit(I, 0);
    if (!int_after) draw_sera();
}

/* Cibles de chasse à dessiner (ciblesVues de l'émulateur) : sur la carte courante, quête à l'étape de l'événement, pas
   doublées par un acteur de leur personnage ni par un exemplaire posé à moins de CIBLE_DOUBLE cases, dans le rayon r et
   devant la caméra ; les CIBLE_DRAW plus proches de Sera, dans out (indices de w->cibles) */
static int cibles_vues(int *out, float r) {
    const world_t *w = &E.w;
    float od[CIBLE_DRAW];
    int n = 0;
    for (int k = 0; k < w->ncibles; k++) {
        const cible_t *c = &w->cibles[k];
        if (c->vaincue || c->carte != w->carte || !script_cible_active(c) || w->persos[c->perso].sheet < 0 || acteur_de(c->perso) >= 0) continue;
        const float d = hypotf(c->x - E.sera.x, c->z - E.sera.z);
        if (d > r || (c->x - E.cam.x) * E.cam.sy + (c->z - E.cam.z) * E.cam.cy < 1) continue;
        int dbl = 0;
        for (int j = 0; j < E.nnpc && !dbl; j++)
            dbl = E.npcs[j].perso == c->perso && npc_present(&E.npcs[j]) && hypotf(E.npcs[j].x - c->x, E.npcs[j].z - c->z) < CIBLE_DOUBLE;
        if (dbl) continue;
        int i = n;
        if (n < CIBLE_DRAW) n++;
        else if (d >= od[CIBLE_DRAW - 1]) continue;
        else i = CIBLE_DRAW - 1;
        while (i > 0 && od[i - 1] > d) { out[i] = out[i - 1]; od[i] = od[i - 1]; i--; }
        out[i] = k; od[i] = d;
    }
    return n;
}

static void image_finir(void);
static void ag_image(float time_s);
void eng_build_frame(float time_s) {
    const world_t *w = &E.w;
    const int n = w->n;
    BP0();
    if (AG.on) { ag_image(time_s); return; }   /* course anti-gravité : sa scène, sans le monde */
    stream_frame();
    E.time_s = time_s;
    {   /* brouillard de la pluie de cendre (fog_of) : 8 niveaux sur une profondeur 1 + CENDRE_BRUME × l'intensité fois plus courte ;
           vol permanent (système solaire) : VOL_BRUME, plus loin (on voit les astres venir) */
        const float k = vol_permanent() ? VOL_BRUME : 1 + CENDRE_BRUME * cendre_dehors();
        dist_maj();   /* distance d'affichage selon le zoom : brouillard, rayon de vue, sol simplifié, herbes */
        const float z0 = fog_z0 * dist_cur;
        fog_fullc = z0 + (fog_full * dist_cur - z0) / k;
        fog_z0c = z0 + (fog_fullc - z0) * VILLE_RECUL * E.ville_v;
        fog_kc = 8.0f / (fog_fullc - fog_z0c);
    }
    /* Combat : caméra fixe orientée sur l'arène (BCAM de l'émulateur), léger balancement ; le décor se centre sur
       l'arène. Sinon, la caméra suit Sera ; en vaisseau, elle suit l'appareil en altitude, un peu plus loin. */
    const int bat = BT.on, hold = E.stream_hold;
    float focx = E.sera.x, focz = E.sera.z;
    if (bat) {
        float fx, fz; battle_center(&fx, &fz);
        const float sw = sinf(E.clock * 1000 / 2600) * 0.08f;
        setup_camera(fx, height_at(fx, fz) + BAT_HAUT / w->ech, fz, BAT_YAW + sw, BAT_PITCH, BAT_DIST / w->ech);
        focx = fx; focz = fz;
    } else {
        const int ship = ride_type() == VEH_VAISSEAU && !CR.on;   /* en course : altitude imposée */
        /* Vaisseau : la caméra suit son altitude, un peu plus loin ; véhicule qui coule : sa hauteur au-dessus du fond ;
           véhicule qui plane sur l'eau : la surface */
        const float alt = ship ? E.ride_alt : E.ride >= 0 && E.ride_coule > 0 ? E.ride_alt
            : E.ride >= 0 && !CR.on && veh_surface(E.npcs[E.ride].perso) ? eau_prof(E.sera.x, E.sera.z)
            : E.w.ile[0] && E.ride < 0 && E.nage && !E.in.on ? swim_alt() : 0;   /* île : à la nage, la surface (en mer profonde, la caméra passait sous l'eau, 11/11/2026 ; computeCamera de l'émulateur de l'île) */
        if (MISE.cam_on) {   /* caméra de script (instruction « camera ») : son point, son lacet, son zoom */
            /* cam[4] : hauteur visée (1 / ech, ou « hauteur ») ; sur l'île, au-dessus de l'eau qui couvre le sol (11/11/2026, retour de
               l'utilisateur : la traversée du prologue filmait le fond, 6 m sous la barque ; miseCamera de l'émulateur de l'île) */
            setup_camera(MISE.cam[0], ground_y(MISE.cam[0], MISE.cam[1]) + (E.w.ile[0] ? eau_prof(MISE.cam[0], MISE.cam[1]) : 0) + MISE.cam[4], MISE.cam[1], MISE.cam[2], E.pitch, 12.5f / w->ech / MISE.cam[3]);
            focx = MISE.cam[0]; focz = MISE.cam[1];
        } else {
            /* (vol permanent : à la distance d'ordinaire et plus à l'horizontale, VOL_PITCH : on regarde devant soi, les
               astres au milieu de l'image ; de plus loin et plus haut, ils restaient en haut de l'écran, dans le brouillard) */
            const int vol = vol_permanent();
            setup_camera(E.sera.x, ground_y(E.sera.x, E.sera.z) + course_alt_joueur() + 1.0f / w->ech + alt, E.sera.z, E.yaw, vol ? VOL_PITCH : E.pitch, E.cam_dist * (1 + (ship && !vol ? 0.35f * alt / (SHIP_ALT / w->ech) : 0)));
        }
    }
    /* Secousse : centre de projection décalé de -4 à +3 pixels natifs × sa force (au-delà de 1, elle retombe à 1 sur
       ses 0,25 dernières secondes) */
    pcx = CXF; pcy = CYF;
    dlg_glisse = 1;
#if !ENG_DYN
    /* Dialogue (07/11/2026, revue des caméras) : le panneau couvre le bas de l'écran, et le point visé en occupait le milieu : on ne voyait que le sol derrière les
       personnages. Le centre de l'image monte de DLG_DECAL, en DLG_VITE : le sujet est dans la partie visible (dlgV de l'émulateur) */
    if (!bat) {
        static float dlg_v, dlg_t, dlg_x, dlg_y, dlg_xc, dlg_yc;
        const float dt_d = fminf(0.1f, fmaxf(0, time_s - dlg_t)), cible_d = hud_cover_y() > 0 || hud_in_choice() ? 1 : 0;
        dlg_t = time_s;
        dlg_v = dlg_v < cible_d ? fminf(cible_d, dlg_v + dt_d * DLG_VITE) : fmaxf(cible_d, dlg_v - dt_d * DLG_VITE);
        pcy -= DLG_DECAL * dlg_v * (2 - dlg_v);
        dlg_glisse = cible_d > 0 ? dlg_v : 1;
        float px_d, pz_d;
        char_view_t cv_d;
        eng_dlg_hors = 0;
        if (!cible_d) dlg_xc = dlg_yc = 0;
        else if (hud_parle_pos(&px_d, &pz_d)) {
            int hors = 1;
            if (char_view(px_d, pz_d, 0, &cv_d)) {
                const float ex = cv_d.sx - CXF, h = E.w.fh * cv_d.s;
                float vx = dlg_xc, vy = dlg_yc;
                if (ex + vx > DLG_GAP) vx = DLG_GAP - ex; else if (ex + vx < -DLG_GAP) vx = -DLG_GAP - ex;
                if (cv_d.sy + vy > DLG_PIED) vy = DLG_PIED - cv_d.sy;
                else if (cv_d.sy + vy - h < DLG_TETE) vy = fminf(DLG_TETE - (cv_d.sy - h), DLG_PIED - cv_d.sy);
                hors = fabsf(vx) > DLG_X_MAX || fabsf(vy) > DLG_Y_MAX;
                if (!hors) { dlg_xc = vx; dlg_yc = vy; }
            }
            if (hors && (px_d != E.sera.x || pz_d != E.sera.z)) { eng_dlg_hors = 1; eng_dlg_x = px_d; eng_dlg_z = pz_d; }   /* Sera, hors de l'image : un plan voulu, sur ce dont elle parle */
        }
        const float k_d = fminf(1, dt_d * DLG_SUIT);
        dlg_x += (dlg_xc - dlg_x) * k_d; dlg_y += (dlg_yc - dlg_y) * k_d;
        pcx += dlg_x; pcy += dlg_y;
    }
#endif
    if ((bat && BT.shake > 0) || (!bat && MISE.sec_t > 0)) {   /* hors combat : instruction « secousse » */
        const float amp = bat ? BT.shake_amp : MISE.sec_f, rest = bat ? BT.shake : MISE.sec_t;
        const float sa = amp > 1 ? 1 + (amp - 1) * fminf(1, rest / 0.25f) : amp;
        pcx += (int)((rng_next() * 8 - 4) * sa) / (float)ENG_SCALE; pcy += (int)((rng_next() * 8 - 4) * sa) / (float)ENG_SCALE;
    }
    champ_init();
    B->nverts = B->nprims = 0;
    memset(&B->stats, 0, sizeof B->stats);
    for (int k = 0; k < ENG_WORKERS; k++) B->px_w[k] = B->seg_w[k] = 0, B->us3d_w[k] = B->usui_w[k] = 0;
    E.scr_sera.vis = E.scr_npc.vis = 0;
    for (int k = 0; k < BT_EMAX; k++) E.scr_enemy[k].vis = 0;
    B->torche = !bat && torche_on(); B->torche_x = B->torche_y = -1e4f;   /* centre posé avec Sera (add_character) */
    lum_setup(bat);
    B->ztest = !E.in.on && !eng_sans_z;   /* (eng_sans_z : banc d'essai, comme #sans-z de l'émulateur) */
    for (int k = 0; k < NMEMB; k++) E.scr_party[k].vis = 0;
    B->stats.chunk_loads = (int)E.stream_loads; B->stats.us_stream = E.stream_us;
    E.stream_loads = E.stream_us = 0;
    for (int b = 0; b < OTN; b++) ot_head[b] = ot_tail[b] = -1;
    occ_poly = NULL; nocc = 0; occ_sera = -1; narb = 0;
    /* Exemplaires dessinés : les NPC_DRAW plus proches de Sera, présents, devant la caméra, dans le rayon du décor ; en
       combat, aucun personnage du monde (ni Sera, ni les exemplaires) */
    int shown[NPC_DRAW], nshown = 0;
    float shown_d[NPC_DRAW];
    if (!bat) {
        occ_add(E.sera.x, E.sera.z);
        occ_sera = nocc > 0 ? 0 : -1;
    }
    sujet_on = 0;
    if (!bat && MISE.cam_on && !MISE.cam_retour) {
        const int n0 = nocc;
        occ_add(MISE.cam[0], MISE.cam[1]);
        if (nocc > n0) {
            occ_sujet = occ[n0]; nocc = n0; sujet_on = 1;
            const float l = occ_sujet.x1 - occ_sujet.x0;
            occ_sujet.x0 -= l; occ_sujet.x1 += l;
        }
    }
    parle_on = 0;
    {
        float px_o, pz_o;
        if (!bat && hud_parle_pos(&px_o, &pz_o) && (px_o != E.sera.x || pz_o != E.sera.z)) {
            const int n0 = nocc;
            occ_add(px_o, pz_o);
            if (nocc > n0) { occ_parle = occ[n0]; nocc = n0; parle_on = 1; }
        }
    }
    for (int k = 0; k < (bat ? 0 : E.nnpc); k++) {
        const npc_t *nc = &E.npcs[k];
        const float vr = (float)view_r, dx = nc->x - E.sera.x, dz = nc->z - E.sera.z;
        if (fabsf(dx) > vr || fabsf(dz) > vr || !npc_present(nc) || acteur_de(nc->perso) >= 0) continue;   /* remplacé par son acteur */
        float d = hypotf(dx, dz);
        if (d > vr || (nc->x - E.cam.x) * E.cam.sy + (nc->z - E.cam.z) * E.cam.cy < 1) continue;
        int i = nshown;
        if (nshown < NPC_DRAW) nshown++;
        else if (d >= shown_d[NPC_DRAW - 1]) continue;
        else i = NPC_DRAW - 1;
        while (i > 0 && shown_d[i - 1] > d) { shown[i] = shown[i - 1]; shown_d[i] = shown_d[i - 1]; i--; }
        shown[i] = k; shown_d[i] = d;
    }
    for (int k = 0; k < nshown; k++) occ_add(E.npcs[shown[k]].x, E.npcs[shown[k]].z);
    for (int k = 0; k < (bat || E.in.on ? 0 : ACT_MAX); k++) if (MISE.act[k].on) occ_add(MISE.act[k].x, MISE.act[k].z);
    for (int k = 0; k < (bat || E.in.on ? 0 : POL_AGENTS); k++) if (POL.ag[k].on) occ_add(POL.ag[k].x, POL.ag[k].z);   /* agents de la police */
    int cib[CIBLE_DRAW], ncib = bat || E.in.on ? 0 : cibles_vues(cib, (float)view_r);
    plan_bat = 0;
    for (int k = 0; k < ncib; k++) occ_add(E.w.cibles[cib[k]].x, E.w.cibles[cib[k]].z);
    for (int s = 0; s < (bat || E.in.on ? 0 : PATR_MAX); s++) if (PATR[s].on) occ_add(PATR[s].x[0], PATR[s].z[0]);   /* patrouilles hostiles */

    const camera_t *cam = &E.cam;
    const float fx = cam->sy, fz = cam->cy, R = (float)view_r;
    B->inside = E.in.on && !bat; B->ciel = E.w.ciel;
    B->noir = hud_verrou_on() || accueil_voile() >= 1 || carte_plein();   /* carte du menu : elle couvre l'écran */
    if (B->noir) goto aplatir;   /* montre verrouillée, page opaque de l'écran titre : rien du décor à préparer */
    if (B->inside && !int_pret()) B->inside = 0;   /* intérieur relu s'il avait rendu le bloc de la fenêtre */
    if (B->inside) { interieur_scene(time_s); goto aplatir; }   /* intérieur : sa scène seule */
    /* Fenêtre du décor avancée de 5 cases devant la caméra (même rayon) : on voit plus loin devant, la géométrie apparaît
       dans le brouillard dense ; derrière Sera, ce que la caméra ne voit pas n'est plus parcouru. Les détails (blocs
       réduits, herbes) restent comptés depuis Sera. */
    const float av = bat ? 0 : 5.0f / w->ech, vcx = focx + fx * av, vcz = focz + fz * av;
    const int ci = (int)floorf(vcx), cj = (int)floorf(vcz);
    /* Cases proches : le bas de l'écran voit le sol (au niveau de Sera) à near_d devant la caméra ; une case n'est écartée
       que si son centre est 2,5 cases en deçà (pentes, demi-diagonale, bas de l'écran en course). À 180 % de zoom, near_d tombe vers 1,8 ; à 300 % et 500 %, plus bas encore. */
    float near_d;
    {
        const float h = cam->y - ground_y(focx, focz) - (bat ? 0 : course_alt_joueur()), a = cam->pitch + atanf(CYF / FOCAL);   /* hauteur suivie (pont, remblai) */
        near_d = a >= PI_F / 2 - 0.01f ? 0 : h / tanf(a);
        near_d -= 2.5f;
    }
    if (!vc_stamp) return;
    frame_stamp++; vc_i0 = ci - VIEW_R - 1; vc_j0 = cj - VIEW_R - 1;
    /* Fenêtre de chunks à recentrer (début ou fin de combat) : image sans 3D (le fondu au noir la couvre) */
    const int i0 = ci - view_r < 0 ? 0 : ci - view_r, i1 = hold ? i0 : ci + view_r < n ? ci + view_r : n;
    const int j0 = cj - view_r < 0 ? 0 : cj - view_r, j1 = hold ? j0 : cj + view_r < n ? cj + view_r : n;
    BP(BPROF_DEBUT);
    sol_bas = 1e9f;
    /* Blocs de 2 × 2 cases loin de Sera : un polygone chacun */
    for (int bj = j0 >> 1; bj <= (j1 - 1) >> 1; bj++)
    for (int bi = i0 >> 1; bi <= (i1 - 1) >> 1; bi++) {
        if (!block_lod(bi, bj, focx, focz)) continue;
        float dxw = bi * 2 + 1 - cam->x, dzw = bj * 2 + 1 - cam->z;
        if (dxw * fx + dzw * fz < 1) continue;
        if (dist_gt(bi * 2 + 1 - vcx, bj * 2 + 1 - vcz, R + 1.5f)) continue;
        submit_block(bi, bj, focx, focz);
    }
    BP(BPROF_BLOCS);
    /* Cases à pleine résolution */
    for (int j = j0; j < j1; j++)
    for (int i = i0; i < i1; i++) {
        if (block_lod(i >> 1, j >> 1, focx, focz)) continue;
        float dxw = i + 0.5f - cam->x, dzw = j + 0.5f - cam->z;
        if (dxw * fx + dzw * fz < near_d) continue;
        if (dist_gt(i + 0.5f - vcx, j + 0.5f - vcz, R)) continue;
        const chunk_t *ch = chunk_of_cell(i, j);
        if (!ch) continue;
        const int lx = i % CS, lz = j % CS, c = lz * CS + lx;
        const float *h4 = &ch->hv[lz * CV + lx];
        const float hmin = min_f(min_f(h4[0], h4[1]), min_f(h4[CV], h4[CV + 1]));
        const float wh = ch->water_h[c];
        { const float hs = max_f(hmin, wh); if (hs < sol_bas) sol_bas = hs; }   /* sol, ou surface de l'eau qui le couvre */
        if (ch->hidden[c]) { if (sol_fondu(i + 0.5f, j + 0.5f)) sol_noir(i, j, ch, lx, lz, c); continue; }
        int t = ch->tile[c], water = ch->water[c];
        const int sea = wh == w->wl;   /* sommets d'eau du cache (niveau de la mer) */
        const int clair = water && w->neaux;   /* eau transparente : le fond à son relief, puis la surface */
        /* Sol et eau de la case : rien à soumettre s'ils sont hors du champ (submit_poly les écarterait) ; boîte du relief
           et de la surface de l'eau. Ses décors et ses routes ont leur propre test. */
        float ylo = hmin, yhi = max_f(max_f(h4[0], h4[1]), max_f(h4[CV], h4[CV + 1]));
        if (water || hmin < wh) { ylo = min_f(ylo, wh); yhi = max_f(yhi, wh); }
        int cb = -1;
        if (!boite_hors_champ(i + 0.5f, (ylo + yhi) / 2, j + 0.5f, 0.5f, (yhi - ylo) / 2, 0.5f)) {
            float S = t >= (int)w->g0 ? 32.0f : 16.0f;
            static const int CX4[4] = { 0, 1, 1, 0 }, CZ4[4] = { 0, 0, 1, 1 };
            vv_t q[4];
            for (int k = 0; k < 4; k++) {
                int x = i + CX4[k], z = j + CZ4[k];
                v3 o = clair ? grid_vertex(x, z, 0) : water && !sea ? to_view((float)x, wh, (float)z) : grid_vertex(x, z, water);
                q[k].x = o.x; q[k].y = o.y; q[k].z = o.z; q[k].u = CX4[k] * S; q[k].v = CZ4[k] * S;
                q[k].l = water && !clair ? 12.0f : ch->lv[(lz + CZ4[k]) * CV + lx + CX4[k]];
            }
            cur_cat = 0;
            poly_bias = -SOL_RECUL;
            /* Case tordue (rugosité du sol, fond d'un lac, coin d'une rive) : deux triangles, pliés selon la diagonale la moins
               haute ; chacun son plan de texture, qui raccorde avec les voisines (SOL_TORDU), et plus de trous où les quatre
               coins ne font plus une forme convexe à l'écran. Eau transparente : son fond a le relief. SOL_TORDU de l'émulateur */
            const float tor = !water || clair ? fabsf(h4[0] - h4[1] + h4[CV + 1] - h4[CV]) : 0;
            if (tor > SOL_TORDU) {
                const int tl = clair && t < (int)w->g0 ? T_BED : t;
                vv_t tr[3];
                const int ad = fabsf(h4[0] - h4[CV + 1]) <= fabsf(h4[1] - h4[CV]);   /* diagonale 0-2, sinon 1-3 */
                tr[0] = q[0]; tr[1] = q[1]; tr[2] = q[ad ? 2 : 3];
                cb = submit_poly(tr, 3, tl, -1, 0);
                tr[0] = ad ? q[0] : q[1]; tr[1] = q[2]; tr[2] = q[3];
                const int cb2 = submit_poly(tr, 3, tl, -1, 0);
                if (cb2 >= 0 && (cb < 0 || cb2 < cb)) cb = cb2;
            }
            else cb = submit_poly(q, 4, clair && t < (int)w->g0 ? T_BED : t, -1, 0);   /* fond : la texture de la case (T_BED sans) */
            poly_bias = 0;
            if (clair) { const int wb = eau_surface(i, j, wh, cb, water - 1); if (wb >= 0 && (cb < 0 || wb < cb)) cb = wb; }
            else if (!water && hmin < wh && w->neaux) {   /* rive : la surface de l'eau voisine, qui s'arrête où le sol sort */
                const int wb = eau_surface(i, j, wh, cb, eau_autour(i, j));
                if (wb >= 0 && (cb < 0 || wb < cb)) cb = wb;
            } else if (!water && hmin < wh) {
                for (int k = 0; k < 4; k++) {
                    v3 o = sea ? grid_vertex(i + CX4[k], j + CZ4[k], 1) : to_view((float)(i + CX4[k]), wh, (float)(j + CZ4[k]));
                    q[k].x = o.x; q[k].y = o.y; q[k].z = o.z; q[k].u = CX4[k] * 16.0f; q[k].v = CZ4[k] * 16.0f; q[k].l = 12;
                }
                cur_cat = 1;
                int wb = submit_poly(q, 4, T_WATER, -1, 0);
                if (wb >= 0 && (cb < 0 || wb < cb)) cb = wb;
            }
        }
        cell_plants(ch, c, i, j, cb, focx, focz);
        /* Morceaux de route de la case : même seau, juste après le sol */
        for (int k = 0; k < ch->road_count[c]; k++) {
            cur_cat = 3;
            road_piece(chunk_poly(ch, ch->road_first[c] + k), cb);
        }
    }
    BP(BPROF_CASES);
    /* Garde-corps des ponts (et glissières d'une piste de course), bâtiments et blocs du décor (épave) de chaque chunk chargé ; une place pas encore
       rechargée garde un chunk voisin, réel lui aussi : le rayon de vue l'écarte s'il est trop loin */
    for (int sl = 0; sl < (hold ? 0 : CWIN * CWIN); sl++) {
        const chunk_t *ch = w->slot[sl];
        if (ch->cx < 0) continue;
        occ_poly = NULL;
        for (uint32_t k = 0; k < ch->rail_count; k++) {
            const pak_poly_t *pp = chunk_poly(ch, ch->rail_first + k);
            float cx = (pp->v[0].x + pp->v[1].x) / 2, cz = (pp->v[0].z + pp->v[1].z) / 2;
            if (dist_gt(cx - vcx, cz - vcz, R)) continue;
            if (CR.on && (pp->tile == T_METAL || pp->tile == T_LEVRE)) {   /* garde-corps ou bord relevé d'un tablier caché (pont_cache) : caché avec lui ; pas le portique du départ */
                float y = 0;
                for (int i = 0; i < pp->nv; i++) y += pp->v[i].y;
                if (pp->nv && pont_cache(cx, y / pp->nv, cz)) continue;
            }
            cur_cat = 3;
            submit_pak_poly(pp, pp->tile, -1, 0);   /* métal ; portique du départ d'une piste : sa texture */
        }
        for (uint32_t k = 0; k < ch->nbld; k++) {
            const pak_building_t *b = &ch->bld[k];
            if (dist_gt(b->cx - vcx, b->cz - vcz, R + b->r)) continue;
            const int p0 = B->nprims;
            if (MISE.cam_on && dist_lt(b->cx - MISE.cam[0], b->cz - MISE.cam[1], b->r + PLAN_BAT)) plan_bat = 1;
            /* Bâtiment entier derrière la caméra ou hors des côtés de l'image (08/11/2026 : à Lucerne, la préparation passait 10 ms par
               image sur la montre à parcourir les polygones de tous les bâtiments du rayon de vue) : ni ses polygones, ni son fondu. Hauteur
               prise large (BAT_CHAMP_H de part et d'autre de son premier polygone, qui peut être un toit) : ce test ne doit jamais écarter un bâtiment visible */
#ifdef ENG_COMPTE
            { extern unsigned long eng_bat_vus, eng_bat_polys; eng_bat_vus++; eng_bat_polys += b->count; }
#endif
            if (b->count) {
                const float rb = b->r + BAT_DEBORD;
                if ((b->cx - E.cam.x) * E.cam.sy + (b->cz - E.cam.z) * E.cam.cy < -rb) { BAT_ECARTE(); continue; }
                if (k < BAT_CACHE) {
                    bat_y_lire(sl, ch);
                    const float lo = bat_y[sl].ylo[k] / BAT_Y_PAS, hi = bat_y[sl].yhi[k] / BAT_Y_PAS;
                    if (boite_hors_champ(b->cx, (lo + hi) / 2, b->cz, rb, (hi - lo) / 2 + 0.1f, rb)) { BAT_ECARTE(); continue; }
                } else if (boite_hors_champ(b->cx, chunk_poly(ch, b->first)->v[0].y, b->cz, rb, BAT_CHAMP_H, rb)) { BAT_ECARTE(); continue; }
            }
            sujet_ici = sujet_on && !dist_lt(b->cx - occ_sujet.x, b->cz - occ_sujet.z, b->r * SUJET_DANS);
            bld_hit = 0; fade_mark = 0;
            /* Le bâtiment qui cachait l'héroïne à l'image d'avant (sa place de fondu, à 1) coupé en entier à sa fondation (île, puis Echoes) */
            const int ile_coupe = 1;
            float ycoupe = INFINITY, ypied = 0;
            uint32_t cv[3] = { 0, 0, 0 };   /* couverture des personnages suivis par ses faces (couv_face) */
            const int_porte_t *pts[4]; int npt = 0;   /* ses portes (est_porte) */
            {
                for (int f = 0; f < FADE_N; f++) {
                    if (!fades[f].used || fades[f].a <= 0 || fabsf(fades[f].cx - b->cx) >= 0.01f || fabsf(fades[f].cz - b->cz) >= 0.01f) continue;
                    float lo = 1e9f;
                    if (k < BAT_CACHE) { bat_y_lire(sl, ch); lo = bat_y[sl].ylo[k] / BAT_Y_PAS; }
                    else {
                        for (uint32_t m = 0; m < b->count; m++) { const pak_poly_t *pp = chunk_poly(ch, b->first + m); for (int i = 0; i < pp->nv && i < 8; i++) lo = fminf(lo, pp->v[i].y); }
                        lo = floorf(lo * BAT_Y_PAS) / BAT_Y_PAS;   /* comme bat_y */
                    }
                    ycoupe = lo + COUPE_FONDATION; ypied = lo;
                    for (int d = 0; d < E.w.nportes && npt < 4; d++) { const int_porte_t *q = &E.w.portes[d]; if (dist_lt(q->x - b->cx, q->z - b->cz, b->r + 1.5f)) pts[npt++] = q; }
                    break;
                }
            }
            int prev_b = -1;   /* seau du polygone précédent : une affiche (POLY_COLLE) s'y range, après son mur */
            for (uint32_t m = 0; m < b->count; m++) {
                const pak_poly_t *pp = chunk_poly(ch, b->first + m);
                /* Ce qui s'accroche au volume principal (vitrines, portes, stores, auvents, enseignes, corniches, toits)
                   un peu plus près, dans l'ordre de construction : presque à la même profondeur que le mur, ils ne passent
                   plus derrière lui quand la caméra tourne ; le reste (volume principal, annexes, tas de conteneurs) par
                   sa profondeur (submitPoly de l'émulateur) */
                const int rk = POLY_RANK(pp);
                poly_bias = rk == POLY_RANK_MAX ? 0.4f : rk && rk != POLY_COLLE ? 0.6f + 0.004f * (rk - 1) : 0;   /* objet posé (conteneur), accessoire : rang + 1 ; le reste par sa profondeur */
                cur_cat = 4; occ_poly = pp;
                const int dos = culled(pp);
                if (rk == POLY_COLLE && prev_b < 0) continue;
                if (dos && !(ile_coupe && ycoupe < INFINITY)) { prev_b = -1; continue; }
                if (ile_coupe) {   /* ce qu'elle couvre de l'héroïne (polygone entier) */
                    if (!dos && (occ_sera >= 0 || sujet_ici || parle_on)) couv_face(pp, sujet_ici, cv);
                    if (ycoupe < INFINITY) {
                        if (npt && est_porte(pp, pts, npt, ypied)) {   /* une porte : entière (sa face de dos, cachée par les autres faces, non) */
                            if (dos) { prev_b = -1; continue; }
                        } else { prev_b = submit_pak_poly_coupe(pp, pp->tile, rk == POLY_COLLE ? prev_b : -1, pp->flags & POLY_ALPHA, ycoupe, dos); continue; }
                    }
                }
                prev_b = submit_pak_poly(pp, pp->tile, rk == POLY_COLLE ? prev_b : -1, pp->flags & POLY_ALPHA);
            }
            poly_bias = 0;
            fade_mark = 0;
            bld_hit = couv_cache(cv, ycoupe < INFINITY);   /* caché à moitié et plus (gardé coupé à COUV_GARDE) */
            /* Place de fondu : celle du bâtiment s'il en a une, une nouvelle s'il cache Sera */
            int slot = -1, freeslot = -1;
            for (int f = 0; f < FADE_N; f++) {
                if (!fades[f].used) { if (freeslot < 0) freeslot = f; continue; }
                if (fabsf(fades[f].cx - b->cx) < 0.01f && fabsf(fades[f].cz - b->cz) < 0.01f) { slot = f; break; }
            }
            if (slot < 0 && bld_hit && freeslot >= 0) { slot = freeslot; fades[slot] = (fade_t){ b->cx, b->cz, 0, 1, 0, b->r }; }
            if (slot >= 0 && bld_hit) fades[slot].hit = 1;
            for (int q = p0; q < B->nprims; q++) if (B->prims[q].fade == 255) B->prims[q].fade = (uint8_t)(slot >= 0 ? slot + 1 : 0);
        }
        for (uint32_t k = 0; k < ch->box_count; k++) {
            const pak_poly_t *pp = chunk_poly(ch, ch->box_first + k);
            if (dist_gt(pp->v[0].x - vcx, pp->v[0].z - vcz, R + 4)) continue;
            cur_cat = 4; occ_poly = pp; if (!culled(pp)) submit_pak_poly(pp, pp->tile, -1, 0);
        }
    }
    occ_poly = NULL;
    /* Coulées des avalanches (section COUL) une fois leur drapeau levé : leurs faces par leur profondeur, comme celles d'un
       bâtiment (le tampon de profondeur les règle avec le sol qu'elles recouvrent) */
    for (int k = 0; k < (hold ? 0 : w->ncoul); k++) {
        const coulee_t *cl = &w->coul[k];
        if (!coulee_visible(k) || dist_gt(cl->x - vcx, cl->z - vcz, R + 1.5f * (cl->rx > cl->rz ? cl->rx : cl->rz))) continue;
        const uint8_t *m = cl->poly;
        for (int i = 0; i < cl->npoly; i++) {
            const pak_poly_t *pp = (const pak_poly_t *)m;
            m += 16 + pp->nv * 24;
            cur_cat = 4;
            if (!culled(pp)) submit_pak_poly(pp, pp->tile, -1, 0);
        }
    }
    BP(BPROF_CHUNKS);
    /* (seulement l'image tout à fait relevée : à la première image d'un dialogue, l'image n'a pas encore glissé et le bateau d'une traversée,
       qui sera au-dessus des répliques, était jugé trop bas ; le plan passait pour vide et la caméra quittait le bateau, 11/11/2026) */
    if (!bat && !E.in.on && MISE.cam_on && !MISE.cam_retour && MISE.cam_t >= MISE.cam_d && hud_cover_y() > 0 && dlg_glisse >= 0.99f && !eng_dlg_hors && !plan_bat && !dans_bande(E.sera.x, E.sera.z)) {
        int vu = 0;
        for (int k = 0; k < nshown && !vu; k++) vu = dans_bande(E.npcs[shown[k]].x, E.npcs[shown[k]].z);
        for (int k = 0; k < ACT_MAX && !vu; k++) vu = MISE.act[k].on && !MISE.act[k].chasse && dans_bande(MISE.act[k].x, MISE.act[k].z);
        for (int k = 0; k < ncib && !vu; k++) vu = dans_bande(E.w.cibles[cib[k]].x, E.w.cibles[cib[k]].z);
        if (!vu) { eng_dlg_hors = 1; eng_dlg_x = E.sera.x; eng_dlg_z = E.sera.z; }
    }
    /* Fondus : chaque place va vers 1 si son bâtiment cache Sera, vers 0 sinon (libérée à 0) ; puis le niveau de chaque
       face (pixels sautés sur 16) remplace sa place */
    {
        float dt = fade_time < 0 ? 0 : time_s - fade_time;
        if (dt < 0 || dt > 0.1f) dt = 0.1f;
        fade_time = time_s;
        uint8_t lvl[FADE_N + 1] = { 0 };
        B->sera_faded = 0;
        for (int f = 0; f < FADE_N; f++) {
            fade_t *fd = &fades[f];
            if (!fd->used) continue;
            if (fd->r > 0) fd->a = fd->hit ? 1.0f : 0.0f;   /* bâtiment coupé ou entier d'un coup (île, puis Echoes) */
            else fd->a += (fd->hit ? dt : -dt) / FADE_S;
            if (fd->a > 1) fd->a = 1;
            if (fd->a <= 0 && !fd->hit) { fd->used = 0; continue; }
            if (fd->a < 0) fd->a = 0;
            if (fd->hit && fd->a > 0) B->sera_faded = 1;
            fd->hit = 0;
            lvl[f + 1] = (uint8_t)lroundf(fd->a * FADE_MAX);
        }
        for (int q = 0; q < B->nprims; q++) { prim_t *pq = &B->prims[q]; if (pq->fade >= 200) pq->fade -= 200; else if (pq->fade) pq->fade = lvl[pq->fade]; }
    }
    /* Pluie : gouttes qui tombent (position tirée de leur numéro, avancée avec le temps ; le vent penche les traits, la
       rotation de la caméra les décale), éclaboussures renouvelées toutes les 0,12 s dans les deux tiers bas du cercle */
    B->rain = E.rain; B->nrain = 0;
    if (E.rain > 0.01f && neige_ici()) {
        /* Neige (01/11/2026) : flocons qui descendent lentement (22 à 53 pixels par seconde) en dérivant de côté, un sur
           quatre de 2 × 2 pixels (len -2, sinon -1), sans éclaboussure (flocons de drawRain dans l'émulateur) */
        const int nf = (int)(E.rain * RAIN_MAX * 0.7f);
        const float shift = E.yaw * 90.0f;
        for (int k = 0; k < nf && B->nrain < RAIN_MAX; k++) {
            uint32_t hsh = (uint32_t)k * 2654435761u;
            const float speed = 22 + (hsh >> 24) * 0.12f;
            const float y = fmodf((hsh >> 12 & 1023) + time_s * speed, RH + 8.0f) - 4;
            float x = fmodf((hsh & 4095) * (RW / 4096.0f) + shift + 5 * sinf(time_s * 1.3f + k * 0.7f), (float)RW);
            if (x < 0) x += RW;
            B->drops[B->nrain].x = (int16_t)x; B->drops[B->nrain].y = (int16_t)y;
            B->drops[B->nrain].len = (int8_t)(((hsh >> 20) & 3) == 0 ? -2 : -1); B->drops[B->nrain].dx = 0; B->nrain++;
        }
    }
    else if (E.rain > 0.01f) {
        const int nd = (int)(E.rain * RAIN_MAX * 0.75f), ns = (int)(E.rain * RAIN_MAX * 0.25f);
        const float shift = E.yaw * 90.0f;
        for (int k = 0; k < nd && B->nrain < RAIN_MAX; k++) {
            uint32_t hsh = (uint32_t)k * 2654435761u;
            const float speed = 260 + (hsh >> 24) * 0.6f, len = 5 + ((hsh >> 8) & 3);
            const float y = fmodf((hsh >> 12 & 1023) + time_s * speed, RH + 16.0f) - 8;
            float x = fmodf((hsh & 4095) * (RW / 4096.0f) + shift - y * 0.18f, (float)RW);
            if (x < 0) x += RW;
            B->drops[B->nrain].x = (int16_t)x; B->drops[B->nrain].y = (int16_t)y;
            B->drops[B->nrain].len = (int8_t)len; B->drops[B->nrain].dx = -2; B->nrain++;
        }
        const uint32_t epoch = (uint32_t)(time_s / 0.12f);
        for (int k = 0; k < ns && B->nrain < RAIN_MAX; k++) {
            uint32_t hsh = ((uint32_t)k * 2246822519u) ^ (epoch * 3266489917u);
            hsh ^= hsh >> 15; hsh *= 2654435761u; hsh ^= hsh >> 13;
            B->drops[B->nrain].x = (int16_t)(hsh % RW); B->drops[B->nrain].y = (int16_t)(RH / 3 + (hsh >> 16) % (RH * 2 / 3));
            B->drops[B->nrain].len = 0; B->drops[B->nrain].dx = 0; B->nrain++;
        }
    }
    /* Personnages */
    const int ps = member_sheet(0);   /* Sera dans sa tenue */
    if (bat && !hold) {
        /* Équipe de dos face à l'ennemi (Sera dans sa planche propre si elle en a une, les autres dans la planche commune
           avec leur table de couleurs ; KO en gris), en quinconce, avancée de son élan ; ennemi de la bibliothèque de
           face, BAT_ETAILLE fois sa taille, blanc quand il est touché ; point visé au-dessus de lui */
        static const int REMAP[4] = { 0, 2, 3, 4 };
        /* Équipe en bas à droite, tournée vers l'ennemi (trois quarts dos) ; l'élan la rapproche de lui. Ceux qui combattent
           (in_team) prennent les places 0 à EQUIPE_MAX - 1 dans l'ordre de PARTY (team_place), la réserve n'est pas là */
        float tx = 0, tz = 0;   /* centre de l'équipe : celui des EQUIPE_MAX places */
        for (int k = 0; k < EQUIPE_MAX; k++) { float bx, bz; battle_member(k, &bx, &bz); tx += bx / EQUIPE_MAX; tz += bz / EQUIPE_MAX; }
        /* Ordre d'affichage de l'équipe (11/11/2026, retour de jeu : « le personnage le plus à droite passe au-dessus des autres en
           lançant l'attaque ») : les membres sont presque à la même profondeur (moins de 1/64 d'écart), le test de profondeur ne
           tranche pas ; le seau, qui avance chaque sprite de sa largeur, puis l'ordre d'arrivée décidaient, et le dernier de PARTY
           (le plus à droite) passait devant ses voisins dès qu'il bougeait. Désormais tous dans le même seau (celui de la moitié de
           la profondeur du plus proche, comme char_avant au plus près), du plus loin au plus proche : à égalité, le plus proche
           l'emporte (dessiné en premier) ; l'attaquant au contact de l'ennemi garde le sien (char_devant). Même ordre dans
           l'émulateur, qui peint du fond vers l'avant */
        int ordre[NMEMB], no = 0;
        float prof[NMEMB], pmin = 1e9f;
        for (int i = 0; i < NMEMB; i++) {
            if (!in_team(i)) continue;
            float bx, bz, vx, vz; battle_member(team_place(i), &bx, &bz); battle_vise(i, &vx, &vz);
            const float dx = vx - bx, dz = vz - bz, dl = hypotf(dx, dz), pas = BT.off[i] + BT.avance[i] * PAS_ACTIF;
            char_view_t cv;
            prof[i] = char_view(bx + dx / dl * pas / w->ech, bz + dz / dl * pas / w->ech, 0, &cv) ? cv.z : 0;
            if (prof[i] > 0 && prof[i] < pmin) pmin = prof[i];
            int k = no++;
            while (k > 0 && prof[ordre[k - 1]] < prof[i]) { ordre[k] = ordre[k - 1]; k--; }
            ordre[k] = i;
        }
        const int seau_equipe = pmin < 1e9f ? bucket_of(pmin * 0.5f) : -1;
        for (int oi = 0; oi < no; oi++) {
            const int i = ordre[oi];
            char_bucket = seau_equipe;
            float bx, bz; battle_member(team_place(i), &bx, &bz);
            float vx, vz; battle_vise(i, &vx, &vz);   /* tourné vers l'ennemi visé (groupe), celui de son action */
            const int retourne = BT.phase == BP_ANIM && BT.an.who == i && BT.an.melee && E.clock - BT.an.t0 >= ELAN_FRAPPE && BT.off[i] > 0;   /* élan : le retour, en courant vers sa place */
            const float dx = vx - bx, dz = vz - bz, dl = hypotf(dx, dz), dir = atan2f(dx, dz) + (retourne ? PI_F : 0);
            const float pas = BT.off[i] + BT.avance[i] * PAS_ACTIF;   /* élan, et le pas du membre qui choisit */
            bx += dx / dl * pas / w->ech; bz += dz / dl * pas / w->ech;
            const int fr = BT.off[i] != 0 ? BT.frame[i] : 0;
            /* Planche propre : celle du joueur pour Sera, sinon celle du personnage de la bibliothèque de ce nom */
            const int pp = i ? w->party_perso[i] : -1, own = i ? member_sheet(i) : ps;   /* dans sa tenue */
            /* Élan au corps à corps : l'attaquant devant l'ennemi qu'il frappe. Une machine ancrée au milieu de son emprise
               (Glaneur, char) a sa profondeur avancée (add_character) : arrêté à BAT_CONTACT de son centre, l'attaquant
               passait derrière elle (02/11/2026). Seulement près de lui (ELAN_DEVANT), et à la clé de l'ennemi (ses pieds,
               sauf une machine) : avant le 11/11/2026, celle du haut de son grand sprite, pendant toute la course, et le
               dernier de l'équipe à droite passait devant ceux qu'il croisait (retour de jeu) */
            if (BT.phase == BP_ANIM && BT.an.who == i && BT.an.melee && BT.off[i] > 0) {
                const int k = battle_vise_en(i);
                if (k < BT.nen && BT.en[k].perso >= 0) {
                    const perso_t *pe = &w->persos[BT.en[k].perso];
                    float ex, ez; battle_enemy_pos(k, &ex, &ez);
                    if (hypotf(ex - bx, ez - bz) * w->ech < BAT_CONTACT + ELAN_DEVANT) {
                        int f2; const int esh = battle_anim(pe->sheet, BT_ENEMY + k, 0, &f2);
                        float cle = 0;   /* hors de la vue : 0, rien d'imposé */
                        char_devant = char_avant(ex, ez, esh, pe->taille * BAT_ETAILLE, sera_row(atan2f(tx - ex, tz - ez)), &cle) * 0.96f;
                        char_devant_z = cle * 0.96f;
                        char_bucket = -1;   /* au contact : le seau de l'ennemi, pas celui de l'équipe */
                    }
                }
            }
            const uint32_t hx6 = hud_theme_c2();
            char_anneau = (BT.phase == BP_ALLIE ? BT.allie == i : BT.actor == i && bt_choix()) ? 24 + 3 * sinf(E.clock * 6) : 0;
            char_anneau_c = BT.phase == BP_ALLIE ? pack565(0x7D, 0xFF, 0x9A) : pack565(hx6 >> 16 & 255, hx6 >> 8 & 255, hx6 & 255);
            if (own >= 0 && (PARTY[i].hp > 0 || has_anim(own, ANIM_KO))) {
                int f2; const int sh = battle_anim(own, i, fr, &f2);
                char_tissu = member_tissu(i); char_teinte = BT.transfo[i] ? i + 1 : 0;
                /* île : héroïnes à 75 % (une taille 0 vaut celle de la planche : prise avant la réduction) */
                const float kt = w->ile[0] ? ILE_BAT_TAILLE : 1.0f;
                #define TAILLE_K(t) ((kt == 1.0f ? (t) : ((t) > 0 ? (t) : w->sheets[own].taille)) * kt)
                if (i) { const perso_vue_t pv = perso_vue(pp, 0); add_character(bx, bz, f2, sera_row(dir), pv.remap, sh, WHO_MEMBRE + i, TAILLE_K(pv.taille), 0, 0, pv.ombre); }
                else add_character(bx, bz, f2, sera_row(dir), 0, sh, WHO_MEMBRE, TAILLE_K(joueur_taille()), 0, 0, OMBRE_SERA);
                #undef TAILLE_K
                char_tissu = char_teinte = 0;
            }
            else add_character(bx, bz, fr, sprite_row(dir), PARTY[i].hp > 0 ? REMAP[i & 3] : 5, -1, WHO_MEMBRE + i, 0, 0, 0, -1);
            char_devant = char_devant_z = 0; char_anneau = 0; char_bucket = -1;
        }
        /* Ennemis en haut à gauche (groupe : en arc autour de la place du chef, battle_enemy_pos), tournés vers l'équipe
           (trois quarts face) ; leur position à l'écran : point visé (à mi-hauteur), nom, barre ; un ennemi abattu tombe
           puis disparaît (battle_enemy_vu) */
        for (int k = 0; k < BT.nen; k++) {
            const bt_en_t *e = &BT.en[k];
            if (e->perso < 0 || !battle_enemy_vu(k)) continue;
            const perso_t *pe = &w->persos[e->perso];
            float ex, ez; battle_enemy_pos(k, &ex, &ez);
            int f2; const int sh = battle_anim(pe->sheet, BT_ENEMY + k, 0, &f2);
            char_anneau = BT.visible && BT.phase != BP_INTRO && e->hp > 0 && (BT.phase == BP_ALLIE ? BT.allie == BT_ENEMY + k : BT.nen > 1 && k == BT.cible && bt_choix()) ? 26 + 3 * sinf(E.clock * 6) : 0;
            char_anneau_c = pack565(0xFF, 0x3B, 0x4E);
            add_character(ex, ez, f2, sera_row(atan2f(tx - ex, tz - ez)), e->flash > 0 ? REMAP_WHITE : perso_remap(e->perso, e->var), sh, WHO_ENNEMI + k, pe->taille * BAT_ETAILLE, 0, 0, e->perso);
            char_anneau = 0;
        }
    }
    if (!bat && E.ride < 0 && !accueil_on() && !ile_offrande_cache_sera()) {   /* ouverture : Sera cachée, la caméra survole un lieu ; île : une animation « à deux » d'une succube */
        if (ps >= 0) {   /* attente sans animation d'attente : première image de la marche */
            const int st = E.sera.state, sh = sera_anim(ps);
            const int fr = st == 0 && !has_anim(ps, ANIM_IDLE) ? 0 : E.sera.frame % E.w.sheets[sh].frames;
            /* À la nage : le sprite entier, NAGE_SINK de sa hauteur sous la surface (debout sur le fond là où c'est peu
               profond) : la surface tramée, devant elle, voile le corps immergé selon la profondeur ; son ombre sur le fond */
            const float H = 2.0f / E.w.ech * E.w.sprite_scale * joueur_taille() * E.w.sheets[ps].norm, prof = E.nage ? swim_alt() : 0;
            char_tissu = member_tissu(0); char_teinte = eveil_hors_combat();
            add_character(E.sera.x, E.sera.z, fr, sera_row(E.sera.dir), 0, sh, 0, joueur_taille(), E.w.ile[0] ? ile_nage(prof, H) : fmaxf(0, prof - NAGE_SINK * H), 0, OMBRE_SERA);
            char_tissu = char_teinte = 0;
        } else add_character(E.sera.x, E.sera.z, E.sera.frame % 4, sprite_row(E.sera.dir), 0, -1, 0, 0, 0, 0, -1);
    }
    for (int k = 0; k < nshown; k++) {
        const npc_t *nc = &E.npcs[shown[k]];
        const perso_t *pe = &E.w.persos[nc->perso];
        const int mine = shown[k] == E.ride;   /* véhicule conduit : bulle de sortie au-dessus, en altitude s'il vole */
        const int veh = (pe->flags & PERS_VEHICULE) != 0;
        /* En course : hauteur de la piste ; vaisseau conduit, ou véhicule qui coule : E.ride_alt ; sur l'eau, un véhicule
           qui plane glisse au-dessus de la surface */
        float alt = (mine ? course_alt_joueur() : 0) + ((VEH_TYPE(pe->flags) == VEH_VAISSEAU || E.ride_coule > 0) && veh && mine && !CR.on ? E.ride_alt
            : veh ? veh_hover(nc->perso, (float)k) + (veh_surface(nc->perso) ? eau_prof(nc->x, nc->z) : 0) : 0);
        for (int q = 0; q < E.w.ile_nsocles; q++) if (E.w.ile_socle_perso[q] == nc->perso) alt = E.w.ile_socle[q];   /* statue sur son piédestal (section ILE) */
        const perso_vue_t pv = perso_vue(nc->perso, nc->var);   /* (la recrue de l'héroïne choisie : en Sera) */
        /* Starter des circuits : son drapeau pendant le compte à rebours et l'arrivée */
        const int drapeau = nc->depart && CR.on && (CR.phase == CP_GRILLE || CR.phase == CP_ARRIVEE) && has_anim(pv.sheet, ANIM_DEPART);
        const int sh = anim_sheet(pv.sheet, drapeau ? ANIM_DEPART : nc->walking ? ANIM_WALK : ANIM_IDLE);
        const int fr = drapeau ? (int)(CR.clock * DEPART_IPS) % E.w.sheets[sh].frames : !nc->walking && !has_anim(pv.sheet, ANIM_IDLE) ? 0 : nc->frame % E.w.sheets[sh].frames;
        char_tissu = pv.tissu;
        /* Véhicule qui coule, ou noyé au fond d'un lac : dans le seau de la surface de sa case, soumis après elle (l'eau,
           tramée selon la profondeur, le voile ; le fond, derrière, ne le recouvre pas) */
        if (veh && ((mine && E.ride_coule > 0) || (!mine && veh_noye(nc->perso, nc->x, nc->z)))) {
            const float cx = floorf(nc->x) + 0.5f, cz = floorf(nc->z) + 0.5f;
            const int bs = bucket_of(to_view(cx, ground_y(cx, cz), cz).z + SOL_RECUL);
            char_bucket = bs > 0 ? bs - 1 : 0;
        }
        add_character(nc->x, nc->z, fr, sera_row(nc->dir), pv.remap, sh, shown[k] == E.talk || mine ? 1 : 2, pv.taille, alt, veh && veh_rolls(nc->perso), pv.ombre);
        char_bucket = -1;
        char_tissu = 0;
    }
    /* Acteurs du scénario (instruction « acteur ») : comme les exemplaires, marche ou course, sinon attente */
    for (int k = 0; k < (bat ? 0 : ACT_MAX); k++) {
        const acteur_t *a = &MISE.act[k];
        if (!a->on || (a->x - E.cam.x) * E.cam.sy + (a->z - E.cam.z) * E.cam.cy < 1) continue;
        const perso_vue_t pv = perso_vue(a->perso, a->var);   /* (la recrue de l'héroïne choisie : en Sera) */
        const int sh = anim_sheet(pv.sheet, a->walking ? (a->court && has_anim(pv.sheet, ANIM_RUN) ? ANIM_RUN : ANIM_WALK) : ANIM_IDLE);
        const int fr = !a->walking && !has_anim(pv.sheet, ANIM_IDLE) ? 0 : a->frame % E.w.sheets[sh].frames;
        char_tissu = pv.tissu;
        /* un acteur véhicule flotte comme un exemplaire (île : barque du passeur, pirogue de Maia) */
        const int vh = (E.w.persos[a->perso].flags & PERS_VEHICULE) != 0;
        add_character(a->x, a->z, fr, sera_row(a->dir), pv.remap, sh, 2, pv.taille, vh ? veh_hover(a->perso, (float)k) + (veh_surface(a->perso) ? eau_prof(a->x, a->z) : 0) : 0, 0, pv.ombre);
        char_tissu = 0;
    }
    /* Île du jeu des héroïnes (ile.c) : la compagne de l'héroïne sur la trace de ses pas (une seule visible hors combat :
       ILE_VISIBLES ; l'autre est au combat) ; à la nage, enfoncée comme elle */
    for (int k = 0; k < (bat || !ile_compagnes_vues() ? 0 : ile_ncomp < ILE_VISIBLES ? ile_ncomp : ILE_VISIBLES); k++) {
        const ile_comp_t *c = &ILE_COMP[k];
        if ((c->x - E.cam.x) * E.cam.sy + (c->z - E.cam.z) * E.cam.cy < 1) continue;
        const perso_vue_t pv = perso_vue(c->perso, 0);
        if (pv.sheet < 0) continue;
        const int sh = anim_sheet(pv.sheet, c->st == 2 && has_anim(pv.sheet, ANIM_RUN) ? ANIM_RUN : c->st ? ANIM_WALK : ANIM_IDLE);
        const int fr = !c->st && !has_anim(pv.sheet, ANIM_IDLE) ? 0 : c->frame % E.w.sheets[sh].frames;
        const float H = 2.0f / E.w.ech * E.w.sprite_scale * pv.taille * E.w.sheets[pv.sheet].norm, prof = eau_prof(c->x, c->z);
        char_tissu = pv.tissu;
        add_character(c->x, c->z, fr, sera_row(c->dir), pv.remap, sh, 2, pv.taille, E.w.ile[0] ? (prof > ILE_GUE ? ile_nage(prof, H) : 0) : fmaxf(0, prof - NAGE_SINK * H), 0, pv.ombre);
        char_tissu = 0;
    }
    /* Agents de la police à pied (police.c) : course, marche ou attente, dans le rayon du décor, devant la caméra (comme
       policeVus de l'émulateur) */
    for (int k = 0; k < (bat ? 0 : POL_AGENTS); k++) {
        const agent_t *a = &POL.ag[k];
        if (!a->on || hypotf(a->x - E.sera.x, a->z - E.sera.z) > (float)view_r || (a->x - E.cam.x) * E.cam.sy + (a->z - E.cam.z) * E.cam.cy < 1) continue;
        const perso_t *pe = &E.w.persos[a->perso];
        const int sh = anim_sheet(pe->sheet, a->allure == 2 && has_anim(pe->sheet, ANIM_RUN) ? ANIM_RUN : a->allure ? ANIM_WALK : ANIM_IDLE);
        const int fr = !a->allure && !has_anim(pe->sheet, ANIM_IDLE) ? 0 : a->frame % E.w.sheets[sh].frames;
        char_tissu = pe->vremap;
        add_character(a->x, a->z, fr, sera_row(a->dir), perso_remap(a->perso, a->var), sh, 2, pe->taille, 0, 0, a->perso);
        char_tissu = 0;
    }
    /* Cibles de chasse (CIBL) : attente, tournées vers Sera ; sans bulle ni nom */
    for (int k = 0; k < ncib; k++) {
        const cible_t *c = &E.w.cibles[cib[k]];
        const perso_t *pe = &E.w.persos[c->perso];
        const int sh = anim_sheet(pe->sheet, ANIM_IDLE);
        const int fr = !has_anim(pe->sheet, ANIM_IDLE) ? 0 : (int)(time_s / CIBLE_IMAGE) % E.w.sheets[sh].frames;
        char_tissu = pe->vremap;
        add_character(c->x, c->z, fr, sera_row(atan2f(E.sera.x - c->x, E.sera.z - c->z)), perso_remap(c->perso, 0), sh, 2, pe->taille, 0, 0, c->perso);
        char_tissu = 0;
    }
    /* Patrouilles hostiles (battle.c) : le chef et ses suiveurs dans leurs couleurs, marche (course en chasse) ou attente,
       dans le rayon du décor, devant la caméra (patrouillesVues de l'émulateur) */
    for (int s = 0; s < (bat || E.in.on ? 0 : PATR_MAX); s++) {
        const patr_t *pt = &PATR[s];
        if (!pt->on || hypotf(pt->x[0] - E.sera.x, pt->z[0] - E.sera.z) > (float)view_r) continue;
        for (int i = 0; i < pt->n; i++) {
            if ((pt->x[i] - E.cam.x) * E.cam.sy + (pt->z[i] - E.cam.z) * E.cam.cy < 1) continue;
            const int pp = E.w.enemies[pt->ek[i]].perso;
            const perso_t *pe = &E.w.persos[pp];
            const int va = pt->marche || pt->etat == 1;
            const int sh = anim_sheet(pe->sheet, va ? (pt->etat == 1 && has_anim(pe->sheet, ANIM_RUN) ? ANIM_RUN : ANIM_WALK) : ANIM_IDLE);
            const int fr = !va && !has_anim(pe->sheet, ANIM_IDLE) ? 0 : (va ? pt->frame : (int)(time_s / CIBLE_IMAGE)) % E.w.sheets[sh].frames;
            char_tissu = pe->vremap;
            add_character(pt->x[i], pt->z[i], fr, sera_row(pt->dir[i]), perso_remap(pp, pt->var[i]), sh, 2, pe->taille, 0, 0, pp);
            char_tissu = 0;
        }
    }
    {   /* île : la succube de la séquence « s'offrir à elle » (ile.c), dehors : son animation d'offrande */
        float ox, oz, od; int op, osh, ofr;
        for (int oi = 0; oi < 2; oi++) { const int oa = !bat && !E.in.on ? ile_offrande_acteur(oi, &ox, &oz, &od, &op, &osh, &ofr) : 0; if (oa <= 0) continue;   /* (1 : sa partenaire, paire) */
            const perso_vue_t ov = perso_vue(op, 0);
            char_tissu = oa == 2 ? 0 : ov.tissu;
            add_character(ox, oz, ofr, sera_row(od), oa == 2 ? 0 : ov.remap, osh, 2, ov.taille, 0, 0, ov.ombre);   /* (2 : la scène, ses couleurs telles quelles) */
            char_tissu = 0;
        }
    }
    /* Véhicules du trafic dans le rayon du décor, devant la caméra */
    for (int k = 0; k < (bat ? 0 : TRAFFIC_MAX); k++) {
        const traffic_t *t = &E.traffic[k];
        if (!t->on || hypotf(t->x - E.sera.x, t->z - E.sera.z) > (float)view_r || (t->x - E.cam.x) * E.cam.sy + (t->z - E.cam.z) * E.cam.cy < 1) continue;
        const perso_t *pe = &E.w.persos[t->perso];
        const int sh = anim_sheet(pe->sheet, t->walking ? ANIM_WALK : ANIM_IDLE);
        const int fr = !t->walking && !has_anim(pe->sheet, ANIM_IDLE) ? 0 : t->frame % E.w.sheets[sh].frames;
        add_character(t->x, t->z, fr, sera_row(t->dir), perso_remap(t->perso, t->var), sh, E.talk == TALK_TRAF(k) ? 1 : 2, pe->taille, veh_hover(t->perso, k * 1.7f), veh_rolls(t->perso), t->perso);
    }

    /* Adversaires de la course (le joueur est son véhicule, parmi les exemplaires) ; les éliminés sont sortis */
    for (int k = 0; k < (bat || !CR.on ? 0 : CR.n); k++) {
        const racer_t *r = &CR.r[k];
        if (r->joueur || r->elim || hypotf(r->x - E.sera.x, r->z - E.sera.z) > (float)view_r || (r->x - E.cam.x) * E.cam.sy + (r->z - E.cam.z) * E.cam.cy < 1) continue;
        const perso_t *pe = &E.w.persos[r->perso];
        const int sh = anim_sheet(pe->sheet, r->walking ? ANIM_WALK : ANIM_IDLE);
        const int fr = !r->walking && !has_anim(pe->sheet, ANIM_IDLE) ? 0 : r->frame % E.w.sheets[sh].frames;
        add_character(r->x, r->z, fr, sera_row(r->vis), perso_remap(r->perso, r->var), sh, 2, pe->taille, r->y - ground_y(r->x, r->z) + veh_hover(r->perso, k * 1.3f), veh_rolls(r->perso), r->perso);
    }
    /* Fantôme du contre-la-montre : une image sur deux pixels, teintée (TEINTE_FANTOME), sans ombre */
    {
        const fantome_t *g = bat ? NULL : course_fantome();
        if (g && hypotf(g->x - E.sera.x, g->z - E.sera.z) <= (float)view_r && (g->x - E.cam.x) * E.cam.sy + (g->z - E.cam.z) * E.cam.cy >= 1) {
            const perso_t *pe = &E.w.persos[g->perso];
            const int sh = anim_sheet(pe->sheet, g->walking ? ANIM_WALK : ANIM_IDLE);
            const int fr = !g->walking && !has_anim(pe->sheet, ANIM_IDLE) ? 0 : g->frame % E.w.sheets[sh].frames;
            char_teinte = TEINTE_FANTOME;
            add_character(g->x, g->z, fr, sera_row(g->vis), perso_remap(g->perso, g->var), sh, 2, pe->taille, g->y - ground_y(g->x, g->z) + veh_hover(g->perso, 0.7f), veh_rolls(g->perso), g->perso);
            char_teinte = 0;
        }
    }

    /* Liste aplatie : du fond (seau OTN-1) vers l'avant, dans l'ordre d'arrivée */
aplatir:
    BP(BPROF_PERSOS);
    image_finir();
}

/* Fin de la préparation d'une image (eng_build_frame, ag_image) : liste aplatie et rangée par bande, horizon, textures du sol
   demandées, statistiques, interface */
#if ENG_ROULIS
/* Caméra penchée : couleur du ciel de chaque ligne de l'image non penchée (de -AG_ROUL_M à RH + AG_ROUL_M), comme draw_sky (dégradé
   au-dessus de l'horizon, fond des trous en dessous) ; ciel_roule la prend pixel par pixel */
static void ciel_table(void) {
    uint16_t *t = ag_ciel_tab[B == &FR[0] ? 0 : 1] + AG_ROUL_M;
    const float hy = B->sky_hy;
    const uint16_t fog = pack565((int)amb.fog[0], (int)amb.fog[1], (int)amb.fog[2]);
    for (int y = -AG_ROUL_M; y < RH + AG_ROUL_M; y++) {
        uint16_t col = fog;
        if (y < hy) {
            float tt = 1 - (hy - y) / (hy + 20);
            if (tt < 0) tt = 0; else if (tt > 1) tt = 1;
            col = pack565((int)(amb.top[0] + tt * tt * (amb.hor[0] - amb.top[0])), (int)(amb.top[1] + tt * (amb.hor[1] - amb.top[1])), (int)(amb.top[2] + tt * (amb.hor[2] - amb.top[2])));
        } else {
            const float d = y + 0.5f - hy;
            if (B->trou_hk > 0 && d > 0) {
                const float f = (B->trou_hk / d - B->trou_z0) * B->trou_k;
                if (f < 1) { const float g = f > 0 ? f : 0; col = pack565((int)(amb.fog[0] * g), (int)(amb.fog[1] * g), (int)(amb.fog[2] * g)); }
            }
        }
        t[y] = col;
    }
    B->ciel_tab = t;
}
#endif
static void image_finir(void) {
    const world_t *w = &E.w;
    const camera_t *cam = &E.cam;
    B->norder = 0;
    for (int b = OTN - 1; b >= 0; b--) for (int i = ot_head[b]; i >= 0; i = B->prims[i].next) B->order[B->norder++] = i;
    {
        uint16_t count[NBANDS + 1];
        memset(count, 0, sizeof count);
        int total = 0;
        for (int k = 0; k < B->norder; k++) {
            const prim_t *p = &B->prims[B->order[k]];
            int b0 = p->miny < 0 ? 0 : p->miny / ENG_BAND, b1 = (p->maxy - 1) / ENG_BAND;
            if (b1 >= NBANDS) b1 = NBANDS - 1;
            for (int b = b0; b <= b1; b++) { count[b]++; total++; }
        }
        B->bin_ok = total <= BIN_CAP;
        if (B->bin_ok) {
            uint16_t fill[NBANDS];
            B->bin_first[0] = 0;
            for (int b = 0; b < NBANDS; b++) { B->bin_first[b + 1] = (uint16_t)(B->bin_first[b] + count[b]); fill[b] = B->bin_first[b]; }
            for (int k = 0; k < B->norder; k++) {
                const prim_t *p = &B->prims[B->order[k]];
                int b0 = p->miny < 0 ? 0 : p->miny / ENG_BAND, b1 = (p->maxy - 1) / ENG_BAND;
                if (b1 >= NBANDS) b1 = NBANDS - 1;
                for (int b = b0; b <= b1; b++) B->bin_idx[fill[b]++] = (uint16_t)B->order[k];
            }
        }
    }
    BP(BPROF_TRI);
    /* Ciel : ligne d'horizon de l'image (plus de silhouette de ville : la même tout autour, elle n'allait avec aucun lieu) */
    B->sky_hy = pcy - FOCAL * tanf(cam->pitch);
    {   /* Trous sous l'horizon (draw_sky) : la ligne y touche le plan du sol le plus bas de l'image à la profondeur
           trou_hk / (y + 0,5 - horizon) ; brouillard de cette profondeur, de 0 à 1 */
        const float H = cam->y - sol_bas;
        B->trou_hk = H > 0 && cam->cp > 0.01f ? H * FOCAL / cam->cp : 0;
        B->trou_z0 = fog_z0c; B->trou_k = 1.0f / (fog_fullc - fog_z0c);
    }
#if ENG_ROULIS
    if (B->roule) ciel_table();
#endif
    BP(BPROF_CIEL);
    /* Textures du sol lues à la demande (carte SD) : celles des polygones de l'image, dans la limite de l'image ; les
       autres prennent la texture de remplacement jusqu'à leur arrivée */
    B->stats.tex_need = B->stats.tex_miss = 0;
    if (w->stream)
        for (int i = 0; i < B->nprims; i++) {
            const prim_t *q = &B->prims[i];
            if (q->kind != K_POLY || q->tile < w->g0 || q->tile >= TILE_BIG) continue;
            B->stats.tex_need++;
            if (!ground_need(q->tile - (int)w->g0, 0)) B->stats.tex_miss++;
        }
    BP(BPROF_SOL);   /* (textures du sol demandées : à part de l'interface, 08/11/2026) */
    /* Triangles et sprites comptés une fois par image (pixels et segments : au fil des bandes) */
    B->stats.prims = B->nprims; B->stats.verts = B->nverts;
    for (int i = 0; i < B->nprims; i++) {
        if (B->prims[i].kind == K_POLY) B->stats.tris += B->prims[i].nv - 2;
        else if (B->prims[i].kind == K_SPRITE) B->stats.sprites++;
    }
    /* Interface : liste d'opérations de l'image, en coordonnées natives */
    ui_begin(E.cfg.swap565);
#ifndef ESP_PLATFORM
    extern int eng_essai_sans_ui; if (!eng_essai_sans_ui)   /* rendu_test (scenecams) : l'image 3D seule */
#endif
    { if (AG.on) ag_hud(); else hud_build(); }   /* course anti-gravité (antigrav.c) : son interface */
    B->ui_cover = AG.on ? 0 : hud_cover_y();
    B->stats.ui_ops = ui_op_count(); B->stats.ui_pts = ui_pts_count();
    BP(BPROF_UI);
}

/* ---------- Course anti-gravité (antigrav.c, 13/11/2026) : la scène, sans le monde ----------
   Piste en 3D (chaussée, murs, faces extérieures et dessous du ruban, plaques), piliers, sol plat en dalles, engins en
   maillages low-poly (faces de couleur unie : texels de la tuile 0, qui est la palette), ombre des engins sur la chaussée.
   Le tampon de profondeur fait l'ordre ; seuls ce qui est collé à la chaussée (plaques, ombres) et les engins, qui planent
   juste au-dessus, sont avancés d'un seau fixe.
   Budget (13/11/2026, 30 i/s fixes, demande de l'utilisateur : « on sait que l'on tient environ 4000 triangles à l'écran ») :
   jamais plus de AG_TRI_MAX triangles soumis par image. D'abord les engins, du plus proche au plus loin (niveau de détail selon la
   distance ; AG_TRI_ENGINS au plus, au-delà le niveau le plus simple), puis la piste du plus proche au plus loin, les plaques, les piliers, le
   relief, le sol. Ce qui ne tient plus est écarté.
   Une seule précision de géométrie, à toute distance (14/11/2026, demande de l'utilisateur : « simplifier la géométrie hors de la
   piste pour limiter les polygones et ne plus utiliser différents niveaux de précision de géométrie, c'est trop visible à grande
   vitesse ») : piste en tronçons de AG_SEG sections (la ligne de départ seule en 1), dessous du ruban, faces extérieures des murs et
   piliers partout ; hors de la piste, simplifié : relief en cellules uniformes (ter_g de generer.js), flanc unique des parois
   naturelles, sol en dalles de AG_SOL_DALLE. Seuls les engins gardent leurs niveaux de détail.
   Sans brouillard depuis le 13/11/2026 (demande de l'utilisateur : « augmenter la distance d'affichage et supprimer la brume qui
   consomme de la puissance » : E.fog_on coupé par antigrav.c, chaque face prend le chemin le plus simple du tracé) : tout est vu
   jusqu'à AG_VUE ; terrain autour de la piste (AG.ter : ag_terrain), sol lointain en grandes dalles, traînées translucides des
   réacteurs ; écran de la piste : le circuit entier vu de haut, en basse définition (aperçu). */
#define AG_MUR_H 0.5f        /* hauteur des murs au-dessus de la chaussée */
#define AG_EP 0.35f          /* épaisseur du ruban de la piste */
#define AG_SOL_PAS 12.0f     /* dalle du sol (cases) : sa texture une fois */
#define AG_TEX_L 16.0f       /* texels par case le long de la piste */
#define AG_VUE (AG_TRI_MAX > 4000 ? 64.0f : 52.0f)   /* distance de vue (× VUE_K) ; 36 avec le brouillard ; LDK : 52 depuis les décors proches qui
                                                     masquent le lointain (14/11/2026 : le budget rendu aux engins) */
#define AG_SEG (AG_TRI_MAX > 4000 ? 2 : 4)   /* sections par tronçon de piste (partout ; LDK : 4, la piste simplifiée) */
#define AG_SOL_DALLE 24.0f   /* dalle du sol (cases, sa texture 2 × 2 fois), jusqu'à 3 × la distance de vue */
#define AG_SOL_APERCU 96.0f  /* dalle du sol de l'aperçu de la piste */
#define AG_TRAINEE_W 0.07f   /* demi-largeur de la traînée au réacteur */
#define AG_TRAINEE_L 1.0f    /* longueur de la traînée au plus (cases) : derrière l'engin, elle va vers la caméra de poursuite */
#define AG_PILIER 0.32f      /* demi-côté d'un pilier */
#define AG_TRI_ENGINS (AG_TRI_MAX * 2 / 5)   /* triangles des engins au plus (le reste à la piste, au relief et au sol) */
/* Précision des engins, la même à toute distance (14/11/2026, demande de l'utilisateur : « simplifions les concurrents ») : niveau de
   détail de l'engin suivi et des autres (0 : le plus fin) ; LDK : 320 et 140 triangles ; vitrine de l'écurie : le plus fin */
#if AG_TRI_MAX > 4000
#define AG_LOD_SUIVI 0
#define AG_LOD_ADV 0
#else
#define AG_LOD_SUIVI 1
#define AG_LOD_ADV 1   /* (2, 140 triangles, avant les décors proches : 14/11/2026, demande de l'utilisateur : du budget rendu aux engins) */
#endif
#define AG_BINS 32           /* rangement de la piste par distance */
#define AG_BRUME_VUE 30.0f   /* distance de vue par brume (épreuves de la carrière) */
static const float AG_SOLEIL[3] = { 0.4472f, 0.7746f, 0.4472f };
typedef struct { float p[3], u, v; } ag_pt_t;
static int ag_tris;          /* triangles soumis à l'image */
static int ag_det[4];        /* … dont engins, piste (plaques et piliers compris), relief, sol (eng_ag_detail : journal du simulateur) */
void eng_ag_detail(int *d) { for (int k = 0; k < 4; k++) d[k] = ag_det[k]; }
static float ag_coupe = 1e9f; /* distance de la première pièce de piste écartée faute de budget (1e9 : aucune) */
static int ag_apercu;        /* écran de la piste : le circuit entier vu de haut, en basse définition */
/* Ordre de dessin (13/11/2026, retour de l'utilisateur : « le clignotement des textures est très proche de notre véhicule, il y a des
   problèmes de superposition de textures », « une sorte d'escalier sur les bords de piste ») : là où deux surfaces se touchent (à
   1/64 de profondeur près), la première dessinée l'emporte ; l'ordre des seaux suivait la profondeur moyenne de chaque morceau et
   basculait d'une image à l'autre. Il est fixé, du premier au dernier : l'engin (AG_AV_ENGIN), son ombre, les plaques (AG_AV_PLAQUE),
   les traînées, la chaussée (reculée de AG_REC_CH), les murs et parois (AG_REC_MUR), le relief et le sol (AG_REC_SOL, comme
   SOL_RECUL d'Echoes) ; le tampon de profondeur fait le reste. Textures réduites : la table MIPS (antigrav.c) */
#define AG_AV_ENGIN 1.5f
#define AG_AV_PLAQUE 1.0f
/* Chaussée reculée d'au moins la moitié d'un tronçon de plus que l'ombre et les plaques posées dessus (14/11/2026, retours de
   l'utilisateur : « l'ombre du vaisseau scintille énormément », « les plaques souvent à moitié visibles dans la route » : avec des
   tronçons de AG_SEG sections, celui d'à côté, plus proche en moyenne, passait avant l'ombre ou la plaque posée sur lui) */
#define AG_REC_CH (1.0f + (float)AG_SEG)
#define AG_REC_MUR (AG_REC_CH + 0.7f)
#define AG_REC_SOL (AG_REC_CH + 1.5f)
static float ap_d, ap_y; static int ap_piste = -1;   /* aperçu : distance et décalage vertical lissés, piste de l'ajustement */
/* Tampons de la scène, réservés au chargement de la course (ag_rendu_reserver, depuis ag_charger) : rien sur la montre, qui ne joue
   pas la course et dont la PSRAM est presque pleine */
static uint16_t *ag_seg_i, *ag_seg_ordre;
static uint8_t *ag_seg_m, *ag_seg_bin;
static float *ag_seg_d;
static v3 *ag_vv;
static float (*ag_vw)[3];
static uint16_t *ag_hz_u[2];
static uint16_t ag_hz_pal[4][256];
/* Vue cockpit (antigrav.c, ag_cockpit) : le cadre de l'écurie du joueur (AGCK) posé en bas de l'image, à la hauteur de l'image ou à
   sa largeur s'il le faut pour la couvrir (16:9 sur la LDK : ses côtés rognés), dans la définition la plus petite qui ne soit pas
   agrandie (la plus grande sinon) ; ses pixels pleins sont écrits avant toute la 3D, couverts et au plus près (ag_cockpit_band) :
   ce qui est derrière n'est ni texturé ni écrit */
static const ag_image_t *ag_ck_im;
static uint16_t ag_ck_pal[256];
/* Vitre du cockpit par ligne de son image (première et dernière colonne vide ; l > r : ligne pleine) : les lignes de l'image bornées
   à elle (span_l, span_r, comme le cercle de la montre) : la 3D et le ciel derrière le cadre ne sont plus parcourus du tout (banc de la
   LDK du 14/11/2026 : chaque pixel caché coûtait encore son test de couverture et de profondeur, la vue cockpit était plus lente que
   dehors) ; ag_spans_ck : bornes posées (remises à toute la ligne hors du cockpit) */
/* Écrans du tableau de bord (14/11/2026, demande de l'utilisateur : « des emplacements d'affichage du hud de vitesse et de la carte
   comme un écran du cockpit ») : rectangles en part du cadre 16:9 (u, v de son coin haut gauche à son coin bas droit), peints sur
   chaque image du cockpit par tools/antigrav/forge_images.py (ECRANS, à garder identiques) : 0 la vitesse (au milieu, sur le capot
   des instruments), 1 la carte (à gauche), 2 la place et le tour (à droite) ; ag_cockpit_ecran les donne à l'interface (antigrav.c) */
static const float AG_ECRANS[3][4] = { { 0.435f, 0.705f, 0.565f, 0.815f }, { 0.17f, 0.77f, 0.31f, 0.955f }, { 0.69f, 0.77f, 0.83f, 0.955f } };
static float ag_ecr[3][4];   /* repère de l'interface (466 au milieu) */
static int ag_ecr_ok;
int ag_cockpit_ecran(int k, float *x0, float *y0, float *x1, float *y1) {
    if (!ag_ecr_ok || k < 0 || k > 2) return 0;
    *x0 = ag_ecr[k][0]; *y0 = ag_ecr[k][1]; *x1 = ag_ecr[k][2]; *y1 = ag_ecr[k][3];
    return 1;
}
#define AG_CK_HMAX 544
static int16_t ag_ck_ol[AG_CK_HMAX], ag_ck_or[AG_CK_HMAX];
static int ag_spans_ck;
static int16_t *ag_ck_u[2];
static int ag_ck_on;
#define AG_CK_NET 5.0f    /* flou de profondeur (version PC) : net autour de ce point devant l'œil, comme l'engin vu de dehors */
int ag_rendu_reserver(void) {
    ag_hz_u[0] = E.cfg.alloc((RW_MAX + 2 * AG_ROUL_M) * 2, 0); ag_hz_u[1] = E.cfg.alloc((RW_MAX + 2 * AG_ROUL_M) * 2, 0);
    if (!ag_hz_u[0] || !ag_hz_u[1]) return 0;
#if ENG_ROULIS
    ag_ciel_tab[0] = E.cfg.alloc((RH_MAX + 2 * AG_ROUL_M) * 2, 0); ag_ciel_tab[1] = E.cfg.alloc((RH_MAX + 2 * AG_ROUL_M) * 2, 0);
    if (!ag_ciel_tab[0] || !ag_ciel_tab[1]) return 0;
#endif
    ag_ck_u[0] = E.cfg.alloc(RW_MAX * 2, 0); ag_ck_u[1] = E.cfg.alloc(RW_MAX * 2, 0);
    if (!ag_ck_u[0] || !ag_ck_u[1]) return 0;
    ag_seg_i = E.cfg.alloc(AG_SECT_MAX * 2, 0); ag_seg_ordre = E.cfg.alloc(AG_SECT_MAX * 2, 0);
    ag_seg_m = E.cfg.alloc(AG_SECT_MAX, 0); ag_seg_bin = E.cfg.alloc(AG_SECT_MAX, 0); ag_seg_d = E.cfg.alloc(AG_SECT_MAX * 4, 0);
    ag_vv = E.cfg.alloc(sizeof(v3) * AG_SOMMETS_MAX, 0); ag_vw = E.cfg.alloc(sizeof(float) * 3 * AG_SOMMETS_MAX, 0);
    return ag_seg_i && ag_seg_ordre && ag_seg_m && ag_seg_bin && ag_seg_d && ag_vv && ag_vw;
}
void ag_rendu_piste(void) {
    amb_t a;
    memset(&a, 0, sizeof a);
    for (int c = 0; c < 3; c++) { a.top[c] = AG.ciel[0][c]; a.hor[c] = AG.ciel[1][c]; a.fog[c] = AG.ciel[2][c]; a.tint[c] = AG.teinte[c] > 0 ? AG.teinte[c] : 1; }
    a.lum = AG.lum > 0 ? AG.lum : 1;
    amb = a; amb_ok = 1;
    build_cmap(NULL, 0);   /* (la palette du thème vient de changer) */
    if (AG.horizon < 0) return;
    static const float VOILE[4] = { 0.1f, 0.22f, 0.36f, 0.5f }, VOILE_BRUME[4] = { 0.55f, 0.66f, 0.76f, 0.85f };   /* (brume de la carrière : l'horizon presque noyé) */
    const ag_image_t *h = &AG.hz[AG.horizon];
    const float sombre = AG.cond & AGC_NUIT ? 0.42f : 1;   /* (nuit de la carrière : l'horizon assombri) */
    for (int b = 0; b < 4; b++) {
        ag_hz_pal[b][0] = 0;
        for (int i = 0; i < 255; i++) {
            if (i >= h->ncoul) { ag_hz_pal[b][i + 1] = 0; continue; }
            const float v = AG.cond & AGC_BRUME ? VOILE_BRUME[b] : VOILE[b];
            ag_hz_pal[b][i + 1] = pack565((int)(h->rgb[i * 3] * sombre * (1 - v) + amb.fog[0] * v), (int)(h->rgb[i * 3 + 1] * sombre * (1 - v) + amb.fog[1] * v), (int)(h->rgb[i * 3 + 2] * sombre * (1 - v) + amb.fog[2] * v));
        }
    }
}
static inline void cross3_r(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static inline float ag_lum(const float *n) { const float d = n[0] * AG_SOLEIL[0] + n[1] * AG_SOLEIL[1] + n[2] * AG_SOLEIL[2]; return 5.0f + 10.0f * (d > 0 ? d : 0); }
static inline int ag_vue(const float *p, const float *n) { return (E.cam.x - p[0]) * n[0] + (E.cam.y - p[1]) * n[1] + (E.cam.z - p[2]) * n[2] > 0; }
static inline float ag_rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
/* Polygone soumis dans le budget : -2 s'il n'y tient plus, -1 s'il n'est pas à l'écran */
static int ag_soumettre(const vv_t *v, int n, int tile, int forced) {
    if (ag_tris + n - 2 > AG_TRI_MAX) return -2;
    const int b = submit_poly(v, n, tile, forced, 0);
    if (b >= 0) ag_tris += n - 2;
    return b;
}
/* Polygone de n points du monde ; avance : seau de sa profondeur moyenne moins avance (négative : reculé) ; 0 : le seau de submit_poly.
   Quadrilatère qui n'est pas plat (tronçon de piste qui suit le dévers et la pente, plaque, paroi) : deux triangles, même seau (le
   tracé prend la texture d'un polygone sur un seul plan : sur un quadrilatère tordu elle glissait quand la vue changeait ; 14/11/2026,
   retour de l'utilisateur : « les textures bougent encore beaucoup trop, plein d'effets de déformation horribles », plus visible depuis
   les tronçons de 4 sections ; même nombre de triangles au budget) */
#define AG_TORDU 0.01f       /* écart du quatrième sommet au plan des trois autres, en part de la diagonale */
static int ag_poly(const ag_pt_t *q, int n, float l, int tile, float avance) {
    vv_t v[4];
    float zs = 0;
    for (int i = 0; i < n; i++) {
        const v3 o = to_view(q[i].p[0], q[i].p[1], q[i].p[2]);
        v[i].x = o.x; v[i].y = o.y; v[i].z = o.z; v[i].u = q[i].u; v[i].v = q[i].v; v[i].l = l; zs += o.z;
    }
    const int b = avance != 0 ? bucket_of(zs / n - avance) : -1;
    if (n == 4) {
        const float *A = q[0].p, *Bp = q[1].p, *C = q[2].p, *D = q[3].p;
        const float e1[3] = { Bp[0] - A[0], Bp[1] - A[1], Bp[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] }, e3[3] = { D[0] - A[0], D[1] - A[1], D[2] - A[2] };
        float nn[3];
        cross3_r(e1, e2, nn);
        const float d = nn[0] * e3[0] + nn[1] * e3[1] + nn[2] * e3[2], ln2 = nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2];
        const float dg = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2];
        if (ln2 > 1e-12f && d * d > AG_TORDU * AG_TORDU * ln2 * dg) {   /* (d / |n| : distance de D au plan, comparée à AG_TORDU × la diagonale) */
            const vv_t t1[3] = { v[0], v[1], v[2] }, t2[3] = { v[0], v[2], v[3] };
            const int r = ag_soumettre(t1, 3, tile, b);
            if (r == -2) return -2;
            const int r2 = ag_soumettre(t2, 3, tile, b);
            return r2 == -2 ? -2 : r >= 0 ? r : r2;
        }
    }
    return ag_soumettre(v, n, tile, b);
}
static inline void ag_pt(ag_pt_t *o, const float *a, const float *d, float k, const float *e, float k2, float u, float v) {
    for (int i = 0; i < 3; i++) o->p[i] = a[i] + (d ? d[i] * k : 0) + (e ? e[i] * k2 : 0);
    o->u = u; o->v = v;
}
/* Sol : dalles de pas cases dont le centre est entre r0 et r1 de la caméra, devant elle, les proches d'abord (deux passes) ; pas
   sous le terrain (sa grille : il descend au sol sur ses bords) ; texture une fois par AG_SOL_PAS ; 0 si le budget est épuisé */
static int ag_sol_anneau(float r0, float r1, float pas) {
    const camera_t *c = &E.cam;
    const float fx = c->sy, fz = c->cy, y = AG.sol, l = ag_lum((const float[3]){ 0, 1, 0 }) * 0.9f, tu = pas / AG_SOL_PAS * 64 - 0.01f;
    if (c->y <= y) return 1;
    const float tx0 = AG.ter_x0, tz0 = AG.ter_z0, tx1 = tx0 + AG.ter_g * (float)(AG.ter_nx - 1), tz1 = tz0 + AG.ter_g * (float)(AG.ter_nz - 1);
    const int i0 = (int)floorf((c->x - r1) / pas), i1 = (int)floorf((c->x + r1) / pas);
    const int j0 = (int)floorf((c->z - r1) / pas), j1 = (int)floorf((c->z + r1) / pas);
    for (int passe = 0; passe < 2; passe++)
        for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
            const float x0 = (float)i * pas, z0 = (float)j * pas, cx = x0 + pas / 2 - c->x, cz = z0 + pas / 2 - c->z, d2 = cx * cx + cz * cz;
            if (d2 > (r1 + pas) * (r1 + pas) || d2 < r0 * r0 || cx * fx + cz * fz < -pas || (d2 < (r0 + r1) * (r0 + r1) / 4) != (passe == 0)) continue;
            if (AG.ter && x0 >= tx0 && x0 + pas <= tx1 && z0 >= tz0 && z0 + pas <= tz1) continue;
            const float a0 = ag_apercu ? 20.5f : 0.01f, a1 = ag_apercu ? 20.5f : tu;   /* (aperçu : une couleur) */
            const ag_pt_t q[4] = { { { x0, y, z0 }, a0, a0 }, { { x0 + pas, y, z0 }, a1, a0 },
                                   { { x0 + pas, y, z0 + pas }, a1, a1 }, { { x0, y, z0 + pas }, a0, a1 } };
            if (ag_poly(q, 4, l, TILE_BIG + AG.tex[AGT_SOL], -AG_REC_SOL) == -2) return 0;
        }
    return 1;
}
static void ag_sol(void) {
    const float R = fog_fullc;
    if (ag_apercu) { ag_sol_anneau(0, R, AG_SOL_APERCU); return; }
    ag_sol_anneau(0, R * 3, AG_SOL_DALLE);
}
/* Terrain autour de la piste (AG.ter, generer.js : relief du thème, lit plat au niveau de la chaussée, ponts au-dessus des creux),
   en cellules uniformes de ter_g cases jusqu'au bord de l'île (le sol plat n'est pas dessiné sous elle), à toute distance la même
   précision (14/11/2026 : les super-cellules à 1, 2 ou 4 cellules de pas selon la distance se voyaient changer à grande vitesse) ;
   aperçu de la piste : une cellule sur deux. Anneaux du plus proche au plus loin (le budget écarte le plus loin) ; cellules hors
   du champ écartées (cône des côtés de l'image, élargi à sa diagonale quand la caméra penche) ; deux triangles par cellule (la
   diagonale qui suit le relief) ; pente forte : roche (texture debout), sinon le sol du thème ; lumière de la face ; dessiné après
   le reste (AG_REC_SOL) */
static inline float ter_h(int i, int j) {
    i = i < 0 ? 0 : i >= AG.ter_nx ? AG.ter_nx - 1 : i;
    j = j < 0 ? 0 : j >= AG.ter_nz ? AG.ter_nz - 1 : j;
    const uint8_t *p = AG.ter + ((size_t)j * (size_t)AG.ter_nx + (size_t)i) * 2;
    return AG.sol + (float)(int16_t)(p[0] | p[1] << 8) / 16.0f;
}
static int ter_tri(const float *a, const float *b, const float *d) {
    const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
    float n[3];
    cross3_r(e1, e2, n);
    const float ln = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (ln < 1e-6f) return 1;
    for (int k = 0; k < 3; k++) n[k] /= ln;
    if (!ag_vue(a, n)) return 1;
    const float *P[3] = { a, b, d };
    ag_pt_t q[3];
    const int roche = n[1] < 0.72f, axe = fabsf(n[0]) > fabsf(n[2]) ? 2 : 0;
    float u0 = 1e9f, v0 = 1e9f;
    for (int k = 0; k < 3; k++) {
        q[k].p[0] = P[k][0]; q[k].p[1] = P[k][1]; q[k].p[2] = P[k][2];
        q[k].u = ag_apercu ? 20.5f : roche ? P[k][axe] * 8 : P[k][0] * 5.33f; q[k].v = ag_apercu ? 20.5f : roche ? -P[k][1] * 8 : P[k][2] * 5.33f;   /* (aperçu : une couleur) */
        u0 = fminf(u0, q[k].u); v0 = fminf(v0, q[k].v);
    }
    u0 = floorf(u0 / 64) * 64; v0 = floorf(v0 / 64) * 64;
    for (int k = 0; k < 3; k++) { q[k].u -= u0 - 0.01f; q[k].v -= v0 - 0.01f; }
    const int sable = AG.eau && fminf(a[1], fminf(b[1], d[1])) > AG.sol + 0.25f;   /* (au bord de l'eau : la mer en bas, le sable au-dessus) */
    return ag_poly(q, 3, ag_lum(n), TILE_BIG + AG.tex[roche ? AGT_ROCHE : sable ? AGT_TERRE : AGT_SOL], -AG_REC_SOL) != -2;
}
static int ter_quad(const float *p00, const float *p10, const float *p11, const float *p01) {
    if (fabsf(p00[1] - p11[1]) <= fabsf(p10[1] - p01[1])) return ter_tri(p00, p11, p10) && ter_tri(p00, p01, p11);
    return ter_tri(p10, p00, p01) && ter_tri(p10, p01, p11);
}
/* Visibilité précalculée (14/11/2026, demande de l'utilisateur : « n'avoir que des faces dans le champ de vision, inutile de faire de
   la géométrie qui ne se voit jamais ; les immeubles et montagnes permettent de bloquer la vue ») : bloc VIS1 de la piste (generer.js,
   visibilite) ; pour la section la plus proche de la caméra, la liste de son groupe : cellules du relief, décors, sections de la piste
   qui peuvent se voir de là ; rien d'autre n'est parcouru. Caméra hors du couloir des points de vue (de côté au-delà de hw +
   AG_VIS_COTE, au-dessus de la chaussée au-delà de AG_VIS_HAUT ; aperçu, essais) : sans liste, tout ce qui est dans le champ */
#define AG_VIS_COTE 1.6f     /* (generer.js : VIS_COTE, VIS_H) */
#define AG_VIS_HAUT 3.0f
#define AG_VIS_CEL 4096      /* cellules du relief d'un groupe, au plus */
#ifndef AG_VIS_PARTS
#define AG_VIS_PARTS 7       /* (essais : listes du relief 1, des décors 2, des sections 4) */
#endif
static int ag_vis_on;
static const uint8_t *ag_vis_d;
static uint32_t ag_vsec[(AG_SECT_MAX + 31) / 32];
static inline uint32_t ag_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int ag_rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline int ag_vsec_vu(int i) { return !(AG_VIS_PARTS & 4) || !ag_vis_on || (ag_vsec[i >> 5] >> (i & 31) & 1); }
static int ag_vis_sec = -1, ag_vis_grp = -1;
void eng_ag_vis_diag(float *xyz, int *sec, int *groupe) { xyz[0] = E.cam.x; xyz[1] = E.cam.y; xyz[2] = E.cam.z; *sec = ag_vis_sec; *groupe = ag_vis_on ? ag_vis_grp : -1; }
static void ag_vis_maj(void) {
    ag_vis_on = 0; ag_vis_sec = -1;
    if (!AG.vis || ag_apercu || AG.n <= 0) return;
    const camera_t *c = &E.cam;
    int best = 0;
    float bd = 1e30f;
    for (int i = 0; i < AG.n; i++) {
        const ag_sect_t *S = &AG.s[i];
        const float dx = S->x - c->x, dy = S->y - c->y, dz = S->z - c->z, d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    const ag_sect_t *S = &AG.s[best];
    ag_vis_sec = best;
    const float dx = c->x - S->x, dy = c->y - S->y, dz = c->z - S->z;
    const float lat = dx * S->rx + dy * S->ry + dz * S->rz, haut = dx * S->ux + dy * S->uy + dz * S->uz;
    if (fabsf(lat) > S->hw + AG_VIS_COTE || haut > AG_VIS_HAUT || haut < -0.5f) return;
    const int inv = (AG.cond & AGC_INVERSE) != 0, orig = inv ? (AG.n - best) % AG.n : best;   /* (sens inverse : les listes suivent l'ordre du paquet) */
    const int g = orig / AG.vis_g;
    if (g >= AG.vis_ng) return;
    const uint32_t o0 = ag_rd32(AG.vis_pos + 4 * (size_t)g), o1 = ag_rd32(AG.vis_pos + 4 * (size_t)g + 4);
    if (o1 < o0 + 8 || AG.vis + o1 > AG.vis_fin) return;
    const uint8_t *d = AG.vis + o0;
    const int nt = ag_rd16(d), nd = ag_rd16(d + 2), ns = ag_rd16(d + 4);
    if (8 + 6 * (uint32_t)nt + 2 * (uint32_t)nd + 4 * (uint32_t)ns > o1 - o0) return;
    memset(ag_vsec, 0, sizeof ag_vsec);
    const uint8_t *p = d + 8 + 6 * nt + 2 * nd;
    for (int k = 0; k < ns; k++) {
        const int s0 = ag_rd16(p + 4 * k), s1 = ag_rd16(p + 4 * k + 2);
        for (int s = s0; s <= s1 && s < AG.n; s++) { const int si = inv ? (AG.n - s) % AG.n : s; ag_vsec[si >> 5] |= 1u << (si & 31); }
    }
    ag_vis_d = d; ag_vis_on = 1; ag_vis_grp = g;
}
/* Relief d'après la liste du groupe : cellules dans le champ, du plus proche au plus loin (tranches de distance) */
static void ag_terrain_vis(void) {
    const camera_t *c = &E.cam;
    const float g = AG.ter_g, fx = c->sy, fz = c->cy, marge = 1.2f * g, R = g * (float)(AG.ter_nx + AG.ter_nz);   /* (R : tranches jusqu'au bord du terrain) */
    float k = ((float)RW / 2 + 2) / FOCAL;
#if ENG_ROULIS
    if (cam_roule) { const float ky = ((float)RH / 2 + 2) / FOCAL; k = sqrtf(k * k + ky * ky); }
#endif
    static uint16_t cel[AG_VIS_CEL], ordre[AG_VIS_CEL];
    static uint8_t bin[AG_VIS_CEL];
    int nc = 0, cpt[AG_BINS + 1] = { 0 };
    const int NI = AG.ter_nx - 1, NJ = AG.ter_nz - 1, nt = ag_rd16(ag_vis_d);
    for (int r = 0; r < nt && nc < AG_VIS_CEL; r++) {
        const uint8_t *e = ag_vis_d + 8 + 6 * r;
        const int J = ag_rd16(e), I0 = ag_rd16(e + 2), I1 = ag_rd16(e + 4);
        if (J >= NJ) continue;
        for (int I = I0; I <= I1 && I < NI && nc < AG_VIS_CEL; I++) {
            const float cx = AG.ter_x0 + g * ((float)I + 0.5f) - c->x, cz = AG.ter_z0 + g * ((float)J + 0.5f) - c->z;
            const float dev = cx * fx + cz * fz, tr = cx * fz - cz * fx;
            const float zv = dev * c->cp + (c->y - AG.sol) * c->sp;
            if (zv < -marge || fabsf(tr) > (zv > 0 ? zv : 0) * k + marge) continue;
            const int b = (int)(sqrtf(cx * cx + cz * cz) / R * AG_BINS);
            cel[nc] = (uint16_t)(J * NI + I); bin[nc] = (uint8_t)(b < 0 ? 0 : b >= AG_BINS ? AG_BINS - 1 : b); cpt[bin[nc] + 1]++; nc++;
        }
    }
    for (int b = 0; b < AG_BINS; b++) cpt[b + 1] += cpt[b];
    for (int i = 0; i < nc; i++) ordre[cpt[bin[i]]++] = cel[i];
    for (int o = 0; o < nc; o++) {
        const int I = ordre[o] % NI, J = ordre[o] / NI;
        float p[4][3];
        for (int v = 0; v < 4; v++) {
            const int i = I + (v == 1 || v == 2), j = J + (v >= 2);
            p[v][0] = AG.ter_x0 + g * (float)i; p[v][1] = ter_h(i, j); p[v][2] = AG.ter_z0 + g * (float)j;
        }
        if (!ter_quad(p[0], p[1], p[2], p[3])) return;
    }
}
static void ag_terrain(void) {
    if (!AG.ter) return;
    if ((AG_VIS_PARTS & 1) && ag_vis_on && (AG.ter_nx - 1) * (AG.ter_nz - 1) <= 65535) { ag_terrain_vis(); return; }
    const camera_t *c = &E.cam;
    const int s = ag_apercu ? 2 : 1, NI = (AG.ter_nx - 1) / s, NJ = (AG.ter_nz - 1) / s;
    const float g = AG.ter_g * (float)s, fx = c->sy, fz = c->cy, marge = 1.2f * g;
    float k = ((float)RW / 2 + 2) / FOCAL;   /* côtés de l'image : |travers| <= devant × k */
#if ENG_ROULIS
    if (cam_roule) { const float ky = ((float)RH / 2 + 2) / FOCAL; k = sqrtf(k * k + ky * ky); }
#endif
    const int ic = (int)floorf((c->x - AG.ter_x0) / g), jc = (int)floorf((c->z - AG.ter_z0) / g);
    int rmax = ic > NI - 1 - ic ? ic : NI - 1 - ic;
    rmax = rmax > jc ? rmax : jc; rmax = rmax > NJ - 1 - jc ? rmax : NJ - 1 - jc;
    for (int r = 0; r <= rmax; r++)
        for (int J = jc - r; J <= jc + r; J++)
            for (int I = ic - r; I <= ic + r; I += (J == jc - r || J == jc + r || r == 0) ? 1 : 2 * r) {
                if (I < 0 || J < 0 || I >= NI || J >= NJ) continue;
                const float cx = AG.ter_x0 + g * ((float)I + 0.5f) - c->x, cz = AG.ter_z0 + g * ((float)J + 0.5f) - c->z;
                const float dev = cx * fx + cz * fz, tr = cx * fz - cz * fx;
                const float zv = dev * c->cp + (c->y - AG.sol) * c->sp;   /* profondeur de vue au plus (point au niveau du sol ; tangage compris) */
                if (zv < -marge || fabsf(tr) > (zv > 0 ? zv : 0) * k + marge) continue;   /* derrière la caméra, hors des côtés */
                float p[4][3];
                for (int v = 0; v < 4; v++) {
                    const int i = (I + (v == 1 || v == 2)) * s, j = (J + (v >= 2)) * s;
                    p[v][0] = AG.ter_x0 + AG.ter_g * (float)i; p[v][1] = ter_h(i, j); p[v][2] = AG.ter_z0 + AG.ter_g * (float)j;
                }
                if (!ter_quad(p[0], p[1], p[2], p[3])) return;
            }
}
/* Décors proches (AG.dec, generer.js : decorPiste ; 14/11/2026, demande de l'utilisateur : « du décor beaucoup plus proche avec de la
   hauteur » pour masquer l'apparition des géométries) : prismes à n côtés (immeubles en boîte, aiguilles de roche effilées), du plus
   proche au plus loin jusqu'à la vue, hors du champ écartés ; flancs à la texture de la roche du thème (la ville : béton aux fenêtres
   éclairées), dessus aux piliers ; les plus lointains (au-delà de AG_DEC_FONDU de la vue) apparaissent en fondu translucide */
#define AG_DEC_FONDU 0.8f
static void ag_uv_base(ag_pt_t *q, int n);
static void ag_decors(void) {
    if (!AG.dec || ag_apercu) return;
    const camera_t *c = &E.cam;
    const float R = fog_fullc, fx = c->sy, fz = c->cy;
    float k = ((float)RW / 2 + 2) / FOCAL;
#if ENG_ROULIS
    if (cam_roule) { const float ky = ((float)RH / 2 + 2) / FOCAL; k = sqrtf(k * k + ky * ky); }
#endif
    /* visibles, rangés par distance (tranches) */
    static uint16_t vis[1024];
    static uint8_t bin[1024];
    int nv = 0, cpt[AG_BINS + 1] = { 0 };
    const int dl = (AG_VIS_PARTS & 2) && ag_vis_on, nliste = dl ? ag_rd16(ag_vis_d + 2) : AG.ndec;
    const uint8_t *liste = dl ? ag_vis_d + 8 + 6 * ag_rd16(ag_vis_d) : NULL;
    for (int ii = 0; ii < nliste && nv < 1024; ii++) {
        const int i = liste ? ag_rd16(liste + 2 * ii) : ii;
        if (i >= AG.ndec) continue;
        const uint8_t *p = AG.dec + (size_t)i * 24;
        const float x = ag_rdf(p), z = ag_rdf(p + 4), r = ag_rdf(p + 16), dx = x - c->x, dz = z - c->z, d = sqrtf(dx * dx + dz * dz);
        if (d > R + r) continue;
        const float dev = dx * fx + dz * fz, tr = dx * fz - dz * fx;
        if (dev < -r || fabsf(tr) > (dev > 0 ? dev : 0) * k + r * 1.5f) continue;
        const int b = (int)(d / (R + 1) * AG_BINS);
        bin[nv] = (uint8_t)(b < 0 ? 0 : b >= AG_BINS ? AG_BINS - 1 : b); vis[nv++] = (uint16_t)i; cpt[bin[nv - 1] + 1]++;
    }
    for (int b = 0; b < AG_BINS; b++) cpt[b + 1] += cpt[b];
    static uint16_t ordre[1024];
    for (int v = 0; v < nv; v++) ordre[cpt[bin[v]]++] = vis[v];
    for (int o = 0; o < nv; o++) {
        const uint8_t *p = AG.dec + (size_t)ordre[o] * 24;
        const float x = ag_rdf(p), z = ag_rdf(p + 4), y0 = ag_rdf(p + 8), y1 = ag_rdf(p + 12), r = ag_rdf(p + 16);
        const int n = p[20] < 3 ? 3 : p[20] > 8 ? 8 : p[20], bat = p[21] == 1, veg = p[21] == 2, bati = p[21] == 3;   /* sortes : 0 roche, 1 immeuble, 2 végétal, 3 bâtiment (béton du thème) */
        const float a0 = (float)p[22] * (2 * PI_F / 256), eff = (float)p[23] / 100.0f, dx = x - c->x, dz = z - c->z, d = sqrtf(dx * dx + dz * dz);
        const int fade = d > R * AG_DEC_FONDU ? (int)fminf(14, (d - R * AG_DEC_FONDU) / (R * (1 - AG_DEC_FONDU)) * 15) : 0;
        float Vb[8][3], Vt[8][3];
        for (int j = 0; j < n; j++) {
            const float a = a0 + (float)j * (2 * PI_F / (float)n), ca = cosf(a), sa = sinf(a);
            Vb[j][0] = x + r * ca; Vb[j][1] = y0; Vb[j][2] = z + r * sa;
            Vt[j][0] = x + r * eff * ca; Vt[j][1] = y1; Vt[j][2] = z + r * eff * sa;
        }
        const int p0 = B->nprims;
        float u = 0;
        for (int j = 0; j < n; j++) {
            const int j2 = (j + 1) % n;
            const float e1[3] = { Vb[j2][0] - Vb[j][0], 0, Vb[j2][2] - Vb[j][2] }, L = sqrtf(e1[0] * e1[0] + e1[2] * e1[2]);
            float nn[3] = { e1[2], 0, -e1[0] };   /* dehors (sens des sommets : angle croissant) */
            const float cx = (Vb[j][0] + Vb[j2][0]) / 2 - x, cz = (Vb[j][2] + Vb[j2][2]) / 2 - z;
            if (nn[0] * cx + nn[2] * cz < 0) { nn[0] = -nn[0]; nn[2] = -nn[2]; }
            const float ln = sqrtf(nn[0] * nn[0] + nn[2] * nn[2]);
            if (ln > 1e-6f) { nn[0] /= ln; nn[2] /= ln; }
            const float hy = (1 - eff) * r;   /* flanc effilé : normale relevée */
            nn[1] = hy / fmaxf(1e-3f, y1 - y0); { const float l2 = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]); nn[0] /= l2; nn[1] /= l2; nn[2] /= l2; }
            const float u1 = u + L * 8;
            if (ag_vue(Vb[j], nn)) {
                ag_pt_t q[4] = { { { Vb[j][0], Vb[j][1], Vb[j][2] }, u, (y1 - y0) * 8 }, { { Vb[j2][0], Vb[j2][1], Vb[j2][2] }, u1, (y1 - y0) * 8 },
                                 { { Vt[j2][0], Vt[j2][1], Vt[j2][2] }, u1, 0.01f }, { { Vt[j][0], Vt[j][1], Vt[j][2] }, u, 0.01f } };
                ag_uv_base(q, 4);
                if (ag_poly(q, 4, ag_lum(nn), TILE_BIG + AG.tex[veg ? AGT_FEUILLE : bati ? AGT_PILIER : AGT_ROCHE], -AG_REC_MUR) == -2) return;
            }
            u = fmodf(u1, 64);
        }
        static const float HAUT[3] = { 0, 1, 0 };
        if (c->y > y1) {   /* dessus : éventail de quadrilatères */
            for (int j = 1; j + 1 < n; j += 2) {
                const int j3 = j + 2 < n ? j + 2 : j + 1;
                ag_pt_t q[4] = { { { Vt[0][0], Vt[0][1], Vt[0][2] }, Vt[0][0] * 8, Vt[0][2] * 8 }, { { Vt[j][0], Vt[j][1], Vt[j][2] }, Vt[j][0] * 8, Vt[j][2] * 8 },
                                 { { Vt[j + 1][0], Vt[j + 1][1], Vt[j + 1][2] }, Vt[j + 1][0] * 8, Vt[j + 1][2] * 8 }, { { Vt[j3][0], Vt[j3][1], Vt[j3][2] }, Vt[j3][0] * 8, Vt[j3][2] * 8 } };
                ag_uv_base(q, 4);
                if (ag_poly(q, j3 == j + 1 ? 3 : 4, ag_lum(HAUT), TILE_BIG + AG.tex[bat || bati ? AGT_PILIER : veg ? AGT_FEUILLE : AGT_ROCHE], -AG_REC_MUR) == -2) return;
            }
        }
        if (c->y < y0) {   /* dessous (décor suspendu : chapeau, couronne), vu d'en bas */
            static const float BAS[3] = { 0, -1, 0 };
            for (int j = 1; j + 1 < n; j += 2) {
                const int j3 = j + 2 < n ? j + 2 : j + 1;
                ag_pt_t q[4] = { { { Vb[0][0], Vb[0][1], Vb[0][2] }, Vb[0][0] * 8, Vb[0][2] * 8 }, { { Vb[j3][0], Vb[j3][1], Vb[j3][2] }, Vb[j3][0] * 8, Vb[j3][2] * 8 },
                                 { { Vb[j + 1][0], Vb[j + 1][1], Vb[j + 1][2] }, Vb[j + 1][0] * 8, Vb[j + 1][2] * 8 }, { { Vb[j][0], Vb[j][1], Vb[j][2] }, Vb[j][0] * 8, Vb[j][2] * 8 } };
                ag_uv_base(q, 4);
                const int nq = j3 == j + 1 ? 3 : 4;
                if (nq == 3) { q[1] = q[2]; q[2] = q[3]; }
                if (ag_poly(q, nq, ag_lum(BAS) * 0.7f, TILE_BIG + AG.tex[veg ? AGT_FEUILLE : bati ? AGT_PILIER : AGT_ROCHE], -AG_REC_MUR) == -2) return;
            }
        }
        if (fade) for (int q = p0; q < B->nprims; q++) B->prims[q].fade = (uint8_t)fade;
    }
}
/* Obstacles des canyons (AG.obst, antigrav.c) : piliers de roche posés sur la chaussée, prismes effilés dans le repère de la section
   (le haut de la piste), soulevés avec elle par les ondes du SÉISME ; ceux dont la section est vue (visibilité précalculée) */
static int ag_vue_face(const float *a, const float *b, const float *c, const float *vers, float *n);
static int ag_obstacles(void) {
    const camera_t *c = &E.cam;
    const float R = fog_fullc;
    for (int k = 0; k < AG.nobst; k++) {
        const ag_obst_t *o = &AG.obst[k];
        if (!ag_vsec_vu(o->sec)) continue;
        const ag_sect_t *S = &AG.s[o->sec];
        const float dep = ag_onde_dep(o->sec), u[3] = { S->ux, S->uy, S->uz }, r[3] = { S->rx, S->ry, S->rz }, f[3] = { S->fx, S->fy, S->fz };
        const float b[3] = { S->x + r[0] * o->d + u[0] * dep, S->y + r[1] * o->d + u[1] * dep, S->z + r[2] * o->d + u[2] * dep };
        const float dx = b[0] - c->x, dz = b[2] - c->z;
        if (dx * dx + dz * dz > R * R || dx * c->sy + dz * c->cy < -2) continue;
        float Vb[8][3], Vt[8][3];
        const int n = o->cotes;
        for (int j = 0; j < n; j++) {
            const float a = o->a + (float)j * (6.2831853f / (float)n), ca = cosf(a), sa = sinf(a);
            for (int i = 0; i < 3; i++) {
                const float e = r[i] * ca + f[i] * sa;
                Vb[j][i] = b[i] + e * o->r - u[i] * 0.3f; Vt[j][i] = b[i] + e * o->r * 0.55f + u[i] * o->h;
            }
        }
        float u0 = 0;
        for (int j = 0; j < n; j++) {
            const int j2 = (j + 1) % n;
            ag_pt_t q[4] = { { { Vb[j][0], Vb[j][1], Vb[j][2] }, u0, o->h * 8 }, { { Vb[j2][0], Vb[j2][1], Vb[j2][2] }, u0 + o->r * 6, o->h * 8 },
                             { { Vt[j2][0], Vt[j2][1], Vt[j2][2] }, u0 + o->r * 6, 0.01f }, { { Vt[j][0], Vt[j][1], Vt[j][2] }, u0, 0.01f } };
            u0 += o->r * 6;
            float nn[3];
            if (!ag_vue_face(q[0].p, q[1].p, q[3].p, (const float[3]){ q[0].p[0] * 2 - b[0], q[0].p[1] * 2 - b[1], q[0].p[2] * 2 - b[2] }, nn)) continue;
            ag_uv_base(q, 4);
            if (ag_poly(q, 4, ag_lum(nn), TILE_BIG + AG.tex[AGT_ROCHE], -AG_REC_MUR) == -2) return 0;
        }
        const float top[3] = { b[0] + u[0] * o->h, b[1] + u[1] * o->h, b[2] + u[2] * o->h };
        if ((c->x - top[0]) * u[0] + (c->y - top[1]) * u[1] + (c->z - top[2]) * u[2] > 0)
            for (int j = 1; j + 1 < n; j++) {
                ag_pt_t q[3] = { { { Vt[0][0], Vt[0][1], Vt[0][2] }, Vt[0][0] * 8, Vt[0][2] * 8 }, { { Vt[j][0], Vt[j][1], Vt[j][2] }, Vt[j][0] * 8, Vt[j][2] * 8 },
                                 { { Vt[j + 1][0], Vt[j + 1][1], Vt[j + 1][2] }, Vt[j + 1][0] * 8, Vt[j + 1][2] * 8 } };
                ag_uv_base(q, 3);
                if (ag_poly(q, 3, ag_lum(u), TILE_BIG + AG.tex[AGT_ROCHE], -AG_REC_MUR) == -2) return 0;
            }
    }
    return 1;
}
/* Pilier sous la section : pavé vertical (dans les axes du monde) du dessous du ruban jusqu'au sol */
static void ag_pilier(const ag_sect_t *S) {
    const float top = S->y - S->uy * AG_EP - 0.05f, bas = AG.sol, h = top - bas, p = AG_PILIER;
    if (h < 0.3f) return;
    static const float N[4][3] = { { 1, 0, 0 }, { 0, 0, 1 }, { -1, 0, 0 }, { 0, 0, -1 } };
    for (int f = 0; f < 4; f++) {
        const float nx = N[f][0], nz = N[f][2], ax = -nz, az = nx;   /* ax, az : le long de la face */
        const float cx = S->x + nx * p, cz = S->z + nz * p, pt[3] = { cx, top, cz };
        if (!ag_vue(pt, N[f])) continue;
        const ag_pt_t q[4] = { { { cx - ax * p, top, cz - az * p }, 0.01f, 0.01f }, { { cx + ax * p, top, cz + az * p }, 31.99f, 0.01f },
                               { { cx + ax * p, bas, cz + az * p }, 31.99f, h * 16 }, { { cx - ax * p, bas, cz - az * p }, 0.01f, h * 16 } };
        ag_poly(q, 4, ag_lum(N[f]), TILE_BIG + AG.tex[AGT_PILIER], 0);
    }
}
/* Portion naturelle (13/11/2026, demande de l'utilisateur : « des parties au milieu d'un environnement naturel », comme les courses de
   pods de la N64) : au lieu des murs, une paroi de roche de chaque côté (AG.tex[AGT_ROCHE]), penchée vers l'extérieur, de la hauteur
   de la section (paroi, irrégulière), un plateau au-dessus et une pente jusqu'au sol ; tunnel : une voûte à hauteur de la paroi (au
   plus AG_VOUTE), dessous dans l'ombre, dessus comme le plateau. Les murs de la physique restent au bord de la chaussée */
#define AG_PLATEAU 6.0f      /* largeur du plateau au-dessus d'une paroi */
#define AG_VOUTE 3.2f        /* hauteur de la voûte d'un tunnel au-dessus de la chaussée, au plus */
#define AG_TUNNEL_PENTE 0.6f /* flanc d'un tunnel : vers l'extérieur, AG_TUNNEL_PENTE × sa hauteur + 1 (raide : roche ; le flanc doux d'une paroi prenait
                                la texture du sol et se fondait dans le terrain) */
/* Portail d'un tunnel (14/11/2026, retour de l'utilisateur : « l'entrée des tunnels n'est toujours qu'un tube sans épaisseur ») :
   à l'entrée et à la sortie, un bloc qui avance de AG_PORTAIL_P hors de la coupe, déborde de l'ouverture de AG_PORTAIL_L de chaque
   côté et de AG_PORTAIL_H au-dessus de la voûte, posé jusqu'au sol : façade, côtés, dessus, et l'épaisseur de l'ouverture (ses
   deux tableaux et son plafond) */
#define AG_PORTAIL_P 1.5f
#define AG_PORTAIL_L 1.4f
#define AG_PORTAIL_H 2.4f
#define AG_TOIT 2.0f         /* épaisseur du toit d'un tunnel au moins (14/11/2026, retour de l'utilisateur : « les tunnels n'ont toujours pas
                                de volume » : le dessus était la voûte même, sans épaisseur) ; dessus à la hauteur de la paroi s'il est plus haut */
/* Face plane par trois de ses points, vue de face ? n : sa normale (unitaire), tournée vers le point vers (le côté d'où elle se voit) ;
   13/11/2026 : les parois penchées des portions naturelles étaient testées avec la perpendiculaire à la chaussée relevée, et
   disparaissaient quand la caméra passait au-dessus et près d'elles (retour de l'utilisateur : « quand on est proche, les textures
   disparaissent ») */
static int ag_vue_face(const float *a, const float *b, const float *c, const float *vers, float *n) {
    const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    cross3_r(e1, e2, n);
    float ln = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (ln < 1e-9f) return 0;
    if ((vers[0] - a[0]) * n[0] + (vers[1] - a[1]) * n[1] + (vers[2] - a[2]) * n[2] < 0) ln = -ln;
    for (int k = 0; k < 3; k++) n[k] /= ln;
    return ag_vue(a, n);
}
/* Coordonnées de texture d'une face ramenées près de zéro d'un même multiple de 64 pour tous ses points (ramenées point par point,
   deux points de part et d'autre d'un multiple de 64 repliaient la texture à travers toute la face) */
static void ag_uv_base(ag_pt_t *q, int n) {
    float u0 = 1e9f, v0 = 1e9f;
    for (int i = 0; i < n; i++) { u0 = fminf(u0, q[i].u); v0 = fminf(v0, q[i].v); }
    u0 = floorf(u0 / 64) * 64; v0 = floorf(v0 / 64) * 64;
    for (int i = 0; i < n; i++) { q[i].u -= u0 - 0.01f; q[i].v -= v0 - 0.01f; }
}
/* Bout d'une portion naturelle (13/11/2026, retour de l'utilisateur : « les environnements ne sont pas fermés à l'entrée et à la
   sortie ») : de chaque côté, la coupe fermée dans le plan de la section S, du sol sous le bord de la chaussée au pied de la paroi,
   à son haut, au bout du plateau et au pied de la pente ; vers sens × le devant de S (-1 : l'entrée, vue en arrivant ; 1 : la sortie) */
/* Portail (AG_PORTAIL_*) : chaque face dans le repère de la coupe (x vers la droite de la section, y le haut de la piste, z vers
   dehors), ses quatre coins, sa normale ; façade et dehors du bloc à la texture des piliers, épaisseur de l'ouverture à celle des
   parois du tunnel, dans l'ombre */
static int ag_portail(const ag_sect_t *S, float h, float sens) {
    const float c[3] = { S->x, S->y, S->z }, r[3] = { S->rx, S->ry, S->rz }, u[3] = { S->ux, S->uy, S->uz }, z[3] = { S->fx * sens, S->fy * sens, S->fz * sens };
    const float w = S->hw, X = w + AG_PORTAIL_L, D = AG_PORTAIL_P, H = h + AG_PORTAIL_H, B = -fmaxf(0.3f, c[1] - AG.sol);
    /* coins (x, y, z) et normale (x, y, z) de chaque face ; dedans : l'épaisseur de l'ouverture */
    static const int NF = 9;
    const float F[9][4][3] = {
        { { -X, B, D }, { -w, B, D }, { -w, H, D }, { -X, H, D } },     /* façade : montant gauche */
        { { w, B, D }, { X, B, D }, { X, H, D }, { w, H, D } },         /* montant droit */
        { { -w, h, D }, { w, h, D }, { w, H, D }, { -w, H, D } },       /* linteau */
        { { -X, B, 0 }, { -X, B, D }, { -X, H, D }, { -X, H, 0 } },     /* côté gauche */
        { { X, B, D }, { X, B, 0 }, { X, H, 0 }, { X, H, D } },         /* côté droit */
        { { -X, H, D }, { X, H, D }, { X, H, 0 }, { -X, H, 0 } },       /* dessus */
        { { -w, AG_MUR_H, 0 }, { -w, AG_MUR_H, D }, { -w, h, D }, { -w, h, 0 } },   /* tableau gauche (au-dessus du mur de la piste) */
        { { w, AG_MUR_H, D }, { w, AG_MUR_H, 0 }, { w, h, 0 }, { w, h, D } },     /* tableau droit */
        { { -w, h, 0 }, { w, h, 0 }, { w, h, D }, { -w, h, D } },       /* plafond de l'ouverture */
    };
    static const float N[9][3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, -1, 0 } };
    for (int k = 0; k < NF; k++) {
        const int dedans = k >= 6;
        float n[3], P[4][3];
        for (int i = 0; i < 3; i++) n[i] = r[i] * N[k][0] + u[i] * N[k][1] + z[i] * N[k][2];
        for (int v = 0; v < 4; v++) for (int i = 0; i < 3; i++) P[v][i] = c[i] + r[i] * F[k][v][0] + u[i] * F[k][v][1] + z[i] * F[k][v][2];
        if (!ag_vue(P[0], n)) continue;
        ag_pt_t q[4];
        for (int v = 0; v < 4; v++) {
            const float *L = F[k][v];
            q[v].p[0] = P[v][0]; q[v].p[1] = P[v][1]; q[v].p[2] = P[v][2];
            q[v].u = (N[k][2] != 0 || N[k][1] != 0 ? L[0] : L[2]) * 8; q[v].v = (N[k][1] != 0 ? L[2] : H - L[1]) * 8;
        }
        ag_uv_base(q, 4);
        if (ag_poly(q, 4, ag_lum(n) * (dedans ? 0.55f : 1), TILE_BIG + AG.tex[dedans ? AGT_ROCHE : AGT_PILIER], -AG_REC_MUR) == -2) return 0;
    }
    return 1;
}
static int ag_bout(const ag_sect_t *S, const float *L, const float *R, float h, float ht, float pen, float sens) {
    const float u[3] = { S->ux, S->uy, S->uz }, nf[3] = { S->fx * sens, S->fy * sens, S->fz * sens }, c[3] = { S->x, S->y, S->z };
    if (h <= 0.05f || !ag_vue(c, nf)) return 1;
    const float l = ag_lum(nf) * 0.85f, sol = AG.sol;
    for (int cote = 0; cote < 2; cote++) {
        const float *P = cote ? R : L, sg = cote ? 1.0f : -1.0f, o[3] = { S->rx * sg, S->ry * sg, S->rz * sg };
        float Pb[3], T[3], G[3], Bs[3];
        for (int k = 0; k < 3; k++) { Pb[k] = P[k] - u[k] * 0.3f; T[k] = P[k] + u[k] * ht + o[k] * ht * pen; }   /* (ht : le dessus) */
        if (T[1] <= sol) continue;
        const float ext = ht > h + 0.01f ? AG_TUNNEL_PENTE * (T[1] - sol) + 1 : AG_PLATEAU + T[1] - sol;   /* pied du flanc (ag_naturel) */
        for (int k = 0; k < 3; k++) G[k] = T[k] + o[k] * ext;
        G[1] = sol;
        Bs[0] = P[0]; Bs[1] = sol; Bs[2] = P[2];
        const float *V[4] = { Bs, Pb, T, G };
        ag_pt_t q[4];
        for (int i = 0; i < 4; i++) {   /* roche debout : en travers depuis le bord, vers le bas depuis le haut de la paroi */
            const float d[3] = { V[i][0] - P[0], V[i][1] - P[1], V[i][2] - P[2] };
            q[i].p[0] = V[i][0]; q[i].p[1] = V[i][1]; q[i].p[2] = V[i][2];
            q[i].u = fabsf(d[0] * o[0] + d[2] * o[2]) * 8 + 0.01f; q[i].v = (T[1] - V[i][1]) * 8 + 0.01f;
        }
        if (ag_poly(q, 4, l, TILE_BIG + AG.tex[AGT_ROCHE], -AG_REC_MUR) == -2) return 0;
    }
    if (ht > h + 0.01f && !ag_portail(S, h, sens)) return 0;   /* tunnel : son portail */
    return 1;
}
static int ag_naturel(const ag_sect_t *a, const ag_sect_t *b, const float *La, const float *Lb, const float *Ra, const float *Rb, float lon) {
    const float ua[3] = { a->ux, a->uy, a->uz }, ub[3] = { b->ux, b->uy, b->uz };
    /* tunnel d'après la section de départ du segment (jusqu'à la suivante, même hors du tunnel : la voûte va jusqu'au bout) ; la sortie
       garde la hauteur de la paroi (avant le 13/11/2026, elle retombait à zéro sur une case : un biseau ouvert) */
    const int tun = (a->drap & AGS_TUNNEL) != 0, sortie = !(b->drap & AGS_NATUREL), entree = !(AG.s[((int)(a - AG.s) - 1 + AG.n) % AG.n].drap & AGS_NATUREL);
    const float pa = a->paroi, pb = sortie ? a->paroi : b->paroi;
    const float ha = tun ? fminf(pa, AG_VOUTE) : pa, hb = tun ? fminf(pb, AG_VOUTE) : pb, pen = tun ? 0 : 0.28f;
    const float ta = tun ? fmaxf(pa, ha + AG_TOIT) : ha, tb = tun ? fmaxf(pb, hb + AG_TOIT) : hb;   /* hauteur du dessus (tunnel : toit épais) */
    if (entree && !ag_bout(a, La, Ra, ha, ta, pen, -1)) return 0;
    if (sortie && !ag_bout(b, Lb, Rb, hb, tb, pen, 1)) return 0;
    /* tunnel qui commence ou finit dans un canyon (la section voisine naturelle, sans voûte) : son portail seul, les parois du canyon
       continuent (14/11/2026 : sans bout reconnu, la voûte commençait dans le vide, « un tube sans épaisseur ») */
    if (tun && !entree && !(AG.s[((int)(a - AG.s) - 1 + AG.n) % AG.n].drap & AGS_TUNNEL) && !ag_portail(a, ha, -1)) return 0;
    if (tun && !sortie && !(b->drap & AGS_TUNNEL) && !ag_portail(b, hb, 1)) return 0;
    const float wa = fmodf(a->s * 8, 64), wb = wa + lon * 8;
    ag_pt_t q[4];
    float Ta[2][3], Tb[2][3], Ua[2][3], Ub[2][3];   /* hauts des deux parois, dessus de la masse (tunnel : au-dessus du toit) */
    for (int cote = 0; cote < 2; cote++) {
        const float *Pa = cote ? Ra : La, *Pb = cote ? Rb : Lb, sg = cote ? 1.0f : -1.0f;
        const float oa[3] = { a->rx * sg, a->ry * sg, a->rz * sg }, ob[3] = { b->rx * sg, b->ry * sg, b->rz * sg };   /* vers l'extérieur */
        for (int k = 0; k < 3; k++) { Ta[cote][k] = Pa[k] + ua[k] * ha + oa[k] * ha * pen; Tb[cote][k] = Pb[k] + ub[k] * hb + ob[k] * hb * pen; }
        for (int k = 0; k < 3; k++) { Ua[cote][k] = Pa[k] + ua[k] * ta + oa[k] * ta * pen; Ub[cote][k] = Pb[k] + ub[k] * tb + ob[k] * tb * pen; }
        float nin[3];
        const float route[3] = { a->x + ua[0], a->y + ua[1], a->z + ua[2] };
        ag_pt(&q[0], Pa, ua, -0.3f, NULL, 0, wa, ha * 8 + 2.4f); ag_pt(&q[1], Pb, ub, -0.3f, NULL, 0, wb, hb * 8 + 2.4f);
        ag_pt(&q[2], Tb[cote], NULL, 0, NULL, 0, wb, 0.01f); ag_pt(&q[3], Ta[cote], NULL, 0, NULL, 0, wa, 0.01f);
        if (ag_vue_face(q[0].p, q[1].p, q[3].p, route, nin))   /* face de la paroi, vers la chaussée */
            if (ag_poly(q, 4, ag_lum(nin) * (tun ? 0.55f : 1), TILE_BIG + AG.tex[AGT_ROCHE], -AG_REC_MUR) == -2) return 0;
        /* Flanc (14/11/2026 : à la place du plateau et de la pente, dessinés de près seulement) : du haut de la paroi jusqu'au sol,
           à AG_PLATEAU plus sa hauteur vers l'extérieur ; texturé comme le relief (ter_tri : roche si raide, sinon le sol du thème) */
        float Ga[3], Gb[3];
        const float exa = tun ? AG_TUNNEL_PENTE * (Ua[cote][1] - AG.sol) + 1 : AG_PLATEAU + Ua[cote][1] - AG.sol, exb = tun ? AG_TUNNEL_PENTE * (Ub[cote][1] - AG.sol) + 1 : AG_PLATEAU + Ub[cote][1] - AG.sol;
        for (int k = 0; k < 3; k++) { Ga[k] = Ua[cote][k] + oa[k] * exa; Gb[k] = Ub[cote][k] + ob[k] * exb; }
        Ga[1] = Gb[1] = AG.sol;
        if (Ua[cote][1] > AG.sol || Ub[cote][1] > AG.sol) {
            const float e1[3] = { Ub[cote][0] - Ua[cote][0], Ub[cote][1] - Ua[cote][1], Ub[cote][2] - Ua[cote][2] }, e2[3] = { Ga[0] - Ua[cote][0], Ga[1] - Ua[cote][1], Ga[2] - Ua[cote][2] };
            float nf[3];
            cross3_r(e1, e2, nf);
            const int ok = nf[1] >= 0 ? ter_tri(Ua[cote], Ub[cote], Ga) && ter_tri(Ub[cote], Gb, Ga) : ter_tri(Ua[cote], Ga, Ub[cote]) && ter_tri(Ub[cote], Ga, Gb);
            if (!ok) return 0;
        }
    }
    if (tun) {   /* voûte : dessous dans l'ombre ; dessus de la masse, au-dessus du toit, comme le relief */
        float bas[3], dessus[3];
        const float route[3] = { a->x, a->y, a->z }, ciel[3] = { Ua[0][0], Ua[0][1] + 50, Ua[0][2] };
        if (ag_vue_face(Ta[0], Ta[1], Tb[1], route, bas)) {
            ag_pt(&q[0], Ta[0], NULL, 0, NULL, 0, wa, 0.01f); ag_pt(&q[1], Ta[1], NULL, 0, NULL, 0, wa, 63.99f);
            ag_pt(&q[2], Tb[1], NULL, 0, NULL, 0, wb, 63.99f); ag_pt(&q[3], Tb[0], NULL, 0, NULL, 0, wb, 0.01f);
            if (ag_poly(q, 4, 3, TILE_BIG + AG.tex[AGT_ROCHE], -AG_REC_MUR) == -2) return 0;
        }
        if (ag_vue_face(Ua[0], Ua[1], Ub[1], ciel, dessus)) {
            ag_pt(&q[0], Ua[0], NULL, 0, NULL, 0, Ua[0][0] * 5.33f, Ua[0][2] * 5.33f); ag_pt(&q[1], Ua[1], NULL, 0, NULL, 0, Ua[1][0] * 5.33f, Ua[1][2] * 5.33f);
            ag_pt(&q[2], Ub[1], NULL, 0, NULL, 0, Ub[1][0] * 5.33f, Ub[1][2] * 5.33f); ag_pt(&q[3], Ub[0], NULL, 0, NULL, 0, Ub[0][0] * 5.33f, Ub[0][2] * 5.33f);
            ag_uv_base(q, 4);
            if (ag_poly(q, 4, ag_lum(dessus), TILE_BIG + AG.tex[AGT_SOL], -AG_REC_MUR) == -2) return 0;
        }
    }
    return 1;
}
/* Segment de piste de la section i à la section i + m : chaussée (ou dessous, vue d'en bas), murs (face intérieure, ou extérieure
   si elle est vue et que le segment est proche) ; 0 si le budget est épuisé */
static int ag_segment(int i, int m) {
    const ag_sect_t *a = &AG.s[i], *b = &AG.s[(i + m) % AG.n];
    float lon = 0;
    for (int k = 0; k < m; k++) lon += AG.s[(i + k) % AG.n].l;
    const float da = ag_onde_dep(i), db = ag_onde_dep((i + m) % AG.n);   /* piste soulevée par l'onde d'un SÉISME (antigrav.c) */
    const float ca[3] = { a->x + a->ux * da, a->y + a->uy * da, a->z + a->uz * da }, cb[3] = { b->x + b->ux * db, b->y + b->uy * db, b->z + b->uz * db };
    const float ra[3] = { a->rx, a->ry, a->rz }, rb[3] = { b->rx, b->ry, b->rz };
    const float ua[3] = { a->ux, a->uy, a->uz }, ub[3] = { b->ux, b->uy, b->uz }, nua[3] = { -a->ux, -a->uy, -a->uz };
    const float va = fmodf(a->s * AG_TEX_L, 64), vb = va + lon * AG_TEX_L, wa = fmodf(a->s * AG_TEX_L, 32), wb = wa + lon * AG_TEX_L;
    float La[3], Lb[3], Ra[3], Rb[3];
    for (int k = 0; k < 3; k++) { La[k] = ca[k] - ra[k] * a->hw; Lb[k] = cb[k] - rb[k] * b->hw; Ra[k] = ca[k] + ra[k] * a->hw; Rb[k] = cb[k] + rb[k] * b->hw; }
    const int nat = (a->drap & AGS_NATUREL) != 0;
    ag_pt_t q[4];
    if (ag_apercu) {   /* aperçu : la chaussée seule, élargie, à la couleur du néon B de son bord (pleine lumière), vue des deux côtés */
        const float k = 2.4f;
        for (int i = 0; i < 3; i++) { La[i] = ca[i] - ra[i] * a->hw * k; Lb[i] = cb[i] - rb[i] * b->hw * k; Ra[i] = ca[i] + ra[i] * a->hw * k; Rb[i] = cb[i] + rb[i] * b->hw * k; }
        ag_pt(&q[0], La, ua, 0.1f, NULL, 0, 61.5f, 1.5f); ag_pt(&q[1], Ra, ua, 0.1f, NULL, 0, 61.5f, 1.5f);
        ag_pt(&q[2], Rb, ub, 0.1f, NULL, 0, 61.5f, 1.5f); ag_pt(&q[3], Lb, ub, 0.1f, NULL, 0, 61.5f, 1.5f);
        return ag_poly(q, 4, 15, TILE_BIG + AG.tex[AGT_PISTE], 1e6f) != -2;   /* (texel du néon B du bord droit ; premier seau : devant tout) */
    }
    if (ag_vue(ca, ua)) {
        ag_pt(&q[0], La, ua, 0, NULL, 0, 0.01f, va); ag_pt(&q[1], Ra, ua, 0, NULL, 0, 63.99f, va);
        ag_pt(&q[2], Rb, ub, 0, NULL, 0, 63.99f, vb); ag_pt(&q[3], Lb, ub, 0, NULL, 0, 0.01f, vb);
        if (ag_poly(q, 4, fminf(15, ag_lum(ua) + (da + db) * 4), TILE_BIG + AG.tex[nat ? AGT_TERRE : (a->drap & AGS_DEPART) && m == 1 ? AGT_DEPART : AGT_PISTE], -AG_REC_CH) == -2) return 0;
    } else if (!nat) {   /* dessous du ruban */
        ag_pt(&q[0], La, ua, -AG_EP, NULL, 0, 0.01f, wa); ag_pt(&q[1], Ra, ua, -AG_EP, NULL, 0, 31.99f, wa);
        ag_pt(&q[2], Rb, ub, -AG_EP, NULL, 0, 31.99f, wb); ag_pt(&q[3], Lb, ub, -AG_EP, NULL, 0, 0.01f, wb);
        if (ag_poly(q, 4, ag_lum(nua), TILE_BIG + AG.tex[AGT_DESSOUS], -AG_REC_CH) == -2) return 0;
    }
    if (nat) return ag_naturel(a, b, La, Lb, Ra, Rb, lon);
    /* Murs verticaux (13/11/2026 : perpendiculaires à la chaussée relevée d'un virage, celui du bord haut penchait au-dessus d'elle et
       mangeait son néon en dents de scie, d'un segment à l'autre) ; face intérieure, ou extérieure vue de près */
    static const float HAUT[3] = { 0, 1, 0 };
    for (int cote = -1; cote <= 1; cote += 2) {
        const float *Pa = cote < 0 ? La : Ra, *Pb = cote < 0 ? Lb : Rb;
        float nin[3];
        for (int k = 0; k < 3; k++) nin[k] = -ra[k] * (float)cote;   /* intérieure : vers l'axe */
        nin[1] = 0;
        if (ag_vue(Pa, nin)) {
            ag_pt(&q[0], Pa, ua, 0, NULL, 0, va, 31.99f); ag_pt(&q[1], Pb, ub, 0, NULL, 0, vb, 31.99f);
            ag_pt(&q[2], Pb, HAUT, AG_MUR_H, NULL, 0, vb, 0.01f); ag_pt(&q[3], Pa, HAUT, AG_MUR_H, NULL, 0, va, 0.01f);
            if (ag_poly(q, 4, ag_lum(nin), TILE_BIG + AG.tex[AGT_MUR], -AG_REC_MUR) == -2) return 0;
        } else {
            const float nout[3] = { -nin[0], 0, -nin[2] };
            ag_pt(&q[0], Pa, HAUT, AG_MUR_H, NULL, 0, wa, 0.01f); ag_pt(&q[1], Pb, HAUT, AG_MUR_H, NULL, 0, wb, 0.01f);
            ag_pt(&q[2], Pb, ub, -AG_EP, NULL, 0, wb, (AG_MUR_H + AG_EP) * 32); ag_pt(&q[3], Pa, ua, -AG_EP, NULL, 0, wa, (AG_MUR_H + AG_EP) * 32);
            if (ag_poly(q, 4, ag_lum(nout), TILE_BIG + AG.tex[AGT_DESSOUS], -AG_REC_MUR) == -2) return 0;
        }
    }
    return 1;
}
static void ag_piste(void) {
    uint16_t *seg_i = ag_seg_i, *seg_ordre = ag_seg_ordre;
    uint8_t *seg_m = ag_seg_m, *seg_bin = ag_seg_bin;
    float *seg_d = ag_seg_d;
    const camera_t *c = &E.cam;
    const float R = fog_fullc + 4, fx = c->sy, fz = c->cy, Rb = fminf(R, far_z);
    /* Segments visibles, dans l'ordre de la piste : AG_SEG sections, calés sur ses multiples ; la ligne de départ seule (sa texture) */
    int ns = 0;
    for (int i = 0; i < AG.n;) {
        const ag_sect_t *a = &AG.s[i];
        const float dx = a->x - c->x, dy = a->y - c->y, dz = a->z - c->z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        int m = ag_apercu ? 8 : AG_SEG;
        while (m > 1 && i % m) m >>= 1;   /* calé sur un multiple de m */
        if (!ag_apercu) for (int k = 0; k < m; k++) if (AG.s[(i + k) % AG.n].drap & AGS_DEPART) { m = k ? k : 1; break; }
        if (!ag_apercu && m > 1) for (int k = 0; k < m; k++) if (ag_onde_zone((i + k) % AG.n)) { m = 1; break; }   /* sous une onde : section par section (bosse lisse) */
        if (!ag_apercu) {   /* coupé aux limites des portions naturelles et des tunnels (leurs bouts, ag_naturel) */
            const int f0 = AG.s[i].drap & (AGS_NATUREL | AGS_TUNNEL);
            for (int k = 1; k < m; k++) if ((AG.s[(i + k) % AG.n].drap & (AGS_NATUREL | AGS_TUNNEL)) != f0) { m = k; break; }
        }
        if (i + m > AG.n) m = AG.n - i;
        const ag_sect_t *b = &AG.s[(i + m) % AG.n];
        const float ex = b->x - c->x, ey = b->y - c->y, ez = b->z - c->z, db = sqrtf(ex * ex + ey * ey + ez * ez);
        int vu = !(AG_VIS_PARTS & 4) || !ag_vis_on;   /* (visibilité précalculée : une de ses sections au moins) */
        for (int k = 0; k <= m && !vu; k++) vu = ag_vsec_vu((i + k) % AG.n);
        if (vu && (d < R || db < R) && !(dx * fx + dz * fz < -3 && ex * fx + ez * fz < -3)) {
            seg_i[ns] = (uint16_t)i; seg_m[ns] = (uint8_t)m; seg_d[ns] = fminf(d, db);
            const int k = (int)(seg_d[ns] / Rb * AG_BINS);
            seg_bin[ns] = (uint8_t)(k < 0 ? 0 : k >= AG_BINS ? AG_BINS - 1 : k);
            ns++;
        }
        i += m;
    }
    /* Rangés du plus proche au plus loin (comptage par tranches de distance) */
    int cpt[AG_BINS + 1] = { 0 };
    for (int s = 0; s < ns; s++) cpt[seg_bin[s] + 1]++;
    for (int k = 0; k < AG_BINS; k++) cpt[k + 1] += cpt[k];
    for (int s = 0; s < ns; s++) seg_ordre[cpt[seg_bin[s]]++] = (uint16_t)s;
    ag_coupe = 1e9f;
    for (int o = 0; o < ns; o++) {
        const int s = seg_ordre[o];
        if (!ag_segment(seg_i[s], seg_m[s])) { ag_coupe = seg_d[s]; break; }
    }
    /* Plaques : posées sur la chaussée, avancées d'un seau */
    for (int k = 0; k < AG.npl; k++) {
        const ag_plaque_t *P = &AG.pl[k];
        const ag_sect_t *a = &AG.s[P->sec], *b = &AG.s[(P->sec + P->len) % AG.n];
        if (!ag_vsec_vu(P->sec) && !ag_vsec_vu((P->sec + P->len) % AG.n)) continue;
        const float dx = a->x - c->x, dz = a->z - c->z;
        if (dx * dx + dz * dz > fminf(R, ag_coupe) * fminf(R, ag_coupe) || (dx * fx + dz * fz < -3 && (b->x - c->x) * fx + (b->z - c->z) * fz < -3)) continue;
        const float ua[3] = { a->ux, a->uy, a->uz };
        if (!ag_vue((const float[3]){ a->x, a->y, a->z }, ua)) continue;
        ag_pt_t q[4];
        for (int e = 0; e < 2; e++) {
            const ag_sect_t *S = e ? b : a;
            const float dp = 0.02f + ag_onde_dep(e ? (P->sec + P->len) % AG.n : P->sec);
            const float cc[3] = { S->x + S->ux * dp, S->y + S->uy * dp, S->z + S->uz * dp }, rr[3] = { S->rx, S->ry, S->rz };
            ag_pt(&q[e ? 3 : 0], cc, rr, P->d - P->hw, NULL, 0, 0.01f, e ? 63.99f : 0.01f);
            ag_pt(&q[e ? 2 : 1], cc, rr, P->d + P->hw, NULL, 0, 63.99f, e ? 63.99f : 0.01f);
        }
        /* plaque d'arme au repos (AG_PLAQUE_REPOS) : éteinte, puis rallumée en clignotant la dernière seconde */
        const float rep = P->sorte ? P->repos : 0, lum = rep <= 0 ? 15 : rep < 1 && fmodf(rep, 0.25f) < 0.125f ? 15 : 3;
        ag_poly(q, 4, lum, TILE_BIG + AG.tex[P->sorte ? AGT_ARME : AGT_VITESSE], AG_AV_PLAQUE);
    }
    /* Piliers (sous les segments soumis : en deçà de la coupure) ; retirés le 14/11/2026 (demande de l'utilisateur : « supprimer les
       piliers sous la route au profit d'une meilleure distance d'affichage des décors ») : AG_PILIERS à 1 pour les remettre */
#ifndef AG_PILIERS
#define AG_PILIERS 0
#endif
    if (!AG_PILIERS) return;
    const float RP = fminf(R, ag_coupe);
    for (int o = 0; o < ns; o++) {
        const int s = seg_ordre[o];
        if (seg_d[s] > RP) break;
        for (int k = 0; k < seg_m[s]; k++) { const ag_sect_t *S = &AG.s[(seg_i[s] + k) % AG.n]; if ((S->drap & AGS_PILIER) && !(S->drap & AGS_NATUREL) && AG.sol < S->y) ag_pilier(S); }
    }
}
/* Engins, du plus proche au plus loin : maillage tourné selon le cap, le haut de la piste et le roulis, niveau de détail selon la
   distance (le plus simple quand les engins ont pris AG_TRI_ENGINS) ; ombre sur la chaussée ; flamme des réacteurs en poussée */
/* Blanc de la palette commune (cœur de la flamme des réacteurs) : cherché une fois */
static uint8_t ag_blanc_idx(void) {
    static int ok;
    static uint8_t b = 1;
    if (!ok) {
        int bd = 1 << 30;
        for (int i = 1; i < AG_PAL_COMMUN; i++) { const int dr = E.w.pal[i][0] - 250, dg = E.w.pal[i][1] - 250, db = E.w.pal[i][2] - 255, d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; b = (uint8_t)i; } }
        ok = 1;
    }
    return b;
}
static void ag_engins(void) {
    const camera_t *c = &E.cam;
    v3 *vv = ag_vv;
    float (*vw)[3] = ag_vw;
    int ord[AG_NAV_MAX] = { 0 }, no = 0;
    float dist[AG_NAV_MAX] = { 0 };
    for (int k = 0; k < AG.nn; k++) {
        if (ag_ck_on && k == AG.suivi) continue;   /* vue cockpit : on est dedans */
        const ag_nav_t *n = &AG.nav[k];
        if (n->detruit) continue;
        const float dx = n->x - c->x, dy = n->y - c->y, dz = n->z - c->z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > fog_fullc || dx * c->sy + dz * c->cy < -2) continue;
        int j = no++;
        while (j > 0 && dist[j - 1] > d) { ord[j] = ord[j - 1]; dist[j] = dist[j - 1]; j--; }
        ord[j] = k; dist[j] = d;
    }
    for (int o = 0; o < no; o++) {
        const ag_nav_t *n = &AG.nav[ord[o]];
        const ag_ecurie_t *C = &AG.ec[n->ecurie];
        const int var = n->variante && C->modele2 >= 0, livree_k = var ? C->livree2 : C->livree;   /* (second engin de l'écurie : AGE2) */
        const ag_modele_t *M = &AG.mod[var ? C->modele2 : C->modele];
        int l = AG.phase == AGP_ECURIE || AG.phase == AGP_GARAGE ? 0 : ord[o] == AG.suivi ? AG_LOD_SUIVI : AG_LOD_ADV;
        if (l > M->nlod - 1) l = M->nlod - 1;
        if (ag_tris + M->lod[l].nf > AG_TRI_ENGINS) l = M->nlod - 1;
        const ag_lod_t *L = &M->lod[l];
        if (ag_tris + L->nf > AG_TRI_ENGINS && o > 0) break;   /* (le plus proche toujours) */
        float h[3] = { n->hx, n->hy, n->hz }, u[3] = { n->ux, n->uy, n->uz }, s[3];
        cross3_r(u, h, s);
        const float ro = -n->roulis + 0.08f * n->choc * sinf(E.clock * 40), cr = cosf(ro), sr = sinf(ro);
        float u2[3], s2[3];
        for (int i = 0; i < 3; i++) { u2[i] = u[i] * cr + s[i] * sr; s2[i] = s[i] * cr - u[i] * sr; }
        /* Ombre : ellipse translucide sur la chaussée sous l'engin (draw_shadow : la chaussée peinte ensuite y est assombrie),
           dans un seau entre l'engin et la chaussée ; ses rayons : l'engin projeté (demi-largeur, demi-longueur). Retirée le
           14/11/2026 (demande de l'utilisateur : « supprime l'ombre des vaisseaux ») : AG_OMBRE à 1 pour la remettre */
#ifndef AG_OMBRE
#define AG_OMBRE 0
#endif
        if (AG_OMBRE) {
            float pc[3], r[3], un[3];
            ag_cadre(n->sec, n->t, pc, r, un, NULL);
            const float og[3] = { pc[0] + r[0] * n->d, pc[1] + r[1] * n->d, pc[2] + r[2] * n->d };
            const v3 a = to_view(og[0], og[1], og[2]), av = to_view(og[0] + h[0] * 0.6f, og[1] + h[1] * 0.6f, og[2] + h[2] * 0.6f),
                     co = to_view(og[0] + s[0] * 0.45f, og[1] + s[1] * 0.45f, og[2] + s[2] * 0.45f);
            if (a.z > NEAR_Z + 0.3f && av.z > NEAR_Z && co.z > NEAR_Z && ag_vue(og, un)) {
                const float sx = pcx + a.x * FOCAL / a.z, sy = pcy - a.y * FOCAL / a.z;
                const float rx = hypotf(pcx + co.x * FOCAL / co.z - sx, pcy - co.y * FOCAL / co.z - sy), ry = fmaxf(1.5f, fabsf(pcy - av.y * FOCAL / av.z - sy));
                prim_t *sh = new_prim(bucket_of(a.z - 0.2f));
                if (sh) { sh->kind = K_SHADOW; sh->sx = sx; sh->sy = sy; sh->s = rx; sh->ry = ry; sh->miny = (int16_t)floorf(sy - ry); sh->maxy = (int16_t)ceilf(sy + ry + 1); }
            }
        }
        for (int i = 0; i < L->nv; i++) {
            const uint8_t *p = L->v + (size_t)i * 12;
            const float mx = ag_rdf(p), my = ag_rdf(p + 4), mz = ag_rdf(p + 8);
            for (int a = 0; a < 3; a++) vw[i][a] = (a == 0 ? n->x : a == 1 ? n->y : n->z) + s2[a] * mx + u2[a] * my + h[a] * mz;
            vv[i] = to_view(vw[i][0], vw[i][1], vw[i][2]);
        }
        for (int f = 0; f < L->nf; f++) {
            const uint8_t *fa = L->f + (size_t)f * 20;
            const int nv = fa[0] == 4 ? 4 : 3;
            int fv[4];
            for (int i = 0; i < nv; i++) { const int v = fa[4 + 2 * i] | fa[5 + 2 * i] << 8; fv[i] = v < L->nv ? v : 0; }
            const float *a = vw[fv[0]], *b = vw[fv[1]], *d = vw[fv[2]];
            const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
            float nn[3];
            cross3_r(e1, e2, nn);
            if ((c->x - a[0]) * nn[0] + (c->y - a[1]) * nn[1] + (c->z - a[2]) * nn[2] <= 0) continue;   /* face de dos */
            const float ln = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            if (ln < 1e-9f) continue;
            for (int i = 0; i < 3; i++) nn[i] /= ln;
            /* Livrée de l'écurie (drapeau 1 : texture DECO, coordonnées de la face), sinon couleur unie : place de l'écurie, ou couleur
               fixe du modèle (place 255), texel de la tuile 0 */
            const int livree = (fa[3] & 1) && (uint32_t)livree_k < E.w.deco_n;
            const uint8_t col = fa[1] < AGC_N ? C->c[fa[1]] : fa[2];
            const float cu = (float)(col & 15) + 0.5f, cv = (float)(col >> 4) + 0.5f, lu = ag_lum(nn);
            vv_t q[4];
            float zs = 0;
            for (int i = 0; i < nv; i++) {
                const v3 ov = vv[fv[i]];
                q[i].x = ov.x; q[i].y = ov.y; q[i].z = ov.z; q[i].l = lu; zs += ov.z;
                if (livree) { q[i].u = (float)fa[12 + 2 * i] + 0.5f; q[i].v = (float)fa[13 + 2 * i] + 0.5f; } else { q[i].u = cu; q[i].v = cv; }
            }
            ag_soumettre(q, nv, livree ? TILE_BIG + livree_k : 0, bucket_of(zs / nv - AG_AV_ENGIN));
        }
        /* Flamme : losange de la couleur du réacteur (pleine lumière) derrière l'engin, qui vacille, plus grande en accélération */
        if (n->poussee > 0.05f) {
            const float t = (0.07f + 0.05f * (n->boost > 0)) * n->poussee * (0.85f + 0.15f * sinf(E.clock * 47 + (float)ord[o] * 2.1f));
            const float zf = -0.74f, yf = 0.12f;   /* juste derrière la coque (l'engin va de -0,7 à 0,7) */
            const float P[4][3] = { { 0, yf + t, zf }, { t, yf, zf }, { 0, yf - t, zf }, { -t, yf, zf } };
            for (int coeur = 0; coeur < 2; coeur++) {   /* (néon, 14/11/2026 : un cœur blanc, plus petit et juste devant, dans la couleur du réacteur) */
                const uint8_t col = coeur ? ag_blanc_idx() : C->c[AGC_REACTEUR];
                const float kt = coeur ? 0.45f : 1, dz = coeur ? -0.03f : 0;
                vv_t q[4];
                float zs = 0;
                for (int i = 0; i < 4; i++) {
                    const float px = P[i][0] * kt, py = yf + (P[i][1] - yf) * kt, pz = P[i][2] + dz;
                    const v3 ov = to_view(n->x + s2[0] * px + u2[0] * py + h[0] * pz, n->y + s2[1] * px + u2[1] * py + h[1] * pz, n->z + s2[2] * px + u2[2] * py + h[2] * pz);
                    q[i].x = ov.x; q[i].y = ov.y; q[i].z = ov.z; q[i].u = (float)(col & 15) + 0.5f; q[i].v = (float)(col >> 4) + 0.5f; q[i].l = 15; zs += ov.z;
                }
                ag_soumettre(q, 4, 0, bucket_of(zs / 4 - 0.5f - 0.05f * (float)coeur));
            }
        }
        /* Traînée (13/11/2026, demande de l'utilisateur) : ruban du réacteur par les points enregistrés (antigrav.c, trace_maj), tourné
           vers la caméra, qui s'amincit et s'efface vers le bout : faces translucides (fondu de fondu_px, opacité (16 - fade) / 16),
           couleur du réacteur en pleine lumière */
        if (n->trace_n >= 1 && n->vitesse > 3) {
            float P[AG_TRACE_N + 1][3], Sd[AG_TRACE_N + 1][3];
            int np = 0;
            for (int i = 0; i < 3; i++) P[0][i] = (i == 0 ? n->x : i == 1 ? n->y : n->z) - h[i] * 0.74f + u2[i] * 0.12f;
            np = 1;
            float lg = 0;
            const float lmax = n == &AG.nav[AG.suivi] ? AG_TRAINEE_L * 0.5f : AG_TRAINEE_L;   /* (l'engin suivi : la sienne va vers la caméra) */
            for (int k = 0; k < n->trace_n && lg < lmax; k++) {   /* jusqu'à AG_TRAINEE_L du réacteur : le dernier point raccourci */
                const float *t = n->trace[(n->trace_i - 1 - k + 2 * AG_TRACE_N) % AG_TRACE_N], *a = P[np - 1];
                const float d[3] = { t[0] - a[0], t[1] - a[1], t[2] - a[2] }, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                const float k2 = l > 1e-4f && lg + l > lmax ? (lmax - lg) / l : 1;
                for (int i = 0; i < 3; i++) P[np][i] = a[i] + d[i] * k2;
                lg += l * k2; np++;
            }
            for (int k = 0; k < np; k++) {   /* côté de chaque point : travers du ruban, vu de la caméra */
                const float *a = P[k > 0 ? k - 1 : 0], *b = P[k + 1 < np ? k + 1 : np - 1];
                const float dir[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, vue[3] = { P[k][0] - c->x, P[k][1] - c->y, P[k][2] - c->z };
                cross3_r(dir, vue, Sd[k]);
                const float ls = sqrtf(Sd[k][0] * Sd[k][0] + Sd[k][1] * Sd[k][1] + Sd[k][2] * Sd[k][2]);
                for (int i = 0; i < 3; i++) Sd[k][i] = ls > 1e-6f ? Sd[k][i] / ls : 0;
            }
            const uint8_t col = C->c[AGC_REACTEUR];
            const float cu = (float)(col & 15) + 0.5f, cv = (float)(col >> 4) + 0.5f, w0 = AG_TRAINEE_W * (0.7f + 0.5f * n->poussee + 0.4f * (n->boost > 0)) * (n == &AG.nav[AG.suivi] ? 0.7f : 1.0f);
            for (int k = 0; k + 1 < np; k++) {
                const float wa = w0 * (1 - (float)k / (float)(np - 1)), wb = w0 * (1 - (float)(k + 1) / (float)(np - 1)) + 0.01f;
                vv_t q[4];
                float zs = 0;
                for (int i = 0; i < 4; i++) {
                    const int e = i == 1 || i == 2;
                    const float *p = P[k + e], *sd = Sd[k + e], w = (e ? wb : wa) * (i == 0 || i == 1 ? -1.0f : 1.0f);
                    const v3 ov = to_view(p[0] + sd[0] * w, p[1] + sd[1] * w, p[2] + sd[2] * w);
                    q[i].x = ov.x; q[i].y = ov.y; q[i].z = ov.z; q[i].u = cu; q[i].v = cv; q[i].l = 15; zs += ov.z;
                }
                const int np0 = B->nprims;
                if (ag_soumettre(q, 4, 0, bucket_of(zs / 4 - 0.3f)) >= 0 && B->nprims > np0) B->prims[np0].fade = (uint8_t)(6 + (k * 9) / (np - 1));
            }
        }
    }
}
/* Armes (antigrav.c) : missiles, mines, boucliers, traits du rayon (et sa charge), éclats et boules de feu des explosions ; formes
   faites ici, faces de couleur unie (palette commune, couleurs les plus proches choisies une fois), après les engins dans le budget */
static uint8_t ag_cols[8]; static int ag_cols_ok;
enum { AGK_ROUGE, AGK_BLANC, AGK_ORANGE, AGK_JAUNE, AGK_CYAN, AGK_SOMBRE, AGK_MAGENTA, AGK_GRIS };
static void ag_cols_faire(void) {
    static const uint8_t RVB[8][3] = { { 230, 40, 40 }, { 245, 245, 250 }, { 255, 140, 30 }, { 255, 225, 90 }, { 90, 235, 255 }, { 40, 40, 52 }, { 255, 80, 215 }, { 150, 155, 170 } };
    for (int k = 0; k < 8; k++) {
        int best = 1, bd = 1 << 30;
        for (int i = 1; i < AG_PAL_COMMUN; i++) { const int dr = E.w.pal[i][0] - RVB[k][0], dg = E.w.pal[i][1] - RVB[k][1], db = E.w.pal[i][2] - RVB[k][2], d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = i; } }
        ag_cols[k] = (uint8_t)best;
    }
    ag_cols_ok = 1;
}
/* Face de couleur unie (palette) de n points du monde, lumière l, opacité (16 - fade) / 16 ; 0 si le budget est épuisé */
static int ag_face(const float (*P)[3], int n, uint8_t col, float l, int fade) {
    ag_pt_t q[4];
    for (int i = 0; i < n; i++) { q[i].p[0] = P[i][0]; q[i].p[1] = P[i][1]; q[i].p[2] = P[i][2]; q[i].u = (float)(col & 15) + 0.5f; q[i].v = (float)(col >> 4) + 0.5f; }
    const int p0 = B->nprims, r = ag_poly(q, n, l, 0, 0.5f);
    if (fade) for (int k = p0; k < B->nprims; k++) B->prims[k].fade = (uint8_t)fade;
    return r != -2;
}
/* Octaèdre (mines, charge du rayon) : centre, demi-tailles le long de a (axe), b et c ; faces vues seulement */
static int ag_octaedre(const float *o, const float *a, const float *b, const float *c, float ra, float rb, uint8_t col, float lum, int fade) {
    const camera_t *cm = &E.cam;
    float V[6][3];
    for (int i = 0; i < 3; i++) { V[0][i] = o[i] + a[i] * ra; V[1][i] = o[i] - a[i] * ra; V[2][i] = o[i] + b[i] * rb; V[3][i] = o[i] - b[i] * rb; V[4][i] = o[i] + c[i] * rb; V[5][i] = o[i] - c[i] * rb; }
    static const int F[8][3] = { { 0, 2, 4 }, { 0, 4, 3 }, { 0, 3, 5 }, { 0, 5, 2 }, { 1, 4, 2 }, { 1, 3, 4 }, { 1, 5, 3 }, { 1, 2, 5 } };
    for (int f = 0; f < 8; f++) {
        const float *p0 = V[F[f][0]], *p1 = V[F[f][1]], *p2 = V[F[f][2]];
        const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        float nn[3];
        cross3_r(e1, e2, nn);
        const float vers = (p0[0] - o[0]) * nn[0] + (p0[1] - o[1]) * nn[1] + (p0[2] - o[2]) * nn[2];
        if (vers < 0) for (int i = 0; i < 3; i++) nn[i] = -nn[i];   /* (normale vers l'extérieur) */
        if ((cm->x - p0[0]) * nn[0] + (cm->y - p0[1]) * nn[1] + (cm->z - p0[2]) * nn[2] <= 0) continue;
        const float ln = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        if (ln < 1e-9f) continue;
        for (int i = 0; i < 3; i++) nn[i] /= ln;
        const float P[3][3] = { { p0[0], p0[1], p0[2] }, { p1[0], p1[1], p1[2] }, { p2[0], p2[1], p2[2] } };
        if (!ag_face(P, 3, col, lum > 0 ? lum : ag_lum(nn), fade)) return 0;
    }
    return 1;
}
static void ag_armes(void) {
    const camera_t *c = &E.cam;
    if (!ag_cols_ok) ag_cols_faire();
    const float R = fminf(fog_fullc, 60);
    #define AG_LOIN(PX, PY, PZ) (((PX) - c->x) * ((PX) - c->x) + ((PY) - c->y) * ((PY) - c->y) + ((PZ) - c->z) * ((PZ) - c->z) > R * R || ((PX) - c->x) * c->sy + ((PZ) - c->z) * c->cy < -2)
    /* missiles : corps effilé blanc, nez rouge, flamme orange */
    for (int i = 0; i < AG_PROJ_MAX; i++) {
        const ag_proj_t *p = &AG.proj[i];
        if (!p->on || AG_LOIN(p->x, p->y, p->z)) continue;
        const float o[3] = { p->x, p->y, p->z }, h[3] = { p->hx, p->hy, p->hz }, u[3] = { p->ux, p->uy, p->uz };
        float s[3];
        cross3_r(u, h, s);
        const float nez[3] = { o[0] + h[0] * 0.25f, o[1] + h[1] * 0.25f, o[2] + h[2] * 0.25f };
        if (!ag_octaedre(o, h, s, u, 0.45f, 0.1f, ag_cols[AGK_BLANC], 0, 0)) return;
        if (!ag_octaedre(nez, h, s, u, 0.22f, 0.07f, ag_cols[AGK_ROUGE], 0, 0)) return;
        const float fl = 0.18f + 0.06f * sinf(E.clock * 60 + (float)i), qu[3] = { o[0] - h[0] * 0.5f, o[1] - h[1] * 0.5f, o[2] - h[2] * 0.5f };
        if (!ag_octaedre(qu, h, s, u, fl, 0.08f, ag_cols[AGK_ORANGE], 15, 4)) return;
    }
    /* mines : octaèdres sombres posés sur la chaussée, qui clignotent en rouge */
    for (int i = 0; i < AG_MINES_MAX; i++) {
        const ag_mine_t *m = &AG.mines[i];
        if (!m->on || AG_LOIN(m->x, m->y, m->z)) continue;
        const float o[3] = { m->x, m->y, m->z }, u[3] = { m->ux, m->uy, m->uz }, a[3] = { 1, 0, 0 };
        float b[3], d[3];
        cross3_r(u, a, b);
        { const float l = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); for (int k = 0; k < 3; k++) b[k] = l > 1e-6f ? b[k] / l : 0; }
        cross3_r(b, u, d);
        const int allume = fmodf(E.clock * 2.5f + (float)i * 0.37f, 1) < 0.35f;
        if (!ag_octaedre(o, u, b, d, 0.16f, 0.24f, ag_cols[allume ? AGK_ROUGE : AGK_SOMBRE], allume ? 15 : 0, 0)) return;
    }
    /* engins : bouclier (bulle translucide), trait du rayon, charge du rayon */
    for (int k = 0; k < AG.nn; k++) {
        const ag_nav_t *n = &AG.nav[k];
        if (n->detruit) continue;
        const float o[3] = { n->x, n->y, n->z }, h[3] = { n->hx, n->hy, n->hz }, u[3] = { n->ux, n->uy, n->uz };
        float s[3];
        cross3_r(u, h, s);
        if (n->bouclier > 0 && !(ag_ck_on && k == AG.suivi) && !AG_LOIN(n->x, n->y, n->z)) {
            const int fade = 12 + (n->bouclier < 1 && fmodf(E.clock * 8, 1) < 0.5f ? 2 : 0);
            float u2[3] = { u[0] * 0.55f, u[1] * 0.55f, u[2] * 0.55f };   /* (plus plat : demi-hauteur 0,4) */
            if (!ag_octaedre(o, h, s, u2, 0.95f, 0.72f, ag_cols[AGK_CYAN], 14, fade)) return;
        }
        if (n->charge > 0) {   /* charge du rayon : une boule qui grossit devant le nez */
            const float f = fminf(1, n->charge / AG_RAYON_CHARGE), r = 0.08f + 0.18f * f, p[3] = { o[0] + h[0] * 0.95f, o[1] + h[1] * 0.95f, o[2] + h[2] * 0.95f };
            if (!ag_octaedre(p, h, s, u, r, r, ag_cols[f >= 1 ? AGK_MAGENTA : AGK_CYAN], 15, 3)) return;
        }
        if (n->rayon_t > 0) {   /* trait : deux rubans croisés du nez au bout, qui s'effacent */
            const float a = fminf(1, fmaxf(0, n->rayon_t / 0.4f)), w = (0.06f + 0.1f * n->rayon_f) * (0.5f + 0.5f * a);
            const float p0[3] = { o[0] + h[0] * 0.9f, o[1] + h[1] * 0.9f, o[2] + h[2] * 0.9f }, *p1 = n->rayon_fin;
            const int fade = (int)(14 - 12 * a);
            for (int cr = 0; cr < 2; cr++) {
                const float *e = cr ? u : s;
                const float P[4][3] = { { p0[0] - e[0] * w, p0[1] - e[1] * w, p0[2] - e[2] * w }, { p1[0] - e[0] * w, p1[1] - e[1] * w, p1[2] - e[2] * w },
                                        { p1[0] + e[0] * w, p1[1] + e[1] * w, p1[2] + e[2] * w }, { p0[0] + e[0] * w, p0[1] + e[1] * w, p0[2] + e[2] * w } };
                if (!ag_face(P, 4, ag_cols[n->rayon_f >= 1 ? AGK_MAGENTA : AGK_CYAN], 15, fade)) return;
            }
        }
    }
    /* boules de feu : octaèdres translucides qui grandissent et s'effacent, orange puis jaune au cœur */
    for (int i = 0; i < AG_BOULES_MAX; i++) {
        const ag_boule_t *b = &AG.boules[i];
        if (b->vie <= 0 || AG_LOIN(b->x, b->y, b->z)) continue;
        const float f = b->t / b->vie, r = b->taille * (0.35f + 0.9f * f), o[3] = { b->x, b->y, b->z };
        static const float X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 }, Z[3] = { 0, 0, 1 };
        if (!ag_octaedre(o, Y, X, Z, r, r, ag_cols[AGK_ORANGE], 15, (int)(3 + 11 * f))) return;
        if (f < 0.6f && !ag_octaedre(o, Y, Z, X, r * 0.55f, r * 0.55f, ag_cols[AGK_JAUNE], 15, (int)(2 + 10 * f))) return;
    }
    /* éclats : triangles qui tournent */
    for (int i = 0; i < AG_ECLATS_MAX; i++) {
        const ag_eclat_t *e = &AG.eclats[i];
        if (e->vie <= 0 || AG_LOIN(e->x, e->y, e->z)) continue;
        const float t = 0.22f, ca = cosf(e->a), sa = sinf(e->a);
        const float P[3][3] = { { e->x + ca * t, e->y + sa * t, e->z }, { e->x - ca * t * 0.5f, e->y + sa * t, e->z + t * 0.8f }, { e->x, e->y - t * 0.6f, e->z - ca * t } };
        if (!ag_face(P, 3, e->col, 12, e->t > e->vie * 0.7f ? 6 : 0)) return;
    }
    #undef AG_LOIN
}
static void ag_image(float time_s) {
    E.time_s = time_s;
    float tx, ty, tz, yaw, pitch, dist, vue = AG_VUE * VUE_K;
    ag_apercu = AG.phase == AGP_PISTE;
    ag_ck_on = !ag_apercu && ag_cockpit();
    if (ag_apercu) {   /* le circuit entier vu de haut, qui tourne : centre et taille de la piste, distance qui le fait tenir */
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, ym = 0;
        for (int i = 0; i < AG.n; i++) { const ag_sect_t *S = &AG.s[i]; x0 = fminf(x0, S->x); x1 = fmaxf(x1, S->x); z0 = fminf(z0, S->z); z1 = fmaxf(z1, S->z); ym += S->y; }
        const float taille = fmaxf(x1 - x0, z1 - z0) * 1.15f + 8;
        tx = (x0 + x1) / 2; tz = (z0 + z1) / 2; ty = ym / (float)(AG.n > 0 ? AG.n : 1);
        if (ap_piste != AG.piste) { ap_piste = AG.piste; ap_d = 0; }
        yaw = E.clock * 0.22f; pitch = 0.95f;
        dist = ap_d > 0 ? ap_d : taille * 1.6f;
        /* emprise de l'axe à l'écran à cette distance : la largeur (0,92 de l'image) et la hauteur (la moitié : entre le titre et la fiche)
           donnent la distance ; son milieu, le centre de projection */
        setup_camera(tx, ty, tz, yaw, pitch, dist);
        float mx = 0, y0 = 1e9f, y1 = -1e9f;
        for (int i = 0; i < AG.n; i += AG.n / 48 + 1) {
            const v3 o = to_view(AG.s[i].x, AG.s[i].y, AG.s[i].z);
            if (o.z < 1) continue;
            const float sx = o.x * FOCAL / o.z, sy = -o.y * FOCAL / o.z;
            mx = fmaxf(mx, fabsf(sx)); y0 = fminf(y0, sy); y1 = fmaxf(y1, sy);
        }
        if (y1 > y0) {
            const float k = fminf(2.0f, fmaxf(0.5f, fmaxf(mx / (0.44f * RW), (y1 - y0) / (0.46f * RH)))), dv = dist * k, yv = -(y0 + y1) / 2 / k;
            const int prem = ap_d <= 0;
            ap_d = prem ? dv : ap_d + (dv - ap_d) * 0.15f; ap_y = prem ? yv : ap_y + (yv - ap_y) * 0.15f;
        }
        dist = ap_d;
        vue = dist + taille;
    } else ag_camera(&tx, &ty, &tz, &yaw, &pitch, &dist);
    if (E.fog_on && !ag_apercu) vue = fminf(vue, AG_BRUME_VUE * VUE_K);   /* brume (carrière) : la vue raccourcie, un brouillard dès 30 % */
    fog_fullc = vue; fog_z0c = vue * (E.fog_on ? 0.3f : 0.6f); fog_kc = 8.0f / (fog_fullc - fog_z0c);   /* (E.fog_on coupé : la distance de vue, le fond des trous) */
    far_z = vue + 16;
    setup_camera(tx, ty, tz, yaw, pitch, dist);
    pcx = CXF; pcy = ag_apercu ? RH * 0.45f + ap_y : CYF; dlg_glisse = 1;   /* (aperçu : entre le titre et la fiche) */
    float sdx = 0, sdy = 0;
    if (AG.secousse > 0) { const float a = AG.secousse * 4 / (float)ENG_SCALE; sdx = (rng_next() * 2 - 1) * a; sdy = (rng_next() * 2 - 1) * a; pcx += sdx; pcy += sdy; }
#if ENG_ROULIS
    cam_roule = ag_ck_on && fabsf(AG.cam_roll) > 1e-3f; cam_rc = cosf(AG.cam_roll); cam_rs = sinf(AG.cam_roll);
    B->roule = cam_roule; B->roul_c = cam_rc; B->roul_s = cam_rs; B->roul_x = pcx; B->roul_y = pcy;
#endif
    B->ck_px = NULL; ag_ecr_ok = 0;
    if (ag_spans_ck) { for (int y = 0; y < RH; y++) { span_l[y] = 0; span_r[y] = (int16_t)(RW - 1); } ag_spans_ck = 0; }
    if (ag_ck_on) {
        cam_dist_cur = AG_CK_NET;
        const int e = AG.nav[AG.suivi].ecurie;
        if (e >= 0 && e < AG_ECURIES_MAX && AG.nck[e] > 0) {
            const ag_image_t *ck = &AG.ck[e][AG.nck[e] - 1];
            for (int t = 0; t < AG.nck[e]; t++) if (AG.ck[e][t].h >= RH) { ck = &AG.ck[e][t]; break; }
            if (ck != ag_ck_im) {
                ag_ck_im = ck; ag_ck_pal[0] = 0;
                for (int i = 0; i < 255; i++) ag_ck_pal[i + 1] = i < ck->ncoul ? pack565(ck->rgb[i * 3], ck->rgb[i * 3 + 1], ck->rgb[i * 3 + 2]) : 0;
                for (int j = 0; j < ck->h && j < AG_CK_HMAX; j++) {
                    const uint8_t *row = ck->px + (size_t)j * (size_t)ck->w;
                    int l = 0, r = ck->w - 1;
                    while (l < ck->w && row[l]) l++;
                    while (r >= 0 && row[r]) r--;
                    ag_ck_ol[j] = (int16_t)l; ag_ck_or[j] = (int16_t)r;
                }
            }
            const float s = fmaxf((float)RW / (float)ck->w, (float)RH / (float)ck->h), x0 = ((float)RW - (float)ck->w * s) / 2 + sdx;
            int16_t *u = ag_ck_u[B == &FR[0] ? 0 : 1];
            for (int x = 0; x < RW; x++) { const int c = (int)floorf(((float)x + 0.5f - x0) / s); u[x] = (int16_t)(c < 0 ? 0 : c >= ck->w ? ck->w - 1 : c); }
            B->ck_px = ck->px; B->ck_pal = ag_ck_pal; B->ck_u = u; B->ck_w = ck->w; B->ck_h = ck->h;
            B->ck_y = (float)RH - (float)ck->h * s + sdy; B->ck_k = 1 / s;
            for (int y = 0; y < RH; y++) {   /* lignes bornées à la vitre (au-dessus de l'image : toute la ligne) */
                int sy = (int)floorf(((float)y + 0.5f - B->ck_y) * B->ck_k);
                if (sy < 0) { span_l[y] = 0; span_r[y] = (int16_t)(RW - 1); continue; }
                if (sy >= ck->h) sy = ck->h - 1;
                if (sy >= AG_CK_HMAX || ag_ck_ol[sy] > ag_ck_or[sy]) { span_l[y] = 1; span_r[y] = 0; continue; }
                const int xl = (int)ceilf(x0 + (float)ag_ck_ol[sy] * s - 0.5f), xr = (int)ceilf(x0 + (float)(ag_ck_or[sy] + 1) * s - 0.5f) - 1;
                span_l[y] = (int16_t)(xl < 0 ? 0 : xl); span_r[y] = (int16_t)(xr > RW - 1 ? RW - 1 : xr);
            }
            ag_spans_ck = 1;
            {   /* écrans du tableau de bord, dans le repère de l'interface : cadre 16:9 de largeur w16 à la hauteur de l'image, dont l'image est
                   le milieu (le 4:3 de la LDK : rogné de xc de chaque côté) */
                const int w16 = (int)lroundf((float)ck->h * 16.0f / 9.0f), xc = (w16 - ck->w) / 2;
                for (int e = 0; e < 3; e++) {
                    ag_ecr[e][0] = (x0 + (AG_ECRANS[e][0] * (float)w16 - (float)xc) * s) * ENG_SCALE - (float)ui_ox;
                    ag_ecr[e][2] = (x0 + (AG_ECRANS[e][2] * (float)w16 - (float)xc) * s) * ENG_SCALE - (float)ui_ox;
                    ag_ecr[e][1] = (B->ck_y + AG_ECRANS[e][1] * (float)ck->h * s) * ENG_SCALE - (float)ui_oy;
                    ag_ecr[e][3] = (B->ck_y + AG_ECRANS[e][3] * (float)ck->h * s) * ENG_SCALE - (float)ui_oy;
                }
                ag_ecr_ok = 1;
            }
#if ENG_DOF || ENG_DYN
            B->ck_dep = dep_of(AG_CK_NET);
#endif
        }
    }
    /* Horizon : colonne du panorama de chaque colonne de l'image (360° sur sa largeur, une puissance de 2) */
    if (AG.horizon >= 0 && ag_hz_u[0]) {
        const ag_image_t *h = &AG.hz[AG.horizon];
        uint16_t *u = ag_hz_u[B == &FR[0] ? 0 : 1] + AG_ROUL_M;   /* (caméra penchée : aussi les colonnes hors de l'image) */
        const float k = (float)h->w / (2 * PI_F);
        const int m = AG_ROUL_M && B->roule ? AG_ROUL_M : 0;
        for (int x = -m; x < RW + m; x++) u[x] = (uint16_t)((int)floorf((E.cam.yaw + atanf(((float)x + 0.5f - pcx) / FOCAL)) * k) & (h->w - 1));
        B->hz_px = h->px; B->hz_pal = &ag_hz_pal[0][0]; B->hz_w = h->w; B->hz_h = h->h; B->hz_base = h->base; B->hz_k = k / FOCAL; B->hz_u = u;
    } else B->hz_px = NULL;
    champ_init();
    B->nverts = B->nprims = 0;
    memset(&B->stats, 0, sizeof B->stats);
    for (int k = 0; k < ENG_WORKERS; k++) B->px_w[k] = B->seg_w[k] = 0, B->us3d_w[k] = B->usui_w[k] = 0;
    B->torche = 0; B->nlum = 0; B->rain = 0; B->nrain = 0; B->sera_faded = 0;
    B->ztest = !eng_sans_z && !ag_apercu; B->inside = 0;   /* (aperçu : sans test de profondeur, la chaussée devant tout, comme une carte) */
    B->ciel = 0; B->noir = 0;
    for (int b = 0; b < OTN; b++) ot_head[b] = ot_tail[b] = -1;
    occ_poly = NULL; nocc = 0; occ_sera = -1; narb = 0; sujet_on = parle_on = 0; poly_bias = 0; fade_mark = 0;
    sol_bas = AG.sol;
    ag_tris = 0;
    if (!ag_apercu) { ag_engins(); ag_armes(); }
    ag_det[0] = ag_tris;
    ag_vis_maj();
    ag_piste();
    if (!ag_apercu) ag_obstacles();
    ag_decors();
    ag_det[1] = ag_tris - ag_det[0];
    ag_terrain();
    ag_det[2] = ag_tris - ag_det[0] - ag_det[1];
    ag_sol();
    ag_det[3] = ag_tris - ag_det[0] - ag_det[1] - ag_det[2];
    AG.tris = ag_tris;
    if (ag_tris > AG.tris_max) AG.tris_max = ag_tris;
    image_finir();
}

/* ---------- Rastérisation dans la bande [K->by0, K->by1) ---------- */
#define ROW(y) (K->band + ((y) - K->by0) * K->bstride - K->bx0)
/* Profilage du rendu des bandes (ENG_PROFILE, banc d'essai sur l'ESP32) : cycles du processeur par poste */
#ifdef ENG_PROFILE
#include "esp_cpu.h"
#define PROF_T() esp_cpu_get_cycle_count()
#define PROF_ADD(k, t0) (K->prof[k] += PROF_T() - (t0))
#else
#define PROF_T() 0u
#define PROF_ADD(k, t0) ((void)(t0))
#endif
#define COV(y) (K->cov + ((y) - K->by0) * K->bstride - K->bx0)
#define ZB(y) (K->zb + ((y) - K->by0) * K->bstride - K->bx0)   /* (zcle_t *) */
#if ENG_DOF
#define DEP(y) (K->dep + ((y) - K->by0) * K->bstride - K->bx0)
#endif

/* Ordre de dessin : de l'avant vers le fond (table d'ordre parcourue à l'envers), avec un masque de couverture par
   bande. Un pixel déjà peint par une primitive plus proche n'est ni texturé ni écrit : le surdessin de l'algorithme
   du peintre (2,5 à 3,5 couches en ville ou en forêt) ne coûte plus que le test du masque. La transparence est
   binaire (index 0), donc l'image est la même. Les ombres marquent les pixels encore libres (bit 1) : ce qui y est
   peint ensuite, le sol sous le personnage, est assombri. Le ciel remplit à la fin les pixels restés libres. */

/* Tramage de Bayer 4 × 4 en 16.16 */
static const int32_t BAYER16[16] = { 0, 32768, 8192, 40960, 49152, 16384, 57344, 24576,
                                     12288, 45056, 4096, 36864, 61440, 28672, 53248, 20480 };
/* Brouillard tramé : la même ligne décalée de deux colonnes, centrée (- 32768) ; zéros sans tramage */
static const int32_t BAYER16_F[16] = { -24576, 8192, -32768, 0, 24576, -8192, 16384, -16384,
                                       -28672, 4096, -20480, 12288, 20480, -12288, 28672, -4096 };
static const int32_t BAYER_NUL[4] = { 0, 0, 0, 0 };

/* Test de profondeur d'un pixel déjà peint (clé zp) : celle du polygone, z8 = clé × 256 avancée en entiers le long de la
   ligne (une addition par pixel au lieu d'un calcul flottant et d'une conversion) ; à moins de Z_MARGE de la limite,
   où les arrondis des deux calculs peuvent différer (au plus 3 + largeur / 256), la clé exacte en flottant décide :
   toujours la réponse de (int32_t)((wy + dwdx × x) × ZK) > Z_PLUS(zp). Le sol caché sous la piste d'une course en
   passe des dizaines de milliers par image */
/* d / n arrondi vers zéro comme la division du C, par un décalage pour un segment entier (n = SUB, le cas courant) */
_Static_assert(SUB == 16 || SUB == 32, "div_seg : décalage de 4 ou 5 pour SUB");
#define SUB_SH (SUB == 32 ? 5 : 4)
static inline int32_t div_seg(int32_t d, int n) { return n == SUB ? (d + ((d >> 31) & (SUB - 1))) >> SUB_SH : d / n; }
#define Z_MARGE (4 + (RW_MAX >> 8))
#ifndef ENG_RECT_OPT
#define ENG_RECT_OPT (ENG_RECT && !ENG_PREMIUM)   /* optimisations du rendu de l'écran rectangulaire (LDK) ; 0 : le rendu de la montre, pour comparer ; version PC premium : 0 */
#endif
#if !ENG_RECT_OPT
static inline int libre_z(zcle_t zp, int32_t z8, float wy, float dwdx, int x) {
    const int32_t t = Z_PLUS((int32_t)zp), k = z8 >> 8;
    if (k > t + Z_MARGE) return 1;
    if (k < t - Z_MARGE) return 0;
    return (int32_t)((wy + dwdx * x) * ZK) > t;
}
#endif
/* Écran rectangulaire (LDK, 11/11/2026 ; la montre garde libre_z) : la clé du polygone avance en entiers avec 2^sh de
   fraction (11 bits, au lieu de 8), et la marge m vient d'une borne de l'écart au calcul flottant faite à chaque ligne
   (zmarge) : réponse sûre si k > t + m (libre) ou k < t - m + 1 (pas libre), le flottant ne décide qu'entre les deux. Sur
   la LDK (FPU lente), le calcul flottant de libre_z prenait 12 % du temps de la 3D ; mêmes réponses qu'avant */
#if ENG_RECT_OPT
static inline int libre_zr(uint16_t zp, int32_t zq, int sh, int m, float wy, float dwdx, int x) {
    const int32_t t = Z_PLUS((int32_t)zp), k = zq >> sh;
    if (k > t + m) return 1;
    if (k < t - m + 1) return 0;
    return (int32_t)((wy + dwdx * x) * ZK) > t;
}
/* Fraction (zfr = 2^sh), pas zdq et marge m d'un polygone (les sommets v, n, pente dwdx de 1/z par colonne). Écart de la
   clé entière d'une ligne (départ en xs, un pas par colonne) au flottant de libre_z ((wy + dwdx × x) × ZK, simple
   précision) : arrondis des deux calculs flottants, au plus ZK × 2^-24 × (4 |wy| + 6 |dwdx| (|x| + |xs|) + 2 |dwdx|
   (x - xs)), plus la troncature du départ et de chaque pas (1 / 2^sh par pas). Sur toutes les lignes du polygone : 1/z
   entre ses sommets (wmax), |wy| ≤ wmax + RW |dwdx|, x et xs < RW : ZK × 2^-24 × (4 wmax + 18 RW |dwdx|) + (RW + 2) / 2^sh ;
   m : cette borne × 1,5, plus 1 (la clé tronquée du flottant est entre k - m et k + m). La borne d'une ligne, faite à
   chaque ligne, coûtait 2,8 % de la 3D (LDK). Trop grand pour 32 bits (polygone presque de profil, tout près) : 8 bits et la
   marge d'origine (Z_MARGE) */
static inline void zmarge_poly(const sv_t *v, int n, float dwdx, float *zfr, int32_t *zdq, int *sh, int *m) {
    float wmax = 0;
    for (int i = 0; i < n; i++) wmax = fmaxf(wmax, fabsf(v[i].w));
    const float adw = fabsf(dwdx);
    if (adw * ZK * 2048.0f * RW < 1.0e9f && wmax * ZK * 2048.0f < 1.0e9f) {
        *sh = 11; *zfr = 2048.0f; *zdq = (int32_t)(dwdx * ZK * 2048.0f);
        const float e = ZK * 5.9604645e-8f * (4 * wmax + 18 * RW * adw) + (RW + 2) / 2048.0f;
        *m = (int)(e * 1.5f) + 1;
    } else {
        *sh = 8; *zfr = 256.0f; *zdq = (int32_t)(dwdx * ZK * 256.0f); *m = Z_MARGE;
    }
}
#endif
/* ceil(v - 0,5) sans appel de bibliothèque (|v| < 8192) */
static inline int ceil_half(float v) { return 8192 - (int)(8192.5f - v); }

/* Polygone convexe texturé dans la bande (case de sol, face de bâtiment, morceau de route, décor), d'un seul tenant :
   une préparation et un parcours de chaque ligne par polygone, au lieu d'un par triangle de l'éventail (une case de sol
   coûtait deux triangles, donc deux préparations et deux fois ses lignes).
   u/z, v/z et 1/z varient linéairement à l'écran sur un polygone plan : leurs équations de plan viennent du plus grand
   triangle (0, i, i + 1). La lumière et le brouillard sont interpolés le long des bords gauche et droit, puis à travers
   la ligne : mêmes valeurs des deux côtés d'une arête partagée, sans couture entre cases voisines. Si la profondeur
   varie de moins de 12 % (polygones lointains, la plupart), u et v sont interpolés directement (affine, aucune
   division) ; sinon la texture exacte est recalculée tous les SUB pixels. Dessin de l'avant vers le fond : les pixels
   déjà couverts sont sautés par plages ; chaque plage repart des valeurs exactes de son premier pixel. */
#if ENG_ROULIS
#define AFFINE_RATIO 1.04f   /* (course anti-gravité : murs rasants de la caméra basse ; à 1,25, un polygone qui passait le seuil en
                                approchant changeait de calcul et sa texture sautait) */
#elif ENG_RECT && !ENG_PREMIUM
#define AFFINE_RATIO 1.25f   /* (LDK : plus de surfaces sans correction de perspective, « perspective allégée ») */
#else
#define AFFINE_RATIO 1.12f
#endif
typedef struct { int i, j; float sx, sl, sf; } edge_t;   /* bord du sommet i au sommet j : pentes par ligne */
static inline void edge_set(const sv_t *v, edge_t *e) {
    float dy = v[e->j].y - v[e->i].y, inv = dy > 1e-6f ? 1.0f / dy : 0;
    e->sx = (v[e->j].x - v[e->i].x) * inv; e->sl = (v[e->j].l - v[e->i].l) * inv; e->sf = (v[e->j].f - v[e->i].f) * inv;
}
/* Avance le bord jusqu'à celui qui contient la ligne py (dir : +1 ou -1 dans l'ordre des sommets) */
static inline void edge_walk(const sv_t *v, int n, edge_t *e, int dir, float py) {
    for (int guard = n; v[e->j].y < py && guard > 0; guard--) { e->i = e->j; e->j = (e->j + dir + n) % n; edge_set(v, e); }
}
/* Pixels gardés par un fondu de niveau l (sur 16) : masque des colonnes x & 3 de la ligne y & 3 (Bayer 4 × 4) */
static const uint8_t BAYER4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline unsigned fade_keep(int l, int y) {
    unsigned k = 0;
    for (int x = 0; x < 4; x++) if (BAYER4[((y & 3) << 2) | x] >= l) k |= 1u << x;
    return k;
}

/* Pixel à demi-teinte (ombre en attente), l'ordre des octets donné (swp) au lieu d'être relu à chaque pixel */
static inline uint16_t demi565(uint16_t c, int swp) {
    if (swp) { c = (uint16_t)((c >> 8) | (c << 8)); c = (c >> 1) & 0x7BEF; return (uint16_t)((c >> 8) | (c << 8)); }
    return (c >> 1) & 0x7BEF;
}
/* Couleur c mêlée à t, opacité a sur 16 (0 à 16) : les trois composantes écartées dans un mot de 32 bits (bleu en 0-4,
   rouge en 11-15, vert en 21-26), mêlées par deux multiplications ; chaque produit tient sous la composante suivante,
   donc chacune vaut (c × (16 - a) + t × a) >> 4, comme calculée à part (même résultat au bit près, deux fois moins
   d'instructions sur l'ESP32) */
#define ECARTE565 0x07E0F81Fu
static inline uint16_t mele565(uint16_t c, uint16_t t, int a, int swp) {
    if (swp) { c = (uint16_t)((c >> 8) | (c << 8)); t = (uint16_t)((t >> 8) | (t << 8)); }
    const uint32_t x = (c | (uint32_t)c << 16) & ECARTE565, y = (t | (uint32_t)t << 16) & ECARTE565;
    const uint32_t m = ((x * (uint32_t)(16 - a) + y * (uint32_t)a) >> 4) & ECARTE565;
    const uint16_t o = (uint16_t)(m | m >> 16);
    return swp ? (uint16_t)((o >> 8) | (o << 8)) : o;
}
/* Créneaux du masque gardés aux pixels translucides (fondu_px : faces en fondu, corps de l'eau) : leur couleur, pixel par
   pixel, dans att ; opacité sur 16 de chacun (huit niveaux ; att_idx : le niveau d'une opacité de 1 à 16) */
#define ATT_SLOT0 56
static const uint8_t ATT_A[8] = { 2, 4, 6, 8, 10, 12, 14, 15 };
static inline int att_idx(int a) { a = (a - 1) >> 1; return a < 0 ? 0 : a > 7 ? 7 : a; }
/* Pixel peint derrière ce qui l'attend (masque cv non nul) : une face en fondu (créneau ATT_SLOT0 et plus : sa couleur du
   pixel q), la surface d'un bassin (teinte de son créneau, bits 2 à 7), l'ombre (bit 1 : demi-teinte) */
static inline uint16_t attente565(const worker_t *K, uint16_t c, unsigned cv, int swp, size_t q) {
    const unsigned s = cv >> 2;
    if (s >= ATT_SLOT0) c = mele565(c, K->att[q], ATT_A[s - ATT_SLOT0], swp);
    else if (s) c = mele565(c, K->teinte[s], K->teinte_a[s], swp);
    return cv & 2 ? demi565(c, swp) : c;
}
/* Pixel translucide (face en fondu : bâtiment devant Sera, verre, décor près de la caméra ; corps de l'eau), couleur cc,
   opacité a sur 16 : sur un pixel déjà peint plus loin, mêlée tout de suite ; sur un pixel libre, sa couleur attend (att,
   créneau de son niveau) ce qui
   sera peint derrière (attente565, le ciel compris) ; sur un pixel qui attend déjà autre chose (bassin, ombre, autre face
   en fondu), la trame d'avant (garde : pixel gardé par fade_keep, ou par le seuil de Bayer de l'eau). fon : face en fondu,
   que la lueur des feux laisse. Renvoie les pixels peints (0 ou 1). Avant le 31/10/2026 : toujours la trame (damier) */
static inline int fondu_px(worker_t *K, uint16_t *o, uint8_t *cv, zcle_t *zp, int32_t D8, uint16_t cc, int a, int garde, int swp, int fon) {
    const size_t q = (size_t)(cv - K->cov);
    const int ai = att_idx(a);
    if (fon) K->fon[q >> 3] |= (uint8_t)(1u << (q & 7));   /* la lueur des feux la laisse */
    if (*cv & 1) { *o = mele565(*o, cc, ATT_A[ai], swp); return 1; }
    if (!*cv) { K->att[q] = cc; *cv = (uint8_t)((ATT_SLOT0 + ai) << 2); return 0; }
    if (!garde) return 0;
    *o = attente565(K, cc, *cv, swp, q); *cv = 1; *zp = (zcle_t)(D8 >> 8);
    return 1;
}
/* Nombre de pixels, au plus n, pendant lesquels X >> s ne change pas (X avance de d à chaque pixel ; au moins 1) */
static inline int pas_meme(int32_t X, int32_t d, int s, int n) {
    int32_t k;
    if (d > 0) k = (((X >> s) + 1) * (1 << s) - X + d - 1) / d;
    else if (d < 0) k = (X - (X >> s) * (1 << s)) / -d + 1;
    else return n;
    return k < n ? (int)k : n;
}
/* Sous-plage de n pixels de même ligne de la table (r1 : lumière et brouillard constants) : texture, couleur, masque et
   profondeur, rien d'autre ; melange (constante à l'appel) : brouillard lissé, mêlée à la ligne suivante (fraction fr
   sur 16). Peu de variables vivantes : la boucle tient dans les registres de l'ESP32 (l'ancienne en relisait une
   vingtaine sur la pile à chaque pixel) */
static inline __attribute__((always_inline)) int sous_plage(uint16_t *o, uint8_t *cv, int n, const uint8_t *tex, int sh, int m,
                                                            int mv, int32_t U, int32_t V, int32_t dU, int32_t dV, const uint16_t *r1,
                                                            int32_t F16, int32_t dF16, int alpha, int swp, const worker_t *K, zcle_t *zr, int32_t D8,
                                                            int32_t dD8, int melange) {
    int px = 0;
    for (int k = 0; k < n; k++) {
        const int id = tex[(((V >> 16) & mv) << sh) | ((U >> 16) & m)];
        if (id || !alpha) {
            uint16_t cc = r1[id];
            if (melange) cc = mele565(cc, r1[256 + id], ((uint32_t)F16 >> 12) & 15, swp);
            o[k] = cv[k] ? attente565(K, cc, cv[k], swp, (size_t)(cv + k - K->cov)) : cc;
            cv[k] = 1; zr[k] = (zcle_t)(D8 >> 8); px++;
        }
        U += dU; V += dV; D8 += dD8; F16 += dF16;
    }
    return px;
}
/* Sous-plage opaque (texel 0 peint aussi), le cas de la plupart des pixels : fonction à part (en IRAM), sans appel dans
   sa boucle, qui tient alors dans les registres et devient une boucle matérielle de l'Xtensa (24 instructions par
   pixel au lieu de 70 ; 50 au lieu de 130 avec le brouillard lissé). Couleur et profondeur d'abord, en notant ce que
   contenait le masque (acc) ; puis, s'il y avait un pixel en attente (ombre, bassin, face en fondu : masque au-delà de
   1, rare), sa couleur passe par attente565 ; masque mis à 1. V et son pas arrivent décalés de sh (masque mvs = mv << sh :
   plus de décalage variable par pixel ; mêmes bits de texture tant que la texture a au plus 65 536 texels). Mêmes
   pixels que sous_plage */
static inline __attribute__((always_inline)) unsigned opaque_boucle(uint16_t *o, const uint8_t *cv, zcle_t *zr, int n, const uint8_t *tex,
                                                                    uint32_t m, uint32_t mvs, uint32_t U, uint32_t Vs, uint32_t dU, uint32_t dVs,
                                                                    const uint16_t *r1, int32_t F16, int32_t dF16, int32_t D8, int32_t dD8, int melange, int swp) {
    unsigned acc = 0;
    const uint16_t *r2 = r1 + 256;
#if ENG_RECT_OPT
    int k = 0;
    /* Écran rectangulaire (LDK) : deux pixels par tour, couleur et profondeur écrites en un mot de 32 bits chacune (moitié
       moins d'écritures) ; o et zr ont le même alignement (lignes de RW pixels de 16 bits) ; mêmes valeurs */
    #define OPAQUE_PX(cc_) do { \
        const int id_ = tex[((Vs >> 16) & mvs) | ((U >> 16) & m)]; \
        cc_ = r1[id_]; \
        if (melange) { \
            const uint32_t fr_ = ((uint32_t)F16 >> 12) & 15, fb_ = 16 - fr_; \
            uint16_t c_ = cc_, t_ = r2[id_]; \
            if (swp) { c_ = (uint16_t)((c_ >> 8) | (c_ << 8)); t_ = (uint16_t)((t_ >> 8) | (t_ << 8)); } \
            const uint32_t x_ = (c_ | (uint32_t)c_ << 16) & ECARTE565, y_ = (t_ | (uint32_t)t_ << 16) & ECARTE565; \
            const uint32_t mm_ = ((x_ * fb_ + y_ * fr_) >> 4) & ECARTE565; \
            cc_ = (uint16_t)(mm_ | mm_ >> 16); \
            if (swp) cc_ = (uint16_t)((cc_ >> 8) | (cc_ << 8)); \
        } } while (0)
    if (n > 0 && ((uintptr_t)o & 2)) {   /* premier pixel seul jusqu'à un mot aligné */
        uint16_t cc; OPAQUE_PX(cc);
        o[0] = cc; zr[0] = (zcle_t)(D8 >> 8); acc |= cv[0];
        U += dU; Vs += dVs; D8 += dD8; F16 += dF16; k = 1;
    }
    if (!(((uintptr_t)(o + k) | (uintptr_t)(zr + k)) & 3))
        for (; k + 1 < n; k += 2) {
            uint16_t ca, cb;
            OPAQUE_PX(ca);
            const uint32_t za = (uint16_t)(D8 >> 8);
            U += dU; Vs += dVs; D8 += dD8; F16 += dF16;
            OPAQUE_PX(cb);
            const uint32_t zb2 = (uint16_t)(D8 >> 8);
            U += dU; Vs += dVs; D8 += dD8; F16 += dF16;
            *(uint32_t *)(void *)(o + k) = ca | (uint32_t)cb << 16;   /* (petit-boutiste : o[k] dans les 16 bits du bas) */
            *(uint32_t *)(void *)(zr + k) = za | zb2 << 16;
            acc |= cv[k] | cv[k + 1];
        }
    #undef OPAQUE_PX
    for (; k < n; k++) {
#else
    for (int k = 0; k < n; k++) {
#endif
        const int id = tex[((Vs >> 16) & mvs) | ((U >> 16) & m)];
        uint16_t cc = r1[id];
        if (melange) {   /* mele565(cc, r2[id], fr, swp), l'ordre des octets fixé à la compilation ; fraction du pixel (08/11/2026 : plus de sous-plage par seizième de niveau) */
            const uint32_t fr = ((uint32_t)F16 >> 12) & 15, fb = 16 - fr;
            uint16_t c = cc, t = r2[id];
            if (swp) { c = (uint16_t)((c >> 8) | (c << 8)); t = (uint16_t)((t >> 8) | (t << 8)); }
            const uint32_t x = (c | (uint32_t)c << 16) & ECARTE565, y = (t | (uint32_t)t << 16) & ECARTE565;
            const uint32_t mm = ((x * fb + y * fr) >> 4) & ECARTE565;
            cc = (uint16_t)(mm | mm >> 16);
            if (swp) cc = (uint16_t)((cc >> 8) | (cc << 8));
        }
        o[k] = cc; zr[k] = (zcle_t)(D8 >> 8); acc |= cv[k];
        U += dU; Vs += dVs; D8 += dD8; F16 += dF16;
    }
    return acc;
}
ENG_HOT static int plage_opaque(uint16_t *o, uint8_t *cv, zcle_t *zr, int n, const uint8_t *tex, uint32_t m, uint32_t mvs, uint32_t U,
                                uint32_t Vs, uint32_t dU, uint32_t dVs, const uint16_t *r1, int mel, int32_t F16, int32_t dF16, int32_t D8, int32_t dD8, const worker_t *K, int swp) {
    unsigned acc;
    if (!mel) acc = opaque_boucle(o, cv, zr, n, tex, m, mvs, U, Vs, dU, dVs, r1, 0, 0, D8, dD8, 0, 0);
    else if (swp) acc = opaque_boucle(o, cv, zr, n, tex, m, mvs, U, Vs, dU, dVs, r1, F16, dF16, D8, dD8, 1, 1);
    else acc = opaque_boucle(o, cv, zr, n, tex, m, mvs, U, Vs, dU, dVs, r1, F16, dF16, D8, dD8, 1, 0);
    if (acc > 1)
        for (int k = 0; k < n; k++) if (cv[k] > 1) o[k] = attente565(K, o[k], cv[k], swp, (size_t)(cv + k - K->cov));   /* masque 1 : attente565 rend la couleur telle quelle */
    for (int k = 0; k < n; k++) cv[k] = 1;
    return n;
}
/* Plage de n pixels libres du cas courant (ni surface d'eau, ni fondu), à partir de la colonne x : les calculs de la
   boucle générale, sans ses tests par pixel. bl, bf : tramage de la lumière et du brouillard par colonne x & 3 (zéros
   sans tramage) ; alpha : texel 0 transparent (constante à l'appel, une boucle par cas). Renvoie les pixels écrits. */
static inline __attribute__((always_inline)) int plage_simple(uint16_t *o, uint8_t *cv, int x, int n, const uint8_t *tex,
                                                              int sh, int m, int mv, int32_t U, int32_t V, int32_t dU, int32_t dV,
                                                              int32_t L16, int32_t dL16, int32_t F16, int32_t dF16, const int32_t *bl,
                                                              const int32_t *bf, const uint16_t *cm, int alpha, int swp, const worker_t *K,
                                                              zcle_t *zr, int32_t D8, int32_t dD8, int lisse) {
    int px = 0;
    if (lisse) {   /* sans tramage (bl, bf nuls) : par sous-plages de couleur constante, mêmes pixels */
        for (int k = 0; k < n;) {
            int L = L16 >> 16, F = F16 >> 16;
            L = L < 0 ? 0 : L > 15 ? 15 : L;
            F = F < 0 ? 0 : F > 7 ? 7 : F;
            /* Brouillard lissé : la sous-plage ne s'arrête plus qu'aux changements de niveau entier (lumière, brouillard) ; la fraction du
               brouillard se calcule par pixel (08/11/2026 : découpée à chaque seizième de niveau, une rue qui s'enfonce dans le brouillard
               faisait des centaines de sous-plages de quelques pixels par image ; mêmes pixels) */
            const int mel = F < 7 && F16 > 0;
            const int r = pas_meme(F16, dF16, 16, pas_meme(L16, dL16, 16, n - k));
#ifdef ENG_COMPTE
            { extern unsigned long eng_px_mel; if (mel) eng_px_mel += r; }
#endif
#ifdef ENG_COMPTE
            { extern unsigned long eng_nsous, eng_nsous_av; eng_nsous++; for (int q = 0, F2 = F16, L2 = L16; q < r; ) { const int r2 = pas_meme(F2, dF16, 12, pas_meme(L2, dL16, 16, r - q)); q += r2; F2 += dF16 * r2; L2 += dL16 * r2; eng_nsous_av++; } }
#endif
            const uint16_t *r1 = cm + (((L << 3) + F) << 8);
            if (!alpha) px += plage_opaque(o + k, cv + k, zr + k, r, tex, (uint32_t)m, (uint32_t)mv << sh, (uint32_t)U, (uint32_t)V << sh,
                                           (uint32_t)dU, (uint32_t)dV << sh, r1, mel, F16, dF16, D8, dD8, K, swp);
            else px += mel ? sous_plage(o + k, cv + k, r, tex, sh, m, mv, U, V, dU, dV, r1, F16, dF16, 1, swp, K, zr + k, D8, dD8, 1)
                           : sous_plage(o + k, cv + k, r, tex, sh, m, mv, U, V, dU, dV, r1, 0, 0, 1, swp, K, zr + k, D8, dD8, 0);
            U += dU * r; V += dV * r; L16 += dL16 * r; F16 += dF16 * r; D8 += dD8 * r; k += r;
        }
        return px;
    }
    for (int k = 0; k < n; k++, x++) {
        const int id = tex[(((V >> 16) & mv) << sh) | ((U >> 16) & m)];
        if (id || !alpha) {
            int L = (L16 + bl[x & 3]) >> 16;
            L = L < 0 ? 0 : L > 15 ? 15 : L;
            int F = (F16 + bf[x & 3]) >> 16;
            F = F < 0 ? 0 : F > 7 ? 7 : F;
            uint16_t cc = cm[(((L << 3) + F) << 8) | id];
            if (lisse && F < 7 && F16 > 0) { const int fr = (F16 >> 12) & 15; if (fr) cc = mele565(cc, cm[(((L << 3) + F + 1) << 8) | id], fr, swp); }   /* brouillard lissé (sans tramage) */
            o[k] = cv[k] ? attente565(K, cc, cv[k], swp, (size_t)(cv + k - K->cov)) : cc;
            cv[k] = 1; zr[k] = (zcle_t)(D8 >> 8); px++;
        }
        U += dU; V += dV; L16 += dL16; F16 += dF16; D8 += dD8;
    }
    return px;
}

#ifdef ENG_PROFILE
int eng_xp;
#endif
/* Île du jeu des héroïnes (ile.c ; 09/11/2026, demande de l'utilisateur : « spécialiser une version du moteur pour maintenir les
   30 i/s ») : mesures sur la montre (SDX XP, firmware de profilage), jungle et lagon : polygones en affine jusqu'à 60 % d'écart
   de profondeur (au lieu de 12 %) : + 1 à 2,5 i/s ; le corps de l'eau du lagon (opaque à 7 % : 0,02 de profondeur pour 0,3)
   faisait passer chaque pixel par le mélange translucide (fondu_px, puis attente565 sous le fond) : 4 i/s ; sous ILE_EAU_MIN
   (opacité × 16) il n'est plus peint, le fond (tuiles à la couleur de l'eau) se dessine seul ; les reflets restent. Le 11/11/2026, le
   lagon a pris sa vraie profondeur (jusqu'à 1 m, mer et lagon d'un seul corps d'eau opaque à 4,5 : opacité 5 / 16 au plus dans le lagon) :
   seuil relevé à 7, le lagon reste sans corps peint sur la montre comme avant, la mer profonde au-delà du récif l'a toujours.
   Même jour, lagon profond (environ 2 m) et eau opaque à 4,5 (à 7, la limite du fond sombre de la mer et du sable du lagon se voyait
   à travers l'eau) : seuil 11, la même profondeur (3 m) qu'au seuil 7 avec l'eau opaque à 7. L'émulateur de l'île mêle désormais le
   corps à son opacité exacte ; ici, les huit niveaux d'ATT_A restent (bandes au-delà du seuil) */
#define ILE_AFFINE 1.6f
#define ILE_EAU_MIN 11
ENG_HOT static void raster_poly(worker_t *K, const sv_t *v, int n, const uint8_t *tex, int sh, int m, int mv, int alpha, int fade, int corps) {
#ifdef ENG_PROFILE
    if (eng_xp & 2) { m &= 1; mv &= 1; }   /* essai : coût des lectures de textures (2 × 2 texels de chaque texture) */
#endif
    const uint32_t pt0 = PROF_T();
    K->prof[PROF_TRIS]++;
    int top = 0;
    float ymin = v[0].y, ymax = v[0].y, wmin = v[0].w, wmax = v[0].w;
    for (int i = 1; i < n; i++) {
        if (v[i].y < ymin) { ymin = v[i].y; top = i; }
        if (v[i].y > ymax) ymax = v[i].y;
        if (v[i].w < wmin) wmin = v[i].w;
        if (v[i].w > wmax) wmax = v[i].w;
    }
    int y0 = ceil_half(ymin), y2 = ceil_half(ymax) - 1;
    if (y0 < K->by0) y0 = K->by0;
    if (y2 > K->by1 - 1) y2 = K->by1 - 1;
    if (y0 > y2) return;
    /* Plan de u/z, v/z, 1/z : plus grand triangle (0, i, i + 1) */
    const sv_t *a = &v[0], *b = &v[1], *c = &v[2];
    float det = 0;
    for (int i = 1; i + 1 < n; i++) {
        float d = (v[i].x - a->x) * (v[i + 1].y - a->y) - (v[i + 1].x - a->x) * (v[i].y - a->y);
        if (fabsf(d) > fabsf(det)) { det = d; b = &v[i]; c = &v[i + 1]; }
    }
    if (det > -1e-6f && det < 1e-6f) return;
    float dx1 = b->x - a->x, dy1 = b->y - a->y, dx2 = c->x - a->x, dy2 = c->y - a->y;
    float idet = 1.0f / det;
    const int affine = wmax < wmin * (E.w.ile[0] ? ILE_AFFINE : AFFINE_RATIO);
#if ENG_DOF
    float wsum = 0;
    for (int i = 0; i < n; i++) wsum += v[i].w;
    const uint8_t pdep = dep_of(wsum > 0 ? n / wsum : 1e3f);
#endif
    /* Attributs : affine -> u, v directs (u/z ÷ 1/z aux sommets) ; perspective -> u/z, v/z, 1/z */
    float ua, ub, uc, va, vb, vc;
    if (affine) { ua = a->qu / a->w; ub = b->qu / b->w; uc = c->qu / c->w; va = a->qv / a->w; vb = b->qv / b->w; vc = c->qv / c->w; }
    else { ua = a->qu; ub = b->qu; uc = c->qu; va = a->qv; vb = b->qv; vc = c->qv; }
    float dudx = ((ub - ua) * dy2 - (uc - ua) * dy1) * idet, dudy = ((uc - ua) * dx1 - (ub - ua) * dx2) * idet;
    float dvdx = ((vb - va) * dy2 - (vc - va) * dy1) * idet, dvdy = ((vc - va) * dx1 - (vb - va) * dx2) * idet;
    float dwdx = ((b->w - a->w) * dy2 - (c->w - a->w) * dy1) * idet, dwdy = ((c->w - a->w) * dx1 - (b->w - a->w) * dx2) * idet;
    const uint16_t *cm = cmap;
    const int dith = E.dither_on, swp = E.cfg.swap565;
    /* alpha 2 : surface de l'eau ; l porte l'opacité × 16 du corps de l'eau (négative au-dessus de l'eau : rien) ;
       texel 0 : le corps de l'eau (couleur corps), laissé libre (on voit le fond, dessiné ensuite) si l'opacité ne passe
       pas le seuil de Bayer ; reflets translucides près du bord, à l'opacité l × 4 / 16 (pleins dès le quart de l'opacité du
       corps ; 08/11/2026, retour de jeu : à pleine opacité dès que la profondeur dépassait zéro, leur zone avait le contour en
       marches des cases d'eau, très visible dans le souterrain) ; lumière fixe */
    int surf = alpha == 2;
    const int eau_min = E.w.ile[0] ? ILE_EAU_MIN : 0;   /* île : corps de l'eau presque transparent laissé (le fond seul) */
    int lsurf = (int)(12.0f - RF->rain * 2.5f);
    if (lsurf < 0) lsurf = 0;
    /* alpha 3 : surface d'un bassin, translucide : rien n'est peint, chaque pixel encore libre (et sans autre teinte) prend
       un créneau de teinte en attente (masque, bits 2 à 7) ; ce qui sera peint derrière (fond, parois, corps d'un nageur)
       s'y mêle : le corps de l'eau à l'opacité des sommets (l : opacité × 16), un reflet à moitié. Plus de créneau libre
       dans la bande : la surface tramée (alpha 2) */
    int verre = 0, tc = 0, tr[2] = { -1, -1 }, tid[2] = { 0, 0 };
    if (alpha == 3) {
        if (K->nteinte < ATT_SLOT0 - 3) {   /* 3 créneaux par surface ; ATT_SLOT0 à 63 : fondus translucides */
            int Fv = (int)v[0].f; Fv = Fv < 0 ? 0 : Fv > 7 ? 7 : Fv;
            float lm = 0;
            for (int i = 0; i < n; i++) lm += v[i].l;
            int ta = (int)lroundf(lm / n); ta = ta < 1 ? 1 : ta > 15 ? 15 : ta;
            tc = K->nteinte++; K->teinte[tc] = cm[(((lsurf << 3) + Fv) << 8) | corps]; K->teinte_a[tc] = (uint8_t)ta;
            tr[0] = K->nteinte++; tr[1] = K->nteinte++;
            for (int k = 0; k < 2; k++) K->teinte_a[tr[k]] = 8;
            verre = 1;
            /* couleur des reflets : celle du texel, à la lumière de la surface (deux couleurs au plus dans T_SURF) */
            for (int k = 0; k < 2; k++) tid[k] = -1;
            for (int t = 0; t < 256; t++) {
                const int id = tex[t];
                if (!id || id == tid[0] || id == tid[1]) continue;
                const int k = tid[0] < 0 ? 0 : tid[1] < 0 ? 1 : -1;
                if (k < 0) break;
                tid[k] = id; K->teinte[tr[k]] = cm[(((lsurf << 3) + Fv) << 8) | id];
            }
        } else surf = 1;
    }
    const float lofs = surf ? 0.0f : (dith ? -0.5f : 0.0f) - RF->rain * 2.5f;   /* pluie : scène assombrie */
    const int zt = RF->ztest;
#if ENG_RECT_OPT
    float zfr = 256.0f;
    int32_t zdq = 0;
    int zsh = 8, zm = Z_MARGE;
    if (zt) zmarge_poly(v, n, dwdx, &zfr, &zdq, &zsh, &zm);
#endif
    const int32_t dD8 = (int32_t)(dwdx * ZK * 256.0f);   /* clé de profondeur × 256 : pas d'une colonne */
    const int32_t adU = (int32_t)(dudx * 65536.0f), adV = (int32_t)(dvdx * 65536.0f);
    /* Bords : chaîne A (sommets suivants depuis le haut) et chaîne B (précédents) */
    edge_t ea = { top, (top + 1) % n, 0, 0, 0 }, eb = { top, (top + n - 1) % n, 0, 0, 0 };
    edge_set(v, &ea); edge_set(v, &eb);
    int px = 0, segs = 0;
    const uint32_t pt1 = PROF_T();
    PROF_ADD(PROF_SETUP, pt0);
    uint32_t pix = 0;
    K->prof[PROF_NROWS] += (uint32_t)(y2 - y0 + 1);
    for (int y = y0; y <= y2; y++) {
        if (span_l[y] > span_r[y]) continue;   /* ligne sans pixel à dessiner (edge_walk rattrape la suivante) */
        const float py = y + 0.5f;
        edge_walk(v, n, &ea, 1, py); edge_walk(v, n, &eb, -1, py);
        const sv_t *pa = &v[ea.i], *pb = &v[eb.i];
        float xl = pa->x + (py - pa->y) * ea.sx, ll = pa->l + (py - pa->y) * ea.sl, fl = pa->f + (py - pa->y) * ea.sf;
        float xr = pb->x + (py - pb->y) * eb.sx, lr = pb->l + (py - pb->y) * eb.sl, fr = pb->f + (py - pb->y) * eb.sf;
        if (xl > xr) { float t = xl; xl = xr; xr = t; t = ll; ll = lr; lr = t; t = fl; fl = fr; fr = t; }
        int xs = ceil_half(xl), xe = ceil_half(xr) - 1;
        if (xs < span_l[y]) xs = span_l[y];
        if (xe > span_r[y]) xe = span_r[y];
        if (xs > xe) continue;
        /* Lumière et brouillard à travers la ligne */
        const float iw = xr - xl > 1e-3f ? 1.0f / (xr - xl) : 0;
        const float dldx = (lr - ll) * iw, dfdx = (fr - fl) * iw;
        const int32_t dL16 = (int32_t)(dldx * 65536.0f), dF16 = (int32_t)(dfdx * 65536.0f);
        /* Plages de pixels libres de la ligne : les pixels déjà couverts sont sautés sans rien calculer (un octet lu) ;
           chaque plage repart des valeurs exactes au centre de son premier pixel. Dans une plage, aucun test de
           couverture : un polygone n'écrit qu'une fois chaque pixel. */
        const float oy = py - a->y;
        uint16_t *orow = ROW(y);
        uint8_t *crow = COV(y);
        zcle_t *zrow = ZB(y);
        const float wy = a->w + dwdy * oy + dwdx * (0.5f - a->x);   /* 1/z au centre du pixel x : wy + dwdx × x */
        /* Pixel libre : jamais peint, ou peint par plus loin que ce polygone de plus de 1/64 (tampon de profondeur ; clé
           × 256 en entiers depuis xs, le flottant près de la limite : libre_z) */
#if ENG_RECT_OPT
        const int32_t zA8 = zt ? (int32_t)((wy + dwdx * xs) * ZK * zfr) : 0, zdA8 = zdq;
        #define LIBRE(xx) (!(crow[xx] & 1) || (zt && libre_zr(zrow[xx], zA8 + zdA8 * ((xx) - xs), zsh, zm, wy, dwdx, (xx))))
#else
        const int32_t zA8 = zt ? (int32_t)((wy + dwdx * xs) * ZK * 256.0f) : 0, zdA8 = zt ? (int32_t)(dwdx * ZK * 256.0f) : 0;
        #define LIBRE(xx) (!(crow[xx] & 1) || (zt && libre_z(zrow[xx], zA8 + zdA8 * ((xx) - xs), wy, dwdx, (xx))))
#endif
        const int32_t *bay = &BAYER16[(y & 3) << 2];
        const unsigned keep = fade ? fade_keep(fade, y) : 0xFu;   /* fondu : pixels laissés libres (on voit derrière) */
        /* fondu translucide (fondu_px) : opacité de la face (16 - fade) / 16 */
        const int trans = fade && !surf && !verre;
        const int simple = !surf && !verre && keep == 0xFu;
        const int32_t *bl = dith ? bay : BAYER_NUL, *bf = dith ? &BAYER16_F[(y & 3) << 2] : BAYER_NUL;
        for (int x = xs; x <= xe;) {
            if (!LIBRE(x)) { x++; while (x <= xe && !LIBRE(x)) x++; continue; }
            int xr2 = x + 1;
#if ENG_RECT_OPT
            /* fin de la plage : quatre octets du masque d'un coup tant qu'aucun des quatre pixels n'est peint (libres sans test
               de profondeur ; 11 % de la 3D sur la LDK, pixel par pixel) ; mêmes plages */
            while (xr2 <= xe && (xr2 & 3) && LIBRE(xr2)) xr2++;
            if (!(xr2 & 3)) while (xr2 + 3 <= xe) { uint32_t w4; memcpy(&w4, crow + xr2, 4); if (w4 & 0x01010101u) break; xr2 += 4; }
            while (xr2 <= xe && LIBRE(xr2)) xr2++;
#else
            while (xr2 <= xe && LIBRE(xr2)) xr2++;
#endif
            const int xend = xr2 - 1;
            K->prof[PROF_NRUNS]++;
            const uint32_t pr0 = PROF_T();
            const float ox = x + 0.5f - a->x, lx = x + 0.5f - xl;
            const float u0 = ua + dudx * ox + dudy * oy, v0 = va + dvdx * ox + dvdy * oy;
            int32_t L16 = (int32_t)((lofs + ll + dldx * lx) * 65536.0f);
            int32_t F16 = (int32_t)((fl + dfdx * lx) * 65536.0f);
            if (F16 < 0) F16 = 0;
            uint16_t *o = orow + x;
            uint8_t *cv = crow + x;
            int32_t D8 = (int32_t)((wy + dwdx * x) * ZK * 256.0f);
            if (affine) {
                int32_t U = (int32_t)(u0 * 65536.0f), V = (int32_t)(v0 * 65536.0f);
                if (simple) {
                    const int nr = xend - x + 1;
#ifdef ENG_COMPTE
                    { extern unsigned long eng_px_aff, eng_px_mini; eng_px_aff += nr; if (adU > 98304 || adU < -98304 || adV > 98304 || adV < -98304) eng_px_mini += nr; }
#endif
                    px += alpha ? plage_simple(o, cv, x, nr, tex, sh, m, mv, U, V, adU, adV, L16, dL16, F16, dF16, bl, bf, cm, 1, swp, K, zrow + x, D8, dD8, !dith)
                                : plage_simple(o, cv, x, nr, tex, sh, m, mv, U, V, adU, adV, L16, dL16, F16, dF16, bl, bf, cm, 0, swp, K, zrow + x, D8, dD8, !dith);
                } else {
#ifdef ENG_COMPTE
                    { extern unsigned long eng_px_lent, eng_px_trans; eng_px_lent += xend - x + 1; if (trans) eng_px_trans += xend - x + 1; }
#endif
                    for (int xx = x; xx <= xend; xx++) {
                        int id = tex[(((V >> 16) & mv) << sh) | ((U >> 16) & m)];
                        int ok = (id || !alpha) && (trans || ((keep >> (xx & 3)) & 1));
                        if (verre) {
                            if (!(*cv >> 2) && L16 > 0) *cv = (uint8_t)(*cv | ((id == tid[0] ? tr[0] : id == tid[1] ? tr[1] : tc) << 2));
                            o++; cv++; U += adU; V += adV; L16 += dL16; F16 += dF16; D8 += dD8;
                            continue;
                        }
                        /* translucide (fondu_px) : face en fondu, ou corps de l'eau (opacité de sa profondeur, opaque au-delà ; reflets opaques) */
                        int ta = trans ? 16 - fade : 0, garde = (keep >> (xx & 3)) & 1;
                        if (surf) { ok = L16 > 0; garde = (L16 >> 4) > bay[(xx + 1) & 3]; if (!id) { const int ga = L16 >> 16; if (ga < eau_min) ok = 0; id = corps; ta = ga >= 16 ? 0 : ga < 1 ? 1 : ga; } else { const int ra = L16 >> 14; if (ra < 16) { ta = ra < 1 ? 1 : ra; garde = (L16 >> 2) > bay[(xx + 3) & 3]; } } }
                        if (ok) {
                            int L = surf ? lsurf : (L16 + (dith ? bay[xx & 3] : 0)) >> 16;
                            L = L < 0 ? 0 : L > 15 ? 15 : L;
                            int F = (F16 + (dith ? bay[(xx + 2) & 3] - 32768 : 0)) >> 16;   /* brouillard tramé : pas de marches entre ses 8 niveaux */
                            F = F < 0 ? 0 : F > 7 ? 7 : F;
                            uint16_t cc = cm[(((L << 3) + F) << 8) | id];
                            if (!dith && F < 7 && F16 > 0) { const int fq = (F16 >> 12) & 15; if (fq) cc = mele565(cc, cm[(((L << 3) + F + 1) << 8) | id], fq, swp); }
                            if (ta) px += fondu_px(K, o, cv, &zrow[xx], D8, cc, ta, garde, swp, trans);
                            else { *o = *cv ? attente565(K, cc, *cv, swp, (size_t)(cv - K->cov)) : cc; *cv = 1; zrow[xx] = (zcle_t)(D8 >> 8); px++; }
                        }
                        o++; cv++; U += adU; V += adV; L16 += dL16; F16 += dF16; D8 += dD8;
                    }
                }
            } else {
                float qu = u0, qv = v0, ww = a->w + dwdx * ox + dwdy * oy;
                float r = 1.0f / ww;
                int32_t U = (int32_t)(qu * r * 65536.0f), V = (int32_t)(qv * r * 65536.0f);
                for (int xx = x; xx <= xend;) {
                    /* Segment de n pixels : texture exacte aux deux bouts, linéaire entre les deux */
                    int nseg = xend - xx + 1; if (nseg > SUB) nseg = SUB;
                    qu += dudx * nseg; qv += dvdx * nseg; ww += dwdx * nseg;
                    r = 1.0f / ww;
                    int32_t U2 = (int32_t)(qu * r * 65536.0f), V2 = (int32_t)(qv * r * 65536.0f);
                    int32_t dU = div_seg(U2 - U, nseg), dV = div_seg(V2 - V, nseg);
                    segs++;
#ifdef ENG_COMPTE
                    { extern unsigned long eng_px_persp, eng_px_mini, eng_px_lent; eng_px_persp += nseg; if (!simple) eng_px_lent += nseg; if (dU > 98304 || dU < -98304 || dV > 98304 || dV < -98304) eng_px_mini += nseg; }
#endif
                    if (simple) {
                        px += alpha ? plage_simple(o, cv, xx, nseg, tex, sh, m, mv, U, V, dU, dV, L16, dL16, F16, dF16, bl, bf, cm, 1, swp, K, zrow + xx, D8, dD8, !dith)
                                    : plage_simple(o, cv, xx, nseg, tex, sh, m, mv, U, V, dU, dV, L16, dL16, F16, dF16, bl, bf, cm, 0, swp, K, zrow + xx, D8, dD8, !dith);
                        o += nseg; cv += nseg; xx += nseg; L16 += dL16 * nseg; F16 += dF16 * nseg; D8 += dD8 * nseg;
                    } else {
                        for (int k = 0; k < nseg; k++, xx++) {
                            int id = tex[(((V >> 16) & mv) << sh) | ((U >> 16) & m)];
                            int ok = (id || !alpha) && (trans || ((keep >> (xx & 3)) & 1));
                            if (verre) {
                                if (!(*cv >> 2) && L16 > 0) *cv = (uint8_t)(*cv | ((id == tid[0] ? tr[0] : id == tid[1] ? tr[1] : tc) << 2));
                                o++; cv++; U += dU; V += dV; L16 += dL16; F16 += dF16; D8 += dD8;
                                continue;
                            }
                            int ta = trans ? 16 - fade : 0, garde = (keep >> (xx & 3)) & 1;
                            if (surf) { ok = L16 > 0; garde = (L16 >> 4) > bay[(xx + 1) & 3]; if (!id) { const int ga = L16 >> 16; if (ga < eau_min) ok = 0; id = corps; ta = ga >= 16 ? 0 : ga < 1 ? 1 : ga; } else { const int ra = L16 >> 14; if (ra < 16) { ta = ra < 1 ? 1 : ra; garde = (L16 >> 2) > bay[(xx + 3) & 3]; } } }
                            if (ok) {
                                int L = surf ? lsurf : (L16 + (dith ? bay[xx & 3] : 0)) >> 16;
                                L = L < 0 ? 0 : L > 15 ? 15 : L;
                                int F = (F16 + (dith ? bay[(xx + 2) & 3] - 32768 : 0)) >> 16;
                                F = F < 0 ? 0 : F > 7 ? 7 : F;
                                uint16_t cc = cm[(((L << 3) + F) << 8) | id];
                                if (!dith && F < 7 && F16 > 0) { const int fq = (F16 >> 12) & 15; if (fq) cc = mele565(cc, cm[(((L << 3) + F + 1) << 8) | id], fq, swp); }
                                if (ta) px += fondu_px(K, o, cv, &zrow[xx], D8, cc, ta, garde, swp, trans);
                                else { *o = *cv ? attente565(K, cc, *cv, swp, (size_t)(cv - K->cov)) : cc; *cv = 1; zrow[xx] = (zcle_t)(D8 >> 8); px++; }
                            }
                            o++; cv++; U += dU; V += dV; L16 += dL16; F16 += dF16; D8 += dD8;
                        }
                    }
                    U = U2; V = V2;
                }
            }
            pix += PROF_T() - pr0;
#if ENG_DOF
            { uint8_t *dr = DEP(y); for (int k = x; k <= xend; k++) if ((crow[k] & 1) && dr[k] == 255) dr[k] = pdep; }
#endif
            x = xr2;
        }
        #undef LIBRE
    }
    K->prof[PROF_PIX] += pix;
    PROF_ADD(PROF_ROWS, pt1);
    K->px += px; K->segs += segs;
}

static void draw_poly(worker_t *K, const prim_t *p) {
    const world_t *w = &E.w;
    int ground = p->tile >= w->g0;
    const int big = p->tile >= TILE_BIG && p->tile < TILE_LOD;
    const uint8_t *tex = p->tile >= TILE_LOD ? w->lodbuf[(p->tile - TILE_LOD) >> 6] + ((p->tile - TILE_LOD) & 63) * 1024
                       : ground ? ground_tex(p->tile - (int)w->g0) : w->tex + p->tile * 256;
    int sh = ground ? 5 : 4, m = ground ? 31 : 15, mv = m;
    if (big) {   /* grand décor à sa taille rognée : lignes de 2^sh octets */
        const uint32_t k = (uint32_t)(p->tile - TILE_BIG);
        if (k >= w->deco_n) return;
        const uint8_t *d = w->deco + k * 12;
        tex = w->tex + w->big_off + (d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24));
        sh = d[8]; m = (1 << sh) - 1; mv = (1 << d[9]) - 1;
        if (k >= w->defi_k0 && k <= w->defi_k1)   /* bandeau défilant d'une enseigne : sa texture en mémoire (defi_maj) */
            for (int i = 0; i < w->defi_n; i++) if (w->defi[i].k == k) { tex = w->defi_buf + (size_t)i * (size_t)(w->defi_w * w->defi_h); break; }
    }
    const sv_t *v = &RF->verts[p->v0];
    raster_poly(K, v, p->nv, tex, sh, m, mv, p->alpha, p->fade, p->remap);
}

/* Pixels du sprite de Sera dans la bande : ceux déjà couverts par plus proche (dessin de l'avant vers le fond) sont ses
   parties cachées, qui deviennent sa silhouette (sil_v = 0) */

ENG_HOT static void draw_sprite(worker_t *K, const prim_t *p) {
    const world_t *w = &E.w;
    /* Planche : commune (fw × fh, largeur sw) ou propre (SPRS) ; blk : les lignes de la direction p->row (planche lue à
       la demande : présente, add_character l'a demandée pour cette image) */
    const sheet_t *own = p->sheet ? &w->sheets[p->sheet - 1] : NULL;
    const int fw = own ? own->fw : w->fw, fh = own ? own->fh : w->fh, sw = own ? own->fw * own->frames : w->sw;
    const uint8_t *blk = own ? sheet_block(p->sheet - 1, p->row, 0) : w->sheet + (size_t)p->row * fh * sw;
    if (!blk) return;
    float s = p->s, wd = fw * s, ht = fh * s;
    int x0 = (int)floorf(p->sx - wd / 2 + 0.5f), y0 = (int)floorf(p->sy - ht + 0.5f);
    int x1 = (int)floorf(p->sx + wd / 2 + 0.5f), y1 = (int)floorf(p->sy + 0.5f);
    int row_base = ((p->L << 3) + p->F) << 8;
    const uint8_t *remap = w->remap[p->remap];
    /* Variante de couleur d'une tenue : sa table sur les pixels du tissu (TIS2 : boîte de la case, un bit par pixel de la boîte) */
    const uint8_t *tissu = p->tissu ? own->tissu : NULL, *vrm = tissu ? w->remap[p->tissu] : NULL, *tbits = NULL;
    int tx0 = 0, ty0 = 0, tw = 0, th = 0;
    if (tissu) {
        const uint8_t *tc = tissu + ((size_t)p->row * own->frames + p->col) * 8;
        tx0 = tc[0]; ty0 = tc[1]; tw = tc[2]; th = tc[3];
        tbits = tissu + TISSU_TABLE(own->frames) + (tc[4] | ((uint32_t)tc[5] << 8) | ((uint32_t)tc[6] << 16) | ((uint32_t)tc[7] << 24));
    }
    const int fant = p->teinte == TEINTE_FANTOME;   /* fantôme du contre-la-montre : un pixel sur deux, bleuté */
    const uint32_t au = fant ? 0x60D8FFu : p->teinte ? eveil_aura(p->teinte - 1) : 0;   /* transformé : couleur de l'aura */
    const int ar = au >> 16 & 255, ag = au >> 8 & 255, ab = au & 255;
    const int zt = RF->ztest, zs = p->tile * ZS;   /* clé de profondeur des pieds */
    const int ile = E.w.ile[0] != 0;   /* île : la silhouette de Sera ne compte pas les pixels qu'un autre personnage lui cache (persoVu de l'émulateur de l'île) */
    /* Réfraction (île) : sous la ligne d'eau yw, les lignes de la planche lues plus vite (tassé vers la surface), la couleur mêlée à l'eau */
    const int yw = p->v0 && (int)p->v0 - 1 < y1 ? (int)p->v0 - 1 : 0x7FFF, y1e = yw < y1 ? yw + (int)lroundf((y1 - yw) * ILE_REFRAC_K) : y1;
    int ys = y0 > K->by0 ? y0 : K->by0, ye = y1e < K->by1 ? y1e : K->by1;
    if (p->ry > 0 && ye > (int)lroundf(p->ry)) ye = (int)lroundf(p->ry);
    /* Colonne de la planche sous chaque colonne de l'écran : la même division, une fois par colonne au lieu d'une par
       pixel (division en logiciel sur l'ESP32) */
    const int cx0 = x0 > 0 ? x0 : 0, cx1 = x1 - 1 < RW - 1 ? x1 - 1 : RW - 1;
    int16_t colx[RW_MAX];
    if (ys < ye) for (int x = cx0; x <= cx1; x++) { const int sx = (int)((x - x0) / s); colx[x - cx0] = (int16_t)(sx > fw - 1 ? fw - 1 : sx); }
    for (int y = ys; y < ye; y++) {
        const int dessous = y >= yw;
        int sy = dessous ? (int)((yw - y0) / s + (y - yw) / (s * ILE_REFRAC_K)) : (int)((y - y0) / s); if (sy > fh - 1) sy = fh - 1;
        const int tl = sy - ty0, tok = tl >= 0 && tl < th, tro = tl * tw - tx0;   /* ligne du masque du tissu, s'il y en a une ici */
        const uint8_t *src = blk + sy * sw + p->col * fw;
        int xs = x0 > span_l[y] ? x0 : span_l[y], xe = x1 - 1 < span_r[y] ? x1 - 1 : span_r[y];
        for (int x = xs; x <= xe; x++) {
            const int sx = colx[x - cx0];
            int id = src[sx];
            if (!id || (fant && ((x + y) & 1))) continue;
            const int ti = tro + sx;
            uint16_t c = cmap[row_base | (tok && sx >= tx0 && sx < tx0 + tw && (tbits[ti >> 3] >> (ti & 7) & 1) ? vrm[id] : remap[id])];
            if (au) c = teinte565(c, ar, ag, ab);
            if (dessous) {   /* sous l'eau : mêlé à la couleur de l'eau */
                const uint16_t c1 = E.cfg.swap565 ? (uint16_t)((c >> 8) | (c << 8)) : c;
                const int r = (c1 >> 11) << 3, g = (c1 >> 5 & 63) << 2, b = (c1 & 31) << 3;
                c = pack565((r * (16 - ILE_EAU_MELE) + ILE_EAU_R * ILE_EAU_MELE) >> 4, (g * (16 - ILE_EAU_MELE) + ILE_EAU_G * ILE_EAU_MELE) >> 4, (b * (16 - ILE_EAU_MELE) + ILE_EAU_B * ILE_EAU_MELE) >> 4);
            }
            int o = (y - K->by0) * K->bstride + x - K->bx0;
            const int hidden = (K->cov[o] & 1) && !(zt && zs > Z_PLUS(K->zb[o]));   /* peint par plus proche que ses pieds */
            if (!hidden) {
                K->band[o] = K->cov[o] ? attente565(K, c, K->cov[o], E.cfg.swap565, (size_t)o) : c; K->cov[o] = 1; K->zb[o] = (zcle_t)zs; K->px++;
#if ENG_DOF
                K->dep[o] = p->zd;
#endif
                if (ile && !p->sera) K->per[o >> 3] |= (uint8_t)(1u << (o & 7));
            }
            else if (!p->sera) continue;
            else if (ile && (K->per[o >> 3] >> (o & 7) & 1)) continue;   /* caché par un personnage, pas par le décor : ni silhouette */
            if (p->sera && K->sil_n < SIL_MAX) {
                /* Bord : un voisin transparent dans la planche */
                int edge = sx == 0 || sx == fw - 1 || sy == 0 || sy == fh - 1 || !src[sx - 1] || !src[sx + 1]
                    || !blk[(sy - 1) * sw + p->col * fw + sx]
                    || !blk[(sy + 1) * sw + p->col * fw + sx];
                K->sil_o[K->sil_n] = (uint16_t)o; K->sil_v[K->sil_n] = hidden ? 0 : 1; K->sil_e[K->sil_n] = (uint8_t)edge; K->sil_n++;
            }
        }
    }
}

/* Anneau au sol : ce qui est déjà peint devant lui (le personnage, dessiné avant) le cache ; un trait plein, et autour un trait tramé pour la lueur */
#define ANNEAU_TRAIT 2.5f
#define ANNEAU_LUEUR 2.5f
static void draw_anneau(worker_t *K, const prim_t *p) {
    const float sx = p->sx, sy = p->sy, rx = p->s, ry = p->ry;
    const float t = ANNEAU_TRAIT / ENG_SCALE, l = ANNEAU_LUEUR / ENG_SCALE, rxo = rx + l, ryo = ry + l * 0.6f, rxi = rx - t, ryi = ry - t * 0.6f;
    const uint16_t c = (uint16_t)p->v0;
    const int zt = RF->ztest, zs = p->tile * ZS;
    int ys = (int)floorf(sy - ryo), ye = (int)ceilf(sy + ryo);
    if (ys < K->by0) ys = K->by0;
    if (ye > K->by1 - 1) ye = K->by1 - 1;
    for (int y = ys; y <= ye; y++) {
        const float dy = y + 0.5f - sy, ao = 1 - dy * dy / (ryo * ryo);
        if (ao <= 0) continue;
        const float am = 1 - dy * dy / (ry * ry), ai = ryi > 0 ? 1 - dy * dy / (ryi * ryi) : 0;
        const float hwo = rxo * sqrtf(ao), hwm = am > 0 ? rx * sqrtf(am) : 0, hwi = ai > 0 ? rxi * sqrtf(ai) : 0;
        int xs = (int)ceilf(sx - hwo), xe = (int)floorf(sx + hwo);
        if (xs < span_l[y]) xs = span_l[y];
        if (xe > span_r[y]) xe = span_r[y];
        for (int x = xs; x <= xe; x++) {
            const float ax = fabsf(x + 0.5f - sx);
            if (ax < hwi || (ax >= hwm && ((x + y) & 1))) continue;
            const int o = (y - K->by0) * K->bstride + x - K->bx0;
            if ((K->cov[o] & 1) && !(zt && zs > Z_PLUS(K->zb[o]))) continue;
            K->band[o] = K->cov[o] ? attente565(K, c, K->cov[o], E.cfg.swap565, (size_t)o) : c; K->cov[o] = 1; K->zb[o] = (zcle_t)zs; K->px++;
        }
    }
}

static void draw_shadow(worker_t *K, const prim_t *p) {
    float sx = p->sx, sy = p->sy, rx = p->s, ry = p->ry;
    int ys = (int)floorf(sy - ry), ye = (int)ceilf(sy + ry);
    if (ys < K->by0) ys = K->by0;
    if (ye > K->by1 - 1) ye = K->by1 - 1;
    for (int y = ys; y <= ye; y++) {
        float dy = (y + 0.5f - sy) / ry;
        if (fabsf(dy) >= 1) continue;
        float hw = rx * sqrtf(1 - dy * dy);
        int xs = (int)ceilf(sx - hw), xe = (int)floorf(sx + hw);
        if (xs < span_l[y]) xs = span_l[y];
        if (xe > span_r[y]) xe = span_r[y];
        uint8_t *cv = COV(y);
        for (int x = xs; x <= xe; x++) if (!cv[x]) cv[x] = 2;
    }
}

/* Ciel : dégradé, silhouette de ville qui tourne avec la caméra, ligne d'horizon */
/* Pluie : traits obliques de len pixels et éclaboussures (len 0, trois pixels) mêlés à moitié à un gris bleuté, par-dessus
   tout le reste de la bande (dessinés à la fin, après le ciel) */
/* Pluie (de cendre, sur la planète) : chaque pixel touché à mi-chemin du gris de la cendre. Moyenne dans l'ordre normal
   des octets : l'écran de la montre les reçoit inversés (swap565), et la moyenne par masque d'un pixel inversé mêlait les
   bits d'une couleur à l'autre (pluie multicolore). Gris de cendre : RAIN_GRIS de l'émulateur */
static void draw_rain(worker_t *K) {
    const int sw = E.cfg.swap565;
    const uint16_t rc = (uint16_t)(((180 & 0xF8) << 8) | ((176 & 0xFC) << 3) | (168 >> 3));
    const uint16_t nc = (uint16_t)(((240 & 0xF8) << 8) | ((244 & 0xFC) << 3) | (250 >> 3));   /* flocon : blanc bleuté */
    for (int k = 0; k < RF->nrain; k++) {
        const int x0 = RF->drops[k].x, y0 = RF->drops[k].y, len = RF->drops[k].len, dx = RF->drops[k].dx;
        if (y0 >= K->by1 || y0 + (len < 0 ? -len : len ? len : 1) <= K->by0) continue;   /* hors de la bande (08/11/2026 : chaque bande parcourait toutes les lignes de toutes les gouttes) */
        if (len < 0) {   /* flocon de -len × -len pixels, aux trois quarts du blanc (deux moyennes) */
            for (int y = y0; y < y0 - len; y++) {
                if (y < K->by0 || y >= K->by1) continue;
                for (int x = x0; x < x0 - len; x++) {
                    if (x < span_l[y] || x > span_r[y]) continue;
                    uint16_t *o = &ROW(y)[x];
                    uint16_t c = sw ? (uint16_t)((*o >> 8) | (*o << 8)) : *o;
                    c = (uint16_t)(((c & 0xF7DE) >> 1) + ((nc & 0xF7DE) >> 1));
                    c = (uint16_t)(((c & 0xF7DE) >> 1) + ((nc & 0xF7DE) >> 1));
                    *o = sw ? (uint16_t)((c >> 8) | (c << 8)) : c;
                }
            }
            continue;
        }
        const int n = len ? len : 1;
        for (int i = 0; i < n; i++) {
            const int y = y0 + i;
            if (y < K->by0 || y >= K->by1) continue;
            const int xs = len ? x0 + (dx * i) / len : x0 - 1, xe = len ? xs : x0 + 1;
            for (int x = xs; x <= xe; x++) {
                if (x < span_l[y] || x > span_r[y]) continue;
                uint16_t *o = &ROW(y)[x];
                uint16_t c = sw ? (uint16_t)((*o >> 8) | (*o << 8)) : *o;
                c = (uint16_t)(((c & 0xF7DE) >> 1) + ((rc & 0xF7DE) >> 1));
                *o = sw ? (uint16_t)((c >> 8) | (c << 8)) : c;
            }
        }
    }
}

/* Pixels libres d'une ligne [x0, x1] : la couleur du fond (ou ce qui l'attend, attente565). Écran rectangulaire
   (ENG_RECT_OPT) : quatre octets du masque lus d'un coup, les groupes de quatre pixels déjà peints sautés (LDK : 4,4 % du
   temps de la 3D, pixel par pixel) ; mêmes pixels */
#if ENG_RECT_OPT
static inline void ciel_ligne(worker_t *K, uint16_t *o, const uint8_t *cv, int x0, int x1, uint16_t col, int swp) {
    int x = x0;
    for (; x <= x1 && (x & 3); x++) if (!(cv[x] & 1)) o[x] = cv[x] ? attente565(K, col, cv[x], swp, (size_t)(&cv[x] - K->cov)) : col;
    for (; x + 3 <= x1; x += 4) {
        uint32_t w;
        memcpy(&w, cv + x, 4);
        if ((w & 0x01010101u) == 0x01010101u) continue;
        for (int i = x; i < x + 4; i++) if (!(cv[i] & 1)) o[i] = cv[i] ? attente565(K, col, cv[i], swp, (size_t)(&cv[i] - K->cov)) : col;
    }
    for (; x <= x1; x++) if (!(cv[x] & 1)) o[x] = cv[x] ? attente565(K, col, cv[x], swp, (size_t)(&cv[x] - K->cov)) : col;
}
#endif
#if ENG_ROULIS
/* Ciel de la caméra penchée : chaque pixel libre ramené dans l'image non penchée (rotation inverse autour du centre de
   projection) : couleur de sa ligne (ciel_table), panorama de l'horizon à sa colonne et sa ligne (hz_u, débordant de AG_ROUL_M) */
static void ciel_roule(worker_t *K) {
    const int swp = E.cfg.swap565, M = AG_ROUL_M;
    const float c = RF->roul_c, s = RF->roul_s, px = RF->roul_x, py = RF->roul_y, hy = RF->sky_hy;
    const int xa = K->bx0, xb = K->bx0 + K->bstride - 1;
    for (int y = K->by0; y < K->by1; y++) {
        uint16_t *o = ROW(y);
        const uint8_t *cv = COV(y);
        const int x0 = span_l[y] > xa ? span_l[y] : xa, x1 = span_r[y] < xb ? span_r[y] : xb;
        const float dy = (float)y + 0.5f - py, dx0 = (float)x0 + 0.5f - px;
        const float sx0 = px + dx0 * c - dy * s + (float)M, sy0 = py + dx0 * s + dy * c + (float)M;   /* (+ M : toujours positifs) */
        /* en 16.16 : point de l'image non penchée, ligne du panorama (14/11/2026 : en flottants, 4 % du temps de la LDK) */
        int32_t SX = (int32_t)(sx0 * 65536), SY = (int32_t)(sy0 * 65536);
        const int32_t dSX = (int32_t)(c * 65536), dSY = (int32_t)(s * 65536);
        int32_t R = RF->hz_px ? (int32_t)(((float)RF->hz_base - (hy + (float)M - sy0) * RF->hz_k) * 65536) : 0;
        const int32_t dR = RF->hz_px ? (int32_t)(s * RF->hz_k * 65536) : 0, RH16 = RF->hz_px ? RF->hz_h << 16 : 0;
        for (int x = x0; x <= x1; x++, SX += dSX, SY += dSY, R += dR) {
            if (cv[x] & 1) continue;
            int iy = (SY >> 16) - M, ix = (SX >> 16) - M;
            iy = iy < -M ? -M : iy >= RH + M ? RH + M - 1 : iy;
            ix = ix < -M ? -M : ix >= RW + M ? RW + M - 1 : ix;
            uint16_t col = RF->ciel_tab[iy];
            if (RF->hz_px) {
                if (R >= 0 && R < RH16) {
                    const int ri = R >> 16, d = RF->hz_base - ri, bnd = d < 4 ? 3 : d < 12 ? 2 : d < 28 ? 1 : 0;
                    const uint8_t q = RF->hz_px[(size_t)ri * (size_t)RF->hz_w + RF->hz_u[ix]];
                    if (q) col = RF->hz_pal[bnd * 256 + q];
                }
            }
            o[x] = cv[x] ? attente565(K, col, cv[x], swp, (size_t)(&cv[x] - K->cov)) : col;
        }
    }
}
#endif
ENG_HOT static void draw_sky(worker_t *K) {
    const camera_t *cam = &E.cam;
    const int swp = E.cfg.swap565;   /* couleurs en attente (faces en fondu) mêlées au ciel */
    float hy = RF->sky_hy;
    (void)cam;
    if (RF->inside || (RF->ciel && RF->ciel != 3)) {   /* intérieur : fond sombre (INT_FOND) ; souterrain : son brouillard ; espace : étoiles */
        const uint16_t col = RF->inside ? pack565((int)INT_FOND[0], (int)INT_FOND[1], (int)INT_FOND[2]) : pack565((int)amb.fog[0], (int)amb.fog[1], (int)amb.fog[2]);
        for (int y = K->by0; y < K->by1; y++) {
            uint16_t *o = ROW(y);
            const uint8_t *cv = COV(y);
#if ENG_RECT_OPT
            ciel_ligne(K, o, cv, span_l[y], span_r[y], col, swp);
#else
            for (int x = span_l[y]; x <= span_r[y]; x++) if (!(cv[x] & 1)) o[x] = cv[x] ? attente565(K, col, cv[x], swp, (size_t)(&cv[x] - K->cov)) : col;
#endif
        }
        if (!RF->inside && RF->ciel == 2)   /* étoiles fixes dans le ciel, qui tournent avec le lacet (drawSky de l'émulateur) */
            for (int k = 0; k < 160; k++) {
                float ang = fmodf(k * 2.399963f - cam->yaw, 2 * PI_F); if (ang < 0) ang += 2 * PI_F;
                const int x = (int)lroundf(ang / (2 * PI_F) * 720) - 120, y = (k * 37) % RH;
                if (x < 0 || x >= RW || y < K->by0 || y >= K->by1 || x < span_l[y] || x > span_r[y] || (COV(y)[x] & 1)) continue;
                ROW(y)[x] = k % 7 ? pack565(200, 205, 230) : pack565(255, 240, 200);
            }
        return;
    }
#if ENG_ROULIS
    if (RF->roule && RF->ciel_tab) { ciel_roule(K); return; }
#endif
    uint16_t fog = pack565((int)amb.fog[0], (int)amb.fog[1], (int)amb.fog[2]);
    for (int y = K->by0; y < K->by1; y++) {
        uint16_t col;
        if (y < hy) {
            float t = 1 - (hy - y) / (hy + 20);
            if (t < 0) t = 0; else if (t > 1) t = 1;
            const float k = 1 - RF->rain * 0.6f, g = RF->rain * 0.6f * 40;   /* pluie : ciel gris */
            col = pack565((int)((amb.top[0] + t * t * (amb.hor[0] - amb.top[0])) * k + g), (int)((amb.top[1] + t * (amb.hor[1] - amb.top[1])) * k + g),
                          (int)((amb.top[2] + t * (amb.hor[2] - amb.top[2])) * k + g * 1.2f));
        } else {
            /* Sous l'horizon : un pixel que rien ne couvre est un trou (case cachée sous un bâtiment que son emprise ne
               couvre pas, face écartée par la profondeur) s'il est en deçà du sol dessiné, noir jusqu'au brouillard, puis
               de la couleur du brouillard comme le sol qui l'entoure ; au-delà (vue qui s'arrête), le brouillard. Plan du
               sol le plus bas de l'image (sol_bas) : la ligne qui voit au-delà du sol dessiné ne rencontre ce plan qu'au
               loin, jamais de noir là où le relief descend (fond des vallées). drawSky de l'émulateur (08/11/2026,
               demande de l'utilisateur : carrés blancs sous les bâtiments) */
            col = fog;
            const float d = y + 0.5f - hy;
            if (RF->trou_hk > 0 && d > 0) {
                const float f = (RF->trou_hk / d - RF->trou_z0) * RF->trou_k;
                if (f < 1) { const float g = f > 0 ? f : 0; col = pack565((int)(amb.fog[0] * g), (int)(amb.fog[1] * g), (int)(amb.fog[2] * g)); }
            }
        }
        uint16_t *o = ROW(y);
        const uint8_t *cv = COV(y);
#if ENG_RECT_OPT
        ciel_ligne(K, o, cv, span_l[y], span_r[y], col, swp);
#else
        for (int x = span_l[y]; x <= span_r[y]; x++) if (!(cv[x] & 1)) o[x] = cv[x] ? attente565(K, col, cv[x], swp, (size_t)(&cv[x] - K->cov)) : col;
#endif
        /* Horizon de la course anti-gravité : la ligne du panorama de cette ligne de l'image, voilée d'autant plus qu'elle est
           près de l'horizon (4 bandes) ; ses pixels vides laissent le ciel */
        if (RF->hz_px) {
            const int ri = (int)floorf((float)RF->hz_base - (hy - ((float)y + 0.5f)) * RF->hz_k);
            if (ri >= 0 && ri < RF->hz_h) {
                const int d = RF->hz_base - ri, bnd = d < 4 ? 3 : d < 12 ? 2 : d < 28 ? 1 : 0;
                const uint16_t *pal = RF->hz_pal + bnd * 256;
                const uint8_t *ligne = RF->hz_px + (size_t)ri * (size_t)RF->hz_w;
                for (int x = span_l[y]; x <= span_r[y]; x++) if (!(cv[x] & 1)) { const uint8_t ix = ligne[RF->hz_u[x]]; if (ix) o[x] = pal[ix]; }
            }
        }
    }
    if (RF->hz_px) return;   /* course anti-gravité : pas de ligne néon */
    if (RF->ciel == 3) return;   /* autre planète : pas de ligne néon */
    const float nu = amb.nuit;
    const int hyr = (int)floorf(hy + 0.5f);
    if (nu > 0.5f && hyr >= K->by0 && hyr < K->by1) {   /* ligne d'horizon néon, la nuit */
        uint16_t mag = pack565(255, 45, 155);
        for (int x = span_l[hyr]; x <= span_r[hyr]; x++) if (!(COV(hyr)[x] & 1)) ROW(hyr)[x] = mag;
    }
}

/* Lampe torche (objet LAMPE TORCHE du sac) : dans le noir (nuit, souterrain, intérieur éteint), les pixels autour de Sera
   éclaircis en dégradé chaud, gain TORCHE_GAIN au centre, nul au bord d'un disque de rayon TORCHE_R du plus petit côté
   de l'image (torcheImage de l'émulateur) */
#define TORCHE_R 0.26f
#define TORCHE_GAIN 1.3f
static int torche_on(void) {
    static int k = -2;
    if (k == -2 || (k >= 0 && (k >= n_items || strcmp(ITEMS[k].name, "LAMPE TORCHE")))) {
        k = -1;
        for (int i = 0; i < n_items; i++) if (ITEMS[i].name && !strcmp(ITEMS[i].name, "LAMPE TORCHE")) { k = i; break; }
    }
    if (k < 0 || ITEMS[k].count <= 0) return 0;
    return E.in.on ? E.in.sombre : E.w.ciel == 1 || amb.nuit > 0.5f;
}
static void torche_band(worker_t *K) {
    const float cx = RF->torche_x, cy = RF->torche_y, r = TORCHE_R * (RW < RH ? RW : RH), r2 = r * r;
    const int sw = E.cfg.swap565;
    int ya = (int)floorf(cy - r), yb = (int)ceilf(cy + r);
    if (ya < K->by0) ya = K->by0;
    if (yb > K->by1) yb = K->by1;
    for (int y = ya; y < yb; y++) {
        const float dy = y + 0.5f - cy, hw2 = r2 - dy * dy;
        if (hw2 <= 0) continue;
        const float hw = sqrtf(hw2);
        int xa = (int)floorf(cx - hw), xb = (int)ceilf(cx + hw);
        if (xa < K->bx0) xa = K->bx0;
        if (xb > K->bx0 + K->bstride) xb = K->bx0 + K->bstride;
        uint16_t *row = K->band + (size_t)(y - K->by0) * K->bstride - K->bx0;
        for (int x = xa; x < xb; x++) {
            const uint16_t c0 = row[x], c = sw ? (uint16_t)((c0 >> 8) | (c0 << 8)) : c0;   /* octets inversés : swap565 */
            if (!c) continue;
            const float dx = x + 0.5f - cx, t = 1 - (dx * dx + dy * dy) / r2;
            if (t <= 0) continue;
            const int g = (int)(t * t * TORCHE_GAIN * 256);   /* gain au-dessus de 1, en 1/256 */
            int R = c >> 11, G = (c >> 5) & 63, Bc = c & 31;
            R += (R * (g + (g * 31 >> 8))) >> 8; G += (G * g) >> 8; Bc += (Bc * (g - (g >> 2))) >> 8;   /* chaud : +12 % de rouge, -25 % de bleu */
            const uint16_t o = (uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (Bc > 31 ? 31 : Bc));
            row[x] = sw ? (uint16_t)((o >> 8) | (o << 8)) : o;
        }
    }
}
/* Lueur des feux (section LUMI : décors sources de feu, tirés par l'émulateur de la table LUMIERES de js/decor.js) :
   dehors, dans le noir (même seuil que la lampe torche), les LUM_DRAW lumières de la carte les plus proches de Sera (à
   moins de LUM_DIST cases) éclaircissent les pixels peints autour d'elles. Boule de rayon r autour de la flamme, distance
   mesurée dans le monde : écart à l'écran ramené à la profondeur de la lumière, écart de profondeur lu dans le tampon
   (zb) : un mur loin devant le feu n'est pas éclairé. Gain (1 - d² / r²)² × intensité × LUM_GAIN, plus LUM_ADD (sur
   255) pour que le noir prenne la couleur du feu ; scintillement tiré du temps et de la position (lumImage de
   l'émulateur). Entrée de LUMI : x u16, z u16 (1/8 de case), y i16 (1/64), carte u8, rayon u8 (1/16 de case), r, g, b u8,
   intensité u8. Seules comptent les lumières devant la caméra (depuis le 31/10/2026 : les lampadaires derrière elle
   prenaient des places) ; LUM_DRAW est plus haut (lum_vue_t) */
#define LUM_DIST 30.0f
#define LUM_GAIN 1.4f
/* Phares du véhicule du joueur (01/11/2026, demande de l'utilisateur ; phareImage de l'émulateur) : la nuit, un faisceau
   vers l'avant seulement, ellipsoïde allongé sur le cap du véhicule, centré PHARE_AV cases devant lui et PHARE_Y
   au-dessus de la piste (course) ou du sol, demi-axes PHARE_LONG le long et PHARE_LARGE de côté et en hauteur (il
   éclaire de PHARE_AV - PHARE_LONG à PHARE_AV + PHARE_LONG cases devant) ; blanc chaud, appliqué par lum_band */
#define PHARE_AV 3.2f
#define PHARE_LONG 3.6f
#define PHARE_LARGE 1.7f
#define PHARE_Y 0.35f
#define PHARE_GAIN 1.2f
static void phare_setup(float nf) {
    float x, y, z, d;
    if (CR.on && CR.n > 0) { const racer_t *j = &CR.r[CR.n - 1]; x = j->x; y = j->y; z = j->z; d = j->dir; }
    else if (E.ride >= 0) { x = E.sera.x; z = E.sera.z; y = ground_y(x, z); d = E.sera.dir; }
    else return;
    const float ax = sinf(d), az = cosf(d), cx = x + ax * PHARE_AV, cy = y + PHARE_Y, cz = z + az * PHARE_AV;
    const v3 o = to_view(cx, cy, cz), o2 = to_view(cx + ax, cy, cz + az);   /* to_view est une rotation : o2 - o est unitaire */
    if (o.z < NEAR_Z + 0.2f) return;
    lum_vue_t *L = &B->lum[B->nlum++];
    L->z = o.z; L->sx = pcx + o.x * FOCAL / o.z; L->sy = pcy - o.y * FOCAL / o.z;
    L->r = PHARE_LONG; L->rp = PHARE_LONG * FOCAL / o.z;
    L->g = PHARE_GAIN * nf; L->cr = 255; L->cg = 238; L->cb = 200;
    L->phare = 1; L->fx = o2.x - o.x; L->fy = o.y - o2.y; L->fz = o2.z - o.z;
    L->ia2 = 1.0f / (PHARE_LONG * PHARE_LONG); L->il2 = 1.0f / (PHARE_LARGE * PHARE_LARGE);
}
static void lum_setup(int bat) {
    const world_t *w = &E.w;
    B->nlum = 0;
    if (bat || E.in.on || !B->ztest || !(w->ciel == 1 || amb.nuit > 0.5f)) return;
    const float nf = w->ciel == 1 ? 1 : fminf(1, 0.4f + (amb.nuit - 0.5f) * 3), t = E.time_s;
    phare_setup(nf);
    if (!w->nlum) return;
    int idx[LUM_DRAW], n = 0;
    float dd[LUM_DRAW];
    for (int k = 0; k < w->nlum; k++) {
        const uint8_t *q = w->lum + k * 12;
        if (q[6] != w->carte) continue;
        const float x = (q[0] | q[1] << 8) / 8.0f, z = (q[2] | q[3] << 8) / 8.0f, d = hypotf(x - E.sera.x, z - E.sera.z);
        if (d > LUM_DIST || (n == LUM_DRAW && d >= dd[n - 1])) continue;
        if (to_view(x, (int16_t)(q[4] | q[5] << 8) / 64.0f, z).z < NEAR_Z + 0.2f) continue;   /* derrière la caméra */
        int j = n < LUM_DRAW ? n++ : n - 1;
        while (j > 0 && dd[j - 1] > d) { dd[j] = dd[j - 1]; idx[j] = idx[j - 1]; j--; }
        dd[j] = d; idx[j] = k;
    }
    for (int m = 0; m < n; m++) {
        const uint8_t *q = w->lum + idx[m] * 12;
        const float x = (q[0] | q[1] << 8) / 8.0f, z = (q[2] | q[3] << 8) / 8.0f, y = (int16_t)(q[4] | q[5] << 8) / 64.0f;
        const v3 o = to_view(x, y, z);
        if (o.z < NEAR_Z + 0.2f) continue;
        const float fl = 1 + 0.08f * sinf(t * 7.3f + x * 1.7f) + 0.05f * sinf(t * 13.1f + z * 2.3f);
        lum_vue_t *L = &B->lum[B->nlum++];
        L->z = o.z; L->sx = pcx + o.x * FOCAL / o.z; L->sy = pcy - o.y * FOCAL / o.z;
        L->r = q[7] / 16.0f * fl; L->rp = L->r * FOCAL / o.z;
        L->g = q[11] / 255.0f * LUM_GAIN * nf * fl; L->cr = q[8]; L->cg = q[9]; L->cb = q[10]; L->phare = 0;
    }
}
static void lum_band(worker_t *K) {
    const int sw = E.cfg.swap565;
    for (int n = 0; n < RF->nlum; n++) {
        const float sx = RF->lum[n].sx, sy = RF->lum[n].sy, zl = RF->lum[n].z, r = RF->lum[n].r, rp = RF->lum[n].rp, r2 = r * r, kk = zl / FOCAL;
        const float g0 = RF->lum[n].g;
        const int cr = RF->lum[n].cr, cg = RF->lum[n].cg, cb = RF->lum[n].cb, ph = RF->lum[n].phare;
        const float fx = RF->lum[n].fx, fy = RF->lum[n].fy, fz = RF->lum[n].fz, ia2 = RF->lum[n].ia2, il2 = RF->lum[n].il2;
        int ya = (int)floorf(sy - rp), yb = (int)ceilf(sy + rp);
        if (ya < K->by0) ya = K->by0;
        if (yb > K->by1) yb = K->by1;
        for (int y = ya; y < yb; y++) {
            const float dy = (y + 0.5f - sy) * kk;
            int xa = (int)floorf(sx - rp), xb = (int)ceilf(sx + rp);
            if (xa < K->bx0) xa = K->bx0;
            if (xb > K->bx0 + K->bstride) xb = K->bx0 + K->bstride;
            uint16_t *row = K->band + (size_t)(y - K->by0) * K->bstride - K->bx0;
            const uint8_t *crow = COV(y);
            const zcle_t *zrow = ZB(y);
            for (int x = xa; x < xb; x++) {
                if (!(crow[x] & 1) || !zrow[x]) continue;   /* ciel, rien de peint : rien à éclairer */
                { const size_t q = (size_t)(crow + x - K->cov); if (K->fon[q >> 3] & (1u << (q & 7))) continue; }   /* face en fondu : pas de damier éclairci */
                const float dx = (x + 0.5f - sx) * kk, dz = ZK / zrow[x] - zl, d2 = dx * dx + dy * dy + dz * dz;
                float tt;
                if (ph) { const float a = dx * fx + dy * fy + dz * fz; tt = 1 - a * a * ia2 - (d2 - a * a) * il2; }   /* faisceau des phares */
                else tt = 1 - d2 / r2;
                if (tt <= 0) continue;
                const int g = (int)(tt * tt * g0 * 256);   /* gain en 1/256 */
                const uint16_t c0 = row[x], c = sw ? (uint16_t)((c0 >> 8) | (c0 << 8)) : c0;
                int R = c >> 11, G = (c >> 5) & 63, Bc = c & 31;
                R += (R * g * cr >> 16) + (g * cr * 6 >> 16); G += (G * g * cg >> 16) + (g * cg * 12 >> 16); Bc += (Bc * g * cb >> 16) + (g * cb * 6 >> 16);   /* LUM_ADD : 48 sur 255 */
                const uint16_t o = (uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (Bc > 31 ? 31 : Bc));
                row[x] = sw ? (uint16_t)((o >> 8) | (o << 8)) : o;
            }
        }
    }
}
/* Cadre du cockpit (ag_image) : ses pixels pleins, couverts et au plus près, avant la 3D ; la ligne sous son bas (secousse) répète
   la dernière */
#if ENG_PREMIUM
#define ZB_DEVANT ((zcle_t)0x3FFFFFFF)
#else
#define ZB_DEVANT ((zcle_t)0xFFFF)
#endif
static void ag_cockpit_band(worker_t *K) {
    const int xa = K->bx0, xb = K->bx0 + K->bstride - 1;
    for (int y = K->by0; y < K->by1; y++) {
        int sy = (int)floorf(((float)y + 0.5f - RF->ck_y) * RF->ck_k);
        if (sy < 0) continue;
        if (sy >= RF->ck_h) sy = RF->ck_h - 1;
        const uint8_t *src = RF->ck_px + (size_t)sy * (size_t)RF->ck_w;
        uint16_t *o = ROW(y);
        uint8_t *cv = COV(y);
        zcle_t *zr = ZB(y);
#if ENG_DOF
        uint8_t *dp = DEP(y);
#endif
        for (int x = xa; x <= xb; x++) {   /* (toute la ligne : les bornes de la ligne, la vitre, excluent le cadre) */
            const uint8_t ix = src[RF->ck_u[x]];
            if (!ix) continue;
            o[x] = RF->ck_pal[ix]; cv[x] = 1; zr[x] = ZB_DEVANT;
#if ENG_DOF
            dp[x] = RF->ck_dep;
#endif
        }
    }
}
static void render_band(worker_t *K, int y0, int h, uint16_t *dst, int x0, int w) {
    K->band = dst; K->by0 = y0; K->by1 = y0 + h; K->bx0 = x0; K->bstride = w;
    K->sil_n = 0;
    uint32_t pt = PROF_T();
    if (!ENG_RECT) memset(dst, 0, (size_t)w * h * 2);   /* hors du cercle : noir (pixels éteints) ; écran rectangulaire : draw_sky
                                                             peint tout pixel que rien ne couvre (1,5 ms par image sur la LDK) */
    memset(K->cov, 0, (size_t)w * h);
    memset(K->fon, 0, ((size_t)w * h + 7) / 8);
    if (E.w.ile[0]) memset(K->per, 0, ((size_t)w * h + 7) / 8);
#if ENG_DOF
    memset(K->dep, 255, (size_t)w * h);
#endif
    K->nteinte = 1;
    if (RF->ck_px) ag_cockpit_band(K);
    PROF_ADD(PROF_CLEAR, pt);
    int b = y0 / ENG_BAND, k0 = RF->bin_ok ? RF->bin_first[b] : 0, k1 = RF->bin_ok ? RF->bin_first[b + 1] : RF->norder;
    for (int k = k1 - 1; k >= k0; k--) {
        const prim_t *p = &RF->prims[RF->bin_ok ? RF->bin_idx[k] : RF->order[k]];
        if (p->maxy < K->by0 || p->miny >= K->by1) continue;
        if (p->kind == K_POLY) draw_poly(K, p);
        else if (p->kind == K_SPRITE) { pt = PROF_T(); draw_sprite(K, p); PROF_ADD(PROF_SPRITE, pt); }
        else if (p->kind == K_ANNEAU) draw_anneau(K, p);
        else { pt = PROF_T(); draw_shadow(K, p); PROF_ADD(PROF_SHADOW, pt); }
    }
    pt = PROF_T();
    draw_sky(K);
    PROF_ADD(PROF_SKY, pt);
    if (RF->nrain) draw_rain(K);
    if (RF->nlum) lum_band(K);
    if (RF->torche) torche_band(K);
    /* Silhouette de Sera là où le décor la cache (plus d'un dixième de ses pixels de la bande) */
    int hidden = 0;
    for (int i = 0; i < K->sil_n; i++) if (!K->sil_v[i]) hidden++;
    if (K->sil_n && hidden * 10 > K->sil_n) {
        uint32_t t = hud_theme_c1();
        int tr = t >> 16, tg = (t >> 8) & 255, tb = t & 255;
        uint16_t edge = pack565(tr, tg, tb), fill = pack565(tr * 3 / 10, tg * 3 / 10, tb * 3 / 10);
        for (int i = 0; i < K->sil_n; i++) {
            if (K->sil_e[i]) K->band[K->sil_o[i]] = edge;
            else if (!K->sil_v[i] && !RF->sera_faded) K->band[K->sil_o[i]] = fill;
        }
    }
}

#if ENG_SCALE == 2
/* Double h lignes internes dans la fenêtre native [x0, x1) (x0, x1 pairs) : chaque pixel écrit deux fois, chaque ligne deux fois */
static void double_band(const uint16_t *src, int h, uint16_t *dst, int x0, int x1) {
    int w = x1 - x0;
    for (int y = 0; y < h; y++) {
        const uint16_t *sr = src + y * RW + x0 / 2;
        uint32_t *d = (uint32_t *)(dst + 2 * y * w);
        for (int i = 0; i < w / 2; i++) { uint32_t c = sr[i]; d[i] = c | (c << 16); }
        memcpy(dst + (2 * y + 1) * w, dst + 2 * y * w, (size_t)w * 2);
    }
}
#endif

/* Bande de l'écran natif, rognée aux colonnes [x0, x1) : 3D (bande interne doublée, ou rendue directement en 466),
   puis interface. dst : (x1 - x0) × h pixels contigus, prêts à envoyer. */
/* L'image préparée devient celle que dessinent les bandes ; la suivante se prépare dans l'autre tampon */
void eng_present(void) {
    RF = B;
    B = B == &FR[0] ? &FR[1] : &FR[0];
    ui_present();
}

/* Statistiques de l'image présentée : préparation, plus les compteurs des deux cœurs */
const eng_stats_t *eng_stats(void) {
    static eng_stats_t out;
    out = RF->stats;
    out.px = out.segments = 0; out.us_3d = out.us_ui = 0;
    for (int k = 0; k < ENG_WORKERS; k++) { out.px += RF->px_w[k]; out.segments += RF->seg_w[k]; out.us_3d += RF->us3d_w[k]; out.us_ui += RF->usui_w[k]; }
    return &out;
}

/* Bande de l'écran natif, rognée aux colonnes [x0, x1), dessinée par le cœur worker (0 ou 1) : 3D (bande interne
   doublée, ou rendue directement en 466), puis interface. dst : (x1 - x0) × h pixels contigus, prêts à envoyer.
   Les deux cœurs peuvent dessiner chacun une bande différente de la même image en même temps. */
void eng_render_band_on(int worker, int y0, int h, uint16_t *dst, int x0, int x1) {
    worker %= ENG_WORKERS;
    worker_t *K = WK[worker];
    uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    K->px = K->segs = 0;
    if (RF->noir) memset(dst, 0, sizeof *dst * (size_t)(x1 - x0) * (size_t)h);   /* montre verrouillée : fond noir (AMOLED éteint) */
    else if (!ENG_DYN && !ENG_RECT && RF->ui_cover > 0 && y0 >= RF->ui_cover) {   /* (écran rectangulaire : la 3D déborde du panneau) */
        /* Sous le panneau opaque du dialogue : pas de 3D, la couleur du panneau (il est découpé au cercle par bande) */
        const uint16_t c = ui_native(ui_rgb(0x080612));
        for (int y = 0; y < h; y++) {
            const float d = y0 + y + 0.5f - ENG_SCREEN / 2.0f, r2 = (ENG_SCREEN / 2.0f) * (ENG_SCREEN / 2.0f) - d * d;
            const float hw = r2 > 0 ? sqrtf(r2) : 0;
            uint16_t *row = dst + y * (x1 - x0);
            for (int x = x0; x < x1; x++) row[x - x0] = fabsf(x + 0.5f - ENG_SCREEN / 2.0f) < hw ? c : 0;
        }
    }
    else {
#if ENG_SCALE == 2
    int ih = (h + 1) / 2;
    render_band(K, y0 / 2, ih, K->buf, 0, RW);
    double_band(K->buf, ih, dst, x0, x1);
#else
    render_band(K, y0, h, dst, x0, x1 - x0);
#endif
    }
    uint32_t t1 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    ui_render_band(y0, h, dst, x0, x1);
    RF->px_w[worker] += K->px; RF->seg_w[worker] += K->segs;
    if (E.cfg.clock_us) { uint32_t t2 = E.cfg.clock_us(); RF->us3d_w[worker] += t1 - t0; RF->usui_w[worker] += t2 - t1; }
}

#if ENG_RECT
/* Écran rectangulaire (RetroFW, 11/11/2026) : lignes internes [y0, y0 + h) (h ≤ ENG_BAND) de l'image de ENG_W × ENG_H_PX,
   prêtes à afficher dans dst (ENG_W × h). La 3D y est rendue telle quelle ; seulement si l'interface dessine sur ces lignes
   (ui_band_ops), elle est doublée dans tmp (ENG_IMG_W × 2h), l'interface posée par-dessus en 640 × 480, puis chaque
   carré de 2 × 2 ramené à sa moyenne. Avant (eng_render_native_band partout, puis réduction par la plateforme) : 4 ms de
   doublement et 8 ms de réduction par image sur la LDK, pour des pixels 3D qui revenaient identiques */
#if !ENG_UI_NATIF && !ENG_PREMIUM
static inline uint16_t moyenne4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {   /* RGB565 : composantes écartées, additionnées */
    a = (a | a << 16) & 0x07E0F81Fu; b = (b | b << 16) & 0x07E0F81Fu; c = (c | c << 16) & 0x07E0F81Fu; d = (d | d << 16) & 0x07E0F81Fu;
    const uint32_t s = ((a + b + c + d) >> 2) & 0x07E0F81Fu;
    return (uint16_t)(s | s >> 16);
}
#endif
void eng_render_band_rect(int y0, int h, uint16_t *dst, uint16_t *tmp) {
    worker_t *K = WK[0];
    uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    K->px = K->segs = 0;
    if (RF->noir) memset(dst, 0, sizeof *dst * (size_t)RW * (size_t)h);
    else render_band(K, y0, h, dst, 0, RW);
    uint32_t t1 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
#if ENG_PREMIUM
    (void)tmp;
    ui_render_band(y0, h, dst, 0, RW);   /* version PC premium : l'interface à la définition de l'image, sans doublement */
#elif ENG_UI_NATIF
    (void)tmp;
    ui_render_band(y0, h, dst, 0, RW);   /* interface déjà en 320 × 240 (ui.c, ENG_UI_NATIF) : ni doublement ni réduction */
#else
    /* Par plages d'opérations séparées par les voiles de toute l'image (menus, fondus, sous-menus : 40 ms par image quand le
       voile faisait doubler toute la bande) : chaque voile appliqué en 320 × 240 à sa place dans l'ordre de dessin */
    const int nops = ui_nops();
    for (int i0 = 0; ; ) {
        const int iv = ui_voile_suivant(i0);
        ui_plage_ops(i0, iv);
        int xs[2 * 8];
        const int ns = i0 < iv ? ui_band_spans(2 * y0, 2 * h, xs, 8) : 0;   /* seulement les colonnes où l'interface dessine */
        for (int s = 0; s < ns; s++) {
            const int nx0 = xs[2 * s] & ~1, nx1 = (xs[2 * s + 1] + 1) & ~1, w = nx1 - nx0;
            if (w <= 0) continue;
            double_band(dst, h, tmp, nx0, nx1);
            ui_render_band(2 * y0, 2 * h, tmp, nx0, nx1);
            for (int j = 0; j < h; j++) {
                const uint16_t *a = tmp + 2 * j * w, *b = a + w;
                uint16_t *o = dst + j * RW + nx0 / 2;
                for (int i = 0; i < w / 2; i++) {   /* quatre sous-pixels égaux (l'interface n'y a rien dessiné) : recopié */
                    const uint16_t p0 = a[2 * i];
                    o[i] = p0 == a[2 * i + 1] && p0 == b[2 * i] && p0 == b[2 * i + 1] ? p0 : moyenne4(p0, a[2 * i + 1], b[2 * i], b[2 * i + 1]);
                }
            }
        }
        if (iv >= nops) break;
        for (int j = 0; j < h; j++) ui_voile_bande(iv, dst + j * RW, RW);
        i0 = iv + 1;
    }
    ui_plage_ops(0, -1);
#endif
    RF->px_w[0] += K->px; RF->seg_w[0] += K->segs;
    if (E.cfg.clock_us) { uint32_t t2 = E.cfg.clock_us(); RF->us3d_w[0] += t1 - t0; RF->usui_w[0] += t2 - t1; }
}
#endif

#if ENG_DYN || ENG_PREMIUM
/* Écran plein (voir eng.h) : la 3D d'une bande de toute la largeur, et sa profondeur (ENG_DOF) ; l'interface ensuite,
   par eng_render_band_ui, une fois l'image floutée selon la profondeur par la plateforme */
void eng_render_band_3d(int worker, int y0, int h, uint16_t *dst, uint8_t *depth) {
    worker %= ENG_WORKERS;
    worker_t *K = WK[worker];
    uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    K->px = K->segs = 0;
    render_band(K, y0, h, dst, 0, RW);
#if ENG_DOF
    if (depth) memcpy(depth, K->dep, (size_t)RW * h);
#else
    if (depth) memset(depth, 255, (size_t)RW * h);
#endif
    RF->px_w[worker] += K->px; RF->seg_w[worker] += K->segs;
    if (E.cfg.clock_us) RF->us3d_w[worker] += E.cfg.clock_us() - t0;
}
void eng_render_band_ui(int worker, int y0, int h, uint16_t *dst) {
    worker %= ENG_WORKERS;
    uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    ui_render_band(y0, h, dst, 0, RW);
    if (E.cfg.clock_us) RF->usui_w[worker] += E.cfg.clock_us() - t0;
}
/* Profondeur de Sera (l'endroit net du flou de profondeur), dans l'échelle de worker_t.dep */
int eng_focus_depth(void) { return dep_of(cam_dist_cur); }
#endif
#if ENG_PREMIUM
/* Version PC premium : définition de l'image choisie par la page (ENG_W × ENG_H_PX au plus) ; le cadre de 466 de l'interface au
   milieu, comme sur la LDK, et la focale de la nouvelle hauteur (la même hauteur de vue) */
void eng_set_screen(int w, int h) {
    RW = w < 640 ? 640 : w > RW_MAX ? RW_MAX : w & ~1;   /* (480 lignes au moins : le cadre de 466 de l'interface) */
    RH = h < 480 ? 480 : h > RH_MAX ? RH_MAX : h & ~1;
    ui_ox = (RW - ENG_SCREEN) / 2; ui_oy = (RH - ENG_SCREEN) / 2; ui_w = RW - ui_ox; ui_h = RH - ui_oy; ui_img_w = RW; ui_img_h = RH;
    FOCAL = CYF / tanf(25.0f * PI_F / 180.0f);
    pcx = CXF; pcy = CYF;
    for (int y = 0; y < RH; y++) { span_l[y] = 0; span_r[y] = (int16_t)(RW - 1); }
}
void eng_screen_size(int *w, int *h) { *w = RW; *h = RH; }
#endif
#if ENG_DYN
void eng_set_screen(int w, int h) {
    RW = w < 64 ? 64 : w > RW_MAX ? RW_MAX : w & ~1;
    RH = h < 64 ? 64 : h > RH_MAX ? RH_MAX : h & ~1;
    ui_ox = ui_oy = 0; ui_w = RW; ui_h = RH; ui_img_w = RW; ui_img_h = RH;   /* l'interface couvre l'image (hud.c : écrans en portrait) */
}
void eng_screen_size(int *w, int *h) { *w = RW; *h = RH; }
/* Zone sûre de l'interface : sous la barre d'état ou l'encoche (haut), au-dessus de la barre de navigation (bas), en
   pixels de l'image ; la 3D remplit toujours l'image entière */
void eng_set_safe(int haut, int bas) {
    haut = haut < 0 ? 0 : haut > RH / 4 ? RH / 4 : haut; bas = bas < 0 ? 0 : bas > RH / 4 ? RH / 4 : bas;
    ui_oy = haut; ui_h = RH - haut - bas;
}
#endif

void eng_render_native_band(int y0, int h, uint16_t *dst, int x0, int x1) { eng_render_band_on(0, y0, h, dst, x0, x1); }

/* Emplacement des tables chaudes en mémoire interne rapide : 1 table des couleurs, puis par fil k (0, 1) : 2 << 3k masque de
   couverture, 4 << 3k tampon de bande, 8 << 3k profondeur ; 128 listes des bandes ; 0 hors de l'ESP32 */
#ifdef ESP_PLATFORM
#include "esp_memory_utils.h"
int eng_profile_mem(void) {
    int m = esp_ptr_internal(cmap) ? 1 : 0;
    for (int k = 0; k < ENG_WORKERS && k < 2; k++)
        if (WK[k]) m |= ((esp_ptr_internal(WK[k]->cov) ? 2 : 0) | (esp_ptr_internal(WK[k]->buf) ? 4 : 0) | (esp_ptr_internal(WK[k]->zb) ? 8 : 0)) << (3 * k);
    return m | (esp_ptr_internal(FR[0].bin_idx) ? 128 : 0);
}
#else
int eng_profile_mem(void) { return 0; }
#endif

/* Compteurs de profilage des deux cœurs additionnés (out : PROF_N valeurs, ou NULL), puis remis à zéro */
void eng_profile(uint32_t *out) {
    for (int k = 0; k < PROF_N; k++) {
        uint32_t v = 0;
        for (int w = 0; w < ENG_WORKERS; w++) if (WK[w]) { v += WK[w]->prof[k]; WK[w]->prof[k] = 0; }
        if (out) out[k] = v;
    }
}

void eng_profile_build(uint32_t *out) {
#ifdef ENG_PROFILE
    for (int k = 0; k < BPROF_N; k++) { if (out) out[k] = bprof[k]; bprof[k] = 0; }
#else
    if (out) memset(out, 0, BPROF_N * sizeof *out);
#endif
}

/* Rayon depuis la caméra à travers le point (px, py) de l'image interne, avancé jusqu'au sol ou à l'eau */
int pick_ground(float px, float py, float *ox, float *oz) {
    const camera_t *c = &E.cam;
    float vx = (px - CXF) / FOCAL, vy = -(py - CYF) / FOCAL, vz = 1;
    float dyv = vy * c->cp - vz * c->sp, z1 = vy * c->sp + vz * c->cp;
    float dx = vx * c->cy + z1 * c->sy, dz = -vx * c->sy + z1 * c->cy, dy = dyv;
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    dx /= l; dy /= l; dz /= l;
    if (E.in.on) {   /* intérieur : son sol plat */
        if (dy >= -1e-3f) return 0;
        const float t = -c->y / dy;
        *ox = c->x + dx * t; *oz = c->z + dz * t;
        return 1;
    }
    int n = E.w.n;
    for (float t = 0; t < 60; t += 0.08f) {
        float x = c->x + dx * t, y = c->y + dy * t, z = c->z + dz * t;
        if (x < 0 || z < 0 || x >= n || z >= n) continue;
        float h = height_at(x, z);
        const chunk_t *ch = chunk_of_cell((int)x, (int)z);
        if (ch && ch->water[cell_local((int)x, (int)z)] && ch->water_h[cell_local((int)x, (int)z)] > h) h = ch->water_h[cell_local((int)x, (int)z)];
        if (y <= h) { *ox = x; *oz = z; return 1; }
    }
    return 0;
}

int project(float x, float y, float z, float *sx, float *sy) {
    v3 o = to_view(x, y, z);
    if (o.z < NEAR_Z) return 0;
    *sx = (pcx + o.x * FOCAL / o.z) * ENG_SCALE - ui_ox;   /* repère de l'interface */
    *sy = (pcy - o.y * FOCAL / o.z) * ENG_SCALE - ui_oy;
    return 1;
}

#ifndef ESP_PLATFORM
int eng_essai_sans_ui;
int eng_essai_planche_absente;
#endif

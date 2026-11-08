/*
 * Carte du menu (sous-menu CARTE de hud.c ; carteDessiner de l'émulateur, à garder identiques)
 *
 * Le plan de chaque carte est exporté par l'émulateur (section PLAN de monde.pak, format dans pak.h) : couleurs plates
 * par terrain, relief en trois tons, eau, rivières, routes et ponts, bâtiments, enceinte, en tuiles de CARTE_T × CARTE_T
 * pixels (index d'une palette par carte, 0 jamais pris) à plusieurs niveaux : le niveau e a 2^-e pixels par case
 * (e > 0 : vues d'ensemble ; e = 0 : un pixel par case ; e = -1, -2 : 2 et 4 pixels par case sur les lieux bâtis
 * seulement, la carte des villes). Les noms (villes, lieux, bâtiments) et les pictogrammes des enseignes sont dessinés à
 * taille fixe par-dessus.
 *
 * CARTE_X : le plan se lit en miroir de celui de l'Atelier (03/11/2026, retour de l'utilisateur : carte inversée gauche-
 * droite). La vue 3D montre le monde retourné (face au nord, -z, les x croissants sont à gauche de l'écran) ; la carte la
 * suit : nord en haut, x croissant vers la gauche (écran = cx - (x - C.x) × C.s), tuiles retournées par ui_miroir. Les
 * textes du jeu disent l'est et l'ouest de la carte du jeu (côté -x : est), pas de celle de l'Atelier.
 *
 * Mémoire : rien de réservé pendant le jeu. Le répertoire de la carte (niveaux, palette, table des tuiles, noms) et un
 * cache de tuiles décompressées vont dans la fin du tampon commun (img_emprunter(TC_CARTE), world.c), qui laisse au pilote
 * automatique la place de ses champs au début : les deux tiennent ensemble. Les tuiles sont lues dans monde.pak (carte SD
 * de la montre) à la demande, CARTE_LECTURES au plus par image, les plus proches du centre d'abord ; une tuile dessinée à
 * cette image ou à la précédente (que l'autre cœur dessine encore) n'est jamais reprise.
 *
 * Gestes : un doigt déplace la carte, deux la zooment autour de leur milieu, double toucher : zoom × 2 au point touché ;
 * boutons ‹ (retour), − et + (zoom), ◎ (recentrer sur Sera) ; molette de la page web et lunette de la montre Wear OS
 * (eng_set_zoom) ; flèches ou stick de la manette : déplacer, OK : zoom × 2 (au plus fort : vue d'ensemble). En véhicule,
 * un toucher y envoie le pilote automatique (hud.c, carte_pilote) ; à pied, il propose de s'y téléporter si l'endroit a
 * été vu (hud.c, carte_tele_demander). Un toucher simple n'est rendu (CARTE_TOUCHE) qu'une fois passé le temps du double
 * toucher (CARTE_DOUBLE), qui zoome.
 *
 * Brouillard de guerre (04/11/2026, demande de l'utilisateur : « on ne découvre la carte qu'au fur et à mesure ; on voit la
 * topographie générale, mais ce qui n'a pas été visité est grisé ») : un bit par bloc de BROU_G × BROU_G cases et par carte,
 * levé quand le centre du bloc passe à moins de BROU_R cases de Sera (hud.c, tele_maj, deux fois par seconde). Pas vu : le
 * relief, l'eau et les côtes restent, grisés (palette FOGI, à la suite de celle du plan) ; les ouvrages (routes, rues,
 * bâtiments, pistes, enceinte, enseignes : couleurs OUVRAGES) prennent la couleur du sol voisin ; ni noms, ni pictogrammes,
 * ni personnes qui ouvrent une quête (« ! »), ni panneaux. Bord adouci : mélange des blocs voisins au centre de chaque
 * pixel du plan, resserré, tramé (4 × 4), sans couleur de plus. Le tout est fait sur la tuile quand elle est lue dans le
 * cache (brumer : le brouillard ne change pas tant que la carte est ouverte), une tuile toute vue n'est pas touchée ; une
 * tuile d'une couleur à moitié vue est faite dans une place du cache. Les objectifs restent montrés partout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "internal.h"
#include "ui.h"
#include "audio.h"

#define CARTE_T 32               /* côté des tuiles (pixels du plan) */
#define CARTE_R 222.0f           /* rayon du disque de la carte (écran rond) */
#define CARTE_SMIN 2.0f          /* pixels d'écran par pixel du plan au moins : le niveau le plus fin qui le permet */
#define CARTE_ZMAX 16.0f         /* zoom le plus fort : pixels d'écran par case */
#define CARTE_VILLE 8.0f         /* zoom à l'ouverture dans un lieu bâti (la carte de la ville) */
#define CARTE_DEHORS 2.0f        /* zoom à l'ouverture ailleurs */
#define CARTE_ANIM 0.12f         /* temps de réponse des zooms et des recentrages animés (s) */
#define CARTE_DOUBLE 0.35f       /* double toucher : le second dans ce temps (s) ; un toucher simple n'est rendu qu'après */
#if ENG_DYN
#define CARTE_LECTURES 48        /* tuiles lues au plus par image (téléphone : paquet projeté en mémoire) */
#else
#define CARTE_LECTURES 12        /* tuiles lues au plus par image (montre : carte SD) */
#endif
#define CARTE_SLOTS_MAX 256      /* places du cache au plus */
/* Places du cache au moins (eng_init fait le tampon commun assez grand) : sur l'écran rond, ~60 tuiles en vue au pire ; en
   plein écran (téléphone, 540 × 960 points), jusqu'à ~130 */
#if ENG_DYN
#define CARTE_SLOTS_MIN 240
#else
#define CARTE_SLOTS_MIN 64
#endif
#define CARTE_GLYPHES 32         /* pictogrammes des enseignes au plus */
#define CARTE_NOMS_VUS 64        /* noms placés au plus par image */
/* Zoom (pixels par case) à partir duquel chaque sorte de nom paraît (PLAN : 0 ville, 1 village, 2 campement, 3 lieu,
   4 bâtiment, 5 astre) ; bâtiments : pictogramme, puis le nom dessous */
static const float NOM_VU[6] = { 0, 0.6f, 1.2f, 0.6f, 3, 0 };
#define NOM_BAT_VU 6.0f
enum { CB_RETOUR, CB_MOINS, CB_PLUS, CB_CENTRE, CB_N };

typedef struct { int8_t e; uint8_t dense; uint16_t nt, first, count; } niveau_t;
typedef struct { uint32_t off, dir, len; } plan_carte_t;

static struct {
    /* Paquet : PLAN (décalage dans monde.pak), cartes, pictogrammes (lus au démarrage) */
    uint32_t off, len; int nc, ngly; uint32_t dir_max;
    plan_carte_t pc[MAX_CARTES];
    /* Plan de la carte chargé dans le tampon : répertoire, puis slot_of, puis les places du cache */
    uint8_t *buf; uint32_t cap; int carte, ok, perdu;
    int n, nniv, ntuiles, nnoms, npal;
    niveau_t niv[8];
    const uint8_t *tuiles, *noms;
    int16_t *slot_of; uint8_t *slots; int nslots;
    uint8_t *etat; int brou;     /* brouillard de la carte (brouillard_carte) ; état de chaque tuile : 0 toute vue, 1 rien de vu, 2 en partie */
    uint32_t us_etat;            /* durée du calcul de ces états à l'ouverture (banc d'essai) */
    /* Vue : centre (cases), zoom (pixels par case), cible des animations */
    int ouverte; float x, z, s, tx, tz, ts; int anim;
    uint32_t trame, lus, lus_total; float calme_t;
    /* Doigts */
    int down, two, btn, tap_att; float x0, y0, gx, gy, moved, t0, d0, s0, wx, wz, tap_t, tap_x, tap_y, now;
    float px, pz;                /* point touché (pilote automatique, téléport) ; tap_att : toucher simple rendu après CARTE_DOUBLE */
    /* Lectures demandées à cette image : tuile, priorité (distance au centre) */
    int nwant; int16_t want[64]; float wantd[64];
} C = { .carte = -1, .trame = 10 };   /* trame : jamais sous 2 (slot_vu = trame - 2) */
static int16_t slot_key[CARTE_SLOTS_MAX] ENG_FROID;
static uint32_t slot_vu[CARTE_SLOTS_MAX] ENG_FROID;
static uint16_t PAL[256] ENG_FROID;
static uint8_t UNI[256];         /* UNI[c] = c : tuile d'une seule couleur, dessinée comme un pixel agrandi */
static ui_mask_t GLY[CARTE_GLYPHES];
static uint8_t gly_data[CARTE_GLYPHES][128] ENG_FROID;
static uint8_t tmp_tuile[1100] ENG_FROID;
/* Couleurs des néons des enseignes (Buildings.NEON_COULEURS de l'atelier : cœur, contour) : 1 vert, 2 magenta, 3 cyan, 4 ambre */
static const uint32_t NEON[5][2] = { { 0xFFFFFF, 0xB8C0D0 }, { 0xC8FFD8, 0x3DFF7A }, { 0xFF9AD2, 0xFF2D9B }, { 0xC8FBFF, 0x22E4FF }, { 0xFFF1C0, 0xFFB020 } };
#define INK 0x05040A
#define GUIDE_RGB 0x5DFF8A

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int lire(uint32_t off, void *dst, uint32_t len) {
    if (E.cfg.pak_read) return E.cfg.pak_read(off, dst, len);
    if ((size_t)off + len > E.pak_len) return -1;
    memcpy(dst, E.pak + off, len);
    return 0;
}
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }   /* division arrondie vers le bas */

/* ---------- Brouillard de guerre : les blocs vus de chaque carte (brouillardVoir… de l'émulateur) ----------
   Octets (la fin du bloc de la sauvegarde, hud.c : écrits tels quels à sa suite) : 'BRO1', nombre de cartes u16, 0 u16,
   côté en blocs de chaque carte u16 (0 : sans brouillard), alignés sur 4, puis les bits de chaque carte (bit i du mot :
   bloc bz × côté + bx ; mots de 32 bits), puis la somme de contrôle (hud.c). Planète de 1 024 : 2 Ko ; paquet du 04/11/2026
   (13 cartes, vol permanent et course sans brouillard) : 4 716 octets */
static struct { uint8_t *mem; uint32_t taille, off[MAX_CARTES]; int nc, nb[MAX_CARTES]; } BR;
static int brou_nb(int k) {
    if (k < 0 || k >= E.w.ncartes || k >= MAX_CARTES) return 0;
    const carte_t *c = &E.w.cartes[k];
    return c->vol || c->course || c->n <= 0 ? 0 : (c->n + BROU_G - 1) / BROU_G;
}
static uint32_t brou_oct(int nb) { return ((uint32_t)nb * (uint32_t)nb + 31) / 32 * 4; }
static uint32_t brou_debut(int nc) { return 8 + ((2u * (uint32_t)nc + 3) & ~3u); }
uint32_t brouillard_taille(void) {
    const int nc = E.w.ncartes < MAX_CARTES ? E.w.ncartes : MAX_CARTES;
    uint32_t t = brou_debut(nc);
    for (int k = 0; k < nc; k++) t += brou_oct(brou_nb(k));
    return t + 4;
}
static void brou_entete(void) {
    uint8_t *m = BR.mem;
    m[0] = 'B'; m[1] = 'R'; m[2] = 'O'; m[3] = '1'; m[4] = (uint8_t)BR.nc; m[5] = 0; m[6] = m[7] = 0;
    for (int k = 0; k < BR.nc; k++) { m[8 + 2 * k] = (uint8_t)BR.nb[k]; m[9 + 2 * k] = (uint8_t)(BR.nb[k] >> 8); }
}
void brouillard_init(uint8_t *mem) {
    memset(&BR, 0, sizeof BR);
    if (!mem) return;
    BR.mem = mem; BR.taille = brouillard_taille(); BR.nc = E.w.ncartes < MAX_CARTES ? E.w.ncartes : MAX_CARTES;
    uint32_t o = brou_debut(BR.nc);
    for (int k = 0; k < BR.nc; k++) { BR.nb[k] = brou_nb(k); BR.off[k] = o; o += brou_oct(BR.nb[k]); }
    memset(mem, 0, BR.taille);
    brou_entete();
}
void brouillard_effacer(void) {
    if (!BR.mem) return;
    memset(BR.mem, 0, BR.taille);
    brou_entete();
}
/* Bloc (bx, bz) de la carte k vu ; hors de la carte : non ; carte sans brouillard : oui */
static int brou_bit(int k, int bx, int bz) {
    if (k < 0 || k >= BR.nc || !BR.mem) return 1;
    const int nb = BR.nb[k];
    if (!nb) return 1;
    if (bx < 0 || bz < 0 || bx >= nb || bz >= nb) return 0;
    const uint32_t i = (uint32_t)bz * (uint32_t)nb + (uint32_t)bx;
    return BR.mem[BR.off[k] + (i >> 3)] >> (i & 7) & 1;
}
int brouillard_carte(int k) { return k >= 0 && k < BR.nc && BR.mem && BR.nb[k] > 0; }
int brouillard_vu(float x, float z, int k) { return brou_bit(k, (int)floorf(x / BROU_G), (int)floorf(z / BROU_G)); }
void brouillard_voir(float x, float z, int k, float r) {
    if (!brouillard_carte(k)) return;
    const int nb = BR.nb[k];
    int b0x = (int)floorf((x - r) / BROU_G), b1x = (int)floorf((x + r) / BROU_G), b0z = (int)floorf((z - r) / BROU_G), b1z = (int)floorf((z + r) / BROU_G);
    if (b0x < 0) b0x = 0;
    if (b0z < 0) b0z = 0;
    if (b1x >= nb) b1x = nb - 1;
    if (b1z >= nb) b1z = nb - 1;
    for (int bz = b0z; bz <= b1z; bz++) for (int bx = b0x; bx <= b1x; bx++) {
        const float dx = (bx + 0.5f) * BROU_G - x, dz = (bz + 0.5f) * BROU_G - z;
        if (dx * dx + dz * dz >= r * r) continue;
        const uint32_t i = (uint32_t)bz * (uint32_t)nb + (uint32_t)bx;
        BR.mem[BR.off[k] + (i >> 3)] |= (uint8_t)(1u << (i & 7));
    }
}
/* Sauvegarde lue en place (hud.c, somme de contrôle déjà vérifiée) : faite avec ce paquet (mêmes cartes, mêmes côtés) ;
   sinon rien de vu, 0 (hud.c reconstitue alors au mieux, comme pour une sauvegarde d'avant) */
int brouillard_accepter(uint32_t n) {
    if (!BR.mem) return 0;
    const uint8_t *m = BR.mem;
    int ok = n == BR.taille && m[0] == 'B' && m[1] == 'R' && m[2] == 'O' && m[3] == '1' && m[4] == BR.nc && !m[5];
    for (int k = 0; k < BR.nc && ok; k++) ok = (m[8 + 2 * k] | m[9 + 2 * k] << 8) == BR.nb[k];
    if (!ok) brouillard_effacer();
    return ok;
}
void brouillard_etat(char *out, int n) {
    int o = snprintf(out, n, "brouillard : %u octets ;", (unsigned)BR.taille);
    for (int k = 0; k < BR.nc && o > 0 && o < n - 48; k++) {
        if (!BR.nb[k]) { o += snprintf(out + o, n - o, " %d sans,", k); continue; }
        int vus = 0;
        for (int b = 0; b < BR.nb[k] * BR.nb[k]; b++) vus += BR.mem[BR.off[k] + (b >> 3)] >> (b & 7) & 1;
        o += snprintf(out + o, n - o, " %d : %d/%d,", k, vus, BR.nb[k] * BR.nb[k]);
    }
}

/* ---------- Démarrage : où est PLAN, ses cartes, ses pictogrammes ---------- */
void carte_init(void) {
    uint32_t sz = 0;
    const uint8_t *p = pak_section("PLAX", &sz);   /* paquet léger (tools/paquet_sd.js) : PLAN dans monde.pak */
    C.nc = 0; C.ngly = 0; C.dir_max = 0; C.carte = -1; C.ok = 0;
    for (int i = 0; i < 256; i++) UNI[i] = (uint8_t)i;
    if (p && sz >= 8) { C.off = rd32(p); C.len = rd32(p + 4); }
    else if ((p = pak_section("PLAN", &sz))) { C.off = (uint32_t)(p - E.pak); C.len = sz; }
    else return;
    uint8_t h[12];
    if (C.len < 12 || lire(C.off, h, 12) || rd32(h) != 0x314E4C50u) return;   /* 'PLN1' */
    const int nc = (int)rd32(h + 4), ng = rd16(h + 8);
    if (rd16(h + 10) != CARTE_T || nc <= 0) return;
    for (int g = 0; g < ng && g < CARTE_GLYPHES; g++) {   /* 16 × 16, une ligne par u16 (bit 15 : à gauche) -> opacités 4 bits */
        uint8_t r[32];
        if (lire(C.off + 12 + g * 32, r, 32)) return;
        for (int y = 0; y < 16; y++) {
            const uint16_t row = rd16(r + y * 2);
            for (int x = 0; x < 16; x += 2)
                gly_data[g][y * 8 + x / 2] = (uint8_t)(((row >> (15 - x)) & 1 ? 0xF0 : 0) | ((row >> (14 - x)) & 1 ? 0x0F : 0));
        }
        GLY[g] = (ui_mask_t){ gly_data[g], 16, 16, -8, -8, 0, 0 };
        C.ngly = g + 1;
    }
    const uint32_t t0 = 12 + (uint32_t)ng * 32;
    for (int k = 0; k < nc && k < MAX_CARTES; k++) {
        uint8_t e[12];
        if (lire(C.off + t0 + k * 12, e, 12)) return;
        C.pc[k].off = rd32(e); C.pc[k].dir = rd32(e + 4); C.pc[k].len = rd32(e + 8);
        if (C.pc[k].dir > C.dir_max) C.dir_max = C.pc[k].dir;
        C.nc = k + 1;
    }
}
/* Place à garder dans le tampon commun (eng_init) : le plus grand répertoire, sa table slot_of (2 octets par tuile, 8 dans
   le répertoire), CARTE_SLOTS_MIN tuiles */
uint32_t carte_taille(void) {
    if (!C.nc) return 0;
    return ((C.dir_max + 3) & ~3u) + ((C.dir_max / 4 + 3) & ~3u) + ((C.dir_max / 8 + 3) & ~3u) + CARTE_SLOTS_MIN * CARTE_T * CARTE_T;   /* (état du brouillard : un octet par tuile) */
}

/* ---------- Brouillard sur les tuiles du plan ---------- */
/* Couleurs des ouvrages du plan (PLAN_FIXE de l'émulateur sans le vide ni les rivières, fonds et contours des enseignes :
   PLAN_FIXE.ens, PLAN_NEON) : pas vus, ils prennent la couleur du sol voisin */
static const uint32_t OUVRAGES[] = { 0xB4AFBA, 0xA8916A, 0xE6D59C, 0xF4C26A, 0xF6F2E6, 0xFF4FB0, 0x2C2735, 0x141119, 0x463C5C, 0xB3A5DC,
                                     0x56505B, 0xCDB98E, 0xFFD23F, 0xFFE27A, 0x244E36, 0x552043, 0x1E4856, 0x56421C, 0xB8C0D0, 0x3DFF7A,
                                     0xFF2D9B, 0x22E4FF, 0xFFB020 };
static uint8_t OUVR[256], FOGI[256];   /* index : ouvrage ; sa couleur dans le brouillard (FOGI de chaque couleur du plan) */
static int FOG_NEUTRE;                 /* gris d'un ouvrage sans sol autour */
/* Couleur grisée : à 62 % vers sa luminance, assombrie (× 5/8), un peu bleutée (brumeRgb de l'émulateur) */
static uint32_t brume_rgb(uint32_t c) {
    const int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255, y = (77 * r + 150 * g + 29 * b) >> 8;
    const int R = ((y * 5 + r * 3) >> 3) * 5 >> 3, G = ((y * 5 + g * 3) >> 3) * 5 >> 3, B = ((y * 5 + b * 3) >> 3) * 5 >> 3;
    return (uint32_t)(R + 10) << 16 | (uint32_t)(G + 12) << 8 | (uint32_t)(B + 18);
}
/* Palette du brouillard, à la suite de celle du plan (rgb : ses couleurs) : une couleur grisée par couleur du plan s'il y a
   la place (2 × npal + 1 ≤ 255), sinon des gris par luminance */
static void brume_palette(const uint8_t *rgb, int npal) {
    const int place = 2 * npal + 1 <= 255, nl = 254 - npal;
    FOG_NEUTRE = place ? 2 * npal + 1 : 255;
    PAL[FOG_NEUTRE] = ui_rgb(brume_rgb(0x5A5A5A));
    memset(OUVR, 0, sizeof OUVR);
    for (int c = 0; c < 256; c++) FOGI[c] = (uint8_t)c;
    for (int c = 1; c <= npal && c < 255; c++) {
        const uint32_t v = (uint32_t)rgb[(c - 1) * 3] << 16 | rgb[(c - 1) * 3 + 1] << 8 | rgb[(c - 1) * 3 + 2];
        for (size_t i = 0; i < sizeof OUVRAGES / sizeof OUVRAGES[0]; i++) if (OUVRAGES[i] == v) OUVR[c] = 1;
        if (place) { FOGI[c] = (uint8_t)(npal + c); PAL[npal + c] = ui_rgb(brume_rgb(v)); continue; }
        if (nl < 1) continue;
        const int y = (77 * (int)(v >> 16) + 150 * (int)(v >> 8 & 255) + 29 * (int)(v & 255)) >> 8, k = y * nl / 256;
        FOGI[c] = (uint8_t)(npal + 1 + k); PAL[npal + 1 + k] = ui_rgb(brume_rgb((uint32_t)y * 0x010101u));
    }
}
/* Bloc (bx, bz) vu sur la carte du plan */
static int vu_bloc(int bx, int bz) { return brou_bit(C.carte, bx, bz); }
static int vu_ici(float x, float z) { return !C.brou || brouillard_vu(x, z, C.carte); }
/* État de la tuile (tx, tz) du niveau L : 0 toute vue, 1 rien de vu, 2 en partie ; ses blocs et leur couronne (le mélange
   des bords les prend) */
static uint8_t tuile_etat(const niveau_t *L, int tx, int tz) {
    const int sh = 5 + L->e;   /* 2^sh cases par tuile */
    if (sh < 0) return 2;
    const int b0x = ((tx << sh) >> 3) - 1, b1x = ((((tx + 1) << sh) - 1) >> 3) + 1, b0z = ((tz << sh) >> 3) - 1, b1z = ((((tz + 1) << sh) - 1) >> 3) + 1;
    int vu = 0, cache = 0;
    for (int bz = b0z; bz <= b1z; bz++) for (int bx = b0x; bx <= b1x; bx++) {
        if (vu_bloc(bx, bz)) vu = 1; else cache = 1;
        if (vu && cache) return 2;
    }
    return vu ? 0 : 1;
}
/* Pixels pas vus de la tuile (tx, tz) du niveau L (état 1 ou 2) : grisés ; un ouvrage d'un pixel de large prend le sol d'à
   côté (gauche, sinon dessus), un plus large la couleur naturelle la plus présente de la tuile (vote de Boyer-Moore) : à plat,
   sans contour qui trahirait un bâtiment. Vu : mélange bilinéaire des blocs
   autour du centre du pixel (en 1/64 de case), resserré (le passage prend un demi-bloc), seuil tramé 4 × 4 (brumer de
   l'émulateur) */
static const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static void brumer(uint8_t *px, const niveau_t *L, int tx, int tz, int etat) {
    int maj = 0, cpt = 0, nat = 0;
    for (int i = 0; i < CARTE_T * CARTE_T; i++) {
        const int c = px[i];
        if (OUVR[c]) continue;
        nat = 1;
        if (!cpt) { maj = c; cpt = 1; } else cpt += c == maj ? 1 : -1;
    }
    const int sh = L->e + 5;
    if (sh < 0) etat = 1;
    uint8_t prev[CARTE_T] = { 0 }, cur[CARTE_T];   /* (prev : la ligne d'avant, lue à partir de la deuxième) */
    for (int v = 0; v < CARTE_T; v++) {
        int j0 = 0, s = 0;
        if (etat == 2) { const int fz = ((2 * (tz * CARTE_T + v) + 1) << sh) - 256; j0 = fdiv(fz, 512); s = fz - j0 * 512; }
        memcpy(cur, px + v * CARTE_T, CARTE_T);
        for (int u = 0; u < CARTE_T; u++) {
            const int c = cur[u];
            if (etat == 2) {
                const int fx = ((2 * (tx * CARTE_T + u) + 1) << sh) - 256, i0 = fdiv(fx, 512), t = fx - i0 * 512;
                const int a = vu_bloc(i0, j0), b = vu_bloc(i0 + 1, j0), cc = vu_bloc(i0, j0 + 1), d = vu_bloc(i0 + 1, j0 + 1);
                const int val = 2 * ((a * (512 - t) + b * t) * (512 - s) + (cc * (512 - t) + d * t) * s) - 131072;   /* 0 à 262 144 avant le resserrement */
                if (val > (2 * BAYER[(v & 3) * 4 + (u & 3)] + 1) * 8192) continue;   /* vu */
            }
            int n;
            if (!OUVR[c]) n = FOGI[c];
            else if (u > 0 && u < CARTE_T - 1 && !OUVR[cur[u - 1]] && !OUVR[cur[u + 1]]) n = FOGI[cur[u - 1]];   /* trait d'un pixel : le sol à côté */
            else if (v > 0 && v < CARTE_T - 1 && !OUVR[prev[u]] && !OUVR[px[(v + 1) * CARTE_T + u]]) n = FOGI[prev[u]];
            else n = nat ? FOGI[maj] : FOG_NEUTRE;   /* plus large (rues, bâtiments, routes de plusieurs pixels) : à plat */
            px[v * CARTE_T + u] = (uint8_t)n;
        }
        memcpy(prev, cur, CARTE_T);
    }
}

/* ---------- Plan de la carte courante dans le tampon ---------- */
void carte_perdue(void) { C.ok = 0; C.buf = NULL; C.perdu = 1; }
static int charger(void) {
    const int k = E.w.carte;
    if (C.ok && C.carte == k && C.buf) return 1;
    C.ok = 0;
    if (k < 0 || k >= C.nc) return 0;
    const uint32_t place = img_carte_place();
    uint8_t *b = place ? img_emprunter(TC_CARTE, place) : NULL;
    if (!b) return 0;
    C.buf = b; C.cap = place; C.perdu = 0;
    const plan_carte_t *pc = &C.pc[k];
    if (pc->dir < 16 || pc->dir > place || lire(C.off + pc->off, b, pc->dir)) return 0;
    C.n = rd16(b); C.nniv = b[2]; C.npal = b[3]; C.ntuiles = rd16(b + 4); C.nnoms = rd16(b + 6);
    const int nsur = rd16(b + 8);
    if (C.nniv > 8) return 0;
    const uint8_t *q = b + 16;
    for (int i = 0; i < C.nniv; i++, q += 8) { C.niv[i].e = (int8_t)q[0]; C.niv[i].dense = q[1]; C.niv[i].nt = rd16(q + 2); C.niv[i].first = rd16(q + 4); C.niv[i].count = rd16(q + 6); }
    PAL[0] = 0;
    for (int c = 0; c < 255; c++) PAL[c + 1] = c < C.npal ? ui_rgb(((uint32_t)q[c * 3] << 16) | (q[c * 3 + 1] << 8) | q[c * 3 + 2]) : 0;
    C.brou = brouillard_carte(k);
    if (C.brou) brume_palette(q, C.npal);   /* couleurs du brouillard, à la suite */
    q += (C.npal * 3 + 3) & ~3;
    C.tuiles = q; q += C.ntuiles * 8;
    C.noms = q; q += C.nnoms * 32;
    /* Noms sûrs (édition unique, contenu 18+ verrouillé) : à la place du nom, dans la copie du tampon */
    for (int i = 0; i < nsur; i++, q += 26) {
        const int r = rd16(q);
        if (r < C.nnoms && verrou18()) { memcpy((uint8_t *)C.noms + r * 32 + 8, q + 2, 24); ((uint8_t *)C.noms)[r * 32 + 31] = 0; }
    }
    uint32_t o = (pc->dir + 3) & ~3u;
    C.slot_of = (int16_t *)(b + o); o += ((uint32_t)C.ntuiles * 2 + 3) & ~3u;
    C.etat = b + o; o += ((uint32_t)C.ntuiles + 3) & ~3u;
    C.slots = b + o;
    C.nslots = o < place ? (int)((place - o) / (CARTE_T * CARTE_T)) : 0;
    if (C.nslots > CARTE_SLOTS_MAX) C.nslots = CARTE_SLOTS_MAX;
    if (C.nslots < 8) return 0;
    for (int i = 0; i < C.ntuiles; i++) C.slot_of[i] = -1;
    for (int s = 0; s < C.nslots; s++) { slot_key[s] = -1; slot_vu[s] = 0; }
    C.carte = k;
    /* État du brouillard de chaque tuile : il ne change pas tant que la carte est ouverte (Sera ne bouge pas) */
    const uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    memset(C.etat, 0, (size_t)C.ntuiles);
    for (int i = 0; i < C.nniv && C.brou; i++) {
        const niveau_t *L = &C.niv[i];
        for (int t = L->first; t < L->first + L->count && t < C.ntuiles && L->nt > 0; t++) {
            const int key = rd16(C.tuiles + t * 8);
            C.etat[t] = tuile_etat(L, key % L->nt, key / L->nt);
        }
    }
    C.us_etat = E.cfg.clock_us ? E.cfg.clock_us() - t0 : 0;
    C.ok = 1;
    return 1;
}

/* Tuile (tx, tz) du niveau L : son rang dans la table, -1 s'il n'y en a pas */
static int tuile_de(const niveau_t *L, int tx, int tz) {
    if (tx < 0 || tz < 0 || tx >= L->nt || tz >= L->nt) return -1;
    if (L->dense) return L->first + tz * L->nt + tx;
    const int key = tz * L->nt + tx;
    int a = L->first, b = L->first + L->count - 1;
    while (a <= b) {
        const int m = (a + b) / 2, k = rd16(C.tuiles + m * 8);
        if (k == key) return m;
        if (k < key) a = m + 1; else b = m - 1;
    }
    return -1;
}
/* Index de la tuile t s'il est prêt (NULL sinon) ; *uni : sa couleur si elle n'en a qu'une (et NULL) */
static const uint8_t *tuile_px(int t, int *uni) {
    const uint8_t *e = C.tuiles + t * 8;
    *uni = -1;
    if (!rd16(e + 2)) {   /* une seule couleur : telle quelle, ou grisée ; à moitié vue, faite dans une place du cache (lire_tuile) */
        const int u = (int)(rd32(e + 4) & 255), et = C.etat ? C.etat[t] : 0;
        if (et == 0) { *uni = u; return NULL; }
        if (et == 1) { *uni = OUVR[u] ? FOG_NEUTRE : FOGI[u]; return NULL; }
    }
    const int s = C.slot_of[t];
    return s >= 0 ? C.slots + s * CARTE_T * CARTE_T : NULL;
}
static void demander(int t, float d) {
    for (int i = 0; i < C.nwant; i++) if (C.want[i] == t) return;
    if (C.nwant < 64) { C.want[C.nwant] = (int16_t)t; C.wantd[C.nwant++] = d; return; }
    int w = 0;
    for (int i = 1; i < 64; i++) if (C.wantd[i] > C.wantd[w]) w = i;
    if (d < C.wantd[w]) { C.want[w] = (int16_t)t; C.wantd[w] = d; }
}
/* Lit la tuile t dans la place la plus anciennement servie (jamais une servie à cette image ou à la précédente) */
static int lire_tuile(int t) {
    int s = -1; uint32_t best = 0xFFFFFFFFu;
    for (int i = 0; i < C.nslots; i++) {
        if (slot_key[i] < 0) { s = i; break; }
        if (slot_vu[i] + 1 >= C.trame) continue;
        if (slot_vu[i] < best) { best = slot_vu[i]; s = i; }
    }
    if (s < 0) return 0;
    if (slot_key[s] >= 0) C.slot_of[slot_key[s]] = -1;
    slot_key[s] = -1;
    const uint8_t *e = C.tuiles + t * 8;
    const uint32_t n = rd16(e + 2), off = rd32(e + 4);
    uint8_t *dst = C.slots + s * CARTE_T * CARTE_T;
    if (!n) memset(dst, (int)(off & 255), CARTE_T * CARTE_T);   /* une couleur, à moitié vue (tuile_px) */
    else {
        if (n > sizeof tmp_tuile || lire(C.off + C.pc[C.carte].off + off, n == CARTE_T * CARTE_T ? dst : tmp_tuile, n)) return 0;
        if (n != CARTE_T * CARTE_T && lz4_decode(tmp_tuile, (int)n, dst, CARTE_T * CARTE_T) != CARTE_T * CARTE_T) return 0;
    }
    if (C.etat && C.etat[t]) {   /* brouillard : sur la tuile lue, une fois (il ne change pas tant que la carte est ouverte) */
        for (int i = 0; i < C.nniv; i++) {
            const niveau_t *L = &C.niv[i];
            if (t < L->first || t >= L->first + L->count || L->nt <= 0) continue;
            const int key = rd16(e);
            brumer(dst, L, key % L->nt, key / L->nt, C.etat[t]);
            break;
        }
    }
    slot_key[s] = (int16_t)t; slot_vu[s] = C.trame - 2; C.slot_of[t] = (int16_t)s;
    C.lus++; C.lus_total++;
    return 1;
}

/* ---------- Vue ---------- */
static void aire(float *cx, float *cy, float *hw, float *hh, float *cr) {
#if ENG_DYN
    *cx = ui_w / 2.0f; *cy = ui_h / 2.0f; *hw = ui_w / 2.0f; *hh = ui_h / 2.0f; *cr = 8000;
#else
    *cx = 233; *cy = 233; *hw = *hh = *cr = CARTE_R;
#endif
}
static float zoom_min(void) {
    float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr);
    const float n = (float)(C.ok ? C.n : E.w.n);
    return n > 0 ? 2 * fminf(hw, hh) / n * 0.95f : 1;
}
static void borner(float *x, float *z) {
    const float n = (float)(C.ok ? C.n : E.w.n);
    *x = clampf(*x, 0, n); *z = clampf(*z, 0, n);
}
/* Position de Sera sur la carte (dans un intérieur : le point de retour, devant la porte) */
static void sera_pos(float *x, float *z) {
    if (E.in.on) { *x = E.in.rx; *z = E.in.rz; } else { *x = E.sera.x; *z = E.sera.z; }
}
static const niveau_t *niveau_fin(int dense) {
    const niveau_t *f = NULL;
    for (int i = 0; i < C.nniv; i++) if ((!dense || C.niv[i].dense) && (!f || C.niv[i].e < f->e)) f = &C.niv[i];
    return f;
}
/* Sera dans un lieu bâti : une tuile du niveau le plus fin sous elle */
static int dans_ville(float x, float z) {
    const niveau_t *L = niveau_fin(0);
    if (!L || L->dense || L->e >= 0) return 0;
    const float k = ldexpf(1, -L->e);
    return tuile_de(L, (int)(x * k / CARTE_T), (int)(z * k / CARTE_T)) >= 0;
}
void carte_ouvrir(void) {
    C.ouverte = 1; C.down = C.two = 0; C.btn = -1; C.tap_t = -10; C.calme_t = 0; C.tap_att = 0;
    float x, z; sera_pos(&x, &z);
    if (!BT.on && !accueil_on()) brouillard_voir(x, z, E.w.carte, BROU_R);   /* là où elle est, même juste arrivée (avant l'état des tuiles) */
    charger();
    const float s = C.ok && dans_ville(x, z) ? CARTE_VILLE : CARTE_DEHORS;
    C.x = C.tx = x; C.z = C.tz = z; C.s = C.ts = clampf(s, zoom_min(), CARTE_ZMAX); C.anim = 0;
}
/* Appelée à chaque image par hud.c : la carte est-elle à l'écran (sinon le tampon est rendu) */
void carte_actif(int ouverte) {
    if (ouverte || !C.ouverte) return;
    C.ouverte = 0; C.down = C.two = 0;
    if (C.buf) { img_rendre(TC_CARTE); C.buf = NULL; C.ok = 0; }
}
int carte_plein(void) { return C.ouverte; }
static void viser_zoom(float s, float sx, float sy) {   /* zoom animé vers s, le point de l'écran (sx, sy) fixe */
    float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr);
    s = clampf(s, zoom_min(), CARTE_ZMAX);
    const float wx = C.tx - (sx - cx) / C.ts, wz = C.tz + (sy - cy) / C.ts;   /* x en miroir (CARTE_X) */
    C.ts = s; C.tx = wx + (sx - cx) / s; C.tz = wz - (sy - cy) / s;
    borner(&C.tx, &C.tz); C.anim = 1;
}
static void recentrer(void) {
    float x, z; sera_pos(&x, &z);
    C.tx = x; C.tz = z; C.anim = 1;
    if (C.ok && dans_ville(x, z) && C.ts < CARTE_VILLE * 0.99f) C.ts = CARTE_VILLE;
    else if (C.ts < CARTE_DEHORS) C.ts = clampf(CARTE_DEHORS, zoom_min(), CARTE_ZMAX);
}
void carte_molette(float f) { float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr); viser_zoom(C.ts * f, cx, cy); }
void carte_vue(float x, float z, float s) { C.x = C.tx = x; C.z = C.tz = z; C.s = C.ts = clampf(s, zoom_min(), CARTE_ZMAX); C.anim = 0; }
int carte_calme(void) { return C.ouverte && !C.down && !C.anim && !C.nwant && C.calme_t > 1; }
/* Point du plan sous (sx, sy) de l'écran ; 0 hors de la carte */
int carte_point(float sx, float sy, float *x, float *z) {
    float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr);
    *x = C.x - (sx - cx) / C.s; *z = C.z + (sy - cy) / C.s;   /* x en miroir (CARTE_X) */
    return *x >= 0 && *z >= 0 && *x < E.w.n && *z < E.w.n;
}
int carte_touche(float *x, float *z) { *x = C.px; *z = C.pz; return 1; }

/* Boutons : ‹ − + ◎ sur l'arc du bas (écran rond), en colonne à droite et ‹ en haut à gauche (plein écran) */
static void bouton_pos(int i, float *x, float *y) {
#if ENG_DYN
    if (i == CB_RETOUR) { *x = 44; *y = 56; return; }
    *x = ui_w - 56; *y = ui_h - 70 - (i == CB_PLUS ? 2 : i == CB_MOINS ? 1 : 0) * 70; return;
#else
    static const float A[CB_N] = { 145, 115, 65, 35 };
    const float a = A[i] * PI_F / 180;
    *x = 233 + 190 * cosf(a); *y = 233 + 190 * sinf(a);
#endif
}
static int bouton_at(float x, float y) {
    for (int i = 0; i < CB_N; i++) { float bx, by; bouton_pos(i, &bx, &by); if (hypotf(x - bx, y - by) < 30) return i; }
    return -1;
}

/* Doigts (hud.c les passe ici tant que la carte est ouverte) : CARTE_RETOUR, CARTE_TOUCHE (pilote), sinon 0 */
int carte_doigts(const eng_input_t *in, float now) {
    float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr);
    const float x = in->touch_x, y = in->touch_y;
    C.now = now;
    if (in->touch || in->touch2) C.calme_t = 0;
    if (in->touch && in->touch2) {   /* pincement : le point sous le milieu des doigts reste sous lui */
        const float d = hypotf(in->touch2_x - x, in->touch2_y - y), mx = (x + in->touch2_x) / 2, my = (y + in->touch2_y) / 2;
        if (!C.two) { C.two = 1; C.btn = -1; C.moved = 99; C.tap_att = 0; C.d0 = d > 20 ? d : 20; C.s0 = C.s; C.wx = C.x - (mx - cx) / C.s; C.wz = C.z + (my - cy) / C.s; }
        const float s = clampf(C.s0 * d / C.d0, zoom_min(), CARTE_ZMAX);
        C.s = C.ts = s; C.x = C.tx = C.wx + (mx - cx) / s; C.z = C.tz = C.wz - (my - cy) / s;
        borner(&C.x, &C.z); C.tx = C.x; C.tz = C.z; C.anim = 0; C.down = 1; C.gx = x; C.gy = y;
        return 0;
    }
    if (C.two) {   /* un doigt levé : l'autre continue de déplacer, sans toucher au lever */
        C.two = 0; C.down = in->touch; C.gx = x; C.gy = y; C.moved = 99; C.btn = -1;
        return 0;
    }
    if (in->touch && !C.down) {
        C.down = 1; C.x0 = C.gx = x; C.y0 = C.gy = y; C.moved = 0; C.t0 = now; C.btn = bouton_at(x, y);
        if (C.btn >= 0) C.tap_att = 0;   /* un bouton après un toucher : le toucher ne compte plus */
    } else if (in->touch && C.down) {
        C.moved = fmaxf(C.moved, hypotf(x - C.x0, y - C.y0));
        if (C.btn < 0 && C.moved > 6) {   /* la carte suit le doigt */
            C.x += (x - C.gx) / C.s; C.z -= (y - C.gy) / C.s; borner(&C.x, &C.z);   /* x en miroir (CARTE_X) */
            C.tx = C.x; C.tz = C.z; C.ts = C.s; C.anim = 0;
        }
        C.gx = x; C.gy = y;
    } else if (!in->touch && C.down) {
        C.down = 0;
        if (C.btn >= 0) {
            if (bouton_at(C.gx, C.gy) != C.btn) return 0;
            if (C.btn == CB_RETOUR) return CARTE_RETOUR;
            if (C.btn == CB_MOINS) viser_zoom(C.ts / 2, cx, cy);
            else if (C.btn == CB_PLUS) viser_zoom(C.ts * 2, cx, cy);
            else recentrer();
            son_jouer(SON_MENU, 0.35f, 1);
            return 0;
        }
        if (C.moved > 12) { C.tap_att = 0; return 0; }   /* la carte glissée : pas un toucher */
        if (now - C.tap_t < CARTE_DOUBLE && hypotf(C.gx - C.tap_x, C.gy - C.tap_y) < 40) {   /* double toucher : zoom × 2 là */
            C.tap_t = -10; C.tap_att = 0; viser_zoom(C.ts * 2, C.gx, C.gy); return 0;
        }
        C.tap_t = now; C.tap_x = C.gx; C.tap_y = C.gy;
        C.tap_att = carte_point(C.gx, C.gy, &C.px, &C.pz);   /* rendu une fois passé le temps du double toucher */
        return 0;
    }
    /* Toucher simple sans second toucher à temps : pilote automatique ou téléport (hud.c) */
    if (!in->touch && !C.down && C.tap_att && now - C.tap_t >= CARTE_DOUBLE) { C.tap_att = 0; return CARTE_TOUCHE; }
    return 0;
}
void carte_touche_essai(float x, float z) { C.px = x; C.pz = z; }
/* Touches (pressed : appuyées à cette image, keys : tenues) ; renvoie celles qui restent à hud.c (retour, menu) */
uint32_t carte_touches(uint32_t pressed, const eng_input_t *in, float dt) {
    float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr);
    float vx = 0, vz = 0;
    if (in->manette) {
        vx = fabsf(in->stick_x) < 0.15f ? 0 : in->stick_x;
        vz = fabsf(in->stick_y) < 0.15f ? 0 : in->stick_y;
    }
    else {
        if (in->keys & ENG_KEY_LEFT) vx -= 1;
        if (in->keys & ENG_KEY_RIGHT) vx += 1;
        if (in->keys & ENG_KEY_UP) vz -= 1;
        if (in->keys & ENG_KEY_DOWN) vz += 1;
    }
    if (vx || vz) {   /* 240 pixels d'écran par seconde */
        C.x -= vx * 240 * dt / C.s; C.z += vz * 240 * dt / C.s; borner(&C.x, &C.z);   /* x en miroir (CARTE_X) */
        C.tx = C.x; C.tz = C.z; C.calme_t = 0;
    }
    if (in->keys & (ENG_KEY_ROT_L | ENG_KEY_ROT_R)) {   /* A / E : zoom continu, × 2 en 0,6 s */
        const float f = powf(2, ((in->keys & ENG_KEY_ROT_R) ? 1 : -1) * dt / 0.6f);
        C.s = C.ts = clampf(C.s * f, zoom_min(), CARTE_ZMAX); C.anim = 0; C.calme_t = 0;
    }
    if (pressed & ENG_KEY_OK) { if (C.ts >= CARTE_ZMAX * 0.99f) viser_zoom(zoom_min(), cx, cy); else viser_zoom(C.ts * 2, cx, cy); }
    return pressed & ~(uint32_t)(ENG_KEY_OK | ENG_KEY_UP | ENG_KEY_DOWN | ENG_KEY_LEFT | ENG_KEY_RIGHT);
}
/* Animation (zooms des boutons, recentrage) : rejoint la cible avec un temps de réponse de CARTE_ANIM */
void carte_maj(float dt) {
    if (!C.ouverte) return;
    C.calme_t += dt;
    if (C.perdu && !C.ok) charger();
    if (!C.anim) return;
    C.calme_t = 0;
    const float k = 1 - expf(-dt / CARTE_ANIM);
    C.s += (C.ts - C.s) * k; C.x += (C.tx - C.x) * k; C.z += (C.tz - C.z) * k;
    if (fabsf(C.ts - C.s) < 0.002f * C.ts && fabsf(C.tx - C.x) * C.s < 0.5f && fabsf(C.tz - C.z) * C.s < 0.5f) { C.s = C.ts; C.x = C.tx; C.z = C.tz; C.anim = 0; }
}

/* ---------- Dessin ---------- */
/* Tuiles du niveau L dans la vue ; base : niveau dense dessiné sous un niveau des lieux bâtis (fin) : ses tuiles que celles
   de fin couvrent toutes (lues) ne sont pas dessinées */
static void tuiles(const niveau_t *L, const niveau_t *fin, float cx, float cy, float hw, float hh, float cr) {
    const float k = ldexpf(1, -L->e), m = C.s / k, tw = CARTE_T * m;   /* pixels du plan par case ; d'écran par pixel du plan */
    const float ox = cx + C.x * C.s, oy = cy - C.z * C.s;                /* écran du coin (0, 0) du plan (x en miroir : CARTE_X) */
    int tx0 = (int)floorf((C.x - hw / C.s) * k / CARTE_T), tx1 = (int)floorf((C.x + hw / C.s) * k / CARTE_T);
    int tz0 = (int)floorf((C.z - hh / C.s) * k / CARTE_T), tz1 = (int)floorf((C.z + hh / C.s) * k / CARTE_T);
    if (tx0 < 0) tx0 = 0;
    if (tz0 < 0) tz0 = 0;
    if (tx1 >= L->nt) tx1 = L->nt - 1;
    if (tz1 >= L->nt) tz1 = L->nt - 1;
    const int r = fin ? 1 << (L->e - fin->e) : 0;
    for (int tz = tz0; tz <= tz1; tz++) for (int tx = tx0; tx <= tx1; tx++) {
        const float x = ox - (tx + 1) * tw, y = oy + tz * tw;   /* bord gauche à l'écran : le bout +x de la tuile */
        const float nx = fmaxf(x, fminf(cx, x + tw)) - cx, ny = fmaxf(y, fminf(cy, y + tw)) - cy;
        if (nx * nx + ny * ny > cr * cr) continue;   /* hors du disque */
        const int t = tuile_de(L, tx, tz);
        if (t < 0) continue;
        if (fin) {   /* couverte par le niveau fin (toutes ses tuiles là présentes et lues) : rien dessous */
            int tout = 1;
            for (int j = 0; j < r && tout; j++) for (int i = 0; i < r && tout; i++) {
                const int f = tuile_de(fin, tx * r + i, tz * r + j); int u;
                if (f < 0 || (!tuile_px(f, &u) && u < 0)) tout = 0;
            }
            if (tout) continue;
        }
        int u;
        const uint8_t *px = tuile_px(t, &u);
        const float d = hypotf(x + tw / 2 - cx, y + tw / 2 - cy);
        if (u >= 0) { ui_sprite(&UNI[u], 1, PAL, 1, 1, x, y, tw, 255, cx, cy, cr); continue; }
        if (px) { ui_sprite(px, CARTE_T, PAL, CARTE_T, CARTE_T, x, y, m, 255, cx, cy, cr); ui_miroir(); slot_vu[C.slot_of[t]] = C.trame; continue; }
        demander(t, d);
        /* En attendant : le quart de la tuile du niveau dense au-dessus, s'il est lu */
        for (int i = 0; i < C.nniv; i++) {
            const niveau_t *P = &C.niv[i];
            if (!P->dense || P->e != L->e + 1) continue;
            const int pt = tuile_de(P, tx >> 1, tz >> 1); int pu;
            if (pt < 0) break;
            const uint8_t *pp = tuile_px(pt, &pu);
            if (pu >= 0) ui_sprite(&UNI[pu], 1, PAL, 1, 1, x, y, tw, 255, cx, cy, cr);
            else if (pp) { ui_sprite(pp + (tz & 1) * 16 * CARTE_T + (tx & 1) * 16, CARTE_T, PAL, 16, 16, x, y, 2 * m, 255, cx, cy, cr); ui_miroir(); slot_vu[C.slot_of[pt]] = C.trame; }
            break;
        }
    }
}
/* Tuiles autour de la vue, lues d'avance s'il reste des lectures à cette image */
static void prelire(const niveau_t *L, float hw, float hh) {
    const float k = ldexpf(1, -L->e);
    const int tx0 = (int)floorf((C.x - hw / C.s) * k / CARTE_T) - 1, tx1 = (int)floorf((C.x + hw / C.s) * k / CARTE_T) + 1;
    const int tz0 = (int)floorf((C.z - hh / C.s) * k / CARTE_T) - 1, tz1 = (int)floorf((C.z + hh / C.s) * k / CARTE_T) + 1;
    for (int tz = tz0; tz <= tz1 && C.lus < CARTE_LECTURES; tz++) for (int tx = tx0; tx <= tx1 && C.lus < CARTE_LECTURES; tx++) {
        if (tz != tz0 && tz != tz1 && tx != tx0 && tx != tx1) continue;   /* la couronne seulement */
        const int t = tuile_de(L, tx, tz); int u;
        if (t >= 0 && !tuile_px(t, &u) && u < 0 && !lire_tuile(t)) return;
    }
}

typedef struct { float x0, y0, x1, y1; } boite_t;
static boite_t PRIS[CARTE_NOMS_VUS * 2] ENG_FROID; static int npris;
static int libre(float x0, float y0, float x1, float y1) {
    for (int i = 0; i < npris; i++) if (x0 < PRIS[i].x1 && x1 > PRIS[i].x0 && y0 < PRIS[i].y1 && y1 > PRIS[i].y0) return 0;
    return 1;
}
static void prendre(float x0, float y0, float x1, float y1) { if (npris < CARTE_NOMS_VUS * 2) PRIS[npris++] = (boite_t){ x0, y0, x1, y1 }; }
static float A_CX, A_CY, A_HW, A_HH, A_CR;   /* aire de la carte à cette image (aire) */
static int dedans(float x, float y) {
    return fabsf(x - A_CX) <= A_HW && fabsf(y - A_CY) <= A_HH && (x - A_CX) * (x - A_CX) + (y - A_CY) * (y - A_CY) <= A_CR * A_CR;
}
/* Nom sur une plaque sombre, centré en x, ligne de base y ; 0 si la place est prise ou si la plaque sort de la carte */
static int plaque(const char *s, const ui_font_t *f, float x, float y, uint16_t c, int force) {
    const float w = ui_text_width(f, s), h = (float)f->size;
    const float x0 = x - w / 2 - 5, x1 = x + w / 2 + 5, y0 = y - h * 0.8f - 3, y1 = y + h * 0.25f + 3;
    if (!force && (!libre(x0, y0, x1, y1) || !dedans(x0, y0) || !dedans(x1, y0) || !dedans(x0, y1) || !dedans(x1, y1))) return 0;
    prendre(x0, y0, x1, y1);
    ui_rect(x0, y0, x1 - x0, y1 - y0, ui_rgb(INK), 170);
    ui_text(f, x, y, UI_CENTER, c, 255, s);
    return 1;
}
/* Octogone plein, et son contour (le grand puis le petit, pair-impair) : 8 et 18 sommets, là où un cercle en prend 64 et un
   anneau 132 (MAX_PTS de ui.c : des dizaines de pictogrammes par image) */
static void octo(float x, float y, float r, uint16_t c, int a) {
    float q[16];
    for (int k = 0; k < 8; k++) { const float t = (k + 0.5f) * PI_F / 4; q[k * 2] = x + cosf(t) * r; q[k * 2 + 1] = y + sinf(t) * r; }
    ui_poly(q, 8, c, a);
}
static void octo_bord(float x, float y, float r, float w, uint16_t c, int a) {
    float q[36];
    for (int k = 0; k <= 8; k++) {
        const float t = (k + 0.5f) * PI_F / 4;
        q[k * 2] = x + cosf(t) * r; q[k * 2 + 1] = y + sinf(t) * r;
        q[(17 - k) * 2] = x + cosf(t) * (r - w); q[(17 - k) * 2 + 1] = y + sinf(t) * (r - w);
    }
    ui_poly(q, 18, c, a);
}
/* Losange d'un objectif (r : son demi-côté) : bord sombre, sa couleur, un point sombre au milieu (12 points) */
static void losange(float x, float y, float r, uint16_t c) {
    const float a = r + 3, b = r * 0.38f;
    const float o[8] = { x, y - a, x + a, y, x, y + a, x - a, y }, q[8] = { x, y - r, x + r, y, x, y + r, x - r, y }, p[8] = { x, y - b, x + b, y, x, y + b, x - b, y };
    ui_poly(o, 4, ui_rgb(INK), 230);
    ui_poly(q, 4, c, 255);
    ui_poly(p, 4, ui_rgb(INK), 255);
}
static void bouton_pos(int i, float *x, float *y);
static void noms(float cx, float cy, float hw, float hh, float cr, uint16_t c1) {
    const float ox = cx + C.x * C.s, oy = cy - C.z * C.s;   /* x en miroir (CARTE_X) */
    int vus = 0, pictos[CARTE_NOMS_VUS], np = 0;
    /* Les boutons et le titre gardent leur place */
    for (int b = 0; b < CB_N; b++) { float bx, by; bouton_pos(b, &bx, &by); prendre(bx - 24, by - 24, bx + 24, by + 24); }
    prendre(cx - 120, (ENG_DYN ? 64 : 42) - 24, cx + 120, (ENG_DYN ? 64 : 42) + 8);
    /* Passes par importance : villes, villages, lieux et astres, pictogrammes, campements, noms des bâtiments (sous les
       pictogrammes placés) */
    static const int8_t PASSE[6] = { 0, 1, 3, 4, 2, 6 };
    for (int p = 0; p < 6 && vus < CARTE_NOMS_VUS; p++) {
        const int sorte = PASSE[p] == 6 ? 4 : PASSE[p];
        if (PASSE[p] == 6 && C.s < NOM_BAT_VU) break;
        for (int j = 0; j < (PASSE[p] == 6 ? np : C.nnoms) && vus < CARTE_NOMS_VUS; j++) {
            const int i = PASSE[p] == 6 ? pictos[j] : j;
            const uint8_t *q = C.noms + i * 32;
            const int so = q[4];
            if (!(so == sorte || (sorte == 3 && so == 5)) || C.s < NOM_VU[so < 6 ? so : 3]) continue;
            if (!vu_ici(rd16(q) / 16.0f, rd16(q + 2) / 16.0f)) continue;   /* brouillard : ni nom ni pictogramme */
            const float x = ox - rd16(q) / 16.0f * C.s, y = oy + rd16(q + 2) / 16.0f * C.s;
            if (fabsf(x - cx) > hw - 10 || fabsf(y - cy) > hh - 10 || (x - cx) * (x - cx) + (y - cy) * (y - cy) > (cr - 14) * (cr - 14)) continue;
            const char *nom = (const char *)q + 8;
            if (so == 4) {
                const int g = q[5], col = q[6] < 5 ? q[6] : 0;
                if (PASSE[p] == 6) {   /* le nom sous son pictogramme */
                    if (nom[0] && plaque(nom, ui_font(FONT_DISP, 16), x, y + 30, ui_rgb(NEON[col][0]), 0)) vus++;
                    continue;
                }
                if (!libre(x - 13, y - 13, x + 13, y + 13)) continue;
                prendre(x - 13, y - 13, x + 13, y + 13); vus++; pictos[np++] = i;
                octo(x, y, 13, ui_rgb(INK), 230);
                octo_bord(x, y, 13, 2, ui_rgb(NEON[col][1]), 255);
                if (g > 0 && g <= C.ngly) ui_masque(&GLY[g - 1], x, y, ui_rgb(NEON[col][0]), 255);
                else { const float d[8] = { x, y - 5, x + 5, y, x, y + 5, x - 5, y }; ui_poly(d, 4, ui_rgb(NEON[col][0]), 255); }
                continue;
            }
            const ui_font_t *f = ui_font(FONT_DISP, so == 0 ? 20 : 16);
            const uint16_t c = so == 0 || so == 5 ? ui_rgb(0xFFFFFF) : so == 3 ? c1 : so == 1 ? ui_rgb(0xE8E2D2) : ui_rgb(0xB4AC9C);
            float dy = 8;
            if (so == 5) {   /* astre : son disque (rayon de la sphère, octet du pictogramme), le nom au-dessus */
                const float r = fmaxf(4, q[5] * C.s);
                if (r > 16 && r < cr) { ui_circle(x, y, r, ui_rgb(0xD8D2C0), 255); ui_ring(x, y, r, 2, ui_rgb(0xFFFFFF), 255); }   /* grand : rond */
                else if (r < cr) { octo(x, y, r, ui_rgb(0xD8D2C0), 255); octo_bord(x, y, r, 2, ui_rgb(0xFFFFFF), 255); }
                dy = r + 6;
            } else if (so == 0 || so == 1) octo(x, y, so == 0 ? 4 : 3, c, 255);   /* le point de la ville, le nom au-dessus */
            if (plaque(nom, f, x, y - dy, c, so == 0)) vus++;
        }
    }
}
/* Repère au bord de la vue vers (x, z) hors de la vue (flèche), sinon dessiné par l'appelant ; 1 : dans la vue */
static int dans_vue(float sx, float sy, float cx, float cy, float hw, float hh, float cr) {
    return fabsf(sx - cx) < hw - 8 && fabsf(sy - cy) < hh - 8 && (sx - cx) * (sx - cx) + (sy - cy) * (sy - cy) < (cr - 8) * (cr - 8);
}
static void fleche_bord(float sx, float sy, float cx, float cy, float hw, float hh, float cr, uint16_t c) {
    const float a = atan2f(sy - cy, sx - cx), ca = cosf(a), sa = sinf(a);
    float d = cr - 16;
    if (hw < cr) { const float dx = fabsf(ca) > 1e-3f ? (hw - 16) / fabsf(ca) : 1e9f, dy = fabsf(sa) > 1e-3f ? (hh - 16) / fabsf(sa) : 1e9f; d = fminf(dx, dy); }
    const float x = cx + ca * d, y = cy + sa * d;
    const float t[6] = { x + ca * 10, y + sa * 10, x - ca * 6 - sa * 7, y - sa * 6 + ca * 7, x - ca * 6 + sa * 7, y - sa * 6 - ca * 7 };
    ui_poly(t, 3, c, 255);
}
static void bouton(int i, uint16_t c1, uint16_t c2) {
    float x, y; bouton_pos(i, &x, &y);
    const int on = C.down && C.btn == i;
    ui_circle(x, y, 21, ui_rgb(INK), 225);
    ui_ring(x, y, 21, 2, on ? c2 : c1, on ? 255 : 200);
    const uint16_t w = ui_rgb(0xFFFFFF);
    if (i == CB_RETOUR) ui_icon("back", x, y, 26, c1, 255);
    else if (i == CB_CENTRE) { ui_ring(x, y, 8, 2, w, 255); ui_circle(x, y, 2.5f, w, 255); ui_rect(x - 1, y - 14, 2, 5, w, 255); ui_rect(x - 1, y + 9, 2, 5, w, 255); ui_rect(x - 14, y - 1, 5, 2, w, 255); ui_rect(x + 9, y - 1, 5, 2, w, 255); }
    else { ui_rect(x - 9, y - 1.5f, 18, 3, w, 255); if (i == CB_PLUS) ui_rect(x - 1.5f, y - 9, 3, 18, w, 255); }
}
/* Titre : la ville la plus proche du centre de la vue s'il est dans un lieu bâti et assez zoomé, sinon la carte */
static const char *titre(void) {
    if (C.ok && C.s >= 4 && dans_ville(C.x, C.z) && vu_ici(C.x, C.z)) {
        const char *best = NULL; float bd = 1e9f;
        for (int i = 0; i < C.nnoms; i++) {
            const uint8_t *q = C.noms + i * 32;
            if (q[4] > 2 || !vu_ici(rd16(q) / 16.0f, rd16(q + 2) / 16.0f)) continue;   /* (pas une ville du brouillard) */
            const float d = hypotf(rd16(q) / 16.0f - C.x, rd16(q + 2) / 16.0f - C.z);
            if (d < bd) { bd = d; best = (const char *)q + 8; }
        }
        if (best && bd < 120) return best;
    }
    return E.w.cartes[E.w.carte].nom;
}

void carte_dessiner(uint16_t c1, uint16_t c2, const carte_reperes_t *rp) {
    float cx, cy, hw, hh, cr; aire(&cx, &cy, &hw, &hh, &cr);
    ui_translate(0, 0);
    C.trame++; C.nwant = 0; C.lus = 0; npris = 0;
    A_CX = cx; A_CY = cy; A_HW = hw; A_HH = hh; A_CR = cr;
    if (!C.ok) charger();
#if ENG_DYN
    ui_voile_image(ui_rgb(INK), 255);   /* toute l'image, barres du système comprises (la 3D n'y est pas noircie) */
#else
    ui_circle(cx, cy, cr + 4, ui_rgb(INK), 255);
#endif
    if (C.ok) {
        /* Niveau : le plus fin qui donne au moins CARTE_SMIN pixels d'écran par pixel du plan ; un niveau des lieux bâtis
           se dessine sur le niveau dense le plus fin qui le permet (ailleurs que sur les lieux bâtis) */
        const niveau_t *L = NULL, *B = NULL;
        for (int i = 0; i < C.nniv; i++) {
            const niveau_t *n = &C.niv[i];
            const float m = C.s * ldexpf(1, n->e);
            if (m >= CARTE_SMIN && (!L || n->e < L->e)) L = n;
            if (n->dense && m >= CARTE_SMIN && (!B || n->e < B->e)) B = n;
        }
        if (!L) for (int i = 0; i < C.nniv; i++) if (!L || C.niv[i].e > L->e) L = &C.niv[i];
        if (!L->dense && !B) B = niveau_fin(1);
        if (!L->dense && B) tuiles(B, L, cx, cy, hw, hh, cr);
        tuiles(L, NULL, cx, cy, hw, hh, cr);
        /* Lectures : les tuiles demandées, les plus proches du centre d'abord, puis la couronne autour de la vue */
        for (int n = 0; n < C.nwant && C.lus < CARTE_LECTURES; n++) {
            int b = n;
            for (int i = n + 1; i < C.nwant; i++) if (C.wantd[i] < C.wantd[b]) b = i;
            const int16_t t = C.want[b]; const float d = C.wantd[b];
            C.want[b] = C.want[n]; C.wantd[b] = C.wantd[n]; C.want[n] = t; C.wantd[n] = d;
            if (!lire_tuile(t)) break;
        }
        if (!C.nwant && C.lus < CARTE_LECTURES) { prelire(L, hw, hh); if (!L->dense && B) prelire(B, hw, hh); }
    } else if (E.w.map_rgb && E.w.map_n > 0) {   /* paquet sans PLAN : l'ancienne carte (MAP), agrandie de même */
        const float k = (float)E.w.map_n / E.w.n;
        ui_image_rgb(E.w.map_rgb, E.w.map_n, E.w.map_n, cx - (E.w.n - C.x) * C.s, cy - C.z * C.s, C.s / k, cx, cy, cr);
        ui_miroir();
    }
    /* Repères : guidage, pilote, objectif, puis les noms, et Sera par-dessus */
    const float ox = cx + C.x * C.s, oy = cy - C.z * C.s;   /* x en miroir (CARTE_X) */
    const uint16_t vert = ui_rgb(GUIDE_RGB);
    if (rp && rp->guide) {
        const float x = ox - rp->gx * C.s, y = oy + rp->gz * C.s;
        if (dans_vue(x, y, cx, cy, hw, hh, cr)) { ui_ring(x, y, 9, 2, vert, 255); ui_circle(x, y, 3, vert, 255); } else fleche_bord(x, y, cx, cy, hw, hh, cr, vert);
    }
    if (rp && rp->pil) {
        const float x = ox - rp->px * C.s, y = oy + rp->pz * C.s;
        if (dans_vue(x, y, cx, cy, hw, hh, cr)) { ui_ring(x, y, 12, 2, vert, 255); ui_ring(x, y, 6, 2, vert, 255); }
    }
    noms(cx, cy, hw, hh, cr, c1);
    {   /* Marques des personnages de la carte (panneaux.c : « ! » ouvre une quête, « ? » l'étape en cours), et les panneaux de
           quêtes qui en proposent une (carteDessiner de l'émulateur) */
        float mx[MARQUES_CARTE], mz[MARQUES_CARTE];
        uint8_t mm[MARQUES_CARTE];
        const int nm = marques_points(mx, mz, mm, MARQUES_CARTE);
        for (int i = 0; i < nm; i++) {
            const float x = ox - mx[i] * C.s, y = oy + mz[i] * C.s;
            if ((mm[i] == MQ_OUVRE || mm[i] == MQ_OUVRE_PRINC) && !vu_ici(mx[i], mz[i])) continue;   /* « ! » dans le brouillard : à découvrir */
            if (dans_vue(x, y, cx, cy, hw, hh, cr)) marque_dessin(x, y - 10, mm[i], 0.7f);
        }
        const uint16_t or_ = ui_rgb(MQ_RGB);
        for (int k = 0; k < panneau_n(); k++) {
            float px, pz; int pc;
            if (!panneau_lieu(k, &px, &pz, &pc) || pc != E.w.carte || !panneau_quetes(k) || !vu_ici(px, pz)) continue;
            const float x = ox - px * C.s, y = oy + pz * C.s;
            if (!dans_vue(x, y, cx, cy, hw, hh, cr)) continue;
            ui_rect(x - 9, y - 8, 18, 12, ui_rgb(INK), 230);   /* le panneau : planche et pied */
            const float b[8] = { x - 9, y - 8, x + 9, y - 8, x + 9, y + 4, x - 9, y + 4 };
            ui_outline(b, 4, 1.5f, or_, 255);
            ui_line(x - 5, y - 4, x + 5, y - 4, 1, or_, 255); ui_line(x - 5, y, x + 3, y, 1, or_, 255);
            ui_rect(x - 1, y + 4, 2, 6, or_, 255);
        }
    }
    {   /* Objectifs des autres quêtes ouvertes (script_objectifs) : un losange cyan (principale) ou or (secondaire), même dans
           le brouillard (c'est une indication) ; sur une personne, sa marque « ? » le montre déjà */
        static float qx[OBJ_CARTE] ENG_FROID, qz[OBJ_CARTE] ENG_FROID;   /* (hors de la pile : 8 Ko sur l'ESP32) */
        static uint8_t qg[OBJ_CARTE] ENG_FROID;
        const int no = script_objectifs(qx, qz, qg, OBJ_CARTE);
        for (int i = 0; i < no; i++) {
            if (qg[i] & OBJ_PERSO) continue;
            const float x = ox - qx[i] * C.s, y = oy + qz[i] * C.s;
            if (!dans_vue(x, y, cx, cy, hw, hh, cr) || (rp && rp->obj && fabsf(qx[i] - rp->ox) < 1 && fabsf(qz[i] - rp->oz) < 1)) continue;
            losange(x, y, 7, ui_rgb(qg[i] & OBJ_PRINC ? MQ_RGB_PRINC : MQ_RGB));
        }
    }
    if (rp && rp->obj) {   /* la quête suivie (guidage) : cible dans un anneau, sinon une flèche au bord, de sa couleur */
        const float x = ox - rp->ox * C.s, y = oy + rp->oz * C.s;
        const uint16_t c = ui_rgb(rp->obj_princ ? MQ_RGB_PRINC : MQ_RGB);
        if (dans_vue(x, y, cx, cy, hw, hh, cr)) {
            octo(x, y, 15, ui_rgb(INK), 150);
            ui_ring(x, y, 13, 2.5f, c, 255);
            losange(x, y, 6, c);
        } else fleche_bord(x, y, cx, cy, hw, hh, cr, c);
    }
    {
        float sx0, sz0; sera_pos(&sx0, &sz0);
        const float px = ox - sx0 * C.s, py = oy + sz0 * C.s;
        if (dans_vue(px, py, cx, cy, hw, hh, cr)) {
            const float ds = -sinf(E.sera.dir), dc = cosf(E.sera.dir);   /* cap à l'écran, x en miroir */
            const float tri[6] = { px + ds * 12, py + dc * 12, px - dc * 7 - ds * 6, py + ds * 7 - dc * 6, px + dc * 7 - ds * 6, py - ds * 7 - dc * 6 };
            ui_circle(px, py, 15, ui_rgb(INK), 120);
            ui_poly(tri, 3, c2, 255);
            ui_ring(px, py, 15, 2, c2, 160);
        } else fleche_bord(px, py, cx, cy, hw, hh, cr, c2);
    }
    /* Habillage : bord, titre, boutons */
#if !ENG_DYN
    ui_ring(cx, cy, cr + 2, 2, c1, 200);
#endif
    {
        const char *t = titre();
        const ui_font_t *f = ui_fit(FONT_DISP, t, 220, 20, 16);
        const float w = ui_text_width(f, t), y = ENG_DYN ? 64 : 42;
        float q[12]; ui_chamfer_path(cx - w / 2 - 16, y - 20, w + 32, 28, 7, q);
        ui_poly(q, 6, ui_rgb(INK), 215); ui_outline(q, 6, 1, c1, 150);
        ui_text(f, cx, y, UI_CENTER, ui_rgb(0xFFFFFF), 255, t);
        /* Sous le titre : le dernier message (refus du téléport…), sinon ce que fait un toucher */
        const char *aide = rp && rp->aide ? rp->aide : rp && rp->pilote_ok ? "Toucher : pilote automatique" : rp && rp->tele_ok ? "Toucher : se téléporter" : NULL;
#if ENG_DYN
        const float aw = ui_w - 48.0f;
#else
        const float aw = 300;
#endif
        if (aide) {   /* sur une plaque sombre : lisible par-dessus le plan */
            const ui_font_t *fa = ui_fit(FONT_MONO, aide, aw, 18, 14);
            const float wa = ui_text_width(fa, aide);
            ui_rect(cx - wa / 2 - 6, y + 26 - fa->size * 0.8f - 2, wa + 12, fa->size + 4.0f, ui_rgb(INK), 170);
            ui_text(fa, cx, y + 26, UI_CENTER, c2, 255, aide);
        }
    }
    for (int i = 0; i < CB_N; i++) bouton(i, c1, c2);
}

/* Banc d'essai (rendu_test carte) : état de la carte ouverte */
void carte_etat(char *out, int n) {
    int prets = 0, et[3] = { 0, 0, 0 };
    for (int s = 0; s < C.nslots; s++) prets += slot_key[s] >= 0;
    for (int t = 0; t < C.ntuiles && C.ok && C.etat; t++) et[C.etat[t] < 3 ? C.etat[t] : 2]++;
    if (C.ok && C.brou) {   /* brouillard : tuiles toutes vues, sans rien de vu, en partie */
        int no = 0;
        for (int c = 1; c <= C.npal; c++) no += OUVR[c];
        const int o = snprintf(out, n, "brouillard de la carte : %d tuiles vues, %d cachées, %d en partie (%u µs), %d couleurs d'ouvrages ; ", et[0], et[1], et[2], (unsigned)C.us_etat, no);
        if (o > 0 && o < n) { out += o; n -= o; }
    }
    snprintf(out, n, "plan %s (carte %d, %d cartes au paquet) : côté %d, %d niveaux, %d tuiles, %d noms, %d couleurs ; tampon %u o (répertoire %u, %d places, %d lues) ; vue %.1f %.1f, zoom %.2f px/case ; %u tuiles lues en tout",
             C.ok ? "chargé" : "absent", C.carte, C.nc, C.n, C.nniv, C.ntuiles, C.nnoms, C.npal, (unsigned)C.cap, C.ok ? (unsigned)C.pc[C.carte].dir : 0u, C.nslots, prets,
             C.x, C.z, C.s, (unsigned)C.lus_total);
}

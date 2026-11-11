/* Couche 2D de l'interface (voir ui.h) */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "internal.h"
#include "ui.h"

/* Pires cas mesurés le 28/10/2026 (rendu_test, 72 écrans : titre, menus et sous-menus, combats, course, intérieurs) :
   428 opérations (intérieur meublé, menu ouvert), 7 818 points (écran titre) ; en double en PSRAM : 228 Ko (au lieu de
   490 avec 3 000 et 30 000, qui ne tenaient plus avec le monde de 1 024) */
#ifndef MAX_OPS
#define MAX_OPS 1200
#endif
#ifndef MAX_PTS
#define MAX_PTS 16000
#endif
#define MAX_X 96

enum { OP_POLY, OP_MASK, OP_IMAGE, OP_RMASK, OP_SPRITE, OP_HALO, OP_BORD, OP_VOILE };   /* OP_VOILE : polygone de ui_voile_image (toute l'image, uni) */   /* OP_SPRITE : image indexée, index 0 transparent, lignes de c octets ; OP_BORD : halo des bords de l'image (plein écran) */
typedef struct {
    uint8_t kind, a;
    uint16_t c;
    int16_t ymin, ymax;
    union {
        struct { uint32_t p0, np; } poly;
        struct { const ui_mask_t *m; int16_t x, y; } mask;
        struct { const ui_mask_t *m; float ox, oy, co, si; int16_t x0, x1; } rmask;   /* glyphe tourné : origine, angle */
        /* image : px (masque msk, ou aucun), ou rgb (3 octets par pixel, convertis au dessin : carte du menu) */
        struct { const uint16_t *px; const uint8_t *msk, *rgb; uint16_t w, h; float x, y, s, ccx, ccy, cr; } img;
        struct { float cx, cy, r0, r1; uint8_t g2; } halo;   /* anneau dégradé (ui_halo) */
        struct { float x0, y0, x1, y1, e; } bord;            /* bords de l'image, épaisseur e (ui_halo_bord) */
    } u;
} op_t;

/* Liste d'opérations en double, comme les données 3D : UB se remplit (cœur 0), UR se dessine (cœur 1) */
typedef struct { op_t *ops; float *pts; int nops, npts; } ui_frame_t;
static ui_frame_t UF[2], *UB = &UF[0], *UR = &UF[0];
static int swap;
/* Disque de l'écran : colonnes [circ_l[y], circ_r[y]] de chaque ligne native (calculées une fois) */
static int16_t circ_l[ENG_SCREEN], circ_r[ENG_SCREEN];
static void circle_spans(void) {
    for (int y = 0; y < ENG_SCREEN; y++) {
        float d = y + 0.5f - ENG_SCREEN / 2.0f, r2 = (ENG_SCREEN / 2.0f) * (ENG_SCREEN / 2.0f) - d * d, hw = r2 > 0 ? sqrtf(r2) : 0;
        circ_l[y] = (int16_t)ceilf(ENG_SCREEN / 2.0f - hw - 0.5f); circ_r[y] = (int16_t)floorf(ENG_SCREEN / 2.0f + hw - 0.5f);
    }
}
/* Origine de l'interface dans l'image : (0, 0) sur l'écran rond ; plein écran (ENG_DYN) : le cadre de 466 × 466 centré,
   posé par eng_set_screen. Les opérations restent dans le repère du cadre, décalées au dessin. */
#if ENG_RECT
/* Écran rectangulaire (RetroFW) : le cadre de 466 au milieu de l'image ; le canevas va jusqu'à son bord droit et son bas
   (panneau du dialogue sur toute la largeur), la mise en page reste celle de la montre (UW, UH de hud.c) */
int ui_ox = (ENG_IMG_W - ENG_SCREEN) / 2, ui_oy = (ENG_IMG_H - ENG_SCREEN) / 2;
int ui_w = ENG_IMG_W - (ENG_IMG_W - ENG_SCREEN) / 2, ui_h = ENG_IMG_H - (ENG_IMG_H - ENG_SCREEN) / 2;
#else
int ui_ox, ui_oy;
/* Taille du canevas de l'interface : 466 × 466 (écran rond), ou toute l'image (plein écran : eng_set_screen) */
int ui_w = ENG_SCREEN, ui_h = ENG_SCREEN;
#endif
/* Taille de toute l'image (plein écran : eng_set_screen) ; le canevas peut s'arrêter avant, aux marges de la barre d'état et
   de la barre de navigation (eng_set_safe) */
int ui_img_w = ENG_IMG_W, ui_img_h = ENG_IMG_H;
/* Décalage appliqué à l'enregistrement de chaque opération (ui_translate) : un écran pas encore refait pour le plein
   écran se dessine dans un cadre de 466 × 466 centré */
static float tx, ty;
void ui_translate(float x, float y) { tx = x; ty = y; }
float ui_tx(void) { return tx; }
float ui_ty(void) { return ty; }
static ui_font_t fonts[16] ENG_FROID;   /* en PSRAM (08/11/2026 : la SRAM interne va aux tampons d'envoi à l'écran) */
static int nfonts;
static ui_mask_t *icons;
static char (*icon_names)[9];
static uint8_t *icon_sizes;
static int nicons;

static inline uint16_t sw16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

uint16_t ui_mix(uint16_t a, uint16_t b, float t) {
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + (int)lroundf((br - ar) * t), g = ag + (int)lroundf((bg - ag) * t), bl = ab + (int)lroundf((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* Écrit c (natif) sur le pixel *d avec l'opacité a (0..255). Mélange RGB565 en une seule multiplication : les trois
   composantes sont écartées dans un mot de 32 bits (masque 0x07E0F81F), opacité ramenée sur 5 bits. */
static inline void blend(uint16_t *d, uint16_t c, int a) {
    if (a >= 255) { *d = swap ? sw16(c) : c; return; }
    uint32_t o = swap ? sw16(*d) : *d, a5 = (uint32_t)(a + 4) >> 3;
    uint32_t bg = (o | (o << 16)) & 0x07E0F81Fu, fg = ((uint32_t)c | ((uint32_t)c << 16)) & 0x07E0F81Fu;
    uint32_t m = ((fg * a5 + bg * (32 - a5)) >> 5) & 0x07E0F81Fu;
    uint16_t v = (uint16_t)(m | (m >> 16));
    *d = swap ? sw16(v) : v;
}

/* Bande en cours de dessin : lignes [by0, by1), colonnes [x0, x1), dst de largeur w. Passée en paramètre : les deux
   cœurs peuvent dessiner chacun une bande en même temps. */
typedef struct { uint16_t *dst; int by0, by1, x0, x1, w; } band_t;

static inline int ceil_half(float v) { return 8192 - (int)(8192.5f - v); }

/* ---------- Chargement des polices et des icônes ---------- */
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static const uint8_t *read_mask(const uint8_t *p, ui_mask_t *m) {
    m->x0 = (int8_t)p[0]; m->y0 = (int8_t)p[1]; m->w = p[2]; m->h = p[3];
    m->data = p + 4;
    return p + 4 + ((m->w + 1) >> 1) * m->h;
}

int ui_init(void) {
    circle_spans();
    for (int k = 0; k < 2; k++) {
        UF[k].ops = E.cfg.alloc(sizeof(op_t) * MAX_OPS, 0);
        UF[k].pts = E.cfg.alloc(sizeof(float) * MAX_PTS, 0);
        if (!UF[k].ops || !UF[k].pts) { E.error = "mémoire insuffisante (interface)"; return -1; }
    }
    uint32_t sz;
    const uint8_t *p = pak_section("FONT", &sz);
    if (p) {
        uint32_t n = rd32(p); p += 4;
        for (uint32_t i = 0; i < n && nfonts < 16; i++) {
            ui_font_t *f = &fonts[nfonts++];
            f->style = p[0]; f->size = p[1]; f->n = rd16(p + 2); p += 4;
            f->g = E.cfg.alloc(sizeof(ui_mask_t) * f->n, 0);
            if (!f->g) { E.error = "mémoire insuffisante (polices)"; return -1; }
            memset(f->ascii, 0, sizeof f->ascii);
            for (int k = 0; k < f->n; k++) {
                ui_mask_t *g = &f->g[k];
                g->cp = rd16(p); g->adv16 = rd16(p + 2);
                p = read_mask(p + 4, g);
                if (g->cp < 128) f->ascii[g->cp] = g;
            }
        }
    }
    p = pak_section("ICON", &sz);
    if (p) {
        nicons = (int)rd32(p); p += 4;
        icons = E.cfg.alloc(sizeof(ui_mask_t) * nicons, 0);
        icon_names = E.cfg.alloc(9 * nicons, 0);
        icon_sizes = E.cfg.alloc(nicons, 0);
        if (!icons || !icon_names || !icon_sizes) { E.error = "mémoire insuffisante (icônes)"; return -1; }
        for (int i = 0; i < nicons; i++) {
            memcpy(icon_names[i], p, 8); icon_names[i][8] = 0;
            icon_sizes[i] = p[8];
            p = read_mask(p + 9, &icons[i]);
        }
    }
    return 0;
}

/* ---------- Liste d'opérations ---------- */
/* Couleur 565 dans l'ordre des octets des bandes envoyées */
uint16_t ui_native(uint16_t c) { return swap ? sw16(c) : c; }

static int img_der = -1;   /* rang de la dernière image enregistrée à cette image (ui_miroir), -1 : aucune */
void ui_begin(int swap565) { UB->nops = UB->npts = 0; swap = swap565; img_der = -1; }
int ui_op_count(void) { return UB->nops; }
int ui_pts_count(void) { return UB->npts; }
void ui_present(void) { UR = UB; UB = UB == &UF[0] ? &UF[1] : &UF[0]; }

/* Interface en 320 × 240 natif (ENG_UI_NATIF, écran rectangulaire, 11/11/2026, choix de l'utilisateur : « interface en 320
   natif » ; avant : dessinée en 640 × 480 sur la 3D doublée, puis réduite de moitié, 4 à 6 ms par image sur la LDK). La mise
   en page reste dans le repère de l'interface (466 × 466 au milieu de l'image de 640 × 480 : hud.c inchangé) ; chaque
   opération est enregistrée dans l'image de 320 × 240 (NX, NY : la moitié de la position dans l'image de 640 × 480).
   Formes, images et halos se dessinent ensuite tels quels (au pixel, sans antialiasing) ; glyphes et icônes gardent leur
   masque d'origine, moyenné 2 × 2 au dessin (band_mask_demi, band_rmask_demi : le même lissage qu'avant la réduction) */
#ifndef ENG_UI_NATIF
#define ENG_UI_NATIF ENG_RECT
#endif
#if ENG_UI_NATIF
#define NX(x) (((x) + ui_ox) * 0.5f)
#define NY(y) (((y) + ui_oy) * 0.5f)
#define UI_HAUT ((float)ENG_H_PX)
#else
#define UI_HAUT ((float)ui_h)
#endif
static op_t *new_op(int kind, uint16_t c, int a, float ymin, float ymax) {
    if (UB->nops >= MAX_OPS || a <= 0) return NULL;
    if (ymax < 0 || ymin >= UI_HAUT) return NULL;
    op_t *o = &UB->ops[UB->nops++];
    o->kind = (uint8_t)kind; o->c = c; o->a = (uint8_t)(a > 255 ? 255 : a);
    o->ymin = (int16_t)floorf(ymin < 0 ? 0 : ymin); o->ymax = (int16_t)ceilf(ymax > UI_HAUT ? UI_HAUT : ymax);
    return o;
}

void ui_poly(const float *xy, int n, uint16_t c, int alpha) {
    if (n < 3 || UB->npts + n * 3 > MAX_PTS) return;
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) { ymin = fminf(ymin, xy[i * 2 + 1] + ty); ymax = fmaxf(ymax, xy[i * 2 + 1] + ty); }
#if ENG_UI_NATIF
    ymin = NY(ymin); ymax = NY(ymax);
#endif
    op_t *o = new_op(OP_POLY, c, alpha, ymin, ymax);
    if (!o) return;
    /* Sommets rangés par trois : x, y, pente dx/dy de l'arête vers le suivant (aucune division par ligne au dessin) */
    float *q = &UB->pts[UB->npts];
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        float dy = xy[j * 2 + 1] - xy[i * 2 + 1];
#if ENG_UI_NATIF
        q[i * 3] = NX(xy[i * 2] + tx); q[i * 3 + 1] = NY(xy[i * 2 + 1] + ty);   /* (pente inchangée : même échelle en x et en y) */
#else
        q[i * 3] = xy[i * 2] + tx; q[i * 3 + 1] = xy[i * 2 + 1] + ty;
#endif
        q[i * 3 + 2] = dy != 0 ? (xy[j * 2] - xy[i * 2]) / dy : 0;
    }
    o->u.poly.p0 = (uint32_t)UB->npts; o->u.poly.np = (uint32_t)n;
    UB->npts += n * 3;
}

void ui_rect(float x, float y, float w, float h, uint16_t c, int alpha) {
    float q[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    ui_poly(q, 4, c, alpha);
}

/* Voile sur toute l'image, marges du téléphone comprises (barre d'état, barre de navigation : hors du canevas, où new_op
   rogne tout le reste), sans le décalage d'ui_translate ; sur l'écran rond, le canevas est l'image (01/11/2026 : les voiles
   des sorts, l'éclair et les fondus laissaient des bandes claires en haut et en bas du téléphone) */
void ui_voile_image(uint16_t c, int alpha) {
    if (UB->nops >= MAX_OPS || alpha <= 0 || UB->npts + 12 > MAX_PTS) return;
#if ENG_UI_NATIF
    const float x0 = 0, x1 = (float)ENG_W, y0 = 0, y1 = (float)ENG_H_PX;   /* toute l'image de 320 × 240 */
#else
    const float x0 = (float)-ui_ox, x1 = (float)(ui_img_w - ui_ox), y0 = (float)-ui_oy, y1 = (float)(ui_img_h - ui_oy);
#endif
    op_t *o = &UB->ops[UB->nops++];
    o->kind = ENG_RECT ? OP_VOILE : OP_POLY; o->c = c; o->a = (uint8_t)(alpha > 255 ? 255 : alpha);   /* (dessiné comme un polygone ; écran rectangulaire : en 320 × 240, ui_voile_bande) */
    o->ymin = (int16_t)y0; o->ymax = (int16_t)y1;
    const float xy[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
    float *q = &UB->pts[UB->npts];
    for (int i = 0; i < 4; i++) {
        const int j = (i + 1) & 3;
        const float dy = xy[j * 2 + 1] - xy[i * 2 + 1];
        q[i * 3] = xy[i * 2]; q[i * 3 + 1] = xy[i * 2 + 1];
        q[i * 3 + 2] = dy != 0 ? (xy[j * 2] - xy[i * 2]) / dy : 0;
    }
    o->u.poly.p0 = (uint32_t)UB->npts; o->u.poly.np = 4;
    UB->npts += 12;
}

void ui_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    const float k = w / 2 / l, nx = -dy * k, ny = dx * k;   /* (une division au lieu de deux : logicielle sur l'ESP32) */
    float q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    ui_poly(q, 4, c, alpha);
}

void ui_outline(const float *xy, int n, float w, uint16_t c, int alpha) {
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        ui_line(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], w, c, alpha);
    }
}

#define SEGS 64
void ui_circle(float cx, float cy, float r, uint16_t c, int alpha) {
    float q[SEGS * 2];
    for (int i = 0; i < SEGS; i++) { float a = i * 2 * PI_F / SEGS; q[i * 2] = cx + cosf(a) * r; q[i * 2 + 1] = cy + sinf(a) * r; }
    ui_poly(q, SEGS, c, alpha);
}

/* Arc épais = contour extérieur de a0 à a1 puis intérieur de a1 à a0 (pair-impair) */
void ui_arc(float cx, float cy, float r, float a0, float a1, float w, uint16_t c, int alpha) {
    int n = (int)(fabsf(a1 - a0) / (2 * PI_F) * SEGS) + 2;
    if (n > SEGS + 1) n = SEGS + 1;
    float q[(SEGS + 2) * 4];
    float ro = r + w / 2, ri = r - w / 2;
    for (int i = 0; i < n; i++) {
        float a = a0 + (a1 - a0) * i / (n - 1);
        q[i * 2] = cx + cosf(a) * ro; q[i * 2 + 1] = cy + sinf(a) * ro;
        q[(2 * n - 1 - i) * 2] = cx + cosf(a) * ri; q[(2 * n - 1 - i) * 2 + 1] = cy + sinf(a) * ri;
    }
    ui_poly(q, 2 * n, c, alpha);
}

void ui_ring(float cx, float cy, float r, float w, uint16_t c, int alpha) { ui_arc(cx, cy, r, 0, 2 * PI_F, w, c, alpha); }

void ui_ellipse_ring(float cx, float cy, float rx, float ry, float w, uint16_t c, int alpha) {
    const int n = 32;
    float q[(n + 1) * 4];
    for (int i = 0; i <= n; i++) {
        float a = i * 2 * PI_F / n;
        q[i * 2] = cx + cosf(a) * (rx + w / 2); q[i * 2 + 1] = cy + sinf(a) * (ry + w / 2);
        q[(2 * n + 1 - i) * 2] = cx + cosf(a) * (rx - w / 2); q[(2 * n + 1 - i) * 2 + 1] = cy + sinf(a) * (ry - w / 2);
    }
    ui_poly(q, 2 * (n + 1), c, alpha);
}

void ui_hex_path(float cx, float cy, float w, float h, float *q) {
    float v[12] = { cx - w / 4, cy - h / 2, cx + w / 4, cy - h / 2, cx + w / 2, cy, cx + w / 4, cy + h / 2, cx - w / 4, cy + h / 2, cx - w / 2, cy };
    memcpy(q, v, sizeof v);
}

void ui_chamfer_path(float x, float y, float w, float h, float c, float *q) {
    float v[12] = { x + c, y, x + w, y, x + w, y + h - c, x + w - c, y + h, x, y + h, x, y + c };
    memcpy(q, v, sizeof v);
}

/* ---------- Texte ---------- */
static uint32_t utf8_next(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xF0) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

static const ui_mask_t *glyph(const ui_font_t *f, uint32_t cp) {
    if (cp < 128) return f->ascii[cp] ? f->ascii[cp] : f->ascii['?'];
    for (int i = 0; i < f->n; i++) if (f->g[i].cp == cp) return &f->g[i];
    /* Signes typographiques absents de la police exportée : leur équivalent simple plutôt que « ? » (01/11/2026 : le
       moins de « −40 CR » des boutiques) */
    const int rep = cp == 0x2212 || cp == 0x2013 || cp == 0x2014 ? '-' : cp == 0x2019 || cp == 0x2018 ? '\''
                  : cp == 0xA0 || cp == 0x202F ? ' ' : '?';
    return f->ascii[rep] ? f->ascii[rep] : f->ascii['?'];
}

const ui_font_t *ui_font(int style, int size) {
    const ui_font_t *best = NULL, *small = NULL;
    for (int i = 0; i < nfonts; i++) {
        const ui_font_t *f = &fonts[i];
        if (f->style != style) continue;
        if (f->size <= size && (!best || f->size > best->size)) best = f;
        if (!small || f->size < small->size) small = f;
    }
    return best ? best : small;
}

/* Filtre des textes (ui_filtre) : le texte filtré, le temps d'un appel (l'interface se construit sur un seul fil) */
static ui_filtre_t filtre;
static char filtre_buf[256];
ui_filtre_t ui_filtre(ui_filtre_t f) { const ui_filtre_t a = filtre; filtre = f; return a; }
static const char *filtrer(const char *s) { return filtre && s ? filtre(s, filtre_buf, (int)sizeof filtre_buf) : s; }

static float text_w(const ui_font_t *f, const char *s) {
    if (!f) return 0;
    float w = 0;
    while (*s) { const ui_mask_t *g = glyph(f, utf8_next(&s)); if (g) w += g->adv16 / 16.0f; }
    return w;
}
const char *ui_fin_mot(const char *p) {
    for (;;) {
        while (*p && *p != ' ') p++;
        if (*p != ' ') return p;
        const uint8_t c = (uint8_t)p[1];
        const int k = (c == '?' || c == '!' || c == ':' || c == ';') ? 2 : (c == 0xC2 && (uint8_t)p[2] == 0xBB) ? 3 : 0;   /* (0xC2 0xBB : « ») */
        if (!k || (p[k] && p[k] != ' ')) return p;
        p += k;
    }
}
float ui_text_width(const ui_font_t *f, const char *s) { return text_w(f, filtrer(s)); }

const ui_font_t *ui_fit(int style, const char *s, float max_w, int size, int min) {
    const ui_font_t *f = ui_font(style, size);
    s = filtrer(s);
    while (f && text_w(f, s) > max_w && f->size > min) {
        const ui_font_t *g = ui_font(style, f->size - 1);
        if (!g || g == f) break;
        f = g;
    }
    return f;
}

float ui_text(const ui_font_t *f, float x, float y, int align, uint16_t c, int alpha, const char *s) {
    if (!f) return 0;
    s = filtrer(s);
    float w = text_w(f, s);
    float pen = (align == UI_CENTER ? x - w / 2 : align == UI_RIGHT ? x - w : x) + tx;
    y += ty;
    while (*s) {
        const ui_mask_t *g = glyph(f, utf8_next(&s));
        if (!g) continue;
        if (g->w) {
            float gx = floorf(pen + 0.5f) + g->x0, gy = floorf(y + 0.5f) + g->y0;
#if ENG_UI_NATIF   /* coin du glyphe dans l'image de 640 × 480 (band_mask_demi) ; lignes de l'image de 320 × 240 */
            gx += ui_ox; gy += ui_oy;
            op_t *o = new_op(OP_MASK, c, alpha, gy * 0.5f, (gy + g->h) * 0.5f);
#else
            op_t *o = new_op(OP_MASK, c, alpha, gy, gy + g->h);
#endif
            if (o) { o->u.mask.m = g; o->u.mask.x = (int16_t)gx; o->u.mask.y = (int16_t)gy; }
        }
        pen += g->adv16 / 16.0f;
    }
    return w;
}

void ui_text_arc(const ui_font_t *f, float cx, float cy, float r, float a, int bottom, uint16_t c, int alpha, const char *s) {
    if (!f || r < 1) return;
    cx += tx; cy += ty;
    s = filtrer(s);
    const float w = text_w(f, s), dir = bottom ? -1.0f : 1.0f;
    float pen = 0;
    while (*s) {
        const ui_mask_t *g = glyph(f, utf8_next(&s));
        if (!g) continue;
        const float adv = g->adv16 / 16.0f;
        if (g->w) {
            /* Milieu de l'avance sur le cercle, glyphe tourné selon la tangente, origine une demi-avance avant */
            const float t = a + dir * (pen + adv / 2 - w / 2) / r, th = t + dir * PI_F / 2;
            const float co = cosf(th), si = sinf(th), ox = cx + cosf(t) * r - co * adv / 2, oy = cy + sinf(t) * r - si * adv / 2;
            float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
            for (int k = 0; k < 4; k++) {
                const float u = g->x0 + (k & 1 ? g->w : 0), v = g->y0 + (k & 2 ? g->h : 0);
                const float x = ox + u * co - v * si, y = oy + u * si + v * co;
                xmin = fminf(xmin, x); xmax = fmaxf(xmax, x); ymin = fminf(ymin, y); ymax = fmaxf(ymax, y);
            }
#if ENG_UI_NATIF   /* origine et boîte dans l'image de 320 × 240 ; band_rmask_demi revient au repère du glyphe à l'échelle 2 */
            op_t *o = new_op(OP_RMASK, c, alpha, NY(ymin), NY(ymax));
            if (o) {
                o->u.rmask.m = g; o->u.rmask.ox = NX(ox); o->u.rmask.oy = NY(oy); o->u.rmask.co = co; o->u.rmask.si = si;
                o->u.rmask.x0 = (int16_t)floorf(NX(xmin)); o->u.rmask.x1 = (int16_t)ceilf(NX(xmax));
            }
#else
            op_t *o = new_op(OP_RMASK, c, alpha, ymin, ymax);
            if (o) {
                o->u.rmask.m = g; o->u.rmask.ox = ox; o->u.rmask.oy = oy; o->u.rmask.co = co; o->u.rmask.si = si;
                o->u.rmask.x0 = (int16_t)floorf(xmin); o->u.rmask.x1 = (int16_t)ceilf(xmax);
            }
#endif
        }
        pen += adv;
    }
}

void ui_icon(const char *name, float cx, float cy, int size, uint16_t c, int alpha) {
    const ui_mask_t *best = NULL; int bd = 1 << 20;
    for (int i = 0; i < nicons; i++) {
        if (strcmp(icon_names[i], name)) continue;
        int d = abs(icon_sizes[i] - size);
        if (d < bd) { bd = d; best = &icons[i]; }
    }
    if (!best && !strcmp(name, "team")) { ui_icon("stat", cx, cy, size, c, alpha); return; }   /* paquet d'avant le menu ÉQUIPE */
    if (!best || !best->w) return;
    float gx = floorf(cx + tx + 0.5f) + best->x0, gy = floorf(cy + ty + 0.5f) + best->y0;
#if ENG_UI_NATIF
    gx += ui_ox; gy += ui_oy;
    op_t *o = new_op(OP_MASK, c, alpha, gy * 0.5f, (gy + best->h) * 0.5f);
#else
    op_t *o = new_op(OP_MASK, c, alpha, gy, gy + best->h);
#endif
    if (o) { o->u.mask.m = best; o->u.mask.x = (int16_t)gx; o->u.mask.y = (int16_t)gy; }
}

/* Carte d'opacité quelconque (m doit rester lisible jusqu'au dessin de l'image suivante), coin à (x0, y0) du centre */
void ui_masque(const ui_mask_t *m, float cx, float cy, uint16_t c, int alpha) {
    if (!m || !m->w) return;
#if ENG_UI_NATIF
    const float gx = floorf(cx + tx + 0.5f) + m->x0 + ui_ox, gy = floorf(cy + ty + 0.5f) + m->y0 + ui_oy;
    op_t *o = new_op(OP_MASK, c, alpha, gy * 0.5f, (gy + m->h) * 0.5f);
#else
    const float gx = floorf(cx + tx + 0.5f) + m->x0, gy = floorf(cy + ty + 0.5f) + m->y0;
    op_t *o = new_op(OP_MASK, c, alpha, gy, gy + m->h);
#endif
    if (o) { o->u.mask.m = m; o->u.mask.x = (int16_t)gx; o->u.mask.y = (int16_t)gy; }
}

static void image_op(const uint16_t *px, const uint8_t *mask, const uint8_t *rgb, int w, int h, float x, float y, float s, float ccx, float ccy, float cr) {
    x += tx; y += ty; ccx += tx; ccy += ty;
#if ENG_UI_NATIF
    x = NX(x); y = NY(y); ccx = NX(ccx); ccy = NY(ccy); s *= 0.5f; cr *= 0.5f;
#endif
    op_t *o = new_op(OP_IMAGE, 0, 255, fmaxf(y, ccy - cr), fminf(y + h * s, ccy + cr));
    img_der = o ? UB->nops - 1 : -1;
    if (!o) return;
    o->u.img.px = px; o->u.img.msk = mask; o->u.img.rgb = rgb; o->u.img.w = (uint16_t)w; o->u.img.h = (uint16_t)h;
    o->u.img.x = x; o->u.img.y = y; o->u.img.s = s; o->u.img.ccx = ccx; o->u.img.ccy = ccy; o->u.img.cr = cr;
}
void ui_image(const uint16_t *px, const uint8_t *mask, int w, int h, float x, float y, float s, float ccx, float ccy, float cr) {
    image_op(px, mask, NULL, w, h, x, y, s, ccx, ccy, cr);
}
/* Image opaque en RGB (3 octets par pixel), lue telle quelle (dans le paquet : pas de copie en mémoire) */
void ui_image_rgb(const uint8_t *rgb, int w, int h, float x, float y, float s, float ccx, float ccy, float cr) {
    image_op(NULL, NULL, rgb, w, h, x, y, s, ccx, ccy, cr);
}
/* Image indexée : les index dans rgb, la palette dans px (les deux à la fois : ce mode-là) */
void ui_image_pal(const uint8_t *ix, const uint16_t *pal, int w, int h, float x, float y, float s, float ccx, float ccy, float cr) {
    image_op(pal, NULL, ix, w, h, x, y, s, ccx, ccy, cr);
}

void ui_sprite(const uint8_t *ix, int stride, const uint16_t *pal, int w, int h, float x, float y, float s, int a, float ccx, float ccy, float cr) {
    x += tx; y += ty; ccx += tx; ccy += ty;
#if ENG_UI_NATIF
    x = NX(x); y = NY(y); ccx = NX(ccx); ccy = NY(ccy); s *= 0.5f; cr *= 0.5f;
#endif
    op_t *o = new_op(OP_SPRITE, (uint16_t)stride, a, fmaxf(y, ccy - cr), fminf(y + h * s, ccy + cr));
    img_der = o ? UB->nops - 1 : -1;
    if (!o) return;
    o->u.img.px = pal; o->u.img.msk = NULL; o->u.img.rgb = ix; o->u.img.w = (uint16_t)w; o->u.img.h = (uint16_t)h;
    o->u.img.x = x; o->u.img.y = y; o->u.img.s = s; o->u.img.ccx = ccx; o->u.img.ccy = ccy; o->u.img.cr = cr;
}
/* L'image que l'appel précédent vient d'enregistrer (ui_sprite, ui_image…) se dessine retournée gauche-droite, dans le même
   rectangle : la carte du menu et les plans des pistes (03/11/2026 : la vue 3D montre le plan de l'Atelier en miroir). Le
   signe de l'échelle le note (band_image), sans place de plus dans l'opération ; rien si cet appel n'a rien enregistré */
void ui_miroir(void) {
    if (img_der >= 0 && img_der == UB->nops - 1) UB->ops[img_der].u.img.s = -fabsf(UB->ops[img_der].u.img.s);
    img_der = -1;
}

/* Anneau dégradé de r0 à r1 autour de (cx, cy) : opacité nulle en r0, alpha en r1, en proportion du carré de la
   distance (g2 : encore au carré, plus vite près du bord). Les halos du bord de l'écran (boost des courses, alarme)
   étaient des piles de 6 à 15 anneaux, polygones de plus de cent côtés repris ligne à ligne dans chaque bande : la
   moitié des images en moins. Ici, une passe : par ligne, les deux bouts de l'anneau, chaque pixel mélangé une fois. */
void ui_halo(float cx, float cy, float r0, float r1, uint16_t c, int alpha, int g2) {
#if ENG_UI_NATIF
    cx = NX(cx); cy = NY(cy); r0 *= 0.5f; r1 *= 0.5f;
#endif
    op_t *o = new_op(OP_HALO, c, alpha, cy - r1, cy + r1);
    if (!o) return;
    o->u.halo.cx = cx; o->u.halo.cy = cy; o->u.halo.r0 = r0; o->u.halo.r1 = r1; o->u.halo.g2 = (uint8_t)!!g2;
}
#if ENG_DYN
/* Halo des bords de toute l'image (plein écran, 02/11/2026 : accélération en course ; le cercle de la montre n'allait
   pas au téléphone) : opacité alpha au bord, nulle à e pixels dedans, en carré de la distance (comme ui_halo) ; marges
   de la barre d'état et de la barre de navigation comprises, sans le décalage d'ui_translate (comme ui_voile_image).
   Une passe : chaque pixel de la bande des bords mélangé une fois, le milieu de l'image n'est pas parcouru */
void ui_halo_bord(float e, uint16_t c, int alpha) {
    if (UB->nops >= MAX_OPS || alpha <= 0 || e < 1) return;
    op_t *o = &UB->ops[UB->nops++];
    o->kind = OP_BORD; o->c = c; o->a = (uint8_t)(alpha > 255 ? 255 : alpha);
    o->u.bord.x0 = (float)-ui_ox; o->u.bord.x1 = (float)(ui_img_w - ui_ox); o->u.bord.y0 = (float)-ui_oy; o->u.bord.y1 = (float)(ui_img_h - ui_oy); o->u.bord.e = e;
    o->ymin = (int16_t)o->u.bord.y0; o->ymax = (int16_t)o->u.bord.y1;
}
#endif

/* ---------- Rastérisation dans une bande (en IRAM sur l'ESP32 : ENG_HOT) ---------- */
ENG_HOT static void band_poly(const op_t *o, const band_t *B) {
    const float *p = &UR->pts[o->u.poly.p0];
    int n = (int)o->u.poly.np;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    if (ys >= ye) return;
    float xs[MAX_X];
    /* Arêtes qui croisent une ligne de la bande, relevées une fois par bande (08/11/2026 : un arc de jauge a une trentaine
       d'arêtes, toutes testées à chacune de ses 150 lignes ; mêmes croisements, dans le même ordre) ; au-delà de 128
       arêtes, toutes */
    uint8_t act[128];
    int na = 0;
    if (n <= 128) {
        const float lo = ys + 0.5f, hi = ye - 0.5f;
        for (int i = 0; i < n; i++) {
            const float ya = p[i * 3 + 1], yb = p[(i + 1 == n ? 0 : i + 1) * 3 + 1];
            if ((ya < yb ? ya : yb) <= hi && (ya > yb ? ya : yb) > lo) act[na++] = (uint8_t)i;
        }
    }
    for (int y = ys; y < ye; y++) {
        float py = y + 0.5f;
        int nx = 0;
        if (n <= 128) for (int a = 0; a < na && nx < MAX_X; a++) {
            const int i = act[a];
            float ya = p[i * 3 + 1], yb = p[(i + 1 == n ? 0 : i + 1) * 3 + 1];
            if ((ya <= py) != (yb <= py)) xs[nx++] = p[i * 3] + (py - ya) * p[i * 3 + 2];
        }
        else for (int i = 0; i < n && nx < MAX_X; i++) {
            float ya = p[i * 3 + 1], yb = p[(i + 1 == n ? 0 : i + 1) * 3 + 1];
            if ((ya <= py) != (yb <= py)) xs[nx++] = p[i * 3] + (py - ya) * p[i * 3 + 2];
        }
        for (int i = 1; i < nx; i++) { float v = xs[i]; int k = i - 1; while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; } xs[k + 1] = v; }
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int k = 0; k + 1 < nx; k += 2) {
            int xl = ceil_half(xs[k]), xr = ceil_half(xs[k + 1]) - 1;
            if (xl < B->x0) xl = B->x0;
            if (xr > B->x1 - 1) xr = B->x1 - 1;
#if ENG_ROUND
            if (xl < circ_l[y]) xl = circ_l[y];   /* rien hors du disque de l'écran */
            if (xr > circ_r[y]) xr = circ_r[y];
#endif
            if (o->a >= 255) { uint16_t v = swap ? sw16(o->c) : o->c; for (int x = xl; x <= xr; x++) row[x] = v; }
            else for (int x = xl; x <= xr; x++) blend(&row[x], o->c, o->a);
        }
    }
}

ENG_HOT static void band_halo(const op_t *o, const band_t *B) {
    const float cx = o->u.halo.cx, cy = o->u.halo.cy, q0 = o->u.halo.r0 * o->u.halo.r0, q1 = o->u.halo.r1 * o->u.halo.r1;
    const float k = o->a / (q1 - q0);
    const int g2 = o->u.halo.g2;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    for (int y = ys; y < ye; y++) {
        const float dy = y + 0.5f - cy, dy2 = dy * dy;
        if (dy2 >= q1) continue;
        const float wo = sqrtf(q1 - dy2), wi = dy2 < q0 ? sqrtf(q0 - dy2) : -1;
        int lo = B->x0, hi = B->x1 - 1;
#if ENG_ROUND
        if (lo < circ_l[y]) lo = circ_l[y];   /* rien hors du disque de l'écran */
        if (hi > circ_r[y]) hi = circ_r[y];
#endif
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int s = 0; s < (wi < 0 ? 1 : 2); s++) {   /* une travée sous et sur le disque intérieur, deux à sa hauteur */
            int xa = (int)ceilf((s ? cx + wi : cx - wo) - 0.5f), xb = (int)floorf((s || wi < 0 ? cx + wo : cx - wi) - 0.5f);
            if (xa < lo) xa = lo;
            if (xb > hi) xb = hi;
            float dx = xa + 0.5f - cx;
            for (int x = xa; x <= xb; x++, dx += 1) {
                float t = (dx * dx + dy2 - q0) * k;   /* 0..alpha */
                if (g2) t = t * t / o->a;
                if (t >= 8) blend(&row[x], o->c, (int)t);
            }
        }
    }
}

#if ENG_DYN
/* Halo des bords (ui_halo_bord) : d = distance au bord le plus proche ; opacité alpha × (1 - d / e)² */
ENG_HOT static void band_bord(const op_t *o, const band_t *B) {
    const float x0 = o->u.bord.x0, x1 = o->u.bord.x1, y0 = o->u.bord.y0, y1 = o->u.bord.y1, e = o->u.bord.e, ie = 1 / e;
    const int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    const int la = (int)ceilf(x0 + e), rb = (int)floorf(x1 - e);   /* colonnes des bandes de gauche [.., la) et de droite [rb, ..) */
    for (int y = ys; y < ye; y++) {
        const float py = y + 0.5f, dy = fminf(py - y0, y1 - py);
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int s = 0; s < 2; s++) {   /* près du haut ou du bas : toute la ligne, en une travée ; sinon les deux côtés */
            int xa = s ? rb : B->x0, xb = s ? B->x1 : la;
            if (dy < e) { if (s) break; xa = B->x0; xb = B->x1; }
            if (xa < B->x0) xa = B->x0;
            if (xb > B->x1) xb = B->x1;
            for (int x = xa; x < xb; x++) {
                const float px = x + 0.5f, d = fminf(dy, fminf(px - x0, x1 - px));
                if (d >= e) continue;
                const float f = 1 - d * ie;
                const int t = (int)(o->a * f * f);
                if (t >= 8) blend(&row[x], o->c, t);
            }
        }
    }
}
#endif

#if !ENG_UI_NATIF   /* (interface en 320 natif : band_mask_demi) */
ENG_HOT static void band_mask(const op_t *o, const band_t *B) {
    const ui_mask_t *m = o->u.mask.m;
    int row_bytes = (m->w + 1) >> 1;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = o->u.mask.x < B->x0 ? B->x0 - o->u.mask.x : 0, xe = o->u.mask.x + m->w > B->x1 ? B->x1 - o->u.mask.x : m->w;
    uint8_t at[16];                       /* opacité de chacun des 16 niveaux, une fois par opération */
    for (int v = 0; v < 16; v++) at[v] = (uint8_t)(v * 17 * o->a / 255);
    uint16_t solid = swap ? sw16(o->c) : o->c;
    for (int y = ys; y < ye; y++) {
        const uint8_t *src = m->data + (y - o->u.mask.y) * row_bytes;
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0 + o->u.mask.x;
        for (int i = xs; i < xe; i++) {
            int v = (i & 1) ? src[i >> 1] & 15 : src[i >> 1] >> 4;
            if (!v) continue;
            if (at[v] >= 255) row[i] = solid; else blend(&row[i], o->c, at[v]);
        }
    }
}
#endif

/* Glyphe tourné : chaque pixel de la boîte englobante revient dans le repère du glyphe (point le plus proche). Le
   long d'une ligne, (u, v) avance de (co, -si), en virgule fixe 16.16 : un seul test non signé par coordonnée, et la
   ligne s'arrête dès qu'elle ressort du glyphe (convexe) (08/11/2026 : les coordonnées en arc de l'écran du monde, 56
   glyphes tournés, étaient près de la moitié du dessin de l'interface ; mêmes pixels). Le plancher sans floorf (un appel
   de bibliothèque sur l'ESP32) : décalé de 2^23, tout reste positif et la troncature vaut le plancher */
#define FIXE_DECAL 8388608.0f
#if !ENG_UI_NATIF
static inline int32_t fixe16(float v) { return (int32_t)(v * 65536 + FIXE_DECAL) - (int32_t)FIXE_DECAL; }
#endif
#if !ENG_UI_NATIF   /* (interface en 320 natif : band_rmask_demi) */
ENG_HOT static void band_rmask(const op_t *o, const band_t *B) {
    const ui_mask_t *m = o->u.rmask.m;
    const int row_bytes = (m->w + 1) >> 1;
    int at[16];                           /* opacité de chacun des 16 niveaux, une fois par opération */
    for (int k = 0; k < 16; k++) at[k] = k * 17 * o->a / 255;
    const float co = o->u.rmask.co, si = o->u.rmask.si;
    const int32_t dU = fixe16(co), dV = fixe16(-si);
    const uint32_t W = (uint32_t)m->w << 16, H = (uint32_t)m->h << 16;
    const float iU = dU ? 1.0f / dU : 0, iV = dV ? 1.0f / dV : 0;   /* entrée dans le glyphe estimée par ligne, à un pixel près */
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = o->u.rmask.x0 > B->x0 ? o->u.rmask.x0 : B->x0, xe = o->u.rmask.x1 < B->x1 ? o->u.rmask.x1 : B->x1;
    const uint16_t c = o->c;
    for (int y = ys; y < ye; y++) {
        const float dy = y + 0.5f - o->u.rmask.oy, dx = xs + 0.5f - o->u.rmask.ox;
        int32_t U = fixe16(dx * co + dy * si - m->x0), V = fixe16(dy * co - dx * si - m->y0);
        const float eu = dU > 0 ? -U * iU : dU < 0 ? ((float)W - U) * iU : 0, ev = dV > 0 ? -V * iV : dV < 0 ? ((float)H - V) * iV : 0;
        const float e = (eu > ev ? eu : ev) - 1;
        const int k0 = e > 0 ? (e < (float)(xe - xs) ? (int)e : xe - xs) : 0;
        U += k0 * dU; V += k0 * dV;
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        int dedans = 0;
        for (int x = xs + k0; x < xe; x++, U += dU, V += dV) {
            if ((uint32_t)U >= W || (uint32_t)V >= H) { if (dedans) break; continue; }
            dedans = 1;
            const int i = U >> 16;
            const uint8_t b = m->data[(V >> 16) * row_bytes + (i >> 1)];
            const int nv = (i & 1) ? b & 15 : b >> 4;
            if (nv) blend(&row[x], c, at[nv]);
        }
    }
}
#endif

ENG_HOT static void band_image(const op_t *o, const band_t *B) {
    float s = o->u.img.s, r2 = o->u.img.cr * o->u.img.cr;
    const int mir = s < 0;   /* retournée gauche-droite (ui_miroir) */
    if (mir) s = -s;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = (int)floorf(o->u.img.x), xe = (int)ceilf(o->u.img.x + o->u.img.w * s);
    if (xs < B->x0) xs = B->x0;
    if (xe > B->x1) xe = B->x1;
    /* Colonne de l'image sous chaque colonne de la bande (-1 : hors de l'image) : la même division, une fois par colonne
       au lieu d'une par pixel (division en logiciel sur l'ESP32) */
    int16_t colx[ENG_W > ENG_IMG_W ? ENG_W : ENG_IMG_W];
    if (ys < ye) for (int x = xs; x < xe; x++) { const int sx = (int)((x + 0.5f - o->u.img.x) / s); colx[x - xs] = (int16_t)(sx < 0 || sx >= o->u.img.w ? -1 : mir ? o->u.img.w - 1 - sx : sx); }
    for (int y = ys; y < ye; y++) {
        int sy = (int)((y + 0.5f - o->u.img.y) / s);
        if (sy < 0 || sy >= o->u.img.h) continue;
        /* Colonnes de la ligne dans le disque de découpe : une racine par ligne au lieu d'un test par pixel (01/11/2026 :
           la carte du menu couvre presque tout l'écran de tuiles) */
        const float dy = y + 0.5f - o->u.img.ccy, q = r2 - dy * dy;
        if (q < 0) continue;
        const float hc = sqrtf(q);
        int xa = (int)ceilf(o->u.img.ccx - hc - 0.5f), xb = (int)floorf(o->u.img.ccx + hc - 0.5f) + 1;
        if (xa < xs) xa = xs;
        if (xb > xe) xb = xe;
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        if (o->kind == OP_SPRITE) {   /* index (0 transparent), lignes de o->c octets */
            const uint8_t *src = o->u.img.rgb + sy * o->c;
            const uint16_t *pl = o->u.img.px;
            if (o->a >= 255) {
                for (int x = xa; x < xb; x++) { const int sx = colx[x - xs]; if (sx < 0) continue; const uint8_t k = src[sx]; if (k) row[x] = swap ? sw16(pl[k]) : pl[k]; }
            } else for (int x = xa; x < xb; x++) { const int sx = colx[x - xs]; if (sx < 0) continue; const uint8_t k = src[sx]; if (k) blend(&row[x], pl[k], o->a); }
            continue;
        }
        for (int x = xa; x < xb; x++) {
            const int sx = colx[x - xs];
            if (sx < 0) continue;
            int i = sy * o->u.img.w + sx;
            if (o->u.img.rgb && o->u.img.px) blend(&row[x], o->u.img.px[o->u.img.rgb[i]], 255);   /* indexée */
            else if (o->u.img.rgb) { const uint8_t *p = o->u.img.rgb + i * 3; blend(&row[x], ui_rgb(((uint32_t)p[0] << 16) | (p[1] << 8) | p[2]), 255); }
            else if (!o->u.img.msk || o->u.img.msk[i]) blend(&row[x], o->u.img.px[i], 255);
        }
    }
}

/* Colonnes de l'image où l'interface dessine sur les lignes [y0, y0 + h) (écran rectangulaire : eng_render_band_rect de
   render.c ne double et ne réduit que celles-là ; 11/11/2026, sur la LDK, doublement et réduction de bandes entières :
   6 ms par image pour une barre en haut et un cadre en bas) : au plus max intervalles [xs[2k], xs[2k + 1]) disjoints,
   rangés, en colonnes de l'image (ui_ox compris), rapprochés quand moins de 32 colonnes les séparent ; renvoie leur nombre
   (0 : rien que la 3D). Étendue de chaque opération d'après sa géométrie, large (cadre englobant) */
/* Plage d'opérations [plage0, plage1) que dessinent ui_render_band et ui_band_spans (écran rectangulaire : entre deux voiles ;
   plage1 < 0 : jusqu'à la dernière) */
static int plage0, plage1 = -1;
void ui_plage_ops(int i0, int i1) { plage0 = i0; plage1 = i1; }
int ui_nops(void) { return UR->nops; }
/* Premier voile de toute l'image (ui_voile_image) à partir de l'opération i ; ui_nops() : aucun */
int ui_voile_suivant(int i) {
    for (; i < UR->nops; i++) if (UR->ops[i].kind == OP_VOILE) return i;
    return UR->nops;
}
/* Le voile i sur n pixels (une ligne de l'image réduite) : le même mélange que band_poly, une fois par pixel au lieu de
   quatre (les quatre pixels doublés sous lui sont identiques) */
void ui_voile_bande(int i, uint16_t *dst, int n) {
    const op_t *o = &UR->ops[i];
    for (int x = 0; x < n; x++) blend(&dst[x], o->c, o->a);
}

int ui_band_spans(int y0, int h, int *xs, int max) {
    y0 -= ui_oy;
    int n = 0;
    for (int i = plage0; i < (plage1 < 0 ? UR->nops : plage1); i++) {
        const op_t *o = &UR->ops[i];
        if (o->ymax <= y0 || o->ymin >= y0 + h) continue;
        float a, b;
        switch (o->kind) {
        case OP_POLY: {
            const float *q = &UR->pts[o->u.poly.p0];
            a = b = q[0];
            for (uint32_t k = 1; k < o->u.poly.np; k++) { a = fminf(a, q[k * 3]); b = fmaxf(b, q[k * 3]); }
            break;
        }
        case OP_MASK: a = o->u.mask.x; b = (float)o->u.mask.x + o->u.mask.m->w; break;
        case OP_RMASK: a = o->u.rmask.x0; b = o->u.rmask.x1; break;
        case OP_IMAGE: case OP_SPRITE: { const float s = fabsf(o->u.img.s); a = o->u.img.x; b = o->u.img.x + o->u.img.w * s; break; }
        case OP_HALO: a = o->u.halo.cx - o->u.halo.r1; b = o->u.halo.cx + o->u.halo.r1; break;
        default: a = -1e9f; b = 1e9f; break;   /* bords de l'image : toute la largeur */
        }
        int x0 = (int)floorf(a) - 1 + ui_ox, x1 = (int)ceilf(b) + 2 + ui_ox;
        if (x0 < 0) x0 = 0;
        if (x1 > ui_img_w) x1 = ui_img_w;
        if (x0 >= x1) continue;
        /* ajout dans la liste rangée, fusion des intervalles qui se touchent à 32 colonnes près */
        int k = 0;
        while (k < n && xs[2 * k + 1] + 32 < x0) k++;
        if (k < n && xs[2 * k] <= x1 + 32) {
            if (x0 < xs[2 * k]) xs[2 * k] = x0;
            if (x1 > xs[2 * k + 1]) xs[2 * k + 1] = x1;
            while (k + 1 < n && xs[2 * (k + 1)] <= xs[2 * k + 1] + 32) {   /* rejoint les suivants */
                if (xs[2 * (k + 1) + 1] > xs[2 * k + 1]) xs[2 * k + 1] = xs[2 * (k + 1) + 1];
                for (int j = k + 1; j + 1 < n; j++) { xs[2 * j] = xs[2 * (j + 1)]; xs[2 * j + 1] = xs[2 * (j + 1) + 1]; }
                n--;
            }
        } else if (n < max) {
            for (int j = n; j > k; j--) { xs[2 * j] = xs[2 * (j - 1)]; xs[2 * j + 1] = xs[2 * (j - 1) + 1]; }
            xs[2 * k] = x0; xs[2 * k + 1] = x1; n++;
        } else {   /* plus de place : un seul intervalle qui couvre tout (jamais deux fois les mêmes colonnes) */
            xs[0] = x0 < xs[0] ? x0 : xs[0];
            xs[1] = x1 > xs[2 * n - 1] ? x1 : xs[2 * n - 1];
            n = 1;
        }
    }
    return n;
}

#if ENG_UI_NATIF
/* Glyphe ou icône (coin en (x, y) de l'image de 640 × 480) dans l'image de 320 × 240 : chaque pixel prend la moyenne des
   quatre niveaux du masque qu'il recouvre (les quatre sous-pixels que la réduction moyennait) */
static inline int niveau_masque(const ui_mask_t *m, int rb, int r, int c) {
    if ((unsigned)r >= m->h || (unsigned)c >= m->w) return 0;
    const uint8_t b = m->data[r * rb + (c >> 1)];
    return (c & 1) ? b & 15 : b >> 4;
}
ENG_HOT static void band_mask_demi(const op_t *o, const band_t *B) {
    const ui_mask_t *m = o->u.mask.m;
    const int rb = (m->w + 1) >> 1, gx = o->u.mask.x, gy = o->u.mask.y;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = gx >> 1, xe = (gx + m->w + 1) >> 1;
    if (xs < B->x0) xs = B->x0;
    if (xe > B->x1) xe = B->x1;
    uint8_t at[61];                       /* opacité d'une somme de quatre niveaux (0 à 60) */
    for (int v = 0; v < 61; v++) at[v] = (uint8_t)(v * 17 * o->a / (4 * 255));
    const uint16_t solid = swap ? sw16(o->c) : o->c;
    for (int y = ys; y < ye; y++) {
        const int r = 2 * y - gy;
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int x = xs; x < xe; x++) {
            const int c = 2 * x - gx;
            const int v = niveau_masque(m, rb, r, c) + niveau_masque(m, rb, r, c + 1) + niveau_masque(m, rb, r + 1, c) + niveau_masque(m, rb, r + 1, c + 1);
            if (!v) continue;
            if (at[v] >= 255) row[x] = solid; else blend(&row[x], o->c, at[v]);
        }
    }
}
/* Glyphe tourné (texte en arc) : origine en coordonnées de l'image de 320 × 240, glyphe à l'échelle de 640 × 480 ; quatre
   échantillons par pixel (aux centres des quatre sous-pixels), moyennés */
ENG_HOT static void band_rmask_demi(const op_t *o, const band_t *B) {
    const ui_mask_t *m = o->u.rmask.m;
    const int rb = (m->w + 1) >> 1;
    const float co = o->u.rmask.co, si = o->u.rmask.si;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = o->u.rmask.x0 > B->x0 ? o->u.rmask.x0 : B->x0, xe = o->u.rmask.x1 < B->x1 ? o->u.rmask.x1 : B->x1;
    for (int y = ys; y < ye; y++) {
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int x = xs; x < xe; x++) {
            int v = 0;
            for (int k = 0; k < 4; k++) {   /* sous-pixels : (x + 0,25 ou 0,75, y + 0,25 ou 0,75), à l'échelle 2 */
                const float dx = (x + 0.25f + 0.5f * (k & 1) - o->u.rmask.ox) * 2, dy = (y + 0.25f + 0.5f * (k >> 1) - o->u.rmask.oy) * 2;
                const float u = dx * co + dy * si - m->x0, w = dy * co - dx * si - m->y0;
                if (u >= 0 && w >= 0) v += niveau_masque(m, rb, (int)w, (int)u);
            }
            if (v) blend(&row[x], o->c, v * 17 * o->a / (4 * 255));
        }
    }
}
#endif

ENG_HOT void ui_render_band(int y0, int h, uint16_t *dst, int x0, int x1) {
#if ENG_UI_NATIF   /* lignes et colonnes de l'image de 320 × 240, dans le repère des opérations */
    band_t B = { dst, y0, y0 + h, x0, x1, x1 - x0 };
#else
    y0 -= ui_oy;   /* la bande dans le repère de l'interface (dst ne bouge pas) */
    band_t B = { dst, y0, y0 + h, x0 - ui_ox, x1 - ui_ox, x1 - x0 };
#endif
#if ENG_RECT
    for (int i = plage0; i < (plage1 < 0 ? UR->nops : plage1); i++) {
#else
    for (int i = 0; i < UR->nops; i++) {
#endif
        const op_t *o = &UR->ops[i];
        if (o->ymax <= y0 || o->ymin >= y0 + h) continue;
#ifdef ENG_COMPTE
        extern unsigned long eng_ui_us[8], eng_ui_n[8]; extern uint32_t (*eng_compte_us)(void);
        const uint32_t tq0 = eng_compte_us ? eng_compte_us() : 0;
#endif
#if ENG_RECT
        if (o->kind == OP_POLY || o->kind == OP_VOILE) band_poly(o, &B);
#else
        if (o->kind == OP_POLY) band_poly(o, &B);
#endif
        else if (o->kind == OP_HALO) band_halo(o, &B);
#if ENG_DYN
        else if (o->kind == OP_BORD) band_bord(o, &B);
#endif
#if ENG_UI_NATIF
        else if (o->kind == OP_MASK) band_mask_demi(o, &B);
        else if (o->kind == OP_RMASK) band_rmask_demi(o, &B);
#else
        else if (o->kind == OP_MASK) band_mask(o, &B);
        else if (o->kind == OP_RMASK) band_rmask(o, &B);
#endif
        else band_image(o, &B);
#ifdef ENG_COMPTE
        { const int kk = o->kind < 8 ? o->kind : 7; eng_ui_us[kk] += (eng_compte_us ? eng_compte_us() : 0) - tq0; eng_ui_n[kk]++; }
#endif
    }
}

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

enum { OP_POLY, OP_MASK, OP_IMAGE, OP_RMASK, OP_SPRITE, OP_HALO };   /* OP_SPRITE : image indexée, index 0 transparent, lignes de c octets */
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
int ui_ox, ui_oy;
/* Taille du canevas de l'interface : 466 × 466 (écran rond), ou toute l'image (plein écran : eng_set_screen) */
int ui_w = ENG_SCREEN, ui_h = ENG_SCREEN;
/* Décalage appliqué à l'enregistrement de chaque opération (ui_translate) : un écran pas encore refait pour le plein
   écran se dessine dans un cadre de 466 × 466 centré */
static float tx, ty;
void ui_translate(float x, float y) { tx = x; ty = y; }
float ui_tx(void) { return tx; }
float ui_ty(void) { return ty; }
static ui_font_t fonts[16];
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

void ui_begin(int swap565) { UB->nops = UB->npts = 0; swap = swap565; }
int ui_op_count(void) { return UB->nops; }
int ui_pts_count(void) { return UB->npts; }
void ui_present(void) { UR = UB; UB = UB == &UF[0] ? &UF[1] : &UF[0]; }

static op_t *new_op(int kind, uint16_t c, int a, float ymin, float ymax) {
    if (UB->nops >= MAX_OPS || a <= 0) return NULL;
    if (ymax < 0 || ymin >= ui_h) return NULL;
    op_t *o = &UB->ops[UB->nops++];
    o->kind = (uint8_t)kind; o->c = c; o->a = (uint8_t)(a > 255 ? 255 : a);
    o->ymin = (int16_t)floorf(ymin < 0 ? 0 : ymin); o->ymax = (int16_t)ceilf(ymax > ui_h ? ui_h : ymax);
    return o;
}

void ui_poly(const float *xy, int n, uint16_t c, int alpha) {
    if (n < 3 || UB->npts + n * 3 > MAX_PTS) return;
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) { ymin = fminf(ymin, xy[i * 2 + 1] + ty); ymax = fmaxf(ymax, xy[i * 2 + 1] + ty); }
    op_t *o = new_op(OP_POLY, c, alpha, ymin, ymax);
    if (!o) return;
    /* Sommets rangés par trois : x, y, pente dx/dy de l'arête vers le suivant (aucune division par ligne au dessin) */
    float *q = &UB->pts[UB->npts];
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        float dy = xy[j * 2 + 1] - xy[i * 2 + 1];
        q[i * 3] = xy[i * 2] + tx; q[i * 3 + 1] = xy[i * 2 + 1] + ty;
        q[i * 3 + 2] = dy != 0 ? (xy[j * 2] - xy[i * 2]) / dy : 0;
    }
    o->u.poly.p0 = (uint32_t)UB->npts; o->u.poly.np = (uint32_t)n;
    UB->npts += n * 3;
}

void ui_rect(float x, float y, float w, float h, uint16_t c, int alpha) {
    float q[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    ui_poly(q, 4, c, alpha);
}

void ui_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float nx = -dy / l * w / 2, ny = dx / l * w / 2;
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
    return f->ascii['?'];
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

float ui_text_width(const ui_font_t *f, const char *s) {
    if (!f) return 0;
    float w = 0;
    while (*s) { const ui_mask_t *g = glyph(f, utf8_next(&s)); if (g) w += g->adv16 / 16.0f; }
    return w;
}

const ui_font_t *ui_fit(int style, const char *s, float max_w, int size, int min) {
    const ui_font_t *f = ui_font(style, size);
    while (f && ui_text_width(f, s) > max_w && f->size > min) {
        const ui_font_t *g = ui_font(style, f->size - 1);
        if (!g || g == f) break;
        f = g;
    }
    return f;
}

float ui_text(const ui_font_t *f, float x, float y, int align, uint16_t c, int alpha, const char *s) {
    if (!f) return 0;
    float w = ui_text_width(f, s);
    float pen = (align == UI_CENTER ? x - w / 2 : align == UI_RIGHT ? x - w : x) + tx;
    y += ty;
    while (*s) {
        const ui_mask_t *g = glyph(f, utf8_next(&s));
        if (!g) continue;
        if (g->w) {
            float gx = floorf(pen + 0.5f) + g->x0, gy = floorf(y + 0.5f) + g->y0;
            op_t *o = new_op(OP_MASK, c, alpha, gy, gy + g->h);
            if (o) { o->u.mask.m = g; o->u.mask.x = (int16_t)gx; o->u.mask.y = (int16_t)gy; }
        }
        pen += g->adv16 / 16.0f;
    }
    return w;
}

void ui_text_arc(const ui_font_t *f, float cx, float cy, float r, float a, int bottom, uint16_t c, int alpha, const char *s) {
    if (!f || r < 1) return;
    cx += tx; cy += ty;
    const float w = ui_text_width(f, s), dir = bottom ? -1.0f : 1.0f;
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
            op_t *o = new_op(OP_RMASK, c, alpha, ymin, ymax);
            if (o) {
                o->u.rmask.m = g; o->u.rmask.ox = ox; o->u.rmask.oy = oy; o->u.rmask.co = co; o->u.rmask.si = si;
                o->u.rmask.x0 = (int16_t)floorf(xmin); o->u.rmask.x1 = (int16_t)ceilf(xmax);
            }
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
    if (!best || !best->w) return;
    float gx = floorf(cx + tx + 0.5f) + best->x0, gy = floorf(cy + ty + 0.5f) + best->y0;
    op_t *o = new_op(OP_MASK, c, alpha, gy, gy + best->h);
    if (o) { o->u.mask.m = best; o->u.mask.x = (int16_t)gx; o->u.mask.y = (int16_t)gy; }
}

static void image_op(const uint16_t *px, const uint8_t *mask, const uint8_t *rgb, int w, int h, float x, float y, float s, float ccx, float ccy, float cr) {
    x += tx; y += ty; ccx += tx; ccy += ty;
    op_t *o = new_op(OP_IMAGE, 0, 255, fmaxf(y, ccy - cr), fminf(y + h * s, ccy + cr));
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
    op_t *o = new_op(OP_SPRITE, (uint16_t)stride, a, fmaxf(y, ccy - cr), fminf(y + h * s, ccy + cr));
    if (!o) return;
    o->u.img.px = pal; o->u.img.msk = NULL; o->u.img.rgb = ix; o->u.img.w = (uint16_t)w; o->u.img.h = (uint16_t)h;
    o->u.img.x = x; o->u.img.y = y; o->u.img.s = s; o->u.img.ccx = ccx; o->u.img.ccy = ccy; o->u.img.cr = cr;
}

/* Anneau dégradé de r0 à r1 autour de (cx, cy) : opacité nulle en r0, alpha en r1, en proportion du carré de la
   distance (g2 : encore au carré, plus vite près du bord). Les halos du bord de l'écran (boost des courses, alarme)
   étaient des piles de 6 à 15 anneaux, polygones de plus de cent côtés repris ligne à ligne dans chaque bande : la
   moitié des images en moins. Ici, une passe : par ligne, les deux bouts de l'anneau, chaque pixel mélangé une fois. */
void ui_halo(float cx, float cy, float r0, float r1, uint16_t c, int alpha, int g2) {
    op_t *o = new_op(OP_HALO, c, alpha, cy - r1, cy + r1);
    if (!o) return;
    o->u.halo.cx = cx; o->u.halo.cy = cy; o->u.halo.r0 = r0; o->u.halo.r1 = r1; o->u.halo.g2 = (uint8_t)!!g2;
}

/* ---------- Rastérisation dans une bande (en IRAM sur l'ESP32 : ENG_HOT) ---------- */
ENG_HOT static void band_poly(const op_t *o, const band_t *B) {
    const float *p = &UR->pts[o->u.poly.p0];
    int n = (int)o->u.poly.np;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    float xs[MAX_X];
    for (int y = ys; y < ye; y++) {
        float py = y + 0.5f;
        int nx = 0;
        for (int i = 0; i < n && nx < MAX_X; i++) {
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

/* Glyphe tourné : chaque pixel de la boîte englobante revient dans le repère du glyphe (point le plus proche). Le
   long d'une ligne, (u, v) avance de (co, -si) ; décalés de 16, ils restent positifs dans la boîte et la troncature
   vaut floorf (un appel de bibliothèque sur l'ESP32) */
ENG_HOT static void band_rmask(const op_t *o, const band_t *B) {
    const ui_mask_t *m = o->u.rmask.m;
    const int row_bytes = (m->w + 1) >> 1;
    int at[16];                           /* opacité de chacun des 16 niveaux, une fois par opération */
    for (int k = 0; k < 16; k++) at[k] = k * 17 * o->a / 255;
    const float co = o->u.rmask.co, si = o->u.rmask.si;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = o->u.rmask.x0 > B->x0 ? o->u.rmask.x0 : B->x0, xe = o->u.rmask.x1 < B->x1 ? o->u.rmask.x1 : B->x1;
    for (int y = ys; y < ye; y++) {
        const float dy = y + 0.5f - o->u.rmask.oy, dx = xs + 0.5f - o->u.rmask.ox;
        float u = dx * co + dy * si - m->x0 + 16, v = dy * co - dx * si - m->y0 + 16;
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int x = xs; x < xe; x++, u += co, v -= si) {
            const int i = (int)u - 16, j = (int)v - 16;
            if (u < 16 || v < 16 || i >= m->w || j >= m->h) continue;
            const uint8_t b = m->data[j * row_bytes + (i >> 1)];
            const int v = (i & 1) ? b & 15 : b >> 4;
            if (v) blend(&row[x], o->c, at[v]);
        }
    }
}

ENG_HOT static void band_image(const op_t *o, const band_t *B) {
    float s = o->u.img.s, r2 = o->u.img.cr * o->u.img.cr;
    int ys = o->ymin > B->by0 ? o->ymin : B->by0, ye = o->ymax < B->by1 ? o->ymax : B->by1;
    int xs = (int)floorf(o->u.img.x), xe = (int)ceilf(o->u.img.x + o->u.img.w * s);
    if (xs < B->x0) xs = B->x0;
    if (xe > B->x1) xe = B->x1;
    /* Colonne de l'image sous chaque colonne de la bande (-1 : hors de l'image) : la même division, une fois par colonne
       au lieu d'une par pixel (division en logiciel sur l'ESP32) */
    int16_t colx[ENG_W > ENG_SCREEN ? ENG_W : ENG_SCREEN];
    if (ys < ye) for (int x = xs; x < xe; x++) { const int sx = (int)((x + 0.5f - o->u.img.x) / s); colx[x - xs] = (int16_t)(sx < 0 || sx >= o->u.img.w ? -1 : sx); }
    for (int y = ys; y < ye; y++) {
        int sy = (int)((y + 0.5f - o->u.img.y) / s);
        if (sy < 0 || sy >= o->u.img.h) continue;
        float dy = y + 0.5f - o->u.img.ccy;
        uint16_t *row = B->dst + (y - B->by0) * B->w - B->x0;
        for (int x = xs; x < xe; x++) {
            float dx = x + 0.5f - o->u.img.ccx;
            if (dx * dx + dy * dy > r2) continue;
            const int sx = colx[x - xs];
            if (sx < 0) continue;
            if (o->kind == OP_SPRITE) { const uint8_t k = o->u.img.rgb[sy * o->c + sx]; if (k) blend(&row[x], o->u.img.px[k], o->a); continue; }
            int i = sy * o->u.img.w + sx;
            if (o->u.img.rgb && o->u.img.px) blend(&row[x], o->u.img.px[o->u.img.rgb[i]], 255);   /* indexée */
            else if (o->u.img.rgb) { const uint8_t *p = o->u.img.rgb + i * 3; blend(&row[x], ui_rgb(((uint32_t)p[0] << 16) | (p[1] << 8) | p[2]), 255); }
            else if (!o->u.img.msk || o->u.img.msk[i]) blend(&row[x], o->u.img.px[i], 255);
        }
    }
}

ENG_HOT void ui_render_band(int y0, int h, uint16_t *dst, int x0, int x1) {
    y0 -= ui_oy;   /* la bande dans le repère de l'interface (dst ne bouge pas) */
    band_t B = { dst, y0, y0 + h, x0 - ui_ox, x1 - ui_ox, x1 - x0 };
    for (int i = 0; i < UR->nops; i++) {
        const op_t *o = &UR->ops[i];
        if (o->ymax <= y0 || o->ymin >= y0 + h) continue;
        if (o->kind == OP_POLY) band_poly(o, &B);
        else if (o->kind == OP_HALO) band_halo(o, &B);
        else if (o->kind == OP_MASK) band_mask(o, &B);
        else if (o->kind == OP_RMASK) band_rmask(o, &B);
        else band_image(o, &B);
    }
}

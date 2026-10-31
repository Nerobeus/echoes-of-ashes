/*
 * Dessin de l'interface de la montre (commun/ui_montre : app montre, menu du PC DOS). L'image (466 × 466 en RGB565,
 * octets dans l'ordre de l'écran) vit en PSRAM ; chaque primitive note les lignes touchées ; d_envoyer les recopie par
 * bandes dans deux tampons DMA internes et les envoie (lcd_band_send, comme Echoes). Rien n'est envoyé si rien n'a
 * changé : l'écran garde son image.
 */
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "board.h"
#include "lcd_co5300.h"
#include "dessin.h"

#define ECR(c) ((uint16_t)((c) >> 8 | (c) << 8))

static uint16_t *fb, *tampon[2];
static int bande = 32;                  /* lignes par envoi (taille des tampons DMA) */
static int sale0 = LCD_H, sale1 = -1;   /* lignes changées [sale0, sale1] */
static int ox;                          /* décalage horizontal (page glissée) */
void d_decalage(int dx) { ox = dx; }

int d_init(void) {
    fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    for (int k = 0; k < 2; k++) tampon[k] = heap_caps_malloc(LCD_W * bande * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!fb || !tampon[0] || !tampon[1]) return 0;
    memset(fb, 0, LCD_W * LCD_H * 2);
    return 1;
}

int d_init_ext(uint16_t *img, uint16_t *t0, uint16_t *t1, int lignes) {
    if (!img || !t0 || !t1 || lignes < 2) return 0;
    fb = img; tampon[0] = t0; tampon[1] = t1; bande = lignes & ~1;
    sale0 = LCD_H; sale1 = -1; ox = 0;
    return 1;
}

static void sale(int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 >= LCD_H) y1 = LCD_H - 1;
    if (y0 < sale0) sale0 = y0;
    if (y1 > sale1) sale1 = y1;
}
void d_tout(void) { sale(0, LCD_H - 1); }

uint16_t d_mele(uint16_t a, uint16_t b, int t) {
    const int ra = a >> 11, ga = a >> 5 & 63, ba = a & 31, rb = b >> 11, gb = b >> 5 & 63, bb = b & 31;
    return (uint16_t)((ra + (rb - ra) * t / 16) << 11 | (ga + (gb - ga) * t / 16) << 5 | (ba + (bb - ba) * t / 16));
}

void d_effacer(uint16_t c) {
    const uint16_t e = ECR(c);
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = e;
    d_tout();
}

void d_rect(int x, int y, int w, int h, uint16_t c) {
    x += ox;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > LCD_W) x1 = LCD_W;
    if (y1 > LCD_H) y1 = LCD_H;
    if (x >= x1 || y >= y1) return;
    const uint16_t e = ECR(c);
    for (int j = y; j < y1; j++) for (int i = x; i < x1; i++) fb[j * LCD_W + i] = e;
    sale(y, y1 - 1);
}

void d_rect_arrondi(int x, int y, int w, int h, int r, uint16_t c) {
    x += ox;
    const uint16_t e = ECR(c);
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if (yy < 0 || yy >= LCD_H) continue;
        int retrait = 0;
        const int dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
        if (dy) retrait = r - (int)sqrtf((float)(r * r - dy * dy));
        for (int i = x + retrait; i < x + w - retrait; i++) if (i >= 0 && i < LCD_W) fb[yy * LCD_W + i] = e;
    }
    sale(y, y + h - 1);
}

/* Portions de la ligne y dans l'anneau r0..r1 de centre (cx, cy), bornées à l'écran : s = { x0, x1, x0, x1 } ; nombre */
static int portions(int cx, int cy, int r0, int r1, int y, int s[4]) {
    const int dy = y - cy;
    if (dy * dy > r1 * r1) return 0;
    const int xe = (int)sqrtf((float)(r1 * r1 - dy * dy));
    const int xi = dy * dy < r0 * r0 ? (int)sqrtf((float)(r0 * r0 - dy * dy)) : -1;
    int n = 0, p[4];
    if (xi < 0) { p[0] = cx - xe; p[1] = cx + xe; n = 1; }
    else { p[0] = cx - xe; p[1] = cx - xi; p[2] = cx + xi; p[3] = cx + xe; n = 2; }
    int m = 0;
    for (int k = 0; k < n; k++) {
        const int a = p[2 * k] < 0 ? 0 : p[2 * k], b = p[2 * k + 1] >= LCD_W ? LCD_W - 1 : p[2 * k + 1];
        if (a <= b) { s[2 * m] = a; s[2 * m + 1] = b; m++; }
    }
    return m;
}

void d_anneau(int cx, int cy, int r0, int r1, uint16_t c) {
    cx += ox;
    const uint16_t e = ECR(c);
    for (int y = cy - r1; y <= cy + r1; y++) {
        if (y < 0 || y >= LCD_H) continue;
        int sp[4];
        const int n = portions(cx, cy, r0, r1, y, sp);
        for (int k = 0; k < n; k++) for (int x = sp[2 * k]; x <= sp[2 * k + 1]; x++) fb[y * LCD_W + x] = e;
    }
    sale(cy - r1, cy + r1);
}

uint16_t *d_tampon(void) { return fb; }
uint16_t *d_cible(uint16_t *img) { uint16_t *a = fb; fb = img; return a; }
int d_decalage_courant(void) { return ox; }

void d_image(const uint16_t *img) {
    if (!img) return;
    const int x0 = ox > 0 ? ox : 0, x1 = ox < 0 ? LCD_W + ox : LCD_W;
    if (x1 <= x0) return;
    for (int y = 0; y < LCD_H; y++) memcpy(fb + y * LCD_W + x0, img + y * LCD_W + x0 - ox, (size_t)(x1 - x0) * 2);
    d_tout();
}

/* pixel mélangé (t16 sur 16) ; x déjà décalé */
static inline void mele(int x, int y, uint16_t c, int t) {
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H || t <= 0) return;
    uint16_t *d = &fb[y * LCD_W + x];
    *d = t >= 16 ? ECR(c) : ECR(d_mele(ECR(*d), c, t));
}

void d_rect_mele(int x, int y, int w, int h, uint16_t c, int t) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) mele(i + ox, j, c, t);
    sale(y, y + h - 1);
}

void d_arc_mele(int cx, int cy, int r0, int r1, float a0, float a1, uint16_t c, int t) {
    if (a1 <= a0) return;
    cx += ox;
    for (int y = cy - r1; y <= cy + r1; y++) {
        if (y < 0 || y >= LCD_H) continue;
        int sp[4];
        const int n = portions(cx, cy, r0, r1, y, sp);
        for (int k = 0; k < n; k++) for (int x = sp[2 * k]; x <= sp[2 * k + 1]; x++) {
            const int dx = x - cx, dy = y - cy;
            float a = atan2f((float)dx, (float)-dy) / 6.2831853f;
            if (a < 0) a += 1;
            if (a >= a0 && a < a1) mele(x, y, c, t);
        }
    }
    sale(cy - r1, cy + r1);
}

void d_poly(const float *p, int n, uint16_t c, int t) {
    if (n < 3 || n > 16) return;
    float y0 = p[1], y1 = p[1];
    for (int k = 1; k < n; k++) { y0 = fminf(y0, p[2 * k + 1]); y1 = fmaxf(y1, p[2 * k + 1]); }
    const int ya = y0 < 0 ? 0 : (int)y0, yb = y1 >= LCD_H ? LCD_H - 1 : (int)y1;
    for (int y = ya; y <= yb; y++) {
        const float yc = y + 0.5f;
        float xs[16];
        int m = 0;
        for (int k = 0; k < n; k++) {
            const float *a = p + 2 * k, *b = p + 2 * ((k + 1) % n);
            if ((a[1] <= yc && b[1] > yc) || (b[1] <= yc && a[1] > yc)) xs[m++] = a[0] + (yc - a[1]) * (b[0] - a[0]) / (b[1] - a[1]) + ox;
        }
        for (int i = 1; i < m; i++) for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { const float v = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = v; }
        for (int i = 0; i + 1 < m; i += 2) {
            const float xl = xs[i], xr = xs[i + 1];
            for (int x = (int)floorf(xl); x <= (int)xr; x++) {
                const float cov = fminf(x + 1, xr) - fmaxf(x, xl);
                if (cov > 0) mele(x, y, c, cov >= 1 ? t : (int)(t * cov + 0.5f));
            }
        }
    }
    sale(ya, yb);
}

/* Trait (capsule d'épaisseur ep), bord anticrénelé */
void d_ligne(float x0, float y0, float x1, float y1, float ep, uint16_t c) {
    x0 += ox; x1 += ox;
    const float r = ep / 2, vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    const int bx0 = (int)floorf(fminf(x0, x1) - r - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + r + 1);
    const int by0 = (int)floorf(fminf(y0, y1) - r - 1), by1 = (int)ceilf(fmaxf(y0, y1) + r + 1);
    for (int y = by0; y <= by1; y++) {
        if (y < 0 || y >= LCD_H) continue;
        for (int x = bx0; x <= bx1; x++) {
            const float px = x - x0, py = y - y0;
            float k = l2 > 0 ? (px * vx + py * vy) / l2 : 0;
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            const float ex = px - k * vx, ey = py - k * vy, d2 = ex * ex + ey * ey;
            if (d2 >= (r + 0.5f) * (r + 0.5f)) continue;
            const float cov = r + 0.5f - sqrtf(d2);
            if (cov > 0) mele(x, y, c, cov >= 1 ? 16 : (int)(cov * 16));
        }
    }
    sale(by0, by1);
}

void d_arc(int cx, int cy, int r0, int r1, float a0, float a1, uint16_t c) { d_arc_mele(cx - ox, cy, r0, r1, a0, a1, c, 16); }

/* ---------- Texte ---------- */
static uint32_t utf8(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = (c & 31) << 6 | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}
static const glyphe_t *glyphe(const police_t *pol, uint32_t c) {
    int a = 0, b = pol->n - 1;
    while (a <= b) {
        const int m = (a + b) / 2;
        if (pol->g[m].code == c) return &pol->g[m];
        if (pol->g[m].code < c) a = m + 1; else b = m - 1;
    }
    if (c != '?') return glyphe(pol, '?');
    return NULL;
}
static int largeur_n(const police_t *p, const char *s, int n) {
    int w = 0;
    const char *f = n < 0 ? NULL : s + n;
    while (*s && (!f || s < f)) { const glyphe_t *g = glyphe(p, utf8(&s)); if (g) w += g->av; }
    return w;
}
int d_largeur(const police_t *p, const char *s) { return largeur_n(p, s, -1); }

static int texte_g(const police_t *p, int x, int y, const char *s, int n, uint16_t c, uint16_t c2, int force) {
    const char *f = n < 0 ? NULL : s + n;
    x += ox;
    const int x0 = x;
    while (*s && (!f || s < f)) {
        const glyphe_t *g = glyphe(p, utf8(&s));
        if (!g) continue;
        const uint8_t *px = p->px + g->off;
        for (int j = 0; j < g->h; j++) {
            const int yy = y + g->dy + j;
            if (yy < 0 || yy >= LCD_H) continue;
            for (int i = 0; i < g->w; i++) {
                const int k = j * g->w + i, a = k & 1 ? px[k >> 1] & 15 : px[k >> 1] >> 4;
                const int xx = x + g->dx + i;
                if (!a || xx < 0 || xx >= LCD_W) continue;
                const uint16_t cc = c2 == c ? c : d_mele(c, c2, (g->dy + j) * 16 / p->hauteur);
                mele(xx, yy, cc, force == 16 ? (a == 15 ? 16 : a + 1) : (a + 1) * force / 16);
            }
        }
        x += g->av;
    }
    sale(y, y + p->hauteur);
    return x - x0;
}
static int texte_n(const police_t *p, int x, int y, const char *s, int n, uint16_t c) { return texte_g(p, x, y, s, n, c, c, 16); }
int d_texte(const police_t *p, int x, int y, const char *s, uint16_t c, int align) {
    const int w = d_largeur(p, s);
    if (align == A_CENTRE) x -= w / 2; else if (align == A_DROITE) x -= w;
    return texte_n(p, x, y, s, -1, c);
}

int d_paragraphe(const police_t *p, int x, int y, int w, int n, const char *s, uint16_t c) {
    int lignes = 0;
    while (*s && lignes < n) {
        /* le plus long morceau qui tient, coupé à un espace si possible */
        const char *q = s, *coupe = NULL;
        while (*q && *q != '\n') {
            const char *r = q;
            utf8(&r);
            if (largeur_n(p, s, (int)(r - s)) > w) break;
            if (*q == ' ') coupe = q;
            q = r;
        }
        const char *fin = !*q || *q == '\n' ? q : coupe && coupe > s ? coupe : q;
        if (fin == s) { const char *r = s; utf8(&r); fin = r; }
        if (lignes == n - 1 && *fin && *fin != '\n') {   /* dernière ligne permise : la fin coupée par … */
            texte_n(p, x, y + lignes * p->hauteur, s, (int)(fin - s), c);
            d_texte(p, x + largeur_n(p, s, (int)(fin - s)), y + lignes * p->hauteur, "…", c, A_GAUCHE);
            return (lignes + 1) * p->hauteur;
        }
        texte_n(p, x, y + lignes * p->hauteur, s, (int)(fin - s), c);
        lignes++;
        s = fin;
        while (*s == ' ' || *s == '\n') s++;
    }
    return lignes * p->hauteur;
}

/* ---------- Envoi ---------- */
void d_envoyer(void) {
    if (sale1 < sale0) return;
    int y0 = sale0 & ~1, y1 = (sale1 + 2) & ~1;
    if (y1 > LCD_H) y1 = LCD_H;
    sale0 = LCD_H; sale1 = -1;
    int t = 0;
    for (int y = y0; y < y1; y += bande) {
        const int h = y1 - y < bande ? y1 - y : bande;
        lcd_buffer_wait(t);
        memcpy(tampon[t], fb + y * LCD_W, LCD_W * h * 2);
        lcd_band_send(0, LCD_W, y, h, tampon[t], t);
        t ^= 1;
    }
    lcd_wait_all();
}

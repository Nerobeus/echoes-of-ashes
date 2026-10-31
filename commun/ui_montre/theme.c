/*
 * Thème de l'app montre, repris des menus d'Echoes of Ashes : mêmes couleurs (hud.c : INK, DIM, teintes THEMES), mêmes
 * rangées (rangee d'accueil.c : chemin à coins coupés de ui_chamfer_path, fond mêlé à 10 % de la teinte, contour), même
 * bouton retour (bouton_retour), même cadran que la montre verrouillée (verrou_build : graduations, arc des secondes,
 * chiffres sept segments à halo, date, batterie).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "board.h"
#include "theme.h"

#define CX 233
#define CY 233
#define TOUR 6.2831853f

static const struct { uint16_t c1, c2; const char *nom; } TEINTES[] = {
    { RGB(0x22, 0xE4, 0xFF), RGB(0xFF, 0x2D, 0x9B), "NÉON" },
    { RGB(0xFF, 0xB0, 0x20), RGB(0x22, 0xE4, 0xFF), "ROUILLE" },
    { RGB(0x8D, 0xFF, 0x5A), RGB(0xFF, 0x2D, 0x9B), "TOXIQUE" },
    { RGB(0xA7, 0x7B, 0xFF), RGB(0xFF, 0xD2, 0x3F), "ULTRAVIOLET" },
    { RGB(0xC4, 0xEC, 0xFF), RGB(0xFF, 0x6A, 0x3D), "GLACE" },
};
#define NT (int)(sizeof TEINTES / sizeof TEINTES[0])
uint16_t T1 = RGB(0x22, 0xE4, 0xFF), T2 = RGB(0xFF, 0x2D, 0x9B);
int teintes_nb(void) { return NT; }
const char *teinte_nom(int k) { return k >= 0 && k < NT ? TEINTES[k].nom : ""; }
void teinte_choisir(int k) { if (k < 0 || k >= NT) k = 0; T1 = TEINTES[k].c1; T2 = TEINTES[k].c2; }
uint16_t th_nuit = RGB(13, 20, 40), th_panneau_c = RGB(6, 14, 26);
void th_couleurs(uint16_t t1, uint16_t t2, uint16_t nuit, uint16_t panneau) { T1 = t1; T2 = t2; th_nuit = nuit; th_panneau_c = panneau; }

int th_texte(const police_t *p, int x, int base, const char *s, uint16_t c, int align) {
    return d_texte(p, x, base - p->base, s, c, align);
}

void th_fond(void) {
    d_anneau(CX, CY, 0, 150, d_mele(T_INK, T_NUIT, 7));   /* disque de nuit (alpha 110) */
    d_anneau(CX, CY, 215, 216, d_mele(T_INK, T1, 3));     /* anneau de la teinte (alpha 46) */
}

void th_titre(int y, const char *titre, const char *sous) {
    th_texte(&P_GRAND, CX, y, titre, T_BLANC, A_CENTRE);
    if (sous && sous[0]) th_texte(&P_MONO, CX, y + 26, sous, T_DIM, A_CENTRE);
}

int th_demi(int y, int h) {
    const int dy = (y + h / 2 < CY ? y : y + h) - CY;   /* le bord le plus loin du centre */
    const float r2 = 233.0f * 233 - (float)dy * dy;
    return r2 > 0 ? (int)sqrtf(r2) - 40 : 0;
}

void th_panneau(int x, int y, int w, int h, uint16_t col, bool choisi) {
    const float c = 10;
    const float q[12] = { x + c, y, x + w, y, x + w, y + h - c, x + w - c, y + h, x, y + h, x, y + c };
    const uint16_t fond = d_mele(T_INK, col, choisi ? 4 : 2);
    d_poly(q, 6, fond, 16);
    const uint16_t trait = choisi ? col : d_mele(fond, col, 8);
    for (int k = 0; k < 6; k++)
        d_ligne(q[2 * k], q[2 * k + 1], q[(2 * k + 2) % 12], q[(2 * k + 3) % 12], choisi ? 2 : 1.2f, trait);
}

void th_rangee(int y, int h, const char *lab, const char *val, bool choisi, uint16_t lc) {
    const int hw = th_demi(y, h), x = CX - hw, w = 2 * hw;
    const uint16_t col = choisi ? T2 : T1;
    th_panneau(x, y, w, h, col, choisi);
    const int base = y + h / 2 + 8;
    if (val && val[0]) {
        th_texte(&P_MONO, x + w - 14, base - 1, val, col, A_DROITE);
        const int vw = d_largeur(&P_MONO, val) + 16;
        d_paragraphe(&P_RANG, x + 16, base - P_RANG.base, w - vw - 32, 1, lab, lc);
    } else if (d_largeur(&P_RANG, lab) <= w - 32) th_texte(&P_RANG, CX, base, lab, lc, A_CENTRE);
    else d_paragraphe(&P_RANG, x + 16, base - P_RANG.base, w - 32, 1, lab, lc);
}

void th_retour(bool choisi) {
    const float cx = CX, cy = 428, w = 72, h = 54;
    const float q[12] = { cx - w / 4, cy - h / 2, cx + w / 4, cy - h / 2, cx + w / 2, cy, cx + w / 4, cy + h / 2, cx - w / 4, cy + h / 2, cx - w / 2, cy };
    d_poly(q, 6, choisi ? d_mele(T_PANNEAU, T2, 4) : T_PANNEAU, 16);
    const uint16_t c = choisi ? T2 : T_DIM;
    for (int k = 0; k < 6; k++) d_ligne(q[2 * k], q[2 * k + 1], q[(2 * k + 2) % 12], q[(2 * k + 3) % 12], 2, c);
    d_ligne(cx + 4, cy - 9, cx - 6, cy, 3, c);   /* flèche de retour */
    d_ligne(cx - 6, cy, cx + 4, cy + 9, 3, c);
}

void th_points(int p, int n) {
    for (int k = 0; k < n; k++) {
        const float x = CX - (n - 1) * 10 + k * 20, y = 446, r = k == p ? 5 : 3;
        const float q[8] = { x, y - r, x + r, y, x, y + r, x - r, y };   /* losanges */
        d_poly(q, 4, k == p ? T1 : T_ETEINT, 16);
    }
}

/* ---------- Cadran (verrou_build de hud.c) ---------- */
static const uint8_t SEG7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
static void segment7(float x0, float y0, float x1, float y1, float t, uint16_t c, int a16) {
    const float h = t / 2;
    if (y0 == y1) { const float q[12] = { x0, y0, x0 + h, y0 - h, x1 - h, y0 - h, x1, y0, x1 - h, y0 + h, x0 + h, y0 + h }; d_poly(q, 6, c, a16); }
    else { const float q[12] = { x0, y0, x0 + h, y0 + h, x0 + h, y1 - h, x0, y1, x0 - h, y1 - h, x0 - h, y0 + h }; d_poly(q, 6, c, a16); }
}
/* Chiffre v (-1 : tous éteints) dans (x, y, w, h) ; éteints à peine visibles, allumés avec un halo */
static void chiffre7(float x, float y, float w, float h, float t, int v, uint16_t c) {
    const float g = 3, l = x + t / 2, r = x + w - t / 2, hh = y + t / 2, m = y + h / 2, b = y + h - t / 2;
    const float S[7][4] = { { l + g, hh, r - g, hh }, { r, hh + g, r, m - g }, { r, m + g, r, b - g }, { l + g, b, r - g, b },
                            { l, m + g, l, b - g }, { l, hh + g, l, m - g }, { l + g, m, r - g, m } };
    for (int k = 0; k < 7; k++) {
        if (v >= 0 && (SEG7[v] >> k & 1)) {
            segment7(S[k][0], S[k][1], S[k][2], S[k][3], t + 8, c, 3);
            segment7(S[k][0], S[k][1], S[k][2], S[k][3], t, c, 16);
        } else segment7(S[k][0], S[k][1], S[k][2], S[k][3], t, c, 1);
    }
}

/* Partie fixe à la minute : graduations, chiffres, date, batterie, état ; gardée composée (compo) */
static void cadran_fixe(const cadran_info_t *i) {
    for (int k = 0; k < 60; k++) {
        const float a = k * TOUR / 60, r0 = k % 5 ? 213 : 203, r1 = 220;
        d_ligne(CX + sinf(a) * r0, CY - cosf(a) * r0, CX + sinf(a) * r1, CY - cosf(a) * r1, k % 5 ? 2 : 3,
                k % 5 ? d_mele(T_INK, T1, 5) : d_mele(T_INK, T_BLANC, 13));
    }
    th_texte(&P_MONO, CX, 120, "ECHOES // SEC-06", d_mele(T_INK, T1, 11), A_CENTRE);
    /* HH:MM ; deux-points qui battent la seconde */
    const float w = 60, h = 112, t = 12, y = 158, ec = 12, dp = 34;
    float x = CX - (4 * w + 2 * ec + dp) / 2;
    const int hr = i->l.tm_hour, mn = i->l.tm_min;
    chiffre7(x, y, w, h, t, i->connue ? hr / 10 : -1, T1); x += w + ec;
    chiffre7(x, y, w, h, t, i->connue ? hr % 10 : -1, T1); x += w;
    x += dp;
    chiffre7(x, y, w, h, t, i->connue ? mn / 10 : -1, T1); x += w + ec;
    chiffre7(x, y, w, h, t, i->connue ? mn % 10 : -1, T1);
    /* date */
    static const char *JS[7] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
    static const char *MS[12] = { "JAN", "FÉV", "MARS", "AVR", "MAI", "JUIN", "JUIL", "AOÛT", "SEPT", "OCT", "NOV", "DÉC" };
    char d[48];
    if (i->connue) snprintf(d, sizeof d, "%s %d %s", JS[i->l.tm_wday % 7], i->l.tm_mday, MS[i->l.tm_mon % 12]);
    else snprintf(d, sizeof d, "HEURE À RÉGLER");
    th_texte(&P_TITRE, CX, 316, d, T_BLANC, A_CENTRE);
    /* batterie : pile et pourcentage */
    if (i->batterie >= 0) {
        const int bx = CX - 40, by = 336;
        const uint16_t cb = i->batterie <= 15 && !i->usb ? T2 : T1;
        const float q[8] = { bx, by, bx + 28, by, bx + 28, by + 14, bx, by + 14 };
        for (int k = 0; k < 4; k++) d_ligne(q[2 * k], q[2 * k + 1], q[(2 * k + 2) % 8], q[(2 * k + 3) % 8], 2, cb);
        d_rect(bx + 28, by + 4, 3, 6, cb);
        d_rect(bx + 3, by + 3, 22 * i->batterie / 100, 8, cb);
        snprintf(d, sizeof d, "%d %%%s", i->batterie, i->usb ? " +" : "");
        th_texte(&P_MONO, bx + 40, by + 13, d, cb, A_GAUCHE);
    }
    /* téléphone et notifications, dans les graduations du bas */
    if (i->nnotifs) snprintf(d, sizeof d, "%d NOTIFICATION%s", i->nnotifs, i->nnotifs > 1 ? "S" : "");
    else snprintf(d, sizeof d, "%s", !i->bt_on ? "BLUETOOTH COUPÉ" : i->bt_lien ? "TÉLÉPHONE RELIÉ" : "TÉLÉPHONE ABSENT");
    th_texte(&P_MONO, CX, 392, d, i->nnotifs ? T2 : i->bt_lien ? d_mele(T_INK, T1, 12) : T_GRIS, A_CENTRE);
}

/* Anneau des secondes : la seconde en cours dans la seconde teinte avec un halo, une traînée de la teinte qui
   s'estompe sur les secondes passées, le rail des secondes à venir à peine visible */
#define SEC_R0 223
#define SEC_R1 231
/* Segment k (sur 60) de l'anneau r0..r1, écart de gap (fraction d'un segment) : un quadrilatère */
static void segment_sec(float k, float r0, float r1, float gap, uint16_t c, int t16) {
    const float a0 = (k + gap / 2) * TOUR / 60, a1 = (k + 1 - gap / 2) * TOUR / 60;
    const float q[8] = { CX + sinf(a0) * r0, CY - cosf(a0) * r0, CX + sinf(a0) * r1, CY - cosf(a0) * r1,
                         CX + sinf(a1) * r1, CY - cosf(a1) * r1, CX + sinf(a1) * r0, CY - cosf(a1) * r0 };
    d_poly(q, 4, c, t16);
}
static void cadran_secondes(const cadran_info_t *i) {
    const int s = i->connue ? i->l.tm_sec : -1;
    for (int k = 0; k < 60; k++) {
        if (s < 0 || k > s) segment_sec(k, SEC_R0, SEC_R1, 0.3f, T1, 2);               /* rail */
        else if (k < s) { const int age = s - k; segment_sec(k, SEC_R0, SEC_R1, 0.3f, T1, age < 24 ? 15 - age * 10 / 24 : 5); }
    }
    if (s >= 0) {
        segment_sec(s - 0.15f, SEC_R0 - 4, 233, 0, T2, 5);   /* halo */
        segment_sec(s, SEC_R0, SEC_R1, 0.3f, T2, 16);
    }
    /* deux-points qui battent la seconde (entre les heures et les minutes) */
    const float w = 60, h = 112, y = 158, ec = 12, dp = 34;
    const int cx = (int)(CX - (4 * w + 2 * ec + dp) / 2 + 2 * w + ec + dp / 2);
    const bool bat = !i->connue || i->l.tm_sec % 2 == 0;
    for (int k = 0; k < 2; k++) {
        const int cy = (int)(y + h * (k ? 0.7f : 0.3f));
        if (bat) d_rect(cx - 6, cy - 6, 12, 12, T1); else d_rect_mele(cx - 6, cy - 6, 12, 12, T1, 4);
    }
}

static uint16_t *compo;
static int cle[9];
static bool compo_ok;
void th_cadran(const cadran_info_t *i) {
    const int k[9] = { i->connue, i->l.tm_min, i->l.tm_hour, i->l.tm_mday, i->batterie, i->usb, i->nnotifs, i->bt_on * 2 + i->bt_lien, T1 };
    if (!compo) compo = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if (compo) {
        if (!compo_ok || memcmp(k, cle, sizeof k)) {
            const int ox = d_decalage_courant();
            d_decalage(0);
            uint16_t *ecran = d_cible(compo);
            d_effacer(T_INK);
            cadran_fixe(i);
            d_cible(ecran);
            d_decalage(ox);
            memcpy(cle, k, sizeof k);
            compo_ok = true;
        }
        d_image(compo);
    } else cadran_fixe(i);
    cadran_secondes(i);
}

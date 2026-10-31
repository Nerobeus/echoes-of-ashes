/*
 * Cadrans de l'app montre. Chaque cadran a un fond (466 × 466, 256 couleurs) fait par outils/cadrans.py et copié sur la
 * carte SD (cadran_<id>.cad dans le dossier du jeu), et dessine ce qui bouge aux places prévues par ce fond : heure,
 * date, aiguilles, balayage du radar, jauges de la batterie et de la température de la puce. Sans son fichier, un
 * cadran se dessine sur du noir. Le cadran CLASSIQUE n'a pas de fond.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "board.h"
#include "dessin.h"
#include "cadrans.h"

#ifndef CADRAN_DIR
#include "sd.h"
#define CADRAN_DIR SD_DIR
#endif

#define CX 233
#define CY 233
#define TOUR 6.2831853f

static const char *JOURS[7] = { "DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI" };
static const char *MOIS[12] = { "JANVIER", "FÉVRIER", "MARS", "AVRIL", "MAI", "JUIN", "JUILLET", "AOÛT", "SEPTEMBRE",
                                "OCTOBRE", "NOVEMBRE", "DÉCEMBRE" };
static const char *JOURS3[7] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
static const char *MOIS3[12] = { "JAN", "FÉV", "MAR", "AVR", "MAI", "JUN", "JUL", "AOÛ", "SEP", "OCT", "NOV", "DÉC" };

static void heure(const cadran_info_t *i, char *b, int n) {
    if (i->connue) snprintf(b, n, "%02d:%02d", i->l.tm_hour, i->l.tm_min); else snprintf(b, n, "--:--");
}
static void date_courte(const cadran_info_t *i, char *b, int n) {
    if (i->connue) snprintf(b, n, "%s %d %s", JOURS3[i->l.tm_wday % 7], i->l.tm_mday, MOIS3[i->l.tm_mon % 12]);
    else snprintf(b, n, "HEURE INCONNUE");
}
static void date_longue(const cadran_info_t *i, char *b, int n) {
    if (i->connue) snprintf(b, n, "%s %d %s", JOURS[i->l.tm_wday % 7], i->l.tm_mday, MOIS[i->l.tm_mon % 12]);
    else snprintf(b, n, "HEURE : CONNECTER LE TÉLÉPHONE");
}
static void bat(const cadran_info_t *i, char *b, int n) {
    if (i->batterie >= 0) snprintf(b, n, "%d %%%s", i->batterie, i->usb ? "+" : ""); else snprintf(b, n, "-- %%");
}
static void temp(const cadran_info_t *i, char *b, int n) {
    if (i->temp > -100) snprintf(b, n, "%d °C", i->temp); else snprintf(b, n, "-- °C");
}
static const char *bt(const cadran_info_t *i) { return !i->bt_on ? "BT COUPÉ" : i->bt_lien ? "TÉLÉPHONE" : "BT"; }
static float sec(const cadran_info_t *i) { return i->connue ? i->s / 60.0f : 0; }
static bool bat_faible(const cadran_info_t *i) { return i->batterie >= 0 && i->batterie < 15 && !i->usb; }
static float fr(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* Aiguille de centre (cx, cy), de r0 (négatif : queue) à r1, à la fraction de tour a */
static void aiguille(float cx, float cy, float r0, float r1, float a, float ep, uint16_t c) {
    const float s = sinf(a * TOUR), co = cosf(a * TOUR);
    d_ligne(cx + s * r0, cy - co * r0, cx + s * r1, cy - co * r1, ep, c);
}

/* ---------- CLASSIQUE (sans fond) ---------- */
static void classique(const cadran_info_t *i) {
    for (int k = 0; k < 60; k++) {
        const float a = k * TOUR / 60;
        const int r0 = k % 5 ? 222 : 214;
        for (int r = r0; r <= 228; r += 2) d_rect(CX + (int)(sinf(a) * r) - 1, CY - (int)(cosf(a) * r) - 1, 2, 2, k % 5 ? C_GRIS : C_BLANC);
    }
    if (i->connue) d_arc(CX, CY, 200, 207, 0, i->s / 60.0f, C_CYAN);
    char b[64];
    heure(i, b, sizeof b);
    d_texte(&P_HEURE, CX, 128, b, C_BLANC, A_CENTRE);
    date_longue(i, b, sizeof b);
    d_texte(&P_TEXTE, CX, 222, b, C_GRIS, A_CENTRE);
    b[0] = 0;
    if (i->batterie >= 0) snprintf(b, sizeof b, "%d %%%s", i->batterie, i->usb ? " · CHARGE" : "");
    d_texte(&P_TEXTE, CX - 20, 270, b, bat_faible(i) ? C_ROUGE : C_BLANC, A_DROITE);
    d_texte(&P_TEXTE, CX + 20, 270, bt(i), i->bt_lien ? C_CYAN : C_GRIS, A_GAUCHE);
    if (i->nnotifs) {
        snprintf(b, sizeof b, "%d NOTIFICATION%s", i->nnotifs, i->nnotifs > 1 ? "S" : "");
        d_rect_arrondi(CX - 110, 310, 220, 34, 14, d_mele(C_FOND, C_MAGENTA, 5));
        d_texte(&P_TEXTE, CX, 316, b, C_MAGENTA, A_CENTRE);
    }
    d_texte(&P_TEXTE, CX, 380, "« NOTIFS    APPS »", d_mele(C_NOIR, C_GRIS, 8), A_CENTRE);
}

/* ---------- NÉON : ville sous la pluie ---------- */
static void neon(const cadran_info_t *i) {
    char b[48], c[24];
    bat(i, c, sizeof c);
    snprintf(b, sizeof b, "%s · %s", c, bt(i));
    d_texte_lueur(&P_MONO, CX, 44, b, bat_faible(i) ? C_ROUGE : RGB(255, 220, 245), C_NOIR, A_CENTRE);
    heure(i, b, sizeof b);
    d_texte_lueur(&P_NEON, CX, d_centre_y(&P_NEON, 126), b, RGB(255, 235, 250), RGB(255, 40, 170), A_CENTRE);
    date_longue(i, b, sizeof b);
    d_texte_lueur(&P_TEXTE, CX, 190, b, RGB(80, 230, 255), C_NOIR, A_CENTRE);
    if (i->nnotifs) {
        snprintf(b, sizeof b, "%d MESSAGE%s", i->nnotifs, i->nnotifs > 1 ? "S" : "");
        d_texte_lueur(&P_MONO, CX, 314, b, RGB(255, 170, 40), C_NOIR, A_CENTRE);
    }
}

/* ---------- HUD : chronographe analogique ---------- */
static void hud(const cadran_info_t *i) {
    char b[32];
    /* compteurs BAT et TEMP : 270° de 0,625 à 1,375 tour */
    const float pb = i->batterie >= 0 ? i->batterie / 100.0f : 0, pt = i->temp > -100 ? fr((i->temp - 20) / 60.0f) : 0;
    aiguille(150, 262, -8, 40, 0.625f + 0.75f * pb, 2.4f, bat_faible(i) ? C_ROUGE : C_ORANGE);
    aiguille(316, 262, -8, 40, 0.625f + 0.75f * pt, 2.4f, C_ORANGE);
    d_anneau(150, 262, 0, 4, C_BLANC);
    d_anneau(316, 262, 0, 4, C_BLANC);
    if (i->usb) d_texte(&P_MONO, 150, 296, "CHARGE", C_VERT, A_CENTRE);
    /* guichet de date */
    if (i->connue) snprintf(b, sizeof b, "%s %d", JOURS3[i->l.tm_wday % 7], i->l.tm_mday); else snprintf(b, sizeof b, "--");
    d_texte(&P_MONO, CX, 341, b, C_BLANC, A_CENTRE);
    d_texte(&P_MONO, CX, 168, bt(i), i->bt_lien ? C_CYAN : C_GRIS, A_CENTRE);
    if (i->nnotifs) { snprintf(b, sizeof b, "%d MSG", i->nnotifs); d_texte(&P_MONO, CX, 190, b, C_MAGENTA, A_CENTRE); }
    if (!i->connue) return;
    const float m = (i->l.tm_min + i->s / 60.0f) / 60, h = (i->l.tm_hour % 12 + m) / 12;
    aiguille(CX + 2, CY + 3, -14, 112, h, 9, RGB(0, 0, 0));             /* ombres */
    aiguille(CX + 2, CY + 3, -18, 168, m, 6, RGB(0, 0, 0));
    aiguille(CX, CY, -14, 112, h, 8, RGB(225, 230, 240));
    aiguille(CX, CY, 70, 108, h, 3, C_CYAN);
    aiguille(CX, CY, -18, 168, m, 5, RGB(225, 230, 240));
    aiguille(CX, CY, 120, 164, m, 2, C_CYAN);
    aiguille(CX, CY, -36, 196, sec(i), 2, C_ROUGE);
    d_anneau(CX, CY, 0, 9, RGB(225, 230, 240));
    d_anneau(CX, CY, 0, 4, C_ROUGE);
}

/* ---------- LUNE ORBITALE ---------- */
static void lune(const cadran_info_t *i) {
    char b[48];
    heure(i, b, sizeof b);
    d_texte_lueur(&P_GEANT, 56, d_centre_y(&P_GEANT, 132), b, C_BLANC, RGB(30, 90, 160), A_GAUCHE);
    date_longue(i, b, sizeof b);
    d_texte(&P_TEXTE, 62, 176, b, RGB(160, 200, 240), A_GAUCHE);
    bat(i, b + 24, 24);
    snprintf(b, 24, "ÉNERGIE ");
    strcat(b, b + 24);
    d_texte(&P_MONO, 62, 262, b, bat_faible(i) ? C_ROUGE : RGB(200, 220, 240), A_GAUCHE);
    temp(i, b + 24, 24);
    snprintf(b, 24, "COQUE   ");
    strcat(b, b + 24);
    d_texte(&P_MONO, 62, 284, b, RGB(200, 220, 240), A_GAUCHE);
    d_texte(&P_MONO, 62, 306, bt(i), i->bt_lien ? RGB(60, 200, 255) : C_GRIS, A_GAUCHE);
    if (i->nnotifs) { snprintf(b, sizeof b, "%d MSG", i->nnotifs); d_texte(&P_MONO, 62, 328, b, RGB(255, 170, 40), A_GAUCHE); }
    /* satellite sur l'orbite (un tour par minute) */
    const float a = sec(i) * TOUR;
    const int sx = 358 + (int)(cosf(a) * 250), sy = 72 + (int)(sinf(a) * 62);   /* orbite du fond (outils/cadrans.py) */
    d_anneau(sx, sy, 0, 9, RGB(20, 70, 110));
    d_anneau(sx, sy, 0, 5, RGB(120, 230, 255));
    d_anneau(sx, sy, 0, 2, C_BLANC);
}

/* ---------- ALERTE ROUGE (mecha) ---------- */
static void mecha(const cadran_info_t *i) {
    char b[64], c[24], t[24];
    heure(i, b, sizeof b);
    d_texte_lueur(&P_ARMOR, CX, d_centre_y(&P_ARMOR, 186), b, RGB(255, 240, 235), RGB(255, 30, 30), A_CENTRE);
    /* barre des secondes : 60 crans */
    const int s = i->connue ? i->l.tm_sec : -1;
    for (int k = 0; k < 60; k++) d_rect(113 + k * 4, 244, 3, 14, k <= s ? (k % 5 == 4 ? RGB(255, 200, 60) : RGB(255, 60, 40)) : RGB(60, 10, 10));
    date_courte(i, b, sizeof b);
    d_texte(&P_MONO, CX, 270, b, RGB(255, 220, 210), A_CENTRE);
    bat(i, c, sizeof c);
    temp(i, t, sizeof t);
    snprintf(b, sizeof b, "BAT %s · %s", c, t);
    d_texte(&P_MONO, CX, 368, b, bat_faible(i) ? RGB(255, 200, 60) : RGB(255, 140, 120), A_CENTRE);
    d_texte(&P_MONO, CX, 300, bt(i), i->bt_lien ? RGB(255, 220, 210) : RGB(140, 60, 60), A_CENTRE);
    if (i->nnotifs) { snprintf(b, sizeof b, "%d MSG", i->nnotifs); d_texte_lueur(&P_MONO, 382, 330, b, RGB(255, 200, 60), C_NOIR, A_CENTRE); }
}

/* ---------- RADAR ---------- */
static void radar(const cadran_info_t *i) {
    const uint16_t vert = RGB(60, 255, 120), sombre = RGB(0, 150, 60);
    const float a = sec(i);
    d_secteur(CX, CY, 0, 204, a, 0.16f, vert, 9);
    aiguille(CX, CY, 0, 204, a, 2, RGB(180, 255, 200));
    /* échos : un par notification, et le téléphone relié */
    for (int k = 0; k < i->nnotifs && k < 8; k++) {
        const float b = 0.11f + k * 0.37f;
        const int r = 60 + (k * 47) % 120;
        const int x = CX + (int)(sinf(b * TOUR) * r), y = CY - (int)(cosf(b * TOUR) * r);
        float d = a - b;
        d -= floorf(d);
        d_anneau(x, y, 0, 7, d_mele(vert, C_NOIR, (int)(d * 12)));
        d_anneau(x, y, 0, 3, C_BLANC);
    }
    if (i->bt_lien) d_anneau(CX + 96, CY - 40, 0, 4, RGB(180, 255, 200));
    char b[32];
    bat(i, b, sizeof b);
    d_texte(&P_MONO, 174, 124, b, bat_faible(i) ? C_ROUGE : vert, A_DROITE);
    temp(i, b, sizeof b);
    d_texte(&P_MONO, 344, 124, b, vert, A_DROITE);
    heure(i, b, sizeof b);
    d_texte_lueur(&P_HEURE, CX, d_centre_y(&P_HEURE, 314), b, RGB(180, 255, 200), sombre, A_CENTRE);
    date_courte(i, b, sizeof b);
    d_texte(&P_MONO, CX, 358, b, vert, A_CENTRE);
}

/* ---------- GEAR TYPE-04 (plan de robot) ---------- */
static void gear(const cadran_info_t *i) {
    const uint16_t orange = RGB(255, 150, 40), clair = RGB(200, 225, 255);
    char b[48], c[24];
    heure(i, b, sizeof b);
    d_texte_lueur(&P_FUTUR, CX, d_centre_y(&P_FUTUR, 84), b, orange, RGB(120, 50, 0), A_CENTRE);
    date_courte(i, b, sizeof b);
    if (i->nnotifs) { snprintf(c, sizeof c, " · %d MSG", i->nnotifs); strcat(b, c); }
    d_texte(&P_MONO, CX, 122, b, i->nnotifs ? RGB(255, 120, 200) : clair, A_CENTRE);
    bat(i, c, sizeof c);
    snprintf(b, sizeof b, "RÉACTEUR %s", c);
    d_texte(&P_MONO, 48, 324, b, bat_faible(i) ? C_ROUGE : clair, A_GAUCHE);
    temp(i, b, sizeof b);
    d_texte(&P_MONO, 418, 324, b, clair, A_DROITE);
    d_texte(&P_MONO, 76, 140, i->bt_lien ? "LIAISON" : "", RGB(90, 230, 255), A_CENTRE);
}

/* ---------- GLOBE ---------- */
static void globe(const cadran_info_t *i) {
    char b[48], c[24];
    heure(i, b, sizeof b);
    d_texte_lueur(&P_HEURE, CX, d_centre_y(&P_HEURE, 76), b, C_BLANC, RGB(20, 90, 110), A_CENTRE);
    /* secondes : arc autour du globe et point en tête */
    const float a = sec(i);
    d_arc(CX, 236, 146, 148, 0, a, RGB(80, 220, 210));
    const int x = CX + (int)(sinf(a * TOUR) * 147), y = 236 - (int)(cosf(a * TOUR) * 147);
    d_anneau(x, y, 0, 5, RGB(160, 255, 240));
    date_longue(i, b, sizeof b);
    d_texte(&P_TEXTE, CX, 366, b, C_BLANC, A_CENTRE);
    bat(i, c, sizeof c);
    if (i->nnotifs) snprintf(b, sizeof b, "%s · %s · %d MSG", c, bt(i), i->nnotifs); else snprintf(b, sizeof b, "%s · %s", c, bt(i));
    d_texte(&P_MONO, CX, 390, b, bat_faible(i) ? C_ROUGE : RGB(80, 220, 210), A_CENTRE);
}

/* ---------- SYNTHWAVE ---------- */
static void synthwave(const cadran_info_t *i) {
    char b[48], c[24];
    heure(i, b, sizeof b);
    const int y = d_centre_y(&P_FUTUR, 94);
    d_texte_lueur(&P_FUTUR, CX, y, b, RGB(40, 8, 60), RGB(255, 60, 200), A_CENTRE);
    d_texte_ex(&P_FUTUR, CX, y, b, RGB(120, 240, 255), RGB(255, 120, 230), 16, A_CENTRE);
    date_longue(i, b, sizeof b);
    d_texte_lueur(&P_TEXTE, CX, 140, b, C_BLANC, RGB(60, 10, 70), A_CENTRE);
    bat(i, c, sizeof c);
    if (i->nnotifs) snprintf(b, sizeof b, "%s · %s · %d MSG", c, bt(i), i->nnotifs); else snprintf(b, sizeof b, "%s · %s", c, bt(i));
    d_texte_lueur(&P_MONO, CX, 346, b, bat_faible(i) ? C_ROUGE : RGB(255, 220, 245), C_NOIR, A_CENTRE);
}

/* ---------- GEAR SYSTEM MK-IV ---------- */
static void mk4(const cadran_info_t *i) {
    const uint16_t teal = RGB(40, 220, 210), orange = RGB(255, 140, 40);
    d_segments(CX, CY, 207, 223, 60, i->connue ? i->l.tm_sec + 1 : 0, 0.18f, teal);
    const float pb = i->batterie >= 0 ? i->batterie / 100.0f : 0, pt = i->temp > -100 ? fr((i->temp - 20) / 60.0f) : 0;
    d_arc(CX, CY, 185, 191, 0.58f, 0.58f + 0.34f * pb, bat_faible(i) ? C_ROUGE : orange);
    d_arc(CX, CY, 185, 191, 0.42f - 0.34f * pt, 0.42f, teal);
    char b[48];
    bat(i, b, sizeof b);
    d_texte(&P_MONO, 78, 244, b, orange, A_CENTRE);
    temp(i, b, sizeof b);
    d_texte(&P_MONO, 388, 244, b, teal, A_CENTRE);
    heure(i, b, sizeof b);
    d_texte_lueur(&P_ARMOR, CX, d_centre_y(&P_ARMOR, 206), b, C_BLANC, RGB(20, 120, 120), A_CENTRE);
    date_courte(i, b, sizeof b);
    d_texte(&P_MONO, CX, 252, b, teal, A_CENTRE);
    if (i->nnotifs) snprintf(b, sizeof b, "%d MSG · %s", i->nnotifs, bt(i)); else snprintf(b, sizeof b, "%s", bt(i));
    d_texte(&P_MONO, CX, 290, b, i->nnotifs ? orange : RGB(40, 160, 160), A_CENTRE);
}

/* ---------- Table ---------- */
typedef struct { const char *id, *nom; void (*dessiner)(const cadran_info_t *); } cadran_t;
static const cadran_t CADRANS[] = {
    { NULL, "CLASSIQUE", classique },
    { "neon", "NÉON", neon },
    { "hud", "CHRONO HUD", hud },
    { "lune", "LUNE", lune },
    { "mecha", "ALERTE", mecha },
    { "radar", "RADAR", radar },
    { "gear", "GEAR TYPE-04", gear },
    { "globe", "GLOBE", globe },
    { "synthwave", "SYNTHWAVE", synthwave },
    { "mk4", "MK-IV", mk4 },
};
#define NB (int)(sizeof CADRANS / sizeof CADRANS[0])
static int choisi;
static uint16_t *fond;          /* image du fond (ordre d'octets de l'écran), PSRAM */
static bool fond_ok;

int cadrans_nb(void) { return NB; }
const char *cadran_nom(int k) { return k >= 0 && k < NB ? CADRANS[k].nom : ""; }

void cadran_choisir(int k) {
    choisi = k >= 0 && k < NB ? k : 0;
    fond_ok = false;
    const char *id = CADRANS[choisi].id;
    if (!id) return;
    char chemin[96];
    snprintf(chemin, sizeof chemin, CADRAN_DIR "/cadran_%s.cad", id);
    FILE *f = fopen(chemin, "rb");
    if (!f) return;
    uint8_t tete[10];
    uint16_t pal[256];
    if (fread(tete, 1, 10, f) == 10 && !memcmp(tete, "CAD1", 4)) {
        const int w = tete[4] | tete[5] << 8, h = tete[6] | tete[7] << 8, n = tete[8] | tete[9] << 8;
        if (w == LCD_W && h == LCD_H && n > 0 && n <= 256 && fread(pal, 2, (size_t)n, f) == (size_t)n) {
            if (!fond) fond = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
            uint8_t *ix = malloc(LCD_W * 32);   /* 32 lignes à la fois */
            if (fond && ix) {
                for (int k = 0; k < n; k++) pal[k] = (uint16_t)(pal[k] >> 8 | pal[k] << 8);   /* ordre de l'écran */
                fond_ok = true;
                for (int y = 0; y < LCD_H && fond_ok; y += 32) {
                    const int hh = LCD_H - y < 32 ? LCD_H - y : 32;
                    if (fread(ix, 1, (size_t)(LCD_W * hh), f) != (size_t)(LCD_W * hh)) { fond_ok = false; break; }
                    for (int j = 0; j < LCD_W * hh; j++) fond[y * LCD_W + j] = pal[ix[j] < n ? ix[j] : 0];
                }
            }
            free(ix);
        }
    }
    fclose(f);
}

void cadran_dessiner(const cadran_info_t *i) {
    if (fond_ok) d_image(fond);
    CADRANS[choisi].dessiner(i);
}

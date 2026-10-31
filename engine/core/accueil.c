/*
 * Ouverture du jeu (comme celle de l'émulateur) : écran titre (CONTINUER, NOUVELLE PARTIE, CHARGER, et RÉGLAGES si la
 * plateforme en a : eng_config_t.reglages, app Android), choix d'une des
 * PARTIES sauvegardées, introduction du monde en cartons (section OUVR, onglet Quêtes de l'Atelier, entrée
 * « Introduction »), puis le choix de l'héroïne (« Qui es-tu ? » : Sera et les personnages cochés « Héroïne au choix »).
 *
 * Pendant l'ouverture, le monde est arrêté (eng_update s'arrête après l'interface) et Sera est cachée : la 3D montre le
 * lieu du fond ou du carton, la caméra tourne lentement autour de lui, à son heure (lumière, néons de Halo). Un carton
 * sur le noir : des cendres qui tombent, une lueur de braise. Gestes : toucher (choisir, carton suivant), glisser à
 * gauche ou à droite (héroïne voisine), maintenir (passer l'introduction) ; touches : haut / bas / gauche / droite, OK,
 * retour.
 */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "internal.h"
#include "ui.h"
#include "audio.h"

#define CX 233.0f
#define CY 233.0f
#define WHITE 0xFFFFFF
#define INK 0x05040A
#define DIM 0x9AA6B8

/* Durées (s) : fondu d'entrée de l'écran titre ; entrée, lecture (base, par caractère, plafond) et sortie d'un carton ;
   appui qui passe l'introduction ; fondu au noir vers le jeu */
#define T_TITRE 1.6f
#define T_IN 0.9f
#define T_LIRE0 2.2f
#define T_LIRE_C 0.05f
#define T_LIRE_MAX 9.0f
#define T_OUT 0.8f
#define T_PASSER 0.8f
#define T_FONDU 0.7f
#define ORBITE 0.05f             /* rad/s : la caméra tourne autour du lieu montré */
#define HEURE_AUBE 0.27f         /* écran du choix de l'héroïne : le départ, à l'aube */
#define HER_DIR_S 1.3f           /* l'héroïne montrée tourne d'un huitième de tour toutes les 1,3 s */
#define HER_PIEDS 318.0f         /* ligne de ses pieds à l'écran */
#define HER_H 168.0f             /* hauteur de sa cellule à l'écran (px) */
#define HER_MAX 128              /* cellule la plus grande dessinée */
#define NCENDRES 40

enum { AP_TITRE, AP_PARTIES, AP_INTRO, AP_HEROINE, AP_APPS, AP_REGL };
enum { AC_CONTINUER, AC_NOUVELLE, AC_CHARGER, AC_REGLAGES, AC_APPS };
/* Nom d'une entrée de l'écran titre (RÉGLAGES renommée par la plateforme : TRANSFERT de l'ESP32) */
static const char *nom_option(const char *const *nom, int o) { return o == AC_REGLAGES && E.cfg.reglages_nom ? E.cfg.reglages_nom : nom[o]; }
enum { B_OPT, B_PARTIE, B_RETOUR, B_PREC, B_SUIV, B_CHOISIR, B_APP, B_REG_MOINS, B_REG_PLUS };

static struct {
    int on, page, sel, nouvelle;        /* nouvelle : page des parties d'une nouvelle partie (sinon : charger) */
    float t, t0;                        /* temps de l'ouverture, début de la page */
    int carte; float ct0;               /* carton en cours, son début */
    float fx, fz, heure;                /* lieu montré (fx < 0 : noir), son heure */
    int her; float ht0;                 /* héroïne montrée (0 : Sera, k : ouv_her[k - 1]), début de sa vue */
    int partie;                         /* partie de la nouvelle partie */
    int fondu; float ft0;               /* fondu au noir, puis la partie commence */
    int down, geste; float x0, y0, cx, cy, dt0;   /* doigt : départ, position, début ; geste déjà pris */
    uint32_t prev;
    float yaw0, zoom0, heure0;          /* caméra et heure du départ, rendues au jeu */
    partie_info_t info[PARTIES]; int ok[PARTIES];
    int opts[4], nopts;                 /* entrées de l'écran titre */
} A;
static struct { float x, y, w, h; int act, arg; } AB[12];
static int nab;
static uint16_t C1, C2;
/* Image de l'héroïne montrée : deux tampons alternés (une image se prépare pendant que l'autre se dessine) */
static uint16_t HPX[2][HER_MAX * HER_MAX] ENG_FROID;
static uint8_t HMK[2][HER_MAX * HER_MAX] ENG_FROID;
static int hbuf;

int accueil_on(void) { return A.on; }
float accueil_heure(void) { return A.heure; }
const char *accueil_musique(void) { return E.w.ouv_musique; }

int heroine_de(const char *nom) {
    for (int k = 0; nom && nom[0] && k < E.w.ouv_nher; k++) if (!strcmp(E.w.persos[E.w.ouv_her[k]].nom, nom)) return E.w.ouv_her[k];
    for (int p = 0; nom && nom[0] && p < E.w.npersos; p++) if (E.w.persos[p].sheet >= 0 && !strcmp(E.w.persos[p].nom, nom)) return p;
    return -1;
}
static int her_perso(int k) { return k > 0 && k <= E.w.ouv_nher ? E.w.ouv_her[k - 1] : -1; }
static const char *her_nom(int k) { const int p = her_perso(k); return p >= 0 ? E.w.persos[p].nom : PARTY[0].name; }

/* ---------- Lieu montré ---------- */
static void montrer(float x, float z, float heure) {
    A.heure = heure < 0 ? A.heure0 : heure;
    if (x < 0) { A.fx = -1; return; }
    if (E.w.carte) world_carte(0, x, z);   /* lieux de l'ouverture : carte 0 */
    if (A.fx < 0 || fabsf(x - A.fx) > 0.5f || fabsf(z - A.fz) > 0.5f) {
        A.fx = x; A.fz = z;
        E.sera.x = x; E.sera.z = z; E.sera_y_ok = 0;
        E.stream_hold = 1;   /* chunks chargés à la mise à jour suivante (l'image préparée entre-temps n'a pas de 3D) */
    }
}
static void parties_lire(void) {
    for (int p = 0; p < PARTIES; p++) A.ok[p] = hud_partie_info(p, &A.info[p]);
    A.nopts = 0;
    int any = 0; for (int p = 0; p < PARTIES; p++) any |= A.ok[p];
    if (any) A.opts[A.nopts++] = AC_CONTINUER;
    A.opts[A.nopts++] = AC_NOUVELLE;
    if (any) A.opts[A.nopts++] = AC_CHARGER;
    if (E.cfg.reglages) A.opts[A.nopts++] = AC_REGLAGES;
    if (E.cfg.napps > 0 || E.cfg.nregls > 0) A.opts[A.nopts++] = AC_APPS;
}
static int derniere_partie(void) {
    int b = -1;
    for (int p = 0; p < PARTIES; p++) if (A.ok[p] && (b < 0 || A.info[p].ordre > A.info[b].ordre)) b = p;
    return b;
}
static float carte_duree(int k) {
    int n = 0; for (const char *s = E.w.ouv_cartes[k].texte; *s; s++) n += (*s & 0xC0) != 0x80;
    const float l = T_LIRE0 + n * T_LIRE_C;
    return T_IN + (l < T_LIRE_MAX ? l : T_LIRE_MAX) + T_OUT;
}
static void page(int p) {
    A.page = p; A.t0 = A.t; A.down = 0; A.geste = 1;   /* le doigt posé pendant le changement ne compte plus */
    if (p == AP_TITRE) { A.sel = 0; montrer(E.w.ouv_fond[0], E.w.ouv_fond[1], E.w.ouv_fond[2]); }
    else if (p == AP_REGL) A.sel = 0;
    else if (p == AP_APPS) { A.sel = 0; montrer(E.w.ouv_fond[0], E.w.ouv_fond[1], E.w.ouv_fond[2]); }
    else if (p == AP_PARTIES) A.sel = A.nouvelle ? 0 : (derniere_partie() > 0 ? derniere_partie() : 0);
    else if (p == AP_INTRO) { A.carte = 0; A.ct0 = A.t; montrer(E.w.ouv_cartes[0].x, E.w.ouv_cartes[0].z, E.w.ouv_cartes[0].heure); }
    else if (p == AP_HEROINE) { A.her = 0; A.ht0 = A.t; montrer(E.w.start_x, E.w.start_z, HEURE_AUBE); }
}

static void charger(int p);
void accueil_debut(void) {
    memset(&A, 0, sizeof A);
    A.on = 1; A.fx = -1;
    A.yaw0 = E.yaw; A.zoom0 = E.zoom; A.heure0 = E.heure; A.heure = E.heure;
    eng_set_zoom(E.w.zoom_min);   /* vue large du lieu */
    parties_lire();
    page(E.cfg.napps > 0 && !E.cfg.montre ? AP_APPS : AP_TITRE);   /* lancé depuis l'app montre : l'écran titre du jeu */
    if (E.cfg.reprendre) { E.cfg.reprendre = 0; if (derniere_partie() >= 0) charger(derniere_partie()); }   /* retour de veille */
}

/* La partie commence : Sera (ou l'héroïne k) au départ, caméra et heure d'origine */
static void jouer(int k) {
    hud_nouvelle(A.partie);
    E.heroine = her_perso(k);
    if (E.w.carte) world_carte(0, E.w.start_x, E.w.start_z);
    E.sera.x = E.w.start_x; E.sera.z = E.w.start_z; E.sera.dir = 0.35f + PI_F; E.sera_y_ok = 0;
    E.yaw = A.yaw0; E.heure = A.heure0; eng_set_zoom(A.zoom0);
    E.stream_hold = 1;
    A.on = 0;
    BT.fade_t = E.clock;   /* sortie du noir, comme après un combat */
}
void accueil_fin(void) { if (A.on) { A.partie = 0; jouer(0); } }
void eng_skip_opening(void) { accueil_fin(); }
static void charger(int p) {
    E.heure = A.heure0; eng_set_zoom(A.zoom0);
    E.yaw = A.yaw0;
    if (!hud_charger(p)) return;
    E.sera_y_ok = 0; E.stream_hold = 1;
    A.on = 0;
    BT.fade_t = E.clock;
}
/* Nouvelle partie : une partie vide (sinon la page des parties), l'introduction s'il y en a, puis le choix */
static void nouvelle(void) {
    for (int p = 0; p < PARTIES; p++) if (!A.ok[p]) { A.partie = p; page(E.w.ouv_ncartes ? AP_INTRO : AP_HEROINE); return; }
    A.nouvelle = 1; page(AP_PARTIES);
}
static void apres_intro(void) { page(AP_HEROINE); }
static void choisir(void) { son_jouer(SON_VALIDER, 0.5f, 1); A.fondu = 1; A.ft0 = A.t; }

static void valider_titre(int k) {
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (A.opts[k] == AC_CONTINUER) charger(derniere_partie());
    else if (A.opts[k] == AC_NOUVELLE) nouvelle();
    else if (A.opts[k] == AC_REGLAGES) E.cfg.reglages();
    else if (A.opts[k] == AC_APPS) page(AP_APPS);
    else { A.nouvelle = 0; page(AP_PARTIES); }
}
static void valider_partie(int p) {
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (!A.nouvelle) { if (A.ok[p]) charger(p); return; }
    A.partie = p; page(E.w.ouv_ncartes ? AP_INTRO : AP_HEROINE);
}
static void carte_suivante(void) {
    if (++A.carte >= E.w.ouv_ncartes) { apres_intro(); return; }
    A.ct0 = A.t;
    montrer(E.w.ouv_cartes[A.carte].x, E.w.ouv_cartes[A.carte].z, E.w.ouv_cartes[A.carte].heure);
}
static void her_pas(int d) { const int n = E.w.ouv_nher + 1; A.her = (A.her + d + n) % n; A.ht0 = A.t; son_jouer(SON_MENU, 0.35f, 1); }
/* Application de la plateforme (eng_config_t.apps) : elle prend la main (l'ESP32 redémarre dans l'autre programme) */
/* Rangées du menu des applications : 0 le jeu (son écran titre), les eng_config_t.apps, puis RÉGLAGES s'il y en a */
static int napps_menu(void) { const int n = 1 + E.cfg.napps + (E.cfg.nregls > 0); return n < 4 ? n : 4; }
static void lancer_app(int k) {
    if (k < 0 || k >= napps_menu()) return;
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (!k) page(AP_TITRE);
    else if (k > E.cfg.napps) page(AP_REGL);
    else if (E.cfg.apps[k - 1].lancer) E.cfg.apps[k - 1].lancer();
}
static void regler(int k, int sens) {
    if (k < 0 || k >= E.cfg.nregls || !E.cfg.regls[k].changer) return;
    A.sel = k;
    E.cfg.regls[k].changer(sens);
    son_jouer(SON_MENU, 0.35f, sens > 0 ? 1.1f : 0.9f);
}
static void retour(void) {
    son_jouer(SON_ANNULER, 0.45f, 1);
    if (A.page == AP_REGL) page(AP_APPS);
    else if (A.page == AP_PARTIES || A.page == AP_HEROINE || A.page == AP_APPS) page(AP_TITRE);
    else if (A.page == AP_INTRO) apres_intro();
}
static void action(int act, int arg) {
    switch (act) {
    case B_OPT: if (arg == A.sel) valider_titre(arg); else A.sel = arg; break;
    case B_PARTIE: if (arg == A.sel) valider_partie(arg); else A.sel = arg; break;
    case B_RETOUR: retour(); break;
    case B_PREC: her_pas(-1); break;
    case B_SUIV: her_pas(1); break;
    case B_CHOISIR: choisir(); break;
    case B_APP: if (arg == A.sel) lancer_app(arg); else A.sel = arg; break;
    case B_REG_MOINS: regler(arg, -1); break;
    case B_REG_PLUS: regler(arg, 1); break;
    }
}

void accueil_update(float dt, const eng_input_t *in) {
    A.t += dt;
    C1 = ui_rgb(hud_theme_c1()); C2 = ui_rgb(hud_theme_c2());
    /* La caméra tourne lentement autour du lieu montré */
    E.yaw = A.yaw0 + A.t * ORBITE;
    if (A.fondu) {
        if (A.t - A.ft0 >= T_FONDU) jouer(A.her);
        A.prev = in->keys;
        return;
    }
    if (A.page == AP_INTRO && A.t - A.ct0 >= carte_duree(A.carte)) carte_suivante();
    /* Doigt : un toucher sur un bouton (ou un carton) ; glisser à gauche ou à droite (héroïne) ; maintenir (passer) */
    if (in->touch && !A.down) { A.down = 1; A.geste = 0; A.x0 = A.cx = in->touch_x; A.y0 = A.cy = in->touch_y; A.dt0 = A.t; }
    else if (in->touch) { A.cx = in->touch_x; A.cy = in->touch_y; }
    if (A.down && in->touch && !A.geste) {
        const float dx = A.cx - A.x0, dy = A.cy - A.y0;
        if (A.page == AP_HEROINE && fabsf(dx) > 50 && fabsf(dx) > fabsf(dy)) { her_pas(dx < 0 ? 1 : -1); A.geste = 1; }
        else if (A.page == AP_INTRO && A.t - A.dt0 > T_PASSER && hypotf(dx, dy) < 30) { apres_intro(); A.geste = 1; }
    }
    if (A.down && !in->touch) {
        A.down = 0;
        if (!A.geste && hypotf(A.cx - A.x0, A.cy - A.y0) < 20) {
            int hit = -1;
            for (int i = 0; i < nab; i++)
                if (A.cx >= AB[i].x && A.cx <= AB[i].x + AB[i].w && A.cy >= AB[i].y && A.cy <= AB[i].y + AB[i].h) { hit = i; break; }
            if (hit >= 0) action(AB[hit].act, AB[hit].arg);
            else if (A.page == AP_INTRO) {   /* carton suivant : sa fin tout de suite (ou son texte entier s'il entrait) */
                const float t = A.t - A.ct0, d = carte_duree(A.carte);
                if (t < T_IN) A.ct0 = A.t - T_IN; else if (t < d - T_OUT) A.ct0 = A.t - (d - T_OUT);
            }
        }
    }
    const uint32_t k = in->keys & ~A.prev;
    A.prev = in->keys;
    if (k & ENG_KEY_BACK) { retour(); return; }
    switch (A.page) {
    case AP_TITRE:
        if (k & (ENG_KEY_DOWN | ENG_KEY_RIGHT)) A.sel = (A.sel + 1) % A.nopts;
        if (k & (ENG_KEY_UP | ENG_KEY_LEFT)) A.sel = (A.sel + A.nopts - 1) % A.nopts;
        if (k & ENG_KEY_OK) valider_titre(A.sel);
        break;
    case AP_PARTIES:
        if ((k & ENG_KEY_DOWN) && A.sel < PARTIES - 1) A.sel++;
        if ((k & ENG_KEY_UP) && A.sel > 0) A.sel--;
        if (k & ENG_KEY_OK) valider_partie(A.sel);
        break;
    case AP_APPS:
        if ((k & ENG_KEY_DOWN) && A.sel < napps_menu() - 1) A.sel++;
        if ((k & ENG_KEY_UP) && A.sel > 0) A.sel--;
        if (k & ENG_KEY_OK) lancer_app(A.sel);
        break;
    case AP_REGL:
        if ((k & ENG_KEY_DOWN) && A.sel < E.cfg.nregls - 1) A.sel++;
        if ((k & ENG_KEY_UP) && A.sel > 0) A.sel--;
        if (k & ENG_KEY_LEFT) regler(A.sel, -1);
        if (k & (ENG_KEY_RIGHT | ENG_KEY_OK)) regler(A.sel, 1);
        break;
    case AP_INTRO:
        if (k & ENG_KEY_OK) carte_suivante();
        break;
    case AP_HEROINE:
        if (k & ENG_KEY_LEFT) her_pas(-1);
        if (k & ENG_KEY_RIGHT) her_pas(1);
        if (k & ENG_KEY_OK) choisir();
        break;
    }
}

/* ---------- Dessin ---------- */
/* Zone tactile dans le repère de l'image (décalée comme le dessin : cadre centré du plein écran) */
static void bouton(float x, float y, float w, float h, int act, int arg) { if (nab < 12) { AB[nab].x = x + ui_tx(); AB[nab].y = y + ui_ty(); AB[nab].w = w; AB[nab].h = h; AB[nab].act = act; AB[nab].arg = arg; nab++; } }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
/* Tirage fixe de la cendre i (0..1), le même que celui de l'émulateur */
static float h01(int i, int k) {
    uint32_t x = (uint32_t)i * 2654435761u ^ (uint32_t)k * 2246822519u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return (float)(x & 0xFFFF) / 65536.0f;
}
static void cendres(float a) {
    for (int i = 0; i < NCENDRES; i++) {
        const float v = 14 + 26 * h01(i, 1), x = h01(i, 0) * 466 + sinf(A.t * 0.4f + i) * 18;
        const float y = fmodf(h01(i, 2) * 520 + A.t * v, 520) - 30, r = 1 + 2 * h01(i, 3);
        const int braise = i % 7 == 0;
        ui_circle(x, y, r, ui_rgb(braise ? 0xFF7A3A : 0x8A8A92), (int)((70 + 120 * h01(i, 4)) * a));
    }
}
static void vignette(void) {
    if (ENG_DYN) return;   /* plein écran : pas de bord rond */
    ui_ring(CX, CY, 222, 30, ui_rgb(INK), 170);
    ui_ring(CX, CY, 196, 22, ui_rgb(INK), 80);
}
/* Texte coupé aux espaces en lignes de max_w pixels au plus (6 au plus) */
static int couper(const ui_font_t *f, const char *s, float max_w, char l[6][128]) {
    int n = 0; l[0][0] = 0;
    for (;;) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char *e = s; while (*e && *e != ' ') e++;
        const size_t m = (size_t)(e - s) < 63 ? (size_t)(e - s) : 63, ln = strlen(l[n]);
        char essai[128];
        const char *w0 = s;
        s = e;
        if (ln + m + 2 > sizeof essai) { if (n == 5) break; memcpy(l[++n], w0, m); l[n][m] = 0; continue; }   /* ligne pleine */
        memcpy(essai, l[n], ln); if (ln) essai[ln] = ' '; memcpy(essai + ln + (ln > 0), w0, m); essai[ln + (ln > 0) + m] = 0;
        if (ln && ui_text_width(f, essai) > max_w) { if (n == 5) break; memcpy(l[++n], w0, m); l[n][m] = 0; }
        else memcpy(l[n], essai, strlen(essai) + 1);
    }
    return l[0][0] ? n + 1 : 0;
}
static void texte_centre(int style, int size, const char *s, float max_w, float ymid, float lh, uint16_t c, int a) {
    char l[6][128];
    const ui_font_t *f = ui_font(style, size);
    const int n = couper(f, s, max_w, l);
    for (int i = 0; i < n; i++) ui_text(f, CX, ymid - (n - 1) * lh / 2 + i * lh + size * 0.35f, UI_CENTER, c, a, l[i]);
}
static void rangee(float x, float y, float w, float h, int sel, const char *lab, const char *val, uint16_t lc) {
    float q[12]; ui_chamfer_path(x, y, w, h, 10, q);
    const uint16_t col = sel ? C2 : C1;
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
    ui_outline(q, 6, sel ? 2 : 1, col, sel ? 255 : 128);
    if (val) {
        ui_text(ui_font(FONT_MONO, 20), x + w - 14, y + h / 2 + 7, UI_RIGHT, col, 255, val);
        const float vw = ui_text_width(ui_font(FONT_MONO, 20), val) + 16;
        ui_text(ui_fit(FONT_DISP, lab, w - vw - 32, 22, 16), x + 16, y + h / 2 + 8, UI_LEFT, lc, 255, lab);
    } else ui_text(ui_fit(FONT_DISP, lab, w - 32, 22, 16), x + w / 2, y + h / 2 + 8, UI_CENTER, lc, 255, lab);
}
static void bouton_retour(void) {
    float q[12]; ui_hex_path(CX, 428, 72, 54, q);
    ui_poly(q, 6, ui_rgb(0x060E1A), 242); ui_outline(q, 6, 2, ui_rgb(DIM), 255);
    ui_icon("back", CX, 428, 26, ui_rgb(DIM), 255);
    bouton(CX - 36, 401, 72, 54, B_RETOUR, 0);
}
static void duree_txt(float s, char *t, int n) { const int m = (int)(s / 60); snprintf(t, n, "%d h %02d", m / 60, m % 60); }

static void draw_titre(void) {
    const float tp = A.t;   /* fondu d'entrée : seulement au tout début */
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(INK), 90);
    vignette();
    cendres(0.6f);
    const ui_font_t *f = ui_fit(FONT_DISP, E.w.ouv_titre, 380, 40, 22);
    ui_text(f, CX + 2, 174, UI_CENTER, ui_rgb(INK), 200, E.w.ouv_titre);
    ui_text(f, CX, 172, UI_CENTER, ui_rgb(WHITE), 255, E.w.ouv_titre);
    ui_line(96, 188, 370, 188, 2, C2, 255);
    if (E.w.ouv_sous[0]) ui_text(ui_fit(FONT_MONO, E.w.ouv_sous, 320, 18, 14), CX, 212, UI_CENTER, C1, 255, E.w.ouv_sous);
    if (E.w.edition == 1) ui_text(ui_fit(FONT_MONO, "ÉDITION ENFANT", 220, 16, 12), CX, 128, UI_CENTER, C1, 255, "ÉDITION ENFANT");
    else if (E.w.edition == 2 && E.deverrou18) ui_text(ui_fit(FONT_MONO, "18+", 220, 16, 12), CX, 128, UI_CENTER, C2, 255, "18+");
    static const char *const NOM[5] = { "CONTINUER", "NOUVELLE PARTIE", "CHARGER", "RÉGLAGES", "APPLICATIONS" };
    const int quatre = A.nopts > 3;   /* RÉGLAGES en plus : rangées resserrées, au-dessus de la ligne d'aide */
    for (int i = 0; i < A.nopts; i++) {
        const float y = quatre ? 228 + i * 44 : 240 + i * 52, h = quatre ? 38 : 42;
        rangee(CX - 125, y, 250, h, i == A.sel, nom_option(NOM, A.opts[i]), NULL, ui_rgb(WHITE));
        bouton(CX - 125, y, 250, h, B_OPT, i);
    }
    char t[128], d[32];
    const int o = A.opts[A.sel], p = derniere_partie();
    if (o == AC_CONTINUER && p >= 0) { duree_txt(A.info[p].temps, d, sizeof d); snprintf(t, sizeof t, "Partie %d · %s · niv. %d · %s", p + 1, A.info[p].lieu, A.info[p].niveau, d); }
    else if (o == AC_NOUVELLE) snprintf(t, sizeof t, "%s", E.w.ouv_ncartes ? "L'histoire du monde, puis ton héroïne" : "Choisir son héroïne");
    else if (o == AC_REGLAGES) snprintf(t, sizeof t, "%s", E.cfg.reglages_aide ? E.cfg.reglages_aide : "Serveur, édition, monde à jour");
    else if (o == AC_APPS) snprintf(t, sizeof t, "Les autres programmes de la montre");
    else snprintf(t, sizeof t, "Reprendre une des %d parties", PARTIES);
    ui_text(ui_fit(FONT_MONO, t, 300, 18, 14), CX, 410, UI_CENTER, ui_rgb(DIM), 255, t);
    if (tp < T_TITRE) ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0), (int)(255 * (1 - tp / T_TITRE)));
}

static void draw_parties(void) {
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(INK), 222);
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    ui_ring(CX, CY, 215, 1, C1, 46);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, A.nouvelle ? "NOUVELLE PARTIE" : "CHARGER");
    const char *sub = A.nouvelle ? "Choisir la partie à remplacer" : "Choisir une partie";
    ui_text(ui_fit(FONT_MONO, sub, 300, 20, 16), CX, 138, UI_CENTER, ui_rgb(DIM), 255, sub);
    char t[128], v[24], d[32];
    for (int p = 0; p < PARTIES; p++) {
        const float y = 156 + p * 52, dy = y + 46 - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        if (A.ok[p]) { snprintf(t, sizeof t, "PARTIE %d · %s", p + 1, A.info[p].heroine[0] ? A.info[p].heroine : PARTY[0].name); snprintf(v, sizeof v, "NIV %d", A.info[p].niveau); }
        else { snprintf(t, sizeof t, "PARTIE %d", p + 1); snprintf(v, sizeof v, "VIDE"); }
        rangee(CX - hw, y, 2 * hw, 46, p == A.sel, t, v, A.ok[p] ? ui_rgb(WHITE) : ui_rgb(DIM));
        bouton(CX - hw, y, 2 * hw, 46, B_PARTIE, p);
    }
    const int p = A.sel;
    if (A.ok[p]) { duree_txt(A.info[p].temps, d, sizeof d); snprintf(t, sizeof t, "%s · %s de jeu · n° %u", A.info[p].lieu, d, (unsigned)A.info[p].seq); }
    else snprintf(t, sizeof t, "%s", A.nouvelle ? "Partie libre" : "Aucune sauvegarde");
    ui_text(ui_fit(FONT_MONO, t, 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, t);
    bouton_retour();
}

/* Applications de la plateforme : une rangée chacune, l'aide de celle choisie en dessous */
static void draw_apps(void) {
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(INK), 222);
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    ui_ring(CX, CY, 215, 1, C1, 46);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, "APPLICATIONS");
    ui_text(ui_fit(FONT_MONO, "Toucher deux fois pour lancer", 300, 20, 16), CX, 138, UI_CENTER, ui_rgb(DIM), 255,
            "Toucher deux fois pour lancer");
    /* Le jeu, les applications de la plateforme, puis RÉGLAGES (4 rangées au plus) */
    const int n = napps_menu();
    for (int k = 0; k < n; k++) {
        const float y = 156 + k * 52, dy = y + 46 - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        const char *nom = !k ? (E.w.ouv_titre[0] ? E.w.ouv_titre : "ECHOES OF ASHES") : k > E.cfg.napps ? "RÉGLAGES" : E.cfg.apps[k - 1].nom;
        rangee(CX - hw, y, 2 * hw, 46, k == A.sel, nom, NULL, ui_rgb(WHITE));
        bouton(CX - hw, y, 2 * hw, 46, B_APP, k);
    }
    const char *aide = !A.sel ? "Le jeu" : A.sel > E.cfg.napps ? "Volume, luminosité, veille de la montre"
                     : E.cfg.apps[A.sel - 1].aide ? E.cfg.apps[A.sel - 1].aide : "";
    ui_text(ui_fit(FONT_MONO, aide, 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, aide);
    bouton_retour();
}

/* Réglages de la montre : une rangée par réglage, moitié gauche = un cran de moins, moitié droite = un de plus */
static void draw_regl(void) {
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(INK), 222);
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    ui_ring(CX, CY, 215, 1, C1, 46);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, "RÉGLAGES");
    ui_text(ui_fit(FONT_MONO, "Gauche : moins · droite : plus", 300, 20, 16), CX, 138, UI_CENTER, ui_rgb(DIM), 255,
            "Gauche : moins · droite : plus");
    const int n = E.cfg.nregls < 4 ? E.cfg.nregls : 4;
    for (int k = 0; k < n; k++) {
        const float y = 156 + k * 52, dy = y + 46 - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        const char *v = E.cfg.regls[k].valeur ? E.cfg.regls[k].valeur() : "";
        rangee(CX - hw, y, 2 * hw, 46, k == A.sel, E.cfg.regls[k].nom, v, ui_rgb(WHITE));
        bouton(CX - hw, y, hw, 46, B_REG_MOINS, k);
        bouton(CX, y, hw, 46, B_REG_PLUS, k);
    }
    ui_text(ui_fit(FONT_MONO, "Gardés sur la carte SD", 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, "Gardés sur la carte SD");
    bouton_retour();
}

/* Bandeau du texte d'un carton, centré sur la hauteur y, au-dessus d'un lieu ou d'une image */
static void bandeau_carton(const char *s, float y, float a, float at) {
    char l[6][128];
    const int n = couper(ui_font(FONT_BODY, 24), s, 330, l);
    const float h = n * 28.0f + 28, y0 = y - h / 2;   /* bandeau à la hauteur du texte */
    ui_rect(0, y0, ENG_SCREEN, h, ui_rgb(INK), (int)(170 * a));
    ui_line(40, y0, 426, y0, 2, C2, (int)(255 * a));
    ui_line(40, y0 + h, 426, y0 + h, 2, C2, (int)(255 * a));
    cendres(0.5f * a);
    texte_centre(FONT_BODY, 24, s, 330, y, 28, ui_rgb(WHITE), (int)(255 * at));
}

static void draw_intro(void) {
    const int k = A.carte;
    const float t = A.t - A.ct0, d = carte_duree(k);
    const float a = clampf(fminf(t / T_IN, (d - t) / T_OUT), 0, 1), at = clampf(fminf((t - 0.25f) / T_IN, (d - t) / T_OUT), 0, 1);
    const char *s = E.w.ouv_cartes[k].texte;
    if (E.w.ouv_cartes[k].x < 0 && E.w.ouv_img[k] >= 0) {   /* image (OUVI) : plein écran, bandeau du texte en bas */
        int iw, ih;
        const uint16_t *pal;
        const uint8_t *ix = img_ix(E.w.ouv_img[k], &iw, &ih, &pal, 1);   /* NULL une image, le temps de la lire */
        ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0), 255);
        if (ix) {
            const float sc = (ENG_SCREEN - 2) / (float)iw;
            ui_image_pal(ix, pal, iw, ih, CX - iw * sc / 2, CY - ih * sc / 2, sc, CX, CY, 233);
            ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0), (int)(255 * (1 - a)));
        }
        bandeau_carton(s, 350, a, at);
    } else if (E.w.ouv_cartes[k].x < 0) {   /* noir : braise au bas de l'écran, cendres qui tombent, texte au milieu */
        ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0), 255);
        ui_circle(CX, 560, 250, ui_rgb(0x6A1A08), (int)(70 * a));
        ui_circle(CX, 600, 250, ui_rgb(0xC4442F), (int)(40 * a));
        cendres(a);
        texte_centre(FONT_BODY, 28, s, 340, CY, 36, ui_rgb(WHITE), (int)(255 * at));
    } else {                          /* un lieu : bandeau du texte sous lui */
        vignette();
        ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0), (int)(255 * (1 - a)));
        bandeau_carton(s, 300, a, at);
    }
    char c[64]; snprintf(c, sizeof c, "%d / %d · MAINTENIR : PASSER", k + 1, E.w.ouv_ncartes);
    ui_text(ui_font(FONT_MONO, 18), CX, 444, UI_CENTER, ui_rgb(DIM), 150, c);
}

/* Image de l'héroïne k (cellule de sa marche, direction row, image fr) dans le tampon libre ; 0 : sans planche */
static int her_image(int k, int row, int fr, int *w, int *h, int *pieds) {
    const int p = her_perso(k), base = p >= 0 ? E.w.persos[p].sheet : E.w.psheet;
    if (base < 0) return 0;
    const int sh = anim_sheet(base, ANIM_WALK);
    const sheet_t *s = &E.w.sheets[sh];
    const uint8_t *rm = E.w.remap[p >= 0 ? E.w.persos[p].remap : 0];
    const int fw = s->fw < HER_MAX ? s->fw : HER_MAX, fh = s->fh < HER_MAX ? s->fh : HER_MAX, sw = s->fw * s->frames;
    fr %= s->frames;
    const uint8_t *blk = sheet_block(sh, row, 2);   /* planche lue à la demande : tout de suite */
    if (!blk) return 0;
    hbuf ^= 1;
    for (int y = 0; y < fh; y++) for (int x = 0; x < fw; x++) {
        const uint8_t id = blk[y * sw + fr * s->fw + x];
        const uint8_t *c = E.w.pal[rm[id]];
        HPX[hbuf][y * fw + x] = ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]);
        HMK[hbuf][y * fw + x] = id != 0;
    }
    *w = fw; *h = fh; *pieds = s->fh - s->foot[row & 7];
    return 1;
}
static void draw_heroine(void) {
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(INK), 110);
    vignette();
    cendres(0.4f);
    texte_centre(FONT_BODY, 24, E.w.ouv_question[0] ? E.w.ouv_question : "Qui es-tu ?", 320, 104, 28, ui_rgb(WHITE), 255);
    /* Elle marche sur place en tournant sur elle-même */
    static const uint8_t TOUR[8] = { 0, 7, 6, 5, 4, 3, 2, 1 };   /* S, SO, O, NO, N, NE, E, SE */
    const float ht = A.t - A.ht0;
    int w, h, pieds;
    ui_circle(CX, 250, 92, ui_mix(ui_rgb(INK), C1, 0.18f), 120);
    ui_ellipse_ring(CX, HER_PIEDS, 56, 13, 2, C1, 170);
    if (her_image(A.her, TOUR[(int)(ht / HER_DIR_S) & 7], (int)(ht / 0.13f), &w, &h, &pieds)) {
        const float s = clampf(HER_H / (float)(h > 0 ? h : 1), 1, 5);
        ui_image(HPX[hbuf], HMK[hbuf], w, h, CX - w * s / 2, HER_PIEDS - pieds * s, s, CX, CY, 233);
    }
    /* Flèches de part et d'autre */
    for (int d = -1; d <= 1; d += 2) {
        const float x = CX + d * 168, y = 250;
        float q[12]; ui_hex_path(x, y, 56, 48, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 220); ui_outline(q, 6, 2, C1, 200);
        const float tri[6] = { x + d * 9, y, x - d * 7, y - 11, x - d * 7, y + 11 };
        ui_poly(tri, 3, C1, 255);
        bouton(x - 28, y - 24, 56, 48, d < 0 ? B_PREC : B_SUIV, 0);
    }
    ui_text(ui_fit(FONT_DISP, her_nom(A.her), 260, 30, 20), CX, 354, UI_CENTER, ui_rgb(WHITE), 255, her_nom(A.her));
    char c[32]; snprintf(c, sizeof c, "%d / %d", A.her + 1, E.w.ouv_nher + 1);
    ui_text(ui_font(FONT_MONO, 18), CX, 376, UI_CENTER, C1, 255, c);
    rangee(CX - 95, 390, 190, 42, 1, "CHOISIR", NULL, ui_rgb(WHITE));
    bouton(CX - 95, 390, 190, 42, B_CHOISIR, 0);
}

#if ENG_DYN
/* Plein écran (portrait) : le titre au tiers haut, les entrées en bas, sous le pouce (maquette
   engine/maquettes/interface_portrait.html) */
static void draw_titre_portrait(void) {
    const float W = (float)ui_w, H = (float)ui_h, cx = W / 2, yt = H * 0.27f;
    ui_rect(0, 0, W, H, ui_rgb(INK), 90);
    cendres(0.6f);
    const ui_font_t *f = ui_fit(FONT_DISP, E.w.ouv_titre, W - 60, 40, 22);
    if (E.w.edition == 1) ui_text(ui_fit(FONT_MONO, "ÉDITION ENFANT", 220, 16, 12), cx, yt - 60, UI_CENTER, C1, 255, "ÉDITION ENFANT");
    ui_text(f, cx + 2, yt + 2, UI_CENTER, ui_rgb(INK), 200, E.w.ouv_titre);
    ui_text(f, cx, yt, UI_CENTER, ui_rgb(WHITE), 255, E.w.ouv_titre);
    ui_line(cx - 170, yt + 18, cx + 170, yt + 18, 3, C2, 255);
    if (E.w.ouv_sous[0]) ui_text(ui_fit(FONT_MONO, E.w.ouv_sous, W - 60, 20, 14), cx, yt + 46, UI_CENTER, C1, 255, E.w.ouv_sous);
    static const char *const NOM[5] = { "CONTINUER", "NOUVELLE PARTIE", "CHARGER", "RÉGLAGES", "APPLICATIONS" };
    const float h = 84, gap = 16, w = W - 140, y0 = H - 150 - A.nopts * h - (A.nopts - 1) * gap;
    for (int i = 0; i < A.nopts; i++) {
        const float y = y0 + i * (h + gap);
        rangee(cx - w / 2, y, w, h, i == A.sel, nom_option(NOM, A.opts[i]), NULL, ui_rgb(WHITE));
        bouton(cx - w / 2, y, w, h, B_OPT, i);
    }
    char t[128], d[32];
    const int o = A.opts[A.sel], p = derniere_partie();
    if (o == AC_CONTINUER && p >= 0) { duree_txt(A.info[p].temps, d, sizeof d); snprintf(t, sizeof t, "Partie %d · %s · niv. %d · %s", p + 1, A.info[p].lieu, A.info[p].niveau, d); }
    else if (o == AC_NOUVELLE) snprintf(t, sizeof t, "%s", E.w.ouv_ncartes ? "L'histoire du monde, puis ton héroïne" : "Choisir son héroïne");
    else if (o == AC_REGLAGES) snprintf(t, sizeof t, "%s", E.cfg.reglages_aide ? E.cfg.reglages_aide : "Serveur, édition, monde à jour");
    else if (o == AC_APPS) snprintf(t, sizeof t, "Les autres programmes de la montre");
    else snprintf(t, sizeof t, "Reprendre une des %d parties", PARTIES);
    ui_text(ui_fit(FONT_MONO, t, W - 60, 18, 14), cx, H - 90, UI_CENTER, ui_rgb(DIM), 255, t);
    if (A.t < T_TITRE) ui_rect(0, 0, W, H, ui_rgb(0), (int)(255 * (1 - A.t / T_TITRE)));
}
#endif

void accueil_build(void) {
    nab = 0;
    C1 = ui_rgb(hud_theme_c1()); C2 = ui_rgb(hud_theme_c2());
    const float W = (float)ui_w, H = (float)ui_h;
#if ENG_DYN
    /* Plein écran : le titre refait pour le portrait ; les autres pages dans un cadre de 466 × 466 centré, sur un fond
       qui couvre l'image */
    if (A.page == AP_TITRE) draw_titre_portrait();
    else {
        if (A.page == AP_PARTIES || A.page == AP_APPS || A.page == AP_REGL) ui_rect(0, 0, W, H, ui_rgb(INK), 222);
        else if (A.page == AP_HEROINE) ui_rect(0, 0, W, H, ui_rgb(INK), 110);
        else if (E.w.ouv_cartes[A.carte].x < 0) ui_rect(0, 0, W, H, ui_rgb(0), 255);
        ui_translate((W - ENG_SCREEN) / 2, (H - ENG_SCREEN) / 2);
    }
    if (A.page != AP_TITRE)
#endif
    switch (A.page) {
    case AP_TITRE: draw_titre(); break;
    case AP_PARTIES: draw_parties(); break;
    case AP_INTRO: draw_intro(); break;
    case AP_HEROINE: draw_heroine(); break;
    case AP_APPS: draw_apps(); break;
    case AP_REGL: draw_regl(); break;
    }
    ui_translate(0, 0);
    if (A.fondu) ui_rect(0, 0, W, H, ui_rgb(0), (int)(255 * clampf((A.t - A.ft0) / T_FONDU, 0, 1)));
    if (E.stream_hold && A.page != AP_PARTIES && A.page != AP_APPS && A.page != AP_REGL && !(A.page == AP_INTRO && E.w.ouv_cartes[A.carte].x < 0)) ui_rect(0, 0, W, H, ui_rgb(0), 255);   /* chunks en route : noir */
}

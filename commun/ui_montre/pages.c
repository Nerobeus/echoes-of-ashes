/*
 * Pages de l'interface de la montre (pages.h), reprises de l'app montre : glissé qui suit le doigt, puis animation de
 * 180 ms (ralentie à la fin) dessinée à chaque pas sans limite d'images ; rangée sous le doigt en surbrillance tant
 * qu'il ne bouge pas ; relâchement fiable avec le CST9217 (trame sans doigt, ou plus de trame depuis LEVE_US ; une trame
 * au point du relâchement moins d'ECHO_US après lui est un écho). Pendant un glissé, les pages ne changent pas : chacune
 * est dessinée une fois dans une image à part (PSRAM), puis seulement recopiée décalée à chaque pas (dessiner une page
 * coûtait environ 24 ms par image : glissés saccadés) ; sans PSRAM libre (menu du PC DOS), dessinées à chaque pas.
 */
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board.h"
#include "touch.h"
#include "dessin.h"
#include "theme.h"
#include "pages.h"

static const char *TAG = "pages";
#define ECHO_US 200000
#define LEVE_US 70000
#define APPUI_LONG_US 600000

static const pg_def_t *D;
int pg_page;
int pg_lectures;
int64_t pg_lecture_us;
static int glisse;                        /* page glissée sous le doigt (pixels), 0 au repos */
static int appui_y = -1, appui_xv;        /* doigt posé (sans glissé) : la rangée dessous prend la teinte du choix */
static struct { bool on; int x0, y0, x, y; int64_t t0; } doigt;
static int64_t vu_t, leve_t;              /* dernière trame avec un doigt ; relâchement */
static struct { int trames; int64_t ecart; } geste;   /* relevé du geste en cours (console) */
static int leve_x, leve_y;

static void redessiner(void) { if (D->redessiner) D->redessiner(); }

void pg_init(const pg_def_t *d, int page) { D = d; pg_page = page; glisse = 0; appui_y = -1; doigt.on = false; }
bool pg_doigt(void) { return doigt.on; }
int pg_glisse(void) { return glisse; }
bool pg_sous_doigt(int y, int h) { return appui_y >= y && appui_y < y + h; }
int pg_appui_x(void) { return appui_xv; }
bool pg_appui(void) { return appui_y >= 0; }
void pg_annuler(void) { doigt.on = false; appui_y = -1; glisse = 0; }

static void page_dx(int p, int dx) { d_decalage(dx); D->dessiner_page(p); d_decalage(0); }

/* Images des pages pendant un glissé : 0 la page courante, 1 la voisine qui entre (cache_p : leur numéro, -1 : à
   redessiner) */
static uint16_t *cache[2];
static int cache_p[2] = { -1, -1 };
static bool cache_essaye;
static uint16_t *page_image(int k, int p) {
    if (!cache_essaye) {
        cache_essaye = true;
        for (int i = 0; i < 2; i++) cache[i] = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
        if (!cache[0] || !cache[1]) { heap_caps_free(cache[0]); heap_caps_free(cache[1]); cache[0] = cache[1] = NULL; }
    }
    if (!cache[k]) return NULL;
    if (cache_p[k] != p) {
        uint16_t *ancien = d_cible(cache[k]);
        const int ox = d_decalage_courant();
        d_decalage(0);
        d_effacer(T_INK);
        D->dessiner_page(p);
        d_decalage(ox);
        d_cible(ancien);
        cache_p[k] = p;
    }
    return cache[k];
}

void pg_pages(void) {
    const int voisine = glisse < 0 ? pg_page + 1 : pg_page - 1;
    const bool a_voisine = voisine >= 0 && voisine < D->n;
    uint16_t *a = glisse ? page_image(0, pg_page) : NULL, *b = a && a_voisine ? page_image(1, voisine) : NULL;
    if (a && (b || !a_voisine)) {
        d_decalage(glisse); d_image(a);
        if (b) { d_decalage(glisse < 0 ? glisse + LCD_W : glisse - LCD_W); d_image(b); }
        d_decalage(0);
        return;
    }
    cache_p[0] = cache_p[1] = -1;   /* au repos : les pages peuvent changer, les images sont refaites au glissé suivant */
    page_dx(pg_page, glisse);
    if (glisse < 0 && pg_page + 1 < D->n) page_dx(pg_page + 1, glisse + LCD_W);
    if (glisse > 0 && pg_page > 0) page_dx(pg_page - 1, glisse - LCD_W);
}

/* Fin d'un glissé : la page part jusqu'à cible (±466 : la voisine prend sa place, 0 : elle revient), dessinée à chaque
   pas, sans limite d'images par seconde */
static void glisser_vers(int cible) {
    const int depart = glisse;
    const int64_t t0 = esp_timer_get_time(), duree = 180000;
    for (;;) {
        const int64_t t = esp_timer_get_time() - t0;
        if (t >= duree) break;
        const float k = (float)t / duree, e = 1 - (1 - k) * (1 - k);   /* ralentit à la fin */
        glisse = depart + (int)((cible - depart) * e);
        D->dessiner();
    }
    glisse = 0;
    if (cible) { pg_page += cible < 0 ? 1 : -1; if (D->page_changee) D->page_changee(); }
    redessiner();
}
static bool voisine(int dx) { const int p = pg_page + (dx < 0 ? 1 : -1); return p >= 0 && p < D->n; }

/* Tactile : le CST9217 envoie des trames tant que le doigt est posé ; entre deux, touch_etat rend -1 (rien de nouveau :
   le doigt reste posé). Relâchement : une trame sans doigt, ou LEVE_US sans trame (le contrôleur n'envoie pas toujours
   la trame du lever) */
void pg_tactile(int64_t now) {
    int xy[4];
    const int64_t tl = esp_timer_get_time();
    const int n = touch_etat(xy);
    pg_lectures++; pg_lecture_us += esp_timer_get_time() - tl;
    if (n < 0 && !(doigt.on && now - vu_t > LEVE_US)) return;
    if (n > 0) {
        if (doigt.on && now - vu_t > geste.ecart) geste.ecart = now - vu_t;
        geste.trames++;
        vu_t = now;
        if (!doigt.on) {
            if (now - leve_t < ECHO_US && abs(xy[0] - leve_x) + abs(xy[1] - leve_y) < 24) return;   /* écho */
            doigt.on = true; doigt.x0 = xy[0]; doigt.y0 = xy[1]; doigt.t0 = now;
            geste.trames = 1; geste.ecart = 0;
            if (!D->appui_permis || D->appui_permis()) { appui_y = xy[1]; appui_xv = xy[0]; redessiner(); }
        }
        doigt.x = xy[0]; doigt.y = xy[1];
        if (D->activite) D->activite(now);
        const int dx = doigt.x - doigt.x0, dy = doigt.y - doigt.y0;
        const bool permis = !D->glisse_permis || D->glisse_permis();
        if (permis && D->n > 1 && (glisse || (abs(dx) > 12 && abs(dx) > abs(dy)))) {   /* la page suit le doigt */
            const int g = voisine(dx) ? dx : dx / 3;   /* au bout : résiste */
            if (g != glisse) { glisse = g ? g : 1; redessiner(); }
        }
        if (appui_y >= 0 && (glisse || abs(dx) + abs(dy) >= 30)) { appui_y = -1; redessiner(); }
        return;
    }
    /* relâchement (trame sans doigt, ou plus de trame) */
    if (!doigt.on) return;
    doigt.on = false;
    ESP_LOGI(TAG, "geste : %d trames en %lld ms, écart max %lld ms, levé par %s, dx %d", geste.trames, (now - doigt.t0) / 1000,
             geste.ecart / 1000, n == 0 ? "trame" : "silence", doigt.x - doigt.x0);
    leve_t = now; leve_x = doigt.x; leve_y = doigt.y;
    if (appui_y >= 0) { appui_y = -1; redessiner(); }
    const int dx = doigt.x - doigt.x0, dy = doigt.y - doigt.y0;
    if (glisse) { glisser_vers(abs(dx) > 70 && voisine(dx) ? (dx < 0 ? -LCD_W : LCD_W) : 0); return; }
    if (D->vertical && abs(dy) > 40 && abs(dy) > 2 * abs(dx)) { D->vertical(dy < 0 ? 1 : -1); redessiner(); return; }
    if (abs(dx) + abs(dy) < 30) {
        if (!(now - doigt.t0 > APPUI_LONG_US && D->appui_long && D->appui_long(doigt.x, doigt.y))) D->toucher(doigt.x, doigt.y);
        redessiner();
    }
}

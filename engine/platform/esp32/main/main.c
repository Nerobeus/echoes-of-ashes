/*
 * Echoes of Ashes sur ESP32-S3 (Waveshare AMOLED 1,75", écran rond 466 × 466)
 *
 * Démarrage : alimentation (AXP2101), écran CO5300, tactile, carte SD, puis le paquet monde est copié de sa
 * partition flash vers la PSRAM. Cœur 0 : entrées, jeu, préparation de l'image suivante. Cœur 1 : chaque bande
 * de LCD_BAND lignes natives est dessinée directement dans l'un des deux tampons DMA (3D doublée ou native,
 * interface par-dessus, rognée au cercle) et envoyée à l'écran pendant que la suivante se dessine.
 */
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"
#include "board.h"
#include "board_power.h"
#include "lcd_co5300.h"
#include "touch.h"
#include "audio_esp.h"
#include "sd.h"
#include "sdx.h"
#include "horloge.h"
#include "veille.h"
#include "transfert.h"
#include "apps.h"
#include "reglages.h"
#include "manette.h"
#include "eng.h"

#define SON_RETENU_US 1600000   /* fondu de l'écran titre (T_TITRE de accueil.c) */
#define ECRAN_CALME_IPS 10      /* écran titre que personne ne touche (eng_ecran_calme) : il prenait 12 i/s à plein (01/11/2026) */
static int64_t son_t0;          /* première image présentée (0 : pas encore ; -1 : son rendu) */

static const char *TAG = "echoes";

_Static_assert(ENG_SCREEN == LCD_W && ENG_SCREEN == LCD_H, "le moteur dessine l'écran natif 466 × 466");
_Static_assert(ENG_NATIVE_BAND == LCD_BAND, "bandes du moteur = bandes envoyées à l'écran");

/* SRAM interne laissée après les tampons « rapides » du moteur : pile de la tâche son (4 Ko), fichiers de la carte SD
   ouverts après eng_init, marge. Les piles des tâches jeu et rendu sont réservées d'avance (statiques) : une pile de
   8 Ko qui ne trouvait plus de bloc libre laissait l'écran figé, sans message. */
#define SRAM_RESERVE (12 * 1024)
/* Avec la manette, réserve de plus, que le moteur ne prend pas (avec 11 Ko libres, la carte SD n'avait plus de tampon
   DMA, lectures en échec et rendu cassé). Le Bluetooth est déjà démarré quand le moteur réserve ses tampons ; mesure du
   08/11/2026, balle connectée pendant eng_init puis 50 s de jeu : la SRAM libre n'est jamais descendue plus de 5 Ko sous
   celle du jeu (46 Ko, au plus bas 41). Essai à 16 Ko le même jour : profondeur et bande du cœur 0 en SRAM, 30 Ko libres
   en jeu, mais rien de mesurable (Lucerne 25,2 -> 24,9 i/s, campagne 26,5 -> 27,1, bandes du cœur 0 -2 % : une bande
   tient dans le cache de données) ; on garde la marge */
#define SRAM_RESERVE_BT (32 * 1024)
/* Troisième tampon de bande avec la manette (08/11/2026) : seulement s'il reste ensuite de quoi démarrer le Bluetooth
   (37 Ko), le reste du démarrage (carte SD, tactile : 13 Ko) et les 20 Ko libres avant eng_init qu'avait le jeu avec
   deux tampons. Sans lui, le cœur 1 attend la fin de l'envoi de chaque bande avant la suivante : à Lucerne, 21,3 i/s
   au lieu de 23,8 ; les tableaux froids du moteur passés en PSRAM (ENG_FROID) lui ont fait la place */
#define SRAM_APRES_TROISIEME (72 * 1024)
static StackType_t pile_rendu[8192], pile_jeu[8192];
static StaticTask_t tache_rendu, tache_jeu;

static void *eng_alloc(size_t size, int fast) {
    if (fast && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= size + SRAM_RESERVE + (manette_active() ? SRAM_RESERVE_BT : 0)) {
        void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p) return p;
    }
#if CONFIG_ECHOES_BANC
    /* Banc : grosses réservations en PSRAM (où va la mémoire au démarrage) */
    if (size >= 64 * 1024) ESP_LOGI(TAG, "réserve %u Ko en PSRAM (libre avant : %u Ko)", (unsigned)(size / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#endif
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/* Luminosité demandée par le menu : appliquée au début de l'image suivante, quand plus aucune bande n'est en
   cours d'envoi (une commande synchrone ne doit pas se glisser entre des transactions en file) */
static volatile int bright_req = -1;
static uint8_t lum_reglee = 0xD0;   /* luminosité en place, rendue au réveil de la veille */
static void brightness(int level) {
    level = level > 255 ? 255 : level < 0 ? 0 : level;   /* niveau 5 du jeu : 64 + 4 × 48 = 256 (l'écran devenait noir) */
    bright_req = level; lum_reglee = (uint8_t)level;
    reglages_luminosite_vue(level);
}
void eng_luminosite_montre(int level) { brightness(level); }
static void lum_passagere(int level) { bright_req = level; }   /* veille : atténuation, puis retour à lum_reglee */
static uint32_t clock_us(void) { return (uint32_t)esp_timer_get_time(); }

/* Paquet : partition « monde » en mémoire projetée (elle le reste), lue à travers le cache de la flash ; le moteur
   copie lui-même les textures en PSRAM s'il en reste assez. Les chunks sont lus à la demande de la même façon (une
   lecture par le pilote de flash suspendrait le cache, donc le rendu sur l'autre cœur). Paquet léger (SPRX,
   tools/paquet_sd.js) : la flash n'a que les petites sections ; chunks, planches et textures du sol sont lus dans
   /sd/echoes/monde.pak (sd_pak_read). */
static const uint8_t *pak_map;
static size_t pak_map_len;
static int pak_leger;
static int pak_read(uint32_t off, void *dst, uint32_t len) {
    if ((size_t)off + len > pak_map_len) return -1;
    memcpy(dst, pak_map + off, len);
    return 0;
}
static const uint8_t *load_pak(size_t *len) {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "monde");
    if (!part) { ESP_LOGE(TAG, "partition « monde » absente"); return NULL; }
    const void *map; esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map, &h) != ESP_OK) return NULL;
    const uint8_t *p = map;
    if (memcmp(p, "ECHO", 4)) { ESP_LOGE(TAG, "pas de paquet monde en flash (idf.py flash l'écrit)"); esp_partition_munmap(h); return NULL; }
    size_t o = 8, pre = 0;
    while (o + 8 <= part->size) {
        uint32_t l; memcpy(&l, p + o + 4, 4);
        if (!memcmp(p + o, "CDAT", 4)) pre = o;
        if (!memcmp(p + o, "SPRX", 4)) pak_leger = 1;
        int end = !memcmp(p + o, "END ", 4);
        o += 8 + l;
        if (end) break;
    }
    if (!pre) pre = o;
    pak_map = p; pak_map_len = o;
    ESP_LOGI(TAG, "paquet : %u Ko, dont %u Ko de chunks%s", (unsigned)(o / 1024), (unsigned)((o - pre) / 1024), pak_leger ? " (léger : le reste sur la carte SD)" : "");
    *len = pre;
    return p;
}

/* Plus grande corde du cercle dans les lignes [y, y + h) : colonnes [x0, x1), paires (contrainte du CO5300) */
static void band_window(int y, int h, int *x0, int *x1) {
    float r = LCD_W / 2.0f, best = 0;
    for (int j = y; j < y + h; j++) {
        float dy = j + 0.5f - r, v = r * r - dy * dy;
        if (v > best) best = v;
    }
    float hw = sqrtf(best);
    int a = (int)floorf(r - hw) & ~1, b = ((int)ceilf(r + hw) + 1) & ~1;
    *x0 = a < 0 ? 0 : a; *x1 = b > LCD_W ? LCD_W : b;
}

/* Deux cœurs.
   Cœur 1 (tâche rendu) : dessine et envoie les bandes de l'image présentée.
   Cœur 0 (tâche jeu) : lit les entrées, fait avancer le jeu, prépare l'image N+1 ; puis il prend lui aussi des bandes
   de l'image N tant qu'il en reste, et présente N+1 quand toutes les bandes de N sont dessinées.
   Les bandes se distribuent par un compteur atomique ; « prête » réveille le cœur 1, « dessinée » est donnée par le
   cœur qui termine la dernière bande. Tampons DMA : 0 et 1 pour le cœur 1, 2 pour le cœur 0, réservés avant eng_init
   (il prend ensuite toute la SRAM interne qu'il trouve, et il ne resterait plus trois blocs de 31 Ko). Avec la manette,
   trois aussi s'il reste la SRAM (SRAM_APRES_TROISIEME), sinon deux (31 Ko de SRAM rendus) : 0 pour le cœur 1, qui
   attend la fin de l'envoi de sa bande avant la suivante, 1 pour le cœur 0. */
enum { NB = (LCD_H + LCD_BAND - 1) / LCD_BAND };
static int16_t wx0[NB], wx1[NB];
static SemaphoreHandle_t sem_ready, sem_drawn;
static volatile int next_band = NB, bands_done;
static volatile int64_t c1_busy, c1_idle;          /* cumuls du cœur 1 (µs) */
static volatile int c1_bands;
static uint16_t *band_dma[3];

/* Dessine et envoie la bande b avec le contexte worker et le tampon tag ; la dernière bande de l'image signale « dessinée » */
static void draw_band(int worker, int b, uint16_t *buf, int tag) {
    int y = b * LCD_BAND, h = LCD_H - y < LCD_BAND ? LCD_H - y : LCD_BAND;
    lcd_buffer_wait(tag);
    eng_render_band_on(worker, y, h, buf, wx0[b], wx1[b]);
    lcd_band_send(wx0[b], wx1[b], y, h, buf, tag);
    if (__atomic_add_fetch(&bands_done, 1, __ATOMIC_SEQ_CST) == NB) xSemaphoreGive(sem_drawn);
}

/* Capture de l'écran (SDX ECRAN, engine/tools/capture_montre.py) : l'image présentée, toutes ses bandes dessinées,
   redessinée bande par bande dans le tampon du cœur 0 (le cœur 1 attend l'image suivante). Par bande, une ligne
   « SDX EB y x0 l h », puis l × h pixels bruts (RGB565 tel qu'envoyé à l'écran, octets inversés) ; à la fin, « SDX
   ECRAN FIN ». Écrit directement dans le FIFO de l'USB, journal coupé le temps de l'envoi : par la console (base64,
   ligne à ligne), 14 Ko/s et des lignes du journal mêlées aux pixels. Le jeu s'arrête le temps de l'envoi. */
static void usb_ecrire(const void *d, int n) {
    const uint8_t *p = d;
    int64_t t0 = esp_timer_get_time();
    while (n > 0) {
        if (!usb_serial_jtag_ll_txfifo_writable()) {
            if (esp_timer_get_time() - t0 > 2000000) return;   /* plus personne ne lit */
            continue;
        }
        const int k = (int)usb_serial_jtag_ll_write_txfifo(p, (uint32_t)n);
        usb_serial_jtag_ll_txfifo_flush();
        p += k; n -= k;
        t0 = esp_timer_get_time();
    }
}
static void capture_ecran(uint16_t *buf, int tag) {
    char l[48];
    lcd_wait_all();
    lcd_buffer_wait(tag);
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_NONE);
    vTaskDelay(pdMS_TO_TICKS(30));   /* ce que la console envoyait encore */
    for (int b = 0; b < NB; b++) {
        const int y = b * LCD_BAND, h = LCD_H - y < LCD_BAND ? LCD_H - y : LCD_BAND, w = wx1[b] - wx0[b];
        eng_render_band_on(1, y, h, buf, wx0[b], wx1[b]);
        usb_ecrire(l, snprintf(l, sizeof l, "\nSDX EB %d %d %d %d\n", y, wx0[b], w, h));
        usb_ecrire(buf, w * h * 2);
    }
    usb_ecrire(l, snprintf(l, sizeof l, "\nSDX ECRAN FIN\n"));
    esp_log_level_set("*", ESP_LOG_INFO);
}

static void render_task(void *arg) {
    uint16_t *dma[2] = { band_dma[0], band_dma[1] };
    const int alterner = band_dma[2] != NULL;   /* manette : un seul tampon pour ce cœur */
    int k = 0;
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(sem_ready, portMAX_DELAY);
        int64_t t1 = esp_timer_get_time();
        for (int b; (b = __atomic_fetch_add(&next_band, 1, __ATOMIC_SEQ_CST)) < NB; k ^= alterner) { draw_band(0, b, dma[k], k); c1_bands++; }
        c1_idle += t1 - t0; c1_busy += esp_timer_get_time() - t1;
    }
}

#if CONFIG_ECHOES_BANC
/* Banc d'essai : positions (x, z, lacet), 1,5 s de mise en route puis 2 s de mesure chacune ; puis traversée du
   monde en diagonale à 3,2 cases/s (pas de marche de Sera) sur 136 cases centrées sur le départ, cadence plafonnée,
   chunks chargés au fil de la marche */
static const float BANC[4][3] = { { 24.5f, 24.5f, 0.35f }, { 24.0f, 14.6f, 2.8f }, { 30.0f, 30.0f, 1.2f }, { 40.0f, 40.0f, 2.0f } };
/* Sans scénario : un en-tête vide que script_load refuse (pas de carton ni de dialogue du prologue devant la 3D mesurée) */
static int banc_scen_read(uint32_t off, void *dst, uint32_t len) { memset(dst, 0, len); return 0; }
#endif

/* Poké Ball Plus (manette.c ; RÉGLAGES › MANETTE) : les touches du clavier de la version web, stick = directions (au-delà
   de 38 % de la course), clic du stick = OK (parler, valider, avancer un dialogue), bouton du haut = RETOUR (menu,
   retour) : comme A et B du Game Boy (choix de l'utilisateur) ; l'inverse avec BOUTON OK : HAUT (manette_inverse) */
static uint32_t manette_touches(void) {
    const manette_t m = manette_etat();
    uint32_t k = 0;
    if (m.boutons & MANETTE_OK) k |= ENG_KEY_OK;
    if (m.boutons & MANETTE_RETOUR) k |= ENG_KEY_BACK;
    if (m.x < -38) k |= ENG_KEY_LEFT; else if (m.x > 38) k |= ENG_KEY_RIGHT;
    if (m.y < -38) k |= ENG_KEY_UP; else if (m.y > 38) k |= ENG_KEY_DOWN;
    return k;
}

static void game_task(void *arg) {
    uint16_t *dma = band_dma[2] ? band_dma[2] : band_dma[1];
    gpio_config_t boot = { .mode = GPIO_MODE_INPUT, .pin_bit_mask = 1ULL << CONFIG_ECHOES_BOOT_GPIO, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&boot);
    int64_t last = esp_timer_get_time(), stat_t = last;
    int boot_prev = 0, boot_long = 0;    /* BOOT : appui court = menu au relâchement, appui long = veille (veille.c) */
    time_t heure_t = 0;
    int64_t t_update = 0, t_build = 0, t_help = 0, t_wait = 0;
    int frames = 0, helped = 0, ch_loads = 0;
    uint32_t ch_max = 0;                 /* plus long chargement de chunks dans une image */
    int64_t upd_max = 0, prev_t4 = last;
    uint32_t man_prec = 0;               /* touches de la manette à l'image d'avant, et plus long délai mesuré */
    int64_t man_retard = 0;
#if CONFIG_ECHOES_FPS_MAX > 0
    int64_t present_t = last;
#endif
#if CONFIG_ECHOES_BANC
    int banc = 0; int64_t banc_t = last;
    float banc0[2];
    int64_t b_frames = 0, b_us3d = 0, b_usui = 0, b_build = 0, b_px = 0, b_tris = 0, b_seg = 0, b_ops = 0;
    int64_t tr_t = 0, tr_period = 0; int tr_frames = 0, tr_loads = 0; uint32_t tr_max = 0;
    float tr_x0 = 4.5f, tr_z0 = 4.5f, tr_len = 0;
    /* Position 0 : le départ du monde (en ville sur un continent de l'Atelier V4) */
    eng_skip_opening();
    eng_start_position(&banc0[0], &banc0[1]);
    eng_teleport(banc0[0], banc0[1], BANC[0][2]);
#endif
    for (;;) {
        /* Copie vers la carte SD en cours (sdx.c) : le jeu n'avance plus et ne lit plus la carte */
        if (sdx_pause()) { vTaskDelay(pdMS_TO_TICKS(20)); last = esp_timer_get_time(); continue; }
#ifdef ENG_PROFILE
        /* Essais de mesure (SDX XP n) : 1 = en série (le cœur 0 attend que l'image présentée soit dessinée avant de préparer la
           suivante, et ne dessine pas de bande : le cœur 1 seul, sans rien à côté), 8 = le cœur 0 ne dessine pas de bande
           (préparation toujours en même temps), 2 = textures lues en 2 × 2 texels (render.c, eng_xp) */
        const int xp = sdx_xp(), xp_serie = xp & 1, xp_aide = !(xp & 9);
        eng_xp = xp;
        if (xp_serie) xSemaphoreTake(sem_drawn, portMAX_DELAY);
#else
        const int xp_serie = 0, xp_aide = 1;
#endif
        int64_t t0 = esp_timer_get_time();
        float dt = (t0 - last) / 1e6f; last = t0;
        eng_input_t in = { 0 };
        int txy[4];
        int nf = touch_read(txy);
        if (nf >= 1) { in.touch = 1; in.touch_x = (float)txy[0]; in.touch_y = (float)txy[1]; }
        if (nf >= 2) { in.touch2 = 1; in.touch2_x = (float)txy[2]; in.touch2_y = (float)txy[3]; }
        const int boot = !gpio_get_level(CONFIG_ECHOES_BOOT_GPIO);
        {   /* mesure : délai entre la trame de la balle qui change les touches et l'image qui les voit */
            const uint32_t mk = manette_touches();
            if (mk != man_prec) { const int64_t r = esp_timer_get_time() - manette_vue(); if (r > man_retard) man_retard = r; man_prec = mk; }
            in.keys |= mk;
        }
        if (manette_connectee()) { const manette_t m = manette_etat(); in.manette = 1; in.stick_x = m.x / 100.0f; in.stick_y = m.y / 100.0f; manette_tick(); }
        /* Veille : ne revient qu'au réveil (moteur verrouillé) ; le temps passé en veille ne compte pas */
        if (veille_image(boot, nf > 0 || in.keys || eng_essai_actif())) {   /* un essai de fluidité compte comme un joueur */ last = esp_timer_get_time(); boot_prev = boot_long = 0; continue; }
        if (boot) boot_long |= veille_boot_long();
        else { if (boot_prev && !boot_long) in.keys |= ENG_KEY_MENU; boot_long = 0; }
        boot_prev = boot;
        /* Heure et date de l'horloge PCF85063, une fois par seconde */
        if (horloge_valide()) {
            const time_t now = time(NULL);
            if (now != heure_t) {
                struct tm t;
                heure_t = now; localtime_r(&now, &t);
                eng_set_horloge(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
                eng_set_date(t.tm_mday, t.tm_mon + 1, t.tm_wday);
            }
        }
        {   /* course d'essai demandée par le PC (SDX COURSE) */
            int k, a;
            if (sdx_course(&k, &a)) { const int ok = eng_essai_course(k, a); printf(ok ? "SDX COURSE OK %d %d\n" : "SDX ERR course %d %d\n", k, a); fflush(stdout); }
            float ev[6];
            if (sdx_essai(ev)) { const int ok = eng_essai_monde(ev[0], ev[1], ev[2], (int)ev[3], ev[4], ev[5]); printf(ok ? "SDX ESSAI OK\n" : "SDX ERR essai refusé\n"); fflush(stdout); }
        }
        eng_update(dt, &in);
        int64_t t1 = esp_timer_get_time();
        eng_build_frame(t0 / 1e6f);
        int64_t t2 = esp_timer_get_time();
        /* Aide : bandes restantes de l'image présentée */
        if (xp_aide) for (int b; (b = __atomic_fetch_add(&next_band, 1, __ATOMIC_SEQ_CST)) < NB;) { draw_band(1, b, dma, 2); helped++; }
        int64_t t3 = esp_timer_get_time();
        if (!xp_serie) xSemaphoreTake(sem_drawn, portMAX_DELAY);   /* toutes les bandes de l'image présentée sont dessinées */
        if (sdx_ecran()) { capture_ecran(dma, 2); prev_t4 = stat_t = esp_timer_get_time(); frames = 0; t_update = t_build = t_help = t_wait = 0; c1_busy = c1_idle = 0; c1_bands = 0; }
        int64_t t4 = esp_timer_get_time();
        t_update += t1 - t0; t_build += t2 - t1; t_help += t3 - t2; t_wait += t4 - t3; frames++;
        {
            const eng_stats_t *s = eng_stats();
            ch_loads += s->chunk_loads;
            if (s->us_stream > ch_max) ch_max = s->us_stream;
            if (t1 - t0 > upd_max) upd_max = t1 - t0;
        }
        const int64_t period = t4 - prev_t4;
        prev_t4 = t4;
        (void)period;
#if CONFIG_ECHOES_BANC
        if (banc < 4) {
            int64_t el = t4 - banc_t;
            if (el > 1500000) {   /* mesure (l'image présentée vient de cette position) */
                if (!b_frames) { eng_profile(NULL); eng_profile_build(NULL); }   /* profilage : depuis le début de la mesure */
                const eng_stats_t *s = eng_stats();
                b_frames++; b_us3d += s->us_3d; b_usui += s->us_ui; b_build += t2 - t1; b_px += s->px; b_tris += s->tris; b_seg += s->segments; b_ops += s->ui_ops;
            }
            if (el > 3500000) {
                float n = (float)b_frames;
                ESP_LOGI(TAG, "BANC %d : %.1f i/s, 3D %.2f ms, interface %.2f ms, préparation %.2f ms, %.0f pixels, %.0f triangles, %.0f segments, %.0f op. interface",
                         banc, n * 1e6f / 2000000.0f, b_us3d / 1000.0f / n, b_usui / 1000.0f / n, b_build / 1000.0f / n, b_px / n, b_tris / n, b_seg / n, b_ops / n);
                uint32_t pf[ENG_PROF_N];
                eng_profile(pf);
                ESP_LOGI(TAG, "BANC %d profil (ms par image, deux cœurs) : effacement %.2f, préparation des triangles %.2f (%.0f appels), lignes %.2f (%.0f) dont pixels %.2f (%.0f plages), sprites %.2f, ombres %.2f, ciel %.2f",
                         banc, pf[PROF_CLEAR] / 240e3f / n, pf[PROF_SETUP] / 240e3f / n, pf[PROF_TRIS] / n, pf[PROF_ROWS] / 240e3f / n, pf[PROF_NROWS] / n, pf[PROF_PIX] / 240e3f / n, pf[PROF_NRUNS] / n,
                         pf[PROF_SPRITE] / 240e3f / n, pf[PROF_SHADOW] / 240e3f / n, pf[PROF_SKY] / 240e3f / n);
                uint32_t bp[BPROF_N];
                eng_profile_build(bp);
                ESP_LOGI(TAG, "BANC %d préparation (ms par image) : début %.2f, blocs %.2f, cases %.2f, chunks %.2f, personnages %.2f, tri %.2f, ciel %.2f, textures du sol %.2f, interface %.2f",
                         banc, bp[BPROF_DEBUT] / 240e3f / n, bp[BPROF_BLOCS] / 240e3f / n, bp[BPROF_CASES] / 240e3f / n, bp[BPROF_CHUNKS] / 240e3f / n,
                         bp[BPROF_PERSOS] / 240e3f / n, bp[BPROF_TRI] / 240e3f / n, bp[BPROF_CIEL] / 240e3f / n, bp[BPROF_SOL] / 240e3f / n, bp[BPROF_UI] / 240e3f / n);
                b_frames = b_us3d = b_usui = b_build = b_px = b_tris = b_seg = b_ops = 0;
                banc++; banc_t = t4;
                if (banc < 4) eng_teleport(BANC[banc][0], BANC[banc][1], BANC[banc][2]);
                else {
                    /* Diagonale de 136 cases passant par le départ, ramenée dans le monde */
                    float sx, sz, lo = 4.5f, hi = eng_world_size() - 8.0f;
                    eng_start_position(&sx, &sz);
                    tr_x0 = fminf(fmaxf(sx - 48, lo), hi); tr_z0 = fminf(fmaxf(sz - 48, lo), hi);
                    tr_len = fminf(136.0f * 0.7071f, hi - fmaxf(tr_x0, tr_z0));
                    eng_teleport(tr_x0, tr_z0, 0.785f); tr_t = t4;
                }
            }
        } else if (banc == 4) {
            const float d = (t4 - tr_t) / 1e6f * 3.2f * 0.7071f;
            if (d > tr_len || t4 - tr_t > 60000000) {
                ESP_LOGI(TAG, "BANC traversée : %.1f s, %.1f i/s, %d chunks chargés, chargement au plus %.2f ms par image, image la plus longue %.1f ms",
                         (t4 - tr_t) / 1e6f, tr_frames * 1e6f / (t4 - tr_t), tr_loads, tr_max / 1000.0f, tr_period / 1000.0f);
                banc++;
                eng_teleport(banc0[0], banc0[1], BANC[0][2]);
            } else {
                const eng_stats_t *s = eng_stats();
                eng_move_to(tr_x0 + d, tr_z0 + d);
                if (tr_frames++ > 0 && period > tr_period) tr_period = period;
                tr_loads += s->chunk_loads;
                if (s->us_stream > tr_max) tr_max = s->us_stream;
            }
        }
#endif

#if CONFIG_ECHOES_BANC
        const int quiet = banc == 4;   /* traversée : pas de console, qui retarderait les images mesurées */
#else
        const int quiet = 0;
#endif
        if (!quiet && t4 - stat_t > 2000000) {
            const eng_stats_t *s = eng_stats();
            float n = (float)frames;
            ESP_LOGI(TAG, "cœur 0 : jeu %.1f ms, préparation %.1f ms, bandes %.1f ms (%.1f par image), attente %.1f ms",
                     t_update / 1000.0f / n, t_build / 1000.0f / n, t_help / 1000.0f / n, helped / n, t_wait / 1000.0f / n);
            ESP_LOGI(TAG, "cœur 1 : bandes %.1f ms (%.1f par image), attente %.1f ms | 3D %.1f ms, interface %.1f ms (deux cœurs)",
                     c1_busy / 1000.0f / n, c1_bands / n, c1_idle / 1000.0f / n, s->us_3d / 1000.0f, s->us_ui / 1000.0f);
            ESP_LOGI(TAG, "%.1f i/s | %d triangles (sol %d, routes %d, bâtiments %d, plantes %d ; écartés %d), %d pixels 3D, %d op. interface | PSRAM libre %u Ko, SRAM libre %u Ko (au plus bas %u)",
                     n * 1e6f / (t4 - stat_t), s->tris, s->cat_tris[0], s->cat_tris[3], s->cat_tris[4], s->cat_tris[2], s->tris_fogged + s->tris_outside,
                     s->px, s->ui_ops, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));   /* au plus bas : depuis le démarrage (réserve du Bluetooth, carte SD) */
            ESP_LOGI(TAG, "chunks : %d chargés, au plus %.2f ms dans une image ; jeu au plus %.1f ms", ch_loads, ch_max / 1000.0f, upd_max / 1000.0f);
#if defined(ENG_PROFILE) && !CONFIG_ECHOES_BANC
            {   /* profilage en jeu (-DECHOES_PROFIL=1) : postes du rendu des bandes et de la préparation, par image */
                uint32_t pf[ENG_PROF_N], bp[BPROF_N];
                eng_profile(pf);
                eng_profile_build(bp);
                ESP_LOGI(TAG, "profil (ms par image, deux cœurs) : effacement %.2f, préparation des triangles %.2f (%.0f appels), lignes %.2f (%.0f) dont pixels %.2f (%.0f plages), sprites %.2f, ombres %.2f, ciel %.2f",
                         pf[PROF_CLEAR] / 240e3f / n, pf[PROF_SETUP] / 240e3f / n, pf[PROF_TRIS] / n, pf[PROF_ROWS] / 240e3f / n, pf[PROF_NROWS] / n, pf[PROF_PIX] / 240e3f / n, pf[PROF_NRUNS] / n,
                         pf[PROF_SPRITE] / 240e3f / n, pf[PROF_SHADOW] / 240e3f / n, pf[PROF_SKY] / 240e3f / n);
                ESP_LOGI(TAG, "préparation (ms par image) : début %.2f, blocs %.2f, cases %.2f, chunks %.2f, personnages %.2f, tri %.2f, ciel %.2f, textures du sol %.2f, interface %.2f",
                         bp[BPROF_DEBUT] / 240e3f / n, bp[BPROF_BLOCS] / 240e3f / n, bp[BPROF_CASES] / 240e3f / n, bp[BPROF_CHUNKS] / 240e3f / n,
                         bp[BPROF_PERSOS] / 240e3f / n, bp[BPROF_TRI] / 240e3f / n, bp[BPROF_CIEL] / 240e3f / n, bp[BPROF_SOL] / 240e3f / n, bp[BPROF_UI] / 240e3f / n);
                uint32_t up[UPROF_N];
                eng_profile_update(up);
                ESP_LOGI(TAG, "jeu (ms par image) : début %.2f, interface %.2f, course %.2f, personnages %.2f, chunks %.2f, scénario %.2f, trafic %.2f, fin %.2f",
                         up[UPROF_DEBUT] / 240e3f / n, up[UPROF_HUD] / 240e3f / n, up[UPROF_COURSE] / 240e3f / n, up[UPROF_PNJ] / 240e3f / n,
                         up[UPROF_CHUNKS] / 240e3f / n, up[UPROF_SCRIPT] / 240e3f / n, up[UPROF_TRAFIC] / 240e3f / n, up[UPROF_FIN] / 240e3f / n);
            }
#endif
            if (manette_connectee()) {
                int mt, me;
                manette_mesure(&mt, &me);
                ESP_LOGI(TAG, "manette : %d trames en 2 s, écart max %d ms ; touches vues au plus %d ms après leur trame", mt, me, (int)(man_retard / 1000));
                man_retard = 0;
            }
            ch_loads = 0; ch_max = 0; upd_max = 0;
            stat_t = t4; frames = helped = 0; t_update = t_build = t_help = t_wait = 0;
            c1_busy = c1_idle = 0; c1_bands = 0;
        }
        /* Luminosité : commande synchrone, une fois tous les envois terminés (le cœur 1 attend « prête ») */
        if (bright_req >= 0) { lcd_wait_all(); lcd_set_brightness((uint8_t)bright_req); bright_req = -1; }
#if CONFIG_ECHOES_FPS_MAX > 0
        /* Cadence plafonnée : l'image est présentée au plus tôt une période après la précédente (attente en ticks
           de 1 ms, puis le reste en boucle). En retard, on repart de maintenant. */
        {
            /* Cadran verrouillé : 20 images par seconde (le glissement qui déverrouille doit suivre le doigt) */
            /* écran titre immobile : ECRAN_CALME_IPS (batterie ; la caméra et les cendres y bougent lentement) */
            int64_t next = present_t + 1000000 / (eng_verrouille() ? 20 : eng_ecran_calme() ? ECRAN_CALME_IPS : CONFIG_ECHOES_FPS_MAX), now = esp_timer_get_time();
#if CONFIG_ECHOES_BANC
            if (banc < 4) next = now;
#endif
            if (next - now > 1000) vTaskDelay(pdMS_TO_TICKS((next - now) / 1000));
            while (esp_timer_get_time() < next) {}
            present_t = next > now ? next : now;
        }
#endif
        eng_present();
        /* Son du démarrage : volume de menuconfig une fois l'écran titre apparu (première image, puis son fondu) */
        if (!son_t0) { son_t0 = esp_timer_get_time(); lcd_wait_all(); lcd_reveiller(); }   /* écran allumé à coup sûr */
        else if (son_t0 > 0 && esp_timer_get_time() - son_t0 >= SON_RETENU_US) { eng_set_volume(reglages_volume() / 100.0f); son_t0 = -1; }
        else if (son_t0 < 0) reglages_suivre(esp_timer_get_time());   /* réglages de la montre gardés sur la carte */
        __atomic_store_n(&bands_done, 0, __ATOMIC_SEQ_CST);
        __atomic_store_n(&next_band, 0, __ATOMIC_SEQ_CST);
        xSemaphoreGive(sem_ready);
    }
}

/* Veille : l'image en cours de dessin achevée et envoyée, plus rien dans la file de l'écran */
static void finir_image(void) {
    xSemaphoreTake(sem_drawn, portMAX_DELAY);
    lcd_wait_all();
    xSemaphoreGive(sem_drawn);
}

/* Entrée APPLICATIONS de l'écran titre : le PC DOS (s'il est dans sa partition) et le mode Transfert */
static void lancer_pcdos(void) { app_lancer("pcdos"); }
static void lancer_montre(void) { app_lancer("montre"); }
static eng_app_t apps[2];
static int napps;

/* Le jeu ne peut pas démarrer (paquet absent, monde de la carte SD qui ne va pas avec le paquet léger, mémoire) : la
   copie par l'USB (sdx) reste possible 10 s sans transfert, puis retour à l'accueil de la montre (avant, écran noir sans
   issue jusqu'à ce que le PC rende la main à l'accueil) */
static void echec_demarrage(void) {
    for (int t = 0; t < 100; t++) { if (sdx_pause()) t = 0; vTaskDelay(pdMS_TO_TICKS(100)); }
    ESP_LOGW(TAG, "retour à l'accueil de la montre");   /* (app_lancer vérifie qu'elle est là) */
    horloge_garder();
    app_lancer("montre");
}
/* Mémoire interne qui manque au jeu alors que la manette tient celle du Bluetooth : manette coupée (réglage), puis
   redémarrage dans le jeu, sans elle (avant : retour à la montre à chaque lancement) */
static void echec_memoire(void) {
    if (manette_active()) {
        ESP_LOGE(TAG, "mémoire interne insuffisante avec la manette (SRAM libre %u Ko, plus grand bloc %u Ko) : manette coupée, redémarrage",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
        reglages_sans_manette();
        horloge_garder();
        esp_restart();
    }
    echec_demarrage();
}

void app_main(void) {
    board_power_init();
    horloge_init();
    /* Mise sous tension (PWR) : l'AXP2101 vient d'allumer les régulateurs de l'écran ; sans attente, l'écran ratait son
       initialisation (jeu lancé et musique, écran éteint) */
    if (esp_reset_reason() == ESP_RST_POWERON) vTaskDelay(pdMS_TO_TICKS(150));
    lcd_init();
    /* Mode Transfert (TRANSFERT de l'écran titre) : Wi-Fi, heure et fichiers hors du jeu, puis retour au jeu */
    if (transfert_demande()) { transfert_demarrer(); return; }
    /* En jeu, pas de Bluetooth, sauf la manette (Poké Ball Plus, RÉGLAGES › MANETTE) : sans elle, sa mémoire statique
       (SRAM interne) est rendue au tas jusqu'au prochain démarrage. Les trois tampons de bande (31 Ko d'un seul tenant
       chacun, en SRAM pour le DMA) sont réservés avant le Bluetooth, dont le contrôleur morcelle la SRAM (sinon :
       « tampons de bande impossibles à réserver ») ; avec la manette, le troisième s'il reste la SRAM */
    const bool manette = reglages_manette();
    if (!manette) esp_bt_mem_release(ESP_BT_MODE_BTDM);
    for (int k = 0; k < 2; k++)
        if (!(band_dma[k] = heap_caps_malloc(LCD_W * LCD_BAND * 2, MALLOC_CAP_DMA))) { ESP_LOGE(TAG, "tampons de bande impossibles à réserver"); echec_demarrage(); return; }
    if (!manette || (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= LCD_W * LCD_BAND * 2 + SRAM_APRES_TROISIEME
                     && heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >= LCD_W * LCD_BAND * 2)) {
        if (!(band_dma[2] = heap_caps_malloc(LCD_W * LCD_BAND * 2, MALLOC_CAP_DMA)) && !manette) { ESP_LOGE(TAG, "tampons de bande impossibles à réserver"); echec_demarrage(); return; }
    }
    if (manette) ESP_LOGI(TAG, "manette : %d tampons de bande, %u Ko de SRAM libres avant le Bluetooth", band_dma[2] ? 3 : 2, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    if (manette) {
        const unsigned avant = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (manette_demarrer())
            ESP_LOGI(TAG, "manette : %u Ko de SRAM pris par le Bluetooth, %u Ko libres", (avant - (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
    anneau_demarrage(0.1f);
    touch_init();
    /* Copie par l'USB (engine/tools/envoi_sd.py) : fichiers reçus mis en place avant toute lecture, puis réception
       possible même si le jeu ne démarre pas (carte vide, paquet absent) */
    sdx_start(sd_init());
    reglages_lire();
    brightness(reglages_luminosite());   /* réglage de la montre, appliqué à la première image */

    size_t len = 0;
    anneau_demarrage(0.4f);
    const uint8_t *pak = load_pak(&len);
    if (!pak) { echec_demarrage(); return; }
    anneau_demarrage(0.7f);
    anneau_demarrage_fin();
    audio_esp_init();
    /* Accueil de la montre (app « montre ») : en tête ; le PC DOS se lance depuis lui (ici seulement sans lui) */
    const int montre = app_presente("montre");
    if (montre) apps[napps++] = (eng_app_t){ "MONTRE", "Cadran, notifications, applications", lancer_montre };
    else if (app_presente("pcdos")) apps[napps++] = (eng_app_t){ "PC DOS", "MS-DOS 6 : jeux, souris au doigt", lancer_pcdos };
    apps[napps++] = (eng_app_t){ "TRANSFERT", "Wi-Fi : heure et monde à jour", transfert_lancer };
    eng_config_t cfg = { .swap565 = 1, .alloc = eng_alloc, .save_write = sd_save_write, .save_read = sd_save_read, .set_brightness = brightness, .clock_us = clock_us, .pak_read = pak_read,
                         .apps = apps, .napps = napps, .regls = reglages_montre, .nregls = reglages_nombre,
                         .regls_jeu = reglages_jeu, .nregls_jeu = reglages_jeu_nombre };
    if (montre) cfg.montre = lancer_montre;
    _Static_assert((int)MANETTE_EFFETS == (int)ENG_EFFET_N && (int)MANETTE_DEFAITE == (int)ENG_EFFET_DEFAITE, "effets de la manette : mêmes numéros que le moteur");
    if (manette) { cfg.effet_manette = manette_effet; manette_ambiance(true); }   /* la Poké Ball Plus s'allume et vibre selon le jeu (mêmes numéros), cyan entre deux effets */
    /* Mise en veille passée par l'app montre (veille.c) : la partie sauvegardée reprend aussitôt, sans l'écran titre */
    if (montre) cfg.reprendre = app_drapeau("reprise");
    /* Paquet léger : le monde entier sur la carte SD */
    if (pak_leger) {
        if (!(cfg.pak_size = sd_pak_open())) { ESP_LOGE(TAG, "paquet léger en flash, mais pas de " SD_DIR "/monde.pak sur la carte SD"); echec_demarrage(); return; }
        cfg.pak_read = sd_pak_read;
    }
    /* Histoire sur la carte SD si elle y est (sinon la section SCEN du paquet en flash) */
    cfg.scen_size = sd_scen_open();
    if (cfg.scen_size) cfg.scen_read = sd_scen_read;
    else ESP_LOGI(TAG, "histoire : section SCEN du paquet (pas de " SD_DIR "/histoire.scn)");
#if CONFIG_ECHOES_BANC
    cfg.scen_size = 4096; cfg.scen_read = banc_scen_read;
    ESP_LOGI(TAG, "banc d'essai : sans scénario");
#endif
    /* Intérieurs sur la carte SD si elle les a (sinon la section INTR du paquet en flash) */
    cfg.intr_size = sd_intr_open();
    if (cfg.intr_size) cfg.intr_read = sd_intr_read;
    else ESP_LOGI(TAG, "intérieurs : section INTR du paquet (pas de " SD_DIR "/interieurs.bin)");
    ESP_LOGI(TAG, "avant eng_init : SRAM libre %u Ko (plus grand bloc %u Ko), PSRAM libre %u Ko", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    if (eng_init(pak, len, &cfg)) {
        ESP_LOGE(TAG, "eng_init : %s (PSRAM libre %u Ko, plus grand bloc %u Ko)", eng_error(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
        echec_memoire();
        return;
    }
#if CONFIG_ECHOES_AUDIO
    eng_set_volume(0);   /* son retenu jusqu'à l'écran titre visible (boucle du jeu : SON_RETENU_US après la première image) */
#endif
    {   /* musiques sur la carte SD (sinon le jeu reste sans musique) */
        const uint32_t ms = sd_music_open();
        if (ms) eng_music_attach(ms, sd_music_read);
        else ESP_LOGI(TAG, "pas de " SD_DIR "/musique.bin : pas de musique");
    }
    {   /* tables chaudes du rendu en SRAM interne (render.c, eng_profile_mem) ; cœur 1 = fil 0, cœur 0 = fil 1 */
        const int pm = eng_profile_mem();
        ESP_LOGI(TAG, "mémoire rapide : table des couleurs %s ; cœur 1 : masque %s, profondeur %s, bande %s ; cœur 0 : masque %s, profondeur %s, bande %s ; listes %s ; SRAM libre %u Ko",
                 pm & 1 ? "oui" : "non", pm & 2 ? "oui" : "non", pm & 8 ? "oui" : "non", pm & 4 ? "oui" : "non", pm & 16 ? "oui" : "non", pm & 64 ? "oui" : "non",
                 pm & 32 ? "oui" : "non", pm & 128 ? "oui" : "non", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
    ESP_LOGI(TAG, "monde %d x %d chargé, 3D interne %d x %d, textures %s, PSRAM libre %u Ko", eng_world_size(), eng_world_size(), ENG_W, ENG_H_PX,
             pak_leger ? "du sol lues sur la carte SD" : eng_textures_in_ram() ? "en PSRAM" : "lues en flash", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    sem_ready = xSemaphoreCreateBinary(); sem_drawn = xSemaphoreCreateBinary();
    xSemaphoreGive(sem_drawn);   /* aucune image en cours de dessin au départ */
    veille_init(finir_image, &lum_reglee, lum_passagere);
    eng_set_batterie(alim_batterie_pct(), alim_usb());
    /* Fenêtre de chaque bande, et part de l'écran réellement envoyée */
    int sent = 0;
    for (int b = 0; b < NB; b++) {
        int y = b * LCD_BAND, h = LCD_H - y < LCD_BAND ? LCD_H - y : LCD_BAND, a, c;
        band_window(y, h, &a, &c); wx0[b] = (int16_t)a; wx1[b] = (int16_t)c; sent += (c - a) * h;
    }
    ESP_LOGI(TAG, "bandes rognées au cercle : %d pixels envoyés par image au lieu de %d (%d %%)", sent, LCD_W * LCD_H, sent * 100 / (LCD_W * LCD_H));
    if (!xTaskCreateStaticPinnedToCore(render_task, "rendu", sizeof pile_rendu, NULL, 5, pile_rendu, &tache_rendu, 1)
        || !xTaskCreateStaticPinnedToCore(game_task, "jeu", sizeof pile_jeu, NULL, 5, pile_jeu, &tache_jeu, 0))
        ESP_LOGE(TAG, "tâches jeu et rendu impossibles à créer");
    audio_esp_start();
}

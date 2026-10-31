/*
 * Plateforme navigateur (WebAssembly, cible wasm32-wasi en mode « reactor ») : la page JavaScript copie le
 * paquet dans la mémoire du module, appelle web_frame à chaque image et affiche le tampon RGBA 466 × 466.
 * La sauvegarde passe par deux fonctions de la page (stockage local du navigateur). Les musiques (musique.bin, que la
 * page cherche à côté d'elle) sont copiées en mémoire et lues de là comme sur la carte SD.
 */
#include <stdlib.h>
#include <string.h>
#include "../../core/eng.h"

#define EXPORT(name) __attribute__((export_name(name)))
#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define S ENG_SCREEN

IMPORT("js_save_write") int js_save_write(int slot, const void *data, int len);
IMPORT("js_save_read") int js_save_read(int slot, void *data, int cap);

static uint32_t *fb;
static uint16_t band[S * ENG_NATIVE_BAND];

static void *alloc_web(size_t size, int fast) { (void)fast; return malloc(size); }

EXPORT("web_alloc") void *web_alloc(int size) { return malloc((size_t)size); }
EXPORT("web_fb") uint32_t *web_fb(void) { return fb; }
EXPORT("web_internal") int web_internal(void) { return ENG_W; }

/* Son : la page tire n échantillons (au plus 4096) à la fréquence de son AudioContext */
static int16_t audio_buf[4096];
EXPORT("web_audio") int16_t *web_audio(int n, int rate) { eng_audio_mix(audio_buf, n > 4096 ? 4096 : n, rate); return audio_buf; }

/* Musiques : le fichier entier en mémoire (web_alloc), après web_init */
static const uint8_t *mus;
static uint32_t mus_n;
static int mus_read_web(uint32_t off, void *dst, uint32_t len) {
    if ((uint64_t)off + len > mus_n) return -1;
    memcpy(dst, mus + off, len);
    return 0;
}
EXPORT("web_music") void web_music(const uint8_t *p, int len) { mus = p; mus_n = (uint32_t)len; eng_music_attach(mus_n, mus_read_web); }

EXPORT("web_init") int web_init(const uint8_t *pak, int len) {
    fb = malloc(sizeof(uint32_t) * S * S);
    if (!fb) return -1;
    eng_config_t cfg = { 0, alloc_web, js_save_write, js_save_read, NULL, NULL, NULL };
    return eng_init(pak, (size_t)len, &cfg);
}

/* Une image : jeu, puis bandes de l'écran natif (3D et interface), converties en RGBA pour le canevas */
EXPORT("web_frame") void web_frame(float dt, float time_s, int keys, int touch, float tx, float ty, int touch2, float tx2, float ty2) {
    eng_input_t in = { (uint32_t)keys, touch, tx, ty, touch2, tx2, ty2 };
    eng_update(dt, &in);
    eng_build_frame(time_s);
    eng_present();
    for (int y = 0; y < S; y += ENG_NATIVE_BAND) {
        int h = S - y < ENG_NATIVE_BAND ? S - y : ENG_NATIVE_BAND;
        eng_render_native_band(y, h, band, 0, ENG_SCREEN);
        uint32_t *o = fb + y * S;
        for (int i = 0; i < S * h; i++) {
            uint16_t c = band[i];
            uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            o[i] = 0xFF000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((r << 3) | (r >> 2));
        }
    }
}

/* Simulation de la montre : heure, date et batterie du navigateur (comme l'horloge PCF85063 et l'AXP2101 de la carte),
   mode verrouillé au réveil de la veille */
EXPORT("web_montre") void web_montre(int secondes, int jour, int mois, int jsem, int batt, int charge) {
    eng_set_horloge(secondes); eng_set_date(jour, mois, jsem); eng_set_batterie(batt, charge);
}
EXPORT("web_verrouiller") void web_verrouiller(void) { eng_verrouiller(); }
EXPORT("web_verrouille") int web_verrouille(void) { return eng_verrouille(); }

EXPORT("web_set_zoom") void web_set_zoom(float z) { eng_set_zoom(z); }
EXPORT("web_zoom") float web_zoom(void) { return eng_zoom(); }

EXPORT("web_stat") int web_stat(int k) {
    const eng_stats_t *s = eng_stats();
    switch (k) { case 0: return s->tris; case 1: return s->px; case 2: return s->sprites; case 3: return s->segments; case 5: return s->ui_ops; default: return s->prims; }
}

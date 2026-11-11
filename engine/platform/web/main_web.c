/*
 * Plateforme navigateur (WebAssembly, cible wasm32-wasi en mode « reactor ») : la page JavaScript copie le
 * paquet dans la mémoire du module, appelle web_frame à chaque image et affiche le tampon RGBA 466 × 466.
 * La sauvegarde passe par deux fonctions de la page (stockage local du navigateur). Les musiques (musique.bin, que la
 * page cherche à côté d'elle) sont copiées en mémoire et lues de là comme sur la carte SD.
 * Écran rectangulaire (ENG_RECT, 11/11/2026, demande de l'utilisateur : « une version plein écran rectangle pour PC avec
 * commande clavier personnalisable » ; tools/build_web.js --pc) : tampon RGBA de ENG_W × ENG_H_PX (3D et interface, comme
 * sur la LDK : eng_render_band_rect), commandes de la page en actions (web_frame_pc, la croix comme le stick d'une manette).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../core/eng.h"

#define EXPORT(name) __attribute__((export_name(name)))
#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define S ENG_SCREEN
#if ENG_RECT
#include "../../core/ui.h"   /* ui_ox, ui_oy : origine du cadre de l'interface dans l'image (clics de la souris) */
#endif

IMPORT("js_save_write") int js_save_write(int slot, const void *data, int len);
IMPORT("js_save_read") int js_save_read(int slot, void *data, int cap);

static uint32_t *fb;
#if ENG_RECT
static uint16_t band[ENG_W * ENG_BAND];
static int iw = ENG_W, ih = ENG_H_PX;   /* définition de l'image (version premium : choisie par la page, web_definition) */
#if ENG_PREMIUM
/* Flou de profondeur (comme l'app Android, platform/android/main_android.c) : l'image 3D entière et sa profondeur (i3d, idep),
   sommes horizontales (ih3), image floutée (iflou) où l'interface se pose ensuite */
static uint16_t *i3d, *iflou, *i2, *b2;
static uint8_t *idep;
static uint32_t *ih3, *icol;
static int flou_on = 1;
static uint8_t flou_lut[256];
#endif
#else
static uint16_t band[S * ENG_NATIVE_BAND];
#endif

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

/* joystick : 1 pour les commandes de conduite d'un seul doigt au départ (page pour PC : la souris n'a qu'un pointeur ;
   eng_config_t.joystick) ; la page de la montre ne le passe pas (0) */
EXPORT("web_init") int web_init(const uint8_t *pak, int len, int joystick) {
#if ENG_RECT
    fb = malloc(sizeof(uint32_t) * ENG_W * ENG_H_PX);
#if ENG_PREMIUM
    const size_t n = (size_t)ENG_W * ENG_H_PX;
    i3d = malloc(n * 2); iflou = malloc(n * 2); idep = malloc(n); ih3 = malloc(n * 4); icol = malloc((size_t)ENG_W * 4 * 3);
    i2 = malloc(n / 2); b2 = malloc(n / 2);
    if (!i3d || !iflou || !idep || !ih3 || !icol || !i2 || !b2) return -1;
#endif
#else
    fb = malloc(sizeof(uint32_t) * S * S);
#endif
    if (!fb) return -1;
    eng_config_t cfg = { 0, alloc_web, js_save_write, js_save_read, NULL, NULL, NULL };
    cfg.joystick = joystick;
    return eng_init(pak, (size_t)len, &cfg);
}

static inline uint32_t rgba(uint16_t c) {
    const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return 0xFF000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((r << 3) | (r >> 2));
}
#if ENG_RECT
/* Taille de l'image (k 0 : largeur, 1 : hauteur), origine du cadre de l'interface dans l'image (2 : x, 3 : y), échelle de
   l'image où vivent les clics (4 : 2, l'image doublée de la LDK ; 1 en premium), taille maximale (5 : largeur, 6 : hauteur) */
EXPORT("web_dim") int web_dim(int k) {
    return k == 0 ? iw : k == 1 ? ih : k == 2 ? ui_ox : k == 3 ? ui_oy : k == 4 ? ENG_SCALE : k == 5 ? ENG_W : ENG_H_PX;
}
#if ENG_PREMIUM
/* Définition de l'image (version premium) : w × h, bornée par ENG_W × ENG_H_PX ; flou de profondeur allumé ou non */
EXPORT("web_definition") void web_definition(int w, int h) { eng_set_screen(w, h); eng_screen_size(&iw, &ih); }
EXPORT("web_flou") void web_flou(int on) { flou_on = on; }
/* Part floutée selon la profondeur, sur 16 (flou_regler de main_android.c) : nette autour de Sera, de plus en plus floue
   derrière (à partir de 4 unités) et devant la caméra ; le ciel flou */
static void flou_regler(void) {
    const int f = eng_focus_depth();
    for (int d = 0; d < 255; d++) {
        const int loin = d - (f + 24), pres = (f - 18) - d;
        const int t = loin > 0 ? loin * 16 / 60 : pres > 0 ? pres * 16 / 24 : 0;
        flou_lut[d] = (uint8_t)(t > 14 ? 14 : t);
    }
    flou_lut[255] = 12;
}
/* Flou en boîte de (2r + 1)² pixels de src (w × h) par sommes glissantes (le coût ne dépend pas du rayon) : horizontal dans ih3
   (composantes côte à côte : rouge << 20, vert << 10, bleu), puis vertical par colonnes (icol), moyenne dans dst */
static void flou_boite(const uint16_t *src, int w, int h, int r, uint16_t *dst) {
    for (int y = 0; y < h; y++) {
        const uint16_t *sl = src + (size_t)y * w;
        uint32_t *o = ih3 + (size_t)y * w;
        uint32_t R = 0, G = 0, Bc = 0;
        for (int k = -r; k <= r; k++) { const uint32_t c = sl[k < 0 ? 0 : k >= w ? w - 1 : k]; R += c >> 11; G += (c >> 5) & 63; Bc += c & 31; }
        for (int x = 0; x < w; x++) {
            o[x] = R << 20 | G << 10 | Bc;
            const uint32_t a = sl[x - r < 0 ? 0 : x - r], b = sl[x + r + 1 >= w ? w - 1 : x + r + 1];
            R += (b >> 11) - (a >> 11); G += ((b >> 5) & 63) - ((a >> 5) & 63); Bc += (b & 31) - (a & 31);
        }
    }
    uint32_t *cr = icol, *cg = icol + w, *cb = icol + 2 * w;
    for (int x = 0; x < w; x++) {
        uint32_t R = 0, G = 0, Bc = 0;
        for (int k = -r; k <= r; k++) { const uint32_t v = ih3[(size_t)(k < 0 ? 0 : k >= h ? h - 1 : k) * w + x]; R += v >> 20; G += (v >> 10) & 1023; Bc += v & 1023; }
        cr[x] = R; cg[x] = G; cb[x] = Bc;
    }
    const uint32_t n = (uint32_t)(2 * r + 1) * (uint32_t)(2 * r + 1);
    for (int y = 0; y < h; y++) {
        uint16_t *o = dst + (size_t)y * w;
        const uint32_t *va = ih3 + (size_t)(y - r < 0 ? 0 : y - r) * w, *vb = ih3 + (size_t)(y + r + 1 >= h ? h - 1 : y + r + 1) * w;
        for (int x = 0; x < w; x++) {
            o[x] = (uint16_t)((cr[x] / n) << 11 | (cg[x] / n) << 5 | (cb[x] / n));
            const uint32_t a = va[x], b = vb[x];
            cr[x] += (b >> 20) - (a >> 20); cg[x] += ((b >> 10) & 1023) - ((a >> 10) & 1023); cb[x] += (b & 1023) - (a & 1023);
        }
    }
}
/* Flou de profondeur sur l'image réduite de moitié (11/11/2026 : en pleine définition, 12 ms par image en 1280 × 720) : moyenne de
   chaque carré de 2 × 2 (i2), flou de rayon r (b2 ; 3 en 1080 lignes, comme les 3 de l'app Android à mi-définition), puis chaque
   pixel net mêlé au flou de sa place selon sa profondeur ; rien à faire là où c'est net */
static void flou(void) {
    const int W = iw, H = ih, w2 = W / 2, h2 = H / 2, r = H / 360 < 1 ? 1 : H / 360;
    for (int y = 0; y < h2; y++) {
        const uint16_t *a = i3d + (size_t)(2 * y) * W, *b = a + W;
        uint16_t *o = i2 + (size_t)y * w2;
        for (int x = 0; x < w2; x++) {
            uint32_t p = a[2 * x], q = a[2 * x + 1], s2 = b[2 * x], t2 = b[2 * x + 1];
            p = (p | p << 16) & 0x07E0F81Fu; q = (q | q << 16) & 0x07E0F81Fu; s2 = (s2 | s2 << 16) & 0x07E0F81Fu; t2 = (t2 | t2 << 16) & 0x07E0F81Fu;
            const uint32_t m = ((p + q + s2 + t2) >> 2) & 0x07E0F81Fu;
            o[x] = (uint16_t)(m | m >> 16);
        }
    }
    flou_boite(i2, w2, h2, r, b2);
    for (int y = 0; y < H; y++) {
        const size_t l = (size_t)y * W;
        const uint16_t *fl = b2 + (size_t)(y / 2 < h2 ? y / 2 : h2 - 1) * w2;
        for (int x = 0; x < W; x++) {
            const uint32_t t = flou_lut[idep[l + x]], c = i3d[l + x];
            if (!t) { iflou[l + x] = (uint16_t)c; continue; }
            const uint32_t f = fl[x / 2 < w2 ? x / 2 : w2 - 1];
            const uint32_t R = f >> 11, G = (f >> 5) & 63, Bc = f & 31, c1 = c >> 11, c2 = (c >> 5) & 63, c3 = c & 31;
            iflou[l + x] = (uint16_t)(((c1 * (16 - t) + R * t) >> 4) << 11 | ((c2 * (16 - t) + G * t) >> 4) << 5 | ((c3 * (16 - t) + Bc * t) >> 4));
        }
    }
}
#endif
/* Actions de la page (touches choisies par le joueur, pc.template.html : TOUCHES), bits : */
enum { A_HAUT = 1, A_BAS = 2, A_GAUCHE = 4, A_DROITE = 8, A_OK = 16, A_RETOUR = 32, A_MENU = 64, A_CAM_G = 128, A_CAM_D = 256,
       A_GAZ = 512, A_FREIN = 1024, A_MONTER = 2048, A_DESCENDRE = 4096, A_PAS = 8192 };
/* Une image de la version PC : actions en entrée du moteur comme les boutons de la LDK (platform/retrofw : entree) : la croix
   comme le stick d'une manette (elle court à fond, au pas avec A_PAS), en véhicule les gaz et le frein (ou haut et bas de la
   croix), la caméra sur ses deux touches ; souris en pixels de l'image doublée (touch : bouton gauche), convertie ici */
EXPORT("web_frame_pc") void web_frame_pc(float dt, float time_s, int a, int touch, float tx, float ty) {
    eng_input_t in;
    memset(&in, 0, sizeof in);
    uint32_t k = 0;
    if (a & A_HAUT) k |= ENG_KEY_UP;
    if (a & A_BAS) k |= ENG_KEY_DOWN;
    if (a & A_GAUCHE) k |= ENG_KEY_LEFT;
    if (a & A_DROITE) k |= ENG_KEY_RIGHT;
    if (a & A_OK) k |= ENG_KEY_OK;
    if (a & A_RETOUR) k |= ENG_KEY_BACK;
    if (a & A_MENU) k |= ENG_KEY_MENU;
    if (a & A_CAM_G) k |= ENG_KEY_ROT_L;
    if (a & A_CAM_D) k |= ENG_KEY_ROT_R;
    if (a & A_MONTER) k |= ENG_KEY_ALT_UP;
    if (a & A_DESCENDRE) k |= ENG_KEY_ALT_DOWN;
    in.keys = k;
    in.manette = 1;
    const float dx = (a & A_DROITE ? 1.0f : 0) - (a & A_GAUCHE ? 1.0f : 0), dy = (a & A_BAS ? 1.0f : 0) - (a & A_HAUT ? 1.0f : 0);
    if (eng_conduite()) {   /* véhicule : gaz (à moitié avec A_PAS), frein puis marche arrière ; sinon haut et bas de la croix */
        in.stick_x = dx;
        in.stick_y = a & A_GAZ ? (a & A_PAS ? -0.5f : -1.0f) : a & A_FREIN ? 1.0f : dy * (a & A_PAS ? 0.5f : 1.0f);
    } else if (dx != 0 || dy != 0) {
        const float f = (a & A_PAS ? 0.5f : 1.0f) / sqrtf(dx * dx + dy * dy);
        in.stick_x = dx * f; in.stick_y = dy * f;
    }
    in.touch = touch; in.touch_x = tx - ui_ox; in.touch_y = ty - ui_oy;
    eng_update(dt, &in);
    eng_build_frame(time_s);
    eng_present();
#if ENG_PREMIUM
    if (flou_on) {   /* 3D de toute l'image et sa profondeur, le flou, puis l'interface par-dessus (comme l'app Android) */
        flou_regler();
        for (int y = 0; y < ih; y += ENG_BAND) {
            const int h = ih - y < ENG_BAND ? ih - y : ENG_BAND;
            eng_render_band_3d(0, y, h, i3d + (size_t)y * iw, idep + (size_t)y * iw);
        }
        flou();
        for (int y = 0; y < ih; y += ENG_BAND) {
            const int h = ih - y < ENG_BAND ? ih - y : ENG_BAND;
            eng_render_band_ui(0, y, h, iflou + (size_t)y * iw);
        }
        for (size_t i = 0, n = (size_t)iw * ih; i < n; i++) fb[i] = rgba(iflou[i]);
        return;
    }
#endif
    for (int y = 0; y < ih; y += ENG_BAND) {
        const int h = ih - y < ENG_BAND ? ih - y : ENG_BAND;
#if ENG_PREMIUM
        eng_render_band_rect(y, h, i3d + (size_t)y * iw, NULL);
#else
        eng_render_band_rect(y, h, band, NULL);
        uint32_t *o = fb + y * iw;
        for (int i = 0; i < iw * h; i++) o[i] = rgba(band[i]);
#endif
    }
#if ENG_PREMIUM
    for (size_t i = 0, n = (size_t)iw * ih; i < n; i++) fb[i] = rgba(i3d[i]);
#endif
}
#endif

/* Une image : jeu, puis bandes de l'écran natif (3D et interface), converties en RGBA pour le canevas */
EXPORT("web_frame") void web_frame(float dt, float time_s, int keys, int touch, float tx, float ty, int touch2, float tx2, float ty2) {
    eng_input_t in = { (uint32_t)keys, touch, tx, ty, touch2, tx2, ty2 };
    eng_update(dt, &in);
    eng_build_frame(time_s);
    eng_present();
#if ENG_RECT
    return;   /* (écran rectangulaire : web_frame_pc) */
#endif
    for (int y = 0; y < S; y += ENG_NATIVE_BAND) {
        int h = S - y < ENG_NATIVE_BAND ? S - y : ENG_NATIVE_BAND;
        eng_render_native_band(y, h, band, 0, ENG_SCREEN);
        uint32_t *o = fb + y * S;
        for (int i = 0; i < S * h; i++) o[i] = rgba(band[i]);
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

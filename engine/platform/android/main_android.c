/*
 * Plateforme Android : montre Wear OS à écran rond (Galaxy Watch) et téléphone, NativeActivity sans code Java (deux
 * manifestes, même code : AndroidManifest.xml et telephone/AndroidManifest.xml).
 *
 * Le paquet monde et les musiques sont dans les assets de l'APK, rangés sans compression ; au lancement, le serveur de
 * l'Atelier en donne de plus récents s'il y en a (maj_android.c ; anneau de progression pendant la recherche et le
 * téléchargement). Deux bibliothèques du même code : libechoes.so (3D 233 × 233 doublée, comme l'ESP32) et
 * libechoes_natif.so (3D native 466 × 466), lancées par deux icônes (activité Natif des manifestes, dans son propre
 * processus). L'écran est une fenêtre en
 * RGB565 que le système met à l'échelle : 466 × 466 sur la montre, aux proportions de l'écran sur un téléphone (cercle
 * centré, bords noirs) ; le toucher est ramené en pixels de l'écran natif.
 * Deux fils comme les deux cœurs de l'ESP32 : le fil d'aide dessine les bandes de l'image présentée pendant que le
 * fil principal lit les entrées, fait avancer le jeu et prépare l'image suivante, puis il prend lui aussi des bandes.
 * Son : AAudio, mono 16 bits à la fréquence de la montre. Sauvegarde dans le dossier privé de l'application.
 * Bouton ou geste retour : ENG_KEY_BACK ; bouton programmable (STEM) : ENG_KEY_MENU ; lunette ou couronne : zoom.
 */
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android/asset_manager.h>
#include <aaudio/AAudio.h>
#include <android_native_app_glue.h>
#include <stdint.h>
#include <jni.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../../core/eng.h"
#include "maj_android.h"
#include "../../core/pak.h"

#define S ENG_SCREEN
#define TAG "echoes"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#ifndef ECHOES_FPS_MAX
#define ECHOES_FPS_MAX 30              /* cadence plafonnée (batterie de la montre) ; 0 : celle de l'écran */
#endif

static struct android_app *APP;
static ANativeWindow *win;
static int win_w = S, win_h = S;        /* taille réelle de la fenêtre (avant mise à l'échelle) */
static int buf_w = S, buf_h = S;        /* tampon de la fenêtre, mis à l'échelle de la fenêtre par le système */
static int img_x, img_y;                /* place de l'image FW × FH dans le tampon (téléphone, écran rond : cercle centré) */
static int FW = S, FH = S;              /* image : S × S (écran rond), ou la moitié de l'écran (plein écran, ENG_DYN) */
static int focused, resumed;
static int pret;                        /* 1 : jeu lancé ; -1 : pas de paquet (anneau rouge, puis fin) */
static double t_start, t_last;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
/* Horloge en microsecondes, sur 32 bits qui reviennent à zéro (le moteur n'en prend que des écarts) */
static uint32_t clock_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}
static void *alloc_android(size_t size, int fast) { (void)fast; return malloc(size); }

/* Sauvegarde : deux fichiers alternés dans le dossier privé */
static void save_path(int slot, char *p, size_t n) { snprintf(p, n, "%s/sauve%d.bin", APP->activity->internalDataPath, slot); }   /* partie × 2 + séquence & 1 */
static int save_write(int slot, const void *data, int len) {
    char p[512]; save_path(slot, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    int w = (int)fwrite(data, 1, len, f);
    fflush(f); fsync(fileno(f));
    fclose(f);
    return w;
}
static int save_read(int slot, void *data, int cap) {
    char p[512]; save_path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    int r = (int)fread(data, 1, cap, f);
    fclose(f);
    return r;
}

/* Fichier téléchargé, projeté en mémoire (aligné sur une page) */
static const uint8_t *projeter(const char *path, size_t *len) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    void *p = fstat(fd, &st) || !st.st_size ? MAP_FAILED : mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *len = (size_t)st.st_size;
    return p;
}

/* Musiques : asset rangé sans compression, projeté en mémoire par AAsset_getBuffer, ou fichier téléchargé */
static const uint8_t *mus;
static uint32_t mus_n;
static int mus_read(uint32_t off, void *dst, uint32_t len) {
    if ((uint64_t)off + len > mus_n) return -1;
    memcpy(dst, mus + off, len);
    return 0;
}

/* Son */
static AAudioStream *audio;
static int audio_rate;
static volatile int audio_lost;          /* flux coupé (casque Bluetooth branché ou retiré) : à rouvrir */
static aaudio_data_callback_result_t audio_cb(AAudioStream *s, void *user, void *data, int32_t n) {
    eng_audio_mix(data, n, audio_rate);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void audio_err(AAudioStream *s, void *user, aaudio_result_t err) { audio_lost = 1; }
static void audio_close(void) {
    if (audio) { AAudioStream_requestStop(audio); AAudioStream_close(audio); audio = NULL; }
}
static void audio_open(void) {
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 1);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    AAudioStreamBuilder_setErrorCallback(b, audio_err, NULL);
    if (AAudioStreamBuilder_openStream(b, &audio) != AAUDIO_OK) audio = NULL;
    AAudioStreamBuilder_delete(b);
    if (!audio) { LOGE("son : flux AAudio impossible à ouvrir"); return; }
    audio_rate = AAudioStream_getSampleRate(audio);
    audio_lost = 0;
    if (resumed) AAudioStream_requestStart(audio);
    LOGI("son : %d Hz", audio_rate);
}

/* Entrées : lues par le fil principal entre deux images. Un appui (doigt ou touche) relâché avant l'image suivante
   reste vu pendant une image, sinon un toucher bref échapperait au jeu. */
static int nf, tap;
static float tx[2], ty[2];
static uint32_t keys_held, keys_tap;

static uint32_t key_of(int code) {
    switch (code) {
    case AKEYCODE_BACK: case AKEYCODE_ESCAPE: return ENG_KEY_BACK;
    case AKEYCODE_STEM_PRIMARY: case AKEYCODE_STEM_1: case AKEYCODE_STEM_2: case AKEYCODE_STEM_3: case AKEYCODE_M: return ENG_KEY_MENU;
    case AKEYCODE_ENTER: case AKEYCODE_DPAD_CENTER: return ENG_KEY_OK;
    /* clavier des émulateurs */
    case AKEYCODE_DPAD_UP: return ENG_KEY_UP;
    case AKEYCODE_DPAD_DOWN: return ENG_KEY_DOWN;
    case AKEYCODE_DPAD_LEFT: return ENG_KEY_LEFT;
    case AKEYCODE_DPAD_RIGHT: return ENG_KEY_RIGHT;
    default: return 0;
    }
}

static int32_t on_input(struct android_app *app, AInputEvent *e) {
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_KEY) {
        uint32_t k = key_of(AKeyEvent_getKeyCode(e));
        if (!k) return 0;
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN) { keys_held |= k; keys_tap |= k; }
        else if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_UP) keys_held &= ~k;
        return 1;
    }
    if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION) return 0;
    const int32_t act = AMotionEvent_getAction(e), a = act & AMOTION_EVENT_ACTION_MASK;
    if ((AInputEvent_getSource(e) & AINPUT_SOURCE_ROTARY_ENCODER) == AINPUT_SOURCE_ROTARY_ENCODER) {
        /* Lunette ou couronne : comme la molette de la page web (sens horaire : valeur négative) */
        if (a == AMOTION_EVENT_ACTION_SCROLL && pret > 0) {
            float v = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_SCROLL, 0);
            v = v > 3 ? 3 : v < -3 ? -3 : v;
            eng_set_zoom(eng_zoom() * powf(1.08f, v));
        }
        return 1;
    }
    /* Doigts posés (deux au plus : pinch), en pixels de l'écran natif. Hors de l'image (bas d'un écran de téléphone) :
       ignorés ; dans la marge de 20 pixels autour, ramenés au bord. */
    const float kx = (float)buf_w / win_w, ky = (float)buf_h / win_h;
    const int up = (act & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    int n = (int)AMotionEvent_getPointerCount(e), k = 0;
    if (a == AMOTION_EVENT_ACTION_UP || a == AMOTION_EVENT_ACTION_CANCEL) n = 0;
    for (int i = 0; i < n && k < 2; i++) {
        if (a == AMOTION_EVENT_ACTION_POINTER_UP && i == up) continue;
        const float x = AMotionEvent_getX(e, i) * kx - img_x, y = AMotionEvent_getY(e, i) * ky - img_y;
        if (x < -20 || y < -20 || x > FW + 20 || y > FH + 20) continue;
        tx[k] = x < 0 ? 0 : x > FW - 1 ? FW - 1 : x;
        ty[k] = y < 0 ? 0 : y > FH - 1 ? FH - 1 : y;
        k++;
    }
    nf = k;
    if (a == AMOTION_EVENT_ACTION_DOWN && k) tap = 1;
    return 1;
}

#if !ENG_DYN
/* Rendu à deux fils. L'image va d'abord dans fb (466 × 466, coins hors du cercle jamais écrits : noirs), copiée
   ensuite dans la fenêtre. Bandes rognées à la plus grande corde du cercle, réparties par un compteur atomique. */
enum { NB = (S + ENG_NATIVE_BAND - 1) / ENG_NATIVE_BAND };
static uint16_t *fb;
static uint16_t band_buf[2][S * ENG_NATIVE_BAND];
static int16_t wx0[NB], wx1[NB];
static volatile int next_band = NB, bands_done;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_go = PTHREAD_COND_INITIALIZER, cv_drawn = PTHREAD_COND_INITIALIZER;
static int go, drawn = 1, quit;

static void band_window(int y, int h, int *x0, int *x1) {
    float rr = S / 2.0f, best = 0;
    for (int j = y; j < y + h; j++) {
        float dy = j + 0.5f - rr, v = rr * rr - dy * dy;
        if (v > best) best = v;
    }
    float hw = sqrtf(best);
    int a = (int)floorf(rr - hw) & ~1, b = ((int)ceilf(rr + hw) + 1) & ~1;
    *x0 = a < 0 ? 0 : a; *x1 = b > S ? S : b;
}

static void draw_bands(int worker) {
    uint16_t *buf = band_buf[worker];
    for (int b; (b = __atomic_fetch_add(&next_band, 1, __ATOMIC_SEQ_CST)) < NB;) {
        const int y = b * ENG_NATIVE_BAND, h = S - y < ENG_NATIVE_BAND ? S - y : ENG_NATIVE_BAND, x0 = wx0[b], w = wx1[b] - x0;
        eng_render_band_on(worker, y, h, buf, x0, wx1[b]);
        for (int j = 0; j < h; j++) memcpy(fb + (y + j) * S + x0, buf + j * w, (size_t)w * 2);
        if (__atomic_add_fetch(&bands_done, 1, __ATOMIC_SEQ_CST) == NB) {
            pthread_mutex_lock(&mtx); drawn = 1; pthread_cond_signal(&cv_drawn); pthread_mutex_unlock(&mtx);
        }
    }
}

static void *helper(void *arg) {
    for (;;) {
        pthread_mutex_lock(&mtx);
        while (!go && !quit) pthread_cond_wait(&cv_go, &mtx);
        go = 0;
        const int q = quit;
        pthread_mutex_unlock(&mtx);
        if (q) return NULL;
        draw_bands(0);
    }
}
#else
/* Plein écran (téléphone, ENG_DYN) : image FW × FH, la moitié de l'écran, agrandie par le système. ENG_WORKERS fils
   (celui-ci, numéro 0, et ENG_WORKERS - 1 fils d'aide) dessinent chaque image en passes séparées par une barrière :
   3D et profondeur des bandes (fb, db), flou de profondeur horizontal (hb : sommes) puis vertical, mêlé à l'image
   nette selon la profondeur (ob), enfin l'interface par-dessus. Le fil principal prépare l'image suivante pendant que
   les autres commencent la 3D de l'image présentée. */
#define FLOU_R 3                        /* rayon du flou, en pixels de l'image (le double à l'écran) */
enum { PAQ = 16 };                      /* lignes par paquet des passes du flou */
static uint16_t *fb, *ob;
static uint8_t *db;
static uint32_t *hb;
static uint8_t flou_lut[256];           /* part de l'image floutée selon la profondeur, sur 16 */
static int flou_on = 1;                 /* réglages : « Flou de profondeur » (flou.txt) */
static volatile int cpt[4];             /* bandes ou paquets de lignes déjà pris, par passe */
static pthread_barrier_t bar;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_go = PTHREAD_COND_INITIALIZER;
static int gen, quit;

static inline int prendre(int k) { return __atomic_fetch_add(&cpt[k], 1, __ATOMIC_SEQ_CST); }
static void passe_3d(int w) {
    const int nb = (FH + ENG_NATIVE_BAND - 1) / ENG_NATIVE_BAND;
    for (int b; (b = prendre(0)) < nb;) {
        const int y = b * ENG_NATIVE_BAND, h = FH - y < ENG_NATIVE_BAND ? FH - y : ENG_NATIVE_BAND;
        eng_render_band_3d(w, y, h, fb + (size_t)y * FW, db + (size_t)y * FW);
    }
}
/* Sommes de 2 FLOU_R + 1 pixels voisins sur la ligne, composantes côte à côte : rouge << 20, vert << 10, bleu */
static void passe_h(void) {
    for (int p; (p = prendre(1)) * PAQ < FH;)
        for (int y = p * PAQ; y < FH && y < (p + 1) * PAQ; y++) {
            const uint16_t *s = fb + (size_t)y * FW;
            uint32_t *o = hb + (size_t)y * FW;
            for (int x = 0; x < FW; x++) {
                uint32_t r = 0, g = 0, b = 0;
                for (int k = -FLOU_R; k <= FLOU_R; k++) {
                    const int xx = x + k < 0 ? 0 : x + k >= FW ? FW - 1 : x + k;
                    const uint32_t c = s[xx];
                    r += c >> 11; g += (c >> 5) & 63; b += c & 31;
                }
                o[x] = r << 20 | g << 10 | b;
            }
        }
}
/* Sommes des lignes voisines : moyenne du carré, mêlée au pixel net selon sa profondeur (rien à faire là où c'est net) */
static void passe_v(void) {
    const uint32_t n = (2 * FLOU_R + 1) * (2 * FLOU_R + 1);
    for (int p; (p = prendre(2)) * PAQ < FH;)
        for (int y = p * PAQ; y < FH && y < (p + 1) * PAQ; y++) {
            const size_t l = (size_t)y * FW;
            for (int x = 0; x < FW; x++) {
                const uint32_t t = flou_lut[db[l + x]], c = fb[l + x];
                if (!t) { ob[l + x] = (uint16_t)c; continue; }
                uint32_t r = 0, g = 0, b = 0;
                for (int k = -FLOU_R; k <= FLOU_R; k++) {
                    const int yy = y + k < 0 ? 0 : y + k >= FH ? FH - 1 : y + k;
                    const uint32_t v = hb[(size_t)yy * FW + x];
                    r += v >> 20; g += (v >> 10) & 1023; b += v & 1023;
                }
                r /= n; g /= n; b /= n;
                const uint32_t cr = c >> 11, cg = (c >> 5) & 63, cb = c & 31;
                ob[l + x] = (uint16_t)(((cr * (16 - t) + r * t) >> 4) << 11 | ((cg * (16 - t) + g * t) >> 4) << 5 | ((cb * (16 - t) + b * t) >> 4));
            }
        }
}
static void passe_ui(int w) {
    const int nb = (FH + ENG_NATIVE_BAND - 1) / ENG_NATIVE_BAND;
    for (int b; (b = prendre(3)) < nb;) {
        const int y = b * ENG_NATIVE_BAND, h = FH - y < ENG_NATIVE_BAND ? FH - y : ENG_NATIVE_BAND;
        eng_render_band_ui(w, y, h, ob + (size_t)y * FW);
    }
}
/* Après la 3D : le flou (s'il est allumé ; sinon ob est fb), l'interface, et la dernière barrière (image finie) */
static void passes(int w) {
    pthread_barrier_wait(&bar);
    if (flou_on) { passe_h(); pthread_barrier_wait(&bar); passe_v(); pthread_barrier_wait(&bar); }
    passe_ui(w);
    pthread_barrier_wait(&bar);
}
/* Part floutée selon la profondeur : nette autour de Sera, de plus en plus floue derrière (à partir de 4 unités) et
   devant la caméra (à moins de 3 unités de Sera) ; le ciel flou (DEP_K = 6 par unité, voir render.c) */
static void flou_regler(void) {
    const int f = eng_focus_depth();
    for (int d = 0; d < 255; d++) {
        const int loin = d - (f + 24), pres = (f - 18) - d;
        int t = loin > 0 ? loin * 16 / 60 : pres > 0 ? pres * 16 / 24 : 0;
        flou_lut[d] = (uint8_t)(t > 14 ? 14 : t);
    }
    flou_lut[255] = 12;
}
static void *helper(void *arg) {
    const int w = (int)(intptr_t)arg;
    int vu = 0;
    for (;;) {
        pthread_mutex_lock(&mtx);
        while (gen == vu && !quit) pthread_cond_wait(&cv_go, &mtx);
        vu = gen;
        const int q = quit;
        pthread_mutex_unlock(&mtx);
        if (q) return NULL;
        passe_3d(w);
        passes(w);
    }
}
#endif

/* Copie l'image FW × FH dans la fenêtre en (img_x, img_y), le reste en noir (RGB565 demandé ; RGBX 8888 si le système
   l'a imposé) */
static void blit(const uint16_t *img) {
    ANativeWindow_Buffer wb;
    if (!win || ANativeWindow_lock(win, &wb, NULL) != 0) return;
    const int bpp = wb.format == WINDOW_FORMAT_RGB_565 ? 2 : 4;
    if (img_x || img_y)
        for (int y = 0; y < wb.height; y++) memset((uint8_t *)wb.bits + (size_t)y * wb.stride * bpp, 0, (size_t)wb.width * bpp);
    const int w = wb.width - img_x < FW ? wb.width - img_x : FW, h = wb.height - img_y < FH ? wb.height - img_y : FH;
    for (int y = 0; y < h; y++) {
        uint8_t *row = (uint8_t *)wb.bits + ((size_t)(y + img_y) * wb.stride + img_x) * bpp;
        const uint16_t *s = img + (size_t)y * FW;
        if (bpp == 2) { memcpy(row, s, (size_t)w * 2); continue; }
        uint32_t *o = (uint32_t *)row;
        for (int x = 0; x < w; x++) {
            uint32_t c = s[x], r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            o[x] = 0xFF000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((r << 3) | (r >> 2));
        }
    }
    ANativeWindow_unlockAndPost(win);
}

/* Écran d'attente de la mise à jour : anneau près du bord, arc qui tourne pendant la recherche du serveur, part
   téléchargée ensuite (depuis le haut, sens horaire) ; rouge en entier s'il n'y a aucun paquet */
static void attente(void) {
    float a0 = 0, a1 = 1.01f;
    if (pret >= 0 && maj_phase == MAJ_TELECHARGE) a1 = maj_part;
    else if (pret >= 0) { a0 = fmodf((float)now_s() * 0.8f, 1); a1 = a0 + 0.12f; }
    const uint16_t allume = pret < 0 ? 0xF800 : 0xFC60, eteint = 0x2104;
    const float cx = FW / 2.0f, cy = FH / 2.0f, rr = (FW < FH ? FW : FH) / 2.0f, r0 = (rr - 22) * (rr - 22), r1 = (rr - 14) * (rr - 14);
    for (int y = 0; y < FH; y++) {
        uint16_t *row = fb + (size_t)y * FW;
        const float dy = y + 0.5f - cy;
        for (int x = 0; x < FW; x++) {
            const float dx = x + 0.5f - cx, d2 = dx * dx + dy * dy;
            if (d2 < r0 || d2 > r1) { row[x] = 0; continue; }
            float a = atan2f(dx, -dy) * 0.15915494f;
            if (a < 0) a += 1;
            row[x] = (a1 > 1 ? a >= a0 || a < a1 - 1 : a >= a0 && a < a1) ? allume : eteint;
        }
    }
    blit(fb);
}

/* Une image : les fils d'aide dessinent l'image présentée N pendant que celui-ci prépare N+1 et les aide ensuite ; N
   est affichée, puis N+1 présentée */
static void frame(void) {
#if !ENG_DYN
    __atomic_store_n(&bands_done, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&next_band, 0, __ATOMIC_SEQ_CST);
    pthread_mutex_lock(&mtx); drawn = 0; go = 1; pthread_cond_signal(&cv_go); pthread_mutex_unlock(&mtx);
#else
    if (flou_on) flou_regler();
    for (int k = 0; k < 4; k++) __atomic_store_n(&cpt[k], 0, __ATOMIC_SEQ_CST);
    pthread_mutex_lock(&mtx); gen++; pthread_cond_broadcast(&cv_go); pthread_mutex_unlock(&mtx);
#endif

    const double t = now_s();
    float dt = (float)(t - t_last);
    t_last = t;
    if (dt > 0.1f) dt = 0.1f;            /* retour de pause : pas de bond */
    eng_input_t in = { keys_held | keys_tap };
    keys_tap = 0;
    if (nf >= 1 || tap) { in.touch = 1; in.touch_x = tx[0]; in.touch_y = ty[0]; }
    if (nf >= 2) { in.touch2 = 1; in.touch2_x = tx[1]; in.touch2_y = ty[1]; }
    tap = 0;
    eng_update(dt, &in);
    eng_build_frame((float)(t - t_start));
#if !ENG_DYN
    draw_bands(1);
    pthread_mutex_lock(&mtx);
    while (!drawn) pthread_cond_wait(&cv_drawn, &mtx);
    pthread_mutex_unlock(&mtx);
#else
    passe_3d(0);
    passes(0);
#endif

#if ECHOES_FPS_MAX > 0
    static double next;
    const double now = now_s();
    if (next > now) {
        struct timespec ts = { (time_t)next, (long)((next - (time_t)next) * 1e9) };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    }
    next = (next > now ? next : now) + 1.0 / ECHOES_FPS_MAX;
#endif
#if !ENG_DYN
    blit(fb);
#else
    blit(ob);
#endif
    /* Cadence et temps de rendu dans le journal (adb logcat -s echoes) toutes les 10 s */
    static int frames;
    static double stat_t;
    static uint32_t us3d, usui;
    const eng_stats_t *st = eng_stats();
    frames++; us3d += st->us_3d; usui += st->us_ui;
    if (t - stat_t > 10) {
        if (stat_t > 0) LOGI("%.1f i/s, 3D %.1f ms, interface %.1f ms (temps additionnés de %d fils), %d triangles", frames / (t - stat_t),
            us3d / 1000.0 / frames, usui / 1000.0 / frames, ENG_DYN ? ENG_WORKERS : 2, st->tris);
        stat_t = t; frames = 0; us3d = usui = 0;
    }
    eng_present();
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        win = app->window;
        if (ANativeWindow_getWidth(win) != buf_w || ANativeWindow_getHeight(win) != buf_h) { win_w = ANativeWindow_getWidth(win); win_h = ANativeWindow_getHeight(win); }
        /* Montre (écran carré à 2 % près) : l'image remplit l'écran. Téléphone : tampon aux proportions de l'écran, cercle
           centré avec 7 % de marge de chaque côté du petit côté (le geste « retour » d'Android part des bords, là où
           l'on fait tourner la caméra) */
#if ENG_DYN
        /* Plein écran : l'image à la moitié de l'écran (côtés pairs), agrandie par le système ; taille fixée au premier
           affichage, le moteur la garde (portrait verrouillé) */
        if (!fb) {
            FW = (win_w / 2) & ~1; FH = (win_h / 2) & ~1;
            if (FW > ENG_W) FW = ENG_W;
            if (FH > ENG_H_PX) FH = ENG_H_PX;
            const size_t n = (size_t)FW * FH;
            fb = calloc(n, 2); ob = calloc(n, 2); db = malloc(n); hb = malloc(n * 4);
            if (!fb || !ob || !db || !hb) { LOGE("mémoire insuffisante (image %d x %d)", FW, FH); free(fb); fb = NULL; break; }
        }
        buf_w = FW; buf_h = FH; img_x = img_y = 0;
#else
        if (abs(win_w - win_h) * 50 <= win_w) buf_w = buf_h = S;
        else if (win_w < win_h) { buf_w = S * 100 / 86; buf_h = (int)((int64_t)buf_w * win_h / win_w); }
        else { buf_h = S * 100 / 86; buf_w = (int)((int64_t)buf_h * win_w / win_h); }
        img_x = (buf_w - S) / 2; img_y = (buf_h - S) / 2;
#endif
        ANativeWindow_setBuffersGeometry(win, buf_w, buf_h, WINDOW_FORMAT_RGB_565);
        LOGI("fenêtre %d x %d, tampon %d x %d mis à l'échelle, image %d x %d en (%d, %d)", win_w, win_h, buf_w, buf_h, FW, FH, img_x, img_y);
        break;
    case APP_CMD_TERM_WINDOW: win = NULL; break;
    case APP_CMD_GAINED_FOCUS: focused = 1; t_last = now_s(); break;
    case APP_CMD_LOST_FOCUS: focused = 0; nf = 0; keys_held = 0; break;
    case APP_CMD_RESUME: resumed = 1; if (audio) AAudioStream_requestStart(audio); break;
    case APP_CMD_PAUSE: resumed = 0; if (audio) AAudioStream_requestPause(audio); break;
    default: break;
    }
}

/* Écran de réglages (Reglages.java : serveur de l'Atelier, édition, régénération du monde), ouvert par JNI depuis
   l'entrée RÉGLAGES de l'écran titre, ou quand il n'y a pas de monde à jouer (pas dans l'APK autonome) */
#if !ECHOES_AUTONOME
static void ouvrir_reglages(void) {
    ANativeActivity *a = APP->activity;
    JNIEnv *env;
    if ((*a->vm)->AttachCurrentThread(a->vm, &env, NULL) != JNI_OK) return;
    jclass ci = (*env)->FindClass(env, "android/content/Intent"), ca = (*env)->GetObjectClass(env, a->clazz);
    jobject it = (*env)->NewObject(env, ci, (*env)->GetMethodID(env, ci, "<init>", "()V"));
    jobject pkg = (*env)->CallObjectMethod(env, a->clazz, (*env)->GetMethodID(env, ca, "getPackageName", "()Ljava/lang/String;"));
    jstring cls = (*env)->NewStringUTF(env, "fr.maetzindustries.echoes.Reglages");
    (*env)->CallObjectMethod(env, it, (*env)->GetMethodID(env, ci, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;"), pkg, cls);
    (*env)->CallVoidMethod(env, a->clazz, (*env)->GetMethodID(env, ca, "startActivity", "(Landroid/content/Intent;)V"), it);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); LOGE("réglages : activité impossible à ouvrir"); }
    (*env)->DeleteLocalRef(env, cls); (*env)->DeleteLocalRef(env, pkg); (*env)->DeleteLocalRef(env, it);
    (*env)->DeleteLocalRef(env, ca); (*env)->DeleteLocalRef(env, ci);
    (*a->vm)->DetachCurrentThread(a->vm);
}
#endif

#if ENG_DYN
/* Hauteur des barres du système (barre d'état ou encoche, barre de navigation), en pixels de l'écran, lue par JNI
   (WindowInsets stables : la barre comptée même cachée) ; 0 si la vue n'est pas encore attachée */
static void marges_systeme(int *haut, int *bas) {
    *haut = *bas = 0;
    ANativeActivity *a = APP->activity;
    JNIEnv *env;
    if ((*a->vm)->AttachCurrentThread(a->vm, &env, NULL) != JNI_OK) return;
    jclass ca = (*env)->GetObjectClass(env, a->clazz);
    jobject w = (*env)->CallObjectMethod(env, a->clazz, (*env)->GetMethodID(env, ca, "getWindow", "()Landroid/view/Window;"));
    jclass cw = w ? (*env)->GetObjectClass(env, w) : NULL;
    jobject v = w ? (*env)->CallObjectMethod(env, w, (*env)->GetMethodID(env, cw, "getDecorView", "()Landroid/view/View;")) : NULL;
    jclass cv = v ? (*env)->GetObjectClass(env, v) : NULL;
    jobject in = v ? (*env)->CallObjectMethod(env, v, (*env)->GetMethodID(env, cv, "getRootWindowInsets", "()Landroid/view/WindowInsets;")) : NULL;
    if (in) {
        jclass ci = (*env)->GetObjectClass(env, in);
        *haut = (*env)->CallIntMethod(env, in, (*env)->GetMethodID(env, ci, "getStableInsetTop", "()I"));
        *bas = (*env)->CallIntMethod(env, in, (*env)->GetMethodID(env, ci, "getStableInsetBottom", "()I"));
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); *haut = *bas = 0; }
    (*a->vm)->DetachCurrentThread(a->vm);
}
#endif

/* Paquet en mémoire : téléchargé du serveur de l'Atelier (projeté), sinon copié de l'APK (l'asset projeté n'est aligné
   que sur 4 octets) */
static int start(struct android_app *app) {
    char p[512];
    const uint8_t *pak = NULL;
    size_t len = 0;
    const int q = maj_fichier("monde.pak", p, sizeof p);
    int dl = q > 0 && (pak = projeter(p, &len));
    /* Copie téléchargée d'un autre format que celui du moteur (exportée avant une mise à jour du moteur, ou après) :
       celle de l'APK, si elle peut servir */
    if (dl && (len < 8 || (uint32_t)(pak[4] | pak[5] << 8 | pak[6] << 16 | (uint32_t)pak[7] << 24) != PAK_VERSION)) {
        LOGE("monde téléchargé en version %d, le moteur lit la version %d : ignoré", len < 8 ? -1 : pak[4], PAK_VERSION);
        munmap((void *)pak, len); pak = NULL; dl = 0;
    }
    if (!dl) {
        /* Édition enfant choisie dans les réglages sans son monde téléchargé : pas le paquet 18+ de l'APK */
        AAsset *a = !maj_apk("monde.pak") ? NULL : AAssetManager_open(app->activity->assetManager, "monde.pak", AASSET_MODE_BUFFER);
        if (!a) { LOGE("pas de monde : ni dans l'APK (ou pas de cette édition), ni téléchargé du serveur de l'Atelier"); return -1; }
        len = (size_t)AAsset_getLength(a);
        uint8_t *copie = malloc(len);
        if (!copie || AAsset_read(a, copie, len) != (int)len) { LOGE("monde.pak illisible"); AAsset_close(a); return -1; }
        AAsset_close(a);
        pak = copie;
    }
    eng_config_t cfg = { 0, alloc_android, save_write, save_read, NULL, clock_us, NULL };
#if !ECHOES_AUTONOME
    cfg.reglages = ouvrir_reglages;   /* entrée RÉGLAGES de l'écran titre (pas dans l'APK autonome) */
#endif
#if ENG_DYN
    eng_set_screen(FW, FH);
    { int haut, bas; marges_systeme(&haut, &bas); eng_set_safe(haut * FH / win_h, bas * FH / win_h);
      LOGI("barres du système : %d px en haut, %d en bas (écran)", haut, bas); }
#endif
    if (eng_init(pak, len, &cfg)) { LOGE("eng_init : %s", eng_error()); return -1; }
    /* musique.bin reste ouvert : lu à mesure par le jeu */
    size_t n = 0;
    if (maj_fichier("musique.bin", p, sizeof p) > 0 && (mus = projeter(p, &n))) mus_n = (uint32_t)n;
    else {
        AAsset *m = AAssetManager_open(app->activity->assetManager, "musique.bin", AASSET_MODE_RANDOM);
        if (m && (mus = AAsset_getBuffer(m))) mus_n = (uint32_t)AAsset_getLength(m);
    }
    if (mus) eng_music_attach(mus_n, mus_read);
    else LOGI("pas de musique.bin : pas de musique");
#if ENG_DYN
    /* Flou de profondeur : coupé par les réglages (flou.txt : 0) */
    snprintf(p, sizeof p, "%s/flou.txt", app->activity->internalDataPath);
    FILE *fl = fopen(p, "r");
    if (fl) { flou_on = fgetc(fl) != '0'; fclose(fl); }
    if (!flou_on) ob = fb;
    LOGI("monde %d x %d chargé (%s), paquet %u Ko, plein écran %d x %d, %d fils, flou de profondeur %s", eng_world_size(), eng_world_size(),
        dl ? "téléchargé" : "de l'APK", (unsigned)(len / 1024), FW, FH, ENG_WORKERS, flou_on ? "oui" : "non");
    pthread_barrier_init(&bar, NULL, ENG_WORKERS);
    for (int k = 1; k < ENG_WORKERS; k++) {
        pthread_t th;
        if (pthread_create(&th, NULL, helper, (void *)(intptr_t)k)) return -1;
        pthread_detach(th);
    }
#else
    LOGI("monde %d x %d chargé (%s), paquet %u Ko, 3D interne %d x %d", eng_world_size(), eng_world_size(), dl ? "téléchargé" : "de l'APK",
        (unsigned)(len / 1024), ENG_W, ENG_H_PX);
    for (int b = 0; b < NB; b++) {
        int y = b * ENG_NATIVE_BAND, h = S - y < ENG_NATIVE_BAND ? S - y : ENG_NATIVE_BAND, x0, x1;
        band_window(y, h, &x0, &x1); wx0[b] = (int16_t)x0; wx1[b] = (int16_t)x1;
    }
    pthread_t th;
    if (pthread_create(&th, NULL, helper, NULL)) return -1;
    pthread_detach(th);
#endif
    audio_open();
    /* Première image préparée et présentée : la boucle dessine toujours l'image présentée */
    t_start = t_last = now_s();
    eng_input_t none = { 0 };
    eng_update(0, &none);
    eng_build_frame(0);
    eng_present();
    return 0;
}

void android_main(struct android_app *app) {
    /* Le cœur ne se lance qu'une fois par bibliothèque et par processus : une deuxième activité du même mode (lancée
       autrement que par son icône, qui ramène la première) se ferme aussitôt */
    static int instances;
    if (__atomic_fetch_add(&instances, 1, __ATOMIC_SEQ_CST)) {
        LOGE("moteur déjà lancé dans ce processus : activité fermée");
        ANativeActivity_finish(app->activity);
        while (!app->destroyRequested) {
            struct android_poll_source *src;
            if (ALooper_pollOnce(-1, NULL, NULL, (void **)&src) >= 0 && src) src->process(app, src);
        }
        return;
    }
    APP = app;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
#if !ENG_DYN
    if (!(fb = calloc((size_t)S * S, 2))) { ANativeActivity_finish(app->activity); return; }
#endif   /* plein écran : à la taille de la fenêtre, quand elle arrive */
    maj_lancer(app->activity->internalDataPath, app->activity->assetManager);
    double t_echec = 0;
    for (;;) {
        int ident;
        struct android_poll_source *src;
        /* Pendant la mise à jour : l'anneau, à 30 images par seconde */
        while ((ident = ALooper_pollOnce(pret <= 0 ? 33 : win && focused ? 0 : -1, NULL, NULL, (void **)&src)) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) {
                /* Le cœur n'est initialisé qu'une fois par processus : on le termine avec l'activité */
                pthread_mutex_lock(&mtx); quit = 1; pthread_cond_broadcast(&cv_go); pthread_mutex_unlock(&mtx);
                audio_close();
                exit(0);
            }
        }
        if (!pret && maj_phase == MAJ_FINI && fb) {
            if (start(app)) { pret = -1; t_echec = now_s(); }
            else { pret = 1; LOGI("prêt"); }
        }
        if (pret <= 0) {
            if (win && fb) attente();
            /* Pas de monde : les réglages (serveur, édition, régénération), puis l'app se ferme */
            if (pret < 0 && now_s() - t_echec > 3) {
#if !ECHOES_AUTONOME
                ouvrir_reglages();
#endif
                ANativeActivity_finish(app->activity); t_echec = 1e30;
            }
            continue;
        }
        if (audio_lost) { audio_close(); audio_open(); }
        if (win && focused) frame();
    }
}

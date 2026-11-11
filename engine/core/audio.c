/*
 * Mixage des sons (voir audio.h). AUDI : nombre u32, volume général en % u8, 0 u8 ×3, puis pour chaque son :
 * échantillons u32, fréquence u16, boucle u8, 0 u8, échantillons mono i16 (complétés à un multiple de 4 octets).
 *
 * Dix voix ; chacune lit un son à sa hauteur (pas en 16.16, rapporté à la fréquence de sortie), avec un volume qui
 * glisse vers sa cible (pas de clic au départ, à l'arrêt ni quand une boucle change de volume). Le jeu écrit les voix,
 * la tâche audio les lit : un échantillon décalé d'une image ne s'entend pas.
 */
#include <math.h>
#include <string.h>
#include "internal.h"
#include "audio.h"

#define VOIX 10

typedef struct { const int16_t *pcm; uint32_t n; int rate, boucle; } son_t;
typedef struct {
    volatile int actif, son, cle, tenue;
    volatile float vol, cible, hauteur;
    uint32_t pos;   /* 16.16 */
} voix_t;

static son_t S[SON_N + 1];
/* Sirène : 7 périodes d'une dent de scie de 700 Hz à 16 kHz (160 échantillons, la boucle ne claque pas) */
static int16_t sirene_pcm[160];
static voix_t V[VOIX];
static int nsons;
static float general = 1.0f;

static void sirene_init(void) {
    for (int k = 0; k < 160; k++) sirene_pcm[k] = (int16_t)(((k * 7) % 160) * 80 - 6400);
    S[SON_SIRENE].pcm = sirene_pcm; S[SON_SIRENE].n = 160; S[SON_SIRENE].rate = 16000; S[SON_SIRENE].boucle = 1;
}

void audio_load(const uint8_t *p, uint32_t size) {
    memset(S, 0, sizeof S); memset(V, 0, sizeof V); nsons = 0;
    sirene_init();
    if (!p || size < 8) return;
    uint32_t n = (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24, o = 8;
    general = p[4] ? p[4] / 100.0f : 1.0f;
    for (uint32_t i = 0; i < n && i < SON_N && o + 8 <= size; i++) {
        const uint32_t ne = (uint32_t)p[o] | p[o + 1] << 8 | p[o + 2] << 16 | (uint32_t)p[o + 3] << 24;
        const int rate = p[o + 4] | p[o + 5] << 8, boucle = p[o + 6];
        if (o + 8 + ne * 2 > size) break;
        S[i].pcm = (const int16_t *)(p + o + 8); S[i].n = ne; S[i].rate = rate ? rate : 16000; S[i].boucle = boucle;
        o += 8 + ((ne * 2 + 3) & ~3u);
        nsons = (int)i + 1;
    }
}
static int son_ok(int son) { return son >= 0 && (son < nsons || son == SON_SIRENE) && S[son].pcm; }
/* Paquet d'avant les sons de SON_TIR à SON_PAS_MECH : le son le plus proche parmi les anciens */
static const uint8_t REPLI[SON_N - SON_TIR] = { SON_EXPLOSION, SON_TECHNIQUE, SON_COUP, SON_EXPLOSION, SON_EXPLOSION, SON_COUP,
    SON_TECHNIQUE, SON_TECHNIQUE, SON_COUP, SON_EXPLOSION, SON_COUP, SON_ATTAQUE, SON_TECHNIQUE, SON_CHOC, SON_MENU, SON_MENU,
    SON_PORTIERE, SON_MOTEUR_VOITURE, SON_MOTEUR_VOITURE, SON_MOTEUR_CAMION, SON_MOTEUR_VAISSEAU, SON_MOTEUR_VAISSEAU, SON_CHOC };
static int repli(int son) { return son >= SON_TIR && son < SON_N && !son_ok(son) ? REPLI[son - SON_TIR] : son; }

void eng_set_volume(float v) { general = v < 0 ? 0 : v > 1 ? 1 : v; }
float eng_volume(void) { return general; }

/* Mixage : niveau de chaque canal (audio.h), réglé dans SYSTÈME › SON et gardé dans la sauvegarde ; un son est rangé
   dans son canal d'après son numéro (canalDe de l'émulateur) */
static float canal[CANAUX] = { 1, 1, 1, 1 };
void audio_canal(int c, float v) { if (c >= 0 && c < CANAUX) canal[c] = v < 0 ? 0 : v > 1 ? 1 : v; }
float audio_canal_niveau(int c) { return c >= 0 && c < CANAUX ? canal[c] : 1; }
static int canal_de(int son) {
    if (son <= SON_MOTEUR_VAISSEAU || (son >= SON_MOTEUR_MOTO && son <= SON_MOTEUR_REACTEUR)) return CANAL_MOTEURS;
    if (son == SON_BIP || son == SON_MENU || son == SON_VALIDER || son == SON_ANNULER) return CANAL_INTERFACE;
    return CANAL_EFFETS;
}

/* Une voix libre, sinon la plus faible des voix ponctuelles */
static voix_t *libre(void) {
    voix_t *best = NULL;
    for (int i = 0; i < VOIX; i++) {
        if (!V[i].actif) return &V[i];
        if (!V[i].cle && (!best || V[i].vol < best->vol)) best = &V[i];
    }
    return best;
}

void son_jouer(int son, float vol, float hauteur) {
    if (son == SON_VALIDER) effet_manette(ENG_EFFET_VALIDER);   /* la manette suit : validation, crash, soin */
    else if (son == SON_CRASH) effet_manette(ENG_EFFET_CRASH);
    else if (son == SON_SOIN) effet_manette(ENG_EFFET_SOIN);
    son = repli(son);
    if (!son_ok(son) || vol <= 0) return;
    voix_t *v = libre();
    if (!v) return;
    v->actif = 0;
    v->son = son; v->cle = 0; v->pos = 0; v->hauteur = hauteur; v->vol = vol; v->cible = vol; v->tenue = 0;
    v->actif = 1;
}

void son_boucle(int cle, int son, float vol, float hauteur) {
    son = repli(son);
    if (!son_ok(son)) return;
    voix_t *v = NULL;
    for (int i = 0; i < VOIX && !v; i++) if (V[i].actif && V[i].cle == cle) v = &V[i];
    if (v && v->son != son) { v->cible = 0; v->cle = 0; v = NULL; }   /* autre son : l'ancien s'éteint, le nouveau part */
    if (!v) {
        if (!(v = libre())) return;
        v->actif = 0;
        v->son = son; v->cle = cle; v->pos = 0; v->vol = 0;
        v->actif = 1;
    }
    v->cible = vol; v->hauteur = hauteur; v->tenue = 1;
}

void audio_fin_image(void) {
    for (int i = 0; i < VOIX; i++) {
        voix_t *v = &V[i];
        if (!v->actif || !v->cle) continue;
        if (!v->tenue) { v->cible = 0; v->cle = 0; }
        v->tenue = 0;
    }
}

void eng_audio_mix(int16_t *out, int n, int rate) {
    const float glisse = 1.0f / (0.03f * (float)rate);   /* fondu de 30 ms */
    for (int k = 0; k < n; k++) out[k] = 0;
    if (rate <= 0 || hud_verrou_on()) return;   /* montre verrouillée : silence, la musique reprend où elle était */
    for (int i = 0; i < VOIX; i++) {
        voix_t *v = &V[i];
        if (!v->actif) continue;
        const son_t *s = &S[v->son];
        if (!s->pcm) { v->actif = 0; continue; }
        const uint32_t fin = s->n << 16, pas = (uint32_t)(v->hauteur * (float)s->rate / (float)rate * 65536.0f);
        /* Voix reprise par le jeu pendant ce mixage (son_jouer, autre fil ou autre cœur) : le nouveau son avec la position de l'ancien
           lirait hors de son tableau (11/11/2026, crash au lancement d'un combat sur la LDK) ; la position est vérifiée avant toute lecture */
        if (v->pos >= fin) { v->actif = 0; continue; }
        const float g = general * canal[canal_de(v->son)];
        float vol = v->vol;
        for (int k = 0; k < n; k++) {
            const float c = v->cible;
            if (vol < c) { vol += glisse; if (vol > c) vol = c; } else if (vol > c) { vol -= glisse; if (vol < c) vol = c; }
            if (vol <= 0 && c <= 0 && !v->cle) { v->actif = 0; break; }
            const int a = s->pcm[v->pos >> 16];
            const int b = s->pcm[(v->pos >> 16) + 1 < s->n ? (v->pos >> 16) + 1 : (s->boucle ? 0 : v->pos >> 16)];
            const float x = a + (b - a) * (float)(v->pos & 0xFFFF) / 65536.0f;
            int m = out[k] + (int)(x * vol * g);
            out[k] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
            v->pos += pas;
            if (v->pos >= fin) {
                if (s->boucle && v->cle) v->pos -= fin;   /* un son de boucle joué en ponctuel ne boucle pas */
                else { v->actif = 0; break; }
            }
        }
        v->vol = vol;
    }
    music_mix(out, n, rate, general * canal[CANAL_MUSIQUE]);
}

/*
 * Musique : pistes de musique.bin (tools/audio/preparer_musique.py, format dans pak.h) lues à mesure sur la carte SD,
 * ou là où la plateforme les trouve (eng_music_attach). IMA ADPCM mono par blocs de 512 octets, environ 12 Ko/s.
 *
 * Le jeu (music_update, à chaque image) choisit la piste voulue et remplit un anneau de 32 blocs (1,3 s) ; la tâche
 * audio (music_mix) les décode et les ajoute aux sons. Piste voulue, de la plus forte à la plus faible :
 *   - course en cours : piste des courses ;
 *   - combat : victoire, défaite, piste de l'ennemi (les boss), sinon celle des combats ;
 *   - instruction « musique » du scénario, jusqu'à « musique auto » (« musique aucune » : silence) ;
 *   - zone de musique où se trouve Sera (la plus petite) ;
 *   - piste par défaut.
 * Les noms viennent du plan de musique du scénario (section SCEN) ; sans plan : la première piste, puis celles qui
 * s'appellent combat, victoire, defaite et course. Changement de piste : fondu de sortie (court avant un combat), puis
 * la nouvelle part ; une piste quittée pour un combat ou une course reprend où elle en était.
 */
#include <math.h>
#include <string.h>
#include "internal.h"

#define MBLK 512                  /* octets par bloc */
#define MSPB ((MBLK - 4) * 2)     /* échantillons par bloc */
#define RING 32
#define MCACHE 64
#define MUS_VOL 0.55f             /* la musique sous les sons */
#define SANS_BOUCLE 0xFFFFFFFFu

typedef struct { char nom[24]; uint32_t off, nblk, nsamp, loop; float vol; } mtrack_t;

static const int16_t STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t IDX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static struct {
    int (*read)(uint32_t off, void *dst, uint32_t len);
    mtrack_t *t; int n, cap, rate;
    uint8_t (*ring)[MBLK];
    uint16_t rskip[RING], rcnt[RING];   /* échantillons à sauter (début de boucle), à jouer dans chaque bloc */
    volatile uint32_t wr, rd;           /* blocs écrits par le jeu, décodés par la tâche audio */
    volatile int muet;                  /* tâche audio tue : le jeu peut vider l'anneau et changer de piste */
    volatile float vol, cible, glisse_s;
    int cur, cur_combat, fin;           /* piste en cours (-1 aucune), voulue par un combat ou une course, fin lue */
    uint32_t next, skip;                /* prochain bloc à lire ; échantillons à sauter dans ce bloc */
    int reprise_p; uint32_t reprise_b;  /* piste quittée pour un combat ou une course, et son bloc */
    int32_t cache_s[MCACHE], cache_t[MCACHE];   /* chaîne du scénario -> piste */
    int16_t pcm[MSPB]; int pn, pi; uint32_t frac;   /* tâche audio : bloc décodé, position */
} M = { .cur = -1, .muet = 1, .reprise_p = -1 };

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void music_reset_names(void) { for (int k = 0; k < MCACHE; k++) M.cache_s[k] = -1; }

void eng_music_attach(uint32_t size, int (*read)(uint32_t off, void *dst, uint32_t len)) {
    uint8_t h[48];
    M.muet = 1; M.cur = -1; M.n = 0; M.read = NULL; M.reprise_p = -1;
    music_reset_names();
    if (!read || size < 16 || read(0, h, 16) || memcmp(h, "MUS1", 4) || rd32(h + 12) != MBLK) return;
    const uint32_t n = rd32(h + 4), rate = rd32(h + 8);
    if (!n || n > 255 || !rate || !E.cfg.alloc) return;
    if (!M.ring && !(M.ring = E.cfg.alloc(RING * MBLK, 0))) return;
    if ((int)n > M.cap) { if (!(M.t = E.cfg.alloc(sizeof(mtrack_t) * n, 0))) return; M.cap = (int)n; }
    for (uint32_t k = 0; k < n; k++) {
        if (read(16 + 48 * k, h, 48)) return;
        mtrack_t *t = &M.t[k];
        memcpy(t->nom, h, 23); t->nom[23] = 0;
        t->off = rd32(h + 24); t->nblk = rd32(h + 28); t->nsamp = rd32(h + 32); t->loop = rd32(h + 36);
        t->vol = (h[40] ? h[40] : 100) / 100.0f;
        if ((uint64_t)t->off + (uint64_t)t->nblk * MBLK > size || t->nsamp > t->nblk * MSPB) t->nblk = t->nsamp = 0;
    }
    M.rate = (int)rate; M.read = read; M.n = (int)n;
}

static int find(const char *s) {
    for (int k = 0; k < M.n; k++) if (!strcmp(M.t[k].nom, s)) return k;
    return -1;
}

/* Piste dont le nom est la chaîne s du scénario (-1 : aucune) */
static int track(int s) {
    if (s < 0 || s >= 65534 || !M.n) return -1;
    const int c = s % MCACHE;
    if (M.cache_s[c] != s) {
        char b[24];
        script_string(s, b, sizeof b);
        M.cache_s[c] = s; M.cache_t[c] = find(b);
    }
    return M.cache_t[c];
}

/* Piste voulue ; *combat : voulue par un combat ou une course (la piste quittée reprendra) */
static int wanted(int *combat) {
    const world_t *w = &E.w;
    const int plan = w->mus_plan;
    *combat = 0;
    if (accueil_on()) { const char *m = accueil_musique(); return m && m[0] ? find(m) : -1; }   /* écran titre, introduction */
    *combat = 1;
    if (CR.on) return plan ? track(w->mus_course) : find("course");
    if (BT.on) {
        if (BT.phase == BP_WIN) return plan ? track(w->mus_victoire) : find("victoire");
        if (BT.phase == BP_LOSE) return plan ? track(w->mus_defaite) : find("defaite");
        for (int e = 0; e < w->mus_ne; e++) if (w->mus_e[e].perso == BT.perso) return track(w->mus_e[e].piste);
        return plan ? track(w->mus_combat) : find("combat");
    }
    *combat = 0;
    if (E.mus_force == 65534) return -1;
    if (E.mus_force != 65535) return track(E.mus_force);
    /* Autre carte : sa piste (les zones ne valent que sur la planète) */
    if (w->carte) return plan && w->carte < 16 && w->mus_carte[w->carte] != 65535 ? track(w->mus_carte[w->carte]) : plan ? track(w->mus_defaut) : 0;
    int best = -1; float br = 1e9f;
    for (int z = 0; z < w->mus_nz; z++) {
        const mus_zone_t *Z = &w->mus_z[z];
        if (Z->r < br && hypotf(E.sera.x - Z->x, E.sera.z - Z->z) < Z->r) { best = z; br = Z->r; }
    }
    if (best >= 0) return track(w->mus_z[best].piste);
    return plan ? track(w->mus_defaut) : 0;
}

/* Remplit l'anneau (thread du jeu : c'est lui qui lit la carte SD) */
static void fill(void) {
    if (M.cur < 0 || M.fin) return;
    const mtrack_t *t = &M.t[M.cur];
    while (M.wr - M.rd < RING) {
        if (M.next >= t->nblk) {
            if (t->loop == SANS_BOUCLE || t->loop >= t->nsamp) { M.fin = 1; return; }
            M.next = t->loop / MSPB; M.skip = t->loop % MSPB;
        }
        const int s = (int)(M.wr % RING);
        if (M.read(t->off + M.next * MBLK, M.ring[s], MBLK)) { M.fin = 1; return; }   /* illisible : la piste s'arrête */
        const uint32_t done = M.next * MSPB;
        M.rskip[s] = (uint16_t)M.skip;
        M.rcnt[s] = (uint16_t)(t->nsamp - done < MSPB ? t->nsamp - done : MSPB);
        M.skip = 0; M.next++;
        __sync_synchronize();
        M.wr++;
    }
}

/* Piste p (tâche audio tue) : depuis le début, ou d'où elle avait été quittée pour un combat */
static void start(int p, int combat) {
    M.wr = M.rd = 0; M.pn = M.pi = 0; M.frac = 0; M.fin = 0; M.skip = 0; M.next = 0;
    M.cur = p; M.cur_combat = combat;
    if (p < 0 || !M.t[p].nblk) { M.cur = p < 0 ? -1 : p; return; }
    if (p == M.reprise_p && !combat) { M.next = M.reprise_b < M.t[p].nblk ? M.reprise_b : 0; M.reprise_p = -1; }
    M.vol = 0; M.cible = M.t[p].vol; M.glisse_s = combat ? 0.05f : 0.8f;
    fill();
    __sync_synchronize();
    M.muet = 0;
}

void music_update(float dt) {
    (void)dt;   /* les fondus suivent les échantillons mixés, pas le temps de jeu */
    if (!M.n) return;
    int combat;
    const int w = wanted(&combat);
    if (w != M.cur) {
        if (!M.muet) { M.glisse_s = combat ? 0.3f : 1.2f; M.cible = 0; }   /* fondu de sortie ; la suite quand la tâche audio s'est tue */
        else {
            if (M.cur >= 0 && !M.cur_combat && combat) {   /* la piste du monde reprendra après le combat */
                const uint32_t pend = M.wr - M.rd;
                M.reprise_p = M.cur; M.reprise_b = M.next >= pend ? M.next - pend : 0;
            }
            start(w, combat);
        }
    } else if (w >= 0 && M.cible <= 0) {   /* revenue pendant son fondu de sortie */
        if (M.muet) start(w, combat);
        else { M.cible = M.t[w].vol; M.glisse_s = 0.8f; }
    }
    fill();
}

static void decode(int s) {
    const uint8_t *b = M.ring[s];
    int pred = (int16_t)(b[0] | (b[1] << 8)), idx = b[2] > 88 ? 88 : b[2], n = 0;
    const int skip = M.rskip[s], cnt = M.rcnt[s];
    for (int i = 0; i < cnt; i++) {
        const int code = (b[4 + (i >> 1)] >> ((i & 1) * 4)) & 15, step = STEPS[idx];
        int dq = step >> 3;
        if (code & 4) dq += step;
        if (code & 2) dq += step >> 1;
        if (code & 1) dq += step >> 2;
        pred += code & 8 ? -dq : dq;
        pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
        idx += IDX[code & 7];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        if (i >= skip) M.pcm[n++] = (int16_t)pred;
    }
    M.pn = n; M.pi = 0;
}

/* Tâche audio : ajoute la musique aux n échantillons déjà mixés (gain : volume général) */
void music_mix(int16_t *out, int n, int rate, float gain) {
    if (M.muet || !M.n || rate <= 0) return;
    const uint32_t pas = (uint32_t)((float)M.rate / (float)rate * 65536.0f);
    const float c = M.cible, gl = 1.0f / ((M.glisse_s > 0.01f ? M.glisse_s : 0.01f) * (float)rate), g = gain * MUS_VOL;
    float vol = M.vol;
    for (int k = 0; k < n; k++) {
        if (vol < c) { vol += gl; if (vol > c) vol = c; } else if (vol > c) { vol -= gl; if (vol < c) vol = c; }
        if (vol <= 0 && c <= 0) { M.vol = 0; M.muet = 1; return; }
        while (M.pi >= M.pn && M.rd != M.wr) {   /* bloc suivant ; anneau vide : silence (fin de piste ou lecture en retard) */
            const int over = M.pi - M.pn;
            __sync_synchronize();
            decode((int)(M.rd % RING));
            M.pi = over;
            M.rd++;
        }
        if (M.pi >= M.pn) continue;
        const int a = M.pcm[M.pi], b = M.pi + 1 < M.pn ? M.pcm[M.pi + 1] : a;
        const float x = (float)a + (float)(b - a) * (float)M.frac / 65536.0f;
        const int m = out[k] + (int)(x * vol * g);
        out[k] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        M.frac += pas;
        M.pi += (int)(M.frac >> 16);
        M.frac &= 0xFFFF;
    }
    M.vol = vol;
}

const char *music_now(void) { return M.cur >= 0 && M.n ? M.t[M.cur].nom : "-"; }

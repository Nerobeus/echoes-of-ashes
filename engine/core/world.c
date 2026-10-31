/* Chargement du paquet monde, chunks autour de Sera et requêtes sur le terrain */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include "internal.h"
#include "ui.h"
#include "audio.h"

eng_t E ENG_FROID;

#define TILE_WATER 5   /* tuile d'eau animée (rivières dans les morceaux de route) */
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }

static void *alloc(size_t size, int fast) {
    void *p = E.cfg.alloc(size, fast);
    if (p) memset(p, 0, size);
    return p;
}

/* Longueur d'une chaîne du paquet, sans dépasser end */
static size_t str_len(const char *s, const char *end) { const char *e = s; while (e < end && *e) e++; return (size_t)(e - s); }

/* Section d'étiquette tag, ou NULL */
static const uint8_t *section(const uint8_t *pak, size_t len, const char *tag, uint32_t *size) {
    size_t o = 8;
    while (o + 8 <= len) {
        uint32_t l = rd32(pak + o + 4);
        if (!memcmp(pak + o, tag, 4)) { if (size) *size = l; return pak + o + 8; }
        o += 8 + l;
    }
    return NULL;
}

/* Octets du paquet : par la plateforme (flash, carte SD), sinon dans le paquet en mémoire */
static int pak_read(uint32_t off, void *dst, uint32_t len) {
    if (E.cfg.pak_read) return E.cfg.pak_read(off, dst, len);
    if ((size_t)off + len > E.pak_len) return -1;
    memcpy(dst, E.pak + off, len);
    return 0;
}

/* ---------- Planches et textures lues à la demande (carte SD) ----------
   Le paquet léger de la flash (SPRX, TEXS) garde les petites sections ; les pixels des planches et les textures du sol
   restent dans monde.pak sur la carte SD, lus par pak_read au moment où le cœur 0 prépare une image qui en a besoin.
   Une place servie à l'image f (sframe) ou à la précédente, que l'autre cœur dessine encore, n'est jamais reprise. */
/* GT_SLOTS : textures du sol de 1 Ko (116 au plus à moins de 8 cases de Sera, monde de 512) ; GT_PAR_IMAGE : lectures
   de textures par image (toutes après une téléportation) ; réglables à la compilation (essais) */
#ifndef GT_SLOTS
#define GT_SLOTS 512
#endif
#ifndef GT_PAR_IMAGE
#define GT_PAR_IMAGE 24
#endif
#define ST_NCLASS 6              /* planches : places par taille, une direction (fh lignes de fw × frames) chacune */
static const uint32_t ST_SIZE[ST_NCLASS] = { 4096, 8192, 16384, 32768, 65536, 131072 };
/* 1,5 Mo. Places servies au plus fort (rendu_test, ECHOES_PLANCHES=1, le 29/10/2026 : rues de Lucerne, combat, course,
   intérieur) : 4 Ko 12, 8 Ko 6, 16 Ko 1 à 2, 32 Ko 4, 64 Ko 3, 128 Ko 0 ; les personnages immobiles (blocs d'attente de
   4 à 8 Ko) prennent le plus de places, ceux qui marchent la classe de 32 Ko (avant : 16, 16, 20, 16, 6, 2, 1,6 Mo) */
static const int ST_COUNT[ST_NCLASS] = { 24, 16, 8, 16, 6, 2 };
static uint32_t st_base[ST_NCLASS]; static int st_i0[ST_NCLASS + 1];

void stream_frame(void) {
    world_t *w = &E.w;
    w->sframe++;
    w->s_tex_budget = GT_PAR_IMAGE; w->s_spr_budget = 1;
    if (w->s_sync) w->s_sync--;
}

const uint8_t *ground_tex(int k) {
    const world_t *w = &E.w;
    if (!w->stream) return w->tex + w->ground_off + (uint32_t)k * 1024;
    const int s = k >= 0 && k < w->gt_n ? w->gt_slot[k] : -1;
    return w->gt_mem + (uint32_t)(s >= 0 ? s : 0) * 1024;   /* place 0 : texture de remplacement (la première) */
}

/* Texture du sol k en cache ; protect : servie à l'image en préparation (sinon, source d'une texture réduite copiée
   tout de suite : la place reste reprenable, une téléportation peut en lire plus que le cache n'en tient) */
static int ground_load(int k, int now, int protect) {
    world_t *w = &E.w;
    if (!w->stream) return 1;
    if (k < 0 || k >= w->gt_n) return 0;
    const int s = w->gt_slot[k];
    if (s >= 0) { if (protect) w->gt_used[s] = w->sframe; return 1; }
    if (!now && !w->s_sync && w->s_tex_budget <= 0) return 0;
    int best = -1; uint32_t bu = 0xFFFFFFFFu;
    for (int i = 1; i < w->gt_nsl; i++) {
        if (w->gt_who[i] == -1) { best = i; break; }
        if (w->gt_used[i] + 1 < w->sframe && w->gt_used[i] < bu) { bu = w->gt_used[i]; best = i; }
    }
    if (best < 0) return 0;
    if (w->gt_who[best] >= 0) w->gt_slot[w->gt_who[best]] = -1;
    w->gt_who[best] = -1;
    if (pak_read(w->gt_off + (uint32_t)k * 1024, w->gt_mem + (uint32_t)best * 1024, 1024)) return 0;
    w->gt_who[best] = k; w->gt_slot[k] = (int16_t)best; w->gt_used[best] = protect ? w->sframe : 0;
    w->s_tex_budget--;
    return 1;
}
int ground_need(int k, int now) { return ground_load(k, now, 1); }

/* Vide sous la direction d (pixels sous la rangée la plus basse avec un pixel opaque, toutes images) */
static uint8_t sheet_foot(const sheet_t *sh, const uint8_t *blk) {
    const int sw = sh->frames * sh->fw;
    for (int y = sh->fh - 1; y >= 0; y--) {
        const uint8_t *r = blk + (size_t)y * sw;
        for (int x = 0; x < sw; x++) if (r[x]) return (uint8_t)(sh->fh - 1 - y > 255 ? 0 : sh->fh - 1 - y);
    }
    return 0;
}

const uint8_t *sheet_block(int s, int d, int load) {
    world_t *w = &E.w;
    if (s < 0 || s >= w->nsheets || !w->sheets[s].ok) return NULL;
    sheet_t *sh = &w->sheets[s];
    d &= 7;
    const uint32_t sz = (uint32_t)sh->fw * sh->frames * sh->fh;
    if (sh->data) return sh->data + (size_t)d * sz;
    if (sh->sl[d] >= 0) {
        const int i = sh->sl[d];
        int c = 0; while (i >= st_i0[c + 1]) c++;
        if (load) w->st_used[i] = w->sframe;
        return w->st_mem + st_base[c] + (uint32_t)(i - st_i0[c]) * ST_SIZE[c];
    }
    if (!load || (load == 1 && !w->s_sync && w->s_spr_budget <= 0) || !w->st_mem) return NULL;
    int c = 0; while (c < ST_NCLASS && ST_SIZE[c] < sz) c++;
    if (c == ST_NCLASS) return NULL;
    int best = -1; uint32_t bu = 0xFFFFFFFFu;
    for (int i = st_i0[c]; i < st_i0[c + 1]; i++) {
        if (w->st_who[i] < 0) { best = i; break; }
        if (w->st_used[i] + 1 < w->sframe && w->st_used[i] < bu) { bu = w->st_used[i]; best = i; }
    }
    if (best < 0) return NULL;
    if (w->st_who[best] >= 0) w->sheets[w->st_who[best] >> 3].sl[w->st_who[best] & 7] = -1;
    w->st_who[best] = -1;
    uint8_t *m = w->st_mem + st_base[c] + (uint32_t)(best - st_i0[c]) * ST_SIZE[c];
    if (pak_read(sh->foff + (uint32_t)d * sz, m, sz)) return NULL;
    if (!(sh->footok >> d & 1)) { sh->foot[d] = sheet_foot(sh, m); sh->footok |= (uint8_t)(1 << d); }
    w->st_who[best] = s << 3 | d; sh->sl[d] = (int16_t)best; w->st_used[best] = w->sframe;
    w->s_spr_budget--;
    return m;
}

/* Caches de la lecture à la demande (après SPRX et TEXS) ; la texture de remplacement dans la place 0 */
static int stream_init(void) {
    world_t *w = &E.w;
    if (!w->stream) return 0;
    w->gt_nsl = w->gt_n < GT_SLOTS ? (w->gt_n > 0 ? w->gt_n + 1 : 1) : GT_SLOTS;
    w->gt_slot = E.cfg.alloc(sizeof(int16_t) * (size_t)(w->gt_n > 0 ? w->gt_n : 1), 0);
    w->gt_who = E.cfg.alloc(sizeof(int32_t) * (size_t)w->gt_nsl, 0); w->gt_used = E.cfg.alloc(sizeof(uint32_t) * (size_t)w->gt_nsl, 0);
    w->gt_mem = E.cfg.alloc(1024u * (uint32_t)w->gt_nsl, 0);
    if (!w->gt_slot || !w->gt_who || !w->gt_used || !w->gt_mem) return -1;
    for (int k = 0; k < w->gt_n; k++) w->gt_slot[k] = -1;
    for (int i = 0; i < w->gt_nsl; i++) { w->gt_who[i] = -1; w->gt_used[i] = 0; }
    w->gt_who[0] = -2;
    if (w->gt_n > 0 && pak_read(w->gt_off, w->gt_mem, 1024)) return -1;
    uint32_t tot = 0; int n = 0;
    for (int c = 0; c < ST_NCLASS; c++) { st_base[c] = tot; st_i0[c] = n; tot += ST_SIZE[c] * (uint32_t)ST_COUNT[c]; n += ST_COUNT[c]; }
    st_i0[ST_NCLASS] = n;
    w->st_nsl = n;
    w->st_mem = E.cfg.alloc(tot, 0); w->st_who = E.cfg.alloc(sizeof(int32_t) * (size_t)n, 0); w->st_used = E.cfg.alloc(sizeof(uint32_t) * (size_t)n, 0);
    if (!w->st_mem || !w->st_who || !w->st_used) return -1;
    for (int i = 0; i < n; i++) { w->st_who[i] = -1; w->st_used[i] = 0; }
    w->sframe = 2;
    return 0;
}

/* Décompression LZ4 (format bloc) ; renvoie la taille décompressée, -1 si les données sont incohérentes */
int lz4_decode(const uint8_t *src, int slen, uint8_t *dst, int dcap) {
    const uint8_t *ip = src, *iend = src + slen;
    uint8_t *op = dst, *oend = dst + dcap;
    while (ip < iend) {
        int tok = *ip++, lit = tok >> 4;
        if (lit == 15) { int b; do { if (ip >= iend) return -1; b = *ip++; lit += b; } while (b == 255); }
        if (lit > iend - ip || lit > oend - op) return -1;
        memcpy(op, ip, lit); op += lit; ip += lit;
        if (ip >= iend) break;                 /* dernières littérales */
        if (iend - ip < 2) return -1;
        int off = ip[0] | (ip[1] << 8), ml = (tok & 15) + 4;
        ip += 2;
        if (off == 0 || off > op - dst) return -1;
        if ((tok & 15) == 15) { int b; do { if (ip >= iend) return -1; b = *ip++; ml += b; } while (b == 255); }
        if (ml > oend - op) return -1;
        const uint8_t *m = op - off;
        while (ml--) *op++ = *m++;             /* la copie peut se recouvrir : octet par octet */
    }
    return (int)(op - dst);
}

/* Place vide (chunk illisible) : rien n'y est dessiné et on ne peut pas y marcher */
static void chunk_empty(chunk_t *c, int cx, int cz) {
    memset(c->hv, 0, sizeof c->hv); memset(c->lv, 0, sizeof c->lv);
    for (int k = 0; k < CS * CS; k++) c->water_h[k] = -1e9f;
    memset(c->flags, 0, sizeof c->flags); memset(c->water, 0, sizeof c->water); memset(c->hidden, 1, sizeof c->hidden);
    memset(c->road_count, 0, sizeof c->road_count); memset(c->plant_cnt, 0, sizeof c->plant_cnt);
    for (int k = 0; k < (CS / 2) * (CS / 2); k++) c->lod[k] = 0xFFFF;
    c->npoly = c->rail_count = c->box_count = c->nbld = c->nsolid = c->nplant = 0;
    c->cx = cx; c->cz = cz;
}

/* Texture réduite du bloc k (2 × 2 cases) : un texel sur deux de chaque case, dans la zone lodbuf du chunk. Une zone
   ne change de chunk que quand le sien sort des 3 × 3 chunks autour de Sera, à plus de 16 cases : le cœur 1, qui
   dessine encore l'image précédente, n'y lit jamais une texture en cours de remplacement (sauf après une
   téléportation). */
static void lod_build(chunk_t *c, int k) {
    const world_t *w = &E.w;
    const int bx = k % (CS / 2), bz = k / (CS / 2);
    uint8_t *t = w->lodbuf[c->lodbuf] + k * 1024;
    for (int dj = 0; dj < 2; dj++) for (int di = 0; di < 2; di++) {
        int tile = c->tile[(bz * 2 + dj) * CS + bx * 2 + di];
        if (tile < (int)w->g0 || tile - (int)w->g0 >= w->gt_n) { c->lod[k] = 0xFFFF; return; }
        if (!ground_load(tile - (int)w->g0, 0, 0)) return;   /* source pas encore lue (carte SD) : bloc gardé en attente */
        const uint8_t *src = ground_tex(tile - (int)w->g0);
        uint8_t *dst = t + dj * 16 * 32 + di * 16;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) dst[y * 32 + x] = src[y * 64 + x * 2];
    }
    c->lod[k] = (uint16_t)(TILE_LOD + c->lodbuf * 64 + k);
}

/* ---------- Bloc de la fenêtre : chaque chunk y prend sa taille ---------- */
/* Octets que demande un chunk développé (CMEM : u16 en unités de 16 octets ; sans elle, le plus gros), multiple de 16 */
static uint32_t taille_cmem(const uint8_t *q) { return ((uint32_t)(q[0] | q[1] << 8) * 16 + 4 + 15) & ~15u; }
/* Taille du bloc en % de la pire fenêtre (100 : juste ce qu'il faut ; plus : des chunks sortis de la fenêtre restent
   en mémoire plus longtemps, comme avec les places égales) */
#ifndef FEN_MARGE
#define FEN_MARGE 100
#endif
/* La fenêtre grandit pour l'intérieur le plus gros (intr_need) jusqu'à INTR_FEN_MAX octets ; un intérieur qui demande plus
   n'est pas visitable (int_parse, « Intérieur trop grand pour la mémoire »). Montre : 2 Mo (CMakeLists du firmware ; le
   31/10/2026, le cinéma et le centre commercial de Lucerne demandaient 3,7 Mo et le jeu ne démarrait plus) ; ailleurs,
   sans limite */
#ifndef INTR_FEN_MAX
#define INTR_FEN_MAX 0xFFFFFFFFu
#endif
static uint32_t chunk_besoin(int cx, int cz) {
    const world_t *w = &E.w;
    return w->cmem ? taille_cmem(w->cmem + ((size_t)cz * w->nc + cx) * 2) : (w->mem_max + 4 + 15) & ~15u;
}
/* Plus grande somme des CWIN × CWIN chunks d'une fenêtre (bord de la carte compris) d'une table CMEM */
static uint32_t fen_pire(const uint8_t *t, int nc) {
    uint32_t pire = 0;
    for (int cz = 0; cz < nc; cz++) for (int cx = 0; cx < nc; cx++) {
        uint32_t s = 0;
        for (int dz = -CWIN / 2; dz <= CWIN / 2; dz++) for (int dx = -CWIN / 2; dx <= CWIN / 2; dx++) {
            const int x = cx + dx, z = cz + dz;
            if (x >= 0 && z >= 0 && x < nc && z < nc) s += taille_cmem(t + ((size_t)z * nc + x) * 2);
        }
        if (s > pire) pire = s;
    }
    return pire;
}
/* Chunk déplacé dans le bloc (tassement) : ses données, puis les pointeurs qui y mènent (le reste est en décalages).
   Le cœur 1 ne lit jamais les chunks (seulement les primitives de l'image), et rien n'en garde d'adresse d'une image
   à l'autre */
static void chunk_deplacer(chunk_t *c, uint8_t *dst) {
    const ptrdiff_t d = dst - c->mem;
    memmove(dst, c->mem, c->mlen);
    c->mem = dst;
    if (c->poly_base) c->poly_base += d;
    if (c->poly_off) c->poly_off = (const uint32_t *)((const uint8_t *)c->poly_off + d);
    if (c->bld) c->bld = (const pak_building_t *)((const uint8_t *)c->bld + d);
    if (c->solids) c->solids = (const float *)((const uint8_t *)c->solids + d);
    if (c->plants) c->plants = (const pak_plant_t *)((const uint8_t *)c->plants + d);
}
/* Place de need octets : le premier trou assez grand entre les chunks en mémoire (rangés par adresse) ; sinon le bloc
   est tassé vers le début et la place prise à la fin. NULL : la fenêtre en tient trop (world_stream lâche alors les
   chunks sortis de la fenêtre, puis réessaie) */
static uint8_t *fen_alloc(uint32_t need) {
    world_t *w = &E.w;
    chunk_t *pris[CWIN * CWIN];
    int n = 0;
    for (int s = 0; s < CWIN * CWIN; s++) if (w->slot[s]->mem) pris[n++] = w->slot[s];
    for (int i = 1; i < n; i++) { chunk_t *t = pris[i]; int j = i; while (j > 0 && pris[j - 1]->mem > t->mem) { pris[j] = pris[j - 1]; j--; } pris[j] = t; }
    uint8_t *cur = w->fen_mem, *const end = w->fen_mem + w->fen_cap;
    for (int i = 0; i < n; i++) {
        if ((size_t)(pris[i]->mem - cur) >= need) return cur;
        cur = pris[i]->mem + pris[i]->mlen;
    }
    if ((size_t)(end - cur) >= need) return cur;
    cur = w->fen_mem;
    for (int i = 0; i < n; i++) { if (pris[i]->mem != cur) chunk_deplacer(pris[i], cur); cur += pris[i]->mlen; }
    return (size_t)(end - cur) >= need ? cur : NULL;
}
static void chunk_lacher(chunk_t *c) { c->mem = NULL; c->mlen = 0; c->poly_base = NULL; c->poly_off = NULL; c->bld = NULL; c->solids = NULL; c->plants = NULL; }

/* Décompresse le chunk (cx, cz) dans la place c, puis le développe au format du rendu (flottants, coordonnées monde) ;
   -2 : pas de place dans le bloc de la fenêtre */
static void lod_release(chunk_t *c);
static int chunk_load(chunk_t *c, int cx, int cz) {
    const world_t *w = &E.w;
    lod_release(c);
    c->cx = c->cz = -1;
    chunk_lacher(c);
    const uint32_t need = chunk_besoin(cx, cz);
    if (!(c->mem = fen_alloc(need))) return -2;
    c->mlen = need;
    const uint8_t *d = w->chunk_dir + (cz * w->nc + cx) * 12;
    uint32_t off = rd32(d), clen = rd32(d + 4), rlen = rd32(d + 8);
    if (clen > w->comp_max || rlen > w->raw_max) return -1;
    if (pak_read(w->cdat_off + off, w->zbuf, clen)) return -1;
    if (lz4_decode(w->zbuf, (int)clen, w->rbuf, (int)rlen) != (int)rlen) return -1;

    const uint8_t *p = w->rbuf, *pend = w->rbuf + rlen;
    const int nroad = rd16(p), nrail = rd16(p + 2), nbox = rd16(p + 4), nbp = rd16(p + 6);
    const int nbld = rd16(p + 8), nsol = rd16(p + 10), npl = rd16(p + 12);
    p += 16;
    const float ox = (float)(cx * CS), oz = (float)(cz * CS);
    if (p + CV * CV * 3 + CS * CS * 6 > pend) return -1;
    for (int k = 0; k < CV * CV; k++) c->hv[k] = rds16(p + k * 2) / 512.0f;
    p += CV * CV * 2;
    for (int k = 0; k < CV * CV; k++) c->lv[k] = p[k] / 16.0f;
    p += CV * CV;
    for (int k = 0; k < CS * CS; k++) c->tile[k] = rd16(p + k * 2);
    p += CS * CS * 2;
    memcpy(c->flags, p, CS * CS); p += CS * CS;
    memcpy(c->water, p, CS * CS); p += CS * CS;
    memcpy(c->hidden, p, CS * CS); p += CS * CS;
    for (int k = 0, r = 0; k < CS * CS; k++) { c->road_count[k] = p[k]; c->road_first[k] = (uint16_t)r; r += p[k]; }
    p += CS * CS;
    for (int k = 0; k < CS * CS; k++) {
        c->road_h[k] = NAN;
        if (!(c->flags[k] & F_ROAD)) continue;
        if (p + 2 > pend) return -1;
        int16_t v = rds16(p); p += 2;
        if (v != -32768) c->road_h[k] = v / 512.0f;
    }
    if (p + CS * CS * 2 + (CS / 2) * (CS / 2) * 2 > pend) return -1;
    /* Niveau de la mer recalé (arrondi au 1/512 dans le paquet) : ses sommets passent par le cache */
    for (int k = 0; k < CS * CS; k++) {
        int16_t v = rds16(p + k * 2);
        float h = v == -32768 ? -1e9f : v / 512.0f;
        c->water_h[k] = fabsf(h - w->wl) < 0.004f ? w->wl : h;
    }
    p += CS * CS * 2;
    for (int k = 0; k < (CS / 2) * (CS / 2); k++) {
        c->lod[k] = rd16(p + k * 2);
        if (c->lod[k] != 0xFFFF) c->lod[k] = LOD_PENDING;   /* texture fabriquée plus tard (world_stream) */
    }
    p += (CS / 2) * (CS / 2) * 2;

    /* Polygones dans la réserve de la place : table des décalages, puis les polygones au format pak_poly_t */
    uint8_t *m = c->mem, *mend = c->mem + c->mlen;
    const uint32_t np = (uint32_t)(nroad + nrail + nbox + nbp);
    uint32_t *poly_off = (uint32_t *)m;
    m += np * 4;
    c->poly_base = m; c->poly_off = poly_off; c->npoly = np;
    for (uint32_t i = 0; i < np; i++) {
        if (p + 4 > pend) return -1;
        const uint8_t flags = p[2], nv = p[3];
        if (m + 16 + nv * 24 > mend || p + 4 + ((flags & POLY_CULL) ? 12 : 0) + nv * 12 > pend) return -1;
        poly_off[i] = (uint32_t)(m - c->poly_base);
        pak_poly_t *pp = (pak_poly_t *)m;
        pp->tile = rd16(p); pp->flags = flags; pp->nv = nv;
        p += 4;
        if (flags & POLY_CULL) { for (int k = 0; k < 3; k++) pp->n[k] = rdf(p + k * 4); p += 12; }
        else pp->n[0] = pp->n[1] = pp->n[2] = 0;
        /* Face de bâtiment : rang de son volume + 1 dans l'octet libre du premier sommet, gardé dans les bits hauts des drapeaux */
        if (nv) { const int rk = p[11] > POLY_RANK_MAX ? POLY_RANK_MAX : p[11]; pp->flags = (uint8_t)((flags & 3) | rk << POLY_RANK_SH); }
        for (int v = 0; v < nv; v++, p += 12) {
            pak_vert_t *q = &pp->v[v];
            q->x = ox + rds16(p) / 256.0f; q->y = rds16(p + 2) / 256.0f; q->z = oz + rds16(p + 4) / 256.0f;
            q->u = rds16(p + 6) / 4.0f; q->v = rds16(p + 8) / 4.0f; q->l = p[10] / 16.0f;
        }
        m += 16 + nv * 24;
    }
    c->rail_first = (uint32_t)nroad; c->rail_count = (uint32_t)nrail;
    c->box_first = (uint32_t)(nroad + nrail); c->box_count = (uint32_t)nbox;
    if (m + nbld * sizeof(pak_building_t) + nsol * 16 + npl * sizeof(pak_plant_t) > mend) return -1;
    if (p + nbld * 10 + nsol * 8 + npl * 12 != pend) return -1;
    pak_building_t *bld = (pak_building_t *)m;
    for (int i = 0; i < nbld; i++, p += 10) {
        bld[i].cx = ox + rds16(p) / 256.0f; bld[i].cz = oz + rds16(p + 2) / 256.0f; bld[i].r = rd16(p + 4) / 256.0f;
        bld[i].first = (uint32_t)(nroad + nrail + nbox) + rd16(p + 6); bld[i].count = rd16(p + 8);
    }
    c->bld = bld; c->nbld = (uint32_t)nbld;
    m += nbld * sizeof(pak_building_t);
    float *sol = (float *)m;
    for (int i = 0; i < nsol; i++, p += 8) {
        sol[i * 4] = ox + rds16(p) / 256.0f; sol[i * 4 + 1] = oz + rds16(p + 2) / 256.0f;
        sol[i * 4 + 2] = ox + rds16(p + 4) / 256.0f; sol[i * 4 + 3] = oz + rds16(p + 6) / 256.0f;
    }
    c->solids = sol; c->nsolid = (uint32_t)nsol;
    m += nsol * 16;
    pak_plant_t *pl = (pak_plant_t *)m;
    memset(c->plant_cnt, 0, sizeof c->plant_cnt);
    for (int i = 0; i < npl; i++, p += 12) {
        int lc = p[0];
        pl[i].cell = (uint32_t)lc;
        pl[i].x = ox + lc % CS + p[1] / 256.0f; pl[i].z = oz + lc / CS + p[2] / 256.0f;
        pl[i].flip = p[3] & 1; pl[i].flags = p[3];
        pl[i].size = rd16(p + 4) / 256.0f; pl[i].tile = rd16(p + 6);
        pl[i].phase = p[8] * (2 * PI_F / 256); pl[i].l = p[9] / 16.0f; pl[i].y = rds16(p + 10) / 512.0f;
        if (!c->plant_cnt[lc]) c->plant_first[lc] = (uint16_t)i;
        c->plant_cnt[lc]++;
    }
    c->plants = pl; c->nplant = (uint32_t)npl;
    c->cx = cx; c->cz = cz;
    return 0;
}

#define LOD_PER_FRAME 16            /* textures réduites fabriquées par image (64 Ko lus par chunk entier) */

/* Zone de textures réduites rendue par un chunk : ses blocs repassent case par case (en attente) */
static void lod_release(chunk_t *c) {
    if (c->lodbuf < 0) return;
    E.w.lodbuf_slot[c->lodbuf] = -1;
    c->lodbuf = -1;
    for (int k = 0; k < (CS / 2) * (CS / 2); k++) if (c->lod[k] != 0xFFFF) c->lod[k] = LOD_PENDING;
}

/* Ordre de chargement de la fenêtre : du chunk de Sera vers les bords */
static int8_t ring_dx[CWIN * CWIN], ring_dz[CWIN * CWIN];
static void ring_init(void) {
    int n = 0;
    for (int r = 0; r <= CWIN / 2; r++)
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++)
            if (abs(dx) == r || abs(dz) == r) { ring_dx[n] = (int8_t)dx; ring_dz[n] = (int8_t)dz; n++; }
}

void world_vider_fenetre(void) {
    world_t *w = &E.w;
    for (int s = 0; s < CWIN * CWIN; s++) { lod_release(w->slot[s]); w->slot[s]->cx = w->slot[s]->cz = -1; chunk_lacher(w->slot[s]); }
}

void world_stream(float x, float z, int sync) {
    world_t *w = &E.w;
    /* Bloc de la fenêtre tenu par un intérieur (visite finie, ou combat pendant la visite) : repris, places vides,
       toutes chargées tout de suite ; l'intérieur, s'il est encore visité, sera relu (int_pret) */
    if (w->fen_int) { w->fen_int = 0; world_vider_fenetre(); sync = 1; }
    const int ccx = (int)floorf(x / CS), ccz = (int)floorf(z / CS);
    uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    int budget = sync ? CWIN * CWIN : 1, loaded = 0;
    if (sync && w->stream) w->s_sync = 3;   /* textures et planches de la nouvelle vue : lues sans limite */
    for (int k = 0; k < CWIN * CWIN; k++) {
        int cx = ccx + ring_dx[k], cz = ccz + ring_dz[k];
        if ((unsigned)cx >= (unsigned)w->nc || (unsigned)cz >= (unsigned)w->nc || chunk_at(cx, cz)) continue;
        /* Les chunks qui touchent celui de Sera sont visibles tout de suite : jamais remis à plus tard */
        if (budget <= 0 && (abs(ring_dx[k]) > 1 || abs(ring_dz[k]) > 1)) break;
        chunk_t *c = w->slot[(cz % CWIN) * CWIN + cx % CWIN];
        int r = chunk_load(c, cx, cz);
        if (r == -2) {   /* bloc plein : les chunks sortis de la fenêtre (pas encore remplacés) lâchent leur place */
            for (int s = 0; s < CWIN * CWIN; s++) {
                chunk_t *o = w->slot[s];
                if (o->mem && (o->cx < 0 || abs(o->cx - ccx) > CWIN / 2 || abs(o->cz - ccz) > CWIN / 2)) { lod_release(o); o->cx = o->cz = -1; chunk_lacher(o); }
            }
            r = chunk_load(c, cx, cz);
        }
        if (r) { E.error = r == -2 ? "fenêtre de chunks pleine" : "chunk illisible"; chunk_lacher(c); chunk_empty(c, cx, cz); }
        budget--; loaded++;
    }
    /* Zones de textures réduites : rendues par les chunks sortis des 3 × 3 autour de Sera, prises par ceux qui y entrent */
    for (int s = 0; s < CWIN * CWIN; s++) {
        chunk_t *c = w->slot[s];
        if (c->lodbuf >= 0 && (c->cx < 0 || abs(c->cx - ccx) > 1 || abs(c->cz - ccz) > 1)) lod_release(c);
    }
    for (int k = 0; k < 9; k++) {
        const chunk_t *cc = chunk_at(ccx + ring_dx[k], ccz + ring_dz[k]);
        if (!cc || cc->lodbuf >= 0) continue;
        int b = 0;
        while (b < LOD_BUFS && w->lodbuf_slot[b] >= 0) b++;
        if (b == LOD_BUFS) break;
        w->lodbuf_slot[b] = (int8_t)cc->slot;
        w->slot[cc->slot]->lodbuf = b;
    }
    /* Textures réduites en attente : LOD_PER_FRAME blocs par image (toutes si sync), chunk de Sera d'abord. Un chunk
       entre dans les 3 × 3 à plus de 16 cases de Sera, avant d'être vu de loin : d'ici là, ses blocs restent case par case. */
    int lod = sync ? 1 << 30 : LOD_PER_FRAME;
    for (int k = 0; k < 9 && lod > 0; k++) {
        const chunk_t *cc = chunk_at(ccx + ring_dx[k], ccz + ring_dz[k]);
        if (!cc || cc->lodbuf < 0) continue;
        chunk_t *c = w->slot[cc->slot];
        for (int b = 0; b < (CS / 2) * (CS / 2) && lod > 0; b++) if (c->lod[b] == LOD_PENDING) { lod_build(c, b); lod--; }
    }
    if (loaded || lod < LOD_PER_FRAME) {
        E.stream_loads += (uint32_t)loaded;
        if (E.cfg.clock_us) E.stream_us += E.cfg.clock_us() - t0;
    }
}

int world_carte(int k, float x, float z) {
    world_t *w = &E.w;
    if (k < 0 || k >= w->ncartes) return -1;
    const carte_t *C = &w->cartes[k];
    if (k == w->carte) { E.sera.x = x; E.sera.z = z; E.sera_y_ok = 0; E.stream_hold = 1; if (C->vol) vol_embarquer(0); return 0; }
    ride_lacher();   /* le véhicule conduit reste sur l'ancienne carte (son indice désignerait un exemplaire de la nouvelle) */
    police_reset();  /* la recherche ne suit pas Sera sur une autre carte */
    if (k) {   /* index des chunks et zone de la carte, lus dans CRTD */
        uint8_t *b = w->c_buf;
        if (!b || pak_read(C->chnk_off, b, C->chnk_len) || pak_read(C->zone_off, b + C->chnk_len, C->zone_len)) return -1;
        w->nc = (int)rd32(b + 4); w->cdat_off = rd32(b + 20); w->chunk_dir = b + 24;
        const uint8_t *zo = b + C->chnk_len;
        const uint32_t zm = C->zone_len >= 4 ? rd32(zo) : 0;
        w->zone_n = zm && 4 + zm * zm <= C->zone_len ? (int)zm : 0; w->zone = w->zone_n ? zo + 4 : NULL;
        /* Carte du menu (192 Ko en 256 × 256) pas lue : la PSRAM ne l'a plus ; le menu n'a pas d'image de fond ici
           (il montrait celle de la carte 0, seule convertie) */
        w->map_n = 0; w->map_rgb = NULL;
        w->cmem = C->cmem;
    } else {
        w->nc = w->c0.nc; w->cdat_off = w->c0.cdat_off; w->chunk_dir = w->c0.dir; w->cmem = w->c0.cmem;
        w->zone = w->c0.zone; w->zone_n = w->c0.zone_n; w->map_rgb = w->c0.map; w->map_n = w->c0.map_n;
    }
    /* Exemplaires des personnages : le bloc de la carte (PNJS) ; la planète garde ses véhicules volés */
    if (!w->carte) E.nnpc_c0 = E.nnpc;
    if (k) { E.npcs = E.npc_tous + E.npc_off[k]; E.nnpc = E.nnpc_base = E.npc_cap = E.npc_cnt[k]; }
    else { E.npcs = E.npc_tous; E.nnpc = E.nnpc_c0; E.nnpc_base = E.npc_cnt[0]; E.npc_cap = E.npc_cap0; }
    w->carte = k; w->ciel = C->ciel;
    w->n = C->n; w->wl = C->wl; memcpy(w->fog_rgb, C->fog, 3);
    w->arena_x = C->ax; w->arena_z = C->az;
    w->epos_x = w->arena_x + (sinf(BAT_YAW) * BAT_EAV - cosf(BAT_YAW) * BAT_EG) / w->ech;
    w->epos_z = w->arena_z + 2.5f + (cosf(BAT_YAW) * BAT_EAV + sinf(BAT_YAW) * BAT_EG) / w->ech;
    /* Fenêtre vidée : rechargée à la mise à jour suivante (l'image préparée entre-temps n'a pas de 3D) */
    world_vider_fenetre();
    E.sera.x = x; E.sera.z = z; E.sera_y_ok = 0; E.has_dest = 0;
    E.stream_hold = 1; E.pass_arme = 0;
    /* Monde visité : son drapeau (le saut du système solaire y mène ensuite) ; système solaire : Sera à bord du vaisseau de
       l'équipe (changerCarte de l'émulateur) */
    if (C->visite && C->visite - 1 < SC_MAX_FLAGS) E.flags[(C->visite - 1) >> 3] |= (uint8_t)(1u << ((C->visite - 1) & 7));
    if (C->vol) vol_embarquer(0);
    return 0;
}

#define FAIL(msg) do { E.error = msg; return -1; } while (0)

/* ---------- Édition unique : le 18+ sous code ---------- */
/* Déverrouillage gardé dans l'emplacement REGLAGES_SLOT de la sauvegarde : 'E18', 1 (déverrouillé) */
void edition_lire(void) {
    uint8_t r[4] = { 0 };
    E.deverrou18 = E.cfg.save_read && E.cfg.save_read(REGLAGES_SLOT, r, 4) == 4 && r[0] == 'E' && r[1] == '1' && r[2] == '8' && r[3] == 1;
}
static void edition_garder(void) {
    const uint8_t r[4] = { 'E', '1', '8', (uint8_t)(E.deverrou18 ? 1 : 0) };
    if (E.cfg.save_write) E.cfg.save_write(REGLAGES_SLOT, r, 4);
}
/* Verrouillé : chaque personnage 18+ prend sa planche de remplacement (ses couleurs PSAG, sans palettes ni tissu), ou
   est masqué sans elle ; noms sûrs des bâtiments (PORT) et des lieux du guidage (GUID). Déverrouillé : tout revient */
void edition_appliquer(void) {
    world_t *w = &E.w;
    const int v = verrou18();
    for (int i = 0; i < w->npersos; i++) {
        perso_t *pe = &w->persos[i];
        if (!pe->adulte) continue;
        if (!pe->base.ok) {   /* la première fois : la planche de base */
            pe->base.ok = 1; pe->base.sheet = (int16_t)pe->sheet; pe->base.remap = (int16_t)pe->remap; pe->base.vremap = (int16_t)pe->vremap;
            pe->base.var0 = (int16_t)pe->var0; pe->base.nvar = (int16_t)pe->nvar;
        }
        if (v && pe->sage >= 0) { pe->sheet = pe->sage; pe->remap = pe->sage_remap; pe->vremap = 0; pe->var0 = pe->nvar = 0; }
        else { pe->sheet = pe->base.sheet; pe->remap = pe->base.remap; pe->vremap = pe->base.vremap; pe->var0 = pe->base.var0; pe->nvar = pe->base.nvar; }
        pe->masque = (uint8_t)(v && pe->sage < 0);
    }
    /* Noms : NSUR (sorte 0 bâtiment de PORT, 1 lieu de GUID ; rang, nom sur 24 octets), sinon ceux de PORT et GUID */
    uint32_t sz = 0;
    const uint8_t *port = section(E.pak, E.pak_len, "PORT", &sz), *guid = section(E.pak, E.pak_len, "GUID", NULL);
    for (int k = 0; k < w->nnsur; k++) {
        const uint8_t *q = w->nsur + k * 28;
        const int i = rd16(q + 2);
        char *dst = q[0] == 0 && i < w->nbats ? w->bats[i].nom : q[0] == 1 && i < w->nguides ? (char *)w->guides[i].nom : NULL;   /* guides : alloués au chargement */
        const uint8_t *src = v ? q + 4 : q[0] == 0 ? (port ? port + 8 + rd32(port) * 16 + rd32(port + 4) * 20 + i * 24 : NULL) : (guid ? guid + 4 + i * 40 + 16 : NULL);
        if (dst && src) { memcpy(dst, src, 23); dst[23] = 0; }
    }
}
int edition_code(const char *code) {
    if (E.w.edition != 2 || strcmp(code, CODE18)) return 0;
    E.deverrou18 = 1; edition_garder(); edition_appliquer();
    return 1;
}
void edition_verrouiller(void) { E.deverrou18 = 0; edition_garder(); edition_appliquer(); }

int eng_init(const uint8_t *pak, size_t len, const eng_config_t *cfg) {
    memset(&E, 0, sizeof E);
    E.cfg = *cfg;
    E.horloge = -1;
    E.pak = pak; E.pak_len = len;
    world_t *w = &E.w;
    if (len < 16 || rd32(pak) != PAK_MAGIC) FAIL("paquet invalide");
    if (rd32(pak + 4) != PAK_VERSION) FAIL("version de paquet inconnue");
    /* Édition (EDIT ; absente : 18+). Le moteur de l'édition enfant ne lance rien d'autre */
    {
        uint32_t esz;
        const uint8_t *ed = section(pak, len, "EDIT", &esz);
        w->edition = ed && esz >= 1 ? ed[0] : 0;
    }
#if ECHOES_ENFANT
    if (w->edition != 1) FAIL("paquet 18+ : la version enfant ne lance que monde_enfant.pak");
#endif

    uint32_t hsz;
    const uint8_t *p = section(pak, len, "HEAD", &hsz);
    if (!p) FAIL("section HEAD absente");
    w->n = (int)rd32(p); w->wl = rdf(p + 4);
    w->ground_off = rd32(p + 8); w->g0 = rd32(p + 12); w->decor_slots = rd32(p + 16); w->tex_len = rd32(p + 20);
    w->start_x = rdf(p + 24); w->start_z = rdf(p + 28); w->npc_x = rdf(p + 32); w->npc_z = rdf(p + 36);
    w->arena_x = rdf(p + 40); w->arena_z = rdf(p + 44); w->epos_x = rdf(p + 48); w->epos_z = rdf(p + 52);
    w->wreck_x = rdf(p + 56); w->wreck_z = rdf(p + 60);
    memcpy(w->fog_rgb, p + 64, 3);
    for (int i = 0; i < 3; i++) w->sun[i] = rdf(p + 68 + i * 4);
    w->ech = hsz >= 84 ? rdf(p + 80) : 1.0f;
    if (!(w->ech >= 0.5f && w->ech <= 3.0f)) w->ech = 1.0f;
    /* Ennemi : en haut à gauche de la caméra de combat (calculé ici : les anciens paquets le plaçaient au nord) */
    w->epos_x = w->arena_x + (sinf(BAT_YAW) * BAT_EAV - cosf(BAT_YAW) * BAT_EG) / w->ech;
    w->epos_z = w->arena_z + 2.5f + (cosf(BAT_YAW) * BAT_EAV + sinf(BAT_YAW) * BAT_EG) / w->ech;
    w->big_off = hsz >= 88 ? rd32(p + 84) : 0;
    w->zoom = hsz >= 92 ? rdf(p + 88) : 1.5f;
    w->zoom_min = hsz >= 96 ? rdf(p + 92) : 1.0f;
    w->sprite_scale = hsz >= 100 ? rdf(p + 96) : 1.0f;
    if (!(w->sprite_scale >= 0.25f && w->sprite_scale <= 4.0f)) w->sprite_scale = 1.0f;
    if (!(w->zoom_min >= 0.5f && w->zoom_min <= ZOOM_MAX)) w->zoom_min = 1.0f;
    if (!(w->zoom >= w->zoom_min)) w->zoom = w->zoom_min;
    if (w->zoom > ZOOM_MAX) w->zoom = ZOOM_MAX;

    if (!(p = section(pak, len, "PAL ", NULL))) FAIL("section PAL absente");
    w->pal_count = (int)rd32(p);
    for (int i = 0; i < 256; i++) { memcpy(w->pal[i], p + 4 + i * 4, 3); w->full[i] = p[4 + i * 4 + 3]; }
    memcpy(w->water, p + 4 + 1024, 8);
    /* Sortes d'eau (EAUC, facultative ; sans elle, l'eau est opaque) : couleur du corps de l'eau, profondeur opaque */
    uint32_t ecsz;
    if ((p = section(pak, len, "EAUC", &ecsz)) && ecsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && i < MAX_EAUX && 4 + (i + 1) * 8 <= ecsz; i++) {
            const float d = rdf(p + 8 + i * 8);
            w->eaux[i].corps = p[4 + i * 8]; w->eaux[i].prof = d > 0.05f && d < 50 ? d : 1.0f;
            w->neaux = (int)i + 1;
        }

    if (!(p = section(pak, len, "TEX ", NULL))) FAIL("section TEX absente");
    w->tex = p;
    /* Textures du sol sur la carte SD (TEXS : décalage dans monde.pak u32, nombre u32 ; TEX ne garde alors que les
       tuiles du décor et les grands décors), sinon dans TEX */
    uint32_t tssz;
    const uint8_t *texs = section(pak, len, "TEXS", &tssz);
    if (texs && tssz >= 8) { w->stream = 1; w->gt_off = rd32(texs); w->gt_n = (int)rd32(texs + 4); }
    else w->gt_n = (int)(((w->big_off ? w->big_off : w->tex_len) - w->ground_off) / 1024);

    if (!(p = section(pak, len, "SPRT", NULL))) FAIL("section SPRT absente");
    w->fw = rd16(p); w->fh = rd16(p + 2); w->sw = rd16(p + 4);
    int rows = rd16(p + 6);
    w->sheet = p + 8;
    const uint8_t *q = w->sheet + w->sw * w->fh * rows;
    uint32_t nmaps = rd32(q); q += 4;
    for (uint32_t i = 0; i < nmaps && i < 6; i++) w->remap[i] = q + i * 256;

    if ((p = section(pak, len, "MAP ", NULL))) { w->map_n = (int)rd32(p); w->map_rgb = p + 4; }
    /* Planches propres (SPRS) : celle du joueur et celles des personnages, dans l'ordre (PERS y renvoie) */
    w->psheet = -1;
    /* SPRX (paquet léger) : mêmes entrées que SPRS, sans les pixels, remplacés par leur décalage u32 dans monde.pak */
    const uint8_t *sprx = section(pak, len, "SPRX", NULL);
    if (sprx) w->stream = 1;
    if ((p = sprx ? sprx : section(pak, len, "SPRS", NULL))) {
        uint32_t ns = rd32(p);
        const uint8_t *q2 = p + 4;
        int last = -1;   /* dernière planche de base : les animations (personnage 2) qui suivent s'y rattachent */
        if (!(w->sheets = alloc(sizeof(sheet_t) * (ns < MAX_SHEETS ? ns : MAX_SHEETS), 0)) && ns) FAIL("mémoire : planches");
        for (uint32_t i = 0; i < ns && i < MAX_SHEETS; i++) {
            int who = q2[0], dirs = q2[1], frames = q2[2], a = q2[3] < ANIM_N ? q2[3] : ANIM_WALK, fw = rd16(q2 + 4), fh = rd16(q2 + 6), t = rd16(q2 + 14);
            sheet_t *sh = &w->sheets[w->nsheets++];
            memset(sh->anim, -1, sizeof sh->anim);
            sh->tissu = NULL;
            sh->ok = dirs == 8 && frames > 0;
            sh->data = sh->ok && !sprx ? q2 + 16 : NULL; sh->foff = sprx ? rd32(q2 + 16) : 0;
            for (int d = 0; d < 8; d++) sh->sl[d] = -1;
            sh->footok = sh->data ? 0xFF : 0;
            if (who == 2) { if (last >= 0 && sh->ok) w->sheets[last].anim[a] = (int16_t)i; }
            else { last = (int)i; if (sh->ok) sh->anim[a] = (int16_t)i; }   /* 0 joueur, 1 personnage, 3 tenue */
            sh->fw = fw; sh->fh = fh; sh->frames = frames;
            sh->px = rd16(q2 + 8); sh->py = rd16(q2 + 10); sh->psize = rd16(q2 + 12);
            sh->taille = t ? t / 100.0f : 1.0f;
            /* Vide sous chaque direction : un véhicule qui roule est posé sur son pixel le plus bas (planches exportées
               avant le recalage de l'émulateur ; celles d'après n'en ont plus) */
            /* (lue à la demande : calculé à la lecture de la direction) */
            memset(sh->foot, 0, sizeof sh->foot);
            for (int d = 0; sh->data && d < 8; d++) sh->foot[d] = sheet_foot(sh, sh->data + (size_t)d * fh * frames * fw);
            if (who == 0 && sh->ok && w->psheet < 0) w->psheet = (int)i;
            q2 += sprx ? 20 : 16 + (size_t)dirs * fh * frames * fw;
        }
    }
    /* Masques du tissu des tenues (TISS) : entrée de SPRS u16, 0 u16, 1 bit par pixel de la planche (complété à 4 octets) */
    uint32_t tisz;
    if ((p = section(pak, len, "TISS", &tisz)) && tisz >= 4) {
        uint32_t o = 4;
        for (uint32_t i = 0; i < rd32(p) && o + 4 <= tisz; i++) {
            const int e = rd16(p + o);
            if (e >= w->nsheets || !w->sheets[e].ok) break;
            const sheet_t *S = &w->sheets[e];
            const uint32_t nb = ((uint32_t)S->fw * S->frames * S->fh * 8 + 7) / 8;
            if (o + 4 + nb > tisz) break;
            w->sheets[e].tissu = p + o + 4;
            o += 4 + ((nb + 3) & ~3u);
        }
    }
    /* Planches des personnages et des tenues sur deux octets (SPIX : au-delà de 254 entrées de SPRS, l'octet de PERS et
       de TENU ne suffit plus) : nombres de personnages et de tenues u32 ×2, puis u16 chacun (65535 : aucune) */
    uint32_t spsz, sp_np = 0, sp_nt = 0;
    const uint8_t *spix = section(pak, len, "SPIX", &spsz);
    if (spix && spsz >= 8) { sp_np = rd32(spix); sp_nt = rd32(spix + 4); if (8 + (sp_np + sp_nt) * 2 > spsz) spix = NULL; } else spix = NULL;
    /* Personnages (PERS) : nom et répliques restent dans le paquet */
    uint32_t psz;
    if ((p = section(pak, len, "PERS", &psz))) {
        uint32_t np = rd32(p);
        const char *q3 = (const char *)p + 4, *end = (const char *)p + psz;
        for (uint32_t i = 0; i < np && i < MAX_PERSOS && q3 + 4 <= end; i++) {
            const int idx = w->npersos;
            perso_t *pe = &w->persos[w->npersos++];
            int sh = spix && i < sp_np ? rd16(spix + 8 + i * 2) : (uint8_t)q3[0], t = (uint8_t)q3[3];
            pe->sheet = sh < w->nsheets && w->sheets[sh].ok ? sh : -1;
            pe->flags = (uint8_t)q3[1]; pe->nlines = (uint8_t)q3[2];
            pe->taille = t ? t / 100.0f : pe->sheet >= 0 ? w->sheets[pe->sheet].taille : 1.0f;
            q3 += 4;
            pe->remap = 0; pe->cond = 65535; pe->neg = 0; pe->vremap = 0; pe->var0 = pe->nvar = 0;
            pe->adulte = !(pe->flags & PERS_VEHICULE) && (pe->flags & PERS_ADULTE); pe->masque = 0; pe->sage = -1; pe->sage_remap = 0; memset(&pe->base, 0, sizeof pe->base);
            if ((pe->flags & PERS_REMAP) && q3 + 256 <= end) { w->remap[6 + idx] = (const uint8_t *)q3; pe->remap = 6 + idx; q3 += 256; }
            pe->nom = q3; q3 += str_len(q3, end) + 1;
            pe->lines = q3;
            for (int k = 0; k < pe->nlines && q3 < end; k++) q3 += str_len(q3, end) + 1;
            if (q3 > end) { pe->nlines = 0; pe->sheet = -1; }
        }
    }
    /* Présence (PVIS) : drapeau u16 du scénario, inversé u8, 0 u8 par personnage */
    uint32_t pvsz;
    if ((p = section(pak, len, "PVIS", &pvsz)) && pvsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && i < (uint32_t)w->npersos && 4 + (i + 1) * 4 <= pvsz; i++) {
            w->persos[i].cond = rd16(p + 4 + i * 4); w->persos[i].neg = p[4 + i * 4 + 2] & 1;
        }
    /* Cibles de chasse (CIBL) : nombre u32, puis personnage u8, carte u8, quête u16, étape u8, 0 u8, x u16, z u16 (1/8 de case) */
    uint32_t cbsz;
    w->ncibles = 0;
    if ((p = section(pak, len, "CIBL", &cbsz)) && cbsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && w->ncibles < CIBLE_MAX && 4 + (i + 1) * 10 <= cbsz; i++) {
            const uint8_t *cq = p + 4 + i * 10;
            if (cq[0] >= w->npersos) continue;
            cible_t *c = &w->cibles[w->ncibles++];
            c->perso = cq[0]; c->carte = cq[1]; c->quest = rd16(cq + 2); c->step = cq[4]; c->vaincue = 0;
            c->x = rd16(cq + 6) / 8.0f; c->z = rd16(cq + 8) / 8.0f;
        }
    /* Lueur des feux (LUMI) : nombre u32, puis 12 octets chacune (lus à chaque image par render.c, lum_setup) */
    uint32_t lusz;
    w->lum = NULL; w->nlum = 0;
    if ((p = section(pak, len, "LUMI", &lusz)) && lusz >= 4) {
        w->lum = p + 4; w->nlum = (int)rd32(p);
        if (4 + (uint64_t)w->nlum * 12 > lusz) w->nlum = (int)((lusz - 4) / 12);
    }
    /* Planche de chaque membre de l'équipe : le personnage de la bibliothèque de son nom (VARN, BORS, LYSE) */
    for (int m = 0; m < 4; m++) {
        w->party_perso[m] = -1;
        for (int i = 0; i < w->npersos && m; i++)
            if (w->persos[i].sheet >= 0 && !strcmp(w->persos[i].nom, PARTY[m].name)) { w->party_perso[m] = i; break; }
    }
    /* Tenues (TENU) : 28 octets chacune ; porteur 255 (le joueur) ou personnage de PERS, planche de SPRS */
    uint32_t tsz;
    int tenu_idx[MAX_TENUES];   /* rang de chaque entrée de TENU dans w.tenues (-1 : écartée) */
    if ((p = section(pak, len, "TENU", &tsz)) && tsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && w->ntenues < MAX_TENUES && 4 + (i + 1) * 28 <= tsz; i++) {
            const uint8_t *r = p + 4 + i * 28;
            const int sh = spix && i < sp_nt ? rd16(spix + 8 + (sp_np + i) * 2) : r[1];
            tenu_idx[i < MAX_TENUES ? i : 0] = -1;
            if (sh >= w->nsheets || !w->sheets[sh].ok || (r[0] != TENUE_JOUEUR && r[0] >= w->npersos)) continue;
            if (r[2] == TENUE_SAGE) { if (r[0] < w->npersos) w->persos[r[0]].sage = (int16_t)sh; continue; }   /* planche de remplacement (édition unique) */
            if (i < MAX_TENUES) tenu_idx[i] = w->ntenues;
            tenue_t *t = &w->tenues[w->ntenues++];
            t->owner = r[0]; t->sheet = sh; t->ctx = r[2]; t->etanche = r[3] & 1; t->adulte = (r[3] >> 1) & 1; t->flag = rd16(r + 4); t->vremap = 0;
            memcpy(t->nom, r + 8, 19); t->nom[19] = 0;
        }
    /* Variantes de couleur des tenues (TENV) : tenue (index de TENU) u16, 0 u16, table de 256 couleurs, sur le tissu */
    uint32_t tvsz, ntv = 0;
    if ((p = section(pak, len, "TENV", &tvsz)) && tvsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && ntv < MAX_TENV && 4 + (i + 1) * 260 <= tvsz; i++) {
            const uint8_t *r = p + 4 + i * 260;
            const int ti = rd16(r) < MAX_TENUES ? tenu_idx[rd16(r)] : -1;
            if (ti < 0) continue;
            w->remap[REMAP_TENV + ntv] = r + 4; w->tenues[ti].vremap = REMAP_TENV + (int)ntv; ntv++;
        }
    /* Variantes de couleur des personnages (PTIV) : personnage (index de PERS) u16, 0 u16, table de 256 couleurs */
    if ((p = section(pak, len, "PTIV", &tvsz)) && tvsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && ntv < MAX_TENV && 4 + (i + 1) * 260 <= tvsz; i++) {
            const uint8_t *r = p + 4 + i * 260;
            if (rd16(r) >= w->npersos) continue;
            w->remap[REMAP_TENV + ntv] = r + 4; w->persos[rd16(r)].vremap = REMAP_TENV + (int)ntv; ntv++;
        }
    /* Couleurs des planches de remplacement (PSAG, édition unique : variante de couleur d'un personnage 18+) : personnage
       (index de PERS) u16, 0 u16, table de 256 couleurs */
    if ((p = section(pak, len, "PSAG", &tvsz)) && tvsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && ntv < MAX_TENV && 4 + (i + 1) * 260 <= tvsz; i++) {
            const uint8_t *r = p + 4 + i * 260;
            if (rd16(r) >= w->npersos) continue;
            w->remap[REMAP_TENV + ntv] = r + 4; w->persos[rd16(r)].sage_remap = (int16_t)(REMAP_TENV + (int)ntv); ntv++;
        }
    /* Palettes des personnages (PVAR) : nombre u32, puis personnage (index de PERS) u16, rang u16 (1, 2… : à la suite
       pour un même personnage), table de 256 couleurs ; chaque exemplaire en tire une (pvar_choix) */
    uint32_t npv = 0;
    if ((p = section(pak, len, "PVAR", &tvsz)) && tvsz >= 4)
        for (uint32_t i = 0; i < rd32(p) && npv < MAX_PVAR && 4 + (i + 1) * 260 <= tvsz; i++) {
            const uint8_t *r = p + 4 + i * 260;
            if (rd16(r) >= w->npersos) continue;
            perso_t *pe = &w->persos[rd16(r)];
            if (pe->nvar && pe->var0 + pe->nvar != REMAP_PVAR + (int)npv) continue;   /* pas à la suite : ignorée */
            if (!pe->nvar) pe->var0 = REMAP_PVAR + (int)npv;
            w->remap[REMAP_PVAR + npv++] = r + 4; pe->nvar++;
        }
    memset(E.tenue_eq, -1, sizeof E.tenue_eq);
    /* Jobs et équipement (JOBS) : types, jobs, membres au départ, bonus des objets d'équipement (voir pak.h) */
    uint32_t jsz;
    w->neqtypes = w->njobs = 0; w->eqit = NULL;
    memset(w->job0, EQ_AUCUN, sizeof w->job0); memset(w->eq0, EQ_AUCUN, sizeof w->eq0); memset(w->jobs0, 0, sizeof w->jobs0);
    if ((p = section(pak, len, "JOBS", &jsz)) && jsz >= 4 && p[0] <= MAX_EQTYPES && p[1] <= MAX_JOBS && p[2] == 4) {
        const uint32_t ts = p[3] >= 1 ? 24 : 20;   /* version 1 : effet et couleur des types ; 2 : sorts */
        const uint32_t oj = 4 + p[0] * ts, om = oj + p[1] * 52u, oi = om + 4 * 8u;
        const uint32_t ni = oi + 4 <= jsz ? rd32(p + oi) : 0;
        if (oi + 4 + ni * 12 <= jsz && (w->eqit = alloc(MAX_ITEMS * sizeof(eqitem_t), 0))) {
            for (int k = 0; k < p[0]; k++) {
                const uint8_t *e = p + 4 + k * ts;
                w->eqtypes[k].slot = e[0]; memcpy(w->eqtypes[k].nom, e + 4, 15); w->eqtypes[k].nom[15] = 0;
                w->eqtypes[k].effet = ts == 24 && e[1] < FX_N ? e[1] : FX_IMPACT;
                w->eqtypes[k].couleur = ts == 24 ? (uint32_t)e[20] << 16 | (uint32_t)e[21] << 8 | e[22] : BT_C1;
            }
            for (int k = 0; k < p[1]; k++) {
                const uint8_t *e = p + oj + k * 52;
                job_t *j = &w->jobs[k];
                memcpy(j->nom, e, 15); j->nom[15] = 0;
                j->pv = (int8_t)e[16]; j->pm = (int8_t)e[17]; j->atq = (int8_t)e[18]; j->vit = (int8_t)e[19];
                memcpy(j->aff, e + 20, MAX_EQTYPES);
            }
            for (int k = 0; k < 4; k++) {
                const uint8_t *e = p + om + k * 8;
                w->job0[k] = e[0] < p[1] ? e[0] : EQ_AUCUN; w->jobs0[k] = (uint16_t)rd16(e + 2);
                for (int s = 0; s < 3; s++) w->eq0[k][s] = e[4 + s];
            }
            memset(w->eqit, EQ_AUCUN, MAX_ITEMS * sizeof(eqitem_t));
            for (uint32_t k = 0; k < ni; k++) {
                const uint8_t *e = p + oi + 4 + k * 12;
                if (e[0] >= MAX_ITEMS || e[1] >= p[0]) continue;
                w->eqit[e[0]] = (eqitem_t){ e[1], e[2], (int8_t)e[3], (int16_t)rd16(e + 4), (int16_t)rd16(e + 6), (int16_t)rd16(e + 8), (int16_t)rd16(e + 10), 0, 0 };
            }
            for (int k = 0; k < p[1]; k++) { w->jobs[k].sorts = 0; w->jobs[k].mag = 0; }
            /* Version 2 : sorts (24 octets), puis pour chaque job puis chaque objet d'équipement (12 octets) : ses sorts
               (masque de 64 bits, 2 × u32) et son MAG (job : % i8 ; objet : i16) */
            uint32_t os = oi + 4 + ni * 12;
            if (p[3] >= 2 && os + 4 <= jsz) {
                const int ns = p[os] < MAX_SORTS ? p[os] : MAX_SORTS;
                const uint32_t om2 = os + 4 + p[os] * 24u;
                if (om2 + (p[1] + ni) * 12u <= jsz) {
                    for (int k = 0; k < ns; k++) {
                        const uint8_t *e = p + os + 4 + k * 24;
                        sort_t *sp = &w->sorts[k];
                        memcpy(sp->nom, e, 15); sp->nom[15] = 0;
                        sp->fx = e[16] >= FX_MAGIE && e[16] < FX_N ? e[16] : FX_SORT(MG_NEUTRE, 1);
                        sp->soin = e[17] <= 2 ? e[17] : 0;
                        sp->pm = rd16(e + 18); sp->mult = rd16(e + 20) / 100.0f;
                    }
                    w->nsorts = ns;
                    const uint64_t garde = ns >= 64 ? ~0ull : (1ull << ns) - 1;
                    for (int k = 0; k < p[1]; k++) {
                        const uint8_t *mq = p + om2 + k * 12;
                        w->jobs[k].sorts = ((uint64_t)rd32(mq + 4) << 32 | rd32(mq)) & garde; w->jobs[k].mag = (int8_t)mq[8];
                    }
                    for (uint32_t k = 0; k < ni; k++) {
                        const uint8_t *e = p + oi + 4 + k * 12, *mq = p + om2 + (p[1] + k) * 12;
                        if (e[0] < MAX_ITEMS && e[1] < p[0]) { w->eqit[e[0]].sorts = ((uint64_t)rd32(mq + 4) << 32 | rd32(mq)) & garde; w->eqit[e[0]].mag = (int16_t)rd16(mq + 8); }
                    }
                }
            }
            w->neqtypes = p[0]; w->njobs = p[1];
        }
    }
    /* Exemplaires (PNJS) : ceux d'un personnage sans planche sont ignorés */
    if ((p = section(pak, len, "PNJS", NULL))) {
        uint32_t nn = rd32(p);
        if (nn > MAX_NPCS) nn = MAX_NPCS;
        if ((E.npcs = alloc(sizeof(npc_t) * (nn + STOLEN_MAX + INT_PNJ_MAX), 0))) {
            E.npc_cap = (int)nn + STOLEN_MAX + INT_PNJ_MAX;
            static int16_t rang_vers[MAX_NPCS];   /* rang dans PNJS -> indice dans E.npcs (-1 : ignoré) */
            /* Autres cartes (bits 4 à 7 des drapeaux ; PNJS rangé par carte) : un bloc par carte après ceux de la planète
               et la réserve des véhicules volés et des intérieurs ; world_carte y fait pointer E.npcs */
            int fin = 0, carte_prec = 0;
            for (uint32_t i = 0; i < nn; i++) {
                const uint8_t *r = p + 4 + i * 12;
                int pe = r[8], kc = r[10] >> 4;
                rang_vers[i] = -1;
                if (pe >= w->npersos || w->persos[pe].sheet < 0) continue;
                if (kc && !carte_prec) { E.npc_cnt[0] = E.nnpc; fin = E.nnpc + STOLEN_MAX + INT_PNJ_MAX; }
                if (kc != carte_prec && kc) { if (carte_prec) E.npc_cnt[carte_prec] = fin - E.npc_off[carte_prec]; E.npc_off[kc] = fin; carte_prec = kc; }
                npc_t *n;
                if (kc) n = &E.npcs[fin++];
                else { rang_vers[i] = (int16_t)E.nnpc; n = &E.npcs[E.nnpc++]; }
                n->x = n->hx = rdf(r); n->z = n->hz = rdf(r + 4);
                n->perso = (uint8_t)pe; n->dir = r[9] * (PI_F / 4); n->wait = (float)(i % 7) * 0.6f; n->depart = r[10] & 1;
                /* Patrouilles : bit 1 meneur, bit 2 suiveur (octet 11 : écart de rang jusqu'à son meneur, placé avant lui) */
                n->meneur = (r[10] >> 1) & 1; n->chef = 0; n->rang = 0;
                if (!kc && (r[10] & 4) && r[11] && r[11] <= i && rang_vers[i - r[11]] >= 0) { n->chef = (uint16_t)(rang_vers[i - r[11]] + 1); n->rang = r[11]; }
                n->graine = (uint16_t)i;   /* graine du nom et de la palette : rang dans PNJS, comme l'émulateur */
                n->var = (uint8_t)pvar_choix(pe, n->graine);
            }
            if (carte_prec) { E.npc_cnt[carte_prec] = fin - E.npc_off[carte_prec]; E.npc_cap = E.nnpc + STOLEN_MAX + INT_PNJ_MAX; }
        }
    }
    if (!E.npcs && (E.npcs = alloc(sizeof(npc_t) * (STOLEN_MAX + INT_PNJ_MAX), 0))) E.npc_cap = STOLEN_MAX + INT_PNJ_MAX;
    E.nnpc_base = E.npc_cnt[0] = E.nnpc;
    E.npc_tous = E.npcs; E.npc_cap0 = E.npc_cap;
    /* Noms des PNJ (NOMT) : nombres u16 ×4 (prénoms féminins, masculins, neutres, noms de famille), décalage u16 de
       chaque chaîne depuis le début des chaînes, puis les chaînes terminées par 0 */
    uint32_t nmsz;
    w->nomt = NULL;
    if ((p = section(pak, len, "NOMT", &nmsz)) && nmsz >= 8) {
        uint32_t tot = 0;
        for (int k = 0; k < 4; k++) { w->nnom[k] = rd16(p + k * 2); tot += (uint32_t)w->nnom[k]; }
        if (w->nnom[3] && 8 + tot * 2 < nmsz) { w->nomt = p; w->nomt_sz = nmsz; }
    }
    /* Axes des routes (ROUT) : le trafic y roule sur la voie de droite */
    uint32_t rsz;
    if ((p = section(pak, len, "ROUT", &rsz))) {
        const uint32_t nr = rd32(p);
        route_t *r = nr ? alloc(sizeof(route_t) * nr, 0) : NULL;
        const uint8_t *rq = p + 4, *rend = p + rsz;
        for (uint32_t i = 0; r && i < nr && rq + 4 <= rend; i++) {
            const int n = rd16(rq);
            if (rq + 4 + n * 8 > rend) break;
            if (n >= 2) { r[w->nroutes].p = rq + 4; r[w->nroutes].n = n; r[w->nroutes].hw = rq[2] / 20.0f; w->nroutes++; }
            rq += 4 + n * 8;
        }
        w->routes = r;
    }
    if ((p = section(pak, len, "DECO", NULL))) { w->deco_n = rd32(p); w->deco = p + 4; }
    /* Textures réduites des bâtiments (MIPS) : première tuile, nombre, puis deux tuiles par texture */
    uint32_t mpsz;
    if ((p = section(pak, len, "MIPS", &mpsz)) && mpsz >= 8 && 8 + rd32(p + 4) * 4 <= mpsz) { w->mip0 = rd32(p); w->nmips = rd32(p + 4); w->mips = p + 8; }
    /* Lieux du menu GUIDAGE (GUID) : villes et bâtiments spéciaux */
    uint32_t gsz;
    if ((p = section(pak, len, "GUID", &gsz)) && gsz >= 4) {
        uint32_t ng = rd32(p);
        if (ng > MAX_GUIDE) ng = MAX_GUIDE;
        if (4 + ng * 40 > gsz) ng = (gsz - 4) / 40;
        guide_t *g = ng ? alloc(sizeof(guide_t) * ng, 0) : NULL;
        for (uint32_t i = 0; g && i < ng; i++) {
            const uint8_t *gq = p + 4 + i * 40;
            g[i].x = rdf(gq); g[i].z = rdf(gq + 4); g[i].r = rdf(gq + 8); g[i].kind = gq[12]; g[i].groupe = gq[13];
            if (g[i].kind == GUIDE_BAT && g[i].groupe < 2) g[i].groupe = 2 + 12;   /* paquet d'avant les groupes : AUTRES */
            memcpy(g[i].nom, gq + 16, 24); g[i].nom[23] = 0;
        }
        if (g) { w->guides = g; w->nguides = (int)ng; }
    }
    /* Ouverture (OUVR) : chaînes laissées dans le paquet (avant CDAT, gardé en mémoire) */
    w->ouv_titre = "ECHOES OF ASHES"; w->ouv_sous = w->ouv_musique = w->ouv_question = "";
    w->ouv_fond[0] = w->start_x; w->ouv_fond[1] = w->start_z; w->ouv_fond[2] = -1;
    uint32_t ovsz;
    if ((p = section(pak, len, "OUVR", &ovsz)) && ovsz >= 16 && p[0] == 1) {
        const int nc = p[1] < OUV_CARTES ? p[1] : OUV_CARTES, nh = p[2] < OUV_HEROINES ? p[2] : OUV_HEROINES;
        const uint32_t oc = 16 + (((uint32_t)p[2] + 3) & ~3u), os = oc + (uint32_t)p[1] * 12;
        if (os <= ovsz && p[ovsz - 1] == 0) {
            for (int k = 0; k < 3; k++) w->ouv_fond[k] = rdf(p + 4 + k * 4);
            for (int k = 0; k < nh; k++) if (p[16 + k] < w->npersos && w->persos[p[16 + k]].sheet >= 0) w->ouv_her[w->ouv_nher++] = p[16 + k];
            const char *s = (const char *)p + os, *fin = (const char *)p + ovsz;
            const char **ch[4] = { &w->ouv_titre, &w->ouv_sous, &w->ouv_musique, &w->ouv_question };
            for (int k = 0; k < 4 + p[1] && s < fin; k++) {
                if (k < 4) { if (*s || k) *ch[k] = s; }
                else if (k - 4 < nc) {
                    const uint8_t *cq = p + oc + (k - 4) * 12;
                    w->ouv_cartes[k - 4].x = rdf(cq); w->ouv_cartes[k - 4].z = rdf(cq + 4); w->ouv_cartes[k - 4].heure = rdf(cq + 8);
                    w->ouv_cartes[k - 4].texte = s; w->ouv_ncartes = k - 3;
                }
                s += strlen(s) + 1;
            }
        }
    }
    /* Images (IMAG : table puis données ; IMGX du paquet léger : table seule, décalages dans monde.pak), cut-ins des
       techniques (CUTI), image de chaque carton (OUVI) */
    uint32_t imsz = 0;
    w->img_tab = NULL; w->nimg = 0; w->img_max = w->img_cap = 0; w->ncutins = 0;
    for (int k = 0; k < OUV_CARTES; k++) w->ouv_img[k] = -1;
    const uint8_t *im = section(pak, len, "IMGX", &imsz);
    w->img_base = 0;
    if (!im && (im = section(pak, len, "IMAG", &imsz))) w->img_base = (uint32_t)(im - pak);
    if (im && imsz >= 4 && 4 + (uint32_t)rd16(im) * 12 <= imsz) {
        w->img_tab = im + 4; w->nimg = rd16(im);
        for (int k = 0; k < w->nimg; k++) {
            const uint32_t n = (uint32_t)rd16(w->img_tab + k * 12) * rd16(w->img_tab + k * 12 + 2);
            if (n > w->img_max) w->img_max = n;
        }
        w->img_cap = w->img_max;
        uint32_t cusz = 0;
        if ((p = section(pak, len, "CUTI", &cusz)) && cusz >= 4 && 4 + (uint32_t)rd16(p) * 8 <= cusz)
            for (int k = 0; k < rd16(p) && w->ncutins < CUT_PERS; k++) {
                const uint8_t *e = p + 4 + k * 8;
                if (!e[2] || rd16(e + 4) + e[2] > w->nimg) continue;
                w->cutins[w->ncutins].pers = e[0]; w->cutins[w->ncutins].tenue = e[1];
                w->cutins[w->ncutins].plein = e[3] & 1; w->cutins[w->ncutins].paire = (e[3] >> 1) & 1;
                w->cutins[w->ncutins].degats = e[3] & 8 ? 2 : e[3] & 4 ? 1 : 0;
                w->cutins[w->ncutins].special = e[3] & 32 ? 2 : e[3] & 16 ? 1 : 0;
                w->cutins[w->ncutins].ed = e[3] & 64 ? 1 : e[3] & 128 ? 2 : 0;   /* édition unique : 18+, version sage */
                if (e[3] & 2)   /* paire face / dos : les deux images à la fois dans le tampon */
                    for (int j = 0; j + 1 < e[2]; j += 2) {
                        const uint8_t *a = w->img_tab + (rd16(e + 4) + j) * 12;
                        const uint32_t n2 = (uint32_t)rd16(a) * rd16(a + 2) + (uint32_t)rd16(a + 12) * rd16(a + 14);
                        if (n2 > w->img_cap) w->img_cap = n2;
                    }
                w->cutins[w->ncutins].n = e[2]; w->cutins[w->ncutins++].first = rd16(e + 4);
            }
        uint32_t snsz = 0;
        w->nscenes = 0;
        if ((p = section(pak, len, "SCNI", &snsz)) && snsz >= 2)
            for (int k = 0; k < rd16(p) && k < SCENES_MAX && 2 + (uint32_t)k * 2 + 2 <= snsz; k++) {
                const int i = rd16(p + 2 + k * 2), j = i == 65535 ? -1 : i & 0x7FFF;   /* bit 15 : scène 18+ */
                w->scene18[w->nscenes] = i != 65535 && (i & 0x8000);
                w->scene_img[w->nscenes++] = j >= 0 && j < w->nimg ? (int16_t)j : -1;
            }
        uint32_t oisz = 0;
        if ((p = section(pak, len, "OUVI", &oisz)) && oisz >= 2)
            for (int k = 0; k < rd16(p) && k < OUV_CARTES && 2 + (uint32_t)k * 2 + 2 <= oisz; k++) {
                const int i = rd16(p + 2 + k * 2);
                w->ouv_img[k] = i < w->nimg ? (int16_t)i : -1;
            }
    }
    if ((p = section(pak, len, "TRAF", NULL))) w->traf_max = p[0] > TRAFFIC_MAX ? TRAFFIC_MAX : p[0];
    /* Cycle jour / nuit (CYCL, facultative : sinon une journée de 12 min, départ le matin) */
    w->jour_s = 720; w->jour_debut = 0.42f;
    uint32_t cysz = 0;
    if ((p = section(pak, len, "CYCL", &cysz)) && cysz >= 8) {
        const float d = rdf(p), h = rdf(p + 4);
        if (d > 10 && d < 100000) w->jour_s = d;
        if (h >= 0 && h < 1) w->jour_debut = h;
    }
    E.heure = w->jour_debut;
    /* Pistes de course (PIS2, facultative ; après les personnages, avec l'allocateur) */
    uint32_t pisz = 0;
    p = section(pak, len, "PIS2", &pisz);
    course_load(p, pisz);
    p = section(pak, len, "PNOM", &pisz);
    course_noms(p, pisz);
    /* Sons (AUDI, facultative : sans elle, le jeu est muet) */
    uint32_t asz = 0;
    p = section(pak, len, "AUDI", &asz);
    audio_load(p, asz);
    memset(w->son_moteur, 255, sizeof w->son_moteur);
    memset(w->veh_alt, 0, sizeof w->veh_alt);
    uint32_t vasz;
    if ((p = section(pak, len, "VALT", &vasz)))
        for (uint32_t i = 0; i < rd32(p) && i < MAX_PERSOS && 4 + (i + 1) * 4 <= vasz; i++) { const float a = rdf(p + 4 + i * 4); w->veh_alt[i] = a > 0.1f && a < 40 ? a : 0; }
    uint32_t msz;
    if ((p = section(pak, len, "SONM", &msz)))
        for (uint32_t i = 0; i < rd32(p) && i < MAX_PERSOS && 4 + i < msz; i++) w->son_moteur[i] = p[4 + i];
    /* Ancrages (PIVO) : un par entrée de SPRS, 8 directions */
    uint32_t vsz2;
    if ((p = section(pak, len, "PIVO", &vsz2))) {
        const uint32_t np = rd32(p);
        for (uint32_t i = 0; i < np && (int)i < w->nsheets && 4 + (i + 1) * 32 <= vsz2; i++) {
            const uint8_t *r = p + 4 + i * 32;
            sheet_t *sh = &w->sheets[i];
            if (rds16(r) == -32768) continue;
            for (int d = 0; d < 8; d++) { sh->pv[d][0] = rds16(r + d * 4); sh->pv[d][1] = rds16(r + d * 4 + 2); }
            sh->has_pv = 1;
        }
    }
    /* Ombres (OMBR) : celle de chaque personnage, puis celle du joueur ; sans OMBR, l'ombre d'origine */
    for (int i = 0; i <= MAX_PERSOS; i++) w->ombre[i] = (ombre_t){ 0, 0, 100, 100, 0 };
    uint32_t osz;
    if ((p = section(pak, len, "OMBR", &osz))) {
        const uint32_t no = rd32(p);
        for (uint32_t i = 0; i <= no && 4 + i * 8 + 8 <= osz; i++) {
            const uint8_t *r = p + 4 + i * 8;
            const uint32_t k = i == no ? OMBRE_JOUEUR : i;
            if (k > MAX_PERSOS || (i < no && i >= MAX_PERSOS)) continue;
            w->ombre[k] = (ombre_t){ (int8_t)r[0], (int8_t)r[1], r[2], r[3], r[4] };
        }
    }
    /* Véhicules qui roulent (ROUL) : 1 au sol, 0 plane ; sans ROUL, selon le type (tous sauf le vaisseau) */
    uint32_t lsz;
    memset(w->veh_roule, -1, sizeof w->veh_roule);
    if ((p = section(pak, len, "ROUL", &lsz)))
        for (uint32_t i = 0; i < rd32(p) && i < MAX_PERSOS && 4 + i < lsz; i++) w->veh_roule[i] = p[4 + i] ? 1 : 0;
    /* Conduite (VEHI) : vitesse maxi et accélération de chaque personnage ; 0 (ou hors bornes) : celles de son type */
    uint32_t vsz;
    if ((p = section(pak, len, "VEHI", &vsz))) {
        const uint32_t nv = rd32(p);
        for (uint32_t i = 0; i < nv && i < MAX_PERSOS && 8 + i * 8 <= vsz; i++) {
            const float v = rdf(p + 4 + i * 8), a = rdf(p + 8 + i * 8);
            w->veh_v[i] = v > 0.2f && v < 60 ? v : 0;
            w->veh_a[i] = a > 0.05f && a < 200 ? a : 0;
        }
    }
    /* Conduite et course (VEH2, remplace VEHI) : vitesse, accélération, adhérence en course, drapeaux (1 : en course) ;
       sans elle, adhérence normale et tous les véhicules en course */
    for (int i = 0; i < MAX_PERSOS; i++) { w->veh_grip[i] = 1; w->veh_course[i] = 1; }
    if ((p = section(pak, len, "VEH2", &vsz))) {
        const uint32_t nv = rd32(p);
        for (uint32_t i = 0; i < nv && i < MAX_PERSOS && 4 + (i + 1) * 16 <= vsz; i++) {
            const uint8_t *e = p + 4 + i * 16;
            const float v = rdf(e), a = rdf(e + 4), g = rdf(e + 8);
            w->veh_v[i] = v > 0.2f && v < 60 ? v : 0;
            w->veh_a[i] = a > 0.05f && a < 200 ? a : 0;
            w->veh_grip[i] = g > 0.2f && g < 3 ? g : 1;
            w->veh_course[i] = e[12] & 1;
            w->veh_service[i] = e[13] <= 6 ? e[13] : 0;
        }
    }
    if ((p = section(pak, len, "METE", NULL))) { w->rain_freq = p[0] > 100 ? 100 : p[0]; w->rain_int = p[1] > 100 ? 100 : p[1]; }
    if (!w->rain_int) w->rain_int = 70;
    /* Rencontres (facultatives) : sans RENC, ENNE ou ZONE, seule l'épave déclenche un combat */
    uint32_t esz;
    if ((p = section(pak, len, "RENC", NULL))) w->renc = rd16(p);
    if ((p = section(pak, len, "ENNE", &esz)) && esz >= 4) {
        uint32_t ne = rd32(p);
        for (uint32_t i = 0; i < ne && 4 + (i + 1) * 16 <= esz && w->nenemies < MAX_ENEMIES; i++) {
            const uint8_t *r = p + 4 + i * 16;
            if (r[0] >= w->npersos || w->persos[r[0]].sheet < 0) continue;   /* ennemi sans planche : jamais tiré */
            enemy_t *en = &w->enemies[w->nenemies++];
            en->perso = r[0]; en->lvl = r[1]; en->poids = r[2] ? r[2] : 1; en->coeur = r[3] & 1; en->src = (int)i; en->sorts = 0; en->magie = 0;
            en->hp = (int)rd32(r + 4); en->atk = rd16(r + 8); en->spd = rd16(r + 10) ? rd16(r + 10) : 1; en->zones = rd32(r + 12);
            if (en->hp < 1) en->hp = 1;
        }
        /* ENSO : sorts des ennemis (12 octets par ennemi de ENNE, dans son ordre) : masque de 64 bits (2 × u32), part des
           tours où il en lance (%) u8, 0 u8 ×3 ; lue après le catalogue des sorts (JOBS) */
        const uint8_t *qs = section(pak, len, "ENSO", &esz);
        if (qs && esz >= 4)
            for (int k = 0; k < w->nenemies; k++) {
                enemy_t *en = &w->enemies[k];
                if ((uint32_t)en->src >= rd32(qs) || 4 + (en->src + 1) * 12u > esz) continue;
                const uint8_t *r = qs + 4 + en->src * 12;
                en->sorts = (uint64_t)rd32(r + 4) << 32 | rd32(r); en->magie = r[8] <= 100 ? r[8] : 100;
            }
    }
    uint32_t ssz;
    p = section(pak, len, "SCEN", &ssz);
    items_init();
    script_load(p, p ? ssz : 0);   /* ou histoire.scn de la carte SD (eng_config_t.scen_read) */
    int_load();   /* bâtiments qu'on visite (PORT), intérieurs (INTR, ou interieurs.bin de la carte SD) */
    /* Édition unique : noms sûrs (NSUR), puis le verrou du 18+ (déverrouillage gardé dans REGLAGES_SLOT) */
    {
        uint32_t nssz = 0;
        const uint8_t *ns = section(pak, len, "NSUR", &nssz);
        w->nsur = ns && nssz >= 4 && 4 + rd32(ns) * 28 <= nssz ? ns + 4 : NULL; w->nnsur = w->nsur ? (int)rd32(ns) : 0;
        edition_lire(); edition_appliquer();
    }
    if ((p = section(pak, len, "ZONE", &esz)) && esz >= 4) {
        uint32_t m = rd32(p);
        if (m && m <= 4096 && 4 + m * m <= esz) { w->zone_n = (int)m; w->zone = p + 4; }
    }
    /* Table blanche (flash de l'ennemi touché) : la couleur de la palette la plus claire, en pleine lumière de préférence */
    {
        static uint8_t white[256];
        int best = 1, bv = -1;
        for (int i = 1; i < 256; i++) {
            int v = w->pal[i][0] + w->pal[i][1] + w->pal[i][2] + (w->full[i] ? 1000 : 0);
            if (v > bv) { bv = v; best = i; }
        }
        memset(white, best, sizeof white); white[0] = 0;
        w->remap[REMAP_WHITE] = white;
    }

    /* Chunks : répertoire, tampons de décompression, fenêtre de CWIN × CWIN places */
    if (!(p = section(pak, len, "CHNK", NULL))) FAIL("section CHNK absente");
    if ((int)rd32(p) != CS) FAIL("taille de chunk inattendue");
    w->nc = (int)rd32(p + 4);
    w->raw_max = rd32(p + 8); w->comp_max = rd32(p + 12); w->mem_max = rd32(p + 16); w->cdat_off = rd32(p + 20);
    w->chunk_dir = p + 24;
    /* Carte 0 (HEAD, CHNK, ZONE, MAP), gardée pour le retour ; cartes secondaires (CART, 80 octets chacune) : leurs
       tampons de chunks prennent la plus grande taille de toutes, leurs données (CRTD) se lisent au changement de carte */
    {
        carte_t *C0 = &w->cartes[0];
        snprintf(C0->nom, sizeof C0->nom, "MONDE"); C0->n = w->n; C0->ciel = 0; C0->wl = w->wl; memcpy(C0->fog, w->fog_rgb, 3);
        C0->vol = 0; C0->visite = 0; C0->vol_alt = 0;
        C0->sx = w->start_x; C0->sz = w->start_z; C0->ax = w->arena_x; C0->az = w->arena_z;
        w->c0.dir = w->chunk_dir; w->c0.nc = w->nc; w->c0.cdat_off = w->cdat_off;
        w->c0.zone = w->zone; w->c0.zone_n = w->zone_n; w->c0.map = w->map_rgb; w->c0.map_n = w->map_n;
        w->ncartes = 1;
        uint32_t csz;
        const uint8_t *ct = section(pak, len, "CART", &csz);
        for (uint32_t k = 0; ct && csz >= 4 && k < rd32(ct) && w->ncartes < MAX_CARTES && 4 + (k + 1) * 80 <= csz; k++) {
            const uint8_t *cq = ct + 4 + k * 80;
            carte_t *C = &w->cartes[w->ncartes++];
            memcpy(C->nom, cq, 23); C->nom[23] = 0;
            C->n = (int)rd32(cq + 24); C->ciel = cq[28]; C->wl = rdf(cq + 32); memcpy(C->fog, cq + 36, 3);
            /* octet 29 : drapeaux de la carte (1 vol permanent), 30 : drapeau de visite + 1 u16, 39 : altitude du vol (1/10) */
            C->vol = cq[29] & 1; C->visite = (uint16_t)rd16(cq + 30); C->vol_alt = cq[39] / 10.0f;
            C->sx = rdf(cq + 40); C->sz = rdf(cq + 44); C->ax = rdf(cq + 48); C->az = rdf(cq + 52);
            C->chnk_off = rd32(cq + 56); C->chnk_len = rd32(cq + 60); C->zone_off = rd32(cq + 64); C->zone_len = rd32(cq + 68);
            C->map_off = rd32(cq + 72); C->map_len = rd32(cq + 76);
            uint8_t h[24];
            if (C->chnk_len < 24 || pak_read(C->chnk_off, h, 24) || (int)rd32(h) != CS) FAIL("carte secondaire illisible (CART, CRTD)");
            if (rd32(h + 8) > w->raw_max) w->raw_max = rd32(h + 8);
            if (rd32(h + 12) > w->comp_max) w->comp_max = rd32(h + 12);
            if (rd32(h + 16) > w->mem_max) w->mem_max = rd32(h + 16);
            const uint32_t need = C->chnk_len + C->zone_len + 16;   /* sans la carte du menu (world_carte) */
            if (need > w->c_cap) w->c_cap = need;
        }
        if (w->c_cap && !(w->c_buf = alloc(w->c_cap, 0))) FAIL("mémoire insuffisante (cartes)");
        /* Passages (PASS, 28 octets chacun) */
        uint32_t psz2;
        const uint8_t *ps = section(pak, len, "PASS", &psz2);
        const uint32_t np2 = ps && psz2 >= 4 ? rd32(ps) : 0;
        if (np2 && 4 + np2 * 28 <= psz2 && (w->pass = alloc(sizeof(passage_t) * np2, 0))) {
            for (uint32_t k = 0; k < np2; k++) {
                const uint8_t *pq = ps + 4 + k * 28;
                passage_t *P = &w->pass[k];
                P->carte = pq[0]; P->vers = pq[1]; P->sorte = pq[2]; P->cond = rd16(pq + 24);
                P->x = rdf(pq + 4); P->z = rdf(pq + 8); P->r = rdf(pq + 12); P->ax = rdf(pq + 16); P->az = rdf(pq + 20);
            }
            w->npass = (int)np2;
        }
        /* Astres du système solaire (ASTR, 48 octets chacun) : nom, carte, carte où l'on se pose, drapeau de visite, centre,
           rayon, point d'orbite */
        uint32_t astr_sz = 0;
        const uint8_t *as = section(pak, len, "ASTR", &astr_sz);
        const uint32_t na = as && astr_sz >= 4 ? rd32(as) : 0;
        w->astres = NULL; w->nastres = 0;
        if (na && na <= 256 && 4 + na * 48 <= astr_sz && (w->astres = alloc(sizeof(astre_t) * na, 0))) {
            for (uint32_t k = 0; k < na; k++) {
                const uint8_t *aq = as + 4 + k * 48;
                astre_t *A = &w->astres[k];
                memcpy(A->nom, aq, 23); A->nom[23] = 0;
                A->carte = aq[24]; A->vers = aq[25] == 255 ? -1 : aq[25]; A->visite = (uint16_t)rd16(aq + 26);
                A->x = rdf(aq + 28); A->z = rdf(aq + 32); A->r = rdf(aq + 36); A->ax = rdf(aq + 40); A->az = rdf(aq + 44);
            }
            w->nastres = (int)na;
        }
        E.pass_arme = 1;
    }
    w->zbuf = alloc(w->comp_max + 4, 0); w->rbuf = alloc(w->raw_max + 4, 0);
    if (!w->zbuf || !w->rbuf) FAIL("mémoire insuffisante (chunks)");
    /* Tailles des chunks (CMEM, facultative) : carte 0, puis les cartes secondaires dans l'ordre de CART */
    {
        uint32_t cmsz = 0;
        const uint8_t *cm = section(pak, len, "CMEM", &cmsz);
        if (cm && cmsz >= 4) {
            const uint8_t *cq = cm + 4, *fin = cm + cmsz;
            for (uint32_t k = 0; k < rd32(cm) && cq + 4 <= fin; k++) {
                const int nc = (int)rd32(cq); cq += 4;
                if (cq + (size_t)nc * nc * 2 > fin) break;
                if (!k && nc == w->c0.nc) w->c0.cmem = cq;
                else if (k && (int)k < w->ncartes && nc == w->cartes[k].n / CS) w->cartes[k].cmem = cq;
                cq += (size_t)nc * nc * 2;
            }
        }
        w->cmem = w->c0.cmem;
    }
    /* Places de la fenêtre : un seul bloc où chaque chunk prend sa taille (CMEM), tassé au besoin (fen_alloc). Il fait
       la plus grande somme des CWIN × CWIN chunks d'une fenêtre, sur toutes les cartes (bien moins que CWIN × CWIN fois
       le plus gros : en ville, les chunks denses ne sont pas tous voisins) ; sans CMEM, CWIN × CWIN fois le plus gros. Au
       moins ce que demande l'intérieur le plus gros (int_load : intr_need) : l'intérieur visité reprend le bloc
       (int_fenetre ; rien ne lit les chunks pendant la visite, la fenêtre est rechargée à la sortie par world_stream). */
    {
        const uint32_t pl = (w->mem_max + 4 + 15) & ~15u;
        uint32_t pire = w->c0.cmem ? fen_pire(w->c0.cmem, w->c0.nc) : 0;
        for (int k = 1; k < w->ncartes && pire; k++) {
            const uint32_t pk = w->cartes[k].cmem ? fen_pire(w->cartes[k].cmem, w->cartes[k].n / CS) : 0;
            pire = pk ? (pk > pire ? pk : pire) : 0;   /* une carte sans tailles : places égales partout */
        }
        w->fen_cap = pire ? (uint32_t)((uint64_t)pire * FEN_MARGE / 100) & ~15u : pl * CWIN * CWIN;
        if (w->fen_cap < pl) w->fen_cap = pl;
        if (w->fen_cap < w->intr_need) w->fen_cap = w->intr_need < (uint32_t)INTR_FEN_MAX ? w->intr_need : w->fen_cap > (uint32_t)INTR_FEN_MAX ? w->fen_cap : (uint32_t)INTR_FEN_MAX;
        if (!(w->fen_mem = alloc(w->fen_cap, 0))) FAIL("mémoire insuffisante (fenêtre de chunks)");
        for (int s = 0; s < CWIN * CWIN; s++) {
            chunk_t *c = alloc(sizeof(chunk_t), 0);
            if (!c) FAIL("mémoire insuffisante (fenêtre de chunks)");
            memset(c, 0, sizeof *c);
            c->cx = c->cz = -1; c->slot = s; c->lodbuf = -1;
            w->slot[s] = c;
        }
    }
    for (int b = 0; b < LOD_BUFS; b++) {
        if (!(w->lodbuf[b] = alloc((CS / 2) * (CS / 2) * 1024, 0))) FAIL("mémoire insuffisante (textures réduites)");
        w->lodbuf_slot[b] = -1;
    }
    ring_init();

    /* Personnages et caméra, comme l'émulateur */
    E.sera.x = w->start_x; E.sera.z = w->start_z; E.sera.dir = 0.35f + PI_F;
    E.yaw = 0.35f; E.pitch = 0.6f; E.zoom = w->zoom; E.cam_dist = 12.5f / w->ech / E.zoom; E.talk = -2; E.ride = -1;
    E.fog_on = 1; E.dither_on = 0;   /* tramage de la lumière et du brouillard : option (SYSTÈME › TRAMAGE), coupé par défaut depuis le 31/10/2026 */ E.volant = VOLANT_DEFAUT;
    E.team = 15;   /* les quatre, jusqu'à une instruction « equipe » du scénario */
    equip_reset();
    party_set_level(E.w.sc_nq ? 1 : 20);   /* une histoire (des quêtes) commence au niveau 1, sinon l'équipe d'essai */
    E.qtrack = -1; E.guide = -1; E.heroine = -1; E.partie = 0;
    E.obj[0] = E.obj[1] = 65535;
    E.mus_force = 65535;
    /* Paquet léger : monde.pak de la carte SD doit être celui dont il a été tiré (SDPK), puis les caches */
    if (w->stream && E.cfg.pak_size) {
        uint32_t sdsz; const uint8_t *sd = section(pak, len, "SDPK", &sdsz);
        if (sd && sdsz >= 4 && rd32(sd) != E.cfg.pak_size) FAIL("monde.pak de la carte SD ne correspond pas au paquet de la flash");
    }
    if (stream_init()) FAIL("mémoire : caches de la carte SD");
    world_stream(E.sera.x, E.sera.z, 1);
    if (E.error) return -1;
    render_init();
    if (!E.error) ui_init();
    if (!E.error) hud_init();
    if (E.error) return -1;
    /* Textures (sans paquet léger : avec celles du sol) en mémoire rapide (PSRAM) s'il reste la place une fois tout le
       reste réservé, sinon lues dans le paquet (sur l'ESP32 : en flash, à travers le cache, quelques pour cent plus lent) */
    uint8_t *tex = E.cfg.alloc(w->tex_len, 0);
    if (tex) { memcpy(tex, w->tex, w->tex_len); w->tex = tex; w->tex_copied = 1; }
    accueil_debut();   /* écran titre : le jeu commence avec une partie */
    return 0;
}

const uint8_t *pak_section(const char *tag, uint32_t *size) { return section(E.pak, E.pak_len, tag, size); }

/* ---------- Images de la banque (IMAG) : une (ou une paire) à la fois dans un tampon commun, lues au besoin ---------- */
/* Le tampon (img_cap octets : les index, dessinés par ui_image_pal avec la palette de leur place) n'est relu que si
   l'image précédente de l'interface ne l'a pas dessiné : son rendu peut encore être en cours. Deux places : une image
   seule prend la première ; une paire face / dos, les deux (la seconde juste après la première). */
static uint8_t *img_buf;
static struct { int k; uint32_t off; uint16_t pal[256]; } img_pl[2] = { { -1, 0, { 0 } }, { -1, 0, { 0 } } };
static uint32_t img_n, img_vu = 0xFFFFFFF0u;
void img_trame(void) { img_n++; }
static int img_tampon(void) {
    if (!img_buf) img_buf = E.cfg.alloc ? E.cfg.alloc(E.w.img_cap, 0) : malloc(E.w.img_cap);
    return img_buf != NULL;
}
/* Image k dans la place p du tampon, à off ; 0 : pas lue */
static int img_lire(int p, int k, uint32_t off) {
    const uint8_t *e = E.w.img_tab + k * 12;
    const uint32_t n = (uint32_t)rd16(e) * rd16(e + 2), src = E.w.img_base + rd32(e + 8);
    const int nc = e[4];
    uint8_t rgb[255 * 3];
    img_pl[p].k = -1;
    if (off + n > E.w.img_cap || pak_read(src, rgb, (uint32_t)nc * 3) || pak_read(src + (uint32_t)nc * 3, img_buf + off, n)) return 0;
    img_pl[p].pal[0] = 0;
    for (int c = 0; c < nc; c++) img_pl[p].pal[c + 1] = ui_rgb(((uint32_t)rgb[c * 3] << 16) | (rgb[c * 3 + 1] << 8) | rgb[c * 3 + 2]);
    for (int c = nc + 1; c < 256; c++) img_pl[p].pal[c] = 0;
    img_pl[p].k = k; img_pl[p].off = off;
    return 1;
}
const uint8_t *img_ix(int k, int *w, int *h, const uint16_t **pal, int dessin) {
    const world_t *W = &E.w;
    if (k < 0 || k >= W->nimg) return NULL;
    int p = img_pl[0].k == k ? 0 : img_pl[1].k == k ? 1 : -1;
    if (p < 0) {
        if (img_n - img_vu < 2 || !img_tampon()) return NULL;
        img_pl[1].k = -1;
        if (!img_lire(0, k, 0)) return NULL;
        p = 0;
    }
    if (dessin) img_vu = img_n;
    const uint8_t *e = W->img_tab + k * 12;
    *w = rd16(e); *h = rd16(e + 2); *pal = img_pl[p].pal;
    return img_buf + img_pl[p].off;
}
int img_paire(int k, int dessin) {
    const world_t *W = &E.w;
    if (k < 0 || k + 1 >= W->nimg) return 0;
    if (img_pl[0].k != k || img_pl[1].k != k + 1) {
        if (img_n - img_vu < 2 || !img_tampon()) return 0;
        const uint8_t *e = W->img_tab + k * 12;
        if (!img_lire(0, k, 0) || !img_lire(1, k + 1, (uint32_t)rd16(e) * rd16(e + 2))) { img_pl[1].k = -1; return 0; }
    }
    if (dessin) img_vu = img_n;
    return 1;
}

const char *eng_error(void) { return E.error ? E.error : ""; }

float height_at(float x, float z) {
    if (E.in.on) return 0;   /* intérieur : sol plat */
    int i = (int)floorf(x), j = (int)floorf(z);
    float fx = x - i, fz = z - j;
    float a = hv(i, j), b = hv(i + 1, j), c = hv(i, j + 1), d = hv(i + 1, j + 1);
    return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}

/* Hauteur où se tiennent les personnages : la chaussée si une route couvre la case, sinon le sol */
/* Hauteur de la chaussée en (x, z) : plan du morceau de route de la case qui contient le point (le plus haut s'il y en a
   plusieurs, l'eau des rivières et les faces verticales des piles exclues), NAN si aucun */
/* ref (NAN : aucune) : hauteur précédente de celui qui se tient là ; parmi plusieurs chaussées (route sous un pont), la
   plus haute qui ne dépasse pas ref de plus de 0,9 (on monte une rampe, on ne saute pas sur le tablier), sinon la plus
   basse */
static float road_surface(const chunk_t *c, int k, float x, float z, float ref) {
    float best = NAN, low = NAN, ok = NAN;
    for (int m = 0; m < c->road_count[k]; m++) {
        const pak_poly_t *pp = chunk_poly(c, c->road_first[k] + m);
        if (pp->tile == TILE_WATER) continue;
        for (int i = 1; i + 1 < pp->nv; i++) {
            const pak_vert_t *a = &pp->v[0], *b = &pp->v[i], *d = &pp->v[i + 1];
            const float den = (b->x - a->x) * (d->z - a->z) - (d->x - a->x) * (b->z - a->z);
            if (fabsf(den) < 1e-5f) continue;
            const float u = ((x - a->x) * (d->z - a->z) - (d->x - a->x) * (z - a->z)) / den;
            const float v = ((b->x - a->x) * (z - a->z) - (x - a->x) * (b->z - a->z)) / den;
            if (u < -0.01f || v < -0.01f || u + v > 1.01f) continue;
            const float y = a->y + (b->y - a->y) * u + (d->y - a->y) * v;
            if (!(y <= best)) best = y;
            if (!(y >= low)) low = y;
            if (y <= ref + 0.9f && !(y <= ok)) ok = y;
        }
    }
    return ref != ref || best != best ? best : ok == ok ? ok : low;
}
/* Tablier au-dessus de (x, z) : la chaussée la plus basse à plus de 0,8 au-dessus de y (pont, circuit) ; NAN s'il n'y en
   a pas, ou si la caméra est dessous (vu d'en bas, il ne cache rien). Un effet de course dessous n'est pas dessiné (il
   passe par-dessus la 3D, hud.c) ; comme tablierAu de l'émulateur */
float deck_above(float x, float z, float y) {
    if (E.in.on) return NAN;
    const int i = (int)floorf(x), j = (int)floorf(z);
    const chunk_t *c = i >= 0 && j >= 0 && i < E.w.n && j < E.w.n ? chunk_of_cell(i, j) : NULL;
    if (!c) return NAN;
    const int k = cell_local(i, j);
    if (!(c->flags[k] & F_ROAD)) return NAN;
    float best = NAN;
    for (int m = 0; m < c->road_count[k]; m++) {
        const pak_poly_t *pp = chunk_poly(c, c->road_first[k] + m);
        if (pp->tile == TILE_WATER) continue;
        for (int t = 1; t + 1 < pp->nv; t++) {
            const pak_vert_t *a = &pp->v[0], *b = &pp->v[t], *d = &pp->v[t + 1];
            const float den = (b->x - a->x) * (d->z - a->z) - (d->x - a->x) * (b->z - a->z);
            if (fabsf(den) < 1e-5f) continue;
            const float u = ((x - a->x) * (d->z - a->z) - (d->x - a->x) * (z - a->z)) / den;
            const float v = ((b->x - a->x) * (z - a->z) - (x - a->x) * (b->z - a->z)) / den;
            if (u < -0.01f || v < -0.01f || u + v > 1.01f) continue;
            const float ys = a->y + (b->y - a->y) * u + (d->y - a->y) * v;
            if (ys > y + 0.8f && !(ys >= best)) best = ys;
        }
    }
    return best == best && E.cam.y > best ? best : NAN;
}
/* Hauteur où se tiennent les personnages et roulent les véhicules : la chaussée sous le point, sinon le sol ; sur un pont,
   le tablier si le point tombe juste à côté d'un morceau (le sol est bien plus bas) */
float ground_y(float x, float z) {
    if (E.in.on) return 0;
    int i = (int)floorf(x), j = (int)floorf(z);
    float g = height_at(x, z);
    const chunk_t *c = i < E.w.n && j < E.w.n ? chunk_of_cell(i, j) : NULL;
    if (!c) return g;
    int k = cell_local(i, j);
    if (!(c->flags[k] & F_ROAD)) return g;
    const float s = road_surface(c, k, x, z, x == E.sera.x && z == E.sera.z && E.sera_y_ok ? E.sera_y : NAN);
    if (s == s) return s > g ? s : g;
    float r = c->road_h[k];
    return r > g + 0.5f ? r : g;
}

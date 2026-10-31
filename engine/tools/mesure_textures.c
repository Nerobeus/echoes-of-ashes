/*
 * Textures du sol utilisées autour de Sera : nombre de textures 32 × 32 différentes (tuiles ≥ G0) dans la fenêtre de
 * chunks chargée, sur une grille de positions, et nouvelles textures quand Sera avance d'un chunk (dimensionnement du
 * cache de textures lu sur la carte SD). mesure_textures monde.pak [pas en cases]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"

static size_t total_alloc;
static void *alloc_pc(size_t size, int fast) {
    total_alloc += size;
    if (size >= 65536 && getenv("MESURE_ALLOC")) fprintf(stderr, "  alloc %7.0f Ko%s\n", size / 1024.0, fast ? " (rapide)" : "");
    return malloc(size);
}

/* Textures des cases à moins de r cases de (x, z) (r ≤ 0 : toute la fenêtre) */
static int compte_r(uint8_t *vu, int n, float x0, float z0, float r) {
    int k = 0;
    memset(vu, 0, (size_t)n);
    for (int s = 0; s < CWIN * CWIN; s++) {
        const chunk_t *c = E.w.slot[s];
        if (!c || c->cx < 0) continue;
        for (int i = 0; i < CS * CS; i++) {
            const float x = c->cx * CS + i % CS + 0.5f, z = c->cz * CS + i / CS + 0.5f;
            if (r > 0 && (x - x0) * (x - x0) + (z - z0) * (z - z0) > r * r) continue;
            const int t = c->tile[i] - (int)E.w.g0;
            if (t >= 0 && t < n && !vu[t]) { vu[t] = 1; k++; }
        }
    }
    return k;
}
static int compte(uint8_t *vu, int n) {
    int k = 0;
    memset(vu, 0, (size_t)n);
    for (int s = 0; s < CWIN * CWIN; s++) {
        const chunk_t *c = E.w.slot[s];
        if (!c || c->cx < 0) continue;
        for (int i = 0; i < CS * CS; i++) {
            const int t = c->tile[i] - (int)E.w.g0;
            if (t >= 0 && t < n && !vu[t]) { vu[t] = 1; k++; }
        }
    }
    return k;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "mesure_textures monde.pak [pas]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *pak = malloc((size_t)len);
    if (fread(pak, 1, (size_t)len, f) != (size_t)len) return 1;
    fclose(f);
    eng_config_t cfg = { 0 };
    cfg.alloc = alloc_pc;
    if (eng_init(pak, (size_t)len, &cfg)) { fprintf(stderr, "eng_init : %s\n", eng_error()); return 1; }
    printf("mémoire réservée par le moteur après eng_init : %.2f Mo (textures copiées : %d)\n", total_alloc / 1048576.0, eng_textures_in_ram());
    const int N = eng_world_size(), pas = argc > 2 ? atoi(argv[2]) : 64;
    const int nt = (int)((E.w.big_off ? E.w.big_off : E.w.tex_len) - E.w.ground_off) / 1024;
    uint8_t *vu = calloc((size_t)nt, 1), *avant = calloc((size_t)nt, 1);
    int pire = 0, somme = 0, n = 0, pire_new = 0, pr[3] = { 0 }, sr[3] = { 0 };
    for (int z = pas / 2; z < N; z += pas) for (int x = pas / 2; x < N; x += pas) {
        eng_teleport((float)x, (float)z, 0);
        for (int ri = 0; ri < 3; ri++) { const float R[3] = { 8, 10, 16 }; const int kr = compte_r(vu, nt, (float)x, (float)z, R[ri]); if (kr > pr[ri]) pr[ri] = kr; sr[ri] += kr; }
        const int k = compte(vu, nt);
        memcpy(avant, vu, (size_t)nt);
        eng_teleport((float)x + CS, (float)z, 0);   /* un chunk plus loin : une colonne de 5 chunks nouvelle */
        compte(vu, nt);
        int nouv = 0;
        for (int t = 0; t < nt; t++) nouv += vu[t] && !avant[t];
        if (k > pire) pire = k;
        if (nouv > pire_new) pire_new = nouv;
        somme += k; n++;
    }
    printf("%d textures du sol en tout ; fenêtre de %d × %d chunks : %d en moyenne, %d au pire ; un chunk de plus vers l'est : %d nouvelles au pire\n",
           nt, CWIN, CWIN, n ? somme / n : 0, pire, pire_new);
    printf("cases à moins de 8 / 10 / 16 cases : %d / %d / %d en moyenne, %d / %d / %d au pire\n", sr[0] / (n ? n : 1), sr[1] / (n ? n : 1), sr[2] / (n ? n : 1), pr[0], pr[1], pr[2]);
    return 0;
}

/* Essai du chargement des chunks sur PC, sur un monde agrandi par tools/pavage.js : Sera traverse le monde en
   diagonale (3,2 cases/s à 30 i/s) ; chunks chargés par image, temps de chargement, cases visibles sans chunk (trous)

     essai_chunks monde.pak */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../core/internal.h"

static void *alloc_pc(size_t n, int fast) { (void)fast; return calloc(1, n); }
static uint32_t clock_pc(void) { return (uint32_t)((double)clock() * 1e6 / CLOCKS_PER_SEC); }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage : essai_chunks monde.pak\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *pak = malloc(len);
    if (fread(pak, 1, len, f) != (size_t)len) return 1;
    fclose(f);
    eng_config_t cfg = { 0, alloc_pc, NULL, NULL, NULL, clock_pc, NULL };
    if (eng_init(pak, len, &cfg)) { printf("eng_init : %s\n", eng_error()); return 1; }
    const int n = E.w.n;
    eng_teleport(3.5f, 3.5f, 0.35f);
    eng_input_t in = { 0 };
    int frames = 0, loads = 0, max_loads = 0, holes = 0, hole_frames = 0;
    uint32_t us_max = 0, us_sum = 0;
    for (float t = 0; t < 1e4f; t += 1.0f / 30) {
        /* diagonale : traverse les chunks dans les deux directions */
        E.sera.x = 3.5f + t * 3.2f * 0.7071f; E.sera.z = 3.5f + t * 3.2f * 0.7071f;
        if (E.sera.x > n - 4) break;
        eng_update(1.0f / 30, &in);
        eng_build_frame(t);
        eng_present();
        frames++;
        /* Statistiques de l'image qui vient d'être préparée (présentée par eng_present) ; la première compte la
           téléportation, chargée d'un coup */
        int fl = eng_stats()->chunk_loads;
        uint32_t fu = eng_stats()->us_stream;
        loads += fl; if (frames > 1 && fl > max_loads) max_loads = fl;
        us_sum += fu; if (fu > us_max) us_max = fu;
        int h = 0;
        for (int j = (int)E.sera.z - 16; j <= (int)E.sera.z + 16; j++)
            for (int i = (int)E.sera.x - 16; i <= (int)E.sera.x + 16; i++)
                if (i >= 0 && j >= 0 && i < n && j < n && hypotf(i + 0.5f - E.sera.x, j + 0.5f - E.sera.z) < 23 && !chunk_of_cell(i, j)) h++;
        if (h) { holes += h; hole_frames++; }
    }
    printf("%d images, %d chunks chargés (au plus %d par image), chargement : %.2f ms au plus, %.3f ms en moyenne ; "
           "cases à moins de 23 de Sera sans chunk : %d sur %d images ; erreur : '%s'\n",
           frames, loads, max_loads, us_max / 1000.0, us_sum / 1000.0 / frames, holes, hole_frames, eng_error());
    return 0;
}

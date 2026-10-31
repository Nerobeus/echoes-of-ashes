/*
 * Test du cœur sur PC : charge monde.pak, joue un scénario d'entrées, puis écrit la dernière image en BMP
 * (466 × 466, telle qu'envoyée à l'écran, interface comprise).
 *
 *   rendu_test monde.pak sortie.bmp [scénario]
 *
 * Scénario : étapes séparées par des virgules, une image chacune (touches relâchées à l'image suivante)
 *   menu, ok, back, up, down, left, right   touches
 *   tap:x:y                                 toucher bref en coordonnées natives (466)
 *   hold:x:y:n                              doigt posé n images
 *   drag:x:y0:y1:n                          doigt glissé de (x, y0) à (x, y1) en n images, puis levé
 *   rim:a0:a1                               glisser le long du bord, angles en degrés (0 = haut, sens horaire)
 *   pos:x:z:lacet                           placer Sera et la caméra
 *   portes, porte:k, entrer:k               lister les portes (PORT) ; Sera devant la porte k ; entrer par elle (forcée)
 *   ipos:x:z:lacet                          dans un intérieur : Sera en (x, z) du plan ; alarme : déclencher l'alarme
 *   escalier:k                              dans un intérieur : Sera au milieu de la zone de l'escalier k (passe au niveau voisin)
 *   pinch:d0:d1                             deux doigts autour du centre, écart de d0 à d1 px (zoom)
 *   wait:n                                  n images sans entrée
 *   mise:NOM                                mise en scène (game.c, MISE) jouée comme serveur/essai-mise.js de l'Atelier :
 *                                           acteur NOM, caméra, marches, fondu, secousse, course de Sera, fuite ; états
 *                                           aux mêmes instants (comparer avec la sortie de l'émulateur)
 *   police, recherche:n[:m], poursuite:n[:k], arreter, bmp:fichier
 *                                           police (police.c) : état ; niveau n imposé (motif m : 1 vol, 2 effraction) ;
 *                                           n images (k = 1 : gaz tenu), état toutes les 30 ; la scène de l'arrestation ;
 *                                           l'image en cours en BMP (captures en cours de scénario)
 *   course:n                                course sur la piste (PIST) : n images pilotées, classement
 *   essai:k:a                               course d'essai de la montre (eng_essai_course, SDX COURSE k a)
 *   heure:h                                 heure du cycle jour / nuit (0 minuit, 0,5 midi)
 *   horloge:s                               heure réelle en secondes depuis minuit (montre : arc du bas, cycle RÉEL)
 *   pluie:x                                 pluie imposée d'intensité x (0 à 1 ; 0 : temps sec)
 *   cendre                                  pluie de cendre : intensité, PV de l'équipe, exemplaires présents et à l'abri
 *   piste:k                                 Sera au départ de la piste k (PIS2), piste active
 *   menucourse, organisateur                menu de course ; Sera devant l'organisateur (monde de course)
 *   parler:NOM, drapeau:k                   Sera devant le personnage NOM (puis « ok » : lui parler) ; lever le drapeau k
 *   finscript                               le script en cours s'arrête (celui du lancement : parler sans lui)
 *   pilote:n                                course en cours : n images pilotées (W tenu, vers l'axe), puis l'état
 *   boost                                   course en cours : le joueur accélère comme sur une plaque (halo de l'écran)
 *   musique:nom                             instruction « musique » du scénario (auto, aucune ou un nom de piste)
 *   pouces:g:v:n                            en véhicule : pouce gauche glissé de g px vers le haut (gaz), pouce
 *                                           droit de v px vers la droite (volant), tenus n images
 *   lac:n[:m], riviere:n[:m]                véhicule ECHOES_VEH (sinon une voiture) près de l'eau la plus proche, n images
 *                                           de gaz vers elle, puis m sans rien (150 ; gué, naufrage, sortie de Sera)
 *
 * ECHOES_VEH=NOM : véhicule de l'étape course: quand aucun n'est placé (carte de l'histoire).
 * ECHOES_MUSIC=musique.bin : musiques lues dans ce fichier comme sur la carte SD ; ECHOES_WAV=sortie.wav : le son de
 * chaque image (24 kHz mono, comme le firmware) y est écrit, musique comprise.
 *   battle:k                                combat contre l'ennemi k de ENNE (-1 : la Sentinelle de l'épave)
 *   equipe:m                                équipe imposée (bit i : PARTY[i], comme l'instruction « equipe »)
 *   tour:m                                  combat : c'est au membre m de choisir (éventail de commandes)
 *   enc                                     rencontre tirée pour le terrain sous Sera (comme au bout de la marche)
 *   hp:k:v                                  PV du membre k de l'équipe à v (essai de la défaite, du KO)
 *   tenue:m:k                               le membre m porte la tenue k de TENU (-1 : la sienne)
 *   job:m:j                                 le membre m prend le job j de JOBS (comme l'instruction « job »)
 *   donner:i:n                              n exemplaires de plus de l'objet i dans le sac
 *   arme:m:i                                le membre m porte l'objet d'équipement i comme arme (255 : aucune)
 *   attaque:m                               combat : le membre m attaque l'ennemi (effet de son arme, sans dégâts)
 *   technique:m:k                           combat : le membre m lance sa technique k sur l'ennemi (cut-in d'abord s'il en a)
 *   cutt:t                                  combat : le cut-in en cours ramené à t secondes (capture)
 *   cutimg:k:p                              combat : le cut-in en cours montre l'image k de IMAG (p = 1 : paire face / dos k, k + 1)
 *   ennemi                                  combat : l'ennemi joue son tour (rafale ou balayage)
 *   gagner                                  combat : l'ennemi tombe (puis le tableau de fin de combat)
 *   code:NNNN                               édition unique : le code du contenu 18+ (verrou18 avant et après) ; code:0 verrouille
 *   balayage                                combat : le tour de l'ennemi en cours devient un balayage de toute l'équipe
 *   fxt:t                                   combat : l'action en cours ramenée à t secondes (capture d'un effet)
 *   fx:k                                    combat : l'action en cours prend l'effet k (FX_IMPACT à FX_BALAYAGE ; couleur
 *                                           du premier type d'arme qui le porte, ou celle de l'effet puissant, sinon la couleur 1 du thème)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../core/eng.h"
#include "../core/internal.h"
#include "../core/audio.h"
#ifdef _WIN32
#include <windows.h>
static uint32_t clock_pc(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (uint32_t)(c.QuadPart * 1000000 / f.QuadPart); }
#else
static uint32_t clock_pc(void) { return (uint32_t)(clock() * 1000000.0 / CLOCKS_PER_SEC); }
#endif

/* Mémoire demandée par le moteur (ECHOES_MEM=1 : bilan après eng_init, 2 : et chaque réservation de 64 Ko et plus) :
   « lente » = PSRAM sur l'ESP32, « rapide » = SRAM interne s'il en reste, sinon PSRAM aussi.
   ECHOES_PSRAM=8011 : la carte simulée (Ko de PSRAM pour le moteur : 8 015 Ko au démarrage du firmware, moins ce qu'il
   prend avant eng_init ; relevé du 28/10/2026), 8 octets de gestion par bloc comme le tas de l'ESP-IDF ; ECHOES_SRAM :
   SRAM interne pour les réservations rapides (131 Ko par défaut : 143 libres avant eng_init, moins la réserve de 12 Ko
   de main.c). Au-delà, la réservation échoue, comme sur la carte. */
static size_t mem_lente, mem_rapide, psram_pris, sram_pris;
static void *alloc_pc(size_t size, int fast) {
    const char *ps = getenv("ECHOES_PSRAM"), *ss = getenv("ECHOES_SRAM");
    if (ps) {
        const size_t psram = (size_t)atoi(ps) * 1024, sram = (size_t)(ss ? atoi(ss) : 131) * 1024;
        if (fast && sram_pris + size <= sram) sram_pris += size;
        else if (psram_pris + size + 8 <= psram) psram_pris += size + 8;
        else {
            if (getenv("ECHOES_MEM")) printf("  ÉCHEC : %zu Ko demandés, %zu Ko de PSRAM libres sur la carte simulée\n", size / 1024, (psram - psram_pris) / 1024);
            return NULL;
        }
    }
    if (fast) mem_rapide += size; else mem_lente += size;
    if (size >= 64 * 1024 && getenv("ECHOES_MEM") && getenv("ECHOES_MEM")[0] == '2') printf("  réserve %zu Ko%s (total lent %zu Ko)\n", size / 1024, fast ? " rapide" : "", mem_lente / 1024);
    return malloc(size);
}
static int save_write_pc(int slot, const void *d, int n) {
    char name[32]; snprintf(name, sizeof name, "build/sauve%d.bin", slot);
    FILE *f = fopen(name, "wb"); if (!f) return -1;
    int w = (int)fwrite(d, 1, n, f); fclose(f); return w;
}
static int save_read_pc(int slot, void *d, int cap) {
    char name[32]; snprintf(name, sizeof name, "build/sauve%d.bin", slot);
    FILE *f = fopen(name, "rb"); if (!f) return -1;
    int r = (int)fread(d, 1, cap, f); fclose(f); return r;
}
static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

static FILE *wav_file;   /* ECHOES_WAV : son de chaque image */
static uint32_t wav_n;
static uint16_t img[ENG_SCREEN * ENG_SCREEN], band[ENG_SCREEN * ENG_NATIVE_BAND];
static int frame_no;

/* Plus grande corde du cercle dans les lignes [y, y + h), colonnes paires (même calcul que le firmware) */
static void band_window(int y, int h, int *x0, int *x1) {
    float r = ENG_SCREEN / 2.0f, best = 0;
    for (int j = y; j < y + h; j++) { float dy = j + 0.5f - r, v = r * r - dy * dy; if (v > best) best = v; }
    float hw = sqrtf(best);
    int a = (int)floorf(r - hw) & ~1, b = ((int)ceilf(r + hw) + 1) & ~1;
    *x0 = a < 0 ? 0 : a; *x1 = b > ENG_SCREEN ? ENG_SCREEN : b;
}

/* ECHOES_PLANCHES=1 : places du cache des planches (carte SD) servies à l'image ou à la précédente, par classe de taille,
   au plus fort sur tout le scénario (classes et nombres de places : ST_SIZE, ST_COUNT de world.c, recopiés ici) */
static const int PL_COUNT[6] = { 24, 16, 8, 16, 6, 2 };
static int pl_pic[6];
static void planches_mesure(void) {
    if (!E.w.st_who || !getenv("ECHOES_PLANCHES")) return;
    for (int c = 0, i0 = 0; c < 6; i0 += PL_COUNT[c], c++) {
        int n = 0;
        for (int i = i0; i < i0 + PL_COUNT[c] && i < E.w.st_nsl; i++) if (E.w.st_who[i] >= 0 && E.w.st_used[i] + 1 >= E.w.sframe) n++;
        if (n > pl_pic[c]) pl_pic[c] = n;
    }
}

static float fx_fix = -1;   /* fxt : instant imposé à l'action en cours jusqu'à l'étape suivante (images de mesure comprises) */
static uint32_t us_build;   /* préparation de la dernière image (eng_build_frame : cœur 0 sur la carte) */
static void frame(const eng_input_t *in) {
    eng_update(1 / 30.0f, in);
    if (fx_fix >= 0 && BT.phase == BP_ANIM) BT.an.t0 = E.clock - fx_fix;
    if (wav_file) {   /* une image de son : 800 échantillons à 24 kHz */
        int16_t s[800];
        eng_audio_mix(s, 800, 24000);
        fwrite(s, 2, 800, wav_file); wav_n += 800;
    }
    const uint32_t tb0 = clock_pc();
    eng_build_frame(frame_no++ / 30.0f);
    us_build = clock_pc() - tb0;
    planches_mesure();
    eng_present();
    for (int y = 0; y < ENG_SCREEN; y += ENG_NATIVE_BAND) {
        int h = ENG_SCREEN - y < ENG_NATIVE_BAND ? ENG_SCREEN - y : ENG_NATIVE_BAND;
        /* Bande rognée au cercle, comme sur la carte, puis replacée dans l'image */
        int x0, x1;
        band_window(y, h, &x0, &x1);
        eng_render_band_on((y / ENG_NATIVE_BAND) & 1, y, h, band, x0, x1);   /* bandes alternées entre les deux contextes, comme sur la carte */
        for (int j = 0; j < h; j++) {
            memset(img + (y + j) * ENG_SCREEN, 0, ENG_SCREEN * 2);
            memcpy(img + (y + j) * ENG_SCREEN + x0, band + j * (x1 - x0), (size_t)(x1 - x0) * 2);
        }
    }
}

/* Dernière image en BMP 24 bits (466 × 466, lignes complétées à 4 octets) ; 1 : échec */
static int ecrire_bmp(const char *nom) {
    FILE *o = fopen(nom, "wb");
    if (!o) { perror(nom); return 1; }
    const int W = ENG_SCREEN;
    uint32_t row = (W * 3 + 3) & ~3u, size = 54 + row * W;
    fputc('B', o); fputc('M', o); put32(o, size); put32(o, 0); put32(o, 54);
    put32(o, 40); put32(o, W); put32(o, W); put16(o, 1); put16(o, 24); put32(o, 0); put32(o, row * W);
    put32(o, 2835); put32(o, 2835); put32(o, 0); put32(o, 0);
    for (int y = W - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint16_t c = img[y * W + x];
            int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            fputc((b << 3) | (b >> 2), o); fputc((g << 2) | (g >> 4), o); fputc((r << 3) | (r >> 2), o);
        }
        for (uint32_t k = W * 3; k < row; k++) fputc(0, o);
    }
    fclose(o);
    return 0;
}
/* État de la police (police.c) : recherche, voitures poursuivantes, agents, arrestation */
static void police_etat(const char *quand) {
    static const char *MODE[4] = { "réseau", "droit", "ralliement", "garée" }, *ETAT[3] = { "chasse", "place", "départ" };
    printf("%s police : niveau %d, motif %d, %s, vue %d, semer %.1f / %.0f s, arrestation %d (%.1f s), Sera %s en %.1f %.1f, crédits %d\n", quand,
           POL.niveau, POL.motif, POL.clan ? "milice" : "Ordre", POL.vue, POL.semer, police_semer_s(), POL.arr, POL.arr_tt,
           E.ride >= 0 ? (E.npcs[E.ride].vole ? "dans un véhicule volé" : "en véhicule") : "à pied", E.sera.x, E.sera.z, E.credits);
    for (int k = 0; k < TRAFFIC_MAX; k++) {
        const traffic_t *t = &E.traffic[k];
        if (t->on && t->poursuite) printf("    voiture %d : %s, %s (%s), v %.1f, à %.1f cases, en %.1f %.1f, case %d %d -> %d %d\n", k, E.w.persos[t->perso].nom, MODE[t->mode & 3], t->lane ? "axe" : "rue", t->v, hypotf(t->x - E.sera.x, t->z - E.sera.z), t->x, t->z, t->ci, t->cj, t->ni, t->nj);
    }
    for (int k = 0; k < POL_AGENTS; k++) {
        const agent_t *a = &POL.ag[k];
        if (a->on) printf("    agent %d : %s, %s, allure %d, à %.1f cases\n", k, E.w.persos[a->perso].nom, ETAT[a->etat % 3], a->allure, hypotf(a->x - E.sera.x, a->z - E.sera.z));
    }
}

static FILE *scn_file, *mus_file, *sd_file, *intr_file;
static int intr_read_pc(uint32_t off, void *dst, uint32_t len) {
    return fseek(intr_file, (long)off, SEEK_SET) || fread(dst, 1, len, intr_file) != len ? -1 : 0;
}
static uint32_t sd_lus;   /* octets lus dans monde.pak (mode carte SD) */
static int sd_read_pc(uint32_t off, void *dst, uint32_t len) {
    sd_lus += len;
    return fseek(sd_file, (long)off, SEEK_SET) || fread(dst, 1, len, sd_file) != len ? -1 : 0;
}
static int mus_read_pc(uint32_t off, void *dst, uint32_t len) {
    return fseek(mus_file, (long)off, SEEK_SET) || fread(dst, 1, len, mus_file) != len ? -1 : 0;
}
static int scn_read_pc(uint32_t off, void *dst, uint32_t len) {
    return fseek(scn_file, (long)off, SEEK_SET) || fread(dst, 1, len, scn_file) != len ? -1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage : rendu_test monde.pak sortie.bmp [scénario]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *pak = malloc(len);
    if (fread(pak, 1, len, f) != (size_t)len) { fprintf(stderr, "lecture incomplète\n"); return 1; }
    fclose(f);

    eng_config_t cfg = { 0, alloc_pc, save_write_pc, save_read_pc, NULL, clock_pc, NULL, 0, NULL, 0, NULL, 0 };
    /* ECHOES_SD=monde.pak : le paquet donné est le paquet léger de la flash (tools/paquet_sd.js) ; chunks, planches et
       textures du sol sont lus dans ce fichier, comme sur la carte SD de l'ESP32 */
    const char *sd = getenv("ECHOES_SD");
    if (sd && (sd_file = fopen(sd, "rb"))) {
        fseek(sd_file, 0, SEEK_END); cfg.pak_size = (uint32_t)ftell(sd_file); cfg.pak_read = sd_read_pc;
        printf("carte SD : %s (%u octets)\n", sd, (unsigned)cfg.pak_size);
    }
    /* ECHOES_INTR=interieurs.bin : intérieurs lus dans ce fichier, comme sur la carte SD (sinon la section INTR) */
    const char *intr = getenv("ECHOES_INTR");
    if (intr && (intr_file = fopen(intr, "rb"))) {
        fseek(intr_file, 0, SEEK_END); cfg.intr_size = (uint32_t)ftell(intr_file); cfg.intr_read = intr_read_pc;
    }
    /* ECHOES_SCN=histoire.scn : scénario lu dans ce fichier, comme sur la carte SD (sinon la section SCEN du paquet) */
    const char *scn = getenv("ECHOES_SCN");
    if (scn && (scn_file = fopen(scn, "rb"))) {
        fseek(scn_file, 0, SEEK_END); cfg.scen_size = (uint32_t)ftell(scn_file); cfg.scen_read = scn_read_pc;
        printf("scénario : %s (%u octets)\n", scn, (unsigned)cfg.scen_size);
    }
    if (eng_init(pak, len, &cfg)) { fprintf(stderr, "eng_init : %s\n", eng_error()); if (getenv("ECHOES_MEM")) printf("eng_init : %s\n", eng_error()); return 1; }
    if (getenv("ECHOES_MEM")) printf("mémoire du moteur après eng_init : %zu Ko lente (PSRAM sur l'ESP32), %zu Ko rapide\n", mem_lente / 1024, mem_rapide / 1024);
    if (getenv("ECHOES_MEM") && getenv("ECHOES_PSRAM")) printf("carte simulée : %zu Ko de PSRAM pris, %zu Ko libres ; %zu Ko de SRAM\n", psram_pris / 1024, (size_t)atoi(getenv("ECHOES_PSRAM")) - psram_pris / 1024, sram_pris / 1024);
    /* Ouverture (écran titre, introduction, héroïne) : seulement si le scénario commence par « accueil » ; sinon nouvelle
       partie tout de suite, avec Sera */
    if (!(argc >= 4 && !strncmp(argv[3], "accueil", 7))) accueil_fin();
    const char *mus = getenv("ECHOES_MUSIC"), *wav = getenv("ECHOES_WAV");
    if (mus && (mus_file = fopen(mus, "rb"))) {
        fseek(mus_file, 0, SEEK_END);
        eng_music_attach((uint32_t)ftell(mus_file), mus_read_pc);
    }
    if (wav && (wav_file = fopen(wav, "wb"))) fseek(wav_file, 44, SEEK_SET);

    eng_input_t none = { 0 }, in;
    frame(&none);
    char script[2048] = "";
    if (argc >= 4) snprintf(script, sizeof script, "%s", argv[3]);
    for (char *tok = strtok(script, ","); tok; tok = strtok(NULL, ",")) {
        memset(&in, 0, sizeof in);
        fx_fix = -1;
        float a, b, c; int n, m2 = 150;
        if (!strcmp(tok, "accueil")) { printf("ouverture : %d cartons, %d héroïnes au choix (+ Sera), titre « %s »%c", E.w.ouv_ncartes, E.w.ouv_nher, E.w.ouv_titre, 10); continue; }
        else if (sscanf(tok, "heroine:%d", &n) == 1) {   /* héroïne k du choix (0 : Sera) */
            E.heroine = n > 0 && n <= E.w.ouv_nher ? E.w.ouv_her[n - 1] : -1;
            printf("héroïne : %s%c", E.heroine >= 0 ? E.w.persos[E.heroine].nom : "SERA", 10);
            continue;
        }
        else if (!strncmp(tok, "tenue:", 6)) {   /* Sera porte la tenue de ce nom (variante de couleur comprise) */
            const int t = tenue_named(0, tok + 6);
            E.tenue_eq[0] = (int8_t)t;
            printf("tenue %s : %d, variante %d%c", tok + 6, t, t >= 0 ? E.w.tenues[t].vremap : -1, 10);
            continue;
        }
        else if (!strcmp(tok, "menu")) in.keys = ENG_KEY_MENU;
        else if (!strcmp(tok, "ok")) in.keys = ENG_KEY_OK;
        else if (!strcmp(tok, "back")) in.keys = ENG_KEY_BACK;
        else if (!strcmp(tok, "up")) in.keys = ENG_KEY_UP;
        else if (!strcmp(tok, "down")) in.keys = ENG_KEY_DOWN;
        else if (!strcmp(tok, "left")) in.keys = ENG_KEY_LEFT;
        else if (!strcmp(tok, "right")) in.keys = ENG_KEY_RIGHT;
        else if (sscanf(tok, "tap:%f:%f", &a, &b) == 2) { in.touch = 1; in.touch_x = a; in.touch_y = b; }
        else if (sscanf(tok, "hold:%f:%f:%d", &a, &b, &n) == 3) {
            in.touch = 1; in.touch_x = a; in.touch_y = b;
            for (int k = 0; k < n - 1; k++) frame(&in);
            printf("doigt en %.0f %.0f (Sera à l'écran en %.0f %.0f) : %s\n", a, b, E.scr_sera.cx, E.scr_sera.cy,
                   E.sera.state == 2 ? "elle court" : E.sera.state == 1 ? "elle marche" : "immobile");
        } else if (sscanf(tok, "pouces:%f:%f:%d", &a, &b, &n) == 3) {
            /* Conduite à deux mains : pouce gauche posé en (110, 320), glissé de a px vers le haut ; pouce droit posé en
               (350, 330), glissé de b px vers la droite (en 5 images) ; tenus n images, puis levés ; état toutes les 15 */
            const float d0 = E.sera.dir;
            for (int k = 0; k < n; k++) {
                const float t = k < 5 ? k / 5.0f : 1;
                in.touch = 1; in.touch_x = 110; in.touch_y = 320 - a * t;
                in.touch2 = 1; in.touch2_x = 350 + b * t; in.touch2_y = 330;
                if (k & 1) {   /* l'ordre des doigts change d'une image à l'autre : chacun doit garder son rôle */
                    in.touch_x = 350 + b * t; in.touch_y = 330; in.touch2_x = 110; in.touch2_y = 320 - a * t;
                }
                frame(&in);
                if ((k + 1) % 15 == 0) printf("  %3d : th %.2f st %.2f, v %.2f, cap %+.0f deg\n", k + 1, E.drive_th, E.drive_st, E.ride_v, (E.sera.dir - d0) * 57.3f);
            }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "drag:%f:%f:%f:%d", &a, &b, &c, &n) == 4) {
            for (int k = 0; k <= n; k++) { in.touch = 1; in.touch_x = a; in.touch_y = b + (c - b) * k / n; frame(&in); }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "rim:%f:%f", &a, &b) == 2) {
            for (int k = 0; k <= 12; k++) {
                float t = (a + (b - a) * k / 12) * PI_F / 180;
                in.touch = 1; in.touch_x = 233 + sinf(t) * 210; in.touch_y = 233 - cosf(t) * 210;
                frame(&in);
            }
        } else if (sscanf(tok, "pinch:%f:%f", &a, &b) == 2) {
            for (int k = 0; k <= 12; k++) {
                float d = (a + (b - a) * k / 12) / 2;
                in.touch = in.touch2 = 1; in.touch_x = 233 - d; in.touch2_x = 233 + d; in.touch_y = in.touch2_y = 233;
                frame(&in);
            }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "pos:%f:%f:%f", &a, &b, &c) == 3) { E.sera.x = a; E.sera.z = b; E.yaw = c; }
        else if (!strncmp(tok, "pnj:", 4) || !strncmp(tok, "placer:", 7)) {
            /* pnj:NOM : Sera posée sur l'exemplaire de ce nom (contre lui) ; placer:NOM : à côté (sera_placer, comme
               « teleporter ») */
            const int sur = tok[1] == 'n';
            const char *nom = tok + (sur ? 4 : 7);
            for (int k = 0; k < E.nnpc; k++) if (!strcmp(E.w.persos[E.npcs[k].perso].nom, nom)) {
                eng_teleport(E.npcs[k].x, E.npcs[k].z, E.yaw);
                for (int im = 0; im < 3; im++) frame(&in);   /* chunks autour */
                if (!sur) sera_placer(E.npcs[k].x, E.npcs[k].z, 6);
                printf("%s en %.2f %.2f, Sera en %.2f %.2f (%.2f case)%c", nom, E.npcs[k].x, E.npcs[k].z, E.sera.x, E.sera.z,
                       hypotf(E.sera.x - E.npcs[k].x, E.sera.z - E.npcs[k].z), 10);
                break;
            }
            continue;
        }
        else if (!strcmp(tok, "ou")) { printf("Sera en %.2f %.2f%c", E.sera.x, E.sera.z, 10); continue; }
        else if (sscanf(tok, "carte:%d", &n) == 1) {   /* passer sur la carte n (CART), à son départ */
            if (n >= 0 && n < E.w.ncartes) world_carte(n, n ? E.w.cartes[n].sx : E.w.start_x, n ? E.w.cartes[n].sz : E.w.start_z);
        } else if (!strcmp(tok, "cartes")) {
            for (int k = 0; k < E.w.ncartes; k++) printf("carte %d : %s, %d × %d, ciel %d\n", k, E.w.cartes[k].nom, E.w.cartes[k].n, E.w.cartes[k].n, E.w.cartes[k].ciel);
            for (int k = 0; k < E.w.npass; k++) { const passage_t *P = &E.w.pass[k]; printf("passage %d : carte %d (%.1f, %.1f) -> carte %d (%.1f, %.1f)\n", k, P->carte, P->x, P->z, P->vers, P->ax, P->az); }
        }
        else if (sscanf(tok, "porte:%d", &n) == 1 && n >= 0 && n < E.w.nportes) {   /* Sera devant la porte n, face à elle */
            const int_porte_t *d = &E.w.portes[n];
            eng_teleport(d->x + d->nx * 0.5f, d->z + d->nz * 0.5f, atan2f(-d->nx, -d->nz));
            E.sera.dir = atan2f(-d->nx, -d->nz);
            printf("porte %d : bâtiment %d, mode %d, niveau %d, (%.1f %.1f)\n", n, d->bat, E.w.bats[d->bat].mode, E.w.bats[d->bat].niveau, d->x, d->z);
        }
        else if (sscanf(tok, "approche:%d", &n) == 1 && n >= 0 && n < E.w.nportes) {   /* Sera à 3 cases devant la porte n, face à elle */
            const int_porte_t *d = &E.w.portes[n];
            eng_teleport(d->x + d->nx * 3, d->z + d->nz * 3, atan2f(-d->nx, -d->nz));
        }
        else if (!strcmp(tok, "solides")) {   /* rectangles de collision à moins de 3 cases de Sera */
            const chunk_t *c = chunk_of_cell((int)E.sera.x, (int)E.sera.z);
            for (uint32_t k = 0; c && k < c->nsolid; k++) {
                const float *r = &c->solids[k * 4];
                if (r[0] < E.sera.x + 3 && r[2] > E.sera.x - 3 && r[1] < E.sera.z + 3 && r[3] > E.sera.z - 3) printf("  solide %.2f %.2f - %.2f %.2f%c", r[0], r[1], r[2], r[3], 10);
            }
            printf("  marge %.2f, ech %.2f%c", 0.35f / E.w.ech, E.w.ech, 10);
        }
        else if (!strcmp(tok, "etat")) {
            float bd = 1e9f; int bk = -1;
            for (int k = 0; k < E.w.nportes; k++) { const float l = hypotf(E.w.portes[k].x - E.sera.x, E.w.portes[k].z - E.sera.z); if (l < bd) { bd = l; bk = k; } }
            printf("état : Sera %.2f %.2f, porte devant %d, porte la plus proche %d à %.2f, dedans %d\n", E.sera.x, E.sera.z, E.porte, bk, bd, E.in.on);
            if (E.in.on) {
                printf("  intérieur : %d personnage(s) (exemplaires %d à %d), interlocuteur %d, commerce %d\n", E.in.npnj, E.in.npc0, E.in.npc0 + E.in.npcn - 1, E.talk, E.w.bats[E.in.bat].comm);
                for (int i = 0; i < E.in.npnj; i++) printf("    %s, métier %d, en %.2f %.2f\n", E.w.persos[E.in.pnj[i].perso].nom, E.in.pnj[i].metier, E.in.pnj[i].x, E.in.pnj[i].z);
            }
        }
        else if (sscanf(tok, "entrer:%d", &n) == 1) {   /* entrer par la porte n (forcée s'il le faut) */
            const int ok = int_entrer_porte(n, 1);
            printf("entrer %d : %s, %d polygones, %d objets, %d nœuds, grille %d × %d\n", n, ok ? "dedans" : "échec", E.in.npoly, E.in.nobj, E.in.nnode, E.in.gw, E.in.gh);
        }
        else if (sscanf(tok, "escalier:%d", &n) == 1 && E.in.on && n >= 0 && n < E.in.nesc) {
            const int_esc_t *e = &E.in.esc[n];
            E.sera.x = IO + (e->x0 + e->x1) / 2; E.sera.z = IO + (e->z0 + e->z1) / 2;
            printf("escalier %d (vers %+d) du niveau %d\n", n, e->vers, E.in.etage);
        }
        else if (sscanf(tok, "ipos:%f:%f:%f", &a, &b, &c) == 3) { E.sera.x = IO + a; E.sera.z = IO + b; E.yaw = c; }   /* dans l'intérieur (repère du plan) */
        else if (!strcmp(tok, "portes")) {
            printf("%d bâtiments visitables, %d portes, %d intérieurs\n", E.w.nbats, E.w.nportes, E.w.intr_n);
            for (int k = 0; k < E.w.nportes && k < 400; k++) { const int_porte_t *d = &E.w.portes[k]; if (!d->di) printf("  porte %d : bâtiment %d mode %d (%.1f %.1f)\n", k, d->bat, E.w.bats[d->bat].mode, d->x, d->z); }
        }
        else if (!strcmp(tok, "alarme")) { E.alarme.on = 1; E.alarme.t = 0; E.alarme.duree = 20; E.alarme.x = E.in.on ? E.in.rx : E.sera.x; E.alarme.z = E.in.on ? E.in.rz : E.sera.z; E.alarme.amende = 100; }
        else if (sscanf(tok, "pluie:%f", &a) == 1) { E.weather = a > 0 ? 2 : 1; E.rain = a; }
        else if (!strcmp(tok, "cendre")) {   /* pluie de cendre (game.c, cendre_update) : intensité, PV de l'équipe, passants à l'abri */
            int abri = 0, pres = 0, passants = 0, proches = 0; float pd = 1e9f, px = 0, pz = 0;
            for (int k = 0; k < E.nnpc; k++) {
                const npc_t *q = &E.npcs[k];
                const int fl = E.w.persos[q->perso].flags;
                abri += q->abri; pres += npc_present(q);
                if ((fl & PERS_FLANE) && PERS_GENRE(fl) && !q->meneur && !q->chef && !q->depart && !q->fuit) {   /* passant (abri_maj) */
                    const float d = hypotf(q->x - E.sera.x, q->z - E.sera.z);
                    passants++; proches += d < 24.0f / E.w.ech;
                    if (d < pd) { pd = d; px = q->x; pz = q->z; }
                }
            }
            printf("cendre %.2f dehors %.2f, exposition %.1f s, dernier coup il y a %.2f s :", E.rain, cendre_dehors(), E.cendre_t, E.clock - E.cendre_coup);
            for (int m = 0; m < 4; m++) if (in_team(m)) printf(" %s %d/%d", PARTY[m].name, PARTY[m].hp, PARTY[m].max);
            printf(", %d exemplaires présents, %d passants (%d à moins de 24 m, le plus proche en %.1f %.1f à %.1f cases), %d à l'abri",
                   pres, passants, proches, px, pz, pd, abri);
            for (int i = 0; i < n_items; i++) if (ITEMS[i].name && !strcmp(ITEMS[i].name, "MASQUE À FILTRE")) printf(", masque (objet %d) × %d", i, ITEMS[i].count);
            printf("%c", 10);
        }
        else if (sscanf(tok, "heure:%f", &a) == 1) { E.heure = a; E.cycle = 0; }   /* cycle jour / nuit : 0 minuit, 0,5 midi */
        else if (sscanf(tok, "horloge:%d", &n) == 1) { eng_set_horloge(n); if (n >= 0) E.cycle = 3; }   /* heure réelle (montre), cycle RÉEL */
        else if (!strcmp(tok, "verrou")) { eng_set_date(29, 10, 4); eng_set_batterie(78, 0); eng_verrouiller(); }   /* montre verrouillée (cadran) */
        else if (sscanf(tok, "arbre:%d", &n) == 1) {
            /* Sera 1,2 case derrière le n-ième grand décor (plus de 1,2 de haut) des chunks chargés, vu de la caméra */
            int found = 0;
            for (int s = 0; s < CWIN * CWIN && !found; s++) {
                const chunk_t *ch = E.w.slot[s];
                if (ch->cx < 0) continue;
                for (uint32_t k = 0; k < ch->nplant && !found; k++) {
                    const pak_plant_t *p = &ch->plants[k];
                    if (p->size <= 1.2f || hypotf(p->x - E.sera.x, p->z - E.sera.z) > 12 || n-- > 0) continue;
                    E.sera.x = p->x + sinf(E.yaw) * 1.2f; E.sera.z = p->z + cosf(E.yaw) * 1.2f; found = 1;
                    printf("arbre en %.1f %.1f (hauteur %.1f), Sera derrière\n", p->x, p->z, p->size);
                }
            }
        }
        else if (sscanf(tok, "wait:%d", &n) == 1) { for (int k = 0; k < n - 1; k++) frame(&none); }
        else if (!strncmp(tok, "cam:", 4)) {   /* cam:x:z : caméra de scène vers (x, z), coupe franche (chunks là-bas : world_stream) */
            float cx2, cz2;
            if (sscanf(tok + 4, "%f:%f", &cx2, &cz2) == 2) { mise_camera(0, cx2, cz2, -10, 0, 0.05f); frame(&none); }
        }
        else if (!strncmp(tok, "mise:", 5)) {   /* le script de serveur/essai-mise.js, par les fonctions de game.c */
            int p = -1;
            for (int q = 0; q < E.w.npersos && p < 0; q++) if (E.w.persos[q].nom && !strcmp(E.w.persos[q].nom, tok + 5)) p = q;
            if (p < 0) { fprintf(stderr, "personnage inconnu : %s\n", tok + 5); return 1; }
            const float x0 = E.sera.x, z0 = E.sera.z, deg = PI_F / 180;
            const float inst[] = { 0.5f, 1.2f, 2.5f, 3.3f, 3.8f, 5.0f, 6.5f, 8.0f, 10, 14 };
            int ph = 0, ii = 0;
            float t = 0, tw = 0;   /* tw : fin de l'attente en cours (caméra, fondu) */
            acteur_poser(p, x0 + 4, z0 + 4, atan2f(-4, -4));
            mise_camera(0, x0 + 2, z0 + 2, 45 * deg, 1.8f, 1); tw = 1;
            while (ii < 10) {
                const int k = acteur_de(p);
                if (t >= tw && !mise_marche(-2) && !MISE.fuite_on) switch (ph++) {   /* instruction suivante (comme le script) */
                    case 0: MISE.act[k].tx = x0 + 1; MISE.act[k].tz = z0 + 1; MISE.act[k].v = 1; MISE.act[k].walking = 1; break;
                    case 1: E.sera.dir = atan2f(MISE.act[k].x - E.sera.x, MISE.act[k].z - E.sera.z); MISE.sec_f = 2; MISE.sec_t = 0.5f;
                            MISE.fondu_a = MISE.fondu; MISE.fondu_b = 1; MISE.fondu_t = 0; MISE.fondu_d = 0.5f; tw = t + 0.5f; break;
                    case 2: MISE.fondu_a = MISE.fondu; MISE.fondu_b = 0; MISE.fondu_t = 0; MISE.fondu_d = 0.5f; tw = t + 0.5f; break;
                    case 3: MISE.sera_on = 1; MISE.sera_tx = x0 - 2; MISE.sera_tz = z0; MISE.sera_v = 2; break;
                    case 4: mise_camera(1, 0, 0, 0, 0, 0.8f); tw = t + 0.8f; break;
                    case 5: mise_fuite(x0 - 6, z0, 1.5f, 8, p, 2); break;
                    case 6: printf("fuite %s\n", MISE.fuite_ok ? "réussie" : "ratée"); mise_fin(0); break;
                    default: break;
                }
                frame(&none); t += 1 / 30.0f;
                if (t >= inst[ii]) {
                    printf("%.1f s cam %s %.2f %.2f %.2f %.2f | sera %.2f %.2f %d | fondu %.2f | fuite %.1f |", inst[ii], MISE.cam_on ? "oui" : "non", MISE.cam[0], MISE.cam[1], MISE.cam[2], MISE.cam[3],
                           E.sera.x, E.sera.z, E.sera.state, MISE.fondu, MISE.fuite_on ? MISE.fuite_t : -1);
                    for (int a = 0; a < ACT_MAX; a++) if (MISE.act[a].on) printf(" [%.2f %.2f %s%s]", MISE.act[a].x, MISE.act[a].z, MISE.act[a].walking ? (MISE.act[a].court ? "run" : "walk") : "idle", MISE.act[a].chasse ? " chasse" : "");
                    printf("\n");
                    ii++;
                }
            }
            continue;
        }
        else if (!strncmp(tok, "cible:", 6)) {   /* cible:NOM:ETAPE : cible de chasse (CIBL) à 3 cases devant la caméra, quête 0 à
                                                    l'étape 2 ; la quête 0 mise à ETAPE (comme serveur/essai-cibles.js) */
            char nom[64]; snprintf(nom, sizeof nom, "%s", tok + 6);
            char *dp = strrchr(nom, ':'); const int st = dp ? atoi(dp + 1) : 2; if (dp) *dp = 0;
            int p = -1;
            for (int pq = 0; pq < E.w.npersos && p < 0; pq++) if (E.w.persos[pq].nom && !strcmp(E.w.persos[pq].nom, nom)) p = pq;
            if (p < 0 || !E.w.sc_nq) { fprintf(stderr, "personnage inconnu ou scénario sans quête : %s\n", nom); return 1; }
            cible_t *cb = &E.w.cibles[0]; E.w.ncibles = 1;
            cb->perso = (uint8_t)p; cb->carte = (uint8_t)E.w.carte; cb->quest = 0; cb->step = 2;
            cb->x = E.sera.x + E.cam.sy * 3; cb->z = E.sera.z + E.cam.cy * 3;
            E.quests[0] = (uint8_t)st;
            for (int k = 0; k < 3; k++) frame(&none);
            printf("cible %s en %.2f %.2f, quête 0 à l'étape %d (cible à l'étape 2)\n", nom, cb->x, cb->z, st);
            continue;
        }
        else if (!strncmp(tok, "palette:", 8)) {   /* palette:NOM : palette d'un combat du scénario contre NOM (celle de ce
                                                      qu'on voit : palette_vue de battle.c), puis 12 rencontres (au hasard) */
            int p = -1, k = -1;
            for (int pq = 0; pq < E.w.npersos && p < 0; pq++) if (E.w.persos[pq].nom && !strcmp(E.w.persos[pq].nom, tok + 8)) p = pq;
            for (int q = 0; q < E.w.nenemies && k < 0; q++) if (E.w.enemies[q].perso == p) k = q;
            if (k < 0) { fprintf(stderr, "ennemi inconnu : %s\n", tok + 8); return 1; }
            if (!E.w.ncibles) for (int j = 0; j < E.nnpc; j++) if (E.npcs[j].perso == p && perso_present(p)) {   /* sans cible : à côté d'un exemplaire posé */
                E.sera.x = E.npcs[j].x + 2; E.sera.z = E.npcs[j].z;
                printf("exemplaire posé en %.1f %.1f, palette %d ; ", E.npcs[j].x, E.npcs[j].z, E.npcs[j].var);
                break;
            }
            printf("%s (%d palettes en plus de la base) : combat du scénario", tok + 8, E.w.persos[p].nvar);
            for (int r = 0; r < 4; r++) { battle_start(k, 5, 1); printf(" %d", BT.var); BT.on = 0; }
            printf(" ; rencontres");
            for (int r = 0; r < 12; r++) { battle_start(k, 5, 0); printf(" %d", BT.var); BT.on = 0; }
            printf("\n");
            continue;
        }
        else if (!strncmp(tok, "hscript:", 8)) {   /* hscript:21.5 : comme l'instruction « heure » (OP_HOUR de script.c), en heures */
            const float h = (float)atof(tok + 8) / 24;
            if (E.cycle != 3) E.heure = h - floorf(h);
            for (int k = 0; k < 3; k++) frame(&none);
            printf("heure %.4f\n", E.heure);
            continue;
        }
        else if (!strcmp(tok, "voiture")) {
            /* Sera 3 cases derrière le premier véhicule du trafic sur un axe, caméra dans son sens de marche */
            int k = 0;
            while (k < TRAFFIC_MAX && !(E.traffic[k].on && E.traffic[k].lane)) k++;
            if (k == TRAFFIC_MAX) { printf("aucun véhicule sur un axe\n"); continue; }
            const traffic_t *t = &E.traffic[k];
            E.sera.x = t->x - sinf(t->dir) * 3; E.sera.z = t->z - cosf(t->dir) * 3; E.yaw = t->dir;
            printf("derrière le trafic %d en %.1f %.1f\n", k, t->x, t->z);
        }
        else if (sscanf(tok, "son:%d", &n) == 1) {
            /* Joue le son n, mixe 0,5 s à 24 kHz : crête et énergie */
            static int16_t buf[12000];
            son_jouer(n, 1, 1);
            eng_audio_mix(buf, 12000, 24000);
            double e = 0; int pk = 0;
            for (int i = 0; i < 12000; i++) { e += (double)buf[i] * buf[i]; if (abs(buf[i]) > pk) pk = abs(buf[i]); }
            printf("son %d : crête %d, efficace %.0f\n", n, pk, sqrt(e / 12000));
        }
        else if (!strcmp(tok, "mix")) {
            /* Mixe 0,2 s à 24 kHz de ce qui joue déjà (boucles des moteurs comprises) : crête et énergie */
            static int16_t buf[4800];
            eng_audio_mix(buf, 4800, 24000);
            double e = 0; int pk = 0;
            for (int i = 0; i < 4800; i++) { e += (double)buf[i] * buf[i]; if (abs(buf[i]) > pk) pk = abs(buf[i]); }
            printf("mix : crête %d, efficace %.0f (véhicule conduit %d, course %d)\n", pk, sqrt(e / 4800), E.ride, CR.on);
        }
        else if (!strcmp(tok, "vol")) {
            int k = 0;
            while (k < TRAFFIC_MAX && !E.traffic[k].on) k++;
            printf("vol du trafic %d : %s\n", k, k < TRAFFIC_MAX && vehicle_steal(k) ? "conduit" : "échec");
        }
        else if (sscanf(tok, "casse:%d", &n) == 1) {
            /* Monte dans le premier véhicule placé, se met 4 cases avant le n-ième décor solide (drapeau 4) des chunks
               chargés, face à lui, et fonce dessus (au plus 3 s) : le décor casse-t-il ? */
            int k = 0;
            while (k < E.nnpc && !(E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE)) k++;
            if (k == E.nnpc) { printf("aucun véhicule placé\n"); continue; }
            E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z;
            if (!vehicle_board(k)) { printf("montée impossible\n"); continue; }
            world_stream(E.sera.x, E.sera.z, 1);
            const pak_plant_t *cible = NULL;
            for (int s = 0; s < CWIN * CWIN && !cible; s++) {
                const chunk_t *ch = E.w.slot[s];
                if (ch->cx < 0) continue;
                for (uint32_t m = 0; m < ch->nplant && !cible; m++) if ((ch->plants[m].flags & 4) && n-- <= 0) cible = &ch->plants[m];
            }
            if (!cible) { printf("aucun décor solide\n"); continue; }
            const float px = cible->x, pz = cible->z, a = atan2f(px - E.sera.x, pz - E.sera.z);
            E.sera.x = px - sinf(a) * 4; E.sera.z = pz - cosf(a) * 4; E.sera.dir = E.yaw = a; E.ride_v = 0;
            world_stream(E.sera.x, E.sera.z, 1);
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            float t, dx, dz; int f2 = 0;
            while (f2 < 90 && !decor_casse(px, pz, &t, &dx, &dz)) { frame(&up); f2++; }
            printf("décor %s en %.1f %.1f : %s après %d images, v %.2f, Sera à %.1f case\n", cible->flags & 8 ? "rocher" : "arbre", px, pz,
                   f2 < 90 ? "cassé" : "intact", f2, E.ride_v, hypotf(E.sera.x - px, E.sera.z - pz));
            /* Sera 4 cases à côté du décor, caméra vers lui : la chute se voit de profil */
            E.ride_v = 0; E.sera.x = px - cosf(a) * 4; E.sera.z = pz + sinf(a) * 4; E.yaw = a + PI_F / 2;
        }
        else if ((m2 = 150, sscanf(tok, "lac:%d:%d", &n, &m2)) >= 1 || sscanf(tok, "riviere:%d:%d", &n, &m2) >= 1) {
            /* Véhicule ECHOES_VEH (sinon la première voiture) à 4 cases du bord de l'eau la plus proche (lac de plus de 2 m,
               ou rivière), tourné vers elle : n images gaz tenu, puis 150 sans rien ; état toutes les 15 images */
            const int riv = tok[0] == 'r';
            int p = -1;
            for (int q = 0; q < E.w.npersos && p < 0; q++)
                if ((E.w.persos[q].flags & PERS_VEHICULE) && (getenv("ECHOES_VEH") ? !strcmp(E.w.persos[q].nom, getenv("ECHOES_VEH")) : VEH_TYPE(E.w.persos[q].flags) == VEH_VOITURE)) p = q;
            if (p < 0) { printf("aucun véhicule\n"); continue; }
            int li = -1, lj = -1; float bd = 1e9f;
            for (int j = (int)E.sera.z - 40; j <= (int)E.sera.z + 40; j++)
                for (int i = (int)E.sera.x - 40; i <= (int)E.sera.x + 40; i++) {
                    const chunk_t *ch = chunk_of_cell(i, j);
                    if (!ch) continue;
                    const uint8_t fl = ch->flags[cell_local(i, j)];
                    if ((fl & F_ROAD) || !(riv ? (fl & F_RIVER) : (fl & F_WATER) && !(fl & F_RIVER) && eau_prof(i + 0.5f, j + 0.5f) > 2 / E.w.ech)) continue;
                    const float d = hypotf(i + 0.5f - E.sera.x, j + 0.5f - E.sera.z);
                    if (d < bd) { bd = d; li = i; lj = j; }
                }
            if (li < 0) { printf("pas d'eau à moins de 40 cases\n"); continue; }
            float ux = E.sera.x - (li + 0.5f), uz = E.sera.z - (lj + 0.5f), ul = hypotf(ux, uz), sx = li + 0.5f, sz = lj + 0.5f;
            if (ul < 0.01f) { ux = 1; uz = 0; ul = 1; }
            ux /= ul; uz /= ul;
            while (eau_prof(sx, sz) > 0) { sx += ux * 0.25f; sz += uz * 0.25f; }   /* jusqu'à la rive */
            sx += ux * 4; sz += uz * 4;
            const int k = vehicle_spawn(p, sx, sz, atan2f(-ux, -uz));
            E.sera.x = sx; E.sera.z = sz; world_stream(sx, sz, 1);
            if (k < 0 || !vehicle_board(k)) { printf("montée impossible\n"); continue; }
            E.yaw = E.sera.dir;
            printf("%s : %s (roule %d) en %.1f %.1f, eau en %d %d\n", riv ? "rivière" : "lac", E.w.persos[p].nom, veh_rolls(p), sx, sz, li, lj);
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            for (int f2 = 1; f2 <= n + m2; f2++) {
                frame(f2 <= n ? &up : &none);
                if (f2 % 15 == 0) printf("  %3d : %s en %.1f %.1f, v %.2f, eau %.2f m, coule %.2f, alt %.2f m, nage %d\n", f2, E.ride >= 0 ? "conduit" : "à pied",
                                         E.sera.x, E.sera.z, E.ride_v, eau_prof(E.sera.x, E.sera.z) * E.w.ech, E.ride_coule, E.ride_alt * E.w.ech, E.nage);
                if (f2 == n + m2 || (E.ride < 0 && f2 % 15 == 0)) break;
            }
        }
        else if (sscanf(tok, "conduite:%d", &n) == 1) {
            /* Monte dans le premier véhicule placé, roule n images vers le haut, puis lâche : vitesse toutes les 5 images */
            int k = 0;
            while (k < E.nnpc && !(E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE)) k++;
            if (k == E.nnpc) { printf("aucun véhicule placé\n"); continue; }
            E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z;
            if (!vehicle_board(k)) { printf("montée impossible\n"); continue; }
            printf("conduite : %s en %.1f %.1f\n", E.w.persos[E.npcs[k].perso].nom, E.sera.x, E.sera.z);
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            for (int f2 = 1; f2 <= n + 30; f2++) { frame(f2 <= n ? &up : &none); if (f2 % 5 == 0) printf("%s%.2f", f2 == 5 ? "  v :" : " ", E.ride_v); }
            printf("\n");
        }
        else if (sscanf(tok, "vol:%d", &n) == 1) {
            /* Vol permanent (système solaire) : gaz tenu n images ; état toutes les 60 images (à bord, position, vitesse,
               altitude, carte), arrêt à la première rencontre ; puis le coût du rendu (moyenne et pire image) */
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            double s3 = 0, sui = 0, pire = 0, tris = 0; int nf = 0;
            for (int f2 = 1; f2 <= n; f2++) {
                frame(&up);
                if (BT.on) { printf("  %4d : rencontre, %s niveau %d\n", f2, BT.name, BT.lvl); break; }
                const eng_stats_t *st = eng_stats();
                s3 += st->us_3d; sui += st->us_ui; tris += st->tris; nf++;
                if (st->us_3d + st->us_ui > pire) pire = st->us_3d + st->us_ui;
                if (f2 % 60 == 0) printf("  %4d : carte %d, %s, en %.1f %.1f, v %.2f, alt %.2f, cap %.2f\n", f2, E.w.carte, E.ride >= 0 ? "à bord" : "à pied", E.sera.x, E.sera.z, E.ride_v, E.ride_alt, E.sera.dir);
            }
            if (nf) printf("  rendu en vol (%d images) : 3D %.2f ms, interface %.2f ms, pire %.2f ms ; %.0f triangles\n", nf, s3 / nf / 1000, sui / nf / 1000, pire / 1000, tris / nf);
        }
        else if (sscanf(tok, "cap:%f", &a) == 1) { E.sera.dir = a; E.yaw = a; if (E.ride >= 0) E.npcs[E.ride].dir = a; continue; }   /* Sera (et son véhicule) tournés, caméra derrière */
        else if (!strcmp(tok, "astres")) {   /* astres de la carte du système (ASTR), visités ou non */
            for (int k = 0; k < E.w.nastres; k++) { const astre_t *A = &E.w.astres[k]; printf("astre %d : %s, carte %d, vers %d, visité %d, (%.1f, %.1f) r %.1f, orbite (%.1f, %.1f)\n", k, A->nom, A->carte, A->vers, astre_visite(k), A->x, A->z, A->r, A->ax, A->az); }
            printf("vol permanent : %d, à bord : %d, alt %.2f, Sera en %.1f %.1f cap %.2f\n", vol_permanent(), E.ride, E.ride_alt, E.sera.x, E.sera.z, E.sera.dir);
            continue;
        }
        else if (sscanf(tok, "course:%d", &n) == 1) {
            /* Monte dans le premier véhicule placé, va sur la ligne de départ, lance la course et pilote n images (W tenu,
               braquage vers l'axe 3 cases devant) : état toutes les 60 images, classement à la fin */
            int k = 0;
            while (k < E.nnpc && !(E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE)) k++;
            if (k == E.nnpc && getenv("ECHOES_VEH")) {   /* ECHOES_VEH=NOM : un exemplaire de ce véhicule (aucun placé : carte de l'histoire) */
                for (int p = 0; p < E.w.npersos && k == E.nnpc; p++)
                    if ((E.w.persos[p].flags & PERS_VEHICULE) && !strcmp(E.w.persos[p].nom, getenv("ECHOES_VEH"))) { const int sv = vehicle_spawn(p, E.sera.x, E.sera.z, 0); if (sv >= 0) k = sv; }
            }
            if (k == E.nnpc || !PISTE.n) { printf("course : %s\n", PISTE.n ? "aucun véhicule placé" : "pas de piste (PIS2)"); continue; }
            E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z;
            vehicle_board(k); E.ride_phase = 0; E.ride_alt = 0;
            E.sera.x = PISTE.X[PISTE.dep]; E.sera.z = PISTE.Z[PISTE.dep]; world_stream(E.sera.x, E.sera.z, 1);
            frame(&none);
            const int ok = course_ok(), dep = course_start(E.npcs[k].perso, PISTE.tours, NULL, getenv("ECHOES_RIVAUX") ? atoi(getenv("ECHOES_RIVAUX")) : COURSE_N - 1, 1, 1);   /* ECHOES_RIVAUX : adversaires */
            printf("course : drapeau %d, départ %d (%s, piste %d points, %.1f cases)\n", ok, dep, E.w.persos[E.npcs[k].perso].nom, PISTE.n, PISTE.L);
            int ntq = 0, tq0 = 0; float slipmax = 0;   /* tête-à-queue du joueur, plus fort dérapage */
            int pf_n = 0, pf_maxf = 0; double pf_bu = 0, pf_fx = 0, pf_3d = 0, pf_ui = 0, pf_tris = 0, pf_px = 0, pf_spr = 0, pf_ops = 0, pf_cat[5] = { 0 }, pf_max = 0;
            for (int f2 = 1; f2 <= n && CR.on; f2++) {
                const racer_t *j = &CR.r[CR.n - 1];
                if (j->spin > 0 && !tq0) { ntq++; printf("  tête-à-queue à l'image %d : dérapage %.2f rad, rotation %.1f rad/s, v %.1f\n", f2, j->slip, j->w, j->spd); }
                tq0 = j->spin > 0; if (fabsf(j->slip) > slipmax) slipmax = fabsf(j->slip);
                const int e = (j->seg + 6) % PISTE.n;
                float ang = atan2f(PISTE.X[e] - j->x, PISTE.Z[e] - j->z) - j->dir;
                ang = atan2f(sinf(ang), cosf(ang));
                eng_input_t dr = { 0 };
                dr.keys = ENG_KEY_UP | (ang > 0.12f ? ENG_KEY_RIGHT : ang < -0.12f ? ENG_KEY_LEFT : 0);
                /* ECHOES_PILOTE=gaz : gaz seul, sans direction ; =hasard : gauche, droite ou rien tiré toutes les 15 images
                   (pilote très imprécis) */
                const char *pm = getenv("ECHOES_PILOTE");
                if (pm && !strcmp(pm, "gaz")) dr.keys = ENG_KEY_UP;
                else if (pm && !strcmp(pm, "hasard")) {
                    static uint32_t hs = 12345, gk;
                    if (f2 % 15 == 1) { hs = hs * 1103515245u + 12345u; gk = (hs >> 16) % 3; }
                    dr.keys = ENG_KEY_UP | (gk == 1 ? ENG_KEY_LEFT : gk == 2 ? ENG_KEY_RIGHT : 0);
                }
                if (CR.phase == CP_RESULTATS) dr.keys = 0;
                frame(&dr);
                if (CR.phase == CP_COURSE) {   /* coût du rendu pendant la course : moyennes et pire image */
                    const eng_stats_t *st = eng_stats();
                    const double u = st->us_3d + st->us_ui;
                    int nfx = 0; for (int q = 0; q < COURSE_FX; q++) nfx += CR.fx[q].t < CR.fx[q].life;
                    pf_n++; pf_bu += us_build; pf_fx += nfx; pf_3d += st->us_3d; pf_ui += st->us_ui; pf_tris += st->tris; pf_px += st->px; pf_spr += st->sprites; pf_ops += st->ui_ops;
                    for (int c = 0; c < 5; c++) pf_cat[c] += st->cat_tris[c];
                    if (u > pf_max) { pf_max = u; pf_maxf = f2; }
                }
                if (f2 % 60 == 0) printf("  %4d : phase %d, place %d, tour %d, prog %.1f, v %.2f\n", f2, CR.phase, course_rank(NULL), j->tour, j->prog, j->v);
            }
            printf("  joueur : %d tête-à-queue, dérapage maxi %.2f rad\n", ntq, slipmax);
            if (pf_n) printf("  rendu en course (%d images) : préparation %.2f ms, effets %.1f, 3D %.2f ms, interface %.2f ms, pire %.2f ms (image %d) ; triangles %.0f (sol %.0f, eau %.0f, plantes %.0f, routes %.0f, bâtiments et décor %.0f), pixels %.0f, sprites %.1f, interface %.0f opérations\n",
                             pf_n, pf_bu / pf_n / 1000, pf_fx / pf_n, pf_3d / pf_n / 1000, pf_ui / pf_n / 1000, pf_max / 1000, pf_maxf, pf_tris / pf_n, pf_cat[0] / pf_n, pf_cat[1] / pf_n, pf_cat[2] / pf_n, pf_cat[3] / pf_n, pf_cat[4] / pf_n, pf_px / pf_n, pf_spr / pf_n, pf_ops / pf_n);
            if (CR.on) {
                int o[COURSE_N]; course_rank(o);
                for (int i = 0; i < CR.n; i++) printf("  %d. %s%s %.2f s (tour %d)\n", i + 1, E.w.persos[CR.r[o[i]].perso].nom, CR.r[o[i]].joueur ? " (joueur)" : "", CR.r[o[i]].fin, CR.r[o[i]].tour);
            }
        }
        else if (!strncmp(tok, "essai:", 6)) {   /* course d'essai pilotée de la montre (SDX COURSE k a) */
            int k = 0, al = 1; sscanf(tok + 6, "%d:%d", &k, &al);
            printf("essai : %d\n", eng_essai_course(k, al));
        }
        else if (sscanf(tok, "battle:%d", &n) == 1) battle_start(n, 0, 0);
        else if (!strcmp(tok, "gagner") && BT.on) { BT.hp = 0; BT.phase = BP_DYING; BT.t0 = E.clock; }
        else if (sscanf(tok, "equipe:%d", &n) == 1) E.team = n & 15;
        else if (sscanf(tok, "tour:%d", &n) == 1 && BT.on && n >= 0 && n < 4) { BT.actor = n; BT.phase = BP_CMD; BT.locked = 0; }
        else if (sscanf(tok, "piste:%d", &n) == 1) {
            /* Sera sur la ligne de départ de la piste n (PIS2), qui devient la piste active */
            const piste_t *pp = course_piste(n);
            if (!pp) { printf("piste %d : aucune (%d en tout)\n", n, course_npistes()); continue; }
            E.sera.x = pp->X[pp->dep]; E.sera.z = pp->Z[pp->dep]; course_choisir(E.sera.x, E.sera.z); world_stream(E.sera.x, E.sera.z, 1);
            printf("piste %d / %d : %d points, %.1f cases, départ en %.1f %.1f\n", n, course_npistes(), pp->n, pp->L, E.sera.x, E.sera.z);
        }
        else if (!strcmp(tok, "menucourse")) printf("menu de course : %d\n", hud_course_menu(-1, -1, -1, -1));
        else if (!strcmp(tok, "organisateur")) {
            /* Sera devant l'exemplaire du personnage du premier événement « parler » (monde de course), tournée vers lui */
            int k = -1;
            for (int e = 0; e < E.w.sc_nev && k < 0; e++)
                for (int m = 0; E.w.sc_ev[e].trigger == 2 && m < E.nnpc && k < 0; m++) if (E.npcs[m].perso == E.w.sc_ev[e].perso) k = m;
            if (k < 0) { printf("pas d'organisateur\n"); continue; }
            E.sera.x = E.npcs[k].x - 1.2f; E.sera.z = E.npcs[k].z; E.yaw = PI_F / 2;
            printf("organisateur %s en %.1f %.1f\n", E.w.persos[E.npcs[k].perso].nom, E.npcs[k].x, E.npcs[k].z);
        }
        else if (sscanf(tok, "drapeau:%d", &n) == 1 && n >= 0 && n < SC_MAX_FLAGS) E.flags[n >> 3] |= (uint8_t)(1 << (n & 7));
        else if (!strcmp(tok, "finscript")) script_abort();
        else if (!strncmp(tok, "parler:", 7)) {
            int k = -1;
            for (int m = 0; m < E.nnpc && k < 0; m++) if (!strcmp(E.w.persos[E.npcs[m].perso].nom, tok + 7)) k = m;
            if (k < 0) { printf("parler : %s absent\n", tok + 7); continue; }
            E.sera.x = E.npcs[k].x - 1.2f; E.sera.z = E.npcs[k].z; E.yaw = PI_F / 2;
            world_stream(E.sera.x, E.sera.z, 1);
            printf("devant %s en %.1f %.1f\n", tok + 7, E.npcs[k].x, E.npcs[k].z);
        }
        else if (!strcmp(tok, "boost")) { if (CR.on) CR.r[CR.n - 1].boost = 0.8f; }   /* COURSE_PLAQUE_T de course.c */
        else if (sscanf(tok, "pilote:%d", &n) == 1) {
            /* Course en cours (menu de course validé) : n images pilotées comme course:n, puis l'état */
            for (int f2 = 1; f2 <= n && CR.on && CR.phase != CP_RESULTATS; f2++) {
                const racer_t *j = &CR.r[CR.n - 1];
                const int e = (j->seg + 6) % PISTE.n;
                float ang = atan2f(PISTE.X[e] - j->x, PISTE.Z[e] - j->z) - j->dir;
                ang = atan2f(sinf(ang), cosf(ang));
                eng_input_t dr = { 0 };
                dr.keys = ENG_KEY_UP | (ang > 0.12f ? ENG_KEY_RIGHT : ang < -0.12f ? ENG_KEY_LEFT : 0);
                frame(&dr);
            }
            if (CR.on) {
                int o[COURSE_N]; course_rank(o);
                printf("course : phase %d, piste %s, à battre %.0f s, boost %.2f, halo %.2f\n", CR.phase, PISTE.nom, CR.chrono, CR.r[CR.n - 1].boost, CR.r[CR.n - 1].halo);
                for (int i = 0; i < CR.n; i++) printf("  %d. %s%s %.2f s (tour %d)\n", i + 1, E.w.persos[CR.r[o[i]].perso].nom, CR.r[o[i]].joueur ? " (joueur)" : "", CR.r[o[i]].fin, CR.r[o[i]].tour);
            } else printf("course finie : place %d\n", course_place());
        }
        else if (!strcmp(tok, "enc")) { int k = encounter_pick(); printf("rencontre : ennemi %d\n", k); if (k >= 0) battle_start(k, encounter_level(), 0); }
        else if (sscanf(tok, "hp:%d:%f", &n, &a) == 2 && n >= 0 && n < 4) PARTY[n].hp = (int)a;
        else if (sscanf(tok, "ehp:%d", &n) == 1) BT.hp = n;
        else if (sscanf(tok, "tenue:%d:%f", &n, &a) == 2 && n >= 0 && n < 4) E.tenue_eq[n] = (int8_t)a;
        else if (sscanf(tok, "job:%d:%f", &n, &a) == 2 && n >= 0 && n < 4) party_set_job(n, (int)a);
        else if (sscanf(tok, "donner:%d:%f", &n, &a) == 2 && n >= 0 && n < n_items) ITEMS[n].count += (int)a;
        else if (sscanf(tok, "arme:%d:%f", &n, &a) == 2 && n >= 0 && n < 4) {   /* arme portée sans passer par le sac */
            E.eq[n][EQ_ARME] = (uint8_t)a; party_refresh(n);
            const eqitem_t *r = eq_item((int)a);
            printf("arme du membre %d : objet %d, effet %d\n", n, (int)a, r ? E.w.eqtypes[r->cat].effet : -1);
        }
        else if (sscanf(tok, "attaque:%d", &n) == 1 && BT.on && n >= 0 && n < 4) {   /* le membre n attaque l'ennemi */
            BT.actor = n; BT.phase = BP_CMD; battle_command(BC_ATTACK, 0);
            BT.locked = 1; battle_tap(E.scr_enemy.cx, E.scr_enemy.cy);
            BT.an.hit = 1;   /* pas de dégâts : l'ennemi reste debout d'une attaque à l'autre */
        }
        else if (sscanf(tok, "technique:%d:%f", &n, &a) == 2 && BT.on && n >= 0 && n < 4) {   /* technique k du membre n */
            BT.actor = n; BT.phase = BP_TECH; PARTY[n].mp = PARTY[n].maxmp; battle_command(BC_TECH, (int)a);
            if (BT.phase == BP_TARGET) { BT.locked = 1; battle_tap(E.scr_enemy.cx, E.scr_enemy.cy); }
            printf("technique %d du membre %d : phase %d, cut-in %d\n", (int)a, n, BT.phase, BT.cut.img);
        }
        else if (sscanf(tok, "cutt:%f", &a) == 1 && BT.on && BT.phase == BP_CUTIN) BT.cut.t0 = E.clock - a;
        else if (sscanf(tok, "cutimg:%d:%f", &n, &a) == 2 && BT.on && BT.phase == BP_CUTIN) { BT.cut.img = n; BT.cut.paire = a > 0; BT.cut.plein = 0; }
        else if (!strncmp(tok, "code:", 5)) {
            const int v0 = verrou18();
            if (!strcmp(tok + 5, "0")) edition_verrouiller(); else edition_code(tok + 5);
            int nm = 0; for (int i = 0; i < E.w.npersos; i++) nm += E.w.persos[i].masque;
            printf("code %s : édition %d, verrou %d -> %d, %d personnages masqués\n", tok + 5, E.w.edition, v0, verrou18(), nm);
        }
        else if (!strcmp(tok, "ennemi") && BT.on) { BT.actor = BT_ENEMY; BT.phase = BP_ENEMY; BT.t0 = E.clock - 1; }
        else if (!strcmp(tok, "balayage") && BT.on && BT.phase == BP_ANIM && BT.an.who == BT_ENEMY) {   /* tour de l'ennemi changé en balayage */
            BT.an.all = 1; BT.an.fx = FX_BALAYAGE; BT.an.col = 0xFF7A3Du; BT.an.ntarget = 0;
            for (int k = 0; k < 4; k++) if (in_team(k) && PARTY[k].hp > 0) BT.an.targets[BT.an.ntarget++] = k;
        }
        else if (sscanf(tok, "fx:%d", &n) == 1 && BT.on && BT.phase == BP_ANIM && n >= 0 && n < FX_N) {
            BT.an.fx = n; BT.an.col = BT_C1;
            for (int k = 0; k < E.w.neqtypes; k++) if (E.w.eqtypes[k].slot == EQ_ARME && E.w.eqtypes[k].effet == n) { BT.an.col = E.w.eqtypes[k].couleur; break; }
            if (fx_couleur(n)) { BT.an.col = fx_couleur(n); BT.an.k = 1.4f; }   /* effet puissant : joué comme une technique */
            if (BT.an.who == BT_ENEMY) BT.an.col = n == FX_BALAYAGE ? 0xFF7A3Du : 0xFF3B4Eu;
            /* élan au contact : FX_DASH > 0 (battle.c) */
            BT.an.melee = BT.an.who < BT_ENEMY && (n == FX_IMPACT || n == FX_TAILLE || n == FX_FOUET || n == FX_FRAPPE); BT.an.cast = n == FX_ARCANE || n >= FX_MAGIE;
        }
        else if (sscanf(tok, "fxt:%f", &a) == 1 && BT.on && BT.phase == BP_ANIM) {
            /* action en cours ramenée à a secondes (l'horloge du moteur suit le temps réel), sans dégâts ni sons */
            BT.an.t0 = E.clock - a; BT.an.hit = 1; BT.an.cue = 3; BT.an.scue = 3; BT.shake = 0; fx_fix = a;
        }
        else if (!strncmp(tok, "musique:", 8)) {   /* comme l'instruction : la chaîne du scénario qui porte ce nom */
            const char *nm = tok + 8;
            E.mus_force = !strcmp(nm, "auto") ? 65535 : !strcmp(nm, "aucune") ? 65534 : 65535;
            for (int s = 0; s < E.w.sc_nstr && E.mus_force == 65535 && strcmp(nm, "auto"); s++) {
                char nb[24]; script_string(s, nb, sizeof nb);
                if (!strcmp(nb, nm)) E.mus_force = (uint16_t)s;
            }
        }   /* PV de l'ennemi du combat en cours (ehp:0 : vaincu au prochain tour) */
        else if (sscanf(tok, "quete:%d:%f", &n, &a) == 2 && n >= 0 && n < SC_MAX_QUESTS) { E.quests[n] = (uint8_t)a; E.qtrack = n; }   /* état d'une quête, suivie */
        /* Police (police.c) : état ; recherche:n[:m] (niveau n imposé, motif m : 1 vol, 2 effraction, Sera signalée là où
           elle est) ; poursuite:n[:k] (n images, k = 1 gaz tenu ; état toutes les 30 images, arrêt au début d'une
           arrestation) ; arreter (la scène tout de suite) ; bmp:fichier (l'image en cours) */
        else if (!strcmp(tok, "police")) { police_etat(""); continue; }
        else if (!strcmp(tok, "villes")) {   /* villes du guidage (GUID) */
            for (int k = 0; k < E.w.nguides; k++) if (E.w.guides[k].kind == GUIDE_VILLE) printf("ville %s en %.1f %.1f (rayon %.1f)\n", E.w.guides[k].nom, E.w.guides[k].x, E.w.guides[k].z, E.w.guides[k].r);
            continue;
        }
        else if (sscanf(tok, "recherche:%d", &n) == 1) {
            int m = 1; sscanf(tok, "recherche:%*d:%d", &m);
            POL.niveau = n < 0 ? 0 : n > 3 ? 3 : n; POL.motif |= m; POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z; POL.spawn_t = 0;
            if (m & POL_EFFRACTION) POL.amende_eff += 100;
        }
        else if (sscanf(tok, "poursuite:%d", &n) == 1) {
            int m = 0; sscanf(tok, "poursuite:%*d:%d", &m);
            eng_input_t up = { 0 }; up.keys = m == 1 ? ENG_KEY_UP : 0;
            for (int f2 = 1; f2 <= n; f2++) {
                frame(&up);
                if (f2 % 30 == 0 || POL.arr) { char q[24]; snprintf(q, sizeof q, "  %4d :", f2); police_etat(q); }
                if (POL.arr) break;
            }
            continue;
        }
        else if (!strcmp(tok, "arreter")) police_arreter(0, 0, POL.clan);
        else if (!strncmp(tok, "bmp:", 4)) { if (!ecrire_bmp(tok + 4)) printf("image : %s\n", tok + 4); continue; }
        else { fprintf(stderr, "étape inconnue : %s\n", tok); return 1; }
        frame(&in);
        frame(&none);
    }
    clock_t t0 = clock();
    double us3 = 0, usu = 0;   /* moyenne des bandes sur les 10 images (une seule varie du simple au double) */
    for (int k = 0; k < 10; k++) { frame(&none); us3 += eng_stats()->us_3d; usu += eng_stats()->us_ui; }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 10;
    const eng_stats_t *st = eng_stats();
    printf("  bandes : 3D %.2f ms, interface %.2f ms (moyennes de 10 images)\n", us3 / 10000.0, usu / 10000.0);
    printf("%.2f ms par image sur PC (rendu %d x %d) ; primitives %d, sommets %d, triangles %d, pixels 3D %d, interface %d opérations, %d points\n",
           ms, ENG_W, ENG_H_PX, st->prims, st->verts, st->tris, st->px, st->ui_ops, st->ui_pts);
    if (getenv("ECHOES_PLANCHES")) printf("planches : places servies au plus fort (4, 8, 16, 32, 64, 128 Ko) %d/%d %d/%d %d/%d %d/%d %d/%d %d/%d\n",
        pl_pic[0], PL_COUNT[0], pl_pic[1], PL_COUNT[1], pl_pic[2], PL_COUNT[2], pl_pic[3], PL_COUNT[3], pl_pic[4], PL_COUNT[4], pl_pic[5], PL_COUNT[5]);
    printf("  triangles : sol %d, eau %d, plantes %d, routes %d, bâtiments %d ; dans le brouillard %d, hors du cercle %d\n",
           st->cat_tris[0], st->cat_tris[1], st->cat_tris[2], st->cat_tris[3], st->cat_tris[4], st->tris_fogged, st->tris_outside);

    /* Scénario : drapeaux levés, équipe (bit i : PARTY[i]), cœurs du sac, objectif, script en cours */
    printf("scénario : %d événements, %d chaînes, %u octets ; ", E.w.sc_nev, E.w.sc_nstr, (unsigned)E.w.sc_size);
    float ox = 0, oz = 0;
    const int ob = objective_pos(&ox, &oz);
    printf("drapeaux %#llx, équipe %#x, cœurs %d + %d, objectif %s %.1f %.1f, script %s\n", (unsigned long long)E.flags[0] | (unsigned long long)E.flags[1] << 8, E.team,
           item_loot() >= 0 ? ITEMS[item_loot()].count : 0, n_items > 5 ? ITEMS[5].count : 0, ob ? "en" : "aucun", ox, oz, script_running() ? "en cours" : "aucun");
    printf("  musique : %s\n", music_now());
    printf("  niveau %d, exp %d / %d, crédits %d ; sac :", E.level, E.exp, exp_next(E.level), E.credits);
    for (int i = 0; i < n_items; i++) if (ITEMS[i].count) printf(" %s × %d", ITEMS[i].name, ITEMS[i].count);
    printf("\n");
    for (int q = 0; q < script_quest_count(); q++)
        printf("  quête %d %s%s : état %d / %d étapes%s%s\n", q, script_quest_name(q), script_quest_type(q) ? " (secondaire)" : "", E.quests[q],
               script_quest_steps(q), q == E.qtrack ? ", suivie : " : "", q == E.qtrack ? script_quest_text(q, E.quests[q]) : "");
    if (BT.on) {
        static const char *PH[] = { "intro", "commande", "techniques", "objets", "cible", "ennemi", "action", "chute", "victoire", "défaite", "magie", "cut-in" };
        printf("combat : %s, phase %s, tour de %d, ennemi %d/%d PV ; équipe %d %d %d %d PV\n", BT.name, PH[BT.phase], BT.actor, BT.hp, BT.max,
               PARTY[0].hp, PARTY[1].hp, PARTY[2].hp, PARTY[3].hp);
        for (int m = 0; m < 4; m++) {   /* sorts de chaque membre (job, équipement) */
            int l[MAX_SORTS];
            const int ns = sorts_of(m, l);
            printf("  sorts de %s (MAG %d, ATQ %d, %d) :", PARTY[m].name, PARTY[m].mag, PARTY[m].atk, ns);
            for (int k = 0; k < ns; k++) printf(" %s (%d PM)", E.w.sorts[l[k]].nom, E.w.sorts[l[k]].pm);
            printf("\n");
        }
        printf("  échelles : ech %.2f, sprites %.2f, Sera %.2f, ennemi %.2f\n", E.w.ech, E.w.sprite_scale, member_sheet(0) >= 0 ? E.w.sheets[member_sheet(0)].taille : 0,
               BT.perso >= 0 ? E.w.persos[BT.perso].taille : 0);
    } else {
        printf("monde : Sera en %.2f %.2f\n", E.sera.x, E.sera.z);
        /* Tenues : celle de chaque membre (mise d'office ou portée), nage */
        printf("  jobs : %d, types d'équipement %d ;", E.w.njobs, E.w.neqtypes);
        for (int k = 0; k < E.w.neqtypes; k++) if (E.w.eqtypes[k].slot == EQ_ARME) printf(" %s (effet %d, %06X)", E.w.eqtypes[k].nom, E.w.eqtypes[k].effet, (unsigned)E.w.eqtypes[k].couleur);
        printf(" ;");
        for (int m = 0; m < 4; m++) {
            printf(" %s %s [", PARTY[m].name, E.job[m] < E.w.njobs ? E.w.jobs[E.job[m]].nom : "-");
            for (int k = 0; k < 3; k++) printf("%s%s", k ? ", " : "", E.eq[m][k] < n_items ? ITEMS[E.eq[m][k]].name : "-");
            printf("] PV %d ATQ %d DÉF %d VIT %d ;", PARTY[m].max, PARTY[m].atk, PARTY[m].def, PARTY[m].spd);
        }
        printf("\n");
        printf("  tenues : %d ; nage %d ;", E.w.ntenues, E.nage);
        for (int m = 0; m < 4; m++) { const int t = tenue_of(m, 0); printf(" %s %s (planche %d)", PARTY[m].name, t >= 0 ? E.w.tenues[t].nom : "la sienne", member_sheet(m)); }
        printf("\n");
        for (int k = 0; k < TRAFFIC_MAX; k++)
            if (E.traffic[k].on) {
                const traffic_t *t = &E.traffic[k];
                printf("  trafic %d : %s en %.1f %.1f, %s", k, E.w.persos[t->perso].nom, t->x, t->z, t->walking ? "roule" : "arrêté");
                if (t->lane) {   /* côté de l'axe : > 0 à droite (droite de la marche : (hz, -hx)) */
                    const route_t *r = &E.w.routes[t->road];
                    const int a = t->sgn > 0 ? t->seg : t->seg + 1, b = t->sgn > 0 ? t->seg + 1 : t->seg;
                    float ax, az, bx, bz;
                    memcpy(&ax, r->p + a * 8, 4); memcpy(&az, r->p + a * 8 + 4, 4); memcpy(&bx, r->p + b * 8, 4); memcpy(&bz, r->p + b * 8 + 4, 4);
                    const float l = hypotf(bx - ax, bz - az);
                    printf(", axe %d, côté %.2f (demi-largeur %.2f), y %.2f", t->road, ((t->x - ax) * (bz - az) - (t->z - az) * (bx - ax)) / l, r->hw, ground_y(t->x, t->z));
                }
                printf("\n");
            }
    }
    if (ecrire_bmp(argv[2])) return 1;
    if (wav_file) {   /* en-tête WAV : 24 kHz, mono, 16 bits */
        fseek(wav_file, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav_file); put32(wav_file, 36 + wav_n * 2); fwrite("WAVEfmt ", 1, 8, wav_file);
        put32(wav_file, 16); put16(wav_file, 1); put16(wav_file, 1); put32(wav_file, 24000); put32(wav_file, 48000);
        put16(wav_file, 2); put16(wav_file, 16); fwrite("data", 1, 4, wav_file); put32(wav_file, wav_n * 2);
        fclose(wav_file);
        printf("son : %.1f s -> %s\n", wav_n / 24000.0, getenv("ECHOES_WAV"));
    }
    if (E.w.ncartes > 1) printf("carte %d (%s), Sera en %.1f %.1f\n", E.w.carte, E.w.cartes[E.w.carte].nom, E.sera.x, E.sera.z);
    if (sd_file) {
        int attente = 0;   /* blocs lointains dont la texture réduite n'est pas encore fabriquée */
        for (int s = 0; s < CWIN * CWIN; s++)
            if (E.w.slot[s] && E.w.slot[s]->lodbuf >= 0)
                for (int b = 0; b < (CS / 2) * (CS / 2); b++) attente += E.w.slot[s]->lod[b] == LOD_PENDING;
        printf("carte SD : %.2f Mo lus ; dernière image : %d polygones texturés du sol, %d sans leur texture ; %d blocs lointains en attente\n",
               sd_lus / 1048576.0, eng_stats()->tex_need, eng_stats()->tex_miss, attente);
    }
    return 0;
}

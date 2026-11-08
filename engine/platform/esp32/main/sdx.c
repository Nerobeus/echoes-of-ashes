/*
 * Copie vers la carte SD par l'USB (USB Serial/JTAG de l'ESP32-S3 : le port qui sert à flasher), sans sortir la carte :
 * engine/tools/envoi_sd.py envoie monde.pak, interieurs.bin, histoire.scn et musique.bin pendant que le jeu tourne.
 * La tâche lit directement la file de réception du périphérique (la console secondaire n'écrit que dans l'autre sens) ;
 * les réponses partent par printf, en lignes qui commencent par « SDX » (le PC ignore le reste du journal).
 *
 * Protocole (lignes de texte du PC, terminées par \n) :
 *   SDX?                    -> SDX OK <dossier> <Mo libres>, une ligne SDX M <nom> <taille> <empreinte> par fichier du
 *                              manifeste, puis SDX .   (sans carte : SDX SANS-CARTE)
 *   SDX PUT <nom> <taille> <empreinte>
 *                           -> le jeu se met en pause, <dossier>/<nom>.nouveau est ouvert : SDX PRET ; suivent des
 *                              blocs binaires (longueur u32, données, 32 Ko au plus, CRC-32 u32 de zlib, petit-boutiste),
 *                              chacun répondu SDX B OK (écrit) ou SDX B ERR (à renvoyer) ; après le dernier :
 *                              SDX ECRIT <nom>
 *   SDX FIN                 -> fichiers reçus notés dans attente.txt, SDX REDEMARRAGE, redémarrage ; au démarrage,
 *                              sdx_start (avant que le moteur ouvre quoi que ce soit) remplace chaque ancien fichier par
 *                              son .nouveau et note son empreinte dans manifeste.txt
 *   SDX ANNULE              -> fichiers .nouveau effacés, le jeu reprend : SDX ANNULE
 *   SDX NOTE <nom> <taille> <empreinte>
 *                           -> entrée du manifeste sans fichier (paquet léger flashé par l'outil, « flash:… ») : SDX NOTE OK
 *   SDX HEURE <aaaa> <mm> <jj> <hh> <mn> <ss>
 *                           -> horloge PCF85063 réglée à cette heure locale (celle du PC) : SDX HEURE OK
 * Erreur : SDX ERR <raison> ; rien pendant 10 s au milieu d'un fichier : copie annulée, le jeu reprend.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_rom_crc.h"
#include "hal/usb_serial_jtag_ll.h"
#include "nvs.h"
#include "sd.h"
#include "sdx.h"
#include "horloge.h"
#include "veille.h"
#include "transfert.h"
#include "apps.h"
#include "eng.h"

static const char *TAG = "sdx";

#define BLOC_MAX (32 * 1024)      /* bloc reçu, en PSRAM */
#define ECRIT_MAX 4096            /* morceau écrit d'un coup, depuis la SRAM interne (la carte lit en DMA) */
#define FICHIERS_MAX 16
#define NOM_MAX 40
#define EMP_MAX 41
#define ATTENTE_US 10000000       /* rien pendant 10 s au milieu d'un fichier : copie annulée */
#define PILE (6 * 1024)

typedef struct { char nom[NOM_MAX]; uint32_t taille; char emp[EMP_MAX]; } entree_t;

static volatile int demande, arrete, jeu;   /* pause demandée ; le jeu s'est arrêté ; le jeu tourne */
static int carte_ok, remonte;   /* carte montée ; remontée (ou en défaut) depuis le démarrage : redémarrer après */
static entree_t man[FICHIERS_MAX] EXT_RAM_BSS_ATTR, recus[FICHIERS_MAX] EXT_RAM_BSS_ATTR;   /* en PSRAM (08/11/2026 : la SRAM interne manque ; jamais lus pendant une écriture en flash) */
static int n_man, n_recus;
static uint8_t *bloc, *dma;   /* bloc reçu (le tampon commun du moteur, jeu en pause) ; morceau écrit en DMA */
static int bloc_commun;        /* bloc = tampon commun du moteur (repris par le jeu : à redemander) */
static uint32_t *table;        /* table du CRC-32 */

/* ---------- Manifeste (manifeste.txt) et fichiers en attente (attente.txt) : « nom taille empreinte » par ligne ---------- */
static int lire_liste(const char *chemin, entree_t *t) {
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    int n = 0;
    char l[128];
    while (n < FICHIERS_MAX && fgets(l, sizeof l, f)) {
        unsigned long tl;
        if (sscanf(l, "%39s %lu %40s", t[n].nom, &tl, t[n].emp) == 3) { t[n].taille = (uint32_t)tl; n++; }
    }
    fclose(f);
    return n;
}
static void ecrire_liste(const char *chemin, const entree_t *t, int n) {
    FILE *f = fopen(chemin, "w");
    if (!f) { ESP_LOGW(TAG, "%s : écriture impossible", chemin); return; }
    for (int k = 0; k < n; k++) fprintf(f, "%s %lu %s\n", t[k].nom, (unsigned long)t[k].taille, t[k].emp);
    fflush(f); fsync(fileno(f));
    fclose(f);
}
/* Chemin d'un fichier du dossier du jeu (ext : "" ou ".nouveau") ; 0 si le nom ne tient pas */
/* « pcdos/<nom> » : dossier du PC DOS (tiny386, partition pcdos) à la racine de la carte, à côté de celui du jeu */
#define PCDOS "pcdos/"
static int pcdos_nom(const char *nom) { return !strncmp(nom, PCDOS, 6); }
static int chemin(char *p, int n, const char *nom, const char *ext) {
    return (pcdos_nom(nom) ? snprintf(p, n, "/sd/%s%s", nom, ext) : snprintf(p, n, SD_DIR "/%s%s", nom, ext)) < n;
}
static void noter(entree_t *t, int *n, const entree_t *e) {
    for (int k = 0; k < *n; k++) if (!strcmp(t[k].nom, e->nom)) { t[k] = *e; return; }
    if (*n < FICHIERS_MAX) t[(*n)++] = *e;
}

void sdx_oublier(const char *nom) {
    entree_t t[FICHIERS_MAX];
    const int n = lire_liste(SD_DIR "/manifeste.txt", t);
    int m = 0;
    for (int k = 0; k < n; k++) if (strcmp(t[k].nom, nom)) t[m++] = t[k];
    if (m != n) ecrire_liste(SD_DIR "/manifeste.txt", t, m);
}

/* Au démarrage, dans la tâche principale (pile de 3,5 Ko : tables statiques) : fichiers reçus avant le redémarrage mis
   à la place des anciens ; un fichier déjà renommé (coupure avant la fin) est reconnu à sa taille */
static entree_t att[FICHIERS_MAX] EXT_RAM_BSS_ATTR;
static void mise_en_place(void) {
    const int n = lire_liste(SD_DIR "/attente.txt", att);
    n_man = lire_liste(SD_DIR "/manifeste.txt", man);
    if (!n) return;
    for (int k = 0; k < n; k++) {
        char a[96], b[96];
        struct stat st;
        if (!chemin(a, sizeof a, att[k].nom, ".nouveau") || !chemin(b, sizeof b, att[k].nom, "")) continue;
        if (stat(a, &st) || (uint32_t)st.st_size != att[k].taille) {
            remove(a);
            if (!stat(b, &st) && (uint32_t)st.st_size == att[k].taille) noter(man, &n_man, &att[k]);
            else ESP_LOGW(TAG, "%s : fichier reçu absent ou incomplet", att[k].nom);
            continue;
        }
        remove(b);
        if (rename(a, b)) { ESP_LOGE(TAG, "%s : renommage impossible", att[k].nom); continue; }
        noter(man, &n_man, &att[k]);
        ESP_LOGI(TAG, "%s mis en place (%lu Ko)", att[k].nom, (unsigned long)(att[k].taille / 1024));
    }
    ecrire_liste(SD_DIR "/manifeste.txt", man, n_man);
    remove(SD_DIR "/attente.txt");
}

/* ---------- Lien USB ---------- */
static int lire_dispo(uint8_t *d, int n) { return usb_serial_jtag_ll_rxfifo_data_available() ? (int)usb_serial_jtag_ll_read_rxfifo(d, (uint32_t)n) : 0; }
/* Exactement n octets (la file n'en tient que 64 : lue sans attendre tant que la copie dure) ; -1 si plus rien n'arrive */
static int lire(uint8_t *d, uint32_t n) {
    uint32_t k = 0;
    int64_t t = esp_timer_get_time();
    while (k < n) {
        const int r = lire_dispo(d + k, (int)(n - k > 64 ? 64 : n - k));
        if (r > 0) { k += (uint32_t)r; t = esp_timer_get_time(); }
        else if (esp_timer_get_time() - t > ATTENTE_US) return -1;
    }
    return 0;
}
static void reponse(const char *l) { printf("%s\n", l); fflush(stdout); }
static uint32_t le32(const uint8_t *h) { return h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24; }
static uint32_t crc32(const uint8_t *d, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *d++) & 255] ^ (c >> 8);
    return ~c;
}

/* ---------- Pause du jeu ---------- */
int sdx_pause(void) {
    jeu = 1;
    if (!demande) { arrete = 0; return 0; }
    arrete = 1;
    return 1;
}
static void pause_jeu(void) {
    demande = 1;
    for (int k = 0; k < 200 && jeu && !arrete; k++) vTaskDelay(pdMS_TO_TICKS(10));   /* l'image en cours s'achève */
}

/* ---------- Banc de lecture (SDX BANC) : les lectures du jeu (fseek + fread dans la PSRAM) sur monde.pak ---------- */
/* Lecture du banc : directe dans la PSRAM (rb = 0), ou par un tampon de rb octets en SRAM interne (DMA de la carte) */
static uint8_t *rebond;
static int lire_banc(FILE *f, uint8_t *dst, int n, int rb) {
    if (!rb) return fread(dst, 1, n, f) == (size_t)n;
    for (int k = 0; k < n; ) { const int m = n - k < rb ? n - k : rb; if (fread(rebond, 1, m, f) != (size_t)m) return 0; memcpy(dst + k, rebond, m); k += m; }
    return 1;
}
static void banc(int khz, int phase, int rb) {
    pause_jeu();
    remonte = 1;   /* fichiers du jeu ouverts sur l'ancien montage : redémarrer après (SDX REDEMARRE) */
    if (!sd_remonter_a(khz, phase)) { printf("SDX BANC %d %d ERR carte non vue\n", khz, phase); return; }
    FILE *f = fopen(SD_DIR "/monde.pak", "rb");
    uint8_t *b = heap_caps_malloc(BLOC_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rebond = rb ? heap_caps_malloc(rb, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) : NULL;
    if (rb && !rebond) { printf("SDX BANC %d %d ERR pas de %d octets en SRAM interne\n", khz, phase, rb); if (f) fclose(f); free(b); return; }
    if (!f || !b) { printf("SDX BANC %d %d ERR monde.pak\n", khz, phase); if (f) fclose(f); free(b); return; }
    fseek(f, 0, SEEK_END);
    const long taille = ftell(f);
    /* 8 Mo d'affilée par blocs de 32 Ko, avec leur CRC (le même à chaque fréquence si la carte lit juste) */
    fseek(f, 0, SEEK_SET);
    uint32_t crc = 0;
    int64_t t0 = esp_timer_get_time();
    int lus = 0;
    for (int k = 0; k < 256; k++) { if (!lire_banc(f, b, BLOC_MAX, rb)) break; const int n = BLOC_MAX; crc = esp_rom_crc32_le(crc, b, n); lus += n; }
    const float seq = (esp_timer_get_time() - t0) / 1e6f;
    /* Lectures au hasard (mêmes places à chaque essai) : 1 Ko (textures du sol), 16 Ko (chunks, planches) */
    uint32_t g = 12345, crc2 = 0;
    float ms[2];
    const int tailles[2] = { 1024, 16384 }, nombre[2] = { 400, 150 };
    for (int s = 0; s < 2; s++) {
        t0 = esp_timer_get_time();
        for (int k = 0; k < nombre[s]; k++) {
            g = g * 1664525u + 1013904223u;
            fseek(f, (long)(g % (uint32_t)(taille - tailles[s])), SEEK_SET);
            if (lire_banc(f, b, tailles[s], rb)) crc2 = esp_rom_crc32_le(crc2, b, tailles[s]);
        }
        ms[s] = (esp_timer_get_time() - t0) / 1000.0f / nombre[s];
    }
    /* Pilote seul (sans FATFS) : 4 Mo de secteurs bruts, par blocs de rb octets (ou 4 Ko) en SRAM interne */
    float brut = 0;
    if (!rebond) rebond = heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (rebond) {
        const int bloc_s = (rb ? rb : 4096) / 512;
        t0 = esp_timer_get_time();
        int k = 0;
        for (; k < 8192; k += bloc_s) if (sd_secteurs(rebond, 32768 + k, bloc_s)) break;
        const float dt = (esp_timer_get_time() - t0) / 1e6f;
        brut = dt > 0 ? k * 512.0f / dt / 1048576 : 0;
    }
    fclose(f); free(b); free(rebond); rebond = NULL;
    printf("SDX BANC %d %d %d %d bus %d bits brut %.2f Mo/s fichier %.2f %08lx %.3f %.3f %08lx\n", khz, phase, rb, sd_freq_reelle(), sd_bus_bits(), brut, seq > 0 ? lus / seq / 1048576 : 0,
           (unsigned long)crc, ms[0], ms[1], (unsigned long)crc2);
}

/* ---------- Commandes ---------- */
static void bonjour(void) {
    uint64_t tot = 0, libre = 0;
    if (!carte_ok && (carte_ok = sd_remonter())) remonte = 1;   /* carte insérée après le démarrage */
    if (!carte_ok) { reponse("SDX SANS-CARTE"); return; }
    esp_vfs_fat_info("/sd", &tot, &libre);
    printf("SDX OK %s %lu\n", SD_DIR, (unsigned long)(libre / (1024 * 1024)));
    for (int k = 0; k < n_man; k++) printf("SDX M %s %lu %s\n", man[k].nom, (unsigned long)man[k].taille, man[k].emp);
    reponse("SDX .");
}

static void annuler(void) {
    for (int k = 0; k < n_recus; k++) {
        char p[96];
        if (chemin(p, sizeof p, recus[k].nom, ".nouveau")) remove(p);
    }
    n_recus = 0;
    demande = 0;
}

static void recevoir(const char *nom, uint32_t taille, const char *emp) {
    if (!carte_ok) { reponse("SDX ERR pas de carte"); return; }
    const char *base = pcdos_nom(nom) ? nom + 6 : nom;
    if (strchr(base, '/') || strchr(base, '\\') || base[0] == '.' || !base[0]) { reponse("SDX ERR nom"); return; }
    if (pcdos_nom(nom)) mkdir("/sd/pcdos", 0775);
    pause_jeu();
    /* Bloc reçu : le tampon commun du moteur, jeu arrêté (01/11/2026 : rien n'est réservé pendant le jeu, la PSRAM de la
       montre est presque pleine) ; sans jeu en cours, réservé ici une fois */
    uint8_t *commun = jeu && arrete ? eng_tampon_commun(BLOC_MAX) : NULL;
    if (commun) bloc = commun;
    else if (!bloc || bloc_commun) bloc = heap_caps_malloc(BLOC_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bloc_commun = commun != NULL;
    if (!bloc || !dma || !table) { reponse("SDX ERR mémoire"); return; }
    char p[96];
    if (!chemin(p, sizeof p, nom, ".nouveau")) { reponse("SDX ERR nom"); return; }
    FILE *f = fopen(p, "wb");
    if (!f && sd_remonter()) { remonte = 1; ESP_LOGW(TAG, "carte SD remontée"); f = fopen(p, "wb"); }   /* retirée puis remise */
    if (!f) { reponse("SDX ERR ouverture"); return; }
    setvbuf(f, NULL, _IONBF, 0);   /* écritures de 4 Ko directes, sans tampon de stdio */
    ESP_LOGI(TAG, "réception de %s (%lu Ko)", nom, (unsigned long)(taille / 1024));
    reponse("SDX PRET");
    uint32_t recu = 0;
    const char *err = NULL;
    while (recu < taille && !err) {
        uint8_t h[4];
        if (lire(h, 4)) { err = "délai"; break; }
        const uint32_t len = le32(h);
        if (!len || len > BLOC_MAX || len > taille - recu) { err = "bloc"; break; }
        if (lire(bloc, len) || lire(h, 4)) { err = "délai"; break; }
        if (crc32(bloc, len) != le32(h)) { reponse("SDX B ERR"); continue; }
        for (uint32_t o = 0; o < len && !err; o += ECRIT_MAX) {
            const uint32_t m = len - o < ECRIT_MAX ? len - o : ECRIT_MAX;
            memcpy(dma, bloc + o, m);
            if (fwrite(dma, 1, m, f) != m) err = "écriture";
        }
        if (err) break;
        recu += len;
        reponse("SDX B OK");
    }
    if (!err && (fflush(f) || fsync(fileno(f)))) err = "écriture";
    fclose(f);
    if (err) {
        remove(p);
        if (!strcmp(err, "écriture")) remonte = 1;   /* carte retirée ou en défaut : redémarrage à l'annulation */
        ESP_LOGW(TAG, "%s : copie interrompue (%s)", nom, err);
        printf("SDX ERR %s\n", err); fflush(stdout);
        if (!strcmp(err, "délai")) annuler();   /* le PC ne répond plus : le jeu reprend */
        return;
    }
    entree_t e;
    strlcpy(e.nom, nom, sizeof e.nom);
    strlcpy(e.emp, emp, sizeof e.emp);
    e.taille = taille;
    noter(recus, &n_recus, &e);
    printf("SDX ECRIT %s\n", nom); fflush(stdout);
}

static void finir(void) {
    if (!n_recus) { demande = 0; reponse("SDX RIEN"); return; }
    int n = lire_liste(SD_DIR "/attente.txt", att);
    for (int k = 0; k < n_recus; k++) noter(att, &n, &recus[k]);
    ecrire_liste(SD_DIR "/attente.txt", att, n);
    ESP_LOGI(TAG, "%d fichier(s) reçu(s) : redémarrage", n_recus);
    reponse("SDX REDEMARRAGE");
    vTaskDelay(pdMS_TO_TICKS(300));
    horloge_garder(); esp_restart();
}

/* Écritures en flash (otadata, NVS) : le cache est coupé pendant l'écriture, donc pas depuis cette tâche dont la pile
   est en PSRAM (assertion esp_task_stack_is_sane_cache_disabled) ; une petite tâche à pile interne le fait */
typedef struct { int (*f)(const char *, const char *); const char *a, *b; int r; TaskHandle_t att; } en_sram_t;
static void en_sram_tache(void *p) {
    en_sram_t *t = p;
    t->r = t->f(t->a, t->b);
    xTaskNotifyGive(t->att);
    vTaskDelete(NULL);
}
static int en_sram(int (*f)(const char *, const char *), const char *a, const char *b) {
    en_sram_t t = { f, a, b, 0, xTaskGetCurrentTaskHandle() };
    if (xTaskCreatePinnedToCore(en_sram_tache, "sdx_flash", 4096, &t, 4, NULL, 0) != pdPASS) return 0;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return t.r;
}
static int lancer_app(const char *nom, const char *rien) { (void)rien; return app_lancer(nom); }
/* Essais depuis le PC (SDX MANETTE 0|1) : le réglage RÉGLAGES › MANETTE d'Echoes (NVS echoes/manette), puis redémarrage */
static int manette_regler(const char *v, const char *rien) {
    (void)rien;
    nvs_handle_t h;
    if (nvs_open("echoes", NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_set_i8(h, "manette", (int8_t)(v[0] == '1')); nvs_commit(h); nvs_close(h);
    return 1;
}
static int app_la(const char *nom, const char *rien) { (void)rien; return app_presente(nom); }

static volatile int course_dem = -1, course_all = 1, ecran_dem;
int sdx_course(int *piste, int *allege) {
    if (course_dem < 0) return 0;
    *piste = course_dem; *allege = course_all; course_dem = -1;
    return 1;
}
int sdx_ecran(void) { const int d = ecran_dem; ecran_dem = 0; return d; }
static volatile int xp_val;
int sdx_xp(void) { return xp_val; }
static volatile int essai_dem;
static float essai_v[6];
int sdx_essai(float *v) { if (!essai_dem) return 0; memcpy(v, essai_v, sizeof essai_v); essai_dem = 0; return 1; }

static void commande(const char *l) {
    if (!strcmp(l, "SDX?")) bonjour();
    else if (!strncmp(l, "SDX ESSAI ", 10)) {   /* essai de fluidité (eng_essai_monde) : x z cap pluie heure secondes ; réponse : boucle du jeu */
        float v[6] = { 0, 0, 0, 0, -1, 30 };
        if (sscanf(l + 10, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) >= 3) { memcpy(essai_v, v, sizeof v); essai_dem = 1; }
        else reponse("SDX ERR essai");
    }
    else if (!strncmp(l, "SDX COURSE", 10)) { int k = 0, a = 1; sscanf(l + 10, "%d %d", &k, &a); course_all = a; course_dem = k < 0 ? 0 : k; }   /* réponse : boucle du jeu */
    else if (!strcmp(l, "SDX ECRAN")) ecran_dem = 1;
    else if (!strncmp(l, "SDX XP ", 7)) { xp_val = atoi(l + 7); printf("SDX XP OK %d\n", xp_val); fflush(stdout); }   /* essais de mesure (main.c, render.c : eng_xp) */
    else if (!strncmp(l, "SDX MANETTE ", 12)) {
        if (!en_sram(manette_regler, l + 12, NULL)) reponse("SDX ERR manette");
        else { reponse("SDX MANETTE OK"); vTaskDelay(pdMS_TO_TICKS(100)); horloge_garder(); esp_restart(); }
    }
    else if (!strncmp(l, "SDX PUT ", 8)) {
        char nom[NOM_MAX], emp[EMP_MAX];
        unsigned long t;
        if (sscanf(l + 8, "%39s %lu %40s", nom, &t, emp) == 3) recevoir(nom, (uint32_t)t, emp);
        else reponse("SDX ERR commande");
    }
    else if (!strcmp(l, "SDX FIN")) finir();
    else if (!strcmp(l, "SDX VEILLE")) { veille_demander(); reponse("SDX VEILLE OK"); }   /* essai de la veille depuis le PC */
    else if (!strcmp(l, "SDX REVEIL")) { veille_reveiller(); reponse("SDX REVEIL OK"); }   /* et du réveil (montre branchée : la console reste) */
    else if (!strncmp(l, "SDX BANC ", 9)) { int k, p, r = 0; if (sscanf(l + 9, "%d %d %d", &k, &p, &r) >= 2) banc(k, p, r); else reponse("SDX ERR commande"); }
    else if (!strcmp(l, "SDX REDEMARRE")) { reponse("SDX REDEMARRE OK"); vTaskDelay(pdMS_TO_TICKS(100)); horloge_garder(); esp_restart(); }
    else if (!strncmp(l, "SDX APP ", 8)) { if (en_sram(app_la, l + 8, NULL)) { reponse("SDX APP OK"); vTaskDelay(pdMS_TO_TICKS(100)); en_sram(lancer_app, l + 8, NULL); } else reponse("SDX ERR application absente"); }
    else if (!strcmp(l, "SDX TRANSFERT")) { reponse("SDX TRANSFERT OK"); vTaskDelay(pdMS_TO_TICKS(100)); transfert_lancer(); }
    else if (!strncmp(l, "SDX WIFI ", 9)) {   /* réseau du mode Transfert : nom, tabulation, mot de passe */
        char *tab = strchr(l + 9, '\t');
        if (tab) { *tab = 0; reponse(en_sram(transfert_wifi, l + 9, tab + 1) ? "SDX WIFI OK" : "SDX ERR wifi"); }
        else reponse("SDX ERR commande");
    }
    else if (!strncmp(l, "SDX NOTE ", 9)) {   /* fichier posé autrement (paquet léger flashé par l'outil) : noté au manifeste */
        entree_t e;
        unsigned long t;
        if (carte_ok && sscanf(l + 9, "%39s %lu %40s", e.nom, &t, e.emp) == 3) {
            e.taille = (uint32_t)t;
            noter(man, &n_man, &e);
            ecrire_liste(SD_DIR "/manifeste.txt", man, n_man);
            reponse("SDX NOTE OK");
        } else reponse("SDX ERR commande");
    }
    else if (!strncmp(l, "SDX HEURE ", 10)) {   /* l'outil donne l'heure du PC à chaque connexion */
        struct tm t = { 0 };
        if (sscanf(l + 10, "%d %d %d %d %d %d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) == 6) {
            t.tm_year -= 1900; t.tm_mon -= 1;
            mktime(&t);   /* jour de la semaine (TZ=UTC0 : rien d'autre ne change) */
            reponse(horloge_regler(&t) ? "SDX HEURE OK" : "SDX ERR horloge");
        } else reponse("SDX ERR commande");
    }
    else if (!strcmp(l, "SDX ANNULE")) {
        annuler();
        reponse("SDX ANNULE");
        if (remonte) { vTaskDelay(pdMS_TO_TICKS(300)); horloge_garder(); esp_restart(); }   /* fichiers du jeu ouverts sur l'ancien montage */
    }
}

static void tache(void *arg) {
    char l[160];
    int n = 0;
    for (;;) {
        uint8_t c;
        if (lire_dispo(&c, 1) != 1) { vTaskDelay(pdMS_TO_TICKS(demande ? 1 : 20)); continue; }
        if (c == '\r') continue;
        if (c != '\n') { if (n < (int)sizeof l - 1) l[n++] = (char)c; continue; }
        l[n] = 0; n = 0;
        commande(l);
    }
}

void sdx_start(int carte) {
    static StaticTask_t tcb;
    carte_ok = carte;
    if (carte) mise_en_place();
    /* Mémoire de la copie réservée ici, avant le moteur (le bloc reçu, lui, est le tampon commun du moteur) */
    dma = heap_caps_malloc(ECRIT_MAX, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if ((table = heap_caps_malloc(256 * sizeof *table, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)))
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    StackType_t *pile = heap_caps_malloc(PILE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   /* pas de SRAM interne : elle va au moteur */
    if (!pile || !xTaskCreateStaticPinnedToCore(tache, "sdx", PILE, NULL, 3, pile, &tcb, 0)) ESP_LOGE(TAG, "tâche de copie impossible à créer");
}

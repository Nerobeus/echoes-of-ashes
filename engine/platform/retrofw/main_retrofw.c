/*
 * Plateforme RetroFW 2.3 (11/11/2026, demande de l'utilisateur : la LDK Game ; aussi les autres consoles Ingenic JZ4760
 * de RetroFW, RS-97, RG-300…) : binaire MIPS32 statique (Zig, musl, flottants matériels : la JZ4760 a une FPU, et
 * RetroFW est compilé en -mips32 hard-float), sans SDL ni bibliothèque de la console : écran par /dev/fb0, boutons par
 * /dev/input/event* (sinon le clavier de la console en mode brut, comme SDL), son par OSS (/dev/dsp).
 *
 * Écran : moteur compilé en écran rectangulaire (ENG_RECT, ENG_ROUND=0, eng.h) : 3D de 320 × 240 doublée dans une image
 * de 640 × 480, l'interface de la montre (466 × 466) au milieu ; chaque carré de 2 × 2 pixels devient un pixel
 * (moyenne) : la 3D en plein écran à sa définition, l'interface réduite de moitié.
 * Monde comme sur la montre : paquet léger en mémoire (monde_leger.pak = data/monde_esp32.pak), monde.pak (chunks,
 * planches, textures du sol), histoire.scn, interieurs.bin et musique.bin lus à la demande dans le dossier du programme.
 * Sauvegardes et journal : $HOME/.choeur (édition enfant : $HOME/.choeur_enfant).
 * CHOEUR_ILE (11/11/2026, demande de l'utilisateur : l'app de l'île) : le même programme pour l'Île de Nacre du jeu des
 * héroïnes (données de data/ile : son paquet léger entier, monde_esp32.pak, pas le paquet propre de la montre, qui prend
 * ses sections communes dans la flash), dossier games/ile, sauvegardes et journal dans $HOME/.ile ; le moteur reconnaît
 * l'île à la section ILE de son paquet (core/ile.c).
 *
 * Commandes (aucun écran tactile) : le jeu se joue comme avec la manette de la montre (in.manette : stick = croix) :
 *   croix  déplacement, choix          A  OK (parler, valider)       B  RETOUR (menu)
 *   START  menu                         L / R  tourner la caméra, vite (CAM_TOUCHE_V ; carte du menu : zoom)
 *   en véhicule (SYSTÈME › CONDUITE LDK : GÂCHETTES, par défaut) : R accélère, L freine et recule, la croix braque ;
 *   CROIX : haut et bas comme le stick de la manette
 *   X / Y  vaisseau : monter / descendre ; Y tenu : au pas (et gaz à moitié en véhicule)
 *   SELECT + START tenus 1 s : retour à RetroFW (partie sauvegardée si c'est le moment) ; SELECT + R : images par seconde ;
 *   SELECT + L tenus 1,5 s : rouge et bleu échangés (si un écran les inverse)
 * Aussi la ligne VERS RETROFW du menu SYSTÈME (eng_config_t.regls_jeu). Réglages gardés dans reglages.txt ; journal.txt :
 * écran, boutons, son, erreurs. Mesures (temps de chaque étape de l'image, banc du processeur, profileur) : seulement dans le
 * banc d'essai (11/11/2026, demande de l'utilisateur : « enlève le profilage dans les apps qui ne sont pas de banc de test »).
 *
 * Compilé par tools/build_retrofw.js. RETROFW_SIMU : le même programme sur PC (Windows), écran écrit en BMP, boutons
 * d'un script, pour vérifier l'image sans la console :
 *   choeur_simu.exe dossier_des_donnees dossier_de_sortie "w60,shot:titre,skip,w90,shot:jeu,START,w20,shot:menu"
 *   (wN : N images ; shot:nom : nom.bmp ; skip : ouverture passée ; A, B, X, Y, L, R, START, SELECT, HAUT, BAS,
 *   GAUCHE, DROITE, joints par « + », suivis du nombre d'images tenus : « HAUT45 », « SELECT+START40 »)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../../core/eng.h"

#ifdef RETROFW_SIMU
#include <io.h>
#include <direct.h>
#else
#include <pthread.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/soundcard.h>
#include <sys/time.h>
#include <ucontext.h>
#endif

#define LW 320                     /* écran de la console */
#define LH 240
#if !ENG_RECT || ENG_ROUND || ENG_IMG_W != 2 * LW || ENG_IMG_H != 2 * LH
#error "RetroFW : moteur en écran rectangulaire, -DENG_RECT=1 -DENG_ROUND=0 -DENG_W=320 -DENG_H_PX=240 (tools/build_retrofw.js)"
#endif
#ifndef CHOEUR_VERSION
#define CHOEUR_VERSION "?"         /* date et heure de compilation, données par tools/build_retrofw.js */
#endif
#define IPS_MAX 30               /* cadence au plus (comme la montre) */
#define IPS_CALME 10               /* écran titre que personne ne touche (eng_ecran_calme) */
#if CHOEUR_ILE
#define SAUVE_DOSSIER ".ile"
#define TITRE_JEU "ILE DE NACRE"
#define NOM_JEU "L'Île de Nacre"
#elif CHOEUR_ANTIGRAV   /* course anti-gravité (13/11/2026 ; core/antigrav.c) : son paquet seul, data/antigrav */
#define SAUVE_DOSSIER ".antigrav"
#define TITRE_JEU "ANTIGRAV"
#define NOM_JEU "AntiGraV"
#elif ECHOES_ENFANT
#define SAUVE_DOSSIER ".choeur_enfant"
#else
#define SAUVE_DOSSIER ".choeur"
#endif
#ifndef TITRE_JEU
#define TITRE_JEU "LE CHOEUR DES CENDRES"
#define NOM_JEU "Le Chœur des Cendres"
#endif

static uint16_t img[LW * LH] __attribute__((aligned(8)));   /* image de la console, RGB565 (alignée : copie par mots de 64 bits) */
static uint16_t bande[ENG_IMG_W * ENG_NATIVE_BAND];    /* bande doublée où l'interface dessine (eng_render_band_rect), 640 pixels de large */
static char dossier[512], dossier_sauve[512];
static volatile sig_atomic_t fin;
static volatile int quitter;                           /* retour à RetroFW demandé (SELECT + START, menu SYSTÈME) */
static int voir_ips, ips_vu;
/* Banc d'essai (11/11/2026 : comparer deux versions du rendu dans les mêmes conditions) : lien de GMenuNX avec params=banc
   (choeur_banc.lnk), ou « banc » dans le script du simulateur. Partie neuve, prologue coupé, puis trois scènes de BANC_S
   secondes (Sera court à midi : eng_essai_monde, les essais de la montre), une ligne de journal par scène, le profil, et
   retour à GMenuNX ; aucune sauvegarde écrite. Simulateur : une image toutes les 60 dans banc_<scène>_<n>.bmp */
static const struct { const char *nom; float x, z, cap; } BANC[] = {
#if CHOEUR_ANTIGRAV   /* course anti-gravité (14/11/2026) : chaque piste en vue extérieure puis cockpit (x : piste, z : vue), course pilotée */
    { "neons", 0, 0, 0 }, { "neons_cockpit", 0, 1, 0 }, { "crete", 1, 0, 0 }, { "crete_cockpit", 1, 1, 0 },
    { "canyon", 2, 0, 0 }, { "canyon_cockpit", 2, 1, 0 }, { "col", 3, 0, 0 }, { "col_cockpit", 3, 1, 0 },
#elif CHOEUR_ILE
    { "village", -1, -1, 0 },          /* départ de la partie : Port-Nacre */
    { "lagon", 240, 363, 90 },         /* plage de Port-Nacre, au bord du lagon */
    { "jungle", 384, 232, 270 },       /* le cœur de la jungle de l'est (le plus dense de la carte de 512), vers le temple */
#else
    { "friches", -1, -1, 0 },          /* départ de la partie (eng_start_position) */
    { "campagne", 400, 300, 90 },
    { "lucerne", 532, 410, 90 },
#endif
};
#define BANC_N ((int)(sizeof BANC / sizeof BANC[0]))
#if CHOEUR_ANTIGRAV
#define BANC_S 12.0
#else
#define BANC_S 15.0
#endif
static int banc, banc_k = -1;
static double banc_t0 = -1;
void banc_couper_script(void);   /* banc.c : le prologue coupé */
void banc_passage(void);         /* banc.c : l'enceinte de Lucerne ouverte */
int banc_charger(int p);         /* banc.c : simulateur, une partie chargée */
void banc_rencontre(void);       /* banc.c : simulateur, une rencontre tirée */
static int fb_inverse;                                 /* rouge et bleu échangés à la main (SELECT + L, gardé dans reglages.txt) */
static int gachettes = 1;                              /* en véhicule : R accélère, L freine (SYSTÈME › CONDUITE LDK, reglages.txt) */
static void reglages_ecrire(void);
static FILE *journal_f;
#ifndef RETROFW_SIMU
static pthread_t fil_jeu;                /* le fil du jeu (gestionnaire de plantage, chien de garde) */
static volatile uint32_t images_jeu;     /* images montrées (chien de garde du fil du son) */
#define CHIEN_S 3                        /* chien de garde : secondes sans image avant de noter où en est le jeu (« FIGÉ ») */
#endif
/* Relevé des temps (journal.txt toutes les 5 s) : jeu, préparation, bandes du moteur, réduction 640 -> 320, écran */
static double perf_bandes;
static double sommeil_pas = 0.002;                     /* pas du sommeil du noyau (mesuré au démarrage : banc_processeur) */
static void ecran_bilan(void);                         /* durée des échanges de pages (journal) */

/* Boutons de la console (bits 1 << B_*) */
enum { B_HAUT, B_BAS, B_GAUCHE, B_DROITE, B_A, B_B, B_X, B_Y, B_L, B_R, B_START, B_SELECT, B_POWER, B_N };
static uint32_t tenus;
#define T(b) (tenus & (1u << (b)))

/* Écriture directe dans le journal, sans le tampon de stdio, puis forcée sur la carte (gestionnaire de plantage, chien de garde :
   14/11/2026, un plantage en course n'a laissé aucune ligne ; le journal en fprintf n'était vidé qu'à la fin du gestionnaire, et
   jamais forcé sur la carte, perdu si la console s'éteint ou se fige) */
#ifndef RETROFW_SIMU
static int journal_fd = -1;
static void jecrire(const char *fmt, ...) {
    char b[700];
    va_list a;
    va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    if (journal_fd >= 0 && n > 0) { const ssize_t w = write(journal_fd, b, (size_t)n); (void)w; fsync(journal_fd); }
}
#endif
static void journal(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc('\n', stderr);
    if (journal_f) { va_start(a, fmt); vfprintf(journal_f, fmt, a); va_end(a); fputc('\n', journal_f); fflush(journal_f); }
}

/* ---------------------------------------------------------------------------------------------------------------------
   Texte de la plateforme (chargement, erreurs, images par seconde) : police 5 × 7 de l'espace à « Z » et « _ », colonnes
   de bas en haut (bit 0 en haut) ; minuscules en capitales, lettres accentuées sans accent */
static const uint8_t POLICE[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},
    {0x40,0x40,0x40,0x40,0x40},   /* « _ » */
};
static int glyphe(const unsigned char **s) {   /* caractère suivant d'une chaîne UTF-8, rang dans POLICE (-1 : rien) */
    unsigned c = *(*s)++;
    if (c >= 0x80) {   /* lettres accentuées : la lettre de base */
        int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        unsigned u = c & (0x3Fu >> n);
        for (; n > 0 && (**s & 0xC0) == 0x80; n--) u = u << 6 | (*(*s)++ & 0x3Fu);
        static const char LAT1[] = "AAAAAAACEEEEIIIIDNOOOOO OUUUUY  AAAAAAACEEEEIIIIDNOOOOO OUUUUY Y";
        c = u >= 0xC0 && u <= 0xFF ? (unsigned char)LAT1[u - 0xC0] : (u == 0x152 || u == 0x153) ? 'O' : u == 0x2019 ? '\'' : '?';
    }
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c == '_') return 'Z' - 32 + 1;
    return c >= 32 && c <= 'Z' ? (int)(c - 32) : -1;
}
static int texte(int x, int y, const char *s, int k, uint16_t coul) {   /* renvoie l'abscisse après le texte */
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        const int g = glyphe(&p);
        if (g < 0) continue;
        for (int i = 0; i < 5; i++) for (int j = 0; j < 7; j++) {
            const int on = (POLICE[g][i] >> j) & 1;
            if (!on) continue;
            for (int a = 0; a < k; a++) for (int b = 0; b < k; b++) {
                const int px = x + i * k + a, py = y + j * k + b;
                if (px >= 0 && px < LW && py >= 0 && py < LH) img[py * LW + px] = coul;
            }
        }
        x += 6 * k;
    }
    return x;
}
static int texte_largeur(const char *s, int k) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) if (glyphe(&p) >= 0) n++;
    return n * 6 * k - k;
}
static void texte_centre(int y, const char *s, int k, uint16_t coul) { texte((LW - texte_largeur(s, k)) / 2, y, s, k, coul); }
/* Texte centré, coupé aux espaces en lignes d'au plus 50 caractères ; renvoie l'ordonnée après lui */
static int texte_lignes(int y, const char *s, uint16_t coul) {
    char l[64];
    while (*s) {
        int n = (int)strlen(s), coupe = n;
        if (n > 50) { coupe = 50; while (coupe > 10 && s[coupe] != ' ') coupe--; if (s[coupe] != ' ') coupe = 50; }
        snprintf(l, sizeof l, "%.*s", coupe, s);
        texte_centre(y, l, 1, coul);
        y += 12; s += coupe;
        while (*s == ' ') s++;
    }
    return y;
}

/* ---------------------------------------------------------------------------------------------------------------------
   Périphériques : écran, boutons, son, horloge (console, ou simulation sur PC) */
static void ecran_afficher(const uint16_t *src);
static void boutons_lire(void);

#ifdef RETROFW_SIMU
/* --- Simulation sur PC : images en BMP, boutons d'un script --- */
static char sortie[512], capture[64];
static char script[4096], *sc_p;
static int sc_attente, sc_tenir;
static uint32_t sc_bouton;
static double simu_t;
static long long pread_simu(int fd, void *p, size_t n, long long off) {
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    return _read(fd, p, (unsigned)n);
}
#define pread(fd, p, n, off) pread_simu(fd, p, n, off)
#define O_FICHIER (O_RDONLY | O_BINARY)
static double maintenant(void) { return simu_t; }
static void dormir(double s) { (void)s; }
static int ecran_ouvrir(void) { return 0; }
static void ecran_bilan(void) {}
static void profil_demarrer(void) {}
static void profil_ecrire(void) {}
static void son_ouvrir(void) {}
static void peripheriques_fermer(void) {}
static int tri_max;   /* triangles de la plus chargée des images montrées (journal des captures : budget de la course anti-gravité) */
static void ecran_afficher(const uint16_t *src) {
    simu_t += 1.0 / IPS_MAX;
    {   /* le son mixé comme par la tâche de la console (fondus de la musique, changements de piste), sans sortie */
        static int16_t m[24000 / IPS_MAX + 1];
        memset(m, 0, sizeof m);
        eng_audio_mix(m, 24000 / IPS_MAX, 24000);
    }
    const int tris = eng_stats()->tris;
    if (tris > tri_max) tri_max = tris;
    if (!capture[0]) return;
    char f[640]; snprintf(f, sizeof f, "%s/%s.bmp", sortie, capture);
    capture[0] = 0;
    FILE *o = fopen(f, "wb");
    if (!o) { journal("capture : %s impossible", f); return; }
    const uint32_t ligne = LW * 3, taille = 54 + ligne * LH;
    uint8_t h[54] = { 'B', 'M' };
    memcpy(h + 2, &taille, 4); h[10] = 54; h[14] = 40;
    const int32_t w = LW, hh = LH; memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, o);
    for (int y = LH - 1; y >= 0; y--) for (int x = 0; x < LW; x++) {
        const uint16_t c = src[y * LW + x];
        const uint8_t r = (uint8_t)(((c >> 11) & 31) * 255 / 31), g = (uint8_t)(((c >> 5) & 63) * 255 / 63), b = (uint8_t)((c & 31) * 255 / 31);
        fputc(b, o); fputc(g, o); fputc(r, o);
    }
    fclose(o);
    int d[4];
    eng_ag_detail(d);
    float cv[3]; int cs, cg;
    eng_ag_vis_diag(cv, &cs, &cg);
    journal("capture : %s (%d triangles, %d au plus depuis le départ ; engins %d, piste %d, relief %d, sol %d ; caméra %.2f %.2f %.2f, section %d, groupe %d)", f, tris, tri_max, d[0], d[1], d[2], d[3], cv[0], cv[1], cv[2], cs, cg);
}
static uint32_t bouton_nomme(const char **s) {
    static const char *NOMS[B_N] = { "HAUT", "BAS", "GAUCHE", "DROITE", "A", "B", "X", "Y", "L", "R", "START", "SELECT", "POWER" };
    int meilleur = -1, lm = 0;
    for (int b = 0; b < B_N; b++) { const int l = (int)strlen(NOMS[b]); if (!strncmp(*s, NOMS[b], l) && l > lm) { meilleur = b; lm = l; } }
    if (meilleur < 0) return 0;
    *s += lm;
    return 1u << meilleur;
}
static void boutons_lire(void) {   /* une image du script */
    if (sc_tenir > 0) { if (--sc_tenir == 0) { tenus &= ~sc_bouton; sc_attente = 2; } return; }
    if (sc_attente > 0) { sc_attente--; return; }
    for (;;) {
        char *tok = sc_p;
        if (!tok || !*tok) { fin = 1; return; }
        char *v = strchr(tok, ',');
        if (v) { *v = 0; sc_p = v + 1; } else sc_p = tok + strlen(tok);
        if (tok[0] == 'w') { sc_attente = atoi(tok + 1); return; }
        if (!strncmp(tok, "shot:", 5)) { snprintf(capture, sizeof capture, "%s", tok + 5); return; }
        if (!strcmp(tok, "skip")) { eng_skip_opening(); continue; }
        if (!strcmp(tok, "banc")) { banc = 1; continue; }
        if (!strncmp(tok, "charger:", 8)) { journal("script : partie %d chargée : %d", atoi(tok + 8) + 1, banc_charger(atoi(tok + 8))); continue; }
        if (!strcmp(tok, "rencontre")) { banc_rencontre(); continue; }
        if (!strcmp(tok, "pilote")) { eng_ag_pilote(1); continue; }
        if (!strncmp(tok, "classe:", 7)) { eng_ag_classe_essai(atoi(tok + 7)); continue; }   /* course anti-gravité : catégorie de vitesse */
        if (!strncmp(tok, "carriere:", 9)) { int l = 0, e = 0; sscanf(tok + 9, "%d:%d", &l, &e); eng_ag_carriere_essai(l, e); continue; }   /* course anti-gravité : une épreuve de la carrière */
        if (!strncmp(tok, "arme:", 5)) { eng_ag_arme_essai(atoi(tok + 5)); continue; }   /* course anti-gravité : une arme au joueur */
        if (!strcmp(tok, "bilan")) { char b[320]; eng_ag_bilan(b, sizeof b); journal("bilan : %s", b); continue; }   /* course anti-gravité : le joueur à cet instant */
        if (!strncmp(tok, "tunnel:", 7)) { int k = 0; float d = 8, h = 2, l = 0; sscanf(tok + 7, "%d:%f:%f:%f", &k, &d, &h, &l); eng_ag_vue_tunnel(k, d, h, l); continue; }   /* caméra devant un tunnel */
        if (!strncmp(tok, "camera:", 7)) { float d = 0, p = 0; sscanf(tok + 7, "%f:%f", &d, &p); eng_ag_camera_essai(d, p); continue; }   /* course anti-gravité : caméra reculée et baissée */   /* course anti-gravité : le joueur piloté comme un adversaire */
        const char *s = tok; uint32_t b = 0, u;
        while ((u = bouton_nomme(&s))) { b |= u; if (*s == '+') s++; }
        if (!b) { journal("script : « %s » inconnu", tok); continue; }
        sc_bouton = b; tenus |= b; sc_tenir = *s ? atoi(s) : 2;
        if (sc_tenir < 1) sc_tenir = 1;
        return;
    }
}
#else
/* --- La console : /dev/fb0, /dev/input ou la console en mode brut, /dev/dsp --- */
#define O_FICHIER O_RDONLY
/* Heure lue par l'appel système clock_gettime du noyau, sans le vDSO (14/11/2026 : un plantage en pleine course, instruction illégale
   dans le vDSO de RetroFW que clock_gettime de musl appelle, une fois après des minutes de jeu ; environ une microseconde de plus par
   lecture) ; structure du noyau de la console : secondes et nanosecondes sur 32 bits. Simulateur : clock_gettime */
#ifndef RETROFW_SIMU
#include <sys/syscall.h>
#endif
static double horloge_noyau(clockid_t h) {
#if !defined(RETROFW_SIMU) && defined(SYS_clock_gettime32)
    struct { long sec, nsec; } k;
    if (syscall(SYS_clock_gettime32, h, &k) == 0) return (double)k.sec + (double)k.nsec * 1e-9;
#endif
    struct timespec t;
    clock_gettime(h, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static double maintenant(void) { return horloge_noyau(CLOCK_MONOTONIC); }
static void dormir(double s) { struct timespec t = { (time_t)s, (long)((s - (time_t)s) * 1e9) }; while (nanosleep(&t, &t) && errno == EINTR) {} }

static int fb_fd = -1, fb_pages = 1, fb_page, fb_bgr, fb_change;
static struct fb_var_screeninfo fb_var, fb_var0;
static struct fb_fix_screeninfo fb_fix;
static uint8_t *fb_mem = MAP_FAILED;
/* Triple tampon (11/11/2026 : sur la LDK, FBIOPAN_DISPLAY attend le balayage de l'écran, 10 à 15 ms par image ; la SDL de
   RetroFW fait de même, rustine sdl-od-002-triplebuffer) : un fil présente les pages, le jeu dessine dans une troisième
   pendant ce temps. fb_aff : page affichée, fb_file : prête à afficher (-1 : aucune), fb_encours : en cours d'échange */
static pthread_t fb_fil;
static int fb_fil_on, fb_aff, fb_file = -1, fb_encours = -1;
static pthread_mutex_t fb_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fb_cv = PTHREAD_COND_INITIALIZER;
static double fb_pan_s, fb_attente_s;   /* durée des échanges (fil des pages), attente d'une page libre (jeu) */
static int fb_pan_n, fb_pan_tot;        /* échanges depuis le dernier bilan, depuis le début (garde-fou du jeu) */
static void *fb_tache(void *arg) {
    (void)arg;
    sigset_t sp_;   /* le signal du profileur (SIGPROF) pour le seul fil du jeu : ici, il interrompait 100 fois par seconde l'attente du
                       balayage dans FBIOPAN_DISPLAY, recommencée à chaque fois ; avant un balayage sur deux, plus aucun échange (image figée) */
    sigemptyset(&sp_); sigaddset(&sp_, SIGPROF); pthread_sigmask(SIG_BLOCK, &sp_, NULL);
    for (;;) {
        pthread_mutex_lock(&fb_mx);
        while (fb_file < 0 && !fin) pthread_cond_wait(&fb_cv, &fb_mx);
        if (fin) { pthread_mutex_unlock(&fb_mx); return NULL; }
        const int p = fb_file;
        fb_file = -1; fb_encours = p;
        pthread_mutex_unlock(&fb_mx);
        struct fb_var_screeninfo v = fb_var;
        v.yoffset = (uint32_t)p * fb_var.yres;
        const double t = maintenant();
        ioctl(fb_fd, FBIOPAN_DISPLAY, &v);
        const double d = maintenant() - t;
        pthread_mutex_lock(&fb_mx);
        fb_pan_s += d; fb_pan_n++; fb_pan_tot++;
        fb_aff = p; fb_encours = -1;
        pthread_cond_broadcast(&fb_cv);
        pthread_mutex_unlock(&fb_mx);
    }
}
static void ecran_bilan(void) {
    pthread_mutex_lock(&fb_mx);
    if (fb_pan_n || fb_fil_on) journal("écran : %d échanges de pages, %.1f ms chacun (balayage), jeu en attente d'une page %.1f ms", fb_pan_n, fb_pan_s / fb_pan_n * 1e3, fb_attente_s * 1e3);
    fb_pan_s = fb_attente_s = 0; fb_pan_n = 0;
    pthread_mutex_unlock(&fb_mx);
}
static void ecran_fil_arreter(void) {
    if (!fb_fil_on) return;
    pthread_mutex_lock(&fb_mx); fin = 1; pthread_cond_broadcast(&fb_cv); pthread_mutex_unlock(&fb_mx);
    pthread_join(fb_fil, NULL);
    fb_fil_on = 0;
}
static int ecran_ouvrir(void) {
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { journal("/dev/fb0 : %s", strerror(errno)); return -1; }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var0) < 0) { journal("FBIOGET_VSCREENINFO : %s", strerror(errno)); return -1; }
    journal("écran avant : %ux%u (virtuel %ux%u), %u bits, rouge %u/%u, vert %u/%u, bleu %u/%u", fb_var0.xres, fb_var0.yres, fb_var0.xres_virtual,
            fb_var0.yres_virtual, fb_var0.bits_per_pixel, fb_var0.red.offset, fb_var0.red.length, fb_var0.green.offset, fb_var0.green.length, fb_var0.blue.offset, fb_var0.blue.length);
    /* Mode demandé comme la SDL de RetroFW (FB_SetVideoMode de fbcon, forcé en 16 bits par sa rustine sdl-16bpp) : 320 × 240,
       16 bits, trois pages (sinon deux, sinon une), couleurs laissées au pilote (champs à 0) */
    struct fb_var_screeninfo v = fb_var0;
    v.activate = FB_ACTIVATE_NOW; v.accel_flags = 0; v.bits_per_pixel = 16;
    v.xres = v.xres_virtual = LW; v.yres = LH; v.yres_virtual = 3 * LH; v.xoffset = v.yoffset = 0;
    memset(&v.red, 0, sizeof v.red); memset(&v.green, 0, sizeof v.green); memset(&v.blue, 0, sizeof v.blue); memset(&v.transp, 0, sizeof v.transp);
    int ok = ioctl(fb_fd, FBIOPUT_VSCREENINFO, &v) == 0;
    if (!ok) { v.yres_virtual = 2 * LH; ok = ioctl(fb_fd, FBIOPUT_VSCREENINFO, &v) == 0; }
    if (!ok) { v.yres_virtual = LH; if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &v) < 0) journal("FBIOPUT_VSCREENINFO : %s (mode gardé)", strerror(errno)); }
    fb_change = 1;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var) < 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fix) < 0) { journal("écran : mode illisible"); return -1; }
    if (fb_var.bits_per_pixel != 16 && fb_var.bits_per_pixel != 32) { journal("écran : %u bits par pixel, non pris en charge", fb_var.bits_per_pixel); return -1; }
    fb_mem = mmap(NULL, fb_fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) { journal("écran : mmap %s", strerror(errno)); return -1; }
    fb_pages = fb_var.yres ? (int)(fb_var.yres_virtual / fb_var.yres) : 1;
    if (fb_fix.line_length && fb_var.yres && fb_pages > (int)(fb_fix.smem_len / (fb_var.yres * fb_fix.line_length))) fb_pages = (int)(fb_fix.smem_len / (fb_var.yres * fb_fix.line_length));
    if (fb_pages > 3) fb_pages = 3;
    if (fb_pages < 1) fb_pages = 1;
    if (fb_pages > 1) { fb_var.yoffset = 0; ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var); }   /* page 0 affichée */
    if (fb_pages == 3) fb_fil_on = pthread_create(&fb_fil, NULL, fb_tache, NULL) == 0;
    if (fb_fil_on) {
        /* priorité temps réel (11/11/2026 : à priorité normale, le jeu, qui calcule sans jamais dormir, lui prenait tout le
           processeur après une minute : plus aucun échange, image figée pendant que le jeu tournait) ; il ne fait que demander
           l'échange puis attend le balayage */
        struct sched_param sp;
        memset(&sp, 0, sizeof sp);
        sp.sched_priority = 10;
        const int r = pthread_setschedparam(fb_fil, SCHED_FIFO, &sp);
        journal("écran : fil des pages %s", r ? "à priorité normale (temps réel refusé)" : "en temps réel");
    }
    /* BGR seulement si le pilote le dit vraiment (bleu au-dessus du rouge) : des pilotes laissent les champs des couleurs à 0,
       et la SDL prend alors RGB565 (le premier essai, rouge en 0 = BGR, inversait rouge et bleu : Sera violette) */
    fb_bgr = fb_var.bits_per_pixel == 16 && fb_var.red.length && fb_var.blue.length && fb_var.blue.offset > fb_var.red.offset;
    if (fb_var.bits_per_pixel == 32 && !fb_var.red.length && !fb_var.green.length && !fb_var.blue.length) { fb_var.red.offset = 16; fb_var.green.offset = 8; fb_var.blue.offset = 0; }
    memset(fb_mem, 0, fb_fix.smem_len);
    journal("écran : %ux%u (virtuel %ux%u), %u bits, rouge %u/%u, vert %u/%u, bleu %u/%u -> %s, ligne %u octets, %d page%s%s, %u octets, visuel %u",
            fb_var.xres, fb_var.yres, fb_var.xres_virtual, fb_var.yres_virtual, fb_var.bits_per_pixel, fb_var.red.offset, fb_var.red.length,
            fb_var.green.offset, fb_var.green.length, fb_var.blue.offset, fb_var.blue.length, fb_bgr ? "BGR" : "RGB", fb_fix.line_length,
            fb_pages, fb_pages > 1 ? "s" : "", fb_fil_on ? " (triple tampon)" : "", fb_fix.smem_len, fb_fix.visual);
    return 0;
}
static void ecran_afficher(const uint16_t *src) {
    const int W = (int)fb_var.xres, H = (int)fb_var.yres, ll = (int)fb_fix.line_length;
    int p = fb_pages > 1 ? fb_page : 0;
    if (fb_fil_on) {   /* triple tampon : une page ni affichée, ni en attente, ni en cours d'échange */
        const double t0 = maintenant();
        pthread_mutex_lock(&fb_mx);
        for (;;) {
            for (p = 0; p < 3 && (p == fb_aff || p == fb_file || p == fb_encours); p++) {}
            if (p < 3) break;
            pthread_cond_wait(&fb_cv, &fb_mx);
        }
        fb_attente_s += maintenant() - t0;
        pthread_mutex_unlock(&fb_mx);
    }
    uint8_t *page = fb_mem + (size_t)p * H * ll;
    /* 320 × 240 : tel quel ; 320 × 480 (RS-97) : lignes doublées ; plus grand : au centre ; sinon : étiré */
    const int centre = W >= LW && H >= LH && !(W == LW && H == 2 * LH);
    const int dx = centre ? (W - LW) / 2 : 0, dy = centre ? (H - LH) / 2 : 0, w = centre ? LW : W, h = centre ? LH : H;
    const int rb = fb_bgr ^ fb_inverse;   /* rouge et bleu échangés */
    for (int y = 0; y < h; y++) {
        const uint16_t *s = src + (centre ? y : y * LH / H) * LW;
        uint8_t *d = page + (size_t)(y + dy) * ll;
        if (fb_var.bits_per_pixel == 16) {
            uint16_t *o = (uint16_t *)d + dx;
            if (w == LW && !rb && !(((uintptr_t)o | (uintptr_t)s) & 7)) {
                /* mémoire de l'écran non cachée : 5,5 ms par image en mots de 32 bits (memcpy) ; par 64 bits (ldc1 / sdc1 de la
                   FPU), deux fois moins d'écritures (volatile : pas de retour à memcpy par le compilateur) */
                const double *sd = (const double *)(const void *)s;
                volatile double *od = (volatile double *)(void *)o;
                for (int i = 0; i < LW / 4; i++) od[i] = sd[i];
            }
            else if (w == LW && !rb) memcpy(o, s, LW * 2);
            else if (w == LW && !(((uintptr_t)o | (uintptr_t)s) & 7)) {
                /* rouge et bleu échangés (écran BGR, ou réglage) : deux pixels par mot de 32 bits (14/11/2026 : pixel par pixel, 4,8 ms par
                   image, 10 % du temps au banc de la LDK) */
                const uint32_t *s32 = (const uint32_t *)(const void *)s;
                volatile uint32_t *o32 = (volatile uint32_t *)(void *)o;   /* (14/11/2026 : entiers seulement ; par 64 bits avec la FPU, deux plantages) */
                for (int i = 0; i < LW / 2; i++) { const uint32_t c = s32[i]; o32[i] = ((c >> 11) & 0x001F001Fu) | (c & 0x07E007E0u) | ((c << 11) & 0xF800F800u); }
            }
            else for (int x = 0; x < w; x++) {
                const uint16_t c = s[w == LW ? x : x * LW / w];
                o[x] = rb ? (uint16_t)((c >> 11) | (c & 0x07E0) | (c << 11)) : c;
            }
        } else {
            uint32_t *o = (uint32_t *)d + dx;
            for (int x = 0; x < w; x++) {
                uint16_t c = s[w == LW ? x : x * LW / w];
                if (fb_inverse) c = (uint16_t)((c >> 11) | (c & 0x07E0) | (c << 11));
                const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                const uint32_t v = ((r << 3 | r >> 2) << fb_var.red.offset) | ((g << 2 | g >> 4) << fb_var.green.offset) | ((b << 3 | b >> 2) << fb_var.blue.offset);
                o[x] = v;
            }
        }
    }
    if (fb_fil_on) {   /* à afficher (une page en attente plus ancienne est remplacée : la plus récente l'emporte) */
        pthread_mutex_lock(&fb_mx); fb_file = p; pthread_cond_broadcast(&fb_cv); const int tot = fb_pan_tot; pthread_mutex_unlock(&fb_mx);
        /* garde-fou : 15 images demandées de suite sans aucun échange, échange direct par le jeu (compté en images, pas en
           temps : une sauvegarde sur la carte arrête le jeu une demi-seconde sans qu'il demande rien, et l'ancien garde-fou
           en temps s'était déclenché à tort après un QUITTER) */
        static int vu_tot = -1, sans;
        if (tot != vu_tot) { vu_tot = tot; sans = 0; }
        else if (++sans >= 15) { journal("écran : fil des pages arrêté (%d images sans échange), échange direct", sans); fb_fil_on = 0; fb_page = (fb_aff + 1) % 3; }
    }
    else if (fb_pages > 1) {
        fb_var.yoffset = (uint32_t)(fb_page * H);
        if (ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var) < 0) fb_pages = 1;   /* pas d'échange : une seule page */
        fb_page = (fb_page + 1) % (fb_pages > 1 ? fb_pages : 1);
    }
}

/* Boutons : codes du clavier de RetroFW (platform/retrofw.h de GMenuNX : A LCTRL, B LALT, X SPACE, Y LSHIFT, L TAB,
   R BACKSPACE, START RETURN, SELECT ESCAPE, POWER END) */
static int bouton_de(int code) {
    switch (code) {
    case KEY_UP: return B_HAUT;
    case KEY_DOWN: return B_BAS;
    case KEY_LEFT: return B_GAUCHE;
    case KEY_RIGHT: return B_DROITE;
    case KEY_LEFTCTRL: return B_A;
    case KEY_LEFTALT: return B_B;
    case KEY_SPACE: return B_X;
    case KEY_LEFTSHIFT: return B_Y;
    case KEY_TAB: case KEY_PAGEUP: return B_L;
    case KEY_BACKSPACE: case KEY_PAGEDOWN: return B_R;
    case KEY_ENTER: return B_START;
    case KEY_ESC: return B_SELECT;
    case KEY_END: case KEY_HOME: case KEY_POWER: return B_POWER;
    }
    return -1;
}
static void bouton_change(int code, int appuye) {
    const int b = bouton_de(code);
    if (b < 0) return;
    if (appuye) tenus |= 1u << b; else tenus &= ~(1u << b);
}
/* Événements d'un noyau 32 bits (RetroFW : Linux 2.6.31) : temps en deux mots de 32 bits ; la struct input_event de musl
   a un time_t de 64 bits et ne correspond pas */
typedef struct { uint32_t sec, usec; uint16_t type, code; int32_t value; } ev32_t;
static int ev_fd[8], ev_n, ev_pris[8];
static int tty_fd = -1, tty_mode = -1, kb_mode = -1, tty_ok, kb_brut, kb_etat, kb_haut, kb_rel;
static struct termios tty_avant;
static int bit_de(const unsigned long *bits, int k) { return (bits[k / (8 * sizeof(long))] >> (k % (8 * sizeof(long)))) & 1; }
static void boutons_ouvrir(void) {
    for (int i = 0; i < 16 && ev_n < 8; i++) {
        char p[32]; snprintf(p, sizeof p, "/dev/input/event%d", i);
        const int fd = open(p, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1];
        memset(bits, 0, sizeof bits);
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) { close(fd); continue; }
        const int manette = bit_de(bits, KEY_LEFTCTRL) && bit_de(bits, KEY_UP), power = bit_de(bits, KEY_END) || bit_de(bits, KEY_POWER);
        if (!manette && !power) { close(fd); continue; }
        /* les boutons du jeu ne vont plus à la console (sinon ils s'empilent dans son tampon) ; le bouton seul de mise
           en veille reste partagé */
        ev_pris[ev_n] = manette && ioctl(fd, EVIOCGRAB, 1) == 0;
        journal("boutons : %s%s%s", p, manette ? " (manette)" : " (alimentation)", ev_pris[ev_n] ? ", pris" : "");
        ev_fd[ev_n++] = fd;
    }
    /* La console : plus de curseur ni de texte par-dessus l'image ; entrée en mode brut, non bloquante */
    tty_fd = open("/dev/tty0", O_RDWR | O_NONBLOCK);
    if (tty_fd < 0) tty_fd = open("/dev/tty", O_RDWR | O_NONBLOCK);
    if (tty_fd < 0) { journal("console : %s", strerror(errno)); if (!ev_n) journal("AUCUN BOUTON : ni /dev/input/event*, ni console"); return; }
    int m;
    if (ioctl(tty_fd, KDGETMODE, &m) == 0) { tty_mode = m; ioctl(tty_fd, KDSETMODE, KD_GRAPHICS); }
    if (tcgetattr(tty_fd, &tty_avant) == 0) {
        struct termios t = tty_avant;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(tcflag_t)(ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON | BRKINT);
        t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        tty_ok = tcsetattr(tty_fd, TCSAFLUSH, &t) == 0;
    }
    /* Sans /dev/input : le clavier de la console en codes bruts (appui, relâchement), comme SDL sur RetroFW */
    int manette = 0;
    for (int k = 0; k < ev_n; k++) manette |= ev_pris[k];
    if (!manette) {
        if (ioctl(tty_fd, KDGKBMODE, &m) == 0 && ioctl(tty_fd, KDSKBMODE, K_MEDIUMRAW) == 0) { kb_mode = m; kb_brut = 1; journal("boutons : clavier de la console (mode brut)"); }
        else journal("AUCUN BOUTON : clavier de la console : %s", strerror(errno));
    }
}
static void boutons_lire(void) {
    for (int k = 0; k < ev_n; k++) {
        ev32_t e[16];
        ssize_t r;
        while ((r = read(ev_fd[k], e, sizeof e)) >= (ssize_t)sizeof(ev32_t))
            for (int i = 0; i < (int)(r / (ssize_t)sizeof(ev32_t)); i++) if (e[i].type == EV_KEY) bouton_change(e[i].code, e[i].value != 0);
    }
    if (tty_fd >= 0) {
        uint8_t b[64];
        ssize_t r;
        while ((r = read(tty_fd, b, sizeof b)) > 0) {
            if (!kb_brut) continue;   /* (la console vidée : rien ne reste pour GMenuNX) */
            for (int i = 0; i < r; i++) {   /* code sur 7 bits, bit 7 : relâché ; 0 : code long sur les deux octets suivants */
                const int rel = b[i] & 0x80, c = b[i] & 0x7F;
                if (kb_etat == 1) { kb_haut = c; kb_etat = 2; continue; }
                if (kb_etat == 2) { bouton_change(kb_haut << 7 | c, !kb_rel); kb_etat = 0; continue; }
                if (!c) { kb_rel = rel; kb_etat = 1; continue; }
                bouton_change(c, !rel);
            }
        }
    }
}

/* Son : OSS, 16 bits, mono si possible, ~24 kHz ; une tâche qui tire les échantillons du moteur (eng_audio_mix) */
static int son_fd = -1, son_freq = 24000, son_canaux = 1;
static pthread_t son_fil;
static int son_lance;
static void *son_tache(void *arg) {
    (void)arg;
    sigset_t sp_;   /* le signal du profileur (SIGPROF) pour le seul fil du jeu : comme pour le fil des pages */
    sigemptyset(&sp_); sigaddset(&sp_, SIGPROF); pthread_sigmask(SIG_BLOCK, &sp_, NULL);
    int16_t m[256], st[512];
    uint32_t chien_vu = 0;
    int chien_n = 0, chien_dit = 0;
    while (!fin) {
        {   /* chien de garde : 256 échantillons par tour, son_freq / 256 tours par seconde */
            const uint32_t im = images_jeu;
            if (im != chien_vu || im == 0) { chien_vu = im; chien_n = 0; chien_dit = 0; }
            else if (++chien_n > CHIEN_S * son_freq / 256 && !chien_dit) { chien_dit = 1; pthread_kill(fil_jeu, SIGUSR2); }
        }
        eng_audio_mix(m, 256, son_freq);
        const uint8_t *p = (const uint8_t *)m;
        size_t n = sizeof m;
        if (son_canaux == 2) { for (int i = 0; i < 256; i++) st[2 * i] = st[2 * i + 1] = m[i]; p = (const uint8_t *)st; n = sizeof st; }
        while (n && !fin) {
            const ssize_t w = write(son_fd, p, n);
            if (w < 0) { if (errno == EINTR) continue; journal("son : %s", strerror(errno)); return NULL; }
            p += w; n -= (size_t)w;
        }
    }
    return NULL;
}
static void son_ouvrir(void) {
    son_fd = open("/dev/dsp", O_WRONLY);
    if (son_fd < 0) { journal("son : /dev/dsp %s", strerror(errno)); return; }
    int frag = (4 << 16) | 10;   /* 4 fragments de 1 Ko : 85 ms d'avance à 24 kHz */
    ioctl(son_fd, SNDCTL_DSP_SETFRAGMENT, &frag);
    int fmt = AFMT_S16_LE, ch = 1, f = son_freq;
    if (ioctl(son_fd, SNDCTL_DSP_SETFMT, &fmt) < 0 || fmt != AFMT_S16_LE) { journal("son : 16 bits refusés"); close(son_fd); son_fd = -1; return; }
    if (ioctl(son_fd, SNDCTL_DSP_CHANNELS, &ch) < 0 || (ch != 1 && ch != 2)) ch = 2, ioctl(son_fd, SNDCTL_DSP_CHANNELS, &ch);
    if (ioctl(son_fd, SNDCTL_DSP_SPEED, &f) < 0 || f < 8000 || f > 96000) f = 44100, ioctl(son_fd, SNDCTL_DSP_SPEED, &f);
    son_canaux = ch == 2 ? 2 : 1; son_freq = f;
    son_lance = pthread_create(&son_fil, NULL, son_tache, NULL) == 0;
    journal("son : %d Hz, %d voie%s", son_freq, son_canaux, son_canaux > 1 ? "s" : "");
}
static void peripheriques_fermer(void) {   /* aussi en cas de plantage (signal) : la console rendue à GMenuNX */
    for (int k = 0; k < ev_n; k++) if (ev_pris[k]) ioctl(ev_fd[k], EVIOCGRAB, 0);
    if (tty_fd >= 0) {
        if (kb_mode >= 0) ioctl(tty_fd, KDSKBMODE, kb_mode);
        if (tty_ok) tcsetattr(tty_fd, TCSAFLUSH, &tty_avant);
        if (tty_mode >= 0) ioctl(tty_fd, KDSETMODE, tty_mode);
    }
    if (fb_mem != MAP_FAILED) memset(fb_mem, 0, fb_fix.smem_len);
    if (fb_fd >= 0 && fb_change) { fb_var0.yoffset = 0; fb_var0.activate = FB_ACTIVATE_NOW; ioctl(fb_fd, FBIOPUT_VSCREENINFO, &fb_var0); }
}
/* Plantage (11/11/2026 : crash au lancement d'un combat, journal muet) : le signal, l'adresse fautive, le fil (jeu, son,
   pages), le compteur ordinal, l'adresse de retour, la pile, et les mots de la pile qui pointent dans le code (adresses de
   retour probables) : tools/plantage_retrofw.py les traduit en fonctions et lignes avec build/retrofw/<nom>.elf */
extern char __ehdr_start[] __attribute__((weak)), etext[] __attribute__((weak));
/* Une ligne à la fois, écrite et forcée sur la carte aussitôt (jecrire) : ce qui suit peut fauter (pile abîmée, fil à petite pile) ;
   une faute dans le gestionnaire le rappelle (SA_NODEFER) : noté, puis fin */
static volatile sig_atomic_t dans_plantage;
static void contexte_ecrire(const char *titre, int sig, const siginfo_t *si, const ucontext_t *u) {
    const uint32_t pc = (uint32_t)u->uc_mcontext.pc, ra = (uint32_t)u->uc_mcontext.gregs[31], sp = (uint32_t)u->uc_mcontext.gregs[29];
    const pthread_t moi = pthread_self();
    const char *fil = pthread_equal(moi, fil_jeu) ? "jeu" : son_lance && pthread_equal(moi, son_fil) ? "son" : fb_fil_on && pthread_equal(moi, fb_fil) ? "pages" : "?";
    jecrire("%s signal %d, adresse %08lx, fil %s : pc %08lx ra %08lx sp %08lx\n", titre, sig, si ? (unsigned long)(uintptr_t)si->si_addr : 0ul, fil,
            (unsigned long)pc, (unsigned long)ra, (unsigned long)sp);
    char b[400];
    int n = snprintf(b, sizeof b, "REGISTRES");
    for (int r = 1; r < 32 && n < (int)sizeof b - 16; r++) n += snprintf(b + n, sizeof b - (size_t)n, " %d:%08lx", r, (unsigned long)(uint32_t)u->uc_mcontext.gregs[r]);
    jecrire("%s\n", b);
    const uintptr_t t0 = (uintptr_t)__ehdr_start, t1 = (uintptr_t)etext;
    if (t0 && t1 > t0) {
        n = snprintf(b, sizeof b, "PILE");
        const uint32_t *w = (const uint32_t *)(uintptr_t)(sp & ~3u);
        for (int k = 0, m = 0; k < 2048 && m < 40 && n < (int)sizeof b - 12; k++) if (w[k] >= t0 && w[k] < t1 && (w[k] & 3) == 0) { n += snprintf(b + n, sizeof b - (size_t)n, " %08lx", (unsigned long)w[k]); m++; }
        jecrire("%s\n", b);
    }
}
static void plantage(int sig, siginfo_t *si, void *ctx) {
    if (dans_plantage) {
        jecrire("PLANTAGE DANS LE GESTIONNAIRE signal %d, adresse %08lx\n", sig, si ? (unsigned long)(uintptr_t)si->si_addr : 0ul);
        _exit(128 + sig);
    }
    dans_plantage = 1;
    contexte_ecrire("PLANTAGE", sig, si, (const ucontext_t *)ctx);
    { char d[512]; eng_diag_rendu(d, sizeof d); jecrire("RENDU %s\n", d); }
    peripheriques_fermer();
    _exit(128 + sig);
}
/* Chien de garde (14/11/2026) : le fil du son, qui tourne toujours, voit si le jeu n'a pas montré d'image depuis CHIEN_S secondes ;
   il lui envoie alors SIGUSR2, dont le gestionnaire note où il en est (« FIGÉ », sans l'arrêter), une fois par gel */
static void fige(int sig, siginfo_t *si, void *ctx) {
    (void)si;
    contexte_ecrire("FIGÉ (aucune image depuis 3 s)", sig, NULL, (const ucontext_t *)ctx);
}
static void arreter(int sig) { (void)sig; fin = 1; }

/* Profileur par échantillonnage (11/11/2026 : la 3D prend 30 ms sans qu'on sache où) : SIGPROF tous les 2 ms de temps
   processeur (en fait au tic du noyau, 10 ms), on note le compteur ordinal et l'adresse de retour ; à la sortie, profil.bin
   dans le dossier des sauvegardes (« PRF1 », nombre, puis paires pc / ra), lu sur le PC par tools/profil_retrofw.py avec
   build/retrofw/choeur.elf (le même programme avec ses symboles) */
#define PROFIL_MAX (1 << 16)
static uint32_t profil[PROFIL_MAX][2];
static volatile int profil_n;
static void profil_echantillon(int sig, siginfo_t *si, void *ctx) {
    (void)sig; (void)si;
    const ucontext_t *u = ctx;
    const int n = profil_n;
    if (n < PROFIL_MAX) { profil[n][0] = (uint32_t)u->uc_mcontext.pc; profil[n][1] = (uint32_t)u->uc_mcontext.gregs[31]; profil_n = n + 1; }
}
static void profil_demarrer(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = profil_echantillon; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = { { 0, 2000 }, { 0, 2000 } };
    setitimer(ITIMER_PROF, &it, NULL);
}
static void profil_ecrire(void) {
    struct itimerval z;
    memset(&z, 0, sizeof z);
    setitimer(ITIMER_PROF, &z, NULL);
    char f[640]; snprintf(f, sizeof f, "%s/%s", dossier_sauve, banc ? "profil_banc.bin" : "profil.bin");
    FILE *o = fopen(f, "wb");
    if (!o) return;
    const uint32_t n = (uint32_t)profil_n;
    fwrite("PRF1", 1, 4, o); fwrite(&n, 4, 1, o); fwrite(profil, 8, n, o);
    fclose(o);
    journal("profil : %u échantillons dans profil.bin", n);
}
#endif

/* ---------------------------------------------------------------------------------------------------------------------
   Fichiers du monde, sauvegardes */
static int fd_monde = -1, fd_scen = -1, fd_intr = -1, fd_mus = -1;
static int lire(int fd, uint32_t off, void *dst, uint32_t len) {
    uint8_t *p = dst;
    while (len) {
        const long long r = pread(fd, p, len, (long long)off);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return -1; }
        p += r; off += (uint32_t)r; len -= (uint32_t)r;
    }
    return 0;
}
static int pak_lire(uint32_t off, void *dst, uint32_t len) { return lire(fd_monde, off, dst, len); }
static int scen_lire(uint32_t off, void *dst, uint32_t len) { return lire(fd_scen, off, dst, len); }
static int intr_lire(uint32_t off, void *dst, uint32_t len) { return lire(fd_intr, off, dst, len); }
static int mus_lire(uint32_t off, void *dst, uint32_t len) { return lire(fd_mus, off, dst, len); }
static int ouvrir(const char *nom, uint32_t *taille) {   /* fichier du dossier du programme : descripteur, -1 s'il manque */
    char f[640]; snprintf(f, sizeof f, "%s/%s", dossier, nom);
    const int fd = open(f, O_FICHIER);
    if (fd < 0) return -1;
#ifdef RETROFW_SIMU
    const long long n = _lseeki64(fd, 0, SEEK_END);
#else
    const off_t n = lseek(fd, 0, SEEK_END);
#endif
    if (n < 0 || n > 0xFFFFFFFFll) { close(fd); return -1; }
    *taille = (uint32_t)n;
    return fd;
}
static void *reserver(size_t n, int rapide) { (void)rapide; return calloc(1, n); }   /* (une seule sorte de mémoire) */
static int sauve_ecrire(int slot, const void *d, int n) {
    if (banc) return n;   /* banc d'essai : rien d'écrit (sauvegardes automatiques de la partie neuve) */
    char f[640], t[650];
    snprintf(f, sizeof f, "%s/sauve%d.bin", dossier_sauve, slot); snprintf(t, sizeof t, "%s.tmp", f);
    FILE *o = fopen(t, "wb");
    if (!o) return -1;
    const int w = (int)fwrite(d, 1, (size_t)n, o);
    fflush(o);
#ifndef RETROFW_SIMU
    fsync(fileno(o));
#endif
    fclose(o);
    if (w != n) return -1;
#ifdef RETROFW_SIMU
    remove(f);
#endif
    return rename(t, f) ? -1 : w;   /* écrite à côté puis renommée : jamais de sauvegarde à moitié écrite */
}
static int sauve_lire(int slot, void *d, int cap) {
    char f[640]; snprintf(f, sizeof f, "%s/sauve%d.bin", dossier_sauve, slot);
    FILE *i = fopen(f, "rb");
    if (!i) return -1;
    const int r = (int)fread(d, 1, (size_t)cap, i);
    fclose(i);
    return r;
}
static uint32_t horloge_us(void) { return (uint32_t)(uint64_t)(maintenant() * 1e6); }

/* Ligne de la plateforme dans le menu SYSTÈME (et les RÉGLAGES de l'écran titre) : retour à RetroFW */
static const char *retour_valeur(void) { return "QUITTER"; }
static void retour_changer(int sens) { if (sens > 0) quitter = 1; }
/* Conduite (11/11/2026, demande de l'utilisateur) : GÂCHETTES (R accélère, L freine et recule, la croix braque ; L et R ne
   tournent plus la caméra en véhicule) ou CROIX (haut accélère, bas freine, comme la manette de la montre) */
static const char *conduite_valeur(void) { return gachettes ? "GÂCHETTES" : "CROIX"; }
static void conduite_changer(int sens) { (void)sens; gachettes = !gachettes; reglages_ecrire(); }
static const eng_reglage_t REGLS_JEU[] = { { "CONDUITE LDK", conduite_valeur, conduite_changer }, { "VERS RETROFW", retour_valeur, retour_changer } };

/* ---------------------------------------------------------------------------------------------------------------------
   Image : bandes de 17 lignes, la 3D telle quelle, l'interface en 640 × 480 réduite de moitié là où elle dessine
   (eng_render_band_rect de render.c) */
/* Diagnostic du plantage du 11/11/2026 (pointeurs des images écrasés) : après chaque étape de la boucle, la première étape
   après laquelle ils ne sont plus intacts va au journal, une fois */
static int diag_vu;
static void diag(const char *etape) {
    if (diag_vu || eng_diag_intact()) return;
    diag_vu = 1;
    char d[512];
    eng_diag_rendu(d, sizeof d);
    journal("CORROMPU après %s : %s", etape, d);
}
static void dessiner(void) {
    const double t0 = banc ? maintenant() : 0;
    for (int y = 0; y < ENG_H_PX; y += ENG_BAND) {
        const int h = ENG_H_PX - y < ENG_BAND ? ENG_H_PX - y : ENG_BAND;
        eng_render_band_rect(y, h, img + y * LW, bande);
    }
    if (banc) perf_bandes += maintenant() - t0;
}

/* Réglages de la plateforme gardés dans le dossier des sauvegardes (reglages.txt : « inverse=1 », « gachettes=0 ») */
static void reglages_lire(void) {
    char f[640]; snprintf(f, sizeof f, "%s/reglages.txt", dossier_sauve);
    FILE *i = fopen(f, "r");
    if (!i) return;
    char l[64];
    while (fgets(l, sizeof l, i)) {
        if (!strncmp(l, "inverse=", 8)) fb_inverse = atoi(l + 8) != 0;
        if (!strncmp(l, "gachettes=", 10)) gachettes = atoi(l + 10) != 0;
    }
    fclose(i);
    journal("réglages : rouge et bleu %s, conduite %s", fb_inverse ? "échangés à la main" : "tels quels", gachettes ? "aux gâchettes" : "à la croix");
}
static void reglages_ecrire(void) {
    char f[640]; snprintf(f, sizeof f, "%s/reglages.txt", dossier_sauve);
    FILE *o = fopen(f, "w");
    if (!o) return;
    fprintf(o, "inverse=%d\ngachettes=%d\n", fb_inverse, gachettes);
    fclose(o);
}

/* Banc du processeur au démarrage (journal) : une chaîne de multiplications-additions flottantes, puis entières ; avec
   la FPU, quelques millisecondes ; une FPU émulée par le noyau (piège à chaque instruction) en prendrait des centaines */
static void banc_processeur(void) {
    if (banc) {
        volatile float fv = 1.0001f;
        volatile uint32_t iv = 12345;
        float a = 0;
        uint32_t b = 0;
        const double t0 = maintenant();
        for (int i = 0; i < 200000; i++) a = a * 0.9999f + fv;
        const double t1 = maintenant();
        for (int i = 0; i < 200000; i++) b = b * 1664525u + iv;
        const double t2 = maintenant();
        journal("banc : 200 000 mult.-add. flottantes %.2f ms, entières %.2f ms (%g, %u)", (t1 - t0) * 1e3, (t2 - t1) * 1e3, (double)a, b);
    }
    double pas = 0;   /* sommeil de 1 ms demandé : ce que le noyau donne vraiment (tic de 10 ms à 100 Hz) */
    for (int k = 0; k < 5; k++) { const double s = maintenant(); dormir(0.001); const double d = maintenant() - s; if (d > pas) pas = d; }
    sommeil_pas = pas < 0.001 ? 0.001 : pas > 0.025 ? 0.025 : pas;
    journal("sommeil : 1 ms demandée, %.1f ms au plus", pas * 1e3);
#ifndef RETROFW_SIMU
    if (!banc) return;
    FILE *c = fopen("/proc/cpuinfo", "r");
    if (c) { char l[160]; while (fgets(l, sizeof l, c)) { l[strcspn(l, "\n")] = 0; if (*l) journal("cpuinfo : %s", l); } fclose(c); }
    FILE *m = fopen("/proc/meminfo", "r");
    if (m) { char l[160]; for (int k = 0; k < 2 && fgets(l, sizeof l, m); k++) { l[strcspn(l, "\n")] = 0; journal("meminfo : %s", l); } fclose(m); }
#endif
}

/* ---------------------------------------------------------------------------------------------------------------------
   Commandes : boutons de la console -> entrée du moteur */
static void entree(eng_input_t *in, float dt) {
    static uint32_t avant;
    static float quitter_t, couleurs_t;
    const uint32_t appuyes = tenus & ~avant;
    avant = tenus;
    memset(in, 0, sizeof *in);
    in->manette = 1;   /* la croix comme le stick de la manette de la montre (course, conduite, carte), sans commandes au doigt */
    if (T(B_SELECT)) {   /* SELECT tenu : combinaisons de la console, rien pour le jeu */
        if (T(B_START)) { if ((quitter_t += dt) >= 1) quitter = 1; } else quitter_t = 0;
        if (appuyes & (1u << B_R)) voir_ips = !voir_ips;
        /* rouge et bleu échangés : SELECT + L tenus 1,5 s (14/11/2026 : à l'appui, un aérofrein de la course anti-gravité pendant
           SELECT + R, pour voir les images par seconde, inversait les couleurs sans qu'on le veuille, et c'était gardé) */
        if (T(B_L)) {
            const float t0 = couleurs_t;
            couleurs_t += dt;
            if (t0 < 1.5f && couleurs_t >= 1.5f) { fb_inverse = !fb_inverse; reglages_ecrire(); journal("écran : rouge et bleu %s (SELECT + L tenus)", fb_inverse ? "échangés" : "tels quels"); }
        } else couleurs_t = 0;
        return;
    }
    quitter_t = 0; couleurs_t = 0;
    uint32_t k = 0;
    if (T(B_HAUT)) k |= ENG_KEY_UP;
    if (T(B_BAS)) k |= ENG_KEY_DOWN;
    if (T(B_GAUCHE)) k |= ENG_KEY_LEFT;
    if (T(B_DROITE)) k |= ENG_KEY_RIGHT;
    if (T(B_A)) k |= ENG_KEY_OK;
    if (T(B_B)) k |= ENG_KEY_BACK;
    if (T(B_START)) k |= ENG_KEY_MENU;
    const int gach = gachettes && eng_conduite();   /* en véhicule : L et R sont le frein et l'accélérateur */
    if (T(B_L) && !gach) k |= ENG_KEY_ROT_L;
    if (T(B_R) && !gach) k |= ENG_KEY_ROT_R;
    if (T(B_X)) k |= ENG_KEY_ALT_UP;
    if (T(B_Y)) k |= ENG_KEY_ALT_DOWN;
    in->keys = k;
    const float dx = (T(B_DROITE) ? 1.0f : 0) - (T(B_GAUCHE) ? 1.0f : 0), dy = (T(B_BAS) ? 1.0f : 0) - (T(B_HAUT) ? 1.0f : 0), l = hypotf(dx, dy);
    if (gach) {   /* gâchettes : R gaz à fond (Y : à moitié), L frein puis marche arrière, croix gauche-droite : volant */
        in->stick_x = dx;
        in->stick_y = T(B_R) ? (T(B_Y) ? -0.5f : -1.0f) : T(B_L) ? 1.0f : 0;
    }
    else if (l > 0) { const float f = (T(B_Y) ? 0.5f : 1.0f) / l; in->stick_x = dx * f; in->stick_y = dy * f; }   /* à fond : elle court ; Y : au pas */
}

/* Écran de la plateforme (chargement, erreur) : fond, titre, lignes */
static void ecran_message(const char *titre, const char *l1, const char *l2, uint16_t fond) {
    for (int i = 0; i < LW * LH; i++) img[i] = fond;
#if ECHOES_ENFANT
    texte_centre(70, TITRE_JEU, 2, 0xFFFF);
    texte_centre(90, "EDITION ENFANT", 1, 0xC618);
#else
    texte_centre(80, TITRE_JEU, 2, 0xFFFF);
#endif
    texte_centre(120, titre, 1, 0xFE60);
    int y = 140;
    if (l1) y = texte_lignes(y, l1, 0xC618);
    if (l2) texte_lignes(y, l2, 0xC618);
    ecran_afficher(img);
}
static void echec(const char *titre, const char *l1, const char *l2) {   /* écran d'erreur 15 s, ou jusqu'à un bouton */
    journal("ERREUR : %s %s %s", titre, l1 ? l1 : "", l2 ? l2 : "");
    ecran_message(titre, l1, l2, 0x5000);
    texte_centre(200, "JOURNAL : ~/" SAUVE_DOSSIER "/JOURNAL.TXT", 1, 0x8410);
    ecran_afficher(img);
#ifndef RETROFW_SIMU
    const double t0 = maintenant();
    tenus = 0;
    dormir(0.5); boutons_lire(); tenus = 0;
    while (maintenant() - t0 < 15 && !fin) { boutons_lire(); if (tenus) break; dormir(0.05); }
    ecran_fil_arreter();
    peripheriques_fermer();
#endif
    exit(1);
}

int main(int argc, char **argv) {
#ifndef RETROFW_SIMU
    /* vDSO du noyau désactivé pour musl (14/11/2026 : deux plantages de la LDK avec des adresses de la zone du vDSO dans les registres ;
       musl le cherche par AT_SYSINFO_EHDR des valeurs auxiliaires, juste après l'environnement : son adresse effacée, il lit l'heure par
       les appels système) ; avant toute lecture de l'heure */
    {
        char **ev = argv + argc + 1;
        while (*ev) ev++;
        for (size_t *ax = (size_t *)(void *)(ev + 1); ax[0]; ax += 2) if (ax[0] == 33 /* AT_SYSINFO_EHDR */) ax[1] = 0;
    }
#endif
    /* Dossier du programme (données à côté de lui), dossier des sauvegardes */
#ifdef RETROFW_SIMU
    if (argc < 4) { fprintf(stderr, "usage : choeur_simu dossier_des_donnees dossier_de_sortie script\n"); return 1; }
    snprintf(dossier, sizeof dossier, "%s", argv[1]);
    snprintf(sortie, sizeof sortie, "%s", argv[2]);
    snprintf(dossier_sauve, sizeof dossier_sauve, "%s", argv[2]);
    snprintf(script, sizeof script, "%s", argv[3]); sc_p = script;
    _mkdir(sortie);
#else
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "banc")) banc = 1;   /* lien params=banc : le banc d'essai */
    {
        char exe[512];
        const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) exe[n] = 0; else snprintf(exe, sizeof exe, "%s", argv[0]);
        char *s = strrchr(exe, '/');
        if (s) *s = 0; else snprintf(exe, sizeof exe, ".");
        snprintf(dossier, sizeof dossier, "%s", exe);
    }
    if (getenv("CHOEUR_DONNEES")) snprintf(dossier, sizeof dossier, "%s", getenv("CHOEUR_DONNEES"));
    const char *home = getenv("HOME");
    snprintf(dossier_sauve, sizeof dossier_sauve, "%s/" SAUVE_DOSSIER, home && *home ? home : "/home/retrofw");
    mkdir(dossier_sauve, 0755);
#endif
    {
        char f[640]; snprintf(f, sizeof f, "%s/%s", dossier_sauve, banc ? "banc.txt" : "journal.txt");
        journal_f = fopen(f, "w");
#ifndef RETROFW_SIMU
        if (journal_f) journal_fd = fileno(journal_f);
#endif
    }
    journal(NOM_JEU " pour RetroFW%s, version %s : données %s, sauvegardes %s", ECHOES_ENFANT ? " (édition enfant)" : "",
            CHOEUR_VERSION, dossier, dossier_sauve);   /* (la version qui tourne vraiment : une copie par l'USB peut rester dans le cache de Windows) */
#ifndef RETROFW_SIMU
    { struct utsname un; if (uname(&un) == 0) journal("noyau : %s %s %s %s", un.sysname, un.release, un.version, un.machine); }
    signal(SIGINT, arreter); signal(SIGTERM, arreter); signal(SIGHUP, arreter);
    {   /* plantage : adresse et pile dans le journal (SA_SIGINFO), sur une pile à part (une pile débordée ne l'empêche pas) */
        static uint8_t pile_secours[64 * 1024];
        stack_t ss = { .ss_sp = pile_secours, .ss_size = sizeof pile_secours, .ss_flags = 0 };
        sigaltstack(&ss, NULL);
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = plantage; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;   /* (une faute dans le gestionnaire le rappelle : notée) */
        sigemptyset(&sa.sa_mask);
        const int sigs[5] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
        for (int k = 0; k < 5; k++) sigaction(sigs[k], &sa, NULL);
        sa.sa_sigaction = fige; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
        sigaction(SIGUSR2, &sa, NULL);
        fil_jeu = pthread_self();
    }
    signal(SIGPIPE, SIG_IGN);
#endif
    reglages_lire();
    if (ecran_ouvrir()) { journal("pas d'écran : arrêt"); peripheriques_fermer(); return 1; }
#ifndef RETROFW_SIMU
    boutons_ouvrir();
#endif
    ecran_message("CHARGEMENT", NULL, NULL, 0x0000);
    banc_processeur();

    /* Paquet léger en mémoire (celui de la flash de la montre), le reste lu à la demande */
    uint32_t n_leger = 0;
    const char *nom_leger = "monde_leger.pak";
    int fd = ouvrir(nom_leger, &n_leger);
    if (fd < 0) { nom_leger = "monde_esp32.pak"; fd = ouvrir(nom_leger, &n_leger); }
    if (fd < 0) echec("FICHIER MANQUANT", "MONDE_LEGER.PAK", "A COPIER A COTE DU PROGRAMME");
    uint8_t *pak = malloc(n_leger);
    if (!pak || lire(fd, 0, pak, n_leger)) echec("LECTURE IMPOSSIBLE", nom_leger, NULL);
    close(fd);
    journal("paquet léger : %s, %u octets", nom_leger, n_leger);

    eng_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.alloc = reserver;
    cfg.save_write = sauve_ecrire; cfg.save_read = sauve_lire;
    cfg.clock_us = horloge_us;
    cfg.regls_jeu = REGLS_JEU; cfg.nregls_jeu = (int)(sizeof REGLS_JEU / sizeof REGLS_JEU[0]);
    if ((fd_monde = ouvrir("monde.pak", &cfg.pak_size)) >= 0) cfg.pak_read = pak_lire;
#if !CHOEUR_ANTIGRAV   /* (course anti-gravité : tout est dans son paquet) */
    else echec("FICHIER MANQUANT", "MONDE.PAK", "A COPIER A COTE DU PROGRAMME");
#endif
    if ((fd_scen = ouvrir("histoire.scn", &cfg.scen_size)) >= 0) cfg.scen_read = scen_lire;
    else journal("histoire : section SCEN du paquet (pas de histoire.scn)");
    if ((fd_intr = ouvrir("interieurs.bin", &cfg.intr_size)) >= 0) cfg.intr_read = intr_lire;
    else journal("intérieurs : section INTR du paquet (pas de interieurs.bin)");
    journal("monde.pak %u octets, histoire.scn %u, interieurs.bin %u", cfg.pak_size, cfg.scen_size, cfg.intr_size);

    const double ti = maintenant();
    if (eng_init(pak, n_leger, &cfg)) {
        char e[128]; snprintf(e, sizeof e, "%s", eng_error());
        echec("LE MONDE NE DEMARRE PAS", e, NULL);
    }
    journal("eng_init : %.1f s", maintenant() - ti);
    uint32_t n_mus = 0;
    if ((fd_mus = ouvrir("musique.bin", &n_mus)) >= 0) eng_music_attach(n_mus, mus_lire);
    else journal("musique : pas de musique.bin");
    son_ouvrir();
    if (banc) profil_demarrer();   /* profileur : banc d'essai seulement */

    /* Boucle : jeu, préparation de l'image, bandes, écran ; au plus IPS_MAX images par seconde */
    const double t0 = maintenant();
    double prec = t0, ips_t = t0, perf_t = t0, p_jeu = 0, p_prep = 0, p_ecran = 0, p_us3d = 0, p_usui = 0, p_pire = 0;
    int ips_n = 0, perf_n = 0, p_lentes = 0;
    long p_tris = 0, p_px = 0, p_spr = 0, p_prims = 0, p_chunks = 0;
    time_t heure_vue = 0;
    while (!fin && !quitter) {
        boutons_lire(); diag("les boutons");
        const double t = maintenant();
        float dt = (float)(t - prec);
        prec = t;
        if (dt > 0.1f) dt = 0.1f;   /* (veille de la console, chargement long) */
#ifdef RETROFW_SIMU
        dt = 1.0f / IPS_MAX;
        (void)heure_vue;
#else
        {   /* heure et date de la console, une fois par seconde (cycle « RÉEL ») ; inconnue avant 2020 */
            const time_t now = (time_t)horloge_noyau(CLOCK_REALTIME);   /* (time() passe aussi par le vDSO) */
            if (now != heure_vue) {
                struct tm m;
                heure_vue = now; localtime_r(&now, &m);
                if (m.tm_year >= 120) { eng_set_horloge(m.tm_hour * 3600 + m.tm_min * 60 + m.tm_sec); eng_set_date(m.tm_mday, m.tm_mon + 1, m.tm_wday); }
            }
        }
#endif
        eng_input_t in;
        entree(&in, dt);
        if (banc && banc_k >= 0) {
#if !CHOEUR_ANTIGRAV
            banc_couper_script();
#endif
            memset(&in, 0, sizeof in);
        }   /* banc : ni prologue ni bouton, l'essai seul */
        const double ta = maintenant();
        eng_update(dt, &in); diag("eng_update");
        const double tb = maintenant();
        eng_build_frame((float)(t - t0)); diag("eng_build_frame");
        eng_present(); diag("eng_present");
        const double tc = maintenant();
        dessiner(); diag("les bandes");
        if (banc) {   /* relevé de l'image (statistiques de l'image présentée, que les bandes viennent de dessiner) : banc seulement */
            const eng_stats_t *st = eng_stats();
            p_jeu += tb - ta; p_prep += tc - tb; p_us3d += st->us_3d; p_usui += st->us_ui;
            p_tris += st->tris; p_px += st->px; p_spr += st->sprites; p_prims += st->prims; p_chunks += st->chunk_loads;
        }
        ips_n++;
        if (t - ips_t >= 1) { ips_vu = (int)lround(ips_n / (t - ips_t)); ips_n = 0; ips_t = t; }
        if (voir_ips) {   /* images par seconde (SELECT + R), en haut à gauche */
            for (int y = 2; y < 30; y++) for (int x = 2; x < 34; x++) img[y * LW + x] = 0;
            char s[8]; snprintf(s, sizeof s, "%d", ips_vu); texte(4, 4, s, 2, 0xFFE0); texte(4, 20, "I/S", 1, 0xC618);
        }
        const double td = maintenant();
#ifdef RETROFW_SIMU
        if (banc_k >= 0 && banc_k < BANC_N && (perf_n % 60) == 59) snprintf(capture, sizeof capture, "banc_%s_%d", BANC[banc_k].nom, perf_n / 60);
#endif
        ecran_afficher(img); diag("l'écran");
#ifndef RETROFW_SIMU
        images_jeu++;   /* (chien de garde du fil du son) */
#endif
        p_ecran += maintenant() - td;
        {   /* à-coups : la pire image de la période, et celles de plus de 66 ms (moins de 15 i/s) */
            const double di = maintenant() - t;
            if (di > p_pire) p_pire = di;
            if (di > 0.066) p_lentes++;
        }
        ++perf_n;
        if (banc && banc_k >= 0 && t - banc_t0 >= BANC_S) {   /* moyennes par image, à la fin d'une scène du banc (plus de relevé toutes les 5 s en jeu) */
            const double n = perf_n;
            journal("%s%s : %.1f i/s | jeu %.1f ms, préparation %.1f, bandes %.1f (3D %.1f, interface et réduction %.1f), écran %.1f | "
                    "pire image %.0f ms, %d de plus de 66 ms | %ld triangles, %ld px 3D, %ld sprites, %ld primitives, %.1f chunks lus%s",
                    banc ? "banc " : "perf", banc ? BANC[banc_k].nom : "",
                    n / (t - perf_t), p_jeu / n * 1e3, p_prep / n * 1e3, perf_bandes / n * 1e3, p_us3d / n / 1e3, p_usui / n / 1e3,
                    p_ecran / n * 1e3, p_pire * 1e3, p_lentes, (long)(p_tris / n), (long)(p_px / n), (long)(p_spr / n), (long)(p_prims / n),
                    p_chunks / n, eng_ecran_titre() ? " (écran titre)" : "");
            ecran_bilan();
            perf_t = t; perf_n = 0; p_jeu = p_prep = p_ecran = p_us3d = p_usui = perf_bandes = p_pire = 0; p_lentes = 0;
            p_tris = p_px = p_spr = p_prims = p_chunks = 0;
            if (banc) banc_k++, banc_t0 = -1;
        }
        if (banc && (banc_k < 0 || banc_t0 < 0)) {   /* scène suivante du banc */
            if (banc_k < 0) banc_k = 0;
            if (banc_k >= BANC_N) quitter = 1;
            else {
                float x = BANC[banc_k].x, z = BANC[banc_k].z;
#if CHOEUR_ANTIGRAV
                const int ok = eng_ag_essai((int)x, (int)z);
#else
                if (eng_ecran_titre()) eng_skip_opening();
                banc_couper_script();
                banc_passage();
                if (x < 0) eng_start_position(&x, &z);
                const int ok = eng_essai_monde(x, z, BANC[banc_k].cap, 0, 0.45f, BANC_S + 2);
#endif
                journal("banc : scène %s en %.0f, %.0f%s", BANC[banc_k].nom, x, z, ok ? "" : " REFUSÉE");
                banc_t0 = maintenant(); perf_t = banc_t0; perf_n = 0;
                p_jeu = p_prep = p_ecran = p_us3d = p_usui = perf_bandes = p_pire = 0; p_lentes = 0;
                p_tris = p_px = p_spr = p_prims = p_chunks = 0;
            }
        }
        /* Cadence au plus IPS_MAX ; le sommeil du noyau a un pas (sommeil_pas, 10 ms sur un noyau à 100 Hz) : on ne dort
           que s'il reste plus d'un pas, un pas de moins (avant : chaque petit reste coûtait un tic entier) */
        const double reste = 1.0 / (eng_ecran_calme() ? IPS_CALME : IPS_MAX) - (maintenant() - t);
        if (reste > sommeil_pas) dormir(reste - sommeil_pas + 0.001);
    }

    /* Retour à RetroFW : la partie sauvegardée si c'est le moment (pas en combat, en course, en scène…) */
    if (banc) profil_ecrire();
    const int sv = banc ? 0 : eng_sauvegarder();   /* (banc : jamais de sauvegarde, la partie du joueur n'est pas touchée) */
    journal("arrêt%s, partie %s", quitter ? " demandé" : "", sv ? "sauvegardée" : "non sauvegardée (écran titre, combat, scène…)");
    fin = 1;
#ifndef RETROFW_SIMU
    if (son_lance) pthread_join(son_fil, NULL);
    ecran_fil_arreter();
#endif
    peripheriques_fermer();
    if (journal_f) fclose(journal_f);
    return 0;
}

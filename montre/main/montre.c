/*
 * App montre : l'accueil de la montre 1,75" C (partition « montre », lancée au démarrage). Pages glissées à gauche et à
 * droite : NOTIFICATIONS · CADRAN · APPS · RÉGLAGES ; écran d'appel et bandeau des notifications du téléphone
 * (ble.c, montre/PROTOCOLE.md) ; lance Echoes of Ashes, le PC DOS et le Game Boy (apps.c) ; stockage USB (usb.c).
 *
 * Énergie : fréquence dynamique 40 à 160 MHz (240 écran allumé), veille légère automatique écran
 * éteint ; le cadran est redessiné à chaque seconde, les gestes et les glissés sans limite, les autres pages seulement
 * quand quelque chose change ; thème des menus d'Echoes (theme.c) ; écran éteint (veille) par
 * PWR, BOOT ou l'inactivité ; seuls PWR et BOOT le rallument (pas le doigt), ou une notification, ou un appel.
 * Écran éteint, sans « BT EN VEILLE » (réglage, non par défaut) : Bluetooth coupé et veille profonde (réveil par PWR,
 * puis redémarrage sur le cadran) ; avec lui, veille légère et notifications du téléphone écran éteint. Après la mise en
 * veille d'Echoes ou du PC DOS (app_drapeau « veille »), l'app démarre écran éteint, sans rien afficher.
 * Pages et gestes : commun/ui_montre/pages.c (les mêmes que le menu du PC DOS).
 */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/usb_serial_jtag.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "board.h"
#include "board_power.h"
#include "lcd_co5300.h"
#include "touch.h"
#include "sd.h"
#include "horloge.h"
#include "apps.h"
#include "dessin.h"
#include "ble.h"
#include "usb.h"
#include "theme.h"
#include "pages.h"
#include <sys/time.h>

static const char *TAG = "montre";
#define CX 233
#define CY 233
#define PIN_BOOT 0

/* ---------- Réglages (montre.txt partagé avec Echoes : engine/platform/esp32/main/reglages.c) ---------- */
#define REGLAGES SD_DIR "/montre.txt"
static int volume = 60, lum = 4, veille_s = 30, bt_on = 1, teinte = 0;
static int bt_veille = 0;   /* Bluetooth gardé écran éteint (notifications) ; sinon coupé, et veille profonde */
static bool reglages_changes;
static int lum_cmd(int n) { const int v = 64 + 48 * (n - 1); return v > 255 ? 255 : v; }
static void reglages_lire(void) {
    FILE *f = fopen(REGLAGES, "r");
    if (f) {
        char l[64];
        int v;
        while (fgets(l, sizeof l, f)) {
            if (sscanf(l, "volume=%d", &v) == 1) volume = v < 0 ? 0 : v > 100 ? 100 : v;
            else if (sscanf(l, "luminosite=%d", &v) == 1) lum = v < 1 ? 1 : v > 5 ? 5 : v;
            else if (sscanf(l, "veille=%d", &v) == 1) veille_s = v < 0 ? 0 : v;
        }
        fclose(f);
    }
    nvs_handle_t h;
    if (nvs_open("montre", NVS_READONLY, &h) == ESP_OK) {
        int8_t b;
        if (nvs_get_i8(h, "bt", &b) == ESP_OK) bt_on = b;
        if (nvs_get_i8(h, "teinte", &b) == ESP_OK) teinte = b;
        if (nvs_get_i8(h, "btveille", &b) == ESP_OK) bt_veille = b;
        nvs_close(h);
    }
}
static void reglages_ecrire(void) {
    FILE *f = fopen(REGLAGES, "w");
    if (f) { fprintf(f, "volume=%d\nluminosite=%d\nveille=%d\n", volume, lum, veille_s); fclose(f); }
    nvs_handle_t h;
    if (nvs_open("montre", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i8(h, "bt", (int8_t)bt_on); nvs_set_i8(h, "teinte", (int8_t)teinte); nvs_set_i8(h, "btveille", (int8_t)bt_veille); nvs_commit(h); nvs_close(h); }
    reglages_changes = false;
}

/* ---------- Notifications et appel (écrits par la tâche du Bluetooth, lus par celle de l'écran) ---------- */
#define NOTIFS 20
typedef struct { char id[40], app[32], titre[80], texte[404]; int64_t heure; } notif_t;
static notif_t notifs[NOTIFS];
static int nnotifs;
static struct { int etat; char nom[48], numero[32]; } appel;   /* etat : 0 rien, 1 sonne, 2 en cours */
static SemaphoreHandle_t verrou;
static TaskHandle_t tache;
static volatile bool a_dessiner, reveil_demande, bt_lien;
static volatile int bandeau = -1;          /* notification en bandeau (indice), -1 : aucune */
static int64_t bandeau_fin;
#define NOTIFS_FICHIER "/sd/montre/notifs.txt"

/* Fichier : une notification par ligne, champs séparés par 0x1F ; les retours à la ligne des messages y sont écrits 0x1E
   (avant, un message de plusieurs lignes devenait au rechargement plusieurs notifications faites de ses morceaux) */
static void ecrire_champ(FILE *f, const char *s) { for (; *s; s++) fputc(*s == '\n' ? 0x1E : *s == 0x1F ? ' ' : *s, f); }
static void notifs_ecrire(void) {
    mkdir("/sd/montre", 0775);
    FILE *f = fopen(NOTIFS_FICHIER, "w");
    if (!f) return;
    for (int k = 0; k < nnotifs; k++) {
        const char *c[4] = { notifs[k].id, notifs[k].app, notifs[k].titre, notifs[k].texte };
        for (int i = 0; i < 4; i++) { ecrire_champ(f, c[i]); fputc(0x1F, f); }
        fprintf(f, "%lld\n", (long long)notifs[k].heure);
    }
    fclose(f);
}
/* Copie dans d (taille n) sans couper un caractère UTF-8 en deux (texte tronqué par le téléphone ou ici), sans \r */
static void copie_utf8(char *d, int n, const char *s) {
    int k = 0;
    while (*s && k < n - 1) {
        const unsigned char c = (unsigned char)*s;
        const int l = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (k + l > n - 1) break;
        int ok = 1;
        for (int i = 1; i < l; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) ok = 0;
        if (!ok) break;
        if (c != '\r') { memcpy(d + k, s, l); k += l; }
        s += l;
    }
    d[k] = 0;
}
static void notifs_lire(void) {
    FILE *f = fopen(NOTIFS_FICHIER, "r");
    if (!f) return;
    static char l[1024];
    while (nnotifs < NOTIFS && fgets(l, sizeof l, f)) {
        char *c[5] = { l, "", "", "", "0" };
        int k = 1;
        for (char *p = l; *p && k < 5; p++) if (*p == 0x1F) { *p = 0; c[k++] = p + 1; }
        char *nl = strchr(c[4], '\n');
        if (nl) *nl = 0;
        long long h = 0;
        if (k < 5 || !c[0][0] || sscanf(c[4], "%lld", &h) != 1) continue;   /* ligne abîmée (ancien format) : ignorée */
        for (char *p = c[3]; *p; p++) if (*p == 0x1E) *p = '\n';
        notif_t *n = &notifs[nnotifs++];
        copie_utf8(n->id, sizeof n->id, c[0]); copie_utf8(n->app, sizeof n->app, c[1]);
        copie_utf8(n->titre, sizeof n->titre, c[2]); copie_utf8(n->texte, sizeof n->texte, c[3]);
        n->heure = h;
    }
    fclose(f);
}
static void notif_retirer(int k) {
    if (k < 0 || k >= nnotifs) return;
    memmove(&notifs[k], &notifs[k + 1], (nnotifs - k - 1) * sizeof notifs[0]);
    nnotifs--;
}

static void r_heure(int64_t utc, int fuseau) {
    const time_t t = (time_t)(utc + fuseau * 60);
    struct tm l;
    gmtime_r(&t, &l);   /* heure locale gardée telle quelle (le système est en UTC0 : horloge.c) */
    horloge_regler(&l);
    ESP_LOGI(TAG, "heure du téléphone : %02d:%02d (fuseau %+d min)", l.tm_hour, l.tm_min, fuseau);
    a_dessiner = true;
    xTaskNotifyGive(tache);
}
static void r_notif(const char *id, const char *app, const char *titre, const char *texte, int64_t heure) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    for (int k = 0; k < nnotifs; k++) if (!strcmp(notifs[k].id, id)) { notif_retirer(k); break; }   /* même id : remplacée */
    if (nnotifs == NOTIFS) nnotifs--;
    memmove(&notifs[1], &notifs[0], nnotifs * sizeof notifs[0]);
    notif_t *n = &notifs[0];
    copie_utf8(n->id, sizeof n->id, id); copie_utf8(n->app, sizeof n->app, app);
    copie_utf8(n->titre, sizeof n->titre, titre); copie_utf8(n->texte, sizeof n->texte, texte);
    n->heure = heure;
    nnotifs++;
    bandeau = 0;
    notifs_ecrire();
    xSemaphoreGive(verrou);
    reveil_demande = true; a_dessiner = true;
    xTaskNotifyGive(tache);
}
static void r_effacer(const char *id) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    if (!strcmp(id, "*")) nnotifs = 0;
    else for (int k = 0; k < nnotifs; k++) if (!strcmp(notifs[k].id, id)) { notif_retirer(k); break; }
    bandeau = -1;
    notifs_ecrire();
    xSemaphoreGive(verrou);
    a_dessiner = true;
    xTaskNotifyGive(tache);
}
static void r_appel(const char *etat, const char *nom, const char *numero) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    appel.etat = !strcmp(etat, "sonne") ? 1 : !strcmp(etat, "en_cours") ? 2 : 0;
    snprintf(appel.nom, sizeof appel.nom, "%s", nom); snprintf(appel.numero, sizeof appel.numero, "%s", numero);
    xSemaphoreGive(verrou);
    if (appel.etat == 1) reveil_demande = true;
    a_dessiner = true;
    xTaskNotifyGive(tache);
}
static void r_connexion(bool c) { bt_lien = c; a_dessiner = true; xTaskNotifyGive(tache); }
static const ble_rappels_t RAPPELS = { r_heure, r_notif, r_effacer, r_appel, r_connexion };

/* ---------- Pages ---------- */
enum { P_NOTIFS, P_CADRAN, P_APPS, P_REGLAGES, P_NB };
static int detail = -1;                    /* notification ouverte (page courante : pg_page de pages.c) */
static int batterie = -1, usb_alim;
static const char *message;                 /* message passager (bas de l'écran) */
static int64_t message_fin;

/* Mise en page des listes (accueil.c d'Echoes : rangées de 46 tous les 52 depuis 156 ; réglages resserrés) */
#define RANG_Y0 156
#define RANG_H 46
#define RANG_PAS 52
#define EFFACER_Y (RANG_Y0 + 4 * RANG_PAS)   /* TOUT EFFACER, hauteur 40 */
#define DETAIL_Y 346                          /* EFFACER d'une notification ouverte, hauteur 42 ; retour en dessous */
static void rangee(int y, int h, const char *lab, const char *val, uint16_t lc) { th_rangee(y, h, lab, val, pg_sous_doigt(y, h), lc); }

static void dessiner_cadran(void) {
    cadran_info_t i = { .connue = horloge_valide(), .batterie = batterie, .usb = usb_alim, .nnotifs = nnotifs,
                        .bt_on = bt_on, .bt_lien = bt_lien };
    const time_t t = time(NULL);
    localtime_r(&t, &i.l);
    th_cadran(&i);   /* sans repère des pages : la graduation de 6 h est à sa place */
}

static void dessiner_notifs(int det) {
    char b[64];
    if (det >= 0 && det < nnotifs) {
        const notif_t *n = &notifs[det];
        th_fond();
        th_texte(&P_MONO, CX, 74, n->app, T1, A_CENTRE);
        d_paragraphe(&P_RANG, 74, 88, 318, 2, n->titre, T_BLANC);
        d_paragraphe(&P_TEXTE, 56, 148, 354, 8, n->texte, T_DIM);
        rangee(DETAIL_Y, 42, "EFFACER", NULL, T_BLANC);
        th_retour(pg_sous_doigt(398, 68));
        return;
    }
    th_fond();
    snprintf(b, sizeof b, nnotifs ? "%d du téléphone" : "Rien du téléphone", nnotifs);
    th_titre(112, "NOTIFICATIONS", b);
    for (int k = 0; k < nnotifs && k < 4; k++) {
        snprintf(b, sizeof b, "%.12s", notifs[k].app);
        rangee(RANG_Y0 + k * RANG_PAS, RANG_H, notifs[k].titre[0] ? notifs[k].titre : notifs[k].texte, b, T_BLANC);
    }
    if (nnotifs > 4) { snprintf(b, sizeof b, "+ %d AUTRES", nnotifs - 4); th_texte(&P_MONO, CX, EFFACER_Y + 58, b, T_DIM, A_CENTRE); }
    if (nnotifs) rangee(EFFACER_Y, 40, "TOUT EFFACER", NULL, T_BLANC);
    th_points(P_NOTIFS, P_NB);
}

#define APPS_N 5
static const char *APPS_NOMS[APPS_N] = { "ECHOES OF ASHES", "PC DOS", "CONSOLES", "STOCKAGE USB", "RÉGLAGES" };
static const char *APPS_INFOS[APPS_N] = { "JEU", "DOS 6", "GB · NES · DOOM", "CARTE SD", "" };
/* Rangées de la page APPS : cinq, serrées comme les listes */
#define APPS_Y0 140
#define APPS_PAS 44
#define APPS_HT 38
/* PC DOS : INVITE DOS et les jeux de /sd/pcdos/jeux.txt (nom | lanceur | CD | cadre) ; le choix est écrit dans
   /sd/pcdos/lancer.txt, que le PC DOS lit au démarrage (état « invite », puis le lanceur tapé à l'invite) */
#define DOS_JEUX 5
#define DOS_Y0 140
#define DOS_PAS 44
#define DOS_H 38
static bool dos_ouvert;
static int dos_n;
static char dos_nom[DOS_JEUX][32], dos_lanceur[DOS_JEUX][12];
static void dos_lire(void) {
    dos_n = 1;
    snprintf(dos_nom[0], sizeof dos_nom[0], "INVITE DOS"); snprintf(dos_lanceur[0], sizeof dos_lanceur[0], "INVITE");
    FILE *f = fopen("/sd/pcdos/jeux.txt", "r");
    if (!f) return;
    char l[160];
    while (dos_n < DOS_JEUX && fgets(l, sizeof l, f)) {
        if (l[0] == '#') continue;
        char *b = strchr(l, '|');
        if (!b) continue;
        *b++ = 0;
        char *c = strchr(b, '|');
        if (c) *c = 0;
        char *n = l, *x = b;
        while (*n == ' ') n++;
        while (*x == ' ') x++;
        for (char *e = n + strlen(n); e > n && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\n'); ) *--e = 0;
        for (char *e = x + strlen(x); e > x && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\n'); ) *--e = 0;
        if (!n[0] || !x[0]) continue;
        snprintf(dos_nom[dos_n], sizeof dos_nom[0], "%s", n); snprintf(dos_lanceur[dos_n], sizeof dos_lanceur[0], "%s", x);
        dos_n++;
    }
    fclose(f);
}
static void dessiner_dos(void) {
    th_fond();
    th_titre(100, "PC DOS", "Toucher pour lancer");
    for (int k = 0; k < dos_n; k++) rangee(DOS_Y0 + k * DOS_PAS, DOS_H, dos_nom[k], k ? NULL : "C:\\>", T_BLANC);
    th_retour(pg_sous_doigt(398, 68));
}

/* CONSOLES : la console d'abord (Game Boy, NES, Master System, Game Gear, DOOM : les dossiers de l'app consoles, gameboy/ du
   dépôt), puis ses ROM, le dernier jeu joué (/sd/gameboy/dernier.txt : chemin complet, ou nom seul d'un jeu Game Boy)
   en tête, « REPRISE » quand un état attend (<dossier>/etats/<jeu>.reprise.etat, écrit par la veille ou le retour à la
   montre) ; le chemin du jeu choisi est écrit dans /sd/gameboy/lancer.txt, que l'app consoles lit au démarrage. Cinq
   rangées, glissé vertical pour la suite */
#define GB_DOSSIER "/sd/gameboy"
static void lancer(const char *app);   /* plus bas : écran LANCEMENT, puis l'application */
#define GB_VUES 5
static const struct { const char *nom, *info, *dossier, *ext[3]; } CONSOLES[] = {
    { "GAME BOY", "GB · GBC", "/sd/gameboy", { "gb", "gbc", NULL } },
    { "NES", "FAMICOM", "/sd/nes", { "nes", NULL } },
    { "MASTER SYSTEM", "SMS · SG", "/sd/sms", { "sms", "sg", NULL } },
    { "GAME GEAR", "GG", "/sd/gamegear", { "gg", NULL } },
    { "DOOM", "WAD", "/sd/doom", { "wad", NULL } },
};
#define CONSOLES_N (int)(sizeof CONSOLES / sizeof CONSOLES[0])
static bool gb_ouvert;
static int gb_console = -1;   /* -1 : le choix de la console */
static int gb_n, gb_off;
/* Liste sans limite : les noms bout à bout dans un bloc de PSRAM, leurs positions à part ; « REPRISE » cherché pour les
   rangées montrées seulement (une page à la fois) */
static char *gb_noms;
static uint32_t *gb_o;
static int gb_noms_n, gb_noms_cap, gb_cap;
static bool gb_vue_reprise[GB_VUES];
static int gb_vue_off = -1;
static const char *gb_jeu(int k) { return gb_noms + gb_o[k]; }
static bool gb_agrandir(void **bloc, int *cap, int besoin, size_t taille) {
    if (besoin <= *cap) return true;
    const int n = besoin < 2 * *cap ? 2 * *cap : besoin + 256;
    void *q = heap_caps_realloc(*bloc, n * taille, MALLOC_CAP_SPIRAM);
    if (!q) return false;
    *bloc = q; *cap = n;
    return true;
}
static int gb_cmp(const void *a, const void *b) { return strcasecmp(gb_noms + *(const uint32_t *)a, gb_noms + *(const uint32_t *)b); }
static void gb_nom(const char *f, char *b, int n) {   /* sans l'extension */
    strlcpy(b, f, n);
    char *p = strrchr(b, '.');
    if (p && p != b) *p = 0;
}
static bool gb_rom(const char *f) {
    const char *x = strrchr(f, '.');
    if (!x) return false;
    for (int k = 0; CONSOLES[gb_console].ext[k]; k++) if (!strcasecmp(x + 1, CONSOLES[gb_console].ext[k])) return true;
    return false;
}
static void gb_lire(void) {
    gb_n = 0; gb_off = 0; gb_noms_n = 0; gb_vue_off = -1;
    DIR *d = opendir(CONSOLES[gb_console].dossier);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!gb_rom(e->d_name)) continue;
        const int l = strlen(e->d_name) + 1;
        if (!gb_agrandir((void **)&gb_noms, &gb_noms_cap, gb_noms_n + l, 1) || !gb_agrandir((void **)&gb_o, &gb_cap, gb_n + 1, sizeof *gb_o)) break;
        memcpy(gb_noms + gb_noms_n, e->d_name, l);
        gb_o[gb_n++] = gb_noms_n;
        gb_noms_n += l;
    }
    closedir(d);
    qsort(gb_o, gb_n, sizeof *gb_o, gb_cmp);
    char der[320] = "", c[320];
    FILE *f = fopen(GB_DOSSIER "/dernier.txt", "r");
    if (f) { if (!fgets(der, sizeof der, f)) der[0] = 0; fclose(f); der[strcspn(der, "\r\n")] = 0; }
    for (int k = 1; k < gb_n; k++) {   /* le dernier joué en tête */
        snprintf(c, sizeof c, "%s/%s", CONSOLES[gb_console].dossier, gb_jeu(k));
        if (strcmp(c, der) && (gb_console || strcmp(gb_jeu(k), der))) continue;
        const uint32_t o = gb_o[k];
        memmove(gb_o + 1, gb_o, k * sizeof *gb_o);
        gb_o[0] = o;
        break;
    }
}
static void gb_lancer(int k) {   /* chemin complet du jeu dans lancer.txt de l'app consoles */
    mkdir(GB_DOSSIER, 0775);
    FILE *f = fopen(GB_DOSSIER "/lancer.txt", "w");
    if (f) { fprintf(f, "%s/%s\n", CONSOLES[gb_console].dossier, gb_jeu(k)); fclose(f); }
    lancer("gameboy");
}
static void gb_reprises(void) {   /* états « reprise » des rangées montrées */
    if (gb_vue_off == gb_off) return;
    gb_vue_off = gb_off;
    for (int s = 0; s < GB_VUES; s++) {
        char n[256], p[320];
        struct stat st;
        gb_vue_reprise[s] = false;
        if (gb_off + s >= gb_n) continue;
        gb_nom(gb_jeu(gb_off + s), n, sizeof n);
        snprintf(p, sizeof p, "%s/etats/%s.reprise.etat", CONSOLES[gb_console].dossier, n);
        gb_vue_reprise[s] = !stat(p, &st);
    }
}
static void dessiner_gb(void) {
    char b[200], n[256];
    th_fond();
    if (gb_console < 0) {
        th_titre(100, "CONSOLES", "Toucher pour choisir");
        for (int k = 0; k < CONSOLES_N; k++) rangee(APPS_Y0 + k * APPS_PAS, APPS_HT, CONSOLES[k].nom, CONSOLES[k].info, T_BLANC);
        th_retour(pg_sous_doigt(398, 68));
        return;
    }
    const char *titre = CONSOLES[gb_console].nom;
    if (!gb_n) {
        char e[24] = "";
        for (int k = 0; CONSOLES[gb_console].ext[k]; k++) { strlcat(e, k ? " ou ." : ".", sizeof e); strlcat(e, CONSOLES[gb_console].ext[k], sizeof e); }
        th_titre(100, titre, "Aucun jeu sur la carte");
        snprintf(b, sizeof b, "Copier des ROM %s dans le dossier %s de la carte SD (STOCKAGE USB).", e, CONSOLES[gb_console].dossier + 4);
        d_paragraphe(&P_TEXTE, 70, 170, 326, 6, b, T_DIM);
        th_retour(pg_sous_doigt(398, 68));
        return;
    }
    if (gb_n > GB_VUES) snprintf(b, sizeof b, "%d à %d sur %d · glisser haut, bas", gb_off + 1, gb_off + GB_VUES < gb_n ? gb_off + GB_VUES : gb_n, gb_n);
    else snprintf(b, sizeof b, "Toucher pour jouer");
    th_titre(100, titre, b);
    gb_reprises();
    for (int s = 0; s < GB_VUES && gb_off + s < gb_n; s++) {
        gb_nom(gb_jeu(gb_off + s), n, sizeof n);
        rangee(DOS_Y0 + s * DOS_PAS, DOS_H, n, gb_vue_reprise[s] ? "REPRISE" : NULL, T_BLANC);
    }
    th_retour(pg_sous_doigt(398, 68));
}

static void dessiner_apps(void) {
    if (dos_ouvert) { dessiner_dos(); return; }
    if (gb_ouvert) { dessiner_gb(); return; }
    th_fond();
    th_titre(100, "APPLICATIONS", "Toucher pour lancer");
    for (int k = 0; k < APPS_N; k++) rangee(APPS_Y0 + k * APPS_PAS, APPS_HT, APPS_NOMS[k], APPS_INFOS[k], T_BLANC);
    th_points(P_APPS, P_NB);
}

#define REG_N 7
static const char *REG_NOMS[REG_N] = { "TEINTE", "VOLUME", "LUMINOSITÉ", "VEILLE", "BLUETOOTH", "BT EN VEILLE", "OUBLIER LE TÉLÉPHONE" };
#define REG_Y0 92
#define REG_PAS 42
#define REG_H 36
static void reg_valeur(int k, char *b, int n) {
    switch (k) {
    case 0: snprintf(b, n, "%s", teinte_nom(teinte)); break;
    case 1: snprintf(b, n, "%d %%", volume); break;
    case 2: snprintf(b, n, "%d / 5", lum); break;
    case 3: if (!veille_s) snprintf(b, n, "JAMAIS"); else if (veille_s < 60) snprintf(b, n, "%d s", veille_s); else snprintf(b, n, "%d min", veille_s / 60); break;
    case 4: snprintf(b, n, "%s", bt_on ? "OUI" : "NON"); break;
    case 5: snprintf(b, n, "%s", bt_veille ? "OUI" : "NON"); break;
    default: b[0] = 0;
    }
}
static void dessiner_reglages(void) {
    th_fond();
    th_titre(86, "RÉGLAGES", NULL);
    char b[24];
    for (int k = 0; k < REG_N; k++) { reg_valeur(k, b, sizeof b); rangee(REG_Y0 + k * REG_PAS, REG_H, REG_NOMS[k], b, T_BLANC); }
    th_texte(&P_MONO, CX, 402, "« MOINS   ·   PLUS »", T_DIM, A_CENTRE);
    th_points(P_REGLAGES, P_NB);
}

#define APPEL_VERT RGB(0x33, 0xFF, 0x88)
#define APPEL_ROUGE RGB(0xFF, 0x33, 0x44)
static void dessiner_appel(void) {
    th_fond();
    d_anneau(CX, CY, 222, 226, appel.etat == 1 ? APPEL_VERT : T1);
    th_texte(&P_MONO, CX, 104, appel.etat == 1 ? "APPEL ENTRANT" : "APPEL EN COURS", T1, A_CENTRE);
    d_paragraphe(&P_GRAND, 70, 126, 326, 2, appel.nom[0] ? appel.nom : appel.numero, T_BLANC);
    if (appel.nom[0]) th_texte(&P_MONO, CX, 214, appel.numero, T_DIM, A_CENTRE);
    if (appel.etat == 1) {
        th_panneau(70, 280, 150, 70, APPEL_VERT, pg_sous_doigt(270, 90) && pg_appui_x() < CX);
        th_texte(&P_RANG, 145, 323, "RÉPONDRE", APPEL_VERT, A_CENTRE);
        th_panneau(246, 280, 150, 70, APPEL_ROUGE, pg_sous_doigt(270, 90) && pg_appui_x() >= CX);
        th_texte(&P_RANG, 321, 323, "REFUSER", APPEL_ROUGE, A_CENTRE);
    } else {
        th_panneau(158, 290, 150, 60, APPEL_ROUGE, pg_sous_doigt(280, 80));
        th_texte(&P_RANG, CX, 328, "RACCROCHER", APPEL_ROUGE, A_CENTRE);
    }
}

static void dessiner_page(int p) {
    switch (p) {
    case P_NOTIFS: dessiner_notifs(p == pg_page ? detail : -1); break;
    case P_CADRAN: dessiner_cadran(); break;
    case P_APPS: dessiner_apps(); break;
    case P_REGLAGES: dessiner_reglages(); break;
    }
}
static esp_pm_lock_handle_t pm_max, pm_eveil;   /* processeur à 240 MHz ; pas de veille légère (écran allumé) */
/* Relevé (console, toutes les 2 s) : images, temps de dessin et d'envoi, lectures du tactile, plus long tour de boucle */
static struct { int images, lectures; int64_t dessin, dessin_max, envoi, lecture, tour_max; } mes;
static void dessiner(void) {
    const int64_t t0 = esp_timer_get_time();
    if (pg_page != P_CADRAN || pg_glisse() || appel.etat) d_effacer(T_INK);   /* le cadran couvre tout l'écran */
    xSemaphoreTake(verrou, portMAX_DELAY);
    if (appel.etat) dessiner_appel();
    else {
        pg_pages();   /* la page, et pendant un glissé sa voisine qui entre */
        if (!pg_glisse() && bandeau >= 0 && bandeau < nnotifs) {   /* nouvelle notification : bandeau au milieu */
            th_panneau(40, 140, 386, 168, T1, false);
            th_texte(&P_MONO, CX, 170, notifs[bandeau].app, T1, A_CENTRE);
            d_paragraphe(&P_RANG, 64, 182, 338, 1, notifs[bandeau].titre, T_BLANC);
            d_paragraphe(&P_TEXTE, 64, 216, 338, 3, notifs[bandeau].texte, T_DIM);
        }
    }
    if (message) th_texte(&P_MONO, CX, 422, message, T2, A_CENTRE);
    xSemaphoreGive(verrou);
    const int64_t t1 = esp_timer_get_time();
    d_envoyer();
    const int64_t t2 = esp_timer_get_time();
    mes.images++; mes.dessin += t1 - t0; mes.envoi += t2 - t1;
    if (t2 - t0 > mes.dessin_max) mes.dessin_max = t2 - t0;
}

/* ---------- Écran, veille, lancement ---------- */
static bool ecran = true;
static bool lcd_pret;        /* écran initialisé (pas avant le premier allumage après une mise en veille) */
static int64_t derniere;     /* dernière action (doigt, bouton) */
static bool allume_par_notif;

static void ecran_eteindre(void) {
    if (!ecran) return;
    ecran = false;
    if (lcd_pret) { lcd_wait_all(); lcd_dormir(); }
    gpio_intr_disable(PIN_TP_INT);               /* le doigt ne réveille pas la montre */
    if (pm_eveil) esp_pm_lock_release(pm_eveil);
    if (pm_max) esp_pm_lock_release(pm_max);
    if (bt_on && bt_veille) ble_diffusion_rapide(false);
    else if (bt_on) { ble_arreter(); bt_lien = false; }   /* veille sobre : pas de Bluetooth écran éteint */
    if (reglages_changes) reglages_ecrire();
    ESP_LOGI(TAG, "écran éteint");
}
static void ecran_allumer(void) {
    if (ecran) return;
    ecran = true;
    if (pm_eveil) esp_pm_lock_acquire(pm_eveil);
    if (pm_max) esp_pm_lock_acquire(pm_max);
    gpio_intr_enable(PIN_TP_INT);
    if (!lcd_pret) { lcd_init(); lcd_pret = true; d_tout(); } else lcd_reveiller();
    lcd_set_brightness((uint8_t)lum_cmd(lum));
    derniere = esp_timer_get_time();
    if (bt_on && bt_veille) ble_diffusion_rapide(true);
    else if (bt_on) ble_demarrer(&RAPPELS);
    a_dessiner = true;
}
static void dire(const char *m) { message = m; message_fin = esp_timer_get_time() + 2500000; a_dessiner = true; }

static void lancer(const char *app) {
    dire("LANCEMENT…");
    dessiner();
    if (reglages_changes) reglages_ecrire();
    if (!app_lancer(app)) dire("APPLICATION ABSENTE");
}

/* ---------- Doigt ---------- */
static void toucher(int x, int y) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    const int etat_appel = appel.etat;
    xSemaphoreGive(verrou);
    if (etat_appel) {
        if (etat_appel == 1 && y > 270 && y < 360) { ble_action(x < CX ? "appel:repondre" : "appel:refuser"); dire(x < CX ? "RÉPONSE…" : "REFUSÉ"); }
        else if (etat_appel == 2 && y > 280 && y < 360) { ble_action("appel:refuser"); dire("RACCROCHÉ"); }
        return;
    }
    if (bandeau >= 0) { if (pg_page != P_NOTIFS) { pg_page = P_NOTIFS; detail = bandeau; } bandeau = -1; a_dessiner = true; return; }
    const int rang = y < RANG_Y0 ? -1 : (y - RANG_Y0) / RANG_PAS;
    switch (pg_page) {
    case P_CADRAN:
        if (nnotifs && y > 300 && y < 350) { pg_page = P_NOTIFS; detail = -1; }
        break;
    case P_NOTIFS:
        xSemaphoreTake(verrou, portMAX_DELAY);
        if (detail >= 0) {
            if (y >= DETAIL_Y && y < 392 && detail < nnotifs) {
                char a[64];
                snprintf(a, sizeof a, "notif:effacer:%.40s", notifs[detail].id);
                ble_action(a);
                notif_retirer(detail);
                notifs_ecrire();
                detail = -1;
            } else if (y >= 398) detail = -1;
            else if (detail < nnotifs) { char a[64]; snprintf(a, sizeof a, "notif:ouvrir:%.40s", notifs[detail].id); ble_action(a); }
        } else if (rang >= 0 && rang < 4 && rang < nnotifs) detail = rang;
        else if (nnotifs && y >= EFFACER_Y && y < EFFACER_Y + 40) {
            for (int k = 0; k < nnotifs; k++) { char a[64]; snprintf(a, sizeof a, "notif:effacer:%.40s", notifs[k].id); ble_action(a); }
            nnotifs = 0;
            notifs_ecrire();
        }
        xSemaphoreGive(verrou);
        break;
    case P_APPS:
        if (dos_ouvert) {
            const int k = y < DOS_Y0 ? -1 : (y - DOS_Y0) / DOS_PAS;
            if (y >= 398) dos_ouvert = false;
            else if (k >= 0 && k < dos_n) {
                FILE *f = fopen("/sd/pcdos/lancer.txt", "w");
                if (f) { fprintf(f, "%s\n", dos_lanceur[k]); fclose(f); }
                lancer("pcdos");
            }
            break;
        }
        if (gb_ouvert && gb_console < 0) {   /* choix de la console */
            const int k = y < APPS_Y0 || (y - APPS_Y0) % APPS_PAS >= APPS_HT ? -1 : (y - APPS_Y0) / APPS_PAS;
            if (y >= 398) gb_ouvert = false;
            else if (k >= 0 && k < CONSOLES_N) { gb_console = k; gb_lire(); }
            break;
        }
        if (gb_ouvert) {
            const int s = y < DOS_Y0 ? -1 : (y - DOS_Y0) / DOS_PAS;
            if (y >= 398) gb_console = -1;   /* RETOUR : le choix de la console */
            else if (s >= 0 && s < GB_VUES && (y - DOS_Y0) % DOS_PAS < DOS_H && gb_off + s < gb_n) gb_lancer(gb_off + s);
            break;
        }
        {
            const int k = y < APPS_Y0 || (y - APPS_Y0) % APPS_PAS >= APPS_HT ? -1 : (y - APPS_Y0) / APPS_PAS;
            if (k == 0) lancer("echoes");
            else if (k == 1) { dos_lire(); dos_ouvert = true; }
            else if (k == 2) { gb_console = -1; gb_ouvert = true; }
            else if (k == 3) { if (!usb_stockage()) dire("STOCKAGE USB INDISPONIBLE"); }
            else if (k == 4) pg_page = P_REGLAGES;
        }
        break;
    case P_REGLAGES: {
        const int k = y < REG_Y0 ? -1 : (y - REG_Y0) / REG_PAS, s = x < CX ? -1 : 1;
        if (k == 0) { teinte = (teinte + s + teintes_nb()) % teintes_nb(); teinte_choisir(teinte); }
        else if (k == 1) { volume += 10 * s; volume = volume < 0 ? 0 : volume > 100 ? 100 : volume; }
        else if (k == 2) { lum += s; lum = lum < 1 ? 1 : lum > 5 ? 5 : lum; lcd_wait_all(); lcd_set_brightness((uint8_t)lum_cmd(lum)); }
        else if (k == 3) {
            static const int V[] = { 15, 30, 60, 120, 300, 0 };
            int i = 0;
            while (i < 5 && V[i] != veille_s) i++;
            veille_s = V[(i + s + 6) % 6];
        } else if (k == 4) {
            bt_on = !bt_on;
            if (bt_on) ble_demarrer(&RAPPELS); else { ble_arreter(); bt_lien = false; }
        } else if (k == 5) bt_veille = !bt_veille;
        else if (k == 6) {   /* à confirmer par un second appui */
            static int64_t oubli_t;
            const int64_t t = esp_timer_get_time();
            if (oubli_t && t - oubli_t < 3000000) { oubli_t = 0; ble_oublier(); dire("TÉLÉPHONE OUBLIÉ"); }
            else { oubli_t = t; dire("TOUCHER ENCORE POUR OUBLIER"); }
        }
        if (k >= 0 && k <= 5) reglages_changes = true;
        break;
    }
    }
    a_dessiner = true;
}
/* Rappels des pages (commun/ui_montre/pages.c) */
static void p_redessiner(void) { a_dessiner = true; }
static void p_activite(int64_t now) { derniere = now; allume_par_notif = false; }
static void p_page_changee(void) { detail = -1; bandeau = -1; dos_ouvert = false; gb_ouvert = false; }
/* Glissé vertical : la liste des jeux du Game Boy, cinq rangées de plus ou de moins */
static void p_vertical(int sens) {
    if (pg_page != P_APPS || !gb_ouvert || gb_console < 0) return;
    gb_off += sens * GB_VUES;
    if (gb_off > gb_n - GB_VUES) gb_off = gb_n - GB_VUES;
    if (gb_off < 0) gb_off = 0;
}
static int p_glisse_permis(void) { return !appel.etat; }
static int p_appui_permis(void) { return bandeau < 0; }
/* Appui long sur le cadran : teinte suivante */
static int p_appui_long(int x, int y) {
    (void)x; (void)y;
    if (pg_page != P_CADRAN || appel.etat) return 0;
    teinte = (teinte + 1) % teintes_nb();
    teinte_choisir(teinte);
    reglages_changes = true;
    return 1;
}
static const pg_def_t PAGES = { P_NB, dessiner, dessiner_page, toucher, p_appui_long, p_page_changee, p_glisse_permis,
                                p_appui_permis, p_vertical, p_redessiner, p_activite };
/* INT du tactile (front descendant) : l'appui n'est pas perdu entre deux tours de boucle, et la tâche se réveille */
static volatile bool tp_irq;
static void IRAM_ATTR tp_isr(void *a) {
    tp_irq = true;
    BaseType_t r = pdFALSE;
    vTaskNotifyGiveFromISR(tache, &r);
    if (r) portYIELD_FROM_ISR();
}

/* Veille profonde (écran éteint sans BT EN VEILLE, pas de PC sur l'USB) : réglages écrits, Bluetooth et écran coupés,
   amplificateur tenu coupé, reset de l'écran tenu (l'AMOLED reste en SLPIN) ; réveil par PWR seulement (BOOT est une
   broche d'amorçage : tenu au réveil, il mettrait la puce en mode téléchargement), puis redémarrage sur le cadran */
static void dormir_profond(void) {
    if (reglages_changes) reglages_ecrire();
    if (bt_on) ble_arreter();
    if (lcd_pret) { lcd_wait_all(); lcd_set_brightness(0); lcd_dormir(); }
    for (int calme = 0; calme < 15; ) { calme = alim_bouton_pwr() || !gpio_get_level(PIN_BOOT) ? 0 : calme + 1; vTaskDelay(pdMS_TO_TICKS(20)); }
    ESP_LOGI(TAG, "veille profonde (réveil par PWR)");
    horloge_garder();
#if CONFIG_ECHOES_AUDIO
    gpio_set_level(CONFIG_ECHOES_PA_GPIO, 0);
    gpio_hold_en(CONFIG_ECHOES_PA_GPIO);
#endif
    gpio_hold_en(PIN_LCD_RST);
    gpio_hold_en(PIN_TP_RST);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_ext0_wakeup(PIN_PWR, 1);
    esp_deep_sleep_start();
}

/* PWR (GPIO3, haut) et BOOT (GPIO0, bas) : appui court = écran allumé / éteint ; PWR rebondit au relâchement */
typedef struct { bool bas; int64_t calme; } bouton_t;
static bouton_t pwr, boot;
static bool bouton(bool appuye, int64_t now, bouton_t *b) {
    if (appuye) { const bool nouveau = !b->bas && now - b->calme >= 300000; b->bas = true; b->calme = now; return nouveau; }
    if (b->bas) b->calme = now;
    b->bas = false;
    return false;
}

void app_main(void) {
    tache = xTaskGetCurrentTaskHandle();
    verrou = xSemaphoreCreateMutex();
    nvs_flash_init();
    board_power_init();
    horloge_init();
    /* Mise sous tension : l'écran attend que l'AXP2101 ait allumé ses régulateurs */
    if (esp_reset_reason() == ESP_RST_POWERON) vTaskDelay(pdMS_TO_TICKS(150));
    /* Sortie de la veille profonde : les broches tenues sont rendues (le reset de l'écran doit bouger) */
    gpio_hold_dis(PIN_LCD_RST);
    gpio_hold_dis(PIN_TP_RST);
#if CONFIG_ECHOES_AUDIO
    gpio_hold_dis(CONFIG_ECHOES_PA_GPIO);
#endif
    gpio_deep_sleep_hold_dis();
#if CONFIG_ECHOES_AUDIO
    /* amplificateur coupé : sa broche flotterait (souffle, courant) ; l'app montre n'a pas de son */
    gpio_config_t pa = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << CONFIG_ECHOES_PA_GPIO };
    gpio_config(&pa);
    gpio_set_level(CONFIG_ECHOES_PA_GPIO, 0);
#endif
    /* Mise en veille d'Echoes ou du PC DOS : démarrage écran éteint, sans rien afficher (l'écran n'est initialisé
       qu'au premier allumage) */
    const bool apres_veille = app_drapeau("veille");
    if (!apres_veille) { lcd_init(); lcd_pret = true; }
    touch_init();
    if (!d_init()) { ESP_LOGE(TAG, "mémoire insuffisante"); return; }
    usb_console();
    if (usb_demande()) { lcd_set_brightness(0xD0); usb_mode(); return; }   /* stockage USB : ne revient pas */
    sd_init();
    reglages_lire();
    teinte_choisir(teinte);
    notifs_lire();
    if (lcd_pret) lcd_set_brightness((uint8_t)lum_cmd(lum));
    pg_init(&PAGES, P_CADRAN);
    gpio_config_t g = { .mode = GPIO_MODE_INPUT, .pin_bit_mask = 1ULL << PIN_BOOT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&g);
    /* Fréquence dynamique et veille légère automatique (le Bluetooth en veille de modem) */
    esp_pm_config_t pm = { .max_freq_mhz = 240, .min_freq_mhz = 40, .light_sleep_enable = true };
    if (esp_pm_configure(&pm) != ESP_OK) ESP_LOGW(TAG, "gestion d'énergie indisponible");
    /* Câble USB branché (5 V vu par l'AXP2101) : pas de veille légère, sinon l'USB du PC décroche ou ne s'établit
       jamais (montre endormie puis branchée) ; CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION couvre le PC déjà connecté */
    esp_pm_lock_handle_t sans_veille = NULL;
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb", &sans_veille);
    /* Écran allumé : pas de veille légère (réveils lents, appuis manqués) et 240 MHz en permanence (des changements de
       fréquence à chaque image ou geste faisaient saccader les glissés) ; écran éteint : 40 MHz et veille légère */
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "vite", &pm_max);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ecran", &pm_eveil);
    if (pm_eveil) esp_pm_lock_acquire(pm_eveil);
    if (pm_max) esp_pm_lock_acquire(pm_max);
    gpio_set_intr_type(PIN_TP_INT, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_TP_INT, tp_isr, NULL);
    gpio_intr_enable(PIN_TP_INT);
    bool usb_tenu = false;
    int64_t usb_t = 0;
    if (bt_on && (!apres_veille || bt_veille)) ble_demarrer(&RAPPELS);
    derniere = esp_timer_get_time();
    pwr.bas = alim_bouton_pwr();   /* PWR qui a allumé la montre : pas un appui */
    if (apres_veille) {   /* écran éteint d'emblée : veille profonde, ou veille légère avec le Bluetooth */
        ecran = true; ecran_eteindre();
        if (!bt_veille && !usb_serial_jtag_is_connected()) dormir_profond();
    }
    ESP_LOGI(TAG, "accueil prêt");
    time_t seconde = 0;
    int64_t batt_t = 0;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        {
            static int64_t mes_t, tour_t;
            if (tour_t && now - tour_t > mes.tour_max) mes.tour_max = now - tour_t;
            tour_t = now;
            if (now - mes_t > 2000000) {
                if (mes.images || mes.lectures)
                    ESP_LOGI(TAG, "2 s : %d images (dessin %lld ms, envoi %lld ms, pire %lld ms), %d lectures tactiles (%lld us), tour max %lld ms, page %d",
                             mes.images, mes.images ? mes.dessin / mes.images / 1000 : 0, mes.images ? mes.envoi / mes.images / 1000 : 0,
                             mes.dessin_max / 1000, pg_lectures, pg_lectures ? pg_lecture_us / pg_lectures : 0, mes.tour_max / 1000, pg_page);
                memset(&mes, 0, sizeof mes);
                pg_lectures = 0; pg_lecture_us = 0;
                mes_t = now;
            }
        }
        /* boutons */
        const bool p = bouton(alim_bouton_pwr(), now, &pwr), b = bouton(!gpio_get_level(PIN_BOOT), now, &boot);
        if (p || b) {
            pg_annuler();
            if (ecran) { ecran_eteindre(); if (!bt_veille && !usb_serial_jtag_is_connected()) dormir_profond(); }
            else ecran_allumer();
            derniere = now;
        }
        if (reveil_demande) { reveil_demande = false; if (!ecran) { ecran_allumer(); allume_par_notif = true; } bandeau_fin = now + 6000000; derniere = now; }
        if (ecran) {
            if (pg_doigt() || tp_irq || touch_int_bas()) { tp_irq = false; pg_tactile(now); }
            if (bandeau >= 0 && now > bandeau_fin) { bandeau = -1; a_dessiner = true; }
            if (message && now > message_fin) { message = NULL; a_dessiner = true; }
            const int lim = allume_par_notif ? 6 : veille_s;
            if (lim > 0 && !appel.etat && now - derniere > (int64_t)lim * 1000000) {
                ecran_eteindre();
                if (!bt_veille && !usb_serial_jtag_is_connected()) dormir_profond();
            }
        }
        /* 5 V de l'USB (chaque seconde) : veille légère interdite tant qu'il est là */
        if (now - usb_t > 1000000) {
            usb_t = now;
            const bool u = alim_usb();
            if (u != usb_tenu && sans_veille) { usb_tenu = u; if (u) esp_pm_lock_acquire(sans_veille); else esp_pm_lock_release(sans_veille); }
        }
        /* batterie (30 s) */
        if (now - batt_t > 30000000 || !batt_t) {
            batt_t = now;
            const int nb = alim_batterie_pct(), nu = alim_usb();
            if (nb != batterie || nu != usb_alim) { batterie = nb; usb_alim = nu; a_dessiner = true; }
            if (bt_on) ble_etat(batterie, usb_alim);
        }
        /* Cadence : gestes et animations sans limite (glisser_vers, doigt lu toutes les 12 ms), cadran à chaque
           seconde (deux-points, arc des secondes), autres pages seulement quand quelque chose change */
        if (ecran) {
            const time_t sec = time(NULL);
            if (pg_page == P_CADRAN && !appel.etat && sec != seconde) a_dessiner = true;
            seconde = sec;
            if (a_dessiner) { a_dessiner = false; dessiner(); }
        }
        /* attente : la tâche dort (veille légère automatique) jusqu'au prochain tour ou un évènement du téléphone */
        int attente = 250;
        if (ecran) {
            attente = pg_doigt() ? 2 : 100;   /* doigt posé : lu sans attendre (images des pages en cache pendant un glissé) */
            if (pg_page == P_CADRAN && !appel.etat) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                const int r = 1000 - (int)(tv.tv_usec / 1000) + 2;   /* jusqu'à la seconde suivante */
                attente = r < 1 ? 1 : r < attente ? r : attente;
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(attente));
    }
}
